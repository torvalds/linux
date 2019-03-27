/* This file is distributed under the University of Illinois Open Source
 * License. See LICENSE.TXT for details.
 */

/* long double __gcc_qsub(long double x, long double y);
 * This file implements the PowerPC 128-bit double-double add operation.
 * This implementation is shamelessly cribbed from Apple's DDRT, circa 1993(!)
 */

#include "DD.h"

long double __gcc_qsub(long double x, long double y)
{
	static const uint32_t infinityHi = UINT32_C(0x7ff00000);
	
	DD dst = { .ld = x }, src = { .ld = y };
	
	register double A =  dst.s.hi, a =  dst.s.lo,
					B = -src.s.hi, b = -src.s.lo;
	
	/* If both operands are zero: */
	if ((A == 0.0) && (B == 0.0)) {
		dst.s.hi = A + B;
		dst.s.lo = 0.0;
		return dst.ld;
	}
	
	/* If either operand is NaN or infinity: */
	const doublebits abits = { .d = A };
	const doublebits bbits = { .d = B };
	if ((((uint32_t)(abits.x >> 32) & infinityHi) == infinityHi) ||
		(((uint32_t)(bbits.x >> 32) & infinityHi) == infinityHi)) {
		dst.s.hi = A + B;
		dst.s.lo = 0.0;
		return dst.ld;
	}
	
	/* If the computation overflows: */
	/* This may be playing things a little bit fast and loose, but it will do for a start. */
	const double testForOverflow = A + (B + (a + b));
	const doublebits testbits = { .d = testForOverflow };
	if (((uint32_t)(testbits.x >> 32) & infinityHi) == infinityHi) {
		dst.s.hi = testForOverflow;
		dst.s.lo = 0.0;
		return dst.ld;
	}
	
	double H, h;
	double T, t;
	double W, w;
	double Y;
	
	H = B + (A - (A + B));
	T = b + (a - (a + b));
	h = A + (B - (A + B));
	t = a + (b - (a + b));
	
	if (local_fabs(A) <= local_fabs(B))
		w = (a + b) + h;
	else
		w = (a + b) + H;
	
	W = (A + B) + w;
	Y = (A + B) - W;
	Y += w;
	
	if (local_fabs(a) <= local_fabs(b))
		w = t + Y;
	else
		w = T + Y;
	
	dst.s.hi = Y = W + w;
	dst.s.lo = (W - Y) + w;
	
	return dst.ld;
}
