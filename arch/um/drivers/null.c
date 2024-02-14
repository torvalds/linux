// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{linux.intel,addtoit}.com)
 */

#include <stddef.h>
#include <errno.h>
#include <fcntl.h>
#include "chan_user.h"
#include <os.h>

/* This address is used only as a unique identifier */
static int null_chan;

static void *null_init(char *str, int device, const struct chan_opts *opts)
{
	return &null_chan;
}

static int null_open(int input, int output, int primary, void *d,
		     char **dev_out)
{
	int fd;

	*dev_out = NULL;

	fd = open(DEV_NULL, O_RDWR);
	return (fd < 0) ? -errno : fd;
}

static int null_read(int fd, char *c_out, void *unused)
{
	return -ENODEV;
}

static void null_free(void *data)
{
}

const struct chan_ops null_ops = {
	.type		= "null",
	.init		= null_init,
	.open		= null_open,
	.close		= generic_close,
	.read		= null_read,
	.write		= generic_write,
	.console_write	= generic_console_write,
	.window_size	= generic_window_size,
	.free		= null_free,
	.winch		= 0,
};
