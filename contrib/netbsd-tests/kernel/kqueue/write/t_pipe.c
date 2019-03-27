/* $NetBSD: t_pipe.c,v 1.2 2017/01/13 21:30:41 christos Exp $ */

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
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
__RCSID("$NetBSD: t_pipe.c,v 1.2 2017/01/13 21:30:41 christos Exp $");

#include <sys/event.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <atf-c.h>

#include "h_macros.h"

ATF_TC(pipe1);
ATF_TC_HEAD(pipe1, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Checks EVFILT_WRITE for pipes. This test used to trigger "
	    "problem fixed in rev. 1.5.2.7 of sys/kern/sys_pipe.c");
}
ATF_TC_BODY(pipe1, tc)
{
	struct kevent event[1];
	int fds[2];
	int kq, n;

	RL(pipe(fds));
	RL(kq = kqueue());
	RL(close(fds[0]));

	EV_SET(&event[0], fds[1], EVFILT_WRITE, EV_ADD|EV_ENABLE, 0, 0, 0);
	ATF_REQUIRE_EQ_MSG((n = kevent(kq, event, 1, NULL, 0, NULL)),
	    -1, "got: %d", n);
	ATF_REQUIRE_EQ_MSG(errno, EBADF, "got: %s", strerror(errno));
}

ATF_TC(pipe2);
ATF_TC_HEAD(pipe2, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Checks EVFILT_WRITE for pipes. This test used to trigger problem "
	    "fixed in rev. 1.5.2.9 of sys/kern/sys_pipe.c");
}
ATF_TC_BODY(pipe2, tc)
{
	struct kevent event[1];
	char buffer[128];
	int fds[2];
	int kq, n;
	int status;
	pid_t child;

	RL(pipe(fds));
	RL(kq = kqueue());

	EV_SET(&event[0], fds[1], EVFILT_WRITE, EV_ADD|EV_ENABLE, 0, 0, 0);
	RL(kevent(kq, event, 1, NULL, 0, NULL));

	/* spawn child reader */
	RL(child = fork());
	if (child == 0) {
		int sz = read(fds[0], buffer, 128);
		if (sz > 0)
			(void)printf("pipe: child read '%.*s'\n", sz, buffer);
		exit(sz <= 0);
	}

	RL(n = kevent(kq, NULL, 0, event, 1, NULL));

	(void)printf("kevent num %d flags: %#x, fflags: %#x, data: "
	    "%" PRId64 "\n", n, event[0].flags, event[0].fflags, event[0].data);

	RL(n = write(fds[1], "foo", 3));
	RL(close(fds[1]));

	(void)waitpid(child, &status, 0);
}

ATF_TC(pipe3);
ATF_TC_HEAD(pipe3, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Checks EVFILT_WRITE for pipes. This test used to trigger problem "
	    "fixed in rev. 1.5.2.10 of sys/kern/sys_pipe.c");
}
ATF_TC_BODY(pipe3, tc)
{
	struct kevent event[1];
	int fds[2];
	int kq;

	RL(pipe(fds));
	RL(kq = kqueue());

	EV_SET(&event[0], fds[1], EVFILT_WRITE, EV_ADD|EV_ENABLE, 0, 0, 0);
	RL(kevent(kq, event, 1, NULL, 0, NULL));

	/* close 'read' end first, then 'write' */

	RL(close(fds[0]));
	RL(close(fds[1]));
}


ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, pipe1);
	ATF_TP_ADD_TC(tp, pipe2);
	ATF_TP_ADD_TC(tp, pipe3);

	return atf_no_error();
}
