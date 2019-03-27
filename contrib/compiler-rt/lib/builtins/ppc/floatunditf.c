/* This file is distributed under the University of Illinois Open Source
 * License. See LICENSE.TXT for details.
 */

/* long double __floatunditf(unsigned long long x); */
/* This file implements the PowerPC unsigned long long -> long double conversion */

#include "DD.h"

long double __floatunditf(uint64_t a) {
	
	/* Begins with an exact copy of the code from __floatundidf */
	
	static const double twop52 = 0x1.0p52;
	static const double twop84 = 0x1.0p84;
	static const double twop84_plus_twop52 = 0x1.00000001p84;
	
	doublebits high = { .d = twop84 };
	doublebits low  = { .d = twop52 };
	
	high.x |= a >> 32;							/* 0x1.0p84 + high 32 bits of a */
	low.x |= a & UINT64_C(0x00000000ffffffff);	/* 0x1.0p52 + low 32 bits of a */
	
	const double high_addend = high.d - twop84_plus_twop52;
	
	/* At this point, we have two double precision numbers
	 * high_addend and low.d, and we wish to return their sum
	 * as a canonicalized long double:
	 */

	/* This implementation sets the inexact flag spuriously. */
	/* This could be avoided, but at some substantial cost. */
	
	DD result;
	
	result.s.hi = high_addend + low.d;
	result.s.lo = (high_addend - result.s.hi) + low.d;
	
	return result.ld;
	
}
