/* $NetBSD: t_sigmask.c,v 1.3 2013/10/19 17:45:01 christos Exp $ */

/*
 * Copyright (c) 2008, 2010 The NetBSD Foundation, Inc.
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
__COPYRIGHT("@(#) Copyright (c) 2008, 2010\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_sigmask.c,v 1.3 2013/10/19 17:45:01 christos Exp $");

/*
 * Regression test for pthread_sigmask when SA upcalls aren't started yet.
 *
 * Written by Christian Limpach <cl@NetBSD.org>, December 2003.
 * Public domain.
 */

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/resource.h>

#include <atf-c.h>

#include "h_common.h"

static volatile sig_atomic_t flag;
static volatile sig_atomic_t flag2;

static volatile pthread_t thr_usr1;
static volatile pthread_t thr_usr2;

static sig_atomic_t count = 0;

ATF_TC(upcalls_not_started);
ATF_TC_HEAD(upcalls_not_started, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks pthread_sigmask when SA upcalls "
	    "aren't started yet");
}
ATF_TC_BODY(upcalls_not_started, tc)
{
	sigset_t nset;
	struct rlimit rlim;

	rlim.rlim_cur = rlim.rlim_max = 0;
	(void) setrlimit(RLIMIT_CORE, &rlim);

	sigemptyset(&nset);
	sigaddset(&nset, SIGFPE);
	pthread_sigmask(SIG_BLOCK, &nset, NULL);

	kill(getpid(), SIGFPE);
}

static void
upcalls_not_started_handler1(int sig, siginfo_t *info, void *ctx)
{

	kill(getpid(), SIGUSR2);
	/*
	 * If the mask is properly set, SIGUSR2 will not be handled
	 * until this handler returns.
	 */
	flag = 1;
}

static void
upcalls_not_started_handler2(int sig, siginfo_t *info, void *ctx)
{
	if (flag == 1)
		flag = 2;
}

ATF_TC(before_threads);
ATF_TC_HEAD(before_threads, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks that signal masks are respected "
	    "before threads are started");
}
ATF_TC_BODY(before_threads, tc)
{
	struct sigaction act;

	act.sa_sigaction = upcalls_not_started_handler1;
	sigemptyset(&act.sa_mask);
	sigaddset(&act.sa_mask, SIGUSR2);
	act.sa_flags = SA_SIGINFO;

	ATF_REQUIRE_EQ(sigaction(SIGUSR1, &act, NULL), 0);

	act.sa_sigaction = upcalls_not_started_handler2;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO;
	(void)sigaction(SIGUSR2, &act, NULL);

	kill(getpid(), SIGUSR1);

	ATF_REQUIRE_EQ(flag, 2);
	printf("Success: Both handlers ran in order\n");
}

static void
respected_while_running_handler1(int sig, siginfo_t *info, void *ctx)
{

	kill(getpid(), SIGUSR2);
	/*
	 * If the mask is properly set, SIGUSR2 will not be handled
	 * by the current thread until this handler returns.
	 */
	flag = 1;
	thr_usr1 = pthread_self();
}

static void
respected_while_running_handler2(int sig, siginfo_t *info, void *ctx)
{
	if (flag == 1)
		flag = 2;
	flag2 = 1;
	thr_usr2 = pthread_self();
}

static void *
respected_while_running_threadroutine(void *arg)
{

	kill(getpid(), SIGUSR1);
	sleep(1);

	if (flag == 2)
		printf("Success: Both handlers ran in order\n");
	else if (flag == 1 && flag2 == 1 && thr_usr1 != thr_usr2)
		printf("Success: Handlers were invoked by different threads\n");
	else {
		printf("Failure: flag=%d, flag2=%d, thr1=%p, thr2=%p\n",
			(int)flag, (int)flag2, (void *)thr_usr1, (void *)thr_usr2);
		atf_tc_fail("failure");
	}

	return NULL;
}

ATF_TC(respected_while_running);
ATF_TC_HEAD(respected_while_running, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks that signal masks are respected "
	    "while threads are running");
}
ATF_TC_BODY(respected_while_running, tc)
{
	struct sigaction act;
	pthread_t thread;

	act.sa_sigaction = respected_while_running_handler1;
	sigemptyset(&act.sa_mask);
	sigaddset(&act.sa_mask, SIGUSR2);
	act.sa_flags = SA_SIGINFO;

	ATF_REQUIRE_EQ(sigaction(SIGUSR1, &act, NULL), 0);

	act.sa_sigaction = respected_while_running_handler2;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO;
	(void)sigaction(SIGUSR2, &act, NULL);

	PTHREAD_REQUIRE(pthread_create(&thread, NULL,
	    respected_while_running_threadroutine, NULL));
	PTHREAD_REQUIRE(pthread_join(thread, NULL));
}

static void
incorrect_mask_bug_handler(int sig)
{
	count++;
}

static void *
incorrect_mask_bug_sleeper(void* arg)
{
	int i;
	for (i = 0; i < 10; i++)
		sleep(1);

	atf_tc_fail("sleeper");
}

ATF_TC(incorrect_mask_bug);
ATF_TC_HEAD(incorrect_mask_bug, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks for bug in libpthread where "
	    "incorrect signal mask was used");
}
ATF_TC_BODY(incorrect_mask_bug, tc)
{
	pthread_t id;
	struct sigaction act;

	act.sa_sigaction = NULL;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = incorrect_mask_bug_handler;

	ATF_REQUIRE_EQ_MSG(sigaction(SIGALRM, &act, NULL), 0, "%s",
	    strerror(errno));

	sigaddset(&act.sa_mask, SIGALRM);
	PTHREAD_REQUIRE(pthread_sigmask(SIG_SETMASK, &act.sa_mask, NULL));

	PTHREAD_REQUIRE(pthread_create(&id, NULL, incorrect_mask_bug_sleeper,
	    NULL));
	sleep(1);

	sigemptyset(&act.sa_mask);
	PTHREAD_REQUIRE(pthread_sigmask(SIG_SETMASK, &act.sa_mask, NULL));

	for (;;) {
		alarm(1);
		if (select(1, NULL, NULL, NULL, NULL) == -1 && errno == EINTR)
			if (count == 2)
				return;
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, upcalls_not_started);
	ATF_TP_ADD_TC(tp, before_threads);
	ATF_TP_ADD_TC(tp, respected_while_running);
	ATF_TP_ADD_TC(tp, incorrect_mask_bug);

	return atf_no_error();
}
