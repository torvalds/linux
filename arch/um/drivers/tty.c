// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2001 - 2007 Jeff Dike (jdike@{linux.intel,addtoit}.com)
 */

#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include "chan_user.h"
#include <os.h>
#include <um_malloc.h>

struct tty_chan {
	char *dev;
	int raw;
	struct termios tt;
};

static void *tty_chan_init(char *str, int device, const struct chan_opts *opts)
{
	struct tty_chan *data;

	if (*str != ':') {
		printk(UM_KERN_ERR "tty_init : channel type 'tty' must specify "
		       "a device\n");
		return NULL;
	}
	str++;

	data = uml_kmalloc(sizeof(*data), UM_GFP_KERNEL);
	if (data == NULL)
		return NULL;
	*data = ((struct tty_chan) { .dev 	= str,
				     .raw 	= opts->raw });

	return data;
}

static int tty_open(int input, int output, int primary, void *d,
		    char **dev_out)
{
	struct tty_chan *data = d;
	int fd, err, mode = 0;

	if (input && output)
		mode = O_RDWR;
	else if (input)
		mode = O_RDONLY;
	else if (output)
		mode = O_WRONLY;

	fd = open(data->dev, mode);
	if (fd < 0)
		return -errno;

	if (data->raw) {
		CATCH_EINTR(err = tcgetattr(fd, &data->tt));
		if (err)
			return err;

		err = raw(fd);
		if (err)
			return err;
	}

	*dev_out = data->dev;
	return fd;
}

const struct chan_ops tty_ops = {
	.type		= "tty",
	.init		= tty_chan_init,
	.open		= tty_open,
	.close		= generic_close,
	.read		= generic_read,
	.write		= generic_write,
	.console_write	= generic_console_write,
	.window_size	= generic_window_size,
	.free		= generic_free,
	.winch		= 0,
};
