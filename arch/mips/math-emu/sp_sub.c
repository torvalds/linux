/* IEEE754 floating point arithmetic
 * single precision
 */
/*
 * MIPS floating point support
 * Copyright (C) 1994-2000 Algorithmics Ltd.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#include "ieee754sp.h"

union ieee754sp ieee754sp_sub(union ieee754sp x, union ieee754sp y)
{
	int s;

	COMPXSP;
	COMPYSP;

	EXPLODEXSP;
	EXPLODEYSP;

	ieee754_clearcx();

	FLUSHXSP;
	FLUSHYSP;

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
		return ieee754sp_indef();

	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_INF):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_INF):
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_INF):
		return ieee754sp_inf(ys ^ 1);

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
			return ieee754sp_zero(ieee754_csr.rm == FPU_CSR_RD);

	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_ZERO):
		return x;

	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_DNORM):
		/* quick fix up */
		SPSIGN(y) ^= 1;
		return y;

	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_DNORM):
		SPDNORMX;
		/* fall through */

	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_DNORM):
		SPDNORMY;
		break;

	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_NORM):
		SPDNORMX;
		break;

	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_NORM):
		break;
	}
	/* flip sign of y and handle as add */
	ys ^= 1;

	assert(xm & SP_HIDDEN_BIT);
	assert(ym & SP_HIDDEN_BIT);


	/* provide guard,round and stick bit space */
	xm <<= 3;
	ym <<= 3;

	if (xe > ye) {
		/*
		 * have to shift y fraction right to align
		 */
		s = xe - ye;
		ym = XSPSRS(ym, s);
		ye += s;
	} else if (ye > xe) {
		/*
		 * have to shift x fraction right to align
		 */
		s = ye - xe;
		xm = XSPSRS(xm, s);
		xe += s;
	}
	assert(xe == ye);
	assert(xe <= SP_EMAX);

	if (xs == ys) {
		/* generate 28 bit result of adding two 27 bit numbers
		 */
		xm = xm + ym;

		if (xm >> (SP_FBITS + 1 + 3)) { /* carry out */
			SPXSRSX1();	/* shift preserving sticky */
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
				return ieee754sp_zero(1);	/* round negative inf. => sign = -1 */
			else
				return ieee754sp_zero(0);	/* other round modes   => sign = 1 */
		}
		/* normalize to rounding precision
		 */
		while ((xm >> (SP_FBITS + 3)) == 0) {
			xm <<= 1;
			xe--;
		}
	}

	return ieee754sp_format(xs, xe, xm);
}
