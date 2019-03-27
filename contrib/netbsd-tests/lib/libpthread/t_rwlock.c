/* $NetBSD: t_rwlock.c,v 1.2 2015/06/26 11:07:20 pooka Exp $ */

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

#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 2008\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_rwlock.c,v 1.2 2015/06/26 11:07:20 pooka Exp $");

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <atf-c.h>

#include "h_common.h"

pthread_rwlock_t lk;

struct timespec to;

static pthread_rwlock_t static_rwlock = PTHREAD_RWLOCK_INITIALIZER;

/* ARGSUSED */
static void *
do_nothing(void *dummy)
{
	return NULL;
}

ATF_TC(rwlock1);
ATF_TC_HEAD(rwlock1, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks read/write locks");
}
ATF_TC_BODY(rwlock1, tc)
{
	int error;
	pthread_t t;

	PTHREAD_REQUIRE(pthread_create(&t, NULL, do_nothing, NULL));
	PTHREAD_REQUIRE(pthread_rwlock_init(&lk, NULL));
	PTHREAD_REQUIRE(pthread_rwlock_rdlock(&lk));
	PTHREAD_REQUIRE(pthread_rwlock_rdlock(&lk));
	PTHREAD_REQUIRE(pthread_rwlock_unlock(&lk));

	ATF_REQUIRE_EQ(pthread_rwlock_trywrlock(&lk), EBUSY);

	ATF_REQUIRE_EQ_MSG(clock_gettime(CLOCK_REALTIME, &to), 0,
		"%s", strerror(errno));
	to.tv_sec++;
	error = pthread_rwlock_timedwrlock(&lk, &to);
	ATF_REQUIRE_MSG(error == ETIMEDOUT || error == EDEADLK,
		"%s", strerror(error));

	PTHREAD_REQUIRE(pthread_rwlock_unlock(&lk));

	ATF_REQUIRE_EQ_MSG(clock_gettime(CLOCK_REALTIME, &to), 0,
						"%s", strerror(errno));
	to.tv_sec++;
	PTHREAD_REQUIRE(pthread_rwlock_timedwrlock(&lk, &to));

	ATF_REQUIRE_EQ_MSG(clock_gettime(CLOCK_REALTIME, &to), 0,
						"%s", strerror(errno));
	to.tv_sec++;
	error = pthread_rwlock_timedwrlock(&lk, &to);
	ATF_REQUIRE_MSG(error == ETIMEDOUT || error == EDEADLK,
		"%s", strerror(error));
}

ATF_TC(rwlock_static);
ATF_TC_HEAD(rwlock_static, tc)
{
	atf_tc_set_md_var(tc, "descr", "rwlock w/ static initializer");
}
ATF_TC_BODY(rwlock_static, tc)
{

	PTHREAD_REQUIRE(pthread_rwlock_rdlock(&static_rwlock));
	PTHREAD_REQUIRE(pthread_rwlock_unlock(&static_rwlock));
	PTHREAD_REQUIRE(pthread_rwlock_destroy(&static_rwlock));
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, rwlock1);
	ATF_TP_ADD_TC(tp, rwlock_static);

	return atf_no_error();
}
