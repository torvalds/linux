/*
 * Written by Maya Rashish <maya@NetBSD.org>
 * Public domain.
 *
 * Testing IEEE-754 rounding modes (and lrint)
 */

#include <atf-c.h>
#include <fenv.h>
#ifdef __HAVE_FENV
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/*#pragma STDC FENV_ACCESS ON gcc?? */

#define INT 9223L

#define EPSILON 0.001

static const struct {
	int round_mode;
	double input;
	long int expected;
} values[] = {
	{ FE_DOWNWARD,		3.7,		3},
	{ FE_DOWNWARD,		-3.7,		-4},
	{ FE_DOWNWARD,		+0,		0},
	{ FE_DOWNWARD,		-INT-0.01,	-INT-1},
	{ FE_DOWNWARD,		+INT-0.01,	INT-1},
	{ FE_DOWNWARD,		-INT+0.01,	-INT},
	{ FE_DOWNWARD,		+INT+0.01,	INT},
#if 0 /* cpu bugs? */
	{ FE_DOWNWARD,		-0,		-1},

	{ FE_UPWARD,		+0,		1},
#endif
	{ FE_UPWARD,		-0,		0},
	{ FE_UPWARD,		-123.7,		-123},
	{ FE_UPWARD,		123.999,	124},
	{ FE_UPWARD,		-INT-0.01,	-INT},
	{ FE_UPWARD,		+INT-0.01,	INT},
	{ FE_UPWARD,		-INT+0.01,	-INT+1},
	{ FE_UPWARD,		+INT+0.01,	INT+1},

	{ FE_TOWARDZERO,	1.99,		1},
	{ FE_TOWARDZERO,	-1.99,		-1},
	{ FE_TOWARDZERO,	0.2,		0},
	{ FE_TOWARDZERO,	INT+0.01,	INT},
	{ FE_TOWARDZERO,	INT-0.01,	INT - 1},
	{ FE_TOWARDZERO,	-INT+0.01,	-INT + 1},
	{ FE_TOWARDZERO,	+0,		0},
	{ FE_TOWARDZERO,	-0,		0},

	{ FE_TONEAREST,		-INT-0.01,	-INT},
	{ FE_TONEAREST,		+INT-0.01,	INT},
	{ FE_TONEAREST,		-INT+0.01,	-INT},
	{ FE_TONEAREST,		+INT+0.01,	INT},
	{ FE_TONEAREST,		-INT-0.501,	-INT-1},
	{ FE_TONEAREST,		+INT-0.501,	INT-1},
	{ FE_TONEAREST,		-INT+0.501,	-INT+1},
	{ FE_TONEAREST,		+INT+0.501,	INT+1},
	{ FE_TONEAREST,		+0,		0},
	{ FE_TONEAREST,		-0,		0},
};

ATF_TC(fe_round);
ATF_TC_HEAD(fe_round, tc)
{
	atf_tc_set_md_var(tc, "descr","Checking IEEE 754 rounding modes using lrint");
}

ATF_TC_BODY(fe_round, tc)
{
	long int received;

	for (unsigned int i = 0; i < __arraycount(values); i++) {
		fesetround(values[i].round_mode);

		received = lrint(values[i].input);
		ATF_CHECK_MSG(
		    (labs(received - values[i].expected) < EPSILON),
		    "lrint rounding wrong, difference too large\n"
		    "input: %f (index %d): got %ld, expected %ld\n",
		    values[i].input, i, received, values[i].expected);

		/* Do we get the same rounding mode out? */
		ATF_CHECK_MSG(
		    (fegetround() == values[i].round_mode),
		    "Didn't get the same rounding mode out!\n"
		    "(index %d) fed in %d rounding mode, got %d out\n",
		    i, fegetround(), values[i].round_mode);
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, fe_round);

	return atf_no_error();
}
#else
ATF_TC(t_nofe_round);

ATF_TC_HEAD(t_nofe_round, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "dummy test case - no fenv.h support");
}


ATF_TC_BODY(t_nofe_round, tc)
{
	atf_tc_skip("no fenv.h support on this architecture");
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, t_nofe_round);
	return atf_no_error();
}

#endif
