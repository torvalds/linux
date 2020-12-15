// SPDX-License-Identifier: GPL-2.0-only
/* IEEE754 floating point arithmetic
 * double precision: common utilities
 */
/*
 * MIPS floating point support
 * Copyright (C) 1994-2000 Algorithmics Ltd.
 */

#include "ieee754dp.h"

union ieee754dp ieee754dp_sub(union ieee754dp x, union ieee754dp y)
{
	int s;

	COMPXDP;
	COMPYDP;

	EXPLODEXDP;
	EXPLODEYDP;

	ieee754_clearcx();

	FLUSHXDP;
	FLUSHYDP;

	switch (CLPAIR(xc, yc)) {
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_SNAN):
		return ieee754dp_nanxcpt(y);

	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_DNORM):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_INF):
		return ieee754dp_nanxcpt(x);

	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_QNAN):
		return y;

	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_DNORM):
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_INF):
		return x;


	/*
	 * Infinity handling
	 */
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_INF):
		if (xs != ys)
			return x;
		ieee754_setcx(IEEE754_INVALID_OPERATION);
		return ieee754dp_indef();

	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_INF):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_INF):
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_INF):
		return ieee754dp_inf(ys ^ 1);

	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_DNORM):
		return x;

	/*
	 * Zero handling
	 */
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_ZERO):
		if (xs != ys)
			return x;
		else
			return ieee754dp_zero(ieee754_csr.rm == FPU_CSR_RD);

	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_ZERO):
		return x;

	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_DNORM):
		/* quick fix up */
		DPSIGN(y) ^= 1;
		return y;

	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_DNORM):
		DPDNORMX;
		fallthrough;
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_DNORM):
		/* normalize ym,ye */
		DPDNORMY;
		break;

	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_NORM):
		/* normalize xm,xe */
		DPDNORMX;
		break;

	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_NORM):
		break;
	}
	/* flip sign of y and handle as add */
	ys ^= 1;

	assert(xm & DP_HIDDEN_BIT);
	assert(ym & DP_HIDDEN_BIT);


	/* provide guard,round and stick bit dpace */
	xm <<= 3;
	ym <<= 3;

	if (xe > ye) {
		/*
		 * Have to shift y fraction right to align
		 */
		s = xe - ye;
		ym = XDPSRS(ym, s);
		ye += s;
	} else if (ye > xe) {
		/*
		 * Have to shift x fraction right to align
		 */
		s = ye - xe;
		xm = XDPSRS(xm, s);
		xe += s;
	}
	assert(xe == ye);
	assert(xe <= DP_EMAX);

	if (xs == ys) {
		/* generate 28 bit result of adding two 27 bit numbers
		 */
		xm = xm + ym;

		if (xm >> (DP_FBITS + 1 + 3)) { /* carry out */
			xm = XDPSRS1(xm);	/* shift preserving sticky */
			xe++;
		}
	} else {
		if (xm >= ym) {
			xm = xm - ym;
		} else {
			xm = ym - xm;
			xs = ys;
		}
		if (xm == 0) {
			if (ieee754_csr.rm == FPU_CSR_RD)
				return ieee754dp_zero(1);	/* round negative inf. => sign = -1 */
			else
				return ieee754dp_zero(0);	/* other round modes   => sign = 1 */
		}

		/* normalize to rounding precision
		 */
		while ((xm >> (DP_FBITS + 3)) == 0) {
			xm <<= 1;
			xe--;
		}
	}

	return ieee754dp_format(xs, xe, xm);
}
