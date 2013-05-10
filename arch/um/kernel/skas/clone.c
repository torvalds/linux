/*
 * Copyright (C) 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <signal.h>
#include <sched.h>
#include <asm/unistd.h>
#include <sys/time.h>
#include "as-layout.h"
#include "kern_constants.h"
#include "ptrace_user.h"
#include "stub-data.h"
#include "sysdep/stub.h"

/*
 * This is in a separate file because it needs to be compiled with any
 * extraneous gcc flags (-pg, -fprofile-arcs, -ftest-coverage) disabled
 *
 * Use UM_KERN_PAGE_SIZE instead of PAGE_SIZE because that calls getpagesize
 * on some systems.
 */

void __attribute__ ((__section__ (".__syscall_stub")))
stub_clone_handler(void)
{
	struct stub_data *data = (struct stub_data *) STUB_DATA;
	long err;

	err = stub_syscall2(__NR_clone, CLONE_PARENT | CLONE_FILES | SIGCHLD,
			    STUB_DATA + UM_KERN_PAGE_SIZE / 2 - sizeof(void *));
	if (err != 0)
		goto out;

	err = stub_syscall4(__NR_ptrace, PTRACE_TRACEME, 0, 0, 0);
	if (err)
		goto out;

	err = stub_syscall3(__NR_setitimer, ITIMER_VIRTUAL,
			    (long) &data->timer, 0);
	if (err)
		goto out;

	remap_stack(data->fd, data->offset);
	goto done;

 out:
	/*
	 * save current result.
	 * Parent: pid;
	 * child: retcode of mmap already saved and it jumps around this
	 * assignment
	 */
	data->err = err;
 done:
	trap_myself();
}
