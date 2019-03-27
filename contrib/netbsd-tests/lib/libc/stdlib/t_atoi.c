/* $NetBSD: t_atoi.c,v 1.2 2012/03/29 05:56:36 jruoho Exp $ */

/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
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
__RCSID("$NetBSD: t_atoi.c,v 1.2 2012/03/29 05:56:36 jruoho Exp $");

#include <atf-c.h>
#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

ATF_TC(atof_strtod);
ATF_TC_HEAD(atof_strtod, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that atof(3) matches the corresponding strtod(3) call");
}

ATF_TC_BODY(atof_strtod, tc)
{
	char buf[128];

	(void)snprintf(buf, sizeof(buf), "%f\n", DBL_MAX);

	ATF_REQUIRE(atof("0")  == strtod("0", NULL));
	ATF_REQUIRE(atof("-1") == strtod("-1", NULL));
	ATF_REQUIRE(atof(buf)  == strtod(buf, NULL));
}

ATF_TC(atoi_strtol);
ATF_TC_HEAD(atoi_strtol, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that atoi(3) matches the corresponding strtol(3) call");
}

ATF_TC_BODY(atoi_strtol, tc)
{
	char buf[64];

	(void)snprintf(buf, sizeof(buf), "%d\n", INT_MAX);

	ATF_REQUIRE(atoi("0")  == strtol("0", NULL, 10));
	ATF_REQUIRE(atoi("-1") == strtol("-1", NULL, 10));
	ATF_REQUIRE(atoi(buf)  == strtol(buf, NULL, 10));
}

ATF_TC(atol_strtol);
ATF_TC_HEAD(atol_strtol, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that atol(3) matches the corresponding strtol(3) call");
}

ATF_TC_BODY(atol_strtol, tc)
{
	char buf[64];

	(void)snprintf(buf, sizeof(buf), "%ld\n", LONG_MAX);

	ATF_REQUIRE(atol("0")  == strtol("0", NULL, 10));
	ATF_REQUIRE(atol("-1") == strtol("-1", NULL, 10));
	ATF_REQUIRE(atol(buf)  == strtol(buf, NULL, 10));
}

ATF_TC(atoll_strtoll);
ATF_TC_HEAD(atoll_strtoll, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that atoll(3) matches the corresponding strtoll(3) call");
}

ATF_TC_BODY(atoll_strtoll, tc)
{
	char buf[128];

	(void)snprintf(buf, sizeof(buf), "%lld\n", LLONG_MAX);

	ATF_REQUIRE(atoll("0")  == strtoll("0", NULL, 10));
	ATF_REQUIRE(atoll("-1") == strtoll("-1", NULL, 10));
	ATF_REQUIRE(atoll(buf)  == strtoll(buf, NULL, 10));
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, atof_strtod);
	ATF_TP_ADD_TC(tp, atoi_strtol);
	ATF_TP_ADD_TC(tp, atol_strtol);
	ATF_TP_ADD_TC(tp, atoll_strtoll);

	return atf_no_error();
}
