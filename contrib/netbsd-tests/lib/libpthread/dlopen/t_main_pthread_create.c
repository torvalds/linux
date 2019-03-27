/*	$NetBSD: t_main_pthread_create.c,v 1.1 2013/03/21 16:50:21 christos Exp $ */
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
__RCSID("$NetBSD: t_main_pthread_create.c,v 1.1 2013/03/21 16:50:21 christos Exp $");

#include <atf-c.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>

#define DSO TESTDIR "/h_pthread_dlopen.so"

void *
routine(void *arg)
{
	ATF_REQUIRE((intptr_t)arg == 0xcafe);
	return NULL;
}

ATF_TC(main_pthread_create_main);

ATF_TC_HEAD(main_pthread_create_main, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test if -lpthread main can call pthread_create() in main()");
}

ATF_TC_BODY(main_pthread_create_main, tc)
{
	int ret;
	pthread_t thread;
	void *arg = (void *)0xcafe;

	ret = pthread_create(&thread, NULL, routine, arg);
	ATF_REQUIRE(ret == 0);
}

ATF_TC(main_pthread_create_dso);

ATF_TC_HEAD(main_pthread_create_dso, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test if -lpthread main can call pthread_create() in DSO");
}

ATF_TC_BODY(main_pthread_create_dso, tc)
{
	int ret;
	pthread_t thread;
	void *arg = (void *)0xcafe;
	void *handle;
	int (*testf_dso_pthread_create)(pthread_t *, pthread_attr_t *, 
	    void *(*)(void *), void *);

	handle = dlopen(DSO, RTLD_NOW | RTLD_LOCAL);
	ATF_REQUIRE_MSG(handle != NULL, "dlopen fails: %s", dlerror());

	testf_dso_pthread_create = dlsym(handle, "testf_dso_pthread_create");
	ATF_REQUIRE_MSG(testf_dso_pthread_create != NULL, 
	    "dlsym fails: %s", dlerror());

	ret = testf_dso_pthread_create(&thread, NULL, routine, arg);
	ATF_REQUIRE(ret == 0);

	ATF_REQUIRE(dlclose(handle) == 0);

}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, main_pthread_create_main);
	ATF_TP_ADD_TC(tp, main_pthread_create_dso);

	return atf_no_error();
}
