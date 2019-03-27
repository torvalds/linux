/* $NetBSD: t_once.c,v 1.1 2010/07/16 15:42:53 jmmv Exp $ */

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
__RCSID("$NetBSD: t_once.c,v 1.1 2010/07/16 15:42:53 jmmv Exp $");

#ifdef __FreeBSD__
#include <sys/time.h> /* For itimer*, etc. */
#endif
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <atf-c.h>

#include "h_common.h"

static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static int x;

#define NTHREADS 25

static void
ofunc(void)
{
	printf("Variable x has value %d\n", x);
	x++;
}

ATF_TC(once1);
ATF_TC_HEAD(once1, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks pthread_once()");
}
ATF_TC_BODY(once1, tc)
{

	printf("1: Test 1 of pthread_once()\n");

	PTHREAD_REQUIRE(pthread_once(&once, ofunc));
	PTHREAD_REQUIRE(pthread_once(&once, ofunc));

	printf("1: X has value %d\n",x );
	ATF_REQUIRE_EQ(x, 1);
}

static void
once2_ofunc(void)
{
	x++;
	printf("ofunc: Variable x has value %d\n", x);
	x++;
}

static void *
once2_threadfunc(void *arg)
{
	int num;

	PTHREAD_REQUIRE(pthread_once(&once, once2_ofunc));

	num = *(int *)arg;
	printf("Thread %d sees x with value %d\n", num, x);
	ATF_REQUIRE_EQ(x, 2);

	return NULL;
}

ATF_TC(once2);
ATF_TC_HEAD(once2, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks pthread_once()");
}
ATF_TC_BODY(once2, tc)
{
	pthread_t  threads[NTHREADS];
	int id[NTHREADS];
	int i;

	printf("1: Test 2 of pthread_once()\n");

	for (i=0; i < NTHREADS; i++) {
		id[i] = i;
		PTHREAD_REQUIRE(pthread_create(&threads[i], NULL, once2_threadfunc, &id[i]));
	}

	for (i=0; i < NTHREADS; i++)
		PTHREAD_REQUIRE(pthread_join(threads[i], NULL));

	printf("1: X has value %d\n",x );
	ATF_REQUIRE_EQ(x, 2);
}

static void
once3_cleanup(void *m)
{
	pthread_mutex_t *mu = m;

	PTHREAD_REQUIRE(pthread_mutex_unlock(mu));
}

static void
once3_ofunc(void)
{
	pthread_testcancel();
}

static void *
once3_threadfunc(void *arg)
{
	PTHREAD_REQUIRE(pthread_mutex_lock(&mutex));
	pthread_cleanup_push(once3_cleanup, &mutex);
	PTHREAD_REQUIRE(pthread_once(&once, once3_ofunc));
	pthread_cleanup_pop(1);

	return NULL;
}

static void
handler(int sig, siginfo_t *info, void *ctx)
{
	atf_tc_fail("Signal handler was called; "
		"main thread deadlocked in pthread_once()");
}

ATF_TC(once3);
ATF_TC_HEAD(once3, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks pthread_once()");
}
ATF_TC_BODY(once3, tc)
{
	pthread_t thread;
	struct sigaction act;
	struct itimerval it;
	printf("Test 3 of pthread_once() (test versus cancellation)\n");

	act.sa_sigaction = handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO;
	sigaction(SIGALRM, &act, NULL);

	timerclear(&it.it_value);
	it.it_value.tv_usec = 500000;
	timerclear(&it.it_interval);
	setitimer(ITIMER_REAL, &it, NULL);

	PTHREAD_REQUIRE(pthread_mutex_lock(&mutex));
	PTHREAD_REQUIRE(pthread_create(&thread, NULL, once3_threadfunc, NULL));
	PTHREAD_REQUIRE(pthread_cancel(thread));
	PTHREAD_REQUIRE(pthread_mutex_unlock(&mutex));
	PTHREAD_REQUIRE(pthread_join(thread, NULL));

	PTHREAD_REQUIRE(pthread_once(&once, ofunc));

	/* Cancel timer */
	timerclear(&it.it_value);
	setitimer(ITIMER_REAL, &it, NULL);

	printf("Test succeeded\n");
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, once1);
	ATF_TP_ADD_TC(tp, once2);
	ATF_TP_ADD_TC(tp, once3);

	return atf_no_error();
}
