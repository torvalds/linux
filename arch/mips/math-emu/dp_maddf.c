// SPDX-License-Identifier: GPL-2.0-only
/*
 * IEEE754 floating point arithmetic
 * double precision: MADDF.f (Fused Multiply Add)
 * MADDF.fmt: FPR[fd] = FPR[fd] + (FPR[fs] x FPR[ft])
 *
 * MIPS floating point support
 * Copyright (C) 2015 Imagination Technologies, Ltd.
 * Author: Markos Chandras <markos.chandras@imgtec.com>
 */

#include "ieee754dp.h"


/* 128 bits shift right logical with rounding. */
static void srl128(u64 *hptr, u64 *lptr, int count)
{
	u64 low;

	if (count >= 128) {
		*lptr = *hptr != 0 || *lptr != 0;
		*hptr = 0;
	} else if (count >= 64) {
		if (count == 64) {
			*lptr = *hptr | (*lptr != 0);
		} else {
			low = *lptr;
			*lptr = *hptr >> (count - 64);
			*lptr |= (*hptr << (128 - count)) != 0 || low != 0;
		}
		*hptr = 0;
	} else {
		low = *lptr;
		*lptr = low >> count | *hptr << (64 - count);
		*lptr |= (low << (64 - count)) != 0;
		*hptr = *hptr >> count;
	}
}

static union ieee754dp _dp_maddf(union ieee754dp z, union ieee754dp x,
				 union ieee754dp y, enum maddf_flags flags)
{
	int re;
	int rs;
	unsigned int lxm;
	unsigned int hxm;
	unsigned int lym;
	unsigned int hym;
	u64 lrm;
	u64 hrm;
	u64 lzm;
	u64 hzm;
	u64 t;
	u64 at;
	int s;

	COMPXDP;
	COMPYDP;
	COMPZDP;

	EXPLODEXDP;
	EXPLODEYDP;
	EXPLODEZDP;

	FLUSHXDP;
	FLUSHYDP;
	FLUSHZDP;

	ieee754_clearcx();

	/*
	 * Handle the cases when at least one of x, y or z is a NaN.
	 * Order of precedence is sNaN, qNaN and z, x, y.
	 */
	if (zc == IEEE754_CLASS_SNAN)
		return ieee754dp_nanxcpt(z);
	if (xc == IEEE754_CLASS_SNAN)
		return ieee754dp_nanxcpt(x);
	if (yc == IEEE754_CLASS_SNAN)
		return ieee754dp_nanxcpt(y);
	if (zc == IEEE754_CLASS_QNAN)
		return z;
	if (xc == IEEE754_CLASS_QNAN)
		return x;
	if (yc == IEEE754_CLASS_QNAN)
		return y;

	if (zc == IEEE754_CLASS_DNORM)
		DPDNORMZ;
	/* ZERO z cases are handled separately below */

	switch (CLPAIR(xc, yc)) {

	/*
	 * Infinity handling
	 */
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_INF):
		ieee754_setcx(IEEE754_INVALID_OPERATION);
		return ieee754dp_indef();

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
			return ieee754dp_indef();
		}
		/*
		 * z is here either not an infinity, or an infinity having the
		 * same sign as product (x*y) (in case of MADDF.D instruction)
		 * or product -(x*y) (in MSUBF.D case). The result must be an
		 * infinity, and its sign is determined only by the value of
		 * (flags & MADDF_NEGATE_PRODUCT) and the signs of x and y.
		 */
		if (flags & MADDF_NEGATE_PRODUCT)
			return ieee754dp_inf(1 ^ (xs ^ ys));
		else
			return ieee754dp_inf(xs ^ ys);

	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_DNORM):
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_ZERO):
		if (zc == IEEE754_CLASS_INF)
			return ieee754dp_inf(zs);
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

			return ieee754dp_zero(ieee754_csr.rm == FPU_CSR_RD);
		}
		/* x*y is here 0, and z is not 0, so just return z */
		return z;

	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_DNORM):
		DPDNORMX;
		/* fall through */

	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_DNORM):
		if (zc == IEEE754_CLASS_INF)
			return ieee754dp_inf(zs);
		DPDNORMY;
		break;

	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_NORM):
		if (zc == IEEE754_CLASS_INF)
			return ieee754dp_inf(zs);
		DPDNORMX;
		break;

	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_NORM):
		if (zc == IEEE754_CLASS_INF)
			return ieee754dp_inf(zs);
		/* continue to real computations */
	}

	/* Finally get to do some computation */

	/*
	 * Do the multiplication bit first
	 *
	 * rm = xm * ym, re = xe + ye basically
	 *
	 * At this point xm and ym should have been normalized.
	 */
	assert(xm & DP_HIDDEN_BIT);
	assert(ym & DP_HIDDEN_BIT);

	re = xe + ye;
	rs = xs ^ ys;
	if (flags & MADDF_NEGATE_PRODUCT)
		rs ^= 1;

	/* shunt to top of word */
	xm <<= 64 - (DP_FBITS + 1);
	ym <<= 64 - (DP_FBITS + 1);

	/*
	 * Multiply 64 bits xm and ym to give 128 bits result in hrm:lrm.
	 */

	lxm = xm;
	hxm = xm >> 32;
	lym = ym;
	hym = ym >> 32;

	lrm = DPXMULT(lxm, lym);
	hrm = DPXMULT(hxm, hym);

	t = DPXMULT(lxm, hym);

	at = lrm + (t << 32);
	hrm += at < lrm;
	lrm = at;

	hrm = hrm + (t >> 32);

	t = DPXMULT(hxm, lym);

	at = lrm + (t << 32);
	hrm += at < lrm;
	lrm = at;

	hrm = hrm + (t >> 32);

	/* Put explicit bit at bit 126 if necessary */
	if ((int64_t)hrm < 0) {
		lrm = (hrm << 63) | (lrm >> 1);
		hrm = hrm >> 1;
		re++;
	}

	assert(hrm & (1 << 62));

	if (zc == IEEE754_CLASS_ZERO) {
		/*
		 * Move explicit bit from bit 126 to bit 55 since the
		 * ieee754dp_format code expects the mantissa to be
		 * 56 bits wide (53 + 3 rounding bits).
		 */
		srl128(&hrm, &lrm, (126 - 55));
		return ieee754dp_format(rs, re, lrm);
	}

	/* Move explicit bit from bit 52 to bit 126 */
	lzm = 0;
	hzm = zm << 10;
	assert(hzm & (1 << 62));

	/* Make the exponents the same */
	if (ze > re) {
		/*
		 * Have to shift y fraction right to align.
		 */
		s = ze - re;
		srl128(&hrm, &lrm, s);
		re += s;
	} else if (re > ze) {
		/*
		 * Have to shift x fraction right to align.
		 */
		s = re - ze;
		srl128(&hzm, &lzm, s);
		ze += s;
	}
	assert(ze == re);
	assert(ze <= DP_EMAX);

	/* Do the addition */
	if (zs == rs) {
		/*
		 * Generate 128 bit result by adding two 127 bit numbers
		 * leaving result in hzm:lzm, zs and ze.
		 */
		hzm = hzm + hrm + (lzm > (lzm + lrm));
		lzm = lzm + lrm;
		if ((int64_t)hzm < 0) {        /* carry out */
			srl128(&hzm, &lzm, 1);
			ze++;
		}
	} else {
		if (hzm > hrm || (hzm == hrm && lzm >= lrm)) {
			hzm = hzm - hrm - (lzm < lrm);
			lzm = lzm - lrm;
		} else {
			hzm = hrm - hzm - (lrm < lzm);
			lzm = lrm - lzm;
			zs = rs;
		}
		if (lzm == 0 && hzm == 0)
			return ieee754dp_zero(ieee754_csr.rm == FPU_CSR_RD);

		/*
		 * Put explicit bit at bit 126 if necessary.
		 */
		if (hzm == 0) {
			/* left shift by 63 or 64 bits */
			if ((int64_t)lzm < 0) {
				/* MSB of lzm is the explicit bit */
				hzm = lzm >> 1;
				lzm = lzm << 63;
				ze -= 63;
			} else {
				hzm = lzm;
				lzm = 0;
				ze -= 64;
			}
		}

		t = 0;
		while ((hzm >> (62 - t)) == 0)
			t++;

		assert(t <= 62);
		if (t) {
			hzm = hzm << t | lzm >> (64 - t);
			lzm = lzm << t;
			ze -= t;
		}
	}

	/*
	 * Move explicit bit from bit 126 to bit 55 since the
	 * ieee754dp_format code expects the mantissa to be
	 * 56 bits wide (53 + 3 rounding bits).
	 */
	srl128(&hzm, &lzm, (126 - 55));

	return ieee754dp_format(zs, ze, lzm);
}

union ieee754dp ieee754dp_maddf(union ieee754dp z, union ieee754dp x,
				union ieee754dp y)
{
	return _dp_maddf(z, x, y, 0);
}

union ieee754dp ieee754dp_msubf(union ieee754dp z, union ieee754dp x,
				union ieee754dp y)
{
	return _dp_maddf(z, x, y, MADDF_NEGATE_PRODUCT);
}
