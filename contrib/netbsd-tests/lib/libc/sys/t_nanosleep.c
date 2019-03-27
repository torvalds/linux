/* $NetBSD: t_nanosleep.c,v 1.4 2017/01/13 21:15:14 christos Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jukka Ruohonen.
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
__RCSID("$NetBSD: t_nanosleep.c,v 1.4 2017/01/13 21:15:14 christos Exp $");

#include <sys/time.h>
#include <sys/wait.h>

#include <atf-c.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

static void
handler(int signo __unused)
{
	/* Nothing. */
}

static int got_info;
static void
info_handler(int signo __unused)
{

	got_info = 1;
}


ATF_TC(nanosleep_basic);
ATF_TC_HEAD(nanosleep_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test that nanosleep(2) works");
}

ATF_TC_BODY(nanosleep_basic, tc)
{
	static const size_t maxiter = 10;
	struct timespec ts1, ts2, tsn;
	size_t i;

	for (i = 1; i < maxiter; i++) {

		tsn.tv_sec = 0;
		tsn.tv_nsec = i;

		(void)memset(&ts1, 0, sizeof(struct timespec));
		(void)memset(&ts2, 0, sizeof(struct timespec));

		ATF_REQUIRE(clock_gettime(CLOCK_MONOTONIC, &ts1) == 0);
		ATF_REQUIRE(nanosleep(&tsn, NULL) == 0);
		ATF_REQUIRE(clock_gettime(CLOCK_MONOTONIC, &ts2) == 0);

		/*
		 * Verify that we slept at least one nanosecond.
		 */
		if (timespeccmp(&ts2, &ts1, <=) != 0) {

			(void)fprintf(stderr,
			    "sleep time:: sec %llu, nsec %lu\n\t\t"
			    "ts1: sec %llu, nsec %lu\n\t\t"
			    "ts2: sec %llu, nsec %lu\n",
			    (unsigned long long)tsn.tv_sec, tsn.tv_nsec,
			    (unsigned long long)ts1.tv_sec, ts1.tv_nsec,
			    (unsigned long long)ts2.tv_sec, ts2.tv_nsec);

			atf_tc_fail_nonfatal("inaccuracies in sleep time "
			    "(resolution = %lu nsec)", tsn.tv_nsec);
		}
	}
}

ATF_TC(nanosleep_err);
ATF_TC_HEAD(nanosleep_err, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test errors from nanosleep(2) (PR bin/14558)");
}

ATF_TC_BODY(nanosleep_err, tc)
{
	struct timespec ts;

	ts.tv_sec = 1;
	ts.tv_nsec = -1;
	errno = 0;
	ATF_REQUIRE_ERRNO(EINVAL, nanosleep(&ts, NULL) == -1);

	ts.tv_sec = 1;
	ts.tv_nsec = 1000000000;
	errno = 0;
	ATF_REQUIRE_ERRNO(EINVAL, nanosleep(&ts, NULL) == -1);

	ts.tv_sec = -1;
	ts.tv_nsec = 0;
	errno = 0;
	ATF_REQUIRE_ERRNO(0, nanosleep(&ts, NULL) == 0);

	errno = 0;
	ATF_REQUIRE_ERRNO(EFAULT, nanosleep((void *)-1, NULL) == -1);
}

ATF_TC(nanosleep_sig);
ATF_TC_HEAD(nanosleep_sig, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test signal for nanosleep(2)");
}

ATF_TC_BODY(nanosleep_sig, tc)
{
	struct timespec tsn, tsr;
	pid_t pid;
	int sta;

	/*
	 * Test that a signal interrupts nanosleep(2).
	 *
	 * (In which case the return value should be -1 and the
	 * second parameter should contain the unslept time.)
	 */
	pid = fork();

	ATF_REQUIRE(pid >= 0);
	ATF_REQUIRE(signal(SIGINT, handler) == 0);

	if (pid == 0) {

		tsn.tv_sec = 10;
		tsn.tv_nsec = 0;

		tsr.tv_sec = 0;
		tsr.tv_nsec = 0;

		errno = 0;

		if (nanosleep(&tsn, &tsr) != -1)
			_exit(EXIT_FAILURE);

		if (errno != EINTR)
			_exit(EXIT_FAILURE);

		if (tsr.tv_sec == 0 && tsr.tv_nsec == 0)
			_exit(EXIT_FAILURE);

		_exit(EXIT_SUCCESS);
	}

	(void)sleep(1);
	(void)kill(pid, SIGINT);
	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
		atf_tc_fail("signal did not interrupt nanosleep(2)");
}

ATF_TC(nanosleep_eintr);
ATF_TC_HEAD(nanosleep_eintr, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test [EINTR] for nanosleep(2)");
	atf_tc_set_md_var(tc, "timeout", "7");
}

ATF_TC_BODY(nanosleep_eintr, tc)
{
	struct sigaction act;
	struct timespec tso, ts;
	pid_t pid;
	int sta;

	/*
	 * Test that [EINTR] properly handles rmtp for nanosleep(2).
	 */
	pid = fork();

	ATF_REQUIRE(pid >= 0);

	got_info = 0;

	if (pid == 0) {
		act.sa_handler = info_handler;
		sigemptyset(&act.sa_mask);
		act.sa_flags = 0; /* Don't allow restart. */
		ATF_REQUIRE(sigaction(SIGINFO, &act, NULL) == 0);

		tso.tv_sec = 5;
		tso.tv_nsec = 0;

		ts.tv_sec = tso.tv_sec;
		ts.tv_nsec = tso.tv_nsec;

		errno = 0;
		while (nanosleep(&ts, &ts) != 0) {
			ATF_REQUIRE_MSG(timespeccmp(&ts, &tso, <=),
			    "errno=%d ts=%0.9f should be <= last tso=%0.9f\n",
			    errno,
			    ts.tv_sec + ts.tv_nsec / 1e9,
			    tso.tv_sec + tso.tv_nsec / 1e9);
			if (errno == EINTR && got_info == 1) {
				got_info = 0;
				errno = 0;
				tso.tv_sec = ts.tv_sec;
				tso.tv_nsec = ts.tv_nsec;
				continue;
			}
			_exit(EXIT_FAILURE);
		}

		if (errno != 0)
			_exit(EXIT_FAILURE);

		_exit(EXIT_SUCCESS);
	}

	/* Flood the process with SIGINFO until it exits. */
	do {
		for (int i = 0; i < 10; i++)
			ATF_REQUIRE(kill(pid, SIGINFO) == 0);
		ATF_REQUIRE(usleep(10000) == 0);
	} while (waitpid(pid, &sta, WNOHANG) == 0);

	ATF_REQUIRE(WIFEXITED(sta) == 1);

	if (WEXITSTATUS(sta) != EXIT_SUCCESS)
		atf_tc_fail("nanosleep(2) handled rtmp incorrectly");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, nanosleep_basic);
	ATF_TP_ADD_TC(tp, nanosleep_err);
	ATF_TP_ADD_TC(tp, nanosleep_sig);
	ATF_TP_ADD_TC(tp, nanosleep_eintr);

	return atf_no_error();
}
