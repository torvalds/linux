/* $NetBSD: t_clone.c,v 1.3 2011/12/12 20:55:44 joerg Exp $ */

/*-
 * Copyright (c) 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 2008\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_clone.c,v 1.3 2011/12/12 20:55:44 joerg Exp $");

#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

#define	STACKSIZE	(8 * 1024)
#define	FROBVAL		41973
#define	CHILDEXIT	0xa5

static int
dummy(void *arg)
{

	return 0;
}

static int
clone_func(void *arg)
{
	long *frobp = arg, diff;

	printf("child: stack ~= %p, frobme = %p\n", &frobp, frobp);
	fflush(stdout);

	if (frobp[0] != getppid())
		return 1;

	if (frobp[0] == getpid())
		return 2;

	diff = labs(frobp[1] - (long) &frobp);

	if (diff > 1024)
		return 3;

	frobp[1] = FROBVAL;

	return (CHILDEXIT);
}

ATF_TC(clone_basic);
ATF_TC_HEAD(clone_basic, tc)
{

	atf_tc_set_md_var(tc, "descr", "Checks clone(2)");
}

ATF_TC_BODY(clone_basic, tc)
{
	sigset_t mask;
	void *allocstack, *stack;
	pid_t pid;
	volatile long frobme[2];
	int stat;

	allocstack = mmap(NULL, STACKSIZE, PROT_READ|PROT_WRITE|PROT_EXEC,
	    MAP_PRIVATE|MAP_ANON, -1, (off_t) 0);

	ATF_REQUIRE_ERRNO(errno, allocstack != MAP_FAILED);

	stack = allocstack;
#ifndef __MACHINE_STACK_GROWS_UP
	stack = (char *)stack + STACKSIZE;
#endif

	printf("parent: stack = %p, frobme = %p\n", stack, frobme);
	fflush(stdout);

	frobme[0] = (long)getpid();
	frobme[1] = (long)stack;

	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);

	ATF_REQUIRE_ERRNO(errno, sigprocmask(SIG_BLOCK, &mask, NULL) != -1);

	switch (pid = __clone(clone_func, stack,
	    CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|SIGUSR1,
	    __UNVOLATILE(frobme))) {
	case 0:
		atf_tc_fail("clone() returned 0");
		/*NOTREACHED*/
	case -1:
		atf_tc_fail("clone() failed: %s", strerror(errno));
		/*NOTREACHED*/
	default:
		while (waitpid(pid, &stat, __WCLONE) != pid)
			continue;
	}

	ATF_REQUIRE_MSG(WIFEXITED(stat) != 0, "child didn't exit");

	printf("parent: childexit = 0x%x, frobme = %ld\n",
	    WEXITSTATUS(stat), frobme[1]);

	switch (WEXITSTATUS(stat)) {
	case CHILDEXIT:
		ATF_REQUIRE_EQ(frobme[1], FROBVAL);
		break;
	case 1:
		atf_tc_fail("child: argument does not contain parent's pid");
		/*NOTREACHED*/
	case 2:
		atf_tc_fail("child: called in parent's pid");
		/*NOTREACHED*/
	case 3:
		atf_tc_fail("child: called with bad stack");
		/*NOTREACHED*/
	default:
		atf_tc_fail("child returned unknown code: %d",
		    WEXITSTATUS(stat));
		/*NOTREACHED*/
	}

	ATF_REQUIRE_ERRNO(errno, munmap(allocstack, STACKSIZE) != -1);
}

ATF_TC(clone_null_stack);
ATF_TC_HEAD(clone_null_stack, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Checks that clone(2) fails when stack pointer is NULL");
}

ATF_TC_BODY(clone_null_stack, tc)
{
	int rv;

	rv = __clone(dummy, NULL,
	    CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|SIGCHLD, NULL);

	ATF_REQUIRE_EQ(rv, -1);
	ATF_REQUIRE_EQ(errno, EINVAL);
}

ATF_TC(clone_null_func);
ATF_TC_HEAD(clone_null_func, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Checks that clone(2) fails when function pointer is NULL");
}

ATF_TC_BODY(clone_null_func, tc)
{
	void *allocstack, *stack;
	int rv;

	allocstack = mmap(NULL, STACKSIZE, PROT_READ|PROT_WRITE|PROT_EXEC,
	    MAP_PRIVATE|MAP_ANON, -1, (off_t) 0);
	ATF_REQUIRE_ERRNO(errno, allocstack != MAP_FAILED);
	stack = allocstack;
#ifndef __MACHINE_STACK_GROWS_UP
	stack = (char *)stack + STACKSIZE;
#endif

	errno = 0;
	rv = __clone(0, stack,
	    CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|SIGCHLD, NULL);

	ATF_REQUIRE_EQ(rv, -1);
	ATF_REQUIRE_EQ(errno, EINVAL);

	ATF_REQUIRE_ERRNO(errno, munmap(allocstack, STACKSIZE) != -1);
}

ATF_TC(clone_out_of_proc);
ATF_TC_HEAD(clone_out_of_proc, tc)
{

	atf_tc_set_md_var(tc, "descr",
		"Checks that clone(2) fails when running out of processes");
	atf_tc_set_md_var(tc, "require.user", "unprivileged");
}

ATF_TC_BODY(clone_out_of_proc, tc)
{
	struct rlimit rl;
	int rv;

	ATF_REQUIRE_ERRNO(errno, getrlimit(RLIMIT_NPROC, &rl) != -1);

	rl.rlim_cur = 0;
	rl.rlim_max = 0;

	ATF_REQUIRE_ERRNO(errno, setrlimit(RLIMIT_NPROC, &rl) != -1);

	errno = 0;
	rv = __clone(dummy, malloc(10240),
	    CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|SIGCHLD, (void *)&rl);

	ATF_REQUIRE_EQ(rv, -1);
	ATF_REQUIRE_EQ(errno, EAGAIN);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, clone_basic);
	ATF_TP_ADD_TC(tp, clone_null_stack);
	ATF_TP_ADD_TC(tp, clone_null_func);
	ATF_TP_ADD_TC(tp, clone_out_of_proc);

	return atf_no_error();
}
