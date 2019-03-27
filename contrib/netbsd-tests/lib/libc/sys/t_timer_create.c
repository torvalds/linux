/*	$NetBSD: t_timer_create.c,v 1.5 2017/01/16 16:32:13 christos Exp $ */

/*-
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
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

#include <atf-c.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static timer_t t;
static bool fail = true;

static void
timer_signal_handler(int signo, siginfo_t *si, void *osi __unused)
{
	timer_t *tp;

	tp = si->si_value.sival_ptr;

	if (*tp == t && signo == SIGALRM)
		fail = false;

	(void)fprintf(stderr, "%s: %s\n", __func__, strsignal(signo));
}

static void
timer_signal_create(clockid_t cid, bool expire)
{
	struct itimerspec tim;
	struct sigaction act;
	struct sigevent evt;
	sigset_t set;

	t = 0;
	fail = true;

	(void)memset(&evt, 0, sizeof(struct sigevent));
	(void)memset(&act, 0, sizeof(struct sigaction));
	(void)memset(&tim, 0, sizeof(struct itimerspec));

	/*
	 * Set handler.
	 */
	act.sa_flags = SA_SIGINFO;
	act.sa_sigaction = timer_signal_handler;

	ATF_REQUIRE(sigemptyset(&set) == 0);
	ATF_REQUIRE(sigemptyset(&act.sa_mask) == 0);

	/*
	 * Block SIGALRM while configuring the timer.
	 */
	ATF_REQUIRE(sigaction(SIGALRM, &act, NULL) == 0);
	ATF_REQUIRE(sigaddset(&set, SIGALRM) == 0);
	ATF_REQUIRE(sigprocmask(SIG_SETMASK, &set, NULL) == 0);

	/*
	 * Create the timer (SIGEV_SIGNAL).
	 */
	evt.sigev_signo = SIGALRM;
	evt.sigev_value.sival_ptr = &t;
	evt.sigev_notify = SIGEV_SIGNAL;

	ATF_REQUIRE(timer_create(cid, &evt, &t) == 0);

	/*
	 * Start the timer. After this, unblock the signal.
	 */
	tim.it_value.tv_sec = expire ? 5 : 1;
	tim.it_value.tv_nsec = 0;

	ATF_REQUIRE(timer_settime(t, 0, &tim, NULL) == 0);

	(void)sigprocmask(SIG_UNBLOCK, &set, NULL);
	(void)sleep(2);

	if (expire) {
		if (!fail)
			atf_tc_fail("timer fired too soon");
	} else {
		if (fail)
			atf_tc_fail("timer failed to fire");
	}

	ATF_REQUIRE(timer_delete(t) == 0);
}

#ifdef __FreeBSD__
static void
timer_callback(union sigval value)
{
	timer_t *tp;

	tp = value.sival_ptr;

	if (*tp == t)
		fail = false;
}

static void
timer_thread_create(clockid_t cid, bool expire)
{
	struct itimerspec tim;
	struct sigevent evt;

	t = 0;
	fail = true;

	(void)memset(&evt, 0, sizeof(struct sigevent));
	(void)memset(&tim, 0, sizeof(struct itimerspec));

	/*
	 * Create the timer (SIGEV_THREAD).
	 */
	evt.sigev_notify_function = timer_callback;
	evt.sigev_value.sival_ptr = &t;
	evt.sigev_notify = SIGEV_THREAD;

	ATF_REQUIRE(timer_create(cid, &evt, &t) == 0);

	/*
	 * Start the timer.
	 */
	tim.it_value.tv_sec = expire ? 5 : 1;
	tim.it_value.tv_nsec = 0;

	ATF_REQUIRE(timer_settime(t, 0, &tim, NULL) == 0);

	(void)sleep(2);

	if (expire) {
		if (!fail)
			atf_tc_fail("timer fired too soon");
	} else {
		if (fail)
			atf_tc_fail("timer failed to fire");
	}

	ATF_REQUIRE(timer_delete(t) == 0);
}
#endif

ATF_TC(timer_create_err);
ATF_TC_HEAD(timer_create_err, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Check errors from timer_create(2) (PR lib/42434");
}

ATF_TC_BODY(timer_create_err, tc)
{
	struct sigevent ev;

	(void)memset(&ev, 0, sizeof(struct sigevent));

	errno = 0;
	ev.sigev_signo = -1;
	ev.sigev_notify = SIGEV_SIGNAL;

	ATF_REQUIRE_ERRNO(EINVAL, timer_create(CLOCK_REALTIME, &ev, &t) == -1);

	errno = 0;
	ev.sigev_signo = SIGUSR1;
	ev.sigev_notify = SIGEV_THREAD + 100;

	ATF_REQUIRE_ERRNO(EINVAL, timer_create(CLOCK_REALTIME, &ev, &t) == -1);
}

ATF_TC(timer_create_real);
ATF_TC_HEAD(timer_create_real, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Checks timer_create(2) with CLOCK_REALTIME and sigevent(3), "
	    "SIGEV_SIGNAL");
}

ATF_TC_BODY(timer_create_real, tc)
{
	timer_signal_create(CLOCK_REALTIME, false);
}

ATF_TC(timer_create_mono);
ATF_TC_HEAD(timer_create_mono, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Checks timer_create(2) with CLOCK_MONOTONIC and sigevent(3), "
	    "SIGEV_SIGNAL");
}

ATF_TC_BODY(timer_create_mono, tc)
{
	timer_signal_create(CLOCK_MONOTONIC, false);
}

ATF_TC(timer_create_real_expire);
ATF_TC_HEAD(timer_create_real_expire, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Checks timer_create(2) with CLOCK_REALTIME and sigevent(3), "
	    "SIGEV_SIGNAL, with expiration");
}

ATF_TC_BODY(timer_create_real_expire, tc)
{
	timer_signal_create(CLOCK_REALTIME, true);
}

ATF_TC(timer_create_mono_expire);
ATF_TC_HEAD(timer_create_mono_expire, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Checks timer_create(2) with CLOCK_MONOTONIC and sigevent(3), "
	    "SIGEV_SIGNAL, with expiration");
}

ATF_TC_BODY(timer_create_mono_expire, tc)
{
	timer_signal_create(CLOCK_MONOTONIC, true);
}

ATF_TC(timer_thread_create_real);
ATF_TC_HEAD(timer_thread_create_real, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Checks timer_create(2) with CLOCK_REALTIME and sigevent(3), "
	    "SIGEV_THREAD");
}

#ifdef __FreeBSD__
ATF_TC_BODY(timer_thread_create_real, tc)
{
	timer_thread_create(CLOCK_REALTIME, false);
}

ATF_TC(timer_thread_create_mono);
ATF_TC_HEAD(timer_thread_create_mono, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Checks timer_create(2) with CLOCK_MONOTONIC and sigevent(3), "
	    "SIGEV_THREAD");
}

ATF_TC_BODY(timer_thread_create_mono, tc)
{
	timer_thread_create(CLOCK_MONOTONIC, false);
}

ATF_TC(timer_thread_create_real_expire);
ATF_TC_HEAD(timer_thread_create_real_expire, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Checks timer_create(2) with CLOCK_REALTIME and sigevent(3), "
	    "SIGEV_THREAD, with expiration");
}

ATF_TC_BODY(timer_thread_create_real_expire, tc)
{
	timer_thread_create(CLOCK_REALTIME, true);
}

ATF_TC(timer_thread_create_mono_expire);
ATF_TC_HEAD(timer_thread_create_mono_expire, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Checks timer_create(2) with CLOCK_MONOTONIC and sigevent(3), "
	    "SIGEV_THREAD, with expiration");
}

ATF_TC_BODY(timer_thread_create_mono_expire, tc)
{
	timer_thread_create(CLOCK_MONOTONIC, true);
}
#endif

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, timer_create_err);
	ATF_TP_ADD_TC(tp, timer_create_real);
	ATF_TP_ADD_TC(tp, timer_create_mono);
	ATF_TP_ADD_TC(tp, timer_create_real_expire);
	ATF_TP_ADD_TC(tp, timer_create_mono_expire);
#ifdef __FreeBSD__
	ATF_TP_ADD_TC(tp, timer_thread_create_real);
	ATF_TP_ADD_TC(tp, timer_thread_create_mono);
	ATF_TP_ADD_TC(tp, timer_thread_create_real_expire);
	ATF_TP_ADD_TC(tp, timer_thread_create_mono_expire);
#endif

	return atf_no_error();
}
