/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_DIV64_H
#define _ASM_X86_DIV64_H

#ifdef CONFIG_X86_32

#include <linux/types.h>
#include <linux/log2.h>

/*
 * do_div() is NOT a C function. It wants to return
 * two values (the quotient and the remainder), but
 * since that doesn't work very well in C, what it
 * does is:
 *
 * - modifies the 64-bit dividend _in_place_
 * - returns the 32-bit remainder
 *
 * This ends up being the most efficient "calling
 * convention" on x86.
 */
#define do_div(n, base)						\
({								\
	unsigned long __upper, __low, __high, __mod, __base;	\
	__base = (base);					\
	if (__builtin_constant_p(__base) && is_power_of_2(__base)) { \
		__mod = n & (__base - 1);			\
		n >>= ilog2(__base);				\
	} else {						\
		asm("" : "=a" (__low), "=d" (__high) : "A" (n));\
		__upper = __high;				\
		if (__high) {					\
			__upper = __high % (__base);		\
			__high = __high / (__base);		\
		}						\
		asm("divl %2" : "=a" (__low), "=d" (__mod)	\
			: "rm" (__base), "0" (__low), "1" (__upper));	\
		asm("" : "=A" (n) : "a" (__low), "d" (__high));	\
	}							\
	__mod;							\
})

static inline u64 div_u64_rem(u64 dividend, u32 divisor, u32 *remainder)
{
	union {
		u64 v64;
		u32 v32[2];
	} d = { dividend };
	u32 upper;

	upper = d.v32[1];
	d.v32[1] = 0;
	if (upper >= divisor) {
		d.v32[1] = upper / divisor;
		upper %= divisor;
	}
	asm ("divl %2" : "=a" (d.v32[0]), "=d" (*remainder) :
		"rm" (divisor), "0" (d.v32[0]), "1" (upper));
	return d.v64;
}
#define div_u64_rem	div_u64_rem

static inline u64 mul_u32_u32(u32 a, u32 b)
{
	u32 high, low;

	asm ("mull %[b]" : "=a" (low), "=d" (high)
			 : [a] "a" (a), [b] "rm" (b) );

	return low | ((u64)high) << 32;
}
#define mul_u32_u32 mul_u32_u32

/*
 * __div64_32() is never called on x86, so prevent the
 * generic definition from getting built.
 */
#define __div64_32

#else
# include <asm-generic/div64.h>

/*
 * Will generate an #DE when the result doesn't fit u64, could fix with an
 * __ex_table[] entry when it becomes an issue.
 */
static inline u64 mul_u64_u64_div_u64(u64 a, u64 mul, u64 div)
{
	u64 q;

	asm ("mulq %2; divq %3" : "=a" (q)
				: "a" (a), "rm" (mul), "rm" (div)
				: "rdx");

	return q;
}
#define mul_u64_u64_div_u64 mul_u64_u64_div_u64

static inline u64 mul_u64_u32_div(u64 a, u32 mul, u32 div)
{
	return mul_u64_u64_div_u64(a, mul, div);
}
#define mul_u64_u32_div	mul_u64_u32_div

#endif /* CONFIG_X86_32 */

#endif /* _ASM_X86_DIV64_H */
