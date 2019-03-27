/*	$NetBSD: t_dlopen.c,v 1.1 2013/03/21 16:50:21 christos Exp $ */
/*-
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Emmanuel Dreyfus
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
__RCSID("$NetBSD: t_dlopen.c,v 1.1 2013/03/21 16:50:21 christos Exp $");

#include <atf-c.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>

ATF_TC(dlopen);

ATF_TC_HEAD(dlopen, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test if dlopen can load -lpthread DSO");
}

#define DSO TESTDIR "/h_pthread_dlopen.so"

ATF_TC_BODY(dlopen, tc)
{
	void *handle;
	int (*testf_dso_null)(void);
	handle = dlopen(DSO, RTLD_NOW | RTLD_LOCAL);
	ATF_REQUIRE_MSG(handle != NULL, "dlopen fails: %s", dlerror());

	testf_dso_null = dlsym(handle, "testf_dso_null");
	ATF_REQUIRE_MSG(testf_dso_null != NULL, "dlsym fails: %s", dlerror());

	ATF_REQUIRE(testf_dso_null() == 0xcafe);

	ATF_REQUIRE(dlclose(handle) == 0);
}

ATF_TC(dlopen_mutex);

ATF_TC_HEAD(dlopen_mutex, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test if dlopen can load -lpthread DSO without breaking mutex");
}

ATF_TC_BODY(dlopen_mutex, tc)
{
	pthread_mutex_t mtx;
	void *handle;
	int (*testf_dso_null)(void);

	ATF_REQUIRE(pthread_mutex_init(&mtx, NULL) == 0);
	ATF_REQUIRE(pthread_mutex_lock(&mtx) == 0);

	handle = dlopen(DSO, RTLD_NOW | RTLD_LOCAL);
	ATF_REQUIRE_MSG(handle != NULL, "dlopen fails: %s", dlerror());

	testf_dso_null = dlsym(handle, "testf_dso_null");
	ATF_REQUIRE_MSG(testf_dso_null != NULL, "dlsym fails: %s", dlerror());

	ATF_REQUIRE(testf_dso_null() == 0xcafe);

	ATF_REQUIRE(pthread_mutex_unlock(&mtx) == 0);

	ATF_REQUIRE(dlclose(handle) == 0);

	pthread_mutex_destroy(&mtx);
}

ATF_TC(dlopen_mutex_libc);

ATF_TC_HEAD(dlopen_mutex_libc, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test if dlopen can load -lpthread DSO and use libc locked mutex");
}

ATF_TC_BODY(dlopen_mutex_libc, tc)
{
	pthread_mutex_t mtx;
	void *handle;
	int (*testf_dso_mutex_unlock)(pthread_mutex_t *);

	ATF_REQUIRE(pthread_mutex_init(&mtx, NULL) == 0);
	ATF_REQUIRE(pthread_mutex_lock(&mtx) == 0);

	handle = dlopen(DSO, RTLD_NOW | RTLD_LOCAL);
	ATF_REQUIRE_MSG(handle != NULL, "dlopen fails: %s", dlerror());

	testf_dso_mutex_unlock = dlsym(handle, "testf_dso_mutex_unlock");
	ATF_REQUIRE_MSG(testf_dso_mutex_unlock != NULL,
			"dlsym fails: %s", dlerror());

	ATF_REQUIRE(testf_dso_mutex_unlock(&mtx) == 0xcafe);

	dlclose(handle);

	pthread_mutex_destroy(&mtx);
}

ATF_TC(dlopen_mutex_libpthread);

ATF_TC_HEAD(dlopen_mutex_libpthread, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test if dlopen can load -lpthread DSO and use "
	    "libpthread locked mutex");
}

ATF_TC_BODY(dlopen_mutex_libpthread, tc)
{
	pthread_mutex_t mtx;
	void *handle;
	int (*testf_dso_mutex_lock)(pthread_mutex_t *);

	ATF_REQUIRE(pthread_mutex_init(&mtx, NULL) == 0);

	handle = dlopen(DSO, RTLD_NOW | RTLD_LOCAL);
	ATF_REQUIRE_MSG(handle != NULL, "dlopen fails: %s", dlerror());

	testf_dso_mutex_lock = dlsym(handle, "testf_dso_mutex_lock");
	ATF_REQUIRE_MSG(testf_dso_mutex_lock != NULL,
			"dlsym fails: %s", dlerror());

	ATF_REQUIRE(testf_dso_mutex_lock(&mtx) == 0xcafe);

	ATF_REQUIRE(pthread_mutex_unlock(&mtx) == 0);

	dlclose(handle);

	pthread_mutex_destroy(&mtx);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, dlopen);
	ATF_TP_ADD_TC(tp, dlopen_mutex);
	ATF_TP_ADD_TC(tp, dlopen_mutex_libc);
	ATF_TP_ADD_TC(tp, dlopen_mutex_libpthread);

	return atf_no_error();
}
