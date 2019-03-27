/* $NetBSD: t_infinity.c,v 1.6 2012/09/26 07:24:38 jruoho Exp $ */

/*-
 * Copyright (c) 2002, 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Martin Husemann <martin@NetBSD.org>.
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
__RCSID("$NetBSD: t_infinity.c,v 1.6 2012/09/26 07:24:38 jruoho Exp $");

#include <atf-c.h>
#include <math.h>
#include <float.h>
#include <stdlib.h>

ATF_TC(infinity_float);
ATF_TC_HEAD(infinity_float, tc)
{
	atf_tc_set_md_var(tc, "descr",
	     "check FPU generated infinite float values");
}

ATF_TC_BODY(infinity_float, tc)
{
	float v;

	v = FLT_MAX;
	v *= v;
	ATF_REQUIRE(isinf(v));
	ATF_REQUIRE(fpclassify(v) == FP_INFINITE);

	v = -FLT_MAX;
	v *= v;
	ATF_REQUIRE(isinf(v));
	ATF_REQUIRE(fpclassify(v) == FP_INFINITE);
}

ATF_TC(infinity_double);
ATF_TC_HEAD(infinity_double, tc)
{
	atf_tc_set_md_var(tc, "descr",
	     "check FPU generated infinite double values");
}

ATF_TC_BODY(infinity_double, tc)
{
	double v;

	v = DBL_MAX;
	v *= v;
	ATF_REQUIRE(isinf(v));
	ATF_REQUIRE(fpclassify(v) == FP_INFINITE);

	v = -DBL_MAX;
	v *= v;
	ATF_REQUIRE(isinf(v));
	ATF_REQUIRE(fpclassify(v) == FP_INFINITE);
}

ATF_TC(infinity_long_double);
ATF_TC_HEAD(infinity_long_double, tc)
{
	atf_tc_set_md_var(tc, "descr",
	     "check FPU generated infinite long double values");
}

ATF_TC_BODY(infinity_long_double, tc)
{

#ifndef LDBL_MAX
	atf_tc_skip("no long double support on this architecture");
	return;
#else
	long double v;

	v = LDBL_MAX;
	v *= v;
	ATF_REQUIRE(isinf(v));
	ATF_REQUIRE(fpclassify(v) == FP_INFINITE);

	v = -LDBL_MAX;
	v *= v;
	ATF_REQUIRE(isinf(v));
	ATF_REQUIRE(fpclassify(v) == FP_INFINITE);
#endif
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, infinity_float);
	ATF_TP_ADD_TC(tp, infinity_double);
	ATF_TP_ADD_TC(tp, infinity_long_double);

	return atf_no_error();
}
