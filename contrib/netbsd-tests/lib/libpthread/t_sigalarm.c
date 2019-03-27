/* $NetBSD: t_sigalarm.c,v 1.1 2010/07/16 15:42:53 jmmv Exp $ */

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
__RCSID("$NetBSD: t_sigalarm.c,v 1.1 2010/07/16 15:42:53 jmmv Exp $");

#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>

#include <atf-c.h>

#include "h_common.h"

int alarm_set;

#ifdef SA_SIGINFO
static void
alarm_handler(int signo, siginfo_t *si, void *ctx)
{
	ATF_REQUIRE_EQ_MSG(si->si_signo, signo, "Received unexpected signal");
	alarm_set = 1;
}
#else
static void
alarm_handler(int signo)
{
	ATF_REQUIRE_EQ_MSG(SIGALRM, signo, "Received unexpected signal");
	alarm_set = 1;
}
#endif

static void *
setup(void *dummy)
{
	struct sigaction sa;
	sigset_t ss;
#ifdef SA_SIGINFO
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = alarm_handler;
#else
	sa.sa_flags = 0;
	sa.sa_handler = alarm_handler;
#endif
	sigfillset(&ss);
	sigprocmask(SIG_SETMASK, &ss, NULL);
	sigemptyset(&sa.sa_mask);
	sigaction(SIGALRM, &sa, NULL);
	alarm(1);

	return NULL;
}

ATF_TC(sigalarm);
ATF_TC_HEAD(sigalarm, tc)
{
	atf_tc_set_md_var(tc, "descr",
		"Checks sigsuspend in libpthread when pthread lib is initialized");
}
ATF_TC_BODY(sigalarm, tc)
{
	sigset_t set;
	pthread_t self = pthread_self();

	PTHREAD_REQUIRE(pthread_create(&self, NULL, setup, NULL));

	sigemptyset(&set);
	sigsuspend(&set);
	alarm(0);

	ATF_REQUIRE(alarm_set);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, sigalarm);

	return atf_no_error();
}
