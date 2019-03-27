/*	$NetBSD: t_dlvsym.c,v 1.1 2011/06/25 05:45:13 nonaka Exp $	*/

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by NONAKA Kimihiro.
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

#include <sys/types.h>

#include <atf-c.h>
#include <stdlib.h>
#include <dlfcn.h>

ATF_TC(rtld_dlvsym_v1);
ATF_TC_HEAD(rtld_dlvsym_v1, tc)
{
	atf_tc_set_md_var(tc, "descr", "Check dlvsym() function (V_1)");
}
ATF_TC_BODY(rtld_dlvsym_v1, tc)
{
	void *handle;
	char *error;
	int (*sym)(void);
	int result;

	/* Clear previous error */
	(void) dlerror();

	handle = dlopen("libh_helper_symver_dso.so", RTLD_LAZY);
	error = dlerror();
	ATF_CHECK(error == NULL);
	ATF_CHECK(handle != NULL);

	sym = dlvsym(handle, "testfunc", "V_1");
	error = dlerror();
	ATF_CHECK(error == NULL);

	result = (*sym)();
	ATF_CHECK(result == 1);

	dlclose(handle);
	error = dlerror();
	ATF_CHECK(error == NULL);
}

ATF_TC(rtld_dlvsym_v3);
ATF_TC_HEAD(rtld_dlvsym_v3, tc)
{
	atf_tc_set_md_var(tc, "descr", "Check dlvsym() function (V_3)");
}
ATF_TC_BODY(rtld_dlvsym_v3, tc)
{
	void *handle;
	char *error;
	int (*sym)(void);
	int result;

	/* Clear previous error */
	(void) dlerror();

	handle = dlopen("libh_helper_symver_dso.so", RTLD_LAZY);
	error = dlerror();
	ATF_CHECK(error == NULL);
	ATF_CHECK(handle != NULL);

	sym = dlvsym(handle, "testfunc", "V_3");
	error = dlerror();
	ATF_CHECK(error == NULL);

	result = (*sym)();
	ATF_CHECK(result == 3);

	dlclose(handle);
	error = dlerror();
	ATF_CHECK(error == NULL);
}

ATF_TC(rtld_dlvsym_symbol_nonexistent);
ATF_TC_HEAD(rtld_dlvsym_symbol_nonexistent, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Check dlvsym() function (symbol is nonexistent)");
}
ATF_TC_BODY(rtld_dlvsym_symbol_nonexistent, tc)
{
	void *handle;
	char *error;
	int (*sym)(void);

	/* Clear previous error */
	(void) dlerror();

	handle = dlopen("libh_helper_symver_dso.so", RTLD_LAZY);
	error = dlerror();
	ATF_CHECK(error == NULL);
	ATF_CHECK(handle != NULL);

	sym = dlvsym(handle, "symbol_nonexistent", "V_3");
	error = dlerror();
	ATF_CHECK(sym == NULL);
	ATF_CHECK(error != NULL);

	dlclose(handle);
	error = dlerror();
	ATF_CHECK(error == NULL);
}

ATF_TC(rtld_dlvsym_version_nonexistent);
ATF_TC_HEAD(rtld_dlvsym_version_nonexistent, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Check dlvsym() function (version is nonexistent)");
}
ATF_TC_BODY(rtld_dlvsym_version_nonexistent, tc)
{
	void *handle;
	char *error;
	int (*sym)(void);

	/* Clear previous error */
	(void) dlerror();

	handle = dlopen("libh_helper_symver_dso.so", RTLD_LAZY);
	error = dlerror();
	ATF_CHECK(error == NULL);
	ATF_CHECK(handle != NULL);

	sym = dlvsym(handle, "testfunc", "");
	error = dlerror();
	ATF_CHECK(sym == NULL);
	ATF_CHECK(error != NULL);

	dlclose(handle);
	error = dlerror();
	ATF_CHECK(error == NULL);
}

ATF_TC(rtld_dlvsym_version_null);
ATF_TC_HEAD(rtld_dlvsym_version_null, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Check dlvsym() function (version is NULL)");
}
ATF_TC_BODY(rtld_dlvsym_version_null, tc)
{
	void *handle;
	char *error;
	int (*sym)(void);
	int result;

	/* Clear previous error */
	(void) dlerror();

	handle = dlopen("libh_helper_symver_dso.so", RTLD_LAZY);
	error = dlerror();
	ATF_CHECK(error == NULL);
	ATF_CHECK(handle != NULL);

	sym = dlvsym(handle, "testfunc", NULL);
	error = dlerror();
	ATF_CHECK(error == NULL);

	result = (*sym)();
	ATF_CHECK(result == 3);

	dlclose(handle);
	error = dlerror();
	ATF_CHECK(error == NULL);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, rtld_dlvsym_v1);
	ATF_TP_ADD_TC(tp, rtld_dlvsym_v3);
	ATF_TP_ADD_TC(tp, rtld_dlvsym_symbol_nonexistent);
	ATF_TP_ADD_TC(tp, rtld_dlvsym_version_nonexistent);
	ATF_TP_ADD_TC(tp, rtld_dlvsym_version_null);
	return atf_no_error();
}
