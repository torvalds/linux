/*	$NetBSD: t_ifunc.c,v 1.2 2017/01/13 21:30:42 christos Exp $	*/

/*
 * Copyright (c) 2014 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>

#include <atf-c.h>
#include <dlfcn.h>
#include <util.h>

#include "h_macros.h"

ATF_TC(rtld_ifunc);

ATF_TC_HEAD(rtld_ifunc, tc)
{
	atf_tc_set_md_var(tc, "descr", "ifunc functions are resolved");
}

ATF_TC_BODY(rtld_ifunc, tc)
{
	const char *envstr[] = {
	    "0", "1"
	};
	int expected_result[] = {
	    0xdeadbeef, 0xbeefdead
	};
	void *handle;
	int (*sym)(void);
	int result;
	const char *error;
	size_t i;

	for (i = 0; i < __arraycount(envstr); ++i) {
		setenv("USE_IFUNC2", envstr[i], 1);

		handle = dlopen("libh_helper_ifunc_dso.so", RTLD_LAZY);
		error = dlerror();
		ATF_CHECK(error == NULL);
		ATF_CHECK(handle != NULL);

		sym = dlsym(handle, "ifunc");
		error = dlerror();
		ATF_CHECK(error == NULL);
		ATF_CHECK(sym != NULL);

		result = (*sym)();
		ATF_CHECK(result == expected_result[i]);

		dlclose(handle);
		error = dlerror();
		ATF_CHECK(error == NULL);

		char *command;
		easprintf(&command, "%s/h_ifunc %d",
		    atf_tc_get_config_var(tc, "srcdir"), expected_result[i]);
		if (system(command) != EXIT_SUCCESS)
			atf_tc_fail("Test failed; see output for details");
		free(command);
	}
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, rtld_ifunc);
	return 0;
}
