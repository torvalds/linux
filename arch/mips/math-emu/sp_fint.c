// SPDX-License-Identifier: GPL-2.0-only
/* IEEE754 floating point arithmetic
 * single precision
 */
/*
 * MIPS floating point support
 * Copyright (C) 1994-2000 Algorithmics Ltd.
 */

#include "ieee754sp.h"

union ieee754sp ieee754sp_fint(int x)
{
	unsigned int xm;
	int xe;
	int xs;

	ieee754_clearcx();

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
	xe = SP_FBITS + 3;

	if (xm >> (SP_FBITS + 1 + 3)) {
		/* shunt out overflow bits
		 */
		while (xm >> (SP_FBITS + 1 + 3)) {
			SPXSRSX1();
		}
	} else {
		/* normalize in grs extended single precision
		 */
		while ((xm >> (SP_FBITS + 3)) == 0) {
			xm <<= 1;
			xe--;
		}
	}
	return ieee754sp_format(xs, xe, xm);
}
