/*
 * IEEE754 floating point arithmetic
 * single precision: MADDF.f (Fused Multiply Add)
 * MADDF.fmt: FPR[fd] = FPR[fd] + (FPR[fs] x FPR[ft])
 *
 * MIPS floating point support
 * Copyright (C) 2015 Imagination Technologies, Ltd.
 * Author: Markos Chandras <markos.chandras@imgtec.com>
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; version 2 of the License.
 */

#include "ieee754sp.h"


static union ieee754sp _sp_maddf(union ieee754sp z, union ieee754sp x,
				 union ieee754sp y, enum maddf_flags flags)
{
	int re;
	int rs;
	unsigned rm;
	uint64_t rm64;
	uint64_t zm64;
	int s;

	COMPXSP;
	COMPYSP;
	COMPZSP;

	EXPLODEXSP;
	EXPLODEYSP;
	EXPLODEZSP;

	FLUSHXSP;
	FLUSHYSP;
	FLUSHZSP;

	ieee754_clearcx();

	/*
	 * Handle the cases when at least one of x, y or z is a NaN.
	 * Order of precedence is sNaN, qNaN and z, x, y.
	 */
	if (zc == IEEE754_CLASS_SNAN)
		return ieee754sp_nanxcpt(z);
	if (xc == IEEE754_CLASS_SNAN)
		return ieee754sp_nanxcpt(x);
	if (yc == IEEE754_CLASS_SNAN)
		return ieee754sp_nanxcpt(y);
	if (zc == IEEE754_CLASS_QNAN)
		return z;
	if (xc == IEEE754_CLASS_QNAN)
		return x;
	if (yc == IEEE754_CLASS_QNAN)
		return y;

	if (zc == IEEE754_CLASS_DNORM)
		SPDNORMZ;
	/* ZERO z cases are handled separately below */

	switch (CLPAIR(xc, yc)) {


	/*
	 * Infinity handling
	 */
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_INF):
		ieee754_setcx(IEEE754_INVALID_OPERATION);
		return ieee754sp_indef();

	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_INF):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_INF):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_DNORM):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_INF):
		if ((zc == IEEE754_CLASS_INF) &&
		    ((!(flags & MADDF_NEGATE_PRODUCT) && (zs != (xs ^ ys))) ||
		     ((flags & MADDF_NEGATE_PRODUCT) && (zs == (xs ^ ys))))) {
			/*
			 * Cases of addition of infinities with opposite signs
			 * or subtraction of infinities with same signs.
			 */
			ieee754_setcx(IEEE754_INVALID_OPERATION);
			return ieee754sp_indef();
		}
		/*
		 * z is here either not an infinity, or an infinity having the
		 * same sign as product (x*y) (in case of MADDF.D instruction)
		 * or product -(x*y) (in MSUBF.D case). The result must be an
		 * infinity, and its sign is determined only by the value of
		 * (flags & MADDF_NEGATE_PRODUCT) and the signs of x and y.
		 */
		if (flags & MADDF_NEGATE_PRODUCT)
			return ieee754sp_inf(1 ^ (xs ^ ys));
		else
			return ieee754sp_inf(xs ^ ys);

	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_DNORM):
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_ZERO):
		if (zc == IEEE754_CLASS_INF)
			return ieee754sp_inf(zs);
		if (zc == IEEE754_CLASS_ZERO) {
			/* Handle cases +0 + (-0) and similar ones. */
			if ((!(flags & MADDF_NEGATE_PRODUCT)
					&& (zs == (xs ^ ys))) ||
			    ((flags & MADDF_NEGATE_PRODUCT)
					&& (zs != (xs ^ ys))))
				/*
				 * Cases of addition of zeros of equal signs
				 * or subtraction of zeroes of opposite signs.
				 * The sign of the resulting zero is in any
				 * such case determined only by the sign of z.
				 */
				return z;

			return ieee754sp_zero(ieee754_csr.rm == FPU_CSR_RD);
		}
		/* x*y is here 0, and z is not 0, so just return z */
		return z;

	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_DNORM):
		SPDNORMX;

	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_DNORM):
		if (zc == IEEE754_CLASS_INF)
			return ieee754sp_inf(zs);
		SPDNORMY;
		break;

	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_NORM):
		if (zc == IEEE754_CLASS_INF)
			return ieee754sp_inf(zs);
		SPDNORMX;
		break;

	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_NORM):
		if (zc == IEEE754_CLASS_INF)
			return ieee754sp_inf(zs);
		/* fall through to real computations */
	}

	/* Finally get to do some computation */

	/*
	 * Do the multiplication bit first
	 *
	 * rm = xm * ym, re = xe + ye basically
	 *
	 * At this point xm and ym should have been normalized.
	 */

	/* rm = xm * ym, re = xe+ye basically */
	assert(xm & SP_HIDDEN_BIT);
	assert(ym & SP_HIDDEN_BIT);

	re = xe + ye;
	rs = xs ^ ys;
	if (flags & MADDF_NEGATE_PRODUCT)
		rs ^= 1;

	/* Multiple 24 bit xm and ym to give 48 bit results */
	rm64 = (uint64_t)xm * ym;

	/* Shunt to top of word */
	rm64 = rm64 << 16;

	/* Put explicit bit at bit 62 if necessary */
	if ((int64_t) rm64 < 0) {
		rm64 = rm64 >> 1;
		re++;
	}

	assert(rm64 & (1 << 62));

	if (zc == IEEE754_CLASS_ZERO) {
		/*
		 * Move explicit bit from bit 62 to bit 26 since the
		 * ieee754sp_format code expects the mantissa to be
		 * 27 bits wide (24 + 3 rounding bits).
		 */
		rm = XSPSRS64(rm64, (62 - 26));
		return ieee754sp_format(rs, re, rm);
	}

	/* Move explicit bit from bit 23 to bit 62 */
	zm64 = (uint64_t)zm << (62 - 23);
	assert(zm64 & (1 << 62));

	/* Make the exponents the same */
	if (ze > re) {
		/*
		 * Have to shift r fraction right to align.
		 */
		s = ze - re;
		rm64 = XSPSRS64(rm64, s);
		re += s;
	} else if (re > ze) {
		/*
		 * Have to shift z fraction right to align.
		 */
		s = re - ze;
		zm64 = XSPSRS64(zm64, s);
		ze += s;
	}
	assert(ze == re);
	assert(ze <= SP_EMAX);

	/* Do the addition */
	if (zs == rs) {
		/*
		 * Generate 64 bit result by adding two 63 bit numbers
		 * leaving result in zm64, zs and ze.
		 */
		zm64 = zm64 + rm64;
		if ((int64_t)zm64 < 0) {	/* carry out */
			zm64 = XSPSRS1(zm64);
			ze++;
		}
	} else {
		if (zm64 >= rm64) {
			zm64 = zm64 - rm64;
		} else {
			zm64 = rm64 - zm64;
			zs = rs;
		}
		if (zm64 == 0)
			return ieee754sp_zero(ieee754_csr.rm == FPU_CSR_RD);

		/*
		 * Put explicit bit at bit 62 if necessary.
		 */
		while ((zm64 >> 62) == 0) {
			zm64 <<= 1;
			ze--;
		}
	}

	/*
	 * Move explicit bit from bit 62 to bit 26 since the
	 * ieee754sp_format code expects the mantissa to be
	 * 27 bits wide (24 + 3 rounding bits).
	 */
	zm = XSPSRS64(zm64, (62 - 26));

	return ieee754sp_format(zs, ze, zm);
}

union ieee754sp ieee754sp_maddf(union ieee754sp z, union ieee754sp x,
				union ieee754sp y)
{
	return _sp_maddf(z, x, y, 0);
}

union ieee754sp ieee754sp_msubf(union ieee754sp z, union ieee754sp x,
				union ieee754sp y)
{
	return _sp_maddf(z, x, y, MADDF_NEGATE_PRODUCT);
}
