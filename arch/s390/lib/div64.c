/*
 *  arch/s390/lib/div64.c
 *
 *  __div64_32 implementation for 31 bit.
 *
 *    Copyright (C) IBM Corp. 2006
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 */

#include <linux/types.h>
#include <linux/module.h>

#ifdef CONFIG_MARCH_G5

/*
 * Function to divide an unsigned 64 bit integer by an unsigned
 * 31 bit integer using signed 64/32 bit division.
 */
static uint32_t __div64_31(uint64_t *n, uint32_t base)
{
	register uint32_t reg2 asm("2");
	register uint32_t reg3 asm("3");
	uint32_t *words = (uint32_t *) n;
	uint32_t tmp;

	/* Special case base==1, remainder = 0, quotient = n */
	if (base == 1)
		return 0;
	/*
	 * Special case base==0 will cause a fixed point divide exception
	 * on the dr instruction and may not happen anyway. For the
	 * following calculation we can assume base > 1. The first
	 * signed 64 / 32 bit division with an upper half of 0 will
	 * give the correct upper half of the 64 bit quotient.
	 */
	reg2 = 0UL;
	reg3 = words[0];
	asm volatile(
		"	dr	%0,%2\n"
		: "+d" (reg2), "+d" (reg3) : "d" (base) : "cc" );
	words[0] = reg3;
	reg3 = words[1];
	/*
	 * To get the lower half of the 64 bit quotient and the 32 bit
	 * remainder we have to use a little trick. Since we only have
	 * a signed division the quotient can get too big. To avoid this
	 * the 64 bit dividend is halved, then the signed division will
	 * work. Afterwards the quotient and the remainder are doubled.
	 * If the last bit of the dividend has been one the remainder
	 * is increased by one then checked against the base. If the
	 * remainder has overflown subtract base and increase the
	 * quotient. Simple, no ?
	 */
	asm volatile(
		"	nr	%2,%1\n"
		"	srdl	%0,1\n"
		"	dr	%0,%3\n"
		"	alr	%0,%0\n"
		"	alr	%1,%1\n"
		"	alr	%0,%2\n"
		"	clr	%0,%3\n"
		"	jl	0f\n"
		"	slr	%0,%3\n"
		"	ahi	%1,1\n"
		"0:\n"
		: "+d" (reg2), "+d" (reg3), "=d" (tmp)
		: "d" (base), "2" (1UL) : "cc" );
	words[1] = reg3;
	return reg2;
}

/*
 * Function to divide an unsigned 64 bit integer by an unsigned
 * 32 bit integer using the unsigned 64/31 bit division.
 */
uint32_t __div64_32(uint64_t *n, uint32_t base)
{
	uint32_t r;

	/*
	 * If the most significant bit of base is set, divide n by
	 * (base/2). That allows to use 64/31 bit division and gives a
	 * good approximation of the result: n = (base/2)*q + r. The
	 * result needs to be corrected with two simple transformations.
	 * If base is already < 2^31-1 __div64_31 can be used directly.
	 */
	r = __div64_31(n, ((signed) base < 0) ? (base/2) : base);
	if ((signed) base < 0) {
		uint64_t q = *n;
		/*
		 * First transformation:
		 * n = (base/2)*q + r
		 *   = ((base/2)*2)*(q/2) + ((q&1) ? (base/2) : 0) + r
		 * Since r < (base/2), r + (base/2) < base.
		 * With q1 = (q/2) and r1 = r + ((q&1) ? (base/2) : 0)
		 * n = ((base/2)*2)*q1 + r1 with r1 < base.
		 */
		if (q & 1)
			r += base/2;
		q >>= 1;
		/*
		 * Second transformation. ((base/2)*2) could have lost the
		 * last bit.
		 * n = ((base/2)*2)*q1 + r1
		 *   = base*q1 - ((base&1) ? q1 : 0) + r1
		 */
		if (base & 1) {
			int64_t rx = r - q;
			/*
			 * base is >= 2^31. The worst case for the while
			 * loop is n=2^64-1 base=2^31+1. That gives a
			 * maximum for q=(2^64-1)/2^31 = 0x1ffffffff. Since
			 * base >= 2^31 the loop is finished after a maximum
			 * of three iterations.
			 */
			while (rx < 0) {
				rx += base;
				q--;
			}
			r = rx;
		}
		*n = q;
	}
	return r;
}

#else /* MARCH_G5 */

uint32_t __div64_32(uint64_t *n, uint32_t base)
{
	register uint32_t reg2 asm("2");
	register uint32_t reg3 asm("3");
	uint32_t *words = (uint32_t *) n;

	reg2 = 0UL;
	reg3 = words[0];
	asm volatile(
		"	dlr	%0,%2\n"
		: "+d" (reg2), "+d" (reg3) : "d" (base) : "cc" );
	words[0] = reg3;
	reg3 = words[1];
	asm volatile(
		"	dlr	%0,%2\n"
		: "+d" (reg2), "+d" (reg3) : "d" (base) : "cc" );
	words[1] = reg3;
	return reg2;
}

#endif /* MARCH_G5 */
