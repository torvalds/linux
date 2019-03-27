/* This file is distributed under the University of Illinois Open Source
 * License. See LICENSE.TXT for details.
 */

/* int64_t __fixunstfdi(long double x);
 * This file implements the PowerPC 128-bit double-double -> int64_t conversion
 */

#include "DD.h"
#include "../int_math.h"

uint64_t __fixtfdi(long double input)
{
	const DD x = { .ld = input };
	const doublebits hibits = { .d = x.s.hi };
	
	const uint32_t absHighWord = (uint32_t)(hibits.x >> 32) & UINT32_C(0x7fffffff);
	const uint32_t absHighWordMinusOne = absHighWord - UINT32_C(0x3ff00000);
	
	/* If (1.0 - tiny) <= input < 0x1.0p63: */
	if (UINT32_C(0x03f00000) > absHighWordMinusOne)
	{
		/* Do an unsigned conversion of the absolute value, then restore the sign. */
		const int unbiasedHeadExponent = absHighWordMinusOne >> 20;
		
		int64_t result = hibits.x & INT64_C(0x000fffffffffffff); /* mantissa(hi) */
		result |= INT64_C(0x0010000000000000); /* matissa(hi) with implicit bit */
		result <<= 10; /* mantissa(hi) with one zero preceding bit. */
		
		const int64_t hiNegationMask = ((int64_t)(hibits.x)) >> 63;
		
		/* If the tail is non-zero, we need to patch in the tail bits. */
		if (0.0 != x.s.lo)
		{
			const doublebits lobits = { .d = x.s.lo };
			int64_t tailMantissa = lobits.x & INT64_C(0x000fffffffffffff);
			tailMantissa |= INT64_C(0x0010000000000000);
			
			/* At this point we have the mantissa of |tail| */
			/* We need to negate it if head and tail have different signs. */
			const int64_t loNegationMask = ((int64_t)(lobits.x)) >> 63;
			const int64_t negationMask = loNegationMask ^ hiNegationMask;
			tailMantissa = (tailMantissa ^ negationMask) - negationMask;
			
			/* Now we have the mantissa of tail as a signed 2s-complement integer */
			
			const int biasedTailExponent = (int)(lobits.x >> 52) & 0x7ff;
			
			/* Shift the tail mantissa into the right position, accounting for the
			 * bias of 10 that we shifted the head mantissa by.
			 */ 
			tailMantissa >>= (unbiasedHeadExponent - (biasedTailExponent - (1023 - 10)));
			
			result += tailMantissa;
		}
		
		result >>= (62 - unbiasedHeadExponent);
		
		/* Restore the sign of the result and return */
		result = (result ^ hiNegationMask) - hiNegationMask;
		return result;
		
	}

	/* Edge cases handled here: */
	
	/* |x| < 1, result is zero. */
	if (1.0 > crt_fabs(x.s.hi))
		return INT64_C(0);
	
	/* x very close to INT64_MIN, care must be taken to see which side we are on. */
	if (x.s.hi == -0x1.0p63) {
		
		int64_t result = INT64_MIN;
		
		if (0.0 < x.s.lo)
		{
			/* If the tail is positive, the correct result is something other than INT64_MIN.
			 * we'll need to figure out what it is.
			 */

			const doublebits lobits = { .d = x.s.lo };
			int64_t tailMantissa = lobits.x & INT64_C(0x000fffffffffffff);
			tailMantissa |= INT64_C(0x0010000000000000);
			
			/* Now we negate the tailMantissa */
			tailMantissa = (tailMantissa ^ INT64_C(-1)) + INT64_C(1);
			
			/* And shift it by the appropriate amount */
			const int biasedTailExponent = (int)(lobits.x >> 52) & 0x7ff;
			tailMantissa >>= 1075 - biasedTailExponent;
			
			result -= tailMantissa;
		}
		
		return result;
	}
	
	/* Signed overflows, infinities, and NaNs */
	if (x.s.hi > 0.0)
		return INT64_MAX;
	else
		return INT64_MIN;
}
