/* IEEE754 floating point arithmetic
 * single precision
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

#include "ieee754sp.h"

union ieee754sp ieee754sp_rint(union ieee754sp x)
{
	union ieee754sp ret;
	u32 residue;
	int sticky;
	int round;
	int odd;

	COMPXDP;		/* <-- DP needed for 64-bit mantissa tmp */

	ieee754_clearcx();

	EXPLODEXSP;
	FLUSHXSP;

	if (xc == IEEE754_CLASS_SNAN)
		return ieee754sp_nanxcpt(x);

	if ((xc == IEEE754_CLASS_QNAN) ||
	    (xc == IEEE754_CLASS_INF) ||
	    (xc == IEEE754_CLASS_ZERO))
		return x;

	if (xe >= SP_FBITS)
		return x;

	if (xe < -1) {
		residue = xm;
		round = 0;
		sticky = residue != 0;
		xm = 0;
	} else {
		residue = xm << (xe + 1);
		residue <<= 31 - SP_FBITS;
		round = (residue >> 31) != 0;
		sticky = (residue << 1) != 0;
		xm >>= SP_FBITS - xe;
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

	ret = ieee754sp_flong(xm);
	SPSIGN(ret) = xs;

	return ret;
}
