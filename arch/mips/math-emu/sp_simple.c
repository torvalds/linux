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

union ieee754sp ieee754sp_neg(union ieee754sp x)
{
	COMPXSP;

	EXPLODEXSP;
	ieee754_clearcx();
	FLUSHXSP;

	/*
	 * Invert the sign ALWAYS to prevent an endless recursion on
	 * pow() in libc.
	 */
	/* quick fix up */
	SPSIGN(x) ^= 1;

	if (xc == IEEE754_CLASS_SNAN) {
		union ieee754sp y = ieee754sp_indef();
		ieee754_setcx(IEEE754_INVALID_OPERATION);
		SPSIGN(y) = SPSIGN(x);
		return ieee754sp_nanxcpt(y);
	}

	return x;
}

union ieee754sp ieee754sp_abs(union ieee754sp x)
{
	COMPXSP;

	EXPLODEXSP;
	ieee754_clearcx();
	FLUSHXSP;

	/* Clear sign ALWAYS, irrespective of NaN */
	SPSIGN(x) = 0;

	if (xc == IEEE754_CLASS_SNAN) {
		ieee754_setcx(IEEE754_INVALID_OPERATION);
		return ieee754sp_nanxcpt(ieee754sp_indef());
	}

	return x;
}
