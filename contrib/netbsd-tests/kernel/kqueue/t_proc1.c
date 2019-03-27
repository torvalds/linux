/* $NetBSD: t_proc1.c,v 1.3 2017/01/13 21:30:41 christos Exp $ */

/*-
 * Copyright (c) 2002, 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Luke Mewburn and Jaromir Dolecek.
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
__RCSID("$NetBSD: t_proc1.c,v 1.3 2017/01/13 21:30:41 christos Exp $");

/*
 * this also used to trigger problem fixed in
 * rev. 1.1.1.1.2.13 of sys/kern/kern_event.c
 */

#include <sys/param.h>
#include <sys/event.h>
#include <sys/wait.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>

#include <atf-c.h>

#include "h_macros.h"

static int
child(void)
{
	pid_t ch;
	int status;
	char *argv[] = { NULL, NULL };
	char *envp[] = { NULL, NULL };

	if ((argv[0] = strdup("true")) == NULL)
		err(EXIT_FAILURE, "strdup(\"true\")");

	if ((envp[0] = strdup("FOO=BAZ")) == NULL)
		err(EXIT_FAILURE, "strdup(\"FOO=BAZ\")");

	/* Ensure parent is ready */
	(void)sleep(2);

	/* Do fork */
	switch (ch = fork()) {
	case -1:
		return EXIT_FAILURE;
		/* NOTREACHED */
	case 0:
		return EXIT_SUCCESS;
		/* NOTREACHED */
	default:
		wait(&status);
		break;
	}

	/* Exec */
	execve("/usr/bin/true", argv, envp);

	/* NOTREACHED */
	return EXIT_FAILURE;
}

ATF_TC(proc1);
ATF_TC_HEAD(proc1, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks EVFILT_PROC");
}
ATF_TC_BODY(proc1, tc)
{
	struct kevent event[1];
	pid_t pid;
	int kq, status;
	u_int want;

	RL(kq = kqueue());

	/* fork a child for doing the events */
	RL(pid = fork());
	if (pid == 0) {
		_exit(child());
		/* NOTREACHED */
	}

	(void)sleep(1); /* give child some time to come up */

	event[0].ident = (uintptr_t)pid;
	event[0].filter = EVFILT_PROC;
	event[0].flags = EV_ADD | EV_ENABLE;
	event[0].fflags = NOTE_EXIT | NOTE_FORK | NOTE_EXEC; /* | NOTE_TRACK;*/
	want = NOTE_EXIT | NOTE_FORK | NOTE_EXEC;

	RL(kevent(kq, event, 1, NULL, 0, NULL));

	/* wait until we get all events we want */
	while (want) {
		RL(kevent(kq, NULL, 0, event, 1, NULL));
		printf("%ld:", (long)event[0].ident);

		if (event[0].fflags & NOTE_EXIT) {
			want &= ~NOTE_EXIT;
			printf(" NOTE_EXIT");
		}
		if (event[0].fflags & NOTE_EXEC) {
			want &= ~NOTE_EXEC;
			printf(" NOTE_EXEC");
		}
		if (event[0].fflags & NOTE_FORK) {
			want &= ~NOTE_FORK;
			printf(" NOTE_FORK");
		}
		if (event[0].fflags & NOTE_CHILD)
			printf(" NOTE_CHILD, parent = %" PRId64, event[0].data);

		printf("\n");
	}

	(void)waitpid(pid, &status, 0);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, proc1);

	return atf_no_error();
}
