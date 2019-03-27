/* $NetBSD: t_sleep.c,v 1.1 2010/07/16 15:42:53 jmmv Exp $ */

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
__RCSID("$NetBSD: t_sleep.c,v 1.1 2010/07/16 15:42:53 jmmv Exp $");

#include <sys/time.h>

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include <atf-c.h>

#include "h_common.h"

#define LONGTIME 2000000000

static void *
threadfunc(void *arg)
{
	sleep(LONGTIME);

	return NULL;
}

static void
handler(int sig)
{
	/*
	 * Nothing to do; invoking the handler is enough to interrupt
	 * the sleep.
	 */
}

ATF_TC(sleep1);
ATF_TC_HEAD(sleep1, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks sleeping past the time when "
	    "time_t wraps");
}
ATF_TC_BODY(sleep1, tc)
{
	pthread_t thread;
	struct itimerval it;
	struct sigaction act;
	sigset_t mtsm;

	printf("Testing sleeps unreasonably far into the future.\n");

	act.sa_handler = handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGALRM, &act, NULL);

	PTHREAD_REQUIRE(pthread_create(&thread, NULL, threadfunc, NULL));

	/* make sure the signal is delivered to the child thread */
	sigemptyset(&mtsm);
	sigaddset(&mtsm, SIGALRM);
	PTHREAD_REQUIRE(pthread_sigmask(SIG_BLOCK, &mtsm, 0));

	timerclear(&it.it_interval);
	timerclear(&it.it_value);
	it.it_value.tv_sec = 1;
	setitimer(ITIMER_REAL, &it, NULL);

	PTHREAD_REQUIRE(pthread_join(thread, NULL));
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, sleep1);

	return atf_no_error();
}
