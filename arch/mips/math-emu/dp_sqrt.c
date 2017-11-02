/* IEEE754 floating point arithmetic
 * double precision square root
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

#include "ieee754dp.h"

static const unsigned int table[] = {
	0, 1204, 3062, 5746, 9193, 13348, 18162, 23592,
	29598, 36145, 43202, 50740, 58733, 67158, 75992,
	85215, 83599, 71378, 60428, 50647, 41945, 34246,
	27478, 21581, 16499, 12183, 8588, 5674, 3403,
	1742, 661, 130
};

union ieee754dp ieee754dp_sqrt(union ieee754dp x)
{
	struct _ieee754_csr oldcsr;
	union ieee754dp y, z, t;
	unsigned int scalx, yh;
	COMPXDP;

	EXPLODEXDP;
	ieee754_clearcx();
	FLUSHXDP;

	/* x == INF or NAN? */
	switch (xc) {
	case IEEE754_CLASS_SNAN:
		return ieee754dp_nanxcpt(x);

	case IEEE754_CLASS_QNAN:
		/* sqrt(Nan) = Nan */
		return x;

	case IEEE754_CLASS_ZERO:
		/* sqrt(0) = 0 */
		return x;

	case IEEE754_CLASS_INF:
		if (xs) {
			/* sqrt(-Inf) = Nan */
			ieee754_setcx(IEEE754_INVALID_OPERATION);
			return ieee754dp_indef();
		}
		/* sqrt(+Inf) = Inf */
		return x;

	case IEEE754_CLASS_DNORM:
		DPDNORMX;
		/* fall through */

	case IEEE754_CLASS_NORM:
		if (xs) {
			/* sqrt(-x) = Nan */
			ieee754_setcx(IEEE754_INVALID_OPERATION);
			return ieee754dp_indef();
		}
		break;
	}

	/* save old csr; switch off INX enable & flag; set RN rounding */
	oldcsr = ieee754_csr;
	ieee754_csr.mx &= ~IEEE754_INEXACT;
	ieee754_csr.sx &= ~IEEE754_INEXACT;
	ieee754_csr.rm = FPU_CSR_RN;

	/* adjust exponent to prevent overflow */
	scalx = 0;
	if (xe > 512) {		/* x > 2**-512? */
		xe -= 512;	/* x = x / 2**512 */
		scalx += 256;
	} else if (xe < -512) { /* x < 2**-512? */
		xe += 512;	/* x = x * 2**512 */
		scalx -= 256;
	}

	y = x = builddp(0, xe + DP_EBIAS, xm & ~DP_HIDDEN_BIT);

	/* magic initial approximation to almost 8 sig. bits */
	yh = y.bits >> 32;
	yh = (yh >> 1) + 0x1ff80000;
	yh = yh - table[(yh >> 15) & 31];
	y.bits = ((u64) yh << 32) | (y.bits & 0xffffffff);

	/* Heron's rule once with correction to improve to ~18 sig. bits */
	/* t=x/y; y=y+t; py[n0]=py[n0]-0x00100006; py[n1]=0; */
	t = ieee754dp_div(x, y);
	y = ieee754dp_add(y, t);
	y.bits -= 0x0010000600000000LL;
	y.bits &= 0xffffffff00000000LL;

	/* triple to almost 56 sig. bits: y ~= sqrt(x) to within 1 ulp */
	/* t=y*y; z=t;	pt[n0]+=0x00100000; t+=z; z=(x-z)*y; */
	z = t = ieee754dp_mul(y, y);
	t.bexp += 0x001;
	t = ieee754dp_add(t, z);
	z = ieee754dp_mul(ieee754dp_sub(x, z), y);

	/* t=z/(t+x) ;	pt[n0]+=0x00100000; y+=t; */
	t = ieee754dp_div(z, ieee754dp_add(t, x));
	t.bexp += 0x001;
	y = ieee754dp_add(y, t);

	/* twiddle last bit to force y correctly rounded */

	/* set RZ, clear INEX flag */
	ieee754_csr.rm = FPU_CSR_RZ;
	ieee754_csr.sx &= ~IEEE754_INEXACT;

	/* t=x/y; ...chopped quotient, possibly inexact */
	t = ieee754dp_div(x, y);

	if (ieee754_csr.sx & IEEE754_INEXACT || t.bits != y.bits) {

		if (!(ieee754_csr.sx & IEEE754_INEXACT))
			/* t = t-ulp */
			t.bits -= 1;

		/* add inexact to result status */
		oldcsr.cx |= IEEE754_INEXACT;
		oldcsr.sx |= IEEE754_INEXACT;

		switch (oldcsr.rm) {
		case FPU_CSR_RU:
			y.bits += 1;
			/* drop through */
		case FPU_CSR_RN:
			t.bits += 1;
			break;
		}

		/* y=y+t; ...chopped sum */
		y = ieee754dp_add(y, t);

		/* adjust scalx for correctly rounded sqrt(x) */
		scalx -= 1;
	}

	/* py[n0]=py[n0]+scalx; ...scale back y */
	y.bexp += scalx;

	/* restore rounding mode, possibly set inexact */
	ieee754_csr = oldcsr;

	return y;
}
