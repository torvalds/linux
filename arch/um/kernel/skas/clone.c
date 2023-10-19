// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015 Thomas Meyer (thomas@m3y3r.de)
 * Copyright (C) 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <signal.h>
#include <sched.h>
#include <asm/unistd.h>
#include <sys/time.h>
#include <as-layout.h>
#include <ptrace_user.h>
#include <stub-data.h>
#include <sysdep/stub.h>

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
	struct stub_data *data = get_stub_page();
	long err;

	err = stub_syscall2(__NR_clone, CLONE_PARENT | CLONE_FILES | SIGCHLD,
			    (unsigned long)data + UM_KERN_PAGE_SIZE / 2);
	if (err) {
		data->parent_err = err;
		goto done;
	}

	err = stub_syscall4(__NR_ptrace, PTRACE_TRACEME, 0, 0, 0);
	if (err) {
		data->child_err = err;
		goto done;
	}

	remap_stack_and_trap();

 done:
	trap_myself();
}
