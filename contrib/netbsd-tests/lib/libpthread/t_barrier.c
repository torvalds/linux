/* $NetBSD: t_barrier.c,v 1.2 2010/11/03 16:10:22 christos Exp $ */

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
__RCSID("$NetBSD: t_barrier.c,v 1.2 2010/11/03 16:10:22 christos Exp $");

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include <atf-c.h>

#include "h_common.h"

#define COUNT 5

pthread_barrier_t barrier;
pthread_mutex_t mutex;
int serial_count;
int after_barrier_count;

static void *
threadfunc(void *arg)
{
	int which = (int)(long)arg;
	int rv;

	printf("thread %d entering barrier\n", which);
	rv = pthread_barrier_wait(&barrier);
	printf("thread %d leaving barrier -> %d\n", which, rv);

	PTHREAD_REQUIRE(pthread_mutex_lock(&mutex));
	after_barrier_count++;
	if (rv == PTHREAD_BARRIER_SERIAL_THREAD)
		serial_count++;
	PTHREAD_REQUIRE(pthread_mutex_unlock(&mutex));

	return NULL;
}

ATF_TC(barrier);
ATF_TC_HEAD(barrier, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks barriers");
}
ATF_TC_BODY(barrier, tc)
{
	int i;
	pthread_t new[COUNT];
	void *joinval;

	PTHREAD_REQUIRE(pthread_mutex_init(&mutex, NULL));
	PTHREAD_REQUIRE(pthread_barrier_init(&barrier, NULL, COUNT));

	for (i = 0; i < COUNT; i++) {
		PTHREAD_REQUIRE(pthread_mutex_lock(&mutex));
		ATF_REQUIRE_EQ(after_barrier_count, 0);
		PTHREAD_REQUIRE(pthread_mutex_unlock(&mutex));
		PTHREAD_REQUIRE(pthread_create(&new[i], NULL, threadfunc,
						(void *)(long)i));
		sleep(2);
	}

	for (i = 0; i < COUNT; i++) {
		PTHREAD_REQUIRE(pthread_join(new[i], &joinval));
		PTHREAD_REQUIRE(pthread_mutex_lock(&mutex));
		ATF_REQUIRE(after_barrier_count > i);
		PTHREAD_REQUIRE(pthread_mutex_unlock(&mutex));
		printf("main joined with thread %d\n", i);
	}

	PTHREAD_REQUIRE(pthread_mutex_lock(&mutex));
	ATF_REQUIRE_EQ(after_barrier_count, COUNT);
	PTHREAD_REQUIRE(pthread_mutex_unlock(&mutex));
	ATF_REQUIRE_EQ(serial_count, 1);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, barrier);

	return atf_no_error();
}
