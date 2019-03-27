/* $NetBSD: t_sigtimedwait.c,v 1.2 2013/03/08 23:18:00 martin Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
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
__RCSID("$NetBSD: t_sigtimedwait.c,v 1.2 2013/03/08 23:18:00 martin Exp $");

#include <sys/time.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <atf-c.h>


ATF_TC(sigtimedwait_all0timeout);

ATF_TC_HEAD(sigtimedwait_all0timeout, tc)
{
	atf_tc_set_md_var(tc, "timeout", "10");
	atf_tc_set_md_var(tc, "descr", "Test for PR kern/47625: sigtimedwait"
	    " with a timeout value of all zero should return imediately");
}

ATF_TC_BODY(sigtimedwait_all0timeout, tc)
{
	sigset_t block;
	struct timespec ts, before, after, len;
	siginfo_t info;
	int r;

	sigemptyset(&block);
	ts.tv_sec = 0;
	ts.tv_nsec = 0;
	clock_gettime(CLOCK_MONOTONIC, &before);
	r = sigtimedwait(&block, &info, &ts);
	clock_gettime(CLOCK_MONOTONIC, &after);
	ATF_REQUIRE(r == -1);
	ATF_REQUIRE_ERRNO(EAGAIN, errno);
	timespecsub(&after, &before, &len);
	ATF_REQUIRE(len.tv_sec < 1);
}

ATF_TC(sigtimedwait_NULL_timeout);

ATF_TC_HEAD(sigtimedwait_NULL_timeout, tc)
{
	atf_tc_set_md_var(tc, "timeout", "10");
	atf_tc_set_md_var(tc, "descr", "Test sigtimedwait() without timeout");
}

ATF_TC_BODY(sigtimedwait_NULL_timeout, tc)
{
	sigset_t sig;
	siginfo_t info;
	struct itimerval it;
	int r;

	/* arrange for a SIGALRM signal in a few seconds */
	memset(&it, 0, sizeof it);
	it.it_value.tv_sec = 5;
	ATF_REQUIRE(setitimer(ITIMER_REAL, &it, NULL) == 0);

	/* wait without timeout */
	sigemptyset(&sig);
	sigaddset(&sig, SIGALRM);
	r = sigtimedwait(&sig, &info, NULL);
	ATF_REQUIRE(r == SIGALRM);
}

ATF_TC(sigtimedwait_small_timeout);

ATF_TC_HEAD(sigtimedwait_small_timeout, tc)
{
	atf_tc_set_md_var(tc, "timeout", "15");
	atf_tc_set_md_var(tc, "descr", "Test sigtimedwait with a small "
	    "timeout");
}

ATF_TC_BODY(sigtimedwait_small_timeout, tc)
{
	sigset_t block;
	struct timespec ts;
	siginfo_t info;
	int r;

	sigemptyset(&block);
	ts.tv_sec = 5;
	ts.tv_nsec = 0;
	r = sigtimedwait(&block, &info, &ts);
	ATF_REQUIRE(r == -1);
	ATF_REQUIRE_ERRNO(EAGAIN, errno);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, sigtimedwait_all0timeout);
	ATF_TP_ADD_TC(tp, sigtimedwait_NULL_timeout);
	ATF_TP_ADD_TC(tp, sigtimedwait_small_timeout);

	return atf_no_error();
}
