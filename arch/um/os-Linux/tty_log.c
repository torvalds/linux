/*
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com) and
 * geoffrey hing <ghing@net.ohio-state.edu>
 * Licensed under the GPL
 */

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include "init.h"
#include "user.h"
#include "os.h"

#define TTY_LOG_DIR "./"

/* Set early in boot and then unchanged */
static char *tty_log_dir = TTY_LOG_DIR;
static int tty_log_fd = -1;

#define TTY_LOG_OPEN 1
#define TTY_LOG_CLOSE 2
#define TTY_LOG_WRITE 3
#define TTY_LOG_EXEC 4

#define TTY_READ 1
#define TTY_WRITE 2

struct tty_log_buf {
	int what;
	unsigned long tty;
	int len;
	int direction;
	unsigned long sec;
	unsigned long usec;
};

int open_tty_log(void *tty, void *current_tty)
{
	struct timeval tv;
	struct tty_log_buf data;
	char buf[strlen(tty_log_dir) + sizeof("01234567890-01234567\0")];
	int fd;

	gettimeofday(&tv, NULL);
	if(tty_log_fd != -1){
		data = ((struct tty_log_buf) { .what 	= TTY_LOG_OPEN,
					       .tty  = (unsigned long) tty,
					       .len  = sizeof(current_tty),
					       .direction = 0,
					       .sec = tv.tv_sec,
					       .usec = tv.tv_usec } );
		write(tty_log_fd, &data, sizeof(data));
		write(tty_log_fd, &current_tty, data.len);
		return tty_log_fd;
	}

	sprintf(buf, "%s/%0u-%0u", tty_log_dir, (unsigned int) tv.tv_sec,
 		(unsigned int) tv.tv_usec);

	fd = os_open_file(buf, of_append(of_create(of_rdwr(OPENFLAGS()))),
			  0644);
	if(fd < 0){
		printk("open_tty_log : couldn't open '%s', errno = %d\n",
		       buf, -fd);
	}
	return fd;
}

void close_tty_log(int fd, void *tty)
{
	struct tty_log_buf data;
	struct timeval tv;

	if(tty_log_fd != -1){
		gettimeofday(&tv, NULL);
		data = ((struct tty_log_buf) { .what 	= TTY_LOG_CLOSE,
					       .tty  = (unsigned long) tty,
					       .len  = 0,
					       .direction = 0,
					       .sec = tv.tv_sec,
					       .usec = tv.tv_usec } );
		write(tty_log_fd, &data, sizeof(data));
		return;
	}
	os_close_file(fd);
}

static int log_chunk(int fd, const char *buf, int len)
{
	int total = 0, try, missed, n;
	char chunk[64];

	while(len > 0){
		try = (len > sizeof(chunk)) ? sizeof(chunk) : len;
		missed = copy_from_user_proc(chunk, (char *) buf, try);
		try -= missed;
		n = write(fd, chunk, try);
		if(n != try) {
			if(n < 0)
				return -errno;
			return -EIO;
		}
		if(missed != 0)
			return -EFAULT;

		len -= try;
		total += try;
		buf += try;
	}

	return total;
}

int write_tty_log(int fd, const char *buf, int len, void *tty, int is_read)
{
	struct timeval tv;
	struct tty_log_buf data;
	int direction;

	if(fd == tty_log_fd){
		gettimeofday(&tv, NULL);
		direction = is_read ? TTY_READ : TTY_WRITE;
		data = ((struct tty_log_buf) { .what 	= TTY_LOG_WRITE,
					       .tty  = (unsigned long) tty,
					       .len  = len,
					       .direction = direction,
					       .sec = tv.tv_sec,
					       .usec = tv.tv_usec } );
		write(tty_log_fd, &data, sizeof(data));
	}

	return log_chunk(fd, buf, len);
}

void log_exec(char **argv, void *tty)
{
	struct timeval tv;
	struct tty_log_buf data;
	char **ptr,*arg;
	int len;

	if(tty_log_fd == -1) return;

	gettimeofday(&tv, NULL);

	len = 0;
	for(ptr = argv; ; ptr++){
		if(copy_from_user_proc(&arg, ptr, sizeof(arg)))
			return;
		if(arg == NULL) break;
		len += strlen_user_proc(arg);
	}

	data = ((struct tty_log_buf) { .what 	= TTY_LOG_EXEC,
				       .tty  = (unsigned long) tty,
				       .len  = len,
				       .direction = 0,
				       .sec = tv.tv_sec,
				       .usec = tv.tv_usec } );
	write(tty_log_fd, &data, sizeof(data));

	for(ptr = argv; ; ptr++){
		if(copy_from_user_proc(&arg, ptr, sizeof(arg)))
			return;
		if(arg == NULL) break;
		log_chunk(tty_log_fd, arg, strlen_user_proc(arg));
	}
}

extern void register_tty_logger(int (*opener)(void *, void *),
				int (*writer)(int, const char *, int,
					      void *, int),
				void (*closer)(int, void *));

static int register_logger(void)
{
	register_tty_logger(open_tty_log, write_tty_log, close_tty_log);
	return 0;
}

__uml_initcall(register_logger);

static int __init set_tty_log_dir(char *name, int *add)
{
	tty_log_dir = name;
	return 0;
}

__uml_setup("tty_log_dir=", set_tty_log_dir,
"tty_log_dir=<directory>\n"
"    This is used to specify the directory where the logs of all pty\n"
"    data from this UML machine will be written.\n\n"
);

static int __init set_tty_log_fd(char *name, int *add)
{
	char *end;

	tty_log_fd = strtoul(name, &end, 0);
	if((*end != '\0') || (end == name)){
		printf("set_tty_log_fd - strtoul failed on '%s'\n", name);
		tty_log_fd = -1;
	}

	*add = 0;
	return 0;
}

__uml_setup("tty_log_fd=", set_tty_log_fd,
"tty_log_fd=<fd>\n"
"    This is used to specify a preconfigured file descriptor to which all\n"
"    tty data will be written.  Preconfigure the descriptor with something\n"
"    like '10>tty_log tty_log_fd=10'.\n\n"
);
