/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdlib.h>
#include <errno.h>
#include "os.h"
#include "user.h"
#include "kern_util.h"

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
	int fd;

	fd = os_open_file("/dev/ptmx", of_rdwr(OPENFLAGS()), 0);
	if(fd < 0){
		printk("get_pty : Couldn't open /dev/ptmx - err = %d\n", -fd);
		return(fd);
	}

	info.fd = fd;
	initial_thread_cb(grantpt_cb, &info);

	if(info.res < 0){
		printk("get_pty : Couldn't grant pty - errno = %d\n", 
		       -info.err);
		return(-1);
	}
	if(unlockpt(fd) < 0){
		printk("get_pty : Couldn't unlock pty - errno = %d\n", errno);
		return(-1);
	}
	return(fd);
}

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
