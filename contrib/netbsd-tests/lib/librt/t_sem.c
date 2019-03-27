/* $NetBSD: t_sem.c,v 1.3 2017/01/14 20:58:20 christos Exp $ */

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

/*
 * Copyright (C) 2000 Jason Evans <jasone@freebsd.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice(s), this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified other than the possible
 *    addition of one or more copyright notices.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice(s), this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 2008, 2010\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_sem.c,v 1.3 2017/01/14 20:58:20 christos Exp $");

#include <sys/time.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <atf-c.h>

#define NCHILDREN 10

#define SEM_REQUIRE(x) \
	ATF_REQUIRE_EQ_MSG(x, 0, "%s", strerror(errno))

ATF_TC_WITH_CLEANUP(basic);
ATF_TC_HEAD(basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks basic functionality of POSIX "
	    "semaphores");
}
ATF_TC_BODY(basic, tc)
{
	int val;
	sem_t *sem_b;

	if (sysconf(_SC_SEMAPHORES) == -1)
		atf_tc_skip("POSIX semaphores not supported");

	sem_b = sem_open("/sem_b", O_CREAT | O_EXCL, 0644, 0);
	ATF_REQUIRE(sem_b != SEM_FAILED);

	ATF_REQUIRE_EQ(sem_getvalue(sem_b, &val), 0);
	ATF_REQUIRE_EQ(val, 0);

	ATF_REQUIRE_EQ(sem_post(sem_b), 0);
	ATF_REQUIRE_EQ(sem_getvalue(sem_b, &val), 0);
	ATF_REQUIRE_EQ(val, 1);

	ATF_REQUIRE_EQ(sem_wait(sem_b), 0);
	ATF_REQUIRE_EQ(sem_trywait(sem_b), -1);
	ATF_REQUIRE_EQ(errno, EAGAIN);
	ATF_REQUIRE_EQ(sem_post(sem_b), 0);
	ATF_REQUIRE_EQ(sem_trywait(sem_b), 0);
	ATF_REQUIRE_EQ(sem_post(sem_b), 0);
	ATF_REQUIRE_EQ(sem_wait(sem_b), 0);
	ATF_REQUIRE_EQ(sem_post(sem_b), 0);

	ATF_REQUIRE_EQ(sem_close(sem_b), 0);
	ATF_REQUIRE_EQ(sem_unlink("/sem_b"), 0);
}
ATF_TC_CLEANUP(basic, tc)
{
	(void)sem_unlink("/sem_b");
}

ATF_TC_WITH_CLEANUP(child);
ATF_TC_HEAD(child, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks using semaphores to synchronize "
	    "parent with multiple child processes");
	atf_tc_set_md_var(tc, "timeout", "5");
}
ATF_TC_BODY(child, tc)
{
	pid_t children[NCHILDREN];
	unsigned i, j;
	sem_t *sem_a;
	int status;

	pid_t pid;

	if (sysconf(_SC_SEMAPHORES) == -1)
		atf_tc_skip("POSIX semaphores not supported");

	sem_a = sem_open("/sem_a", O_CREAT | O_EXCL, 0644, 0);
	ATF_REQUIRE(sem_a != SEM_FAILED);

	for (j = 1; j <= 2; j++) {
		for (i = 0; i < NCHILDREN; i++) {
			switch ((pid = fork())) {
			case -1:
				atf_tc_fail("fork() returned -1");
			case 0:
				printf("PID %d waiting for semaphore...\n",
				    getpid());
				ATF_REQUIRE_MSG(sem_wait(sem_a) == 0,
				    "sem_wait failed; iteration %d", j);
				printf("PID %d got semaphore\n", getpid());
				_exit(0);
			default:
				children[i] = pid;
				break;
			}
		}

		for (i = 0; i < NCHILDREN; i++) {
			usleep(100000);
			printf("main loop %d: posting...\n", j);
			ATF_REQUIRE_EQ(sem_post(sem_a), 0);
		}

		for (i = 0; i < NCHILDREN; i++) {
			ATF_REQUIRE_EQ(waitpid(children[i], &status, 0), children[i]);
			ATF_REQUIRE(WIFEXITED(status));
			ATF_REQUIRE_EQ(WEXITSTATUS(status), 0);
		}
	}

	ATF_REQUIRE_EQ(sem_close(sem_a), 0);
	ATF_REQUIRE_EQ(sem_unlink("/sem_a"), 0);
}
ATF_TC_CLEANUP(child, tc)
{
	(void)sem_unlink("/sem_a");
}

static inline void
timespec_add_ms(struct timespec *ts, int ms)
{
	ts->tv_nsec += ms * 1000*1000;
	if (ts->tv_nsec > 1000*1000*1000) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000*1000*1000;
	}
}

volatile sig_atomic_t got_sigalrm = 0;

static void
sigalrm_handler(int sig __unused)
{
	got_sigalrm = 1;
}

ATF_TC(timedwait);
ATF_TC_HEAD(timedwait, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests sem_timedwait(3)"
#ifdef __FreeBSD__
	    " and sem_clockwait_np(3)"
#endif
	    );
	atf_tc_set_md_var(tc, "timeout", "20");
}
ATF_TC_BODY(timedwait, tc)
{
	struct timespec ts;
	sem_t sem;
	int result;

	SEM_REQUIRE(sem_init(&sem, 0, 0));
	SEM_REQUIRE(sem_post(&sem));
	ATF_REQUIRE_MSG(clock_gettime(CLOCK_REALTIME, &ts) == 0,
	    "%s", strerror(errno));
        timespec_add_ms(&ts, 100);
	SEM_REQUIRE(sem_timedwait(&sem, &ts));
	ATF_REQUIRE_ERRNO(ETIMEDOUT, sem_timedwait(&sem, &ts));
        ts.tv_sec--;
	ATF_REQUIRE_ERRNO(ETIMEDOUT, sem_timedwait(&sem, &ts));
	SEM_REQUIRE(sem_post(&sem));
	SEM_REQUIRE(sem_timedwait(&sem, &ts));

	/* timespec validation, in the past */
	ts.tv_nsec += 1000*1000*1000;
	ATF_REQUIRE_ERRNO(EINVAL, sem_timedwait(&sem, &ts));
	ts.tv_nsec = -1;
	ATF_REQUIRE_ERRNO(EINVAL, sem_timedwait(&sem, &ts));
	/* timespec validation, in the future */
	ATF_REQUIRE_MSG(clock_gettime(CLOCK_REALTIME, &ts) == 0,
	    "%s", strerror(errno));
	ts.tv_sec++;
	ts.tv_nsec = 1000*1000*1000;
	ATF_REQUIRE_ERRNO(EINVAL, sem_timedwait(&sem, &ts));
	ts.tv_nsec = -1;
	ATF_REQUIRE_ERRNO(EINVAL, sem_timedwait(&sem, &ts));

	/* EINTR */
	struct sigaction act = {
		.sa_handler = sigalrm_handler,
		.sa_flags = 0	/* not SA_RESTART */
	};
	ATF_REQUIRE_MSG(sigemptyset(&act.sa_mask) == 0,
	    "%s", strerror(errno));
	ATF_REQUIRE_MSG(sigaction(SIGALRM, &act, NULL) == 0,
	    "%s", strerror(errno));
	struct itimerval it = {
		.it_value.tv_usec = 50*1000
	};
	ATF_REQUIRE_MSG(setitimer(ITIMER_REAL, &it, NULL) == 0,
	    "%s", strerror(errno));
	ATF_REQUIRE_MSG(clock_gettime(CLOCK_REALTIME, &ts) == 0,
	    "%s", strerror(errno));
        timespec_add_ms(&ts, 100);
	ATF_REQUIRE_ERRNO(EINTR, sem_timedwait(&sem, &ts));
	ATF_REQUIRE_MSG(got_sigalrm, "did not get SIGALRM");

#ifdef __FreeBSD__
	/* CLOCK_MONOTONIC, absolute */
	SEM_REQUIRE(sem_post(&sem));
	ATF_REQUIRE_MSG(clock_gettime(CLOCK_MONOTONIC, &ts) == 0,
	    "%s", strerror(errno));
        timespec_add_ms(&ts, 100);
	SEM_REQUIRE(sem_clockwait_np(&sem, CLOCK_MONOTONIC, TIMER_ABSTIME,
	    &ts, NULL));
	ATF_REQUIRE_ERRNO(ETIMEDOUT,
	    sem_clockwait_np(&sem, CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL));

	/* CLOCK_MONOTONIC, relative */
	SEM_REQUIRE(sem_post(&sem));
	ts.tv_sec = 0;
	ts.tv_nsec = 100*1000*1000;
	SEM_REQUIRE(sem_clockwait_np(&sem, CLOCK_MONOTONIC, 0,
	    &ts, NULL));
	ATF_REQUIRE_ERRNO(ETIMEDOUT,
	    sem_clockwait_np(&sem, CLOCK_MONOTONIC, 0, &ts, NULL));

	/* absolute does not update remaining time on EINTR */
	struct timespec remain = {42, 1000*1000*1000};
	got_sigalrm = 0;
	it.it_value.tv_usec = 50*1000;
	ATF_REQUIRE_MSG(setitimer(ITIMER_REAL, &it, NULL) == 0,
	    "%s", strerror(errno));
	ATF_REQUIRE_MSG(clock_gettime(CLOCK_MONOTONIC, &ts) == 0,
	    "%s", strerror(errno));
        timespec_add_ms(&ts, 100);
	ATF_REQUIRE_ERRNO(EINTR, sem_clockwait_np(&sem, CLOCK_MONOTONIC,
	    TIMER_ABSTIME, &ts, &remain));
	ATF_REQUIRE_MSG(got_sigalrm, "did not get SIGALRM");
	ATF_REQUIRE_MSG(remain.tv_sec == 42 && remain.tv_nsec == 1000*1000*1000,
	    "an absolute clockwait modified the remaining time on EINTR");

	/* relative updates remaining time on EINTR */
	remain.tv_sec = 42;
	remain.tv_nsec = 1000*1000*1000;
	got_sigalrm = 0;
	it.it_value.tv_usec = 50*1000;
	ATF_REQUIRE_MSG(setitimer(ITIMER_REAL, &it, NULL) == 0,
	    "%s", strerror(errno));
	ts.tv_sec = 0;
	ts.tv_nsec = 100*1000*1000;
	ATF_REQUIRE_ERRNO(EINTR, sem_clockwait_np(&sem, CLOCK_MONOTONIC, 0, &ts,
	    &remain));
	ATF_REQUIRE_MSG(got_sigalrm, "did not get SIGALRM");
	/*
	 * If this nsec comparison turns out to be unreliable due to timing,
	 * it could simply check that nsec < 100 ms.
	 */
	ATF_REQUIRE_MSG(remain.tv_sec == 0 &&
	    remain.tv_nsec >= 25*1000*1000 &&
	    remain.tv_nsec <= 75*1000*1000,
	    "the remaining time was not as expected when a relative clockwait"
	    " got EINTR" );
#endif
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, basic);
	ATF_TP_ADD_TC(tp, child);
	ATF_TP_ADD_TC(tp, timedwait);

	return atf_no_error();
}
