/* IEEE754 floating point arithmetic
 * double precision: common utilities
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
#include "ieee754dp.h"

union ieee754dp ieee754dp_fsp(union ieee754sp x)
{
	COMPXSP;

	EXPLODEXSP;

	ieee754_clearcx();

	FLUSHXSP;

	switch (xc) {
	case IEEE754_CLASS_SNAN:
		ieee754_setcx(IEEE754_INVALID_OPERATION);
		return ieee754dp_nanxcpt(ieee754dp_indef());

	case IEEE754_CLASS_QNAN:
		return ieee754dp_nanxcpt(builddp(xs,
						 DP_EMAX + 1 + DP_EBIAS,
						 ((u64) xm
						  << (DP_FBITS -
						      SP_FBITS))));
	case IEEE754_CLASS_INF:
		return ieee754dp_inf(xs);

	case IEEE754_CLASS_ZERO:
		return ieee754dp_zero(xs);

	case IEEE754_CLASS_DNORM:
		/* normalize */
		while ((xm >> SP_FBITS) == 0) {
			xm <<= 1;
			xe--;
		}
		break;

	case IEEE754_CLASS_NORM:
		break;
	}

	/*
	 * Can't possibly overflow,underflow, or need rounding
	 */

	/* drop the hidden bit */
	xm &= ~SP_HIDDEN_BIT;

	return builddp(xs, xe + DP_EBIAS,
		       (u64) xm << (DP_FBITS - SP_FBITS));
}
