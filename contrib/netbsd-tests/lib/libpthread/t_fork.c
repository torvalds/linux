/* $NetBSD: t_fork.c,v 1.2 2017/01/16 16:28:27 christos Exp $ */

/*
 * Copyright (c) 2008 The NetBSD Foundation, Inc.
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
__RCSID("$NetBSD: t_fork.c,v 1.2 2017/01/16 16:28:27 christos Exp $");

/*
 * Written by Love Hörnquist Åstrand <lha@NetBSD.org>, March 2003.
 * Public domain.
 */

#include <sys/types.h>
#include <sys/wait.h>

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

#include "h_common.h"

static pid_t parent;
static int thread_survived = 0;

static void *
print_pid(void *arg)
{
	sleep(3);

	thread_survived = 1;
	if (parent != getpid()) {
		_exit(1);
	}
	return NULL;
}

ATF_TC(fork);
ATF_TC_HEAD(fork, tc)
{
	atf_tc_set_md_var(tc, "descr",
		"Checks that child process doesn't get threads");
}
ATF_TC_BODY(fork, tc)
{
	pthread_t p;
	pid_t fork_pid;

	parent = getpid();

	PTHREAD_REQUIRE(pthread_create(&p, NULL, print_pid, NULL));

	fork_pid = fork();
	ATF_REQUIRE(fork_pid != -1);

	if (fork_pid) {
		int status;

		PTHREAD_REQUIRE(pthread_join(p, NULL));
		ATF_REQUIRE_MSG(thread_survived, "thread did not survive in parent");

		waitpid(fork_pid, &status, 0);
		ATF_REQUIRE_MSG(WIFEXITED(status), "child died wrongly");
		ATF_REQUIRE_EQ_MSG(WEXITSTATUS(status), 0, "thread survived in child");
	} else {
		sleep(5);
		_exit(thread_survived ? 1 : 0);
	}
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, fork);

	return atf_no_error();
}
