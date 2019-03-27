/*	$NetBSD: t_getenv.c,v 1.3 2015/02/27 08:55:35 martin Exp $ */

/*-
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas
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
__RCSID("$NetBSD: t_getenv.c,v 1.3 2015/02/27 08:55:35 martin Exp $");

#include <atf-c.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __FreeBSD__
#include <signal.h>
#endif

extern char	**environ;

ATF_TC(clearenv_basic);
ATF_TC_HEAD(clearenv_basic, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test user clearing environment directly");
}

ATF_TC_BODY(clearenv_basic, tc)
{
	char name[1024], value[1024];

	for (size_t i = 0; i < 1024; i++) {
		snprintf(name, sizeof(name), "crap%zu", i);
		snprintf(value, sizeof(value), "%zu", i);
		ATF_CHECK(setenv(name, value, 1) != -1);
	}

	*environ = NULL;

	for (size_t i = 0; i < 1; i++) {
		snprintf(name, sizeof(name), "crap%zu", i);
		snprintf(value, sizeof(value), "%zu", i);
		ATF_CHECK(setenv(name, value, 1) != -1);
	}

	ATF_CHECK_STREQ(getenv("crap0"), "0");
	ATF_CHECK(getenv("crap1") == NULL);
	ATF_CHECK(getenv("crap2") == NULL);
}

ATF_TC(getenv_basic);
ATF_TC_HEAD(getenv_basic, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test setenv(3), getenv(3)");
}

ATF_TC_BODY(getenv_basic, tc)
{
	ATF_CHECK(setenv("EVIL", "very=bad", 1) != -1);
	ATF_CHECK_STREQ(getenv("EVIL"), "very=bad");
	ATF_CHECK(getenv("EVIL=very") == NULL);
	ATF_CHECK(unsetenv("EVIL") != -1);
}

ATF_TC(putenv_basic);
ATF_TC_HEAD(putenv_basic, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test putenv(3), getenv(3), unsetenv(3)");
}


ATF_TC_BODY(putenv_basic, tc)
{
	char string[1024];

	snprintf(string, sizeof(string), "crap=true");
	ATF_CHECK(putenv(string) != -1);
	ATF_CHECK_STREQ(getenv("crap"), "true");
	string[1] = 'l';
	ATF_CHECK_STREQ(getenv("clap"), "true");
	ATF_CHECK(getenv("crap") == NULL);
	string[1] = 'r';
	ATF_CHECK(unsetenv("crap") != -1);
	ATF_CHECK(getenv("crap") == NULL);

	ATF_CHECK_ERRNO(EINVAL, putenv(NULL) == -1);
	ATF_CHECK_ERRNO(EINVAL, putenv(__UNCONST("val")) == -1);
	ATF_CHECK_ERRNO(EINVAL, putenv(__UNCONST("=val")) == -1);
}

ATF_TC(setenv_basic);
ATF_TC_HEAD(setenv_basic, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test setenv(3), getenv(3), unsetenv(3)");
	atf_tc_set_md_var(tc, "timeout", "600");
}

ATF_TC_BODY(setenv_basic, tc)
{
	const size_t numvars = 8192;
	size_t i, offset;
	char name[1024];
	char value[1024];

	offset = lrand48();
	for (i = 0; i < numvars; i++) {
		(void)snprintf(name, sizeof(name), "var%zu",
		    (i * 7 + offset) % numvars);
		(void)snprintf(value, sizeof(value), "value%ld", lrand48());
		ATF_CHECK(setenv(name, value, 1) != -1);
		ATF_CHECK(setenv(name, "foo", 0) != -1);
		ATF_CHECK_STREQ(getenv(name), value);
	}

	offset = lrand48();
	for (i = 0; i < numvars; i++) {
		(void)snprintf(name, sizeof(name), "var%zu",
		    (i * 11 + offset) % numvars);
		ATF_CHECK(unsetenv(name) != -1);
		ATF_CHECK(getenv(name) == NULL);
		ATF_CHECK(unsetenv(name) != -1);
	}

	ATF_CHECK_ERRNO(EINVAL, setenv(NULL, "val", 1) == -1);
	ATF_CHECK_ERRNO(EINVAL, setenv("", "val", 1) == -1);
	ATF_CHECK_ERRNO(EINVAL, setenv("v=r", "val", 1) == -1);
#ifdef __FreeBSD__
	/*
	   Both FreeBSD and OS/X does not validate the second
	   argument to setenv(3)
	 */
	atf_tc_expect_signal(SIGSEGV, "FreeBSD does not validate the second "
	    "argument to setenv(3); see bin/189805");
#endif

	ATF_CHECK_ERRNO(EINVAL, setenv("var", NULL, 1) == -1);

	ATF_CHECK(setenv("var", "=val", 1) == 0);
	ATF_CHECK_STREQ(getenv("var"), "=val");
}

ATF_TC(setenv_mixed);
ATF_TC_HEAD(setenv_mixed, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test mixing setenv(3), unsetenv(3) and putenv(3)");
}

ATF_TC_BODY(setenv_mixed, tc)
{
	char string[32];

	(void)strcpy(string, "mixedcrap=putenv");

	ATF_CHECK(setenv("mixedcrap", "setenv", 1) != -1);
	ATF_CHECK_STREQ(getenv("mixedcrap"), "setenv");
	ATF_CHECK(putenv(string) != -1);
	ATF_CHECK_STREQ(getenv("mixedcrap"), "putenv");
	ATF_CHECK(unsetenv("mixedcrap") != -1);
	ATF_CHECK(getenv("mixedcrap") == NULL);

	ATF_CHECK(putenv(string) != -1);
	ATF_CHECK_STREQ(getenv("mixedcrap"), "putenv");
	ATF_CHECK(setenv("mixedcrap", "setenv", 1) != -1);
	ATF_CHECK_STREQ(getenv("mixedcrap"), "setenv");
	ATF_CHECK(unsetenv("mixedcrap") != -1);
	ATF_CHECK(getenv("mixedcrap") == NULL);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, clearenv_basic);
	ATF_TP_ADD_TC(tp, getenv_basic);
	ATF_TP_ADD_TC(tp, putenv_basic);
	ATF_TP_ADD_TC(tp, setenv_basic);
	ATF_TP_ADD_TC(tp, setenv_mixed);

	return atf_no_error();
}
