/* IEEE754 floating point arithmetic
 * single precision
 */
/*
 * MIPS floating point support
 * Copyright (C) 1994-2000 Algorithmics Ltd.
 * http://www.algor.co.uk
 *
 * ########################################################################
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
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 */


#include <linux/kernel.h>
#include "ieee754sp.h"

int ieee754sp_tint(ieee754sp x)
{
	COMPXSP;

	CLEARCX;

	EXPLODEXSP;
	FLUSHXSP;

	switch (xc) {
	case IEEE754_CLASS_SNAN:
	case IEEE754_CLASS_QNAN:
	case IEEE754_CLASS_INF:
		SETCX(IEEE754_INVALID_OPERATION);
		return ieee754si_xcpt(ieee754si_indef(), "sp_tint", x);
	case IEEE754_CLASS_ZERO:
		return 0;
	case IEEE754_CLASS_DNORM:
	case IEEE754_CLASS_NORM:
		break;
	}
	if (xe >= 31) {
		/* look for valid corner case */
		if (xe == 31 && xs && xm == SP_HIDDEN_BIT)
			return -0x80000000;
		/* Set invalid. We will only use overflow for floating
		   point overflow */
		SETCX(IEEE754_INVALID_OPERATION);
		return ieee754si_xcpt(ieee754si_indef(), "sp_tint", x);
	}
	/* oh gawd */
	if (xe > SP_MBITS) {
		xm <<= xe - SP_MBITS;
	} else {
		u32 residue;
		int round;
		int sticky;
		int odd;

		if (xe < -1) {
			residue = xm;
			round = 0;
			sticky = residue != 0;
			xm = 0;
		}
		else {
			/* Shifting a u32 32 times does not work,
			* so we do it in two steps. Be aware that xe
			* may be -1 */
			residue = xm << (xe + 1);
			residue <<= 31 - SP_MBITS;
			round = (residue >> 31) != 0;
			sticky = (residue << 1) != 0;
			xm >>= SP_MBITS - xe;
		}
		odd = (xm & 0x1) != 0x0;
		switch (ieee754_csr.rm) {
		case IEEE754_RN:
			if (round && (sticky || odd))
				xm++;
			break;
		case IEEE754_RZ:
			break;
		case IEEE754_RU:	/* toward +Infinity */
			if ((round || sticky) && !xs)
				xm++;
			break;
		case IEEE754_RD:	/* toward -Infinity */
			if ((round || sticky) && xs)
				xm++;
			break;
		}
		if ((xm >> 31) != 0) {
			/* This can happen after rounding */
			SETCX(IEEE754_INVALID_OPERATION);
			return ieee754si_xcpt(ieee754si_indef(), "sp_tint", x);
		}
		if (round || sticky)
			SETCX(IEEE754_INEXACT);
	}
	if (xs)
		return -xm;
	else
		return xm;
}


unsigned int ieee754sp_tuns(ieee754sp x)
{
	ieee754sp hb = ieee754sp_1e31();

	/* what if x < 0 ?? */
	if (ieee754sp_lt(x, hb))
		return (unsigned) ieee754sp_tint(x);

	return (unsigned) ieee754sp_tint(ieee754sp_sub(x, hb)) |
	    ((unsigned) 1 << 31);
}
