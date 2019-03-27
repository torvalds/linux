/* $NetBSD: t_proc2.c,v 1.3 2017/01/13 21:30:41 christos Exp $ */

/*-
 * Copyright (c) 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Peter Werner <Peter.Werner@wgsn.com>.
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
__RCSID("$NetBSD: t_proc2.c,v 1.3 2017/01/13 21:30:41 christos Exp $");

#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <err.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <atf-c.h>

#include "h_macros.h"

static void
child_two(void)
{
	_exit(EXIT_SUCCESS);
}

static void
child_one(void)
{
	pid_t pid;
	struct passwd *pwd;
	const char *nam = "nobody";

	pwd = getpwnam(nam);
	if (pwd == NULL)
		err(EXIT_FAILURE, "getpwnam(\"%s\")", nam);

	if ((setuid(pwd->pw_uid)) == -1)
		err(EXIT_FAILURE, "setuid(%d)", pwd->pw_uid);

	pid = fork();
	if (pid == -1)
		err(EXIT_FAILURE, "fork()");

	if (pid == 0)
		child_two();

	_exit(EXIT_SUCCESS);
}

ATF_TC(proc2);
ATF_TC_HEAD(proc2, tc)
{
	atf_tc_set_md_var(tc, "require.user", "root");
	atf_tc_set_md_var(tc, "descr",
	    "Checks EVFILT_PROC for NOTE_FORK|NOTE_TRACK error path problem "
	    "fixed in rev. 1.1.1.1.2.17 of sys/kern/kern_event.c");
}
ATF_TC_BODY(proc2, tc)
{
	pid_t pid = 0;
	int kq, status;
	struct kevent ke;
	struct timespec timeout;

	RL(kq = kqueue());

	timeout.tv_sec = 0;
	timeout.tv_nsec = 0;

	RL(pid = fork());
	if (pid == 0) {
		(void)sleep(1); /* let parent set kevent */
		child_one();
		/* NOTREACHED */
	}

	EV_SET(&ke, (uintptr_t)pid, EVFILT_PROC, EV_ADD, NOTE_FORK|NOTE_TRACK,
	    0, 0);

	RL(kevent(kq, &ke, 1, NULL, 0, &timeout));

	(void)sleep(2);

	ke.ident = 0;
	ke.fflags = 0;
	ke.flags = EV_ENABLE;

	RL(kevent(kq, NULL, 0, &ke, 1, &timeout));
	RL(close(kq));

	RL(waitpid(pid, &status, 0));
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE_EQ(WEXITSTATUS(status), EXIT_SUCCESS);

	/*
	 * we are expecting an error here as we should not have
	 * been able to add a knote to child 2.
	 */
	ATF_REQUIRE(ke.fflags & NOTE_TRACKERR);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, proc2);

	return atf_no_error();
}
