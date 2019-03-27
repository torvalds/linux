/*	$NetBSD: t_strtod.c,v 1.34 2015/12/22 14:19:25 christos Exp $ */

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

/* Public domain, Otto Moerbeek <otto@drijf.net>, 2006. */

#include <sys/cdefs.h>
__RCSID("$NetBSD: t_strtod.c,v 1.34 2015/12/22 14:19:25 christos Exp $");

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atf-c.h>

#include <fenv.h>

#if !defined(__vax__)
static const char * const inf_strings[] =
    { "Inf", "INF", "-Inf", "-INF", "Infinity", "+Infinity",
      "INFINITY", "-INFINITY", "InFiNiTy", "+InFiNiTy" };
const char *nan_string = "NaN(x)y";
#endif

ATF_TC(strtod_basic);
ATF_TC_HEAD(strtod_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of strtod(3)");
}

ATF_TC_BODY(strtod_basic, tc)
{
	static const size_t n = 1024 * 1000;

	for (size_t i = 1; i < n; i = i + 1024) {
		char buf[512];
		(void)snprintf(buf, sizeof(buf), "%zu.%zu", i, i + 1);

		errno = 0;
		double d = strtod(buf, NULL);

		ATF_REQUIRE(d > 0.0);
		ATF_REQUIRE(errno == 0);
	}
}

ATF_TC(strtod_hex);
ATF_TC_HEAD(strtod_hex, tc)
{
	atf_tc_set_md_var(tc, "descr", "A strtod(3) with hexadecimals");
}

#ifdef __vax__
#define SMALL_NUM       1.0e-38
#else
#define SMALL_NUM       1.0e-40
#endif

ATF_TC_BODY(strtod_hex, tc)
{
	const char *str;
	char *end;
	volatile double d;

	str = "-0x0";
	d = strtod(str, &end);	/* -0.0 */

	ATF_REQUIRE(end == str + 4);
	ATF_REQUIRE(signbit(d) != 0);
	ATF_REQUIRE(fabs(d) < SMALL_NUM);

	str = "-0x";
	d = strtod(str, &end);	/* -0.0 */

	ATF_REQUIRE(end == str + 2);
	ATF_REQUIRE(signbit(d) != 0);
	ATF_REQUIRE(fabs(d) < SMALL_NUM);
}

ATF_TC(strtod_inf);
ATF_TC_HEAD(strtod_inf, tc)
{
	atf_tc_set_md_var(tc, "descr", "A strtod(3) with INF (PR lib/33262)");
}

ATF_TC_BODY(strtod_inf, tc)
{
#ifndef __vax__
	for (size_t i = 0; i < __arraycount(inf_strings); i++) {
		volatile double d = strtod(inf_strings[i], NULL);
		ATF_REQUIRE(isinf(d) != 0);
	}
#else
	atf_tc_skip("vax not supported");
#endif
}

ATF_TC(strtof_inf);
ATF_TC_HEAD(strtof_inf, tc)
{
	atf_tc_set_md_var(tc, "descr", "A strtof(3) with INF (PR lib/33262)");
}

ATF_TC_BODY(strtof_inf, tc)
{
#ifndef __vax__
	for (size_t i = 0; i < __arraycount(inf_strings); i++) {
		volatile float f = strtof(inf_strings[i], NULL);
		ATF_REQUIRE(isinf(f) != 0);
	}
#else
	atf_tc_skip("vax not supported");
#endif
}

ATF_TC(strtold_inf);
ATF_TC_HEAD(strtold_inf, tc)
{
	atf_tc_set_md_var(tc, "descr", "A strtold(3) with INF (PR lib/33262)");
}

ATF_TC_BODY(strtold_inf, tc)
{
#ifndef __vax__
#   ifdef __HAVE_LONG_DOUBLE

	for (size_t i = 0; i < __arraycount(inf_strings); i++) {
		volatile long double ld = strtold(inf_strings[i], NULL);
		ATF_REQUIRE(isinf(ld) != 0);
	}
#   else
	atf_tc_skip("Requires long double support");
#   endif
#else
	atf_tc_skip("vax not supported");
#endif
}

ATF_TC(strtod_nan);
ATF_TC_HEAD(strtod_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "A strtod(3) with NaN");
}

ATF_TC_BODY(strtod_nan, tc)
{
#ifndef __vax__
	char *end;

	volatile double d = strtod(nan_string, &end);
	ATF_REQUIRE(isnan(d) != 0);
	ATF_REQUIRE(strcmp(end, "y") == 0);
#else
	atf_tc_skip("vax not supported");
#endif
}

ATF_TC(strtof_nan);
ATF_TC_HEAD(strtof_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "A strtof(3) with NaN");
}

ATF_TC_BODY(strtof_nan, tc)
{
#ifndef __vax__
	char *end;

	volatile float f = strtof(nan_string, &end);
	ATF_REQUIRE(isnanf(f) != 0);
	ATF_REQUIRE(strcmp(end, "y") == 0);
#else
	atf_tc_skip("vax not supported");
#endif
}

ATF_TC(strtold_nan);
ATF_TC_HEAD(strtold_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "A strtold(3) with NaN (PR lib/45020)");
}

ATF_TC_BODY(strtold_nan, tc)
{
#ifndef __vax__
#   ifdef __HAVE_LONG_DOUBLE

	char *end;

	volatile long double ld = strtold(nan_string, &end);
	ATF_REQUIRE(isnan(ld) != 0);
#ifndef __FreeBSD__
	ATF_REQUIRE(__isnanl(ld) != 0);
#endif
	ATF_REQUIRE(strcmp(end, "y") == 0);
#   else
	atf_tc_skip("Requires long double support");
#   endif
#else
	atf_tc_skip("vax not supported");
#endif
}

ATF_TC(strtod_round);
ATF_TC_HEAD(strtod_round, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test rouding in strtod(3)");
}

ATF_TC_BODY(strtod_round, tc)
{
#ifdef __HAVE_FENV

	/*
	 * Test that strtod(3) honors the current rounding mode.
	 * The used value is somewhere near 1 + DBL_EPSILON + FLT_EPSILON.
	 */
	const char *val =
	    "1.00000011920928977282585492503130808472633361816406";

	(void)fesetround(FE_UPWARD);

	volatile double d1 = strtod(val, NULL);

	(void)fesetround(FE_DOWNWARD);

	volatile double d2 = strtod(val, NULL);

	if (fabs(d1 - d2) > 0.0)
		return;
	else {
		atf_tc_expect_fail("PR misc/44767");
		atf_tc_fail("strtod(3) did not honor fesetround(3)");
	}
#else
	atf_tc_skip("Requires <fenv.h> support");
#endif
}

ATF_TC(strtod_underflow);
ATF_TC_HEAD(strtod_underflow, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test underflow in strtod(3)");
}

ATF_TC_BODY(strtod_underflow, tc)
{

	const char *tmp =
	    "0.0000000000000000000000000000000000000000000000000000"
	    "000000000000000000000000000000000000000000000000000000"
	    "000000000000000000000000000000000000000000000000000000"
	    "000000000000000000000000000000000000000000000000000000"
	    "000000000000000000000000000000000000000000000000000000"
	    "000000000000000000000000000000000000000000000000000000"
	    "000000000000000000000000000000000000000000000000000000"
	    "000000000000000002";

	errno = 0;
	volatile double d = strtod(tmp, NULL);

	if (d != 0 || errno != ERANGE)
		atf_tc_fail("strtod(3) did not detect underflow");
}

/*
 * Bug found by Geza Herman.
 * See
 * http://www.exploringbinary.com/a-bug-in-the-bigcomp-function-of-david-gays-strtod/
 */
ATF_TC(strtod_gherman_bug);
ATF_TC_HEAD(strtod_gherman_bug, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test a bug found by Geza Herman");
}

ATF_TC_BODY(strtod_gherman_bug, tc)
{

	const char *str =
	    "1.8254370818746402660437411213933955878019332885742187";

	errno = 0;
	volatile double d = strtod(str, NULL);

	ATF_CHECK(d == 0x1.d34fd8378ea83p+0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, strtod_basic);
	ATF_TP_ADD_TC(tp, strtod_hex);
	ATF_TP_ADD_TC(tp, strtod_inf);
	ATF_TP_ADD_TC(tp, strtof_inf);
	ATF_TP_ADD_TC(tp, strtold_inf);
	ATF_TP_ADD_TC(tp, strtod_nan);
	ATF_TP_ADD_TC(tp, strtof_nan);
	ATF_TP_ADD_TC(tp, strtold_nan);
	ATF_TP_ADD_TC(tp, strtod_round);
	ATF_TP_ADD_TC(tp, strtod_underflow);
	ATF_TP_ADD_TC(tp, strtod_gherman_bug);

	return atf_no_error();
}
