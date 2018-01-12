/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __MATH_SUPPORT_H
#define __MATH_SUPPORT_H

#if defined(__KERNEL__)
#include <linux/kernel.h> /* Override the definition of max/min from linux kernel*/
#endif /*__KERNEL__*/

#if defined(_MSC_VER)
#include <stdlib.h> /* Override the definition of max/min from stdlib.h*/
#endif /* _MSC_VER */

/* in case we have min/max/MIN/MAX macro's undefine them */
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#ifdef MIN /* also defined in include/hrt/numeric.h from SDK */
#undef MIN
#endif
#ifdef MAX
#undef MAX
#endif
#ifdef ABS
#undef ABS
#endif

#define IS_ODD(a)            ((a) & 0x1)
#define IS_EVEN(a)           (!IS_ODD(a))

/* force a value to a lower even value */
#define EVEN_FLOOR(x)        ((x) & ~1)

#ifdef ISP2401
/* If the number is odd, find the next even number */
#define EVEN_CEIL(x)         ((IS_ODD(x)) ? ((x) + 1) : (x))

#endif
/* A => B */
#define IMPLIES(a, b)        (!(a) || (b))

#define ABS(a)               ((a) >= 0 ? (a) : -(a))

/* for preprocessor and array sizing use MIN and MAX
   otherwise use min and max */
#define MAX(a, b)            (((a) > (b)) ? (a) : (b))
#define MIN(a, b)            (((a) < (b)) ? (a) : (b))
#ifdef ISP2401
#define ROUND_DIV(a, b)      (((b) != 0) ? ((a) + ((b) >> 1)) / (b) : 0)
#endif
#define CEIL_DIV(a, b)       (((b) != 0) ? ((a) + (b) - 1) / (b) : 0)
#define CEIL_MUL(a, b)       (CEIL_DIV(a, b) * (b))
#define CEIL_MUL2(a, b)      (((a) + (b) - 1) & ~((b) - 1))
#define CEIL_SHIFT(a, b)     (((a) + (1 << (b)) - 1)>>(b))
#define CEIL_SHIFT_MUL(a, b) (CEIL_SHIFT(a, b) << (b))
#ifdef ISP2401
#define ROUND_HALF_DOWN_DIV(a, b)	(((b) != 0) ? ((a) + (b / 2) - 1) / (b) : 0)
#define ROUND_HALF_DOWN_MUL(a, b)	(ROUND_HALF_DOWN_DIV(a, b) * (b))
#endif


/*To Find next power of 2 number from x */
#define bit2(x)            ((x)      | ((x) >> 1))
#define bit4(x)            (bit2(x)  | (bit2(x) >> 2))
#define bit8(x)            (bit4(x)  | (bit4(x) >> 4))
#define bit16(x)           (bit8(x)  | (bit8(x) >> 8))
#define bit32(x)           (bit16(x) | (bit16(x) >> 16))
#define NEXT_POWER_OF_2(x) (bit32(x-1) + 1)


/* min and max should not be macros as they will evaluate their arguments twice.
   if you really need a macro (e.g. for CPP or for initializing an array)
   use MIN() and MAX(), otherwise use min() and max().


*/

#if !defined(PIPE_GENERATION)

#ifndef INLINE_MATH_SUPPORT_UTILS
/*
This macro versions are added back as we are mixing types in usage of inline.
This causes corner cases of calculations to be incorrect due to conversions
between signed and unsigned variables or overflows.
Before the addition of the inline functions, max, min and ceil_div were macros
and therefore adding them back.

Leaving out the other math utility functions as they are newly added
*/

#define max(a, b)		(MAX(a, b))
#define min(a, b)		(MIN(a, b))
#define ceil_div(a, b)		(CEIL_DIV(a, b))

#else /* !defined(INLINE_MATH_SUPPORT_UTILS) */

static inline int max(int a, int b)
{
	return MAX(a, b);
}

static inline int min(int a, int b)
{
	return MIN(a, b);
}

static inline unsigned int ceil_div(unsigned int a, unsigned int b)
{
	return CEIL_DIV(a, b);
}
#endif /* !defined(INLINE_MATH_SUPPORT_UTILS) */

static inline unsigned int umax(unsigned int a, unsigned int b)
{
	return MAX(a, b);
}

static inline unsigned int umin(unsigned int a, unsigned int b)
{
	return MIN(a, b);
}


static inline unsigned int ceil_mul(unsigned int a, unsigned int b)
{
	return CEIL_MUL(a, b);
}

static inline unsigned int ceil_mul2(unsigned int a, unsigned int b)
{
	return CEIL_MUL2(a, b);
}

static inline unsigned int ceil_shift(unsigned int a, unsigned int b)
{
	return CEIL_SHIFT(a, b);
}

static inline unsigned int ceil_shift_mul(unsigned int a, unsigned int b)
{
	return CEIL_SHIFT_MUL(a, b);
}

#ifdef ISP2401
static inline unsigned int round_half_down_div(unsigned int a, unsigned int b)
{
	return ROUND_HALF_DOWN_DIV(a, b);
}

static inline unsigned int round_half_down_mul(unsigned int a, unsigned int b)
{
	return ROUND_HALF_DOWN_MUL(a, b);
}
#endif

/* @brief Next Power of Two
 *
 *  @param[in] unsigned number
 *
 *  @return next power of two
 *
 * This function rounds input to the nearest power of 2 (2^x)
 * towards infinity
 *
 * Input Range: 0 .. 2^(8*sizeof(int)-1)
 *
 * IF input is a power of 2
 *     out = in
 * OTHERWISE
 *     out = 2^(ceil(log2(in))
 *
 */

static inline unsigned int ceil_pow2(unsigned int a)
{
	if (a == 0) {
		return 1;
	}
	/* IF input is already a power of two*/
	else if ((!((a)&((a)-1)))) {
		return a;
	}
	else {
		unsigned int v = a;
		v |= v>>1;
		v |= v>>2;
		v |= v>>4;
		v |= v>>8;
		v |= v>>16;
		return (v+1);
	}
}

#endif /* !defined(PIPE_GENERATION) */

#if !defined(__ISP)
/*
 * For SP and ISP, SDK provides the definition of OP_std_modadd.
 * We need it only for host
 */
#define OP_std_modadd(base, offset, size) ((base+offset)%(size))
#endif /* !defined(__ISP) */

#if !defined(__KERNEL__)
#define clamp(a, min_val, max_val) MIN(MAX((a), (min_val)), (max_val))
#endif /* !defined(__KERNEL__) */

#endif /* __MATH_SUPPORT_H */
