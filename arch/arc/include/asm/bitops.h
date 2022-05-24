/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef _ASM_BITOPS_H
#define _ASM_BITOPS_H

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <linux/compiler.h>

#ifdef CONFIG_ISA_ARCOMPACT

/*
 * Count the number of zeros, starting from MSB
 * Helper for fls( ) friends
 * This is a pure count, so (1-32) or (0-31) doesn't apply
 * It could be 0 to 32, based on num of 0's in there
 * clz(0x8000_0000) = 0, clz(0xFFFF_FFFF)=0, clz(0) = 32, clz(1) = 31
 */
static inline __attribute__ ((const)) int clz(unsigned int x)
{
	unsigned int res;

	__asm__ __volatile__(
	"	norm.f  %0, %1		\n"
	"	mov.n   %0, 0		\n"
	"	add.p   %0, %0, 1	\n"
	: "=r"(res)
	: "r"(x)
	: "cc");

	return res;
}

static inline int constant_fls(unsigned int x)
{
	int r = 32;

	if (!x)
		return 0;
	if (!(x & 0xffff0000u)) {
		x <<= 16;
		r -= 16;
	}
	if (!(x & 0xff000000u)) {
		x <<= 8;
		r -= 8;
	}
	if (!(x & 0xf0000000u)) {
		x <<= 4;
		r -= 4;
	}
	if (!(x & 0xc0000000u)) {
		x <<= 2;
		r -= 2;
	}
	if (!(x & 0x80000000u))
		r -= 1;
	return r;
}

/*
 * fls = Find Last Set in word
 * @result: [1-32]
 * fls(1) = 1, fls(0x80000000) = 32, fls(0) = 0
 */
static inline __attribute__ ((const)) int fls(unsigned int x)
{
	if (__builtin_constant_p(x))
	       return constant_fls(x);

	return 32 - clz(x);
}

/*
 * __fls: Similar to fls, but zero based (0-31)
 */
static inline __attribute__ ((const)) int __fls(unsigned long x)
{
	if (!x)
		return 0;
	else
		return fls(x) - 1;
}

/*
 * ffs = Find First Set in word (LSB to MSB)
 * @result: [1-32], 0 if all 0's
 */
#define ffs(x)	({ unsigned long __t = (x); fls(__t & -__t); })

/*
 * __ffs: Similar to ffs, but zero based (0-31)
 */
static inline __attribute__ ((const)) unsigned long __ffs(unsigned long word)
{
	if (!word)
		return word;

	return ffs(word) - 1;
}

#else	/* CONFIG_ISA_ARCV2 */

/*
 * fls = Find Last Set in word
 * @result: [1-32]
 * fls(1) = 1, fls(0x80000000) = 32, fls(0) = 0
 */
static inline __attribute__ ((const)) int fls(unsigned int x)
{
	int n;

	asm volatile(
	"	fls.f	%0, %1		\n"  /* 0:31; 0(Z) if src 0 */
	"	add.nz	%0, %0, 1	\n"  /* 0:31 -> 1:32 */
	: "=r"(n)	/* Early clobber not needed */
	: "r"(x)
	: "cc");

	return n;
}

/*
 * __fls: Similar to fls, but zero based (0-31). Also 0 if no bit set
 */
static inline __attribute__ ((const)) int __fls(unsigned long x)
{
	/* FLS insn has exactly same semantics as the API */
	return	__builtin_arc_fls(x);
}

/*
 * ffs = Find First Set in word (LSB to MSB)
 * @result: [1-32], 0 if all 0's
 */
static inline __attribute__ ((const)) int ffs(unsigned int x)
{
	int n;

	asm volatile(
	"	ffs.f	%0, %1		\n"  /* 0:31; 31(Z) if src 0 */
	"	add.nz	%0, %0, 1	\n"  /* 0:31 -> 1:32 */
	"	mov.z	%0, 0		\n"  /* 31(Z)-> 0 */
	: "=r"(n)	/* Early clobber not needed */
	: "r"(x)
	: "cc");

	return n;
}

/*
 * __ffs: Similar to ffs, but zero based (0-31)
 */
static inline __attribute__ ((const)) unsigned long __ffs(unsigned long x)
{
	unsigned long n;

	asm volatile(
	"	ffs.f	%0, %1		\n"  /* 0:31; 31(Z) if src 0 */
	"	mov.z	%0, 0		\n"  /* 31(Z)-> 0 */
	: "=r"(n)
	: "r"(x)
	: "cc");

	return n;

}

#endif	/* CONFIG_ISA_ARCOMPACT */

/*
 * ffz = Find First Zero in word.
 * @return:[0-31], 32 if all 1's
 */
#define ffz(x)	__ffs(~(x))

#include <asm-generic/bitops/hweight.h>
#include <asm-generic/bitops/fls64.h>
#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/lock.h>
#include <asm-generic/bitops/atomic.h>
#include <asm-generic/bitops/non-atomic.h>

#include <asm-generic/bitops/le.h>
#include <asm-generic/bitops/ext2-atomic-setbit.h>

#endif /* !__ASSEMBLY__ */

#endif
