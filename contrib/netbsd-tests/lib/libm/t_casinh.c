/* $NetBSD: t_casinh.c,v 1.2 2016/09/20 17:19:28 christos Exp $ */

/*
 * Written by Maya Rashish
 * Public domain.
 *
 * Testing special values of casinh
 * Values from ISO/IEC 9899:201x G.6.2.2
 */

#include <atf-c.h>
#include <complex.h>
#include <math.h>

#define RE(z) (((double *)(&z))[0])
#define IM(z) (((double *)(&z))[1])

static const struct {
	double input_re;
	double input_im;
	double result_re;
	double result_im;
} values[] = {
	{ +0,		+0,		+0,		+0},
	{ +5.032E3,	+INFINITY,	+INFINITY,	+M_PI/2},
	{ +INFINITY,	+5.023E3,	+INFINITY,	+0},
	{ +INFINITY,	+INFINITY,	+INFINITY,	+M_PI/4},
#ifdef __HAVE_NANF
	{ +INFINITY,	+NAN,		+INFINITY,	+NAN},
	{ +5.032E3,	+NAN,		+NAN,		+NAN}, /* + FE_INVALID optionally raised */
	{ +NAN,		+0,		+NAN,		+0},
	{ +NAN,		-5.023E3,	+NAN,		+NAN}, /* + FE_INVALID optionally raised */
	{ +NAN,		+INFINITY,	+INFINITY,	+NAN}, /* sign of real part of result unspecified */
	{ +NAN,		+NAN,		+NAN,		+NAN},
#endif
};

#ifdef __HAVE_NANF
#define both_nan(a,b) (isnan(a) && isnan(b))
#else
#define both_nan(a,b) 0
#endif

#define crude_equality(a,b) ((a == b) || both_nan(a,b))

#define ATF_COMPLEX_EQUAL(a,b) do { \
	complex double ci = casinh(a); \
	ATF_CHECK_MSG(crude_equality(creal(ci),creal(b)) && \
	    crude_equality(cimag(ci), cimag(b)), \
	    "for casinh([%g,%g]) = [%g,%g] != [%g,%g]", \
	    creal(a), cimag(a), creal(ci), cimag(ci), creal(b), cimag(b)); \
} while (0/*CONSTCOND*/)


ATF_TC(casinh);
ATF_TC_HEAD(casinh, tc)
{
	atf_tc_set_md_var(tc, "descr","Check casinh family - special values");
}

ATF_TC_BODY(casinh, tc)
{
	complex double input;
	complex double result;
	unsigned int i;
	for (i = 0; i < __arraycount(values); i++) {
		RE(input) = values[i].input_re;
		IM(input) = values[i].input_im;
		RE(result) = values[i].result_re;
		IM(result) = values[i].result_im;
		ATF_COMPLEX_EQUAL(input, result);
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, casinh);

	return atf_no_error();
}
