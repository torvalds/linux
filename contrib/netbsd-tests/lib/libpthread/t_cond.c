/* $NetBSD: t_cond.c,v 1.7 2016/07/03 14:24:59 christos Exp $ */

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
__RCSID("$NetBSD: t_cond.c,v 1.7 2016/07/03 14:24:59 christos Exp $");

#include <sys/time.h>

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include <atf-c.h>

#include "h_common.h"

static pthread_mutex_t mutex;
static pthread_cond_t cond;
static pthread_mutex_t static_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t static_cond = PTHREAD_COND_INITIALIZER;
static int count, share, toggle, total;

static void *
signal_delay_wait_threadfunc(void *arg)
{
	int *shared = (int *) arg;

	printf("2: Second thread.\n");

	printf("2: Locking mutex\n");
	PTHREAD_REQUIRE(pthread_mutex_lock(&mutex));
	printf("2: Got mutex.\n");
	printf("Shared value: %d. Changing to 0.\n", *shared);
	*shared = 0;

	PTHREAD_REQUIRE(pthread_mutex_unlock(&mutex));
	PTHREAD_REQUIRE(pthread_cond_signal(&cond));

	return NULL;
}

ATF_TC(signal_delay_wait);
ATF_TC_HEAD(signal_delay_wait, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks condition variables");
}
ATF_TC_BODY(signal_delay_wait, tc)
{
	pthread_t new;
	void *joinval;
	int sharedval;

	printf("1: condition variable test 1\n");

	PTHREAD_REQUIRE(pthread_mutex_init(&mutex, NULL));
	PTHREAD_REQUIRE(pthread_cond_init(&cond, NULL));

	PTHREAD_REQUIRE(pthread_mutex_lock(&mutex));

	sharedval = 1;

	PTHREAD_REQUIRE(pthread_create(&new, NULL, signal_delay_wait_threadfunc,
	    &sharedval));

	printf("1: Before waiting.\n");
	do {
		sleep(2);
		PTHREAD_REQUIRE(pthread_cond_wait(&cond, &mutex));
		printf("1: After waiting, in loop.\n");
	} while (sharedval != 0);

	printf("1: After the loop.\n");

	PTHREAD_REQUIRE(pthread_mutex_unlock(&mutex));

	printf("1: After releasing the mutex.\n");
	PTHREAD_REQUIRE(pthread_join(new, &joinval));

	printf("1: Thread joined.\n");
}

static void *
signal_before_unlock_threadfunc(void *arg)
{
	int *shared = (int *) arg;

	printf("2: Second thread.\n");

	printf("2: Locking mutex\n");
	PTHREAD_REQUIRE(pthread_mutex_lock(&mutex));
	printf("2: Got mutex.\n");
	printf("Shared value: %d. Changing to 0.\n", *shared);
	*shared = 0;

	/* Signal first, then unlock, for a different test than #1. */
	PTHREAD_REQUIRE(pthread_cond_signal(&cond));
	PTHREAD_REQUIRE(pthread_mutex_unlock(&mutex));

	return NULL;
}

ATF_TC(signal_before_unlock);
ATF_TC_HEAD(signal_before_unlock, tc)
{
	atf_tc_set_md_var(tc, "descr",
		"Checks condition variables: signal before unlocking mutex");
}
ATF_TC_BODY(signal_before_unlock, tc)
{
	pthread_t new;
	void *joinval;
	int sharedval;

	printf("1: condition variable test 2\n");

	PTHREAD_REQUIRE(pthread_mutex_init(&mutex, NULL));
	PTHREAD_REQUIRE(pthread_cond_init(&cond, NULL));

	PTHREAD_REQUIRE(pthread_mutex_lock(&mutex));

	sharedval = 1;

	PTHREAD_REQUIRE(pthread_create(&new, NULL,
	    signal_before_unlock_threadfunc, &sharedval));

	printf("1: Before waiting.\n");
	do {
		sleep(2);
		PTHREAD_REQUIRE(pthread_cond_wait(&cond, &mutex));
		printf("1: After waiting, in loop.\n");
	} while (sharedval != 0);

	printf("1: After the loop.\n");

	PTHREAD_REQUIRE(pthread_mutex_unlock(&mutex));

	printf("1: After releasing the mutex.\n");
	PTHREAD_REQUIRE(pthread_join(new, &joinval));

	printf("1: Thread joined.\n");
}

static void *
signal_before_unlock_static_init_threadfunc(void *arg)
{
	int *shared = (int *) arg;

	printf("2: Second thread.\n");

	printf("2: Locking mutex\n");
	PTHREAD_REQUIRE(pthread_mutex_lock(&static_mutex));
	printf("2: Got mutex.\n");
	printf("Shared value: %d. Changing to 0.\n", *shared);
	*shared = 0;

	/* Signal first, then unlock, for a different test than #1. */
	PTHREAD_REQUIRE(pthread_cond_signal(&static_cond));
	PTHREAD_REQUIRE(pthread_mutex_unlock(&static_mutex));

	return NULL;
}

ATF_TC(signal_before_unlock_static_init);
ATF_TC_HEAD(signal_before_unlock_static_init, tc)
{
	atf_tc_set_md_var(tc, "descr",
		"Checks condition variables: signal before unlocking "
		"mutex, use static initializers");
}
ATF_TC_BODY(signal_before_unlock_static_init, tc)
{
	pthread_t new;
	void *joinval;
	int sharedval;

	printf("1: condition variable test 3\n");

	PTHREAD_REQUIRE(pthread_mutex_lock(&static_mutex));

	sharedval = 1;

	PTHREAD_REQUIRE(pthread_create(&new, NULL,
	    signal_before_unlock_static_init_threadfunc, &sharedval));

	printf("1: Before waiting.\n");
	do {
		sleep(2);
		PTHREAD_REQUIRE(pthread_cond_wait(&static_cond, &static_mutex));
		printf("1: After waiting, in loop.\n");
	} while (sharedval != 0);

	printf("1: After the loop.\n");

	PTHREAD_REQUIRE(pthread_mutex_unlock(&static_mutex));

	printf("1: After releasing the mutex.\n");
	PTHREAD_REQUIRE(pthread_join(new, &joinval));

	printf("1: Thread joined.\n");
}

static void *
signal_wait_race_threadfunc(void *arg)
{
	printf("2: Second thread.\n");
	PTHREAD_REQUIRE(pthread_mutex_lock(&static_mutex));
	printf("2: Before the loop.\n");
	while (count>0) {
		count--;
		total++;
		toggle = 0;
		/* printf("2: Before signal %d.\n", count); */
		PTHREAD_REQUIRE(pthread_cond_signal(&static_cond));
		do {
			PTHREAD_REQUIRE(pthread_cond_wait(&static_cond,
			    &static_mutex));
		} while (toggle != 1);
	}
	printf("2: After the loop.\n");
	PTHREAD_REQUIRE(pthread_mutex_unlock(&static_mutex));

	return NULL;
}

ATF_TC(signal_wait_race);
ATF_TC_HEAD(signal_wait_race, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks condition variables");
}
ATF_TC_BODY(signal_wait_race, tc)
{
	pthread_t new;
	void *joinval;
	int sharedval;

	printf("1: condition variable test 4\n");

	PTHREAD_REQUIRE(pthread_mutex_lock(&static_mutex));

	count = 50000;
	toggle = 0;

	PTHREAD_REQUIRE(pthread_create(&new, NULL, signal_wait_race_threadfunc,
	    &sharedval));

	printf("1: Before waiting.\n");
	while (count>0) {
		count--;
		total++;
		toggle = 1;
		/* printf("1: Before signal %d.\n", count); */
		PTHREAD_REQUIRE(pthread_cond_signal(&static_cond));
		do {
			PTHREAD_REQUIRE(pthread_cond_wait(&static_cond,
			    &static_mutex));
		} while (toggle != 0);
	}
	printf("1: After the loop.\n");

	toggle = 1;
	PTHREAD_REQUIRE(pthread_mutex_unlock(&static_mutex));
	PTHREAD_REQUIRE(pthread_cond_signal(&static_cond));

	printf("1: After releasing the mutex.\n");
	PTHREAD_REQUIRE(pthread_join(new, &joinval));

	printf("1: Thread joined. Final count = %d, total = %d\n",
		count, total);

	ATF_REQUIRE_EQ(count, 0);
	ATF_REQUIRE_EQ(total, 50000);
}

static void *
pthread_cond_timedwait_func(void *arg)
{
	struct timespec ts;
	size_t i = 0;
	int rv;

	for (;;) {

		if (i++ >= 10000)
			pthread_exit(NULL);

		(void)memset(&ts, 0, sizeof(struct timespec));

		ATF_REQUIRE(clock_gettime(CLOCK_REALTIME, &ts) == 0);

		/*
		 * Set to one second in the past:
		 * pthread_cond_timedwait(3) should
		 * return ETIMEDOUT immediately.
		 */
		ts.tv_sec = ts.tv_sec - 1;

		PTHREAD_REQUIRE(pthread_mutex_lock(&static_mutex));
		rv = pthread_cond_timedwait(&static_cond, &static_mutex, &ts);

		/*
		 * Sometimes we catch ESRCH.
		 * This should never happen.
		 */
		ATF_REQUIRE(rv == ETIMEDOUT);
		PTHREAD_REQUIRE(pthread_mutex_unlock(&static_mutex));
	}
}

ATF_TC(cond_timedwait_race);
ATF_TC_HEAD(cond_timedwait_race, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test pthread_cond_timedwait(3)");

}
ATF_TC_BODY(cond_timedwait_race, tc)
{
	pthread_t tid[64];
	size_t i;

	for (i = 0; i < __arraycount(tid); i++) {

		PTHREAD_REQUIRE(pthread_create(&tid[i], NULL,
		    pthread_cond_timedwait_func, NULL));
	}

	for (i = 0; i < __arraycount(tid); i++) {

		PTHREAD_REQUIRE(pthread_join(tid[i], NULL));
	}
}

static void *
broadcast_threadfunc(void *arg)
{
	printf("2: Second thread.\n");

	PTHREAD_REQUIRE(pthread_mutex_lock(&static_mutex));
	while (count>0) {
		count--;
		total++;
		toggle = 0;
		PTHREAD_REQUIRE(pthread_cond_signal(&static_cond));
		do {
			PTHREAD_REQUIRE(pthread_cond_wait(&static_cond,
			    &static_mutex));
		} while (toggle != 1);
	}
	printf("2: After the loop.\n");
	PTHREAD_REQUIRE(pthread_mutex_unlock(&static_mutex));

	return NULL;
}


ATF_TC(broadcast);
ATF_TC_HEAD(broadcast, tc)
{
	atf_tc_set_md_var(tc, "descr",
		"Checks condition variables: use pthread_cond_broadcast()");
}
ATF_TC_BODY(broadcast, tc)
{
	pthread_t new;
	void *joinval;
	int sharedval;

	printf("1: condition variable test 5\n");

	PTHREAD_REQUIRE(pthread_mutex_lock(&static_mutex));

	count = 50000;
	toggle = 0;

	PTHREAD_REQUIRE(pthread_create(&new, NULL, broadcast_threadfunc,
	    &sharedval));

	printf("1: Before waiting.\n");
	while (count>0) {
		count--;
		total++;
		toggle = 1;
		PTHREAD_REQUIRE(pthread_cond_broadcast(&static_cond));
		do {
			PTHREAD_REQUIRE(pthread_cond_wait(&static_cond,
			    &static_mutex));
		} while (toggle != 0);
	}
	printf("1: After the loop.\n");

	toggle = 1;
	PTHREAD_REQUIRE(pthread_mutex_unlock(&static_mutex));
	PTHREAD_REQUIRE(pthread_cond_signal(&static_cond));

	printf("1: After releasing the mutex.\n");
	PTHREAD_REQUIRE(pthread_join(new, &joinval));

	printf("1: Thread joined. Final count = %d, total = %d\n", count,
	    total);

	ATF_REQUIRE_EQ(count, 0);
	ATF_REQUIRE_EQ(total, 50000);
}

static void *
bogus_timedwaits_threadfunc(void *arg)
{
	return NULL;
}

ATF_TC(bogus_timedwaits);
ATF_TC_HEAD(bogus_timedwaits, tc)
{
	atf_tc_set_md_var(tc, "descr",
		"Checks condition variables: bogus timedwaits");
}
ATF_TC_BODY(bogus_timedwaits, tc)
{
	pthread_t new;
	struct timespec ts;
	struct timeval tv;

	printf("condition variable test 6: bogus timedwaits\n");

	PTHREAD_REQUIRE(pthread_mutex_lock(&static_mutex));

	printf("unthreaded test (past)\n");
	gettimeofday(&tv, NULL);
	tv.tv_sec -= 2; /* Place the time in the past */
	TIMEVAL_TO_TIMESPEC(&tv, &ts);

	ATF_REQUIRE_EQ_MSG(pthread_cond_timedwait(&static_cond, &static_mutex,
	    &ts), ETIMEDOUT, "pthread_cond_timedwait() (unthreaded) in the "
	    "past");

	printf("unthreaded test (zero time)\n");
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	TIMEVAL_TO_TIMESPEC(&tv, &ts);

	ATF_REQUIRE_EQ_MSG(pthread_cond_timedwait(&static_cond, &static_mutex,
	    &ts), ETIMEDOUT, "pthread_cond_timedwait() (unthreaded) with zero "
	    "time");

	PTHREAD_REQUIRE(pthread_create(&new, NULL, bogus_timedwaits_threadfunc,
	    NULL));
	PTHREAD_REQUIRE(pthread_join(new, NULL));

	printf("threaded test\n");
	gettimeofday(&tv, NULL);
	tv.tv_sec -= 2; /* Place the time in the past */
	TIMEVAL_TO_TIMESPEC(&tv, &ts);

	ATF_REQUIRE_EQ_MSG(pthread_cond_timedwait(&static_cond, &static_mutex,
	    &ts), ETIMEDOUT, "pthread_cond_timedwait() (threaded) in the past");

	printf("threaded test (zero time)\n");
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	TIMEVAL_TO_TIMESPEC(&tv, &ts);

	ATF_REQUIRE_EQ_MSG(pthread_cond_timedwait(&static_cond, &static_mutex,
	    &ts), ETIMEDOUT, "pthread_cond_timedwait() (threaded) with zero "
	    "time");

	PTHREAD_REQUIRE(pthread_mutex_unlock(&static_mutex));
}

#ifdef __FreeBSD__
static void *
destroy_busy_threadfunc(void *arg)
{
	PTHREAD_REQUIRE(pthread_mutex_lock(&mutex));

	share = 1;
	PTHREAD_REQUIRE(pthread_cond_broadcast(&cond));
	PTHREAD_REQUIRE(pthread_cond_wait(&cond, &mutex));

	PTHREAD_REQUIRE(pthread_mutex_unlock(&mutex));

	return NULL;
}

ATF_TC(destroy_busy);
ATF_TC_HEAD(destroy_busy, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks non-standard behaviour of "
	    "returning EBUSY when attempting to destroy an active condvar");
}
ATF_TC_BODY(destroy_busy, tc)
{
	pthread_t thread;

	PTHREAD_REQUIRE(pthread_mutex_init(&mutex, NULL));
	PTHREAD_REQUIRE(pthread_cond_init(&cond, NULL));
	PTHREAD_REQUIRE(pthread_mutex_lock(&mutex));
	PTHREAD_REQUIRE(pthread_create(&thread, NULL, destroy_busy_threadfunc,
	    NULL));

	while (share == 0) {
		PTHREAD_REQUIRE(pthread_cond_wait(&cond, &mutex));
	}

	PTHREAD_REQUIRE_STATUS(pthread_cond_destroy(&cond), EBUSY);
	PTHREAD_REQUIRE(pthread_cond_signal(&cond));
	PTHREAD_REQUIRE(pthread_cond_destroy(&cond));

	PTHREAD_REQUIRE(pthread_mutex_unlock(&mutex));
	PTHREAD_REQUIRE(pthread_join(thread, NULL));
	PTHREAD_REQUIRE(pthread_mutex_destroy(&mutex));
}
#endif

static void
unlock(void *arg)
{
	pthread_mutex_unlock((pthread_mutex_t *)arg);
}

static void *
destroy_after_cancel_threadfunc(void *arg)
{
	PTHREAD_REQUIRE(pthread_mutex_lock(&mutex));

	pthread_cleanup_push(unlock, &mutex);

	while (1) {
		share = 1;
		PTHREAD_REQUIRE(pthread_cond_broadcast(&cond));
		PTHREAD_REQUIRE(pthread_cond_wait(&cond, &mutex));
	}

	pthread_cleanup_pop(0);
	PTHREAD_REQUIRE(pthread_mutex_unlock(&mutex));

	return NULL;
}

ATF_TC(destroy_after_cancel);
ATF_TC_HEAD(destroy_after_cancel, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks destroying a condition variable "
	    "after cancelling a wait");
}
ATF_TC_BODY(destroy_after_cancel, tc)
{
	pthread_t thread;

	PTHREAD_REQUIRE(pthread_mutex_init(&mutex, NULL));
	PTHREAD_REQUIRE(pthread_cond_init(&cond, NULL));
	PTHREAD_REQUIRE(pthread_mutex_lock(&mutex));
	PTHREAD_REQUIRE(pthread_create(&thread, NULL,
	    destroy_after_cancel_threadfunc, NULL));

	while (share == 0) {
		PTHREAD_REQUIRE(pthread_cond_wait(&cond, &mutex));
	}

	PTHREAD_REQUIRE(pthread_mutex_unlock(&mutex));
	PTHREAD_REQUIRE(pthread_cancel(thread));

	PTHREAD_REQUIRE(pthread_join(thread, NULL));
	PTHREAD_REQUIRE(pthread_cond_destroy(&cond));

	PTHREAD_REQUIRE(pthread_mutex_destroy(&mutex));
}

static void *
destroy_after_signal_threadfunc(void *arg)
{
	PTHREAD_REQUIRE(pthread_mutex_lock(&mutex));

	share = 1;
	PTHREAD_REQUIRE(pthread_cond_broadcast(&cond));
	PTHREAD_REQUIRE(pthread_cond_wait(&cond, &mutex));

	PTHREAD_REQUIRE(pthread_mutex_unlock(&mutex));

	return NULL;
}

ATF_TC(destroy_after_signal);
ATF_TC_HEAD(destroy_after_signal, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks destroying a condition variable "
	    "immediately after signaling waiters");
}
ATF_TC_BODY(destroy_after_signal, tc)
{
	pthread_t thread;

	PTHREAD_REQUIRE(pthread_mutex_init(&mutex, NULL));
	PTHREAD_REQUIRE(pthread_cond_init(&cond, NULL));
	PTHREAD_REQUIRE(pthread_mutex_lock(&mutex));
	PTHREAD_REQUIRE(pthread_create(&thread, NULL,
	    destroy_after_signal_threadfunc, NULL));

	while (share == 0) {
		PTHREAD_REQUIRE(pthread_cond_wait(&cond, &mutex));
	}

	PTHREAD_REQUIRE(pthread_cond_signal(&cond));
	PTHREAD_REQUIRE(pthread_cond_destroy(&cond));
	PTHREAD_REQUIRE(pthread_mutex_unlock(&mutex));

	PTHREAD_REQUIRE(pthread_join(thread, NULL));

	PTHREAD_REQUIRE(pthread_mutex_destroy(&mutex));
}

ATF_TC(condattr);
ATF_TC_HEAD(condattr, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks Condattr");
}
ATF_TC_BODY(condattr, tc)
{
	pthread_condattr_t condattr;
	clockid_t clockid;

	PTHREAD_REQUIRE(pthread_condattr_init(&condattr));
	PTHREAD_REQUIRE(pthread_condattr_setclock(&condattr, CLOCK_REALTIME));
	PTHREAD_REQUIRE(pthread_condattr_getclock(&condattr, &clockid));
	ATF_REQUIRE_EQ(clockid, CLOCK_REALTIME);
	
	PTHREAD_REQUIRE(pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC));
	PTHREAD_REQUIRE(pthread_condattr_getclock(&condattr, &clockid));	
 	ATF_REQUIRE_EQ(clockid, CLOCK_MONOTONIC);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, signal_delay_wait);
	ATF_TP_ADD_TC(tp, signal_before_unlock);
	ATF_TP_ADD_TC(tp, signal_before_unlock_static_init);
	ATF_TP_ADD_TC(tp, signal_wait_race);
	ATF_TP_ADD_TC(tp, cond_timedwait_race);
	ATF_TP_ADD_TC(tp, broadcast);
	ATF_TP_ADD_TC(tp, bogus_timedwaits);
#ifdef __FreeBSD__
	ATF_TP_ADD_TC(tp, destroy_busy);
#endif
	ATF_TP_ADD_TC(tp, destroy_after_cancel);
	ATF_TP_ADD_TC(tp, destroy_after_signal);
	ATF_TP_ADD_TC(tp, condattr);

	return atf_no_error();
}
