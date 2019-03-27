/*	$NetBSD: h_pthread_dlopen.c,v 1.1 2013/03/21 16:50:22 christos Exp $ */
/*-
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Emmanuel Dreyfus.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD: h_pthread_dlopen.c,v 1.1 2013/03/21 16:50:22 christos Exp $");

#if 0
#include <atf-c.h>
#else
#include <assert.h>
#define ATF_REQUIRE(a)	assert(a)
#endif
#include <unistd.h>
#include <pthread.h>

int testf_dso_null(void);
int testf_dso_mutex_lock(pthread_mutex_t *);
int testf_dso_mutex_unlock(pthread_mutex_t *);
int testf_dso_pthread_create(pthread_t *, const pthread_attr_t *, 
    void *(*)(void *), void *);

int
testf_dso_null(void)
{
	return 0xcafe;
}

int
testf_dso_mutex_lock(pthread_mutex_t *mtx)
{
	ATF_REQUIRE(mtx != NULL);
	ATF_REQUIRE(pthread_mutex_lock(mtx) == 0);

	return 0xcafe;
}

int
testf_dso_mutex_unlock(pthread_mutex_t *mtx)
{
	ATF_REQUIRE(mtx != NULL);
	ATF_REQUIRE(pthread_mutex_unlock(mtx) == 0);

	return 0xcafe;
}

int
testf_dso_pthread_create(pthread_t *thread, const pthread_attr_t *attr, 
    void *(*routine)(void *), void *arg)
{
	int ret;

	ret = pthread_create(thread, attr, routine, arg);
	ATF_REQUIRE(ret == 0);

	return 0;
}
