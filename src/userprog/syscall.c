#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include <devices/shutdown.h>
#include <filesys/filesys.h>

struct lock file_lock;

static void syscall_handler (struct intr_frame *);
void check_address(void *addr);
void get_argument(void *esp,uint32_t *arg,int count);
void halt(void);
void exit(int);
bool create(const char*,unsigned);
bool remove(const char*);
tid_t exec(const char* cmd_line);
unsigned tell(int fd);

int wait(tid_t tid);
syscall_init (void) 
{
	lock_init(&file_lock);
	intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
	uint32_t syscall_number=*(uint32_t *)f->esp;
	uint32_t* arg;
	uint32_t i=0;
	uint32_t arg_count=0;
	check_address(f->esp);
	switch(syscall_number)
	{
	case SYS_HALT:
		arg_count=0;
		halt();
		break;
	case SYS_EXIT:
		arg_count=1;
		arg=(uint32_t*)malloc(sizeof(uint32_t)*arg_count);
		get_argument(f->esp,arg,arg_count);
		exit(arg[0]);
		break;
	case SYS_CREATE:
		arg_count=2;
		arg=(uint32_t*)malloc(sizeof(uint32_t)*arg_count);
		get_argument(f->esp,arg,arg_count);
		check_address(arg[0]);
		f->eax=create((char*)arg[0],arg[1]);
		break;
	case SYS_REMOVE:
		arg_count=1;
		arg=(uint32_t*)malloc(sizeof(uint32_t)*arg_count);
		get_argument(f->esp,arg,arg_count);
		check_address(arg[0]);
		f->eax=remove((char*)arg[0]);
		break;
				case SYS_OPEN:
					arg_count=1;
					arg=(uint32_t*)malloc(sizeof(uint32_t)*arg_count);
					get_argument(f->esp,arg,arg_count);
					check_address(arg[0]);
					f->eax=open((char*)arg[0]);
					break;
				case SYS_FILESIZE:
					arg_count=1;
					arg=(uint32_t*)malloc(sizeof(uint32_t)*arg_count);
					get_argument(f->esp,arg,arg_count);
					f->eax=filesize(arg[0]);
					break;
				case SYS_READ:
					arg_count=3;
					arg=(uint32_t*)malloc(sizeof(uint32_t)*arg_count);
					get_argument(f->esp,arg,arg_count);
					check_address(arg[1]);
					f->eax=read(arg[0],(void*)arg[1],arg[2]);
					break;
				case SYS_WRITE:
					arg_count=3;
					arg=(uint32_t*)malloc(sizeof(uint32_t)*arg_count);
					get_argument(f->esp,arg,arg_count);
					check_address(arg[1]);
					f->eax=write(arg[0],(void*)arg[1],arg[2]);
					break;
				case SYS_SEEK:
					arg_count=2;
					arg=(uint32_t*)malloc(sizeof(uint32_t)*arg_count);
					get_argument(f->esp,arg,arg_count);
				seek(arg[0],arg[1]);
					break;
				case SYS_TELL:
					arg_count=1;
					arg=(uint32_t*)malloc(sizeof(uint32_t)*arg_count);
					get_argument(f->esp,arg,arg_count);
					f->eax=tell(arg[0]);
					break;
				case SYS_CLOSE:
					arg_count=1;
					arg=(uint32_t*)malloc(sizeof(uint32_t)*arg_count);
					get_argument(f->esp,arg,arg_count);
					close(arg[0]);
					break;
	case SYS_EXEC:
		arg_count=1;
		arg=(uint32_t*)malloc(sizeof(uint32_t)*arg_count);
		get_argument(f->esp,arg,arg_count);
		f->eax = exec((const char*)arg[0]);
		break;
	case SYS_WAIT:
		arg_count=1;
		arg=(uint32_t*)malloc(sizeof(uint32_t)*arg_count);
		get_argument(f->esp,arg,arg_count);
		f->eax = wait(arg[0]);
		break;
	}
	if(arg_count)
		free(arg);
	// thread_exit ();
}

void check_address(void *addr)
{
	uint32_t address=addr;
	if(address<0x08048000||address>=0xc0000000)
	{
		exit(-1);
	}
}

void get_argument(void *esp,uint32_t *arg,int count)
{
	int i;
	uint32_t ptr;
	for(i=1;i<count+1;i++)
	{
		ptr=(uint32_t*)esp+i;
		check_address(ptr);
		arg[i-1]=((uint32_t*)esp)[i];
	}
}

void halt()
{
	shutdown_power_off();
}

void exit(int status)
{
	struct thread* cur=thread_current();
	cur->exit_status=status;
	printf("%s: exit(%d)\n",cur->name,status);
	thread_exit();
}

bool create(const char* file,unsigned initial_size)
{
	return filesys_create(file,initial_size);
}

bool remove(const char* file)
{
	return filesys_remove(file);
}
tid_t exec(const char* cmd_line)
{
	tid_t pid=process_execute(cmd_line);
	struct thread* cp=get_child_process(pid);

	sema_down(&cp->sema_load);
	if(!cp->load)
	{
		return -1;
	}
	return pid;
}
int wait(tid_t tid)
{
	return process_wait(tid);
}
int open(const char *file_name)
{
	struct file* f=filesys_open(file_name);
	if(f==NULL)
	{
		//printf("File(%s) doesn't exist\n",file_name);
		return -1;
	}
	return process_add_file(f);
}

int filesize(int fd)
{
	struct file *f=process_get_file(fd);
	int fs=0;
	if(f==NULL)
	{
		//printf("process_get_file err\n");
		return -1;
	}
	fs=file_length(f);
	return fs;
}

int read(int fd,void* buffer,unsigned size)
{
	int buffer_size=0;
	struct file* f;
	uint8_t ch,*ch_buffer;
	int result=0;
	if(fd==0)
	{
		ch_buffer=(uint8_t*)buffer;
		while(ch=input_getc())
			ch_buffer[buffer_size++]=ch;
		return buffer_size;
	}
	else if((f=thread_current()->fd_arr[fd])!=NULL)
	{
		lock_acquire(&file_lock);
		result=file_read(f,buffer,size);
		lock_release(&file_lock);
	//	printf("read : fd=%d, result=%d, size=%d\n",fd,result,size);
		return result;
	}
	return -1;
}


int write(int fd,void* buffer,unsigned size)
{
	struct file *f;
	uint8_t *ch_buffer;
	int result=0;
	//printf("syscall:write called fd: %d, size: %d\n",fd,size);
	if(fd==1)
	{
		ch_buffer=(uint8_t*)buffer;
		putbuf(ch_buffer,size);
		//printf("call ok\n");
		return size;
	}
	else if((f=thread_current()->fd_arr[fd])!=NULL)
	{
		lock_acquire(&file_lock);
		result=file_write(f,buffer,size);
		lock_release(&file_lock);
		return result;
	}
	return -1;
}

void seek(int fd,unsigned position)
{
	struct file* f=thread_current()->fd_arr[fd];
	int pos=0;
	if(f==NULL)
	{
		//printf("seek error: File is NULL\n");
		return;
	}
	//pos=tell(fd);
	file_seek(f,pos+position);
}

unsigned tell(int fd)
{
	struct file* f=thread_current()->fd_arr[fd];
	if(f==NULL)
	{
		//printf("tell error: File is NULL\n");
		return 0;
	}
	return file_tell(f);
}

void close(int fd)
{
	struct file* f=thread_current()->fd_arr[fd];
	file_close(f);
	thread_current()->fd_arr[fd]=NULL;
}
