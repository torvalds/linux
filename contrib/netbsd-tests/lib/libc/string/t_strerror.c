/* $NetBSD: t_strerror.c,v 1.4 2017/01/10 20:35:49 christos Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jukka Ruohonen.
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
__RCSID("$NetBSD: t_strerror.c,v 1.4 2017/01/10 20:35:49 christos Exp $");

#include <atf-c.h>
#include <errno.h>
#include <stdio.h>	/* Needed for sys_nerr on FreeBSD */
#include <limits.h>
#include <locale.h>
#include <string.h>

ATF_TC(strerror_basic);
ATF_TC_HEAD(strerror_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of strerror(3)");
}

ATF_TC_BODY(strerror_basic, tc)
{
	int i;

	for (i = 1; i < sys_nerr; i++)
		ATF_REQUIRE(strstr(strerror(i), "Unknown error:") == NULL);

	for (; i < sys_nerr + 10; i++)
		ATF_REQUIRE(strstr(strerror(i), "Unknown error:") != NULL);
}

ATF_TC(strerror_err);
ATF_TC_HEAD(strerror_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors from strerror(3)");
}

ATF_TC_BODY(strerror_err, tc)
{

	errno = 0;

	ATF_REQUIRE(strstr(strerror(INT_MAX), "Unknown error:") != NULL);
	ATF_REQUIRE(errno == EINVAL);

	errno = 0;

	ATF_REQUIRE(strstr(strerror(INT_MIN), "Unknown error:") != NULL);
	ATF_REQUIRE(errno == EINVAL);
}

ATF_TC(strerror_r_basic);
ATF_TC_HEAD(strerror_r_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of strerror_r(3)");
}

ATF_TC_BODY(strerror_r_basic, tc)
{
	char buf[512];
	int i;

	for (i = 1; i < sys_nerr; i++) {
		ATF_REQUIRE(strerror_r(i, buf, sizeof(buf)) == 0);
		ATF_REQUIRE(strstr(buf, "Unknown error:") == NULL);
	}

	for (; i < sys_nerr + 10; i++) {
		ATF_REQUIRE(strerror_r(i, buf, sizeof(buf)) == EINVAL);
		ATF_REQUIRE(strstr(buf, "Unknown error:") != NULL);
	}
}

ATF_TC(strerror_r_err);
ATF_TC_HEAD(strerror_r_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors from strerror_r(3)");
}

ATF_TC_BODY(strerror_r_err, tc)
{
	char buf[512];
	int rv;

	rv = strerror_r(EPERM, buf, 1);
	ATF_REQUIRE(rv == ERANGE);

	rv = strerror_r(INT_MAX, buf, sizeof(buf));

	ATF_REQUIRE(rv == EINVAL);
	ATF_REQUIRE(strstr(buf, "Unknown error:") != NULL);

	rv = strerror_r(INT_MIN, buf, sizeof(buf));

	ATF_REQUIRE(rv == EINVAL);
	ATF_REQUIRE(strstr(buf, "Unknown error:") != NULL);
}

ATF_TP_ADD_TCS(tp)
{

	(void)setlocale(LC_ALL, "C");

	ATF_TP_ADD_TC(tp, strerror_basic);
	ATF_TP_ADD_TC(tp, strerror_err);
	ATF_TP_ADD_TC(tp, strerror_r_basic);
	ATF_TP_ADD_TC(tp, strerror_r_err);

	return atf_no_error();
}
