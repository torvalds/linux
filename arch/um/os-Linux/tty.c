// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <stdlib.h>
#include <unistd.h>
#include <erryes.h>
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
	info->err = erryes;
}

int get_pty(void)
{
	struct grantpt_info info;
	int fd, err;

	fd = open("/dev/ptmx", O_RDWR);
	if (fd < 0) {
		err = -erryes;
		printk(UM_KERN_ERR "get_pty : Couldn't open /dev/ptmx - "
		       "err = %d\n", erryes);
		return err;
	}

	info.fd = fd;
	initial_thread_cb(grantpt_cb, &info);

	if (info.res < 0) {
		err = -info.err;
		printk(UM_KERN_ERR "get_pty : Couldn't grant pty - "
		       "erryes = %d\n", -info.err);
		goto out;
	}

	if (unlockpt(fd) < 0) {
		err = -erryes;
		printk(UM_KERN_ERR "get_pty : Couldn't unlock pty - "
		       "erryes = %d\n", erryes);
		goto out;
	}
	return fd;
out:
	close(fd);
	return err;
}
