/*
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <kern_util.h>
#include <os.h>

struct grantpt_info {
	int fd;
	int res;
	int err;
};

static void grantpt_cb(void *arg)
{
	struct grantpt_info *info = arg;

	info->res = grantpt(info->fd);
	info->err = errno;
}

int get_pty(void)
{
	struct grantpt_info info;
	int fd, err;

	fd = open("/dev/ptmx", O_RDWR);
	if (fd < 0) {
		err = -errno;
		printk(UM_KERN_ERR "get_pty : Couldn't open /dev/ptmx - "
		       "err = %d\n", errno);
		return err;
	}

	info.fd = fd;
	initial_thread_cb(grantpt_cb, &info);

	if (info.res < 0) {
		err = -info.err;
		printk(UM_KERN_ERR "get_pty : Couldn't grant pty - "
		       "errno = %d\n", -info.err);
		goto out;
	}

	if (unlockpt(fd) < 0) {
		err = -errno;
		printk(UM_KERN_ERR "get_pty : Couldn't unlock pty - "
		       "errno = %d\n", errno);
		goto out;
	}
	return fd;
out:
	close(fd);
	return err;
}
