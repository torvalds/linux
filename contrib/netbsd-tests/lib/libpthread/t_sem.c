/* $NetBSD: t_sem.c,v 1.9 2017/01/16 16:22:22 christos Exp $ */

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

/*-
 * Copyright (c)2004 YAMAMOTO Takashi,
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/****************************************************************************
 *
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
 *
 ****************************************************************************/

#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 2008, 2010\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_sem.c,v 1.9 2017/01/16 16:22:22 christos Exp $");

#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

#include "h_common.h"

#define NTHREADS 10

#define _LIBC_R_

#define SEM_REQUIRE(x) \
	ATF_REQUIRE_EQ_MSG(x, 0, "%s", strerror(errno))

static sem_t sem;

ATF_TC(named);
ATF_TC_HEAD(named, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks named semaphores");
}
ATF_TC_BODY(named, tc)
{
	sem_t *semp;

	ATF_REQUIRE_MSG(-1 != sysconf(_SC_SEMAPHORES), "%s", strerror(errno));

	printf("Test begin\n");

	(void) sem_unlink("/foo");
	semp = sem_open("/foo", O_CREAT | O_EXCL, 0644, 0);
	ATF_REQUIRE_MSG(semp != SEM_FAILED, "%s", strerror(errno));
	SEM_REQUIRE(sem_close(semp));
	SEM_REQUIRE(sem_unlink("/foo"));

	printf("Test end\n");
}

ATF_TC(unnamed);
ATF_TC_HEAD(unnamed, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks unnamed semaphores");
}

static void *
entry(void * a_arg)
{
	pthread_t self = pthread_self();
	sem_t *semp = (sem_t *) a_arg;

	printf("Thread %p waiting for semaphore...\n", self);
	sem_wait(semp);
	printf("Thread %p got semaphore\n", self);

	return NULL;
}

ATF_TC_BODY(unnamed, tc)
{
	sem_t sem_a, sem_b;
	pthread_t threads[NTHREADS];
	unsigned i, j;
	int val;

	ATF_REQUIRE_MSG(-1 != sysconf(_SC_SEMAPHORES), "%s", strerror(errno));

	printf("Test begin\n");

	SEM_REQUIRE(sem_init(&sem_b, 0, 0));
	SEM_REQUIRE(sem_getvalue(&sem_b, &val));
	ATF_REQUIRE_EQ(0, val);

	SEM_REQUIRE(sem_post(&sem_b));
	SEM_REQUIRE(sem_getvalue(&sem_b, &val));
	ATF_REQUIRE_EQ(1, val);

	SEM_REQUIRE(sem_wait(&sem_b));
	ATF_REQUIRE_EQ(sem_trywait(&sem_b), -1);
	ATF_REQUIRE_EQ(errno, EAGAIN);
	SEM_REQUIRE(sem_post(&sem_b));
	SEM_REQUIRE(sem_trywait(&sem_b));
	SEM_REQUIRE(sem_post(&sem_b));
	SEM_REQUIRE(sem_wait(&sem_b));
	SEM_REQUIRE(sem_post(&sem_b));

	SEM_REQUIRE(sem_destroy(&sem_b));

	SEM_REQUIRE(sem_init(&sem_a, 0, 0));

	for (j = 0; j < 2; j++) {
		for (i = 0; i < NTHREADS; i++) {
			PTHREAD_REQUIRE(pthread_create(&threads[i], NULL,
				entry, (void *) &sem_a));
		}

		for (i = 0; i < NTHREADS; i++) {
			usleep(10000);
			printf("main loop %u: posting...\n", j+1);
			SEM_REQUIRE(sem_post(&sem_a));
		}

		for (i = 0; i < NTHREADS; i++) {
			PTHREAD_REQUIRE(pthread_join(threads[i], NULL));
		}
	}

	SEM_REQUIRE(sem_destroy(&sem_a));

	printf("Test end\n");
}

static void
sighandler(int signo)
{
	/* printf("signal %d\n", signo); */

	ATF_REQUIRE_EQ_MSG(signo, SIGALRM, "unexpected signal");
	SEM_REQUIRE(sem_post(&sem));
}

static void
alarm_ms(const int ms)
{
	struct itimerval timer;
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_usec = 0;
	timer.it_value.tv_sec = 0;
	timer.it_value.tv_usec = ms * 1000;
	ATF_REQUIRE(setitimer(ITIMER_REAL, &timer, NULL) == 0);
}

static void *
threadfunc(void *arg)
{
	int i, ret;

	printf("Entering loop\n");
	for (i = 0; i < 500; ) {
		if ((i & 1) != 0) {
			do {
				ret = sem_wait(&sem);
			} while (ret == -1 && errno == EINTR);
			ATF_REQUIRE(ret == 0);
		} else {
			ret = sem_trywait(&sem);
			if (ret == -1) {
				ATF_REQUIRE(errno == EAGAIN);
				continue;
			}
		}
		printf("%s: %d\n", __func__, i);
		alarm_ms(5);
		i++;
	}

	return NULL;
}

static void
before_start_test(const bool use_pthread)
{
	pthread_t t;

	SEM_REQUIRE(sem_init(&sem, 0, 0));
	ATF_REQUIRE(SIG_ERR != signal(SIGALRM, sighandler));

	alarm_ms(5);

	if (use_pthread) {
		PTHREAD_REQUIRE(pthread_create(&t, NULL, threadfunc, NULL));
		PTHREAD_REQUIRE(pthread_join(t, NULL));
	} else {
		threadfunc(NULL);
	}
}

ATF_TC(before_start_no_threads);
ATF_TC_HEAD(before_start_no_threads, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks using semaphores without any "
	    "thread running");
	atf_tc_set_md_var(tc, "timeout", "40");
}
ATF_TC_BODY(before_start_no_threads, tc)
{
	before_start_test(false);
}

ATF_TC(before_start_one_thread);
ATF_TC_HEAD(before_start_one_thread, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks using semaphores before "
	    "starting one thread");
	atf_tc_set_md_var(tc, "timeout", "40");
}
ATF_TC_BODY(before_start_one_thread, tc)
{
	before_start_test(true);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, named);
	ATF_TP_ADD_TC(tp, unnamed);
	ATF_TP_ADD_TC(tp, before_start_no_threads);
	ATF_TP_ADD_TC(tp, before_start_one_thread);

	return atf_no_error();
}
