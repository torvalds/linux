/* $NetBSD: t_preempt.c,v 1.2 2010/11/03 16:10:22 christos Exp $ */

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
__RCSID("$NetBSD: t_preempt.c,v 1.2 2010/11/03 16:10:22 christos Exp $");

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

#include "h_common.h"

pthread_mutex_t mutex;
pthread_cond_t cond;
int started;

#define HUGE_BUFFER    1<<20
#define NTHREADS    1

static void *
threadfunc(void *arg)
{
	printf("2: Second thread.\n");

	printf("2: Locking mutex\n");
	PTHREAD_REQUIRE(pthread_mutex_lock(&mutex));
	printf("2: Got mutex.\n");
	started++;

	PTHREAD_REQUIRE(pthread_mutex_unlock(&mutex));
	PTHREAD_REQUIRE(pthread_cond_signal(&cond));
	sleep(1);

	return NULL;
}

ATF_TC(preempt1);
ATF_TC_HEAD(preempt1, tc)
{
	atf_tc_set_md_var(tc, "descr",
		"Checks kernel preemption during a large uiomove");
}
ATF_TC_BODY(preempt1, tc)
{
	int i;
	ssize_t rv;
	pthread_t new;
	void *joinval;

	char *mem;
	int fd;

	mem = malloc(HUGE_BUFFER);
	ATF_REQUIRE_MSG(mem != NULL, "%s", strerror(errno));

	fd = open("/dev/urandom", O_RDONLY, 0);
	ATF_REQUIRE_MSG(fd != -1, "%s", strerror(errno));

	printf("1: preempt test\n");

	PTHREAD_REQUIRE(pthread_cond_init(&cond, NULL));
	PTHREAD_REQUIRE(pthread_mutex_init(&mutex, NULL));

	PTHREAD_REQUIRE(pthread_mutex_lock(&mutex));

	started = 0;

	for (i = 0; i < NTHREADS; i++) {
		PTHREAD_REQUIRE(pthread_create(&new, NULL, threadfunc, NULL));
	}

	while (started < NTHREADS) {
		PTHREAD_REQUIRE(pthread_cond_wait(&cond, &mutex));
	}

	printf("1: Thread has started.\n");

	PTHREAD_REQUIRE(pthread_mutex_unlock(&mutex));
	printf("1: After releasing the mutex.\n");

	rv = read(fd, mem, HUGE_BUFFER);
	close(fd);
	ATF_REQUIRE_EQ(rv, HUGE_BUFFER);

	PTHREAD_REQUIRE(pthread_join(new, &joinval));

	printf("1: Thread joined.\n");
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, preempt1);

	return atf_no_error();
}
