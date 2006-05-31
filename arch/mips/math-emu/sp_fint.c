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

ieee754sp ieee754sp_fint(int x)
{
	unsigned xm;
	int xe;
	int xs;

	CLEARCX;

	if (x == 0)
		return ieee754sp_zero(0);
	if (x == 1 || x == -1)
		return ieee754sp_one(x < 0);
	if (x == 10 || x == -10)
		return ieee754sp_ten(x < 0);

	xs = (x < 0);
	if (xs) {
		if (x == (1 << 31))
			xm = ((unsigned) 1 << 31);	/* max neg can't be safely negated */
		else
			xm = -x;
	} else {
		xm = x;
	}
	xe = SP_MBITS + 3;

	if (xm >> (SP_MBITS + 1 + 3)) {
		/* shunt out overflow bits
		 */
		while (xm >> (SP_MBITS + 1 + 3)) {
			SPXSRSX1();
		}
	} else {
		/* normalize in grs extended single precision
		 */
		while ((xm >> (SP_MBITS + 3)) == 0) {
			xm <<= 1;
			xe--;
		}
	}
	SPNORMRET1(xs, xe, xm, "fint", x);
}


ieee754sp ieee754sp_funs(unsigned int u)
{
	if ((int) u < 0)
		return ieee754sp_add(ieee754sp_1e31(),
				     ieee754sp_fint(u & ~(1 << 31)));
	return ieee754sp_fint(u);
}
