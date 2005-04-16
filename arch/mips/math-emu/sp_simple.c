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


#include "ieee754sp.h"

int ieee754sp_finite(ieee754sp x)
{
	return SPBEXP(x) != SP_EMAX + 1 + SP_EBIAS;
}

ieee754sp ieee754sp_copysign(ieee754sp x, ieee754sp y)
{
	CLEARCX;
	SPSIGN(x) = SPSIGN(y);
	return x;
}


ieee754sp ieee754sp_neg(ieee754sp x)
{
	COMPXSP;

	EXPLODEXSP;
	CLEARCX;
	FLUSHXSP;

	if (xc == IEEE754_CLASS_SNAN) {
		SETCX(IEEE754_INVALID_OPERATION);
		return ieee754sp_nanxcpt(ieee754sp_indef(), "neg");
	}

	if (ieee754sp_isnan(x))	/* but not infinity */
		return ieee754sp_nanxcpt(x, "neg", x);

	/* quick fix up */
	SPSIGN(x) ^= 1;
	return x;
}


ieee754sp ieee754sp_abs(ieee754sp x)
{
	COMPXSP;

	EXPLODEXSP;
	CLEARCX;
	FLUSHXSP;

	if (xc == IEEE754_CLASS_SNAN) {
		SETCX(IEEE754_INVALID_OPERATION);
		return ieee754sp_nanxcpt(ieee754sp_indef(), "abs");
	}

	if (ieee754sp_isnan(x))	/* but not infinity */
		return ieee754sp_nanxcpt(x, "abs", x);

	/* quick fix up */
	SPSIGN(x) = 0;
	return x;
}
