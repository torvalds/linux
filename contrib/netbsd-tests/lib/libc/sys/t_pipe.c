/* $NetBSD: t_pipe.c,v 1.5 2017/01/13 21:30:41 christos Exp $ */

/*-
 * Copyright (c) 2001, 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
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
__RCSID("$NetBSD: t_pipe.c,v 1.5 2017/01/13 21:30:41 christos Exp $");

#include <sys/types.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <atf-c.h>

#include "h_macros.h"

static pid_t pid;
static int nsiginfo = 0;

/*
 * This is used for both parent and child. Handle parent's SIGALRM,
 * the childs SIGINFO doesn't need anything.
 */
static void
sighand(int sig)
{
	if (sig == SIGALRM) {
		kill(pid, SIGINFO);
	}
	if (sig == SIGINFO) {
		nsiginfo++;
	}
}

ATF_TC(pipe_restart);
ATF_TC_HEAD(pipe_restart, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks that writing to pipe "
	    "works correctly after being interrupted and restarted "
	    "(kern/14087)");
}

ATF_TC_BODY(pipe_restart, tc)
{
	int pp[2], st;
	ssize_t sz, todo, done;
	char *f;
	sigset_t asigset, osigset, emptysigset;

	/* Initialise signal masks */
	RL(sigemptyset(&emptysigset));
	RL(sigemptyset(&asigset));
	RL(sigaddset(&asigset, SIGINFO));

	/* Register signal handlers for both read and writer */
	REQUIRE_LIBC(signal(SIGINFO, sighand), SIG_ERR);
	REQUIRE_LIBC(signal(SIGALRM, sighand), SIG_ERR);

	todo = 2 * 1024 * 1024;
	REQUIRE_LIBC(f = malloc(todo), NULL);

	RL(pipe(pp));

	RL(pid = fork());
	if (pid == 0) {
		/* child */
		RL(close(pp[1]));

		/* Do inital write. This should succeed, make
		 * the other side do partial write and wait for us to pick
		 * rest up.
		 */
		RL(done = read(pp[0], f, 128 * 1024));

		/* Wait until parent is alarmed and awakens us */
		RL(sigprocmask(SIG_BLOCK, &asigset, &osigset));
		while (nsiginfo == 0) {
			if (sigsuspend(&emptysigset) != -1 || errno != EINTR)
				atf_tc_fail("sigsuspend(&emptysigset): %s",
				    strerror(errno));
		}
		RL(sigprocmask(SIG_SETMASK, &osigset, NULL));

		/* Read all what parent wants to give us */
		while((sz = read(pp[0], f, 1024 * 1024)) > 0)
			done += sz;

		/*
		 * Exit with 1 if number of bytes read doesn't match
		 * number of expected bytes
		 */
		printf("Read:     %#zx\n", (size_t)done);
		printf("Expected: %#zx\n", (size_t)todo);

		exit(done != todo);

		/* NOTREACHED */
	} else {
		RL(close(pp[0]));

		/*
		 * Arrange for alarm after two seconds. Since we have
		 * handler setup for SIGARLM, the write(2) call should
		 * be restarted internally by kernel.
		 */
		(void)alarm(2);

		/* We write exactly 'todo' bytes. The very first write(2)
		 * should partially succeed, block and eventually
		 * be restarted by kernel
		 */
		while(todo > 0 && ((sz = write(pp[1], f, todo)) > 0))
			todo -= sz;

		/* Close the pipe, so that child would stop reading */
		RL(close(pp[1]));

		/* And pickup child's exit status */
		RL(waitpid(pid, &st, 0));

		ATF_REQUIRE_EQ(WEXITSTATUS(st), 0);
	}
	free(f);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, pipe_restart);

	return atf_no_error();
}
