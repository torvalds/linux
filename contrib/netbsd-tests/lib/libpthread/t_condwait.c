/* $NetBSD: t_condwait.c,v 1.5 2017/01/16 16:29:19 christos Exp $ */

/*
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
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
__RCSID("$NetBSD: t_condwait.c,v 1.5 2017/01/16 16:29:19 christos Exp $");

#include <sys/time.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <atf-c.h>

#include "isqemu.h"

#include "h_common.h"

#define WAITTIME 2	/* Timeout wait secound */

static const int debug = 1;

static void *
run(void *param)
{
	struct timespec ts, to, te;
	clockid_t clck;
	pthread_condattr_t attr;
	pthread_cond_t cond;
	pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
	int ret = 0;


	clck = *(clockid_t *)param;
	PTHREAD_REQUIRE(pthread_condattr_init(&attr));
	PTHREAD_REQUIRE(pthread_condattr_setclock(&attr, clck));
	pthread_cond_init(&cond, &attr);

	ATF_REQUIRE_EQ((ret = pthread_mutex_lock(&m)), 0);

	ATF_REQUIRE_EQ(clock_gettime(clck, &ts), 0);
	to = ts;

	if (debug)
		printf("started: %lld.%09ld sec\n", (long long)to.tv_sec,
		    to.tv_nsec);

	ts.tv_sec += WAITTIME;	/* Timeout wait */

	switch (ret = pthread_cond_timedwait(&cond, &m, &ts)) {
	case ETIMEDOUT:
		/* Timeout */
		ATF_REQUIRE_EQ(clock_gettime(clck, &te), 0);
		timespecsub(&te, &to, &to);
		if (debug) {
			printf("timeout: %lld.%09ld sec\n",
			    (long long)te.tv_sec, te.tv_nsec);
			printf("elapsed: %lld.%09ld sec\n",
			    (long long)to.tv_sec, to.tv_nsec);
		}
		if (isQEMU()) {
			double to_seconds = to.tv_sec + 1e-9 * to.tv_nsec;
			ATF_REQUIRE(to_seconds >= WAITTIME * 0.9);
			/* Loose upper limit because of qemu timing bugs */
			ATF_REQUIRE(to_seconds < WAITTIME * 2.5);
		} else {
			ATF_REQUIRE_EQ(to.tv_sec, WAITTIME);
		}
		break;
	default:
		ATF_REQUIRE_MSG(0, "pthread_cond_timedwait: %s", strerror(ret));
	}

	ATF_REQUIRE_MSG(!(ret = pthread_mutex_unlock(&m)),
	    "pthread_mutex_unlock: %s", strerror(ret));
	pthread_exit(&ret);
}

static void
cond_wait(clockid_t clck, const char *msg) {
	pthread_t child;

	if (debug)
		printf( "**** %s clock wait starting\n", msg);
	ATF_REQUIRE_EQ(pthread_create(&child, NULL, run, &clck), 0);
	ATF_REQUIRE_EQ(pthread_join(child, NULL), 0); /* wait for terminate */
	if (debug)
		printf( "**** %s clock wait ended\n", msg);
}

ATF_TC(cond_wait_real);
ATF_TC_HEAD(cond_wait_real, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks pthread_cond_timedwait "
	    "with CLOCK_REALTIME");
}

ATF_TC_BODY(cond_wait_real, tc) {
	cond_wait(CLOCK_REALTIME, "CLOCK_REALTIME");
}

ATF_TC(cond_wait_mono);
ATF_TC_HEAD(cond_wait_mono, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks pthread_cond_timedwait "
	    "with CLOCK_MONOTONIC");
}

ATF_TC_BODY(cond_wait_mono, tc) {
	cond_wait(CLOCK_MONOTONIC, "CLOCK_MONOTONIC");
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, cond_wait_real);
	ATF_TP_ADD_TC(tp, cond_wait_mono);
	return 0;
}
