/* 
 * Copyright (C) 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include "user.h"
#include "user_util.h"
#include "chan_user.h"

struct fd_chan {
	int fd;
	int raw;
	struct termios tt;
	char str[sizeof("1234567890\0")];
};

static void *fd_init(char *str, int device, struct chan_opts *opts)
{
	struct fd_chan *data;
	char *end;
	int n;

	if(*str != ':'){
		printk("fd_init : channel type 'fd' must specify a file "
		       "descriptor\n");
		return(NULL);
	}
	str++;
	n = strtoul(str, &end, 0);
	if((*end != '\0') || (end == str)){
		printk("fd_init : couldn't parse file descriptor '%s'\n", str);
		return(NULL);
	}
	data = um_kmalloc(sizeof(*data));
	if(data == NULL) return(NULL);
	*data = ((struct fd_chan) { .fd  	= n,
				    .raw  	= opts->raw });
	return(data);
}

static int fd_open(int input, int output, int primary, void *d, char **dev_out)
{
	struct fd_chan *data = d;
	int err;

	if(data->raw && isatty(data->fd)){
		CATCH_EINTR(err = tcgetattr(data->fd, &data->tt));
		if(err)
			return(err);

		err = raw(data->fd);
		if(err)
			return(err);
	}
	sprintf(data->str, "%d", data->fd);
	*dev_out = data->str;
	return(data->fd);
}

static void fd_close(int fd, void *d)
{
	struct fd_chan *data = d;
	int err;

	if(data->raw && isatty(fd)){
		CATCH_EINTR(err = tcsetattr(fd, TCSAFLUSH, &data->tt));
		if(err)
			printk("Failed to restore terminal state - "
			       "errno = %d\n", -err);
		data->raw = 0;
	}
}

struct chan_ops fd_ops = {
	.type		= "fd",
	.init		= fd_init,
	.open		= fd_open,
	.close		= fd_close,
	.read		= generic_read,
	.write		= generic_write,
	.console_write	= generic_console_write,
	.window_size	= generic_window_size,
	.free		= generic_free,
	.winch		= 1,
};

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
