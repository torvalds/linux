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

s64 ieee754sp_tlong(union ieee754sp x)
{
	u32 residue;
	int round;
	int sticky;
	int odd;

	COMPXDP;		/* <-- need 64-bit mantissa tmp */

	ieee754_clearcx();

	EXPLODEXSP;
	FLUSHXSP;

	switch (xc) {
	case IEEE754_CLASS_SNAN:
	case IEEE754_CLASS_QNAN:
		ieee754_setcx(IEEE754_INVALID_OPERATION);
		return ieee754di_indef();

	case IEEE754_CLASS_INF:
		ieee754_setcx(IEEE754_INVALID_OPERATION);
		return ieee754di_overflow(xs);

	case IEEE754_CLASS_ZERO:
		return 0;

	case IEEE754_CLASS_DNORM:
	case IEEE754_CLASS_NORM:
		break;
	}
	if (xe >= 63) {
		/* look for valid corner case */
		if (xe == 63 && xs && xm == SP_HIDDEN_BIT)
			return -0x8000000000000000LL;
		/* Set invalid. We will only use overflow for floating
		   point overflow */
		ieee754_setcx(IEEE754_INVALID_OPERATION);
		return ieee754di_overflow(xs);
	}
	/* oh gawd */
	if (xe > SP_FBITS) {
		xm <<= xe - SP_FBITS;
	} else if (xe < SP_FBITS) {
		if (xe < -1) {
			residue = xm;
			round = 0;
			sticky = residue != 0;
			xm = 0;
		} else {
			residue = xm << (32 - SP_FBITS + xe);
			round = (residue >> 31) != 0;
			sticky = (residue << 1) != 0;
			xm >>= SP_FBITS - xe;
		}
		odd = (xm & 0x1) != 0x0;
		switch (ieee754_csr.rm) {
		case FPU_CSR_RN:
			if (round && (sticky || odd))
				xm++;
			break;
		case FPU_CSR_RZ:
			break;
		case FPU_CSR_RU:	/* toward +Infinity */
			if ((round || sticky) && !xs)
				xm++;
			break;
		case FPU_CSR_RD:	/* toward -Infinity */
			if ((round || sticky) && xs)
				xm++;
			break;
		}
		if ((xm >> 63) != 0) {
			/* This can happen after rounding */
			ieee754_setcx(IEEE754_INVALID_OPERATION);
			return ieee754di_overflow(xs);
		}
		if (round || sticky)
			ieee754_setcx(IEEE754_INEXACT);
	}
	if (xs)
		return -xm;
	else
		return xm;
}
