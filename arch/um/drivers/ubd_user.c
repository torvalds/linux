/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Copyright (C) 2001 Ridgerun,Inc (glonnon@ridgerun.com)
 * Licensed under the GPL
 */

#include <stddef.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/param.h>
#include "asm/types.h"
#include "kern_util.h"
#include "user.h"
#include "ubd_user.h"
#include "os.h"
#include "cow.h"

#include <endian.h>
#include <byteswap.h>

void ignore_sigwinch_sig(void)
{
	signal(SIGWINCH, SIG_IGN);
}

int start_io_thread(unsigned long sp, int *fd_out)
{
	int pid, fds[2], err;

	err = os_pipe(fds, 1, 1);
	if(err < 0){
		printk("start_io_thread - os_pipe failed, err = %d\n", -err);
		goto out;
	}

	kernel_fd = fds[0];
	*fd_out = fds[1];

	pid = clone(io_thread, (void *) sp, CLONE_FILES | CLONE_VM | SIGCHLD,
		    NULL);
	if(pid < 0){
		err = -errno;
		printk("start_io_thread - clone failed : errno = %d\n", errno);
		goto out_close;
	}

	return(pid);

 out_close:
	os_close_file(fds[0]);
	os_close_file(fds[1]);
	kernel_fd = -1;
	*fd_out = -1;
 out:
	return err;
}
