/* $NetBSD: t_sleep.c,v 1.11 2017/01/10 15:43:59 maya Exp $ */

/*-
 * Copyright (c) 2006 Frank Kardel
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
#include <sys/event.h>
#include <sys/signal.h>
#include <sys/time.h>		/* for TIMESPEC_TO_TIMEVAL on FreeBSD */

#include <atf-c.h>
#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "isqemu.h"

#define BILLION		1000000000LL	/* nano-seconds per second */
#define MILLION		1000000LL	/* nano-seconds per milli-second */

#define ALARM		6		/* SIGALRM after this many seconds */
#define MAXSLEEP	22		/* Maximum delay in seconds */
#define KEVNT_TIMEOUT	10300		/* measured in milli-seconds */
#define FUZZ		(40 * MILLION)	/* scheduling fuzz accepted - 40 ms */

/*
 * Timer notes
 *
 * Most tests use FUZZ as their initial delay value, but 'sleep'
 * starts at 1sec (since it cannot handle sub-second intervals).
 * Subsequent passes double the previous interval, up to MAXSLEEP.
 *
 * The current values result in 5 passes for the 'sleep' test (at 1,
 * 2, 4, 8, and 16 seconds) and 10 passes for the other tests (at
 * 0.04, 0.08, 0.16, 0.32, 0.64, 1.28, 2.56, 5.12, 10.24, and 20.48
 * seconds).
 *
 * The ALARM is only set if the current pass's delay is longer, and
 * only if the ALARM has not already been triggered.
 *
 * The 'kevent' test needs the ALARM to be set on a different pass
 * from when the KEVNT_TIMEOUT fires.  So set ALARM to fire on the
 * penultimate pass, and the KEVNT_TIMEOUT on the final pass.  We
 * set KEVNT_TIMEOUT just barely long enough to put it into the
 * last test pass, and set MAXSLEEP a couple seconds longer than
 * necessary, in order to avoid a QEMU bug which nearly doubles
 * some timers.
 */

static volatile int sig;

int sleeptest(int (*)(struct timespec *, struct timespec *), bool, bool);
int do_nanosleep(struct timespec *, struct timespec *);
int do_select(struct timespec *, struct timespec *);
#ifdef __NetBSD__
int do_poll(struct timespec *, struct timespec *);
#endif
int do_sleep(struct timespec *, struct timespec *);
int do_kevent(struct timespec *, struct timespec *);
void sigalrm(int);

void
sigalrm(int s)
{

	sig++;
}

int
do_nanosleep(struct timespec *delay, struct timespec *remain)
{
	int ret;

	if (nanosleep(delay, remain) == -1)
		ret = (errno == EINTR ? 0 : errno);
	else
		ret = 0;
	return ret;
}

int
do_select(struct timespec *delay, struct timespec *remain)
{
	int ret;
	struct timeval tv;

	TIMESPEC_TO_TIMEVAL(&tv, delay);
	if (select(0, NULL, NULL, NULL, &tv) == -1)
		ret = (errno == EINTR ? 0 : errno);
	else
		ret = 0;
	return ret;
}

#ifdef __NetBSD__
int
do_poll(struct timespec *delay, struct timespec *remain)
{
	int ret;
	struct timeval tv;

	TIMESPEC_TO_TIMEVAL(&tv, delay);
	if (pollts(NULL, 0, delay, NULL) == -1)
		ret = (errno == EINTR ? 0 : errno);
	else
		ret = 0;
	return ret;
}
#endif

int
do_sleep(struct timespec *delay, struct timespec *remain)
{
	struct timeval tv;

	TIMESPEC_TO_TIMEVAL(&tv, delay);
	remain->tv_sec = sleep(delay->tv_sec);
	remain->tv_nsec = 0;

	return 0;
}

int
do_kevent(struct timespec *delay, struct timespec *remain)
{
	struct kevent ktimer;
	struct kevent kresult;
	int rtc, kq, kerrno;
	int tmo;

	ATF_REQUIRE_MSG((kq = kqueue()) != -1, "kqueue: %s", strerror(errno));

	tmo = KEVNT_TIMEOUT;

	/*
	 * If we expect the KEVNT_TIMEOUT to fire, and we're running
	 * under QEMU, make sure the delay is long enough to account
	 * for the effects of PR kern/43997 !
	 */
	if (isQEMU() &&
	    tmo/1000 < delay->tv_sec && tmo/500 > delay->tv_sec)
		delay->tv_sec = MAXSLEEP;

	EV_SET(&ktimer, 1, EVFILT_TIMER, EV_ADD, 0, tmo, 0);

	rtc = kevent(kq, &ktimer, 1, &kresult, 1, delay);
	kerrno = errno;

	(void)close(kq);

	if (rtc == -1) {
		ATF_REQUIRE_MSG(kerrno == EINTR, "kevent: %s",
		    strerror(kerrno));
		return 0;
	}

	if (delay->tv_sec * BILLION + delay->tv_nsec > tmo * MILLION)
		ATF_REQUIRE_MSG(rtc > 0,
		    "kevent: KEVNT_TIMEOUT did not cause EVFILT_TIMER event");

	return 0;
}

ATF_TC(nanosleep);
ATF_TC_HEAD(nanosleep, tc) 
{
 
	atf_tc_set_md_var(tc, "descr", "Test nanosleep(2) timing");
	atf_tc_set_md_var(tc, "timeout", "65");
} 

ATF_TC_BODY(nanosleep, tc)
{

	sleeptest(do_nanosleep, true, false);
}

ATF_TC(select);
ATF_TC_HEAD(select, tc) 
{
 
	atf_tc_set_md_var(tc, "descr", "Test select(2) timing");
	atf_tc_set_md_var(tc, "timeout", "65");
} 

ATF_TC_BODY(select, tc)
{

	sleeptest(do_select, true, true);
}

#ifdef __NetBSD__
ATF_TC(poll);
ATF_TC_HEAD(poll, tc) 
{
 
	atf_tc_set_md_var(tc, "descr", "Test poll(2) timing");
	atf_tc_set_md_var(tc, "timeout", "65");
} 

ATF_TC_BODY(poll, tc)
{

	sleeptest(do_poll, true, true);
}
#endif

ATF_TC(sleep);
ATF_TC_HEAD(sleep, tc) 
{
 
	atf_tc_set_md_var(tc, "descr", "Test sleep(3) timing");
	atf_tc_set_md_var(tc, "timeout", "65");
} 

ATF_TC_BODY(sleep, tc)
{

	sleeptest(do_sleep, false, false);
}

ATF_TC(kevent);
ATF_TC_HEAD(kevent, tc) 
{
 
	atf_tc_set_md_var(tc, "descr", "Test kevent(2) timing");
	atf_tc_set_md_var(tc, "timeout", "65");
} 

ATF_TC_BODY(kevent, tc)
{

	sleeptest(do_kevent, true, true);
}

int
sleeptest(int (*test)(struct timespec *, struct timespec *),
	   bool subsec, bool sim_remain)
{
	struct timespec tsa, tsb, tslp, tremain;
	int64_t delta1, delta2, delta3, round;

	sig = 0;
	signal(SIGALRM, sigalrm);

	if (subsec) {
		round = 1;
		delta3 = FUZZ;
	} else {
		round = 1000000000;
		delta3 = round;
	}

	tslp.tv_sec = delta3 / 1000000000;
	tslp.tv_nsec = delta3 % 1000000000;

	while (tslp.tv_sec <= MAXSLEEP) {
		/*
		 * disturb sleep by signal on purpose
		 */ 
		if (tslp.tv_sec > ALARM && sig == 0)
			alarm(ALARM);

		clock_gettime(CLOCK_REALTIME, &tsa);
		(*test)(&tslp, &tremain);
		clock_gettime(CLOCK_REALTIME, &tsb);

		if (sim_remain) {
			timespecsub(&tsb, &tsa, &tremain);
			timespecsub(&tslp, &tremain, &tremain);
		}

		delta1 = (int64_t)tsb.tv_sec - (int64_t)tsa.tv_sec;
		delta1 *= BILLION;
		delta1 += (int64_t)tsb.tv_nsec - (int64_t)tsa.tv_nsec;

		delta2 = (int64_t)tremain.tv_sec * BILLION;
		delta2 += (int64_t)tremain.tv_nsec;

		delta3 = (int64_t)tslp.tv_sec * BILLION;
		delta3 += (int64_t)tslp.tv_nsec - delta1 - delta2;

		delta3 /= round;
		delta3 *= round;

		if (delta3 > FUZZ || delta3 < -FUZZ) {
			if (!sim_remain)
				atf_tc_expect_fail("Long reschedule latency "
				    "due to PR kern/43997");

			atf_tc_fail("Reschedule latency %"PRId64" exceeds "
			    "allowable fuzz %lld", delta3, FUZZ);
		}
		delta3 = (int64_t)tslp.tv_sec * 2 * BILLION;
		delta3 += (int64_t)tslp.tv_nsec * 2;

		delta3 /= round;
		delta3 *= round;
		if (delta3 < FUZZ)
			break;
		tslp.tv_sec = delta3 / BILLION;
		tslp.tv_nsec = delta3 % BILLION;
	}
	ATF_REQUIRE_MSG(sig == 1, "Alarm did not fire!");

	atf_tc_pass();
}

ATF_TP_ADD_TCS(tp) 
{
	ATF_TP_ADD_TC(tp, nanosleep);
	ATF_TP_ADD_TC(tp, select);
#ifdef __NetBSD__
	ATF_TP_ADD_TC(tp, poll); 
#endif
	ATF_TP_ADD_TC(tp, sleep);
	ATF_TP_ADD_TC(tp, kevent);
 
	return atf_no_error();
}
