/* $NetBSD: t_ldexp.c,v 1.16 2016/08/25 00:32:31 maya Exp $ */

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
__RCSID("$NetBSD: t_ldexp.c,v 1.16 2016/08/25 00:32:31 maya Exp $");

#include <sys/param.h>

#include <atf-c.h>

#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define SKIP	9999
#define FORMAT  "%23.23lg"

static const int exps[] = { 0, 1, -1, 100, -100 };

struct ldexp_test {
	double	    x;
	int	    exp1;
	int	    exp2;
	const char *result;
};

struct ldexp_test ldexp_basic[] = {
	{ 1.0,	5,	SKIP,	"                     32" },
	{ 1.0,	1022,	SKIP,	"4.4942328371557897693233e+307" },
	{ 1.0,	1023,	-1,	"4.4942328371557897693233e+307" },
	{ 1.0,	1023,	SKIP,	"8.9884656743115795386465e+307" },
	{ 1.0,	1022,	1,	"8.9884656743115795386465e+307" },
	{ 1.0,	-1022,	2045,	"8.9884656743115795386465e+307" },
	{ 1.0,	-5,	SKIP,	"                0.03125" },
	{ 1.0,	-1021,	SKIP,	"4.4501477170144027661805e-308" },
	{ 1.0,	-1022,	1,	"4.4501477170144027661805e-308" },
	{ 1.0,	-1022,	SKIP,	"2.2250738585072013830902e-308" },
	{ 1.0,	-1021,	-1,	"2.2250738585072013830902e-308" },
	{ 1.0,	1023,	-2045,	"2.2250738585072013830902e-308" },
	{ 1.0,	1023,	-1023,	"                      1" },
	{ 1.0,	-1022,	1022,	"                      1" },
	{ 0,	0,	0,	NULL }
};

struct ldexp_test ldexp_zero[] = {
	{ 0.0,	-1,	SKIP,	"                      0" },
	{ 0.0,	0,	SKIP,	"                      0" },
	{ 0.0,	1,	SKIP,	"                      0" },
	{ 0.0,	1024,	SKIP,	"                      0" },
	{ 0.0,	1025,	SKIP,	"                      0" },
	{ 0.0,	-1023,	SKIP,	"                      0" },
	{ 0.0,	-1024,	SKIP,	"                      0" },
	{ 0,	0,	0,	NULL }
};

struct ldexp_test ldexp_infinity[] = {
	{ 1.0,	1024,	-1,	"                    inf" },
	{ 1.0,	1024,	0,	"                    inf" },
	{ 1.0,	1024,	1,	"                    inf" },
	{ -1.0,	1024,	-1,	"                   -inf" },
	{ -1.0,	1024,	0,	"                   -inf" },
	{ -1.0,	1024,	1,	"                   -inf" },
	{ 0,	0,	0,	NULL }
};

struct ldexp_test ldexp_overflow[] = {
	{ 1.0,	1024,	SKIP,	"                    inf" },
	{ 1.0,	1023,	1,	"                    inf" },
	{ 1.0,	-1022,	2046,	"                    inf" },
	{ 1.0,	1025,	SKIP,	"                    inf" },
	{ 2.0,	INT_MAX,SKIP,	"                    inf" },
	{ -1.0,	1024,	SKIP,	"                   -inf" },
	{ -1.0,	1023,	1,	"                   -inf" },
	{ -1.0,	-1022,	2046,	"                   -inf" },
	{ -1.0,	1025,	SKIP,	"                   -inf" },
	{ -2.0, INT_MAX,SKIP,	"                   -inf" },
	{ 0,	0,	0,	NULL }
};

struct ldexp_test ldexp_denormal[] = {
	{ 1.0,	-1023,	SKIP,	"1.1125369292536006915451e-308" },
	{ 1.0,	-1022,	-1,	"1.1125369292536006915451e-308" },
	{ 1.0,	1023,	-2046,	"1.1125369292536006915451e-308" },
	{ 1.0,	-1024,	SKIP,	"5.5626846462680034577256e-309" },
	{ 1.0,	-1074,	SKIP,	"4.9406564584124654417657e-324" },
	{ -1.0,	-1023,	SKIP,	"-1.1125369292536006915451e-308" },
	{ -1.0,	-1022,	-1,	"-1.1125369292536006915451e-308" },
	{ -1.0,	1023,	-2046,	"-1.1125369292536006915451e-308" },
	{ -1.0,	-1024,	SKIP,	"-5.5626846462680034577256e-309" },
	{ -1.0,	-1074,	SKIP,	"-4.9406564584124654417657e-324" },
	{ 0,	0,	0,	NULL }
};

struct ldexp_test ldexp_underflow[] = {
	{ 1.0,	-1075,	SKIP,	"                      0" },
	{ 1.0,	-1074,	-1,	"                      0" },
	{ 1.0,	1023,	-2098,	"                      0" },
	{ 1.0,	-1076,	SKIP,	"                      0" },
	{ -1.0,	-1075,	SKIP,	"                     -0" },
	{ -1.0,	-1074,	-1,	"                     -0" },
	{ -1.0,	1023,	-2098,	"                     -0" },
	{ -1.0,	-1076,	SKIP,	"                     -0" },
	{ 0,	0,	0,	NULL }
};

struct ldexp_test ldexp_denormal_large[] = {
	{ 1.0,	-1028,	1024,	"                 0.0625" },
	{ 1.0,	-1028,	1025,	"                  0.125" },
	{ 1.0,	-1028,	1026,	"                   0.25" },
	{ 1.0,	-1028,	1027,	"                    0.5" },
	{ 1.0,	-1028,	1028,	"                      1" },
	{ 1.0,	-1028,	1029,	"                      2" },
	{ 1.0,	-1028,	1030,	"                      4" },
	{ 1.0,	-1028,	1040,	"                   4096" },
	{ 1.0,	-1028,	1050,	"                4194304" },
	{ 1.0,	-1028,	1060,	"             4294967296" },
	{ 1.0,	-1028,	1100,	" 4722366482869645213696" },
	{ 1.0,	-1028,	1200,	"5.9863107065073783529623e+51" },
	{ 1.0,	-1028,	1300,	"7.5885503602567541832791e+81" },
	{ 1.0,	-1028,	1400,	"9.6196304190416209014353e+111" },
	{ 1.0,	-1028,	1500,	"1.2194330274671844653834e+142" },
	{ 1.0,	-1028,	1600,	"1.5458150092069033378781e+172" },
	{ 1.0,	-1028,	1700,	"1.9595533242629369747791e+202" },
	{ 1.0,	-1028,	1800,	"2.4840289476811342962384e+232" },
	{ 1.0,	-1028,	1900,	"3.1488807865122869393369e+262" },
	{ 1.0,	-1028,	2000,	"3.9916806190694396233127e+292" },
	{ 1.0,	-1028,	2046,	"2.808895523222368605827e+306" },
	{ 1.0,	-1028,	2047,	"5.6177910464447372116541e+306" },
	{ 1.0,	-1028,	2048,	"1.1235582092889474423308e+307" },
	{ 1.0,	-1028,	2049,	"2.2471164185778948846616e+307" },
	{ 1.0,	-1028,	2050,	"4.4942328371557897693233e+307" },
	{ 1.0,	-1028,	2051,	"8.9884656743115795386465e+307" },
	{ 0,	0,	0,	NULL }
};

static void
run_test(struct ldexp_test *table)
{
	char outbuf[64];
	size_t i;
	double v;

	for (i = 0; table->result != NULL; table++, i++) {

		v = ldexp(table->x, table->exp1);

		if (table->exp2 != SKIP)
			v = ldexp(v, table->exp2);

		(void)snprintf(outbuf, sizeof(outbuf), FORMAT, v);

		ATF_CHECK_STREQ_MSG(table->result, outbuf,
			    "Entry %zu:\n\tExp: \"%s\"\n\tAct: \"%s\"",
			    i, table->result, outbuf);
	}
}

/*
 * ldexp(3)
 */
ATF_TC(ldexp_exp2);
ATF_TC_HEAD(ldexp_exp2, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ldexp(x, n) == x * exp2(n)");
}

ATF_TC_BODY(ldexp_exp2, tc)
{
	const double n[] = { 1, 2, 3, 10, 50, 100 };
#if __DBL_MIN_10_EXP__ <= -40
	const double eps = 1.0e-40;
#else
	const double eps = __DBL_MIN__*4.0;
#endif
	const double x = 12.0;
	double y;
	size_t i;

	for (i = 0; i < __arraycount(n); i++) {

		y = ldexp(x, n[i]);

		if (fabs(y - (x * exp2(n[i]))) > eps) {
			atf_tc_fail_nonfatal("ldexp(%0.01f, %0.01f) "
			    "!= %0.01f * exp2(%0.01f)", x, n[i], x, n[i]);
		}
	}
}

ATF_TC(ldexp_nan);
ATF_TC_HEAD(ldexp_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ldexp(NaN) == NaN");
}

ATF_TC_BODY(ldexp_nan, tc)
{
	const double x = 0.0L / 0.0L;
	double y;
	size_t i;

	ATF_REQUIRE(isnan(x) != 0);

	for (i = 0; i < __arraycount(exps); i++) {
		y = ldexp(x, exps[i]);
		ATF_CHECK(isnan(y) != 0);
	}
}

ATF_TC(ldexp_inf_neg);
ATF_TC_HEAD(ldexp_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ldexp(-Inf) == -Inf");
}

ATF_TC_BODY(ldexp_inf_neg, tc)
{
	const double x = -1.0L / 0.0L;
	size_t i;

	for (i = 0; i < __arraycount(exps); i++)
		ATF_CHECK(ldexp(x, exps[i]) == x);
}

ATF_TC(ldexp_inf_pos);
ATF_TC_HEAD(ldexp_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ldexp(+Inf) == +Inf");
}

ATF_TC_BODY(ldexp_inf_pos, tc)
{
	const double x = 1.0L / 0.0L;
	size_t i;

	for (i = 0; i < __arraycount(exps); i++)
		ATF_CHECK(ldexp(x, exps[i]) == x);
}

ATF_TC(ldexp_zero_neg);
ATF_TC_HEAD(ldexp_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ldexp(-0.0) == -0.0");
}

ATF_TC_BODY(ldexp_zero_neg, tc)
{
	const double x = -0.0L;
	double y;
	size_t i;

	ATF_REQUIRE(signbit(x) != 0);

	for (i = 0; i < __arraycount(exps); i++) {
		y = ldexp(x, exps[i]);
		ATF_CHECK(x == y);
		ATF_CHECK(signbit(y) != 0);
	}
}

ATF_TC(ldexp_zero_pos);
ATF_TC_HEAD(ldexp_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ldexp(+0.0) == +0.0");
}

ATF_TC_BODY(ldexp_zero_pos, tc)
{
	const double x = 0.0L;
	double y;
	size_t i;

	ATF_REQUIRE(signbit(x) == 0);

	for (i = 0; i < __arraycount(exps); i++) {
		y = ldexp(x, exps[i]);
		ATF_CHECK(x == y);
		ATF_CHECK(signbit(y) == 0);
	}
}

/*
 * ldexpf(3)
 */

ATF_TC(ldexpf_exp2f);
ATF_TC_HEAD(ldexpf_exp2f, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ldexpf(x, n) == x * exp2f(n)");
}

ATF_TC_BODY(ldexpf_exp2f, tc)
{
	const float n[] = { 1, 2, 3, 10, 50, 100 };
	const float eps = 1.0e-9;
	const float x = 12.0;
	float y;
	size_t i;

	for (i = 0; i < __arraycount(n); i++) {

		y = ldexpf(x, n[i]);

		if (fabsf(y - (x * exp2f(n[i]))) > eps) {
			atf_tc_fail_nonfatal("ldexpf(%0.01f, %0.01f) "
			    "!= %0.01f * exp2f(%0.01f)", x, n[i], x, n[i]);
		}
	}
}

ATF_TC(ldexpf_nan);
ATF_TC_HEAD(ldexpf_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ldexpf(NaN) == NaN");
}

ATF_TC_BODY(ldexpf_nan, tc)
{
	const float x = 0.0L / 0.0L;
	float y;
	size_t i;

	ATF_REQUIRE(isnan(x) != 0);

	for (i = 0; i < __arraycount(exps); i++) {
		y = ldexpf(x, exps[i]);
		ATF_CHECK(isnan(y) != 0);
	}
}

ATF_TC(ldexpf_inf_neg);
ATF_TC_HEAD(ldexpf_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ldexpf(-Inf) == -Inf");
}

ATF_TC_BODY(ldexpf_inf_neg, tc)
{
	const float x = -1.0L / 0.0L;
	size_t i;

	for (i = 0; i < __arraycount(exps); i++)
		ATF_CHECK(ldexpf(x, exps[i]) == x);
}

ATF_TC(ldexpf_inf_pos);
ATF_TC_HEAD(ldexpf_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ldexpf(+Inf) == +Inf");
}

ATF_TC_BODY(ldexpf_inf_pos, tc)
{
	const float x = 1.0L / 0.0L;
	size_t i;

	for (i = 0; i < __arraycount(exps); i++)
		ATF_CHECK(ldexpf(x, exps[i]) == x);
}

ATF_TC(ldexpf_zero_neg);
ATF_TC_HEAD(ldexpf_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ldexpf(-0.0) == -0.0");
}

ATF_TC_BODY(ldexpf_zero_neg, tc)
{
	const float x = -0.0L;
	float y;
	size_t i;

	ATF_REQUIRE(signbit(x) != 0);

	for (i = 0; i < __arraycount(exps); i++) {
		y = ldexpf(x, exps[i]);
		ATF_CHECK(x == y);
		ATF_CHECK(signbit(y) != 0);
	}
}

ATF_TC(ldexpf_zero_pos);
ATF_TC_HEAD(ldexpf_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ldexpf(+0.0) == +0.0");
}

ATF_TC_BODY(ldexpf_zero_pos, tc)
{
	const float x = 0.0L;
	float y;
	size_t i;

	ATF_REQUIRE(signbit(x) == 0);

	for (i = 0; i < __arraycount(exps); i++) {
		y = ldexpf(x, exps[i]);
		ATF_CHECK(x == y);
		ATF_CHECK(signbit(y) == 0);
	}
}

#define TEST(name, desc)						\
	ATF_TC(name);							\
	ATF_TC_HEAD(name, tc)						\
	{								\
									\
		atf_tc_set_md_var(tc, "descr",				\
		    "Test ldexp(3) for " ___STRING(desc));		\
	}								\
	ATF_TC_BODY(name, tc)						\
	{								\
		if (strcmp("vax", MACHINE_ARCH) == 0)			\
			atf_tc_skip("Test not valid for " MACHINE_ARCH); \
		run_test(name);						\
	}

TEST(ldexp_basic, basics)
TEST(ldexp_zero, zero)
TEST(ldexp_infinity, infinity)
TEST(ldexp_overflow, overflow)
TEST(ldexp_denormal, denormal)
TEST(ldexp_denormal_large, large)
TEST(ldexp_underflow, underflow)

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, ldexp_basic);
	ATF_TP_ADD_TC(tp, ldexp_zero);
	ATF_TP_ADD_TC(tp, ldexp_infinity);
	ATF_TP_ADD_TC(tp, ldexp_overflow);
	ATF_TP_ADD_TC(tp, ldexp_denormal);
	ATF_TP_ADD_TC(tp, ldexp_underflow);
	ATF_TP_ADD_TC(tp, ldexp_denormal_large);

	ATF_TP_ADD_TC(tp, ldexp_exp2);
	ATF_TP_ADD_TC(tp, ldexp_nan);
	ATF_TP_ADD_TC(tp, ldexp_inf_neg);
	ATF_TP_ADD_TC(tp, ldexp_inf_pos);
	ATF_TP_ADD_TC(tp, ldexp_zero_neg);
	ATF_TP_ADD_TC(tp, ldexp_zero_pos);

	ATF_TP_ADD_TC(tp, ldexpf_exp2f);
	ATF_TP_ADD_TC(tp, ldexpf_nan);
	ATF_TP_ADD_TC(tp, ldexpf_inf_neg);
	ATF_TP_ADD_TC(tp, ldexpf_inf_pos);
	ATF_TP_ADD_TC(tp, ldexpf_zero_neg);
	ATF_TP_ADD_TC(tp, ldexpf_zero_pos);

	return atf_no_error();
}
