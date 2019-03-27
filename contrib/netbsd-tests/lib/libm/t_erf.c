/* $NetBSD: t_erf.c,v 1.2 2014/03/03 10:39:08 martin Exp $ */

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
__RCSID("$NetBSD: t_erf.c,v 1.2 2014/03/03 10:39:08 martin Exp $");

#include <atf-c.h>
#include <math.h>

/*
 * erf(3)
 */
ATF_TC(erf_nan);
ATF_TC_HEAD(erf_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test erf(NaN) == NaN");
}

ATF_TC_BODY(erf_nan, tc)
{
	const double x = 0.0L / 0.0L;

	ATF_CHECK(isnan(erf(x)) != 0);
}

ATF_TC(erf_inf_neg);
ATF_TC_HEAD(erf_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test erf(-Inf) == -1.0");
}

ATF_TC_BODY(erf_inf_neg, tc)
{
	const double x = -1.0L / 0.0L;

	if (erf(x) != -1.0)
		atf_tc_fail_nonfatal("erf(-Inf) != -1.0");
}

ATF_TC(erf_inf_pos);
ATF_TC_HEAD(erf_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test erf(+Inf) == 1.0");
}

ATF_TC_BODY(erf_inf_pos, tc)
{
	const double x = 1.0L / 0.0L;

	if (erf(x) != 1.0)
		atf_tc_fail_nonfatal("erf(+Inf) != 1.0");
}

ATF_TC(erf_zero_neg);
ATF_TC_HEAD(erf_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test erf(-0.0) == -0.0");
}

ATF_TC_BODY(erf_zero_neg, tc)
{
	const double x = -0.0L;
	double y = erf(x);

	if (fabs(y) > 0.0 || signbit(y) == 0)
		atf_tc_fail_nonfatal("erf(-0.0) != -0.0");
}

ATF_TC(erf_zero_pos);
ATF_TC_HEAD(erf_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test erf(+0.0) == +0.0");
}

ATF_TC_BODY(erf_zero_pos, tc)
{
	const double x = 0.0L;
	double y = erf(x);

	if (fabs(y) > 0.0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("erf(+0.0) != +0.0");
}

/*
 * erff(3)
 */
ATF_TC(erff_nan);
ATF_TC_HEAD(erff_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test erff(NaN) == NaN");
}

ATF_TC_BODY(erff_nan, tc)
{
	const float x = 0.0L / 0.0L;

	ATF_CHECK(isnan(erff(x)) != 0);
}

ATF_TC(erff_inf_neg);
ATF_TC_HEAD(erff_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test erff(-Inf) == -1.0");
}

ATF_TC_BODY(erff_inf_neg, tc)
{
	const float x = -1.0L / 0.0L;

	if (erff(x) != -1.0)
		atf_tc_fail_nonfatal("erff(-Inf) != -1.0");
}

ATF_TC(erff_inf_pos);
ATF_TC_HEAD(erff_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test erff(+Inf) == 1.0");
}

ATF_TC_BODY(erff_inf_pos, tc)
{
	const float x = 1.0L / 0.0L;

	if (erff(x) != 1.0)
		atf_tc_fail_nonfatal("erff(+Inf) != 1.0");
}

ATF_TC(erff_zero_neg);
ATF_TC_HEAD(erff_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test erff(-0.0) == -0.0");
}

ATF_TC_BODY(erff_zero_neg, tc)
{
	const float x = -0.0L;
	float y = erff(x);

	if (fabsf(y) > 0.0 || signbit(y) == 0)
		atf_tc_fail_nonfatal("erff(-0.0) != -0.0");
}

ATF_TC(erff_zero_pos);
ATF_TC_HEAD(erff_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test erff(+0.0) == +0.0");
}

ATF_TC_BODY(erff_zero_pos, tc)
{
	const float x = 0.0L;
	float y = erff(x);

	if (fabsf(y) > 0.0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("erff(+0.0) != +0.0");
}

/*
 * erfc(3)
 */
ATF_TC(erfc_nan);
ATF_TC_HEAD(erfc_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test erfc(NaN) == NaN");
}

ATF_TC_BODY(erfc_nan, tc)
{
	const double x = 0.0L / 0.0L;

	ATF_CHECK(isnan(erfc(x)) != 0);
}

ATF_TC(erfc_inf_neg);
ATF_TC_HEAD(erfc_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test erfc(-Inf) == 2.0");
}

ATF_TC_BODY(erfc_inf_neg, tc)
{
	const double x = -1.0L / 0.0L;

	if (erfc(x) != 2.0)
		atf_tc_fail_nonfatal("erfc(-Inf) != 2.0");
}

ATF_TC(erfc_inf_pos);
ATF_TC_HEAD(erfc_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test erfc(+Inf) == +0.0");
}

ATF_TC_BODY(erfc_inf_pos, tc)
{
	const double x = 1.0L / 0.0L;
	double y = erfc(x);

	if (fabs(y) > 0.0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("erfc(+Inf) != +0.0");
}

/*
 * erfcf(3)
 */
ATF_TC(erfcf_nan);
ATF_TC_HEAD(erfcf_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test erfcf(NaN) == NaN");
}

ATF_TC_BODY(erfcf_nan, tc)
{
	const float x = 0.0L / 0.0L;

	ATF_CHECK(isnan(erfcf(x)) != 0);
}

ATF_TC(erfcf_inf_neg);
ATF_TC_HEAD(erfcf_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test erfcf(-Inf) == 2.0");
}

ATF_TC_BODY(erfcf_inf_neg, tc)
{
	const float x = -1.0L / 0.0L;

	if (erfcf(x) != 2.0)
		atf_tc_fail_nonfatal("erfcf(-Inf) != 2.0");
}

ATF_TC(erfcf_inf_pos);
ATF_TC_HEAD(erfcf_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test erfcf(+Inf) == +0.0");
}

ATF_TC_BODY(erfcf_inf_pos, tc)
{
	const float x = 1.0L / 0.0L;
	float y = erfcf(x);

	if (fabsf(y) > 0.0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("erfcf(+Inf) != +0.0");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, erf_nan);
	ATF_TP_ADD_TC(tp, erf_inf_neg);
	ATF_TP_ADD_TC(tp, erf_inf_pos);
	ATF_TP_ADD_TC(tp, erf_zero_neg);
	ATF_TP_ADD_TC(tp, erf_zero_pos);

	ATF_TP_ADD_TC(tp, erff_nan);
	ATF_TP_ADD_TC(tp, erff_inf_neg);
	ATF_TP_ADD_TC(tp, erff_inf_pos);
	ATF_TP_ADD_TC(tp, erff_zero_neg);
	ATF_TP_ADD_TC(tp, erff_zero_pos);

	ATF_TP_ADD_TC(tp, erfc_nan);
	ATF_TP_ADD_TC(tp, erfc_inf_neg);
	ATF_TP_ADD_TC(tp, erfc_inf_pos);

	ATF_TP_ADD_TC(tp, erfcf_nan);
	ATF_TP_ADD_TC(tp, erfcf_inf_neg);
	ATF_TP_ADD_TC(tp, erfcf_inf_pos);

	return atf_no_error();
}
