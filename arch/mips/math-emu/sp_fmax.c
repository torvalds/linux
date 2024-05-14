// SPDX-License-Identifier: GPL-2.0-only
/*
 * IEEE754 floating point arithmetic
 * single precision: MAX{,A}.f
 * MAX : Scalar Floating-Point Maximum
 * MAXA: Scalar Floating-Point argument with Maximum Absolute Value
 *
 * MAX.S : FPR[fd] = maxNum(FPR[fs],FPR[ft])
 * MAXA.S: FPR[fd] = maxNumMag(FPR[fs],FPR[ft])
 *
 * MIPS floating point support
 * Copyright (C) 2015 Imagination Technologies, Ltd.
 * Author: Markos Chandras <markos.chandras@imgtec.com>
 */

#include "ieee754sp.h"

union ieee754sp ieee754sp_fmax(union ieee754sp x, union ieee754sp y)
{
	COMPXSP;
	COMPYSP;

	EXPLODEXSP;
	EXPLODEYSP;

	FLUSHXSP;
	FLUSHYSP;

	ieee754_clearcx();

	switch (CLPAIR(xc, yc)) {
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_SNAN):
		return ieee754sp_nanxcpt(y);

	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_DNORM):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_INF):
		return ieee754sp_nanxcpt(x);

	/*
	 * Quiet NaN handling
	 */

	/*
	 *    The case of both inputs quiet NaNs
	 */
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_QNAN):
		return x;

	/*
	 *    The cases of exactly one input quiet NaN (numbers
	 *    are here preferred as returned values to NaNs)
	 */
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_QNAN):
		return x;

	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_DNORM):
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_INF):
		return y;

	/*
	 * Infinity and zero handling
	 */
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_DNORM):
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_ZERO):
		return xs ? y : x;

	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_INF):
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_INF):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_INF):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_INF):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_DNORM):
		return ys ? x : y;

	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_ZERO):
		return ieee754sp_zero(xs & ys);

	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_DNORM):
		SPDNORMX;
		fallthrough;
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_DNORM):
		SPDNORMY;
		break;

	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_NORM):
		SPDNORMX;
	}

	/* Finally get to do some computation */

	assert(xm & SP_HIDDEN_BIT);
	assert(ym & SP_HIDDEN_BIT);

	/* Compare signs */
	if (xs > ys)
		return y;
	else if (xs < ys)
		return x;

	/* Signs of inputs are equal, let's compare exponents */
	if (xs == 0) {
		/* Inputs are both positive */
		if (xe > ye)
			return x;
		else if (xe < ye)
			return y;
	} else {
		/* Inputs are both negative */
		if (xe > ye)
			return y;
		else if (xe < ye)
			return x;
	}

	/* Signs and exponents of inputs are equal, let's compare mantissas */
	if (xs == 0) {
		/* Inputs are both positive, with equal signs and exponents */
		if (xm <= ym)
			return y;
		return x;
	}
	/* Inputs are both negative, with equal signs and exponents */
	if (xm <= ym)
		return x;
	return y;
}

union ieee754sp ieee754sp_fmaxa(union ieee754sp x, union ieee754sp y)
{
	COMPXSP;
	COMPYSP;

	EXPLODEXSP;
	EXPLODEYSP;

	FLUSHXSP;
	FLUSHYSP;

	ieee754_clearcx();

	switch (CLPAIR(xc, yc)) {
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_SNAN):
		return ieee754sp_nanxcpt(y);

	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_DNORM):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_INF):
		return ieee754sp_nanxcpt(x);

	/*
	 * Quiet NaN handling
	 */

	/*
	 *    The case of both inputs quiet NaNs
	 */
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_QNAN):
		return x;

	/*
	 *    The cases of exactly one input quiet NaN (numbers
	 *    are here preferred as returned values to NaNs)
	 */
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_QNAN):
		return x;

	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_DNORM):
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_INF):
		return y;

	/*
	 * Infinity and zero handling
	 */
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_INF):
		return ieee754sp_inf(xs & ys);

	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_DNORM):
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_ZERO):
		return x;

	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_INF):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_INF):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_INF):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_DNORM):
		return y;

	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_ZERO):
		return ieee754sp_zero(xs & ys);

	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_DNORM):
		SPDNORMX;
		fallthrough;
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_DNORM):
		SPDNORMY;
		break;

	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_NORM):
		SPDNORMX;
	}

	/* Finally get to do some computation */

	assert(xm & SP_HIDDEN_BIT);
	assert(ym & SP_HIDDEN_BIT);

	/* Compare exponent */
	if (xe > ye)
		return x;
	else if (xe < ye)
		return y;

	/* Compare mantissa */
	if (xm < ym)
		return y;
	else if (xm > ym)
		return x;
	else if (xs == 0)
		return x;
	return y;
}
