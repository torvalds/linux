/* IEEE754 floating point arithmetic
 * double precision: common utilities
 */
/*
 * MIPS floating point support
 * Copyright (C) 1994-2000 Algorithmics Ltd.
 * Copyright (C) 2017 Imagination Technologies, Ltd.
 * Author: Aleksandar Markovic <aleksandar.markovic@imgtec.com>
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
 *  with this program.
 */

#include "ieee754dp.h"

union ieee754dp ieee754dp_rint(union ieee754dp x)
{
	union ieee754dp ret;
	u64 residue;
	int sticky;
	int round;
	int odd;

	COMPXDP;

	ieee754_clearcx();

	EXPLODEXDP;
	FLUSHXDP;

	if (xc == IEEE754_CLASS_SNAN)
		return ieee754dp_nanxcpt(x);

	if ((xc == IEEE754_CLASS_QNAN) ||
	    (xc == IEEE754_CLASS_INF) ||
	    (xc == IEEE754_CLASS_ZERO))
		return x;

	if (xe >= DP_FBITS)
		return x;

	if (xe < -1) {
		residue = xm;
		round = 0;
		sticky = residue != 0;
		xm = 0;
	} else {
		residue = xm << (64 - DP_FBITS + xe);
		round = (residue >> 63) != 0;
		sticky = (residue << 1) != 0;
		xm >>= DP_FBITS - xe;
	}

	odd = (xm & 0x1) != 0x0;

	switch (ieee754_csr.rm) {
	case FPU_CSR_RN:	/* toward nearest */
		if (round && (sticky || odd))
			xm++;
		break;
	case FPU_CSR_RZ:	/* toward zero */
		break;
	case FPU_CSR_RU:	/* toward +infinity */
		if ((round || sticky) && !xs)
			xm++;
		break;
	case FPU_CSR_RD:	/* toward -infinity */
		if ((round || sticky) && xs)
			xm++;
		break;
	}

	if (round || sticky)
		ieee754_setcx(IEEE754_INEXACT);

	ret = ieee754dp_flong(xm);
	DPSIGN(ret) = xs;

	return ret;
}
