/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_BITOPS_H
#define __ASM_CSKY_BITOPS_H

#include <linux/compiler.h>
#include <asm/barrier.h>

/*
 * asm-generic/bitops/ffs.h
 */
static inline int ffs(int x)
{
	if (!x)
		return 0;

	asm volatile (
		"brev %0\n"
		"ff1  %0\n"
		"addi %0, 1\n"
		: "=&r"(x)
		: "0"(x));
	return x;
}

/*
 * asm-generic/bitops/__ffs.h
 */
static __always_inline unsigned long __ffs(unsigned long x)
{
	asm volatile (
		"brev %0\n"
		"ff1  %0\n"
		: "=&r"(x)
		: "0"(x));
	return x;
}

/*
 * asm-generic/bitops/fls.h
 */
static __always_inline int fls(unsigned int x)
{
	asm volatile(
		"ff1 %0\n"
		: "=&r"(x)
		: "0"(x));

	return (32 - x);
}

/*
 * asm-generic/bitops/__fls.h
 */
static __always_inline unsigned long __fls(unsigned long x)
{
	return fls(x) - 1;
}

#include <asm-generic/bitops/ffz.h>
#include <asm-generic/bitops/fls64.h>

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/hweight.h>
#include <asm-generic/bitops/lock.h>
#include <asm-generic/bitops/atomic.h>

/*
 * bug fix, why only could use atomic!!!!
 */
#include <asm-generic/bitops/non-atomic.h>

#include <asm-generic/bitops/le.h>
#include <asm-generic/bitops/ext2-atomic.h>
#endif /* __ASM_CSKY_BITOPS_H */
