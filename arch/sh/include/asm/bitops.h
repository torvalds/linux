/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_BITOPS_H
#define __ASM_SH_BITOPS_H

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

/* For __swab32 */
#include <asm/byteorder.h>
#include <asm/barrier.h>

#ifdef CONFIG_GUSA_RB
#include <asm/bitops-grb.h>
#elif defined(CONFIG_CPU_SH2A)
#include <asm-generic/bitops/atomic.h>
#include <asm/bitops-op32.h>
#elif defined(CONFIG_CPU_SH4A)
#include <asm/bitops-llsc.h>
#elif defined(CONFIG_CPU_J2) && defined(CONFIG_SMP)
#include <asm/bitops-cas.h>
#else
#include <asm-generic/bitops/atomic.h>
#include <asm-generic/bitops/non-atomic.h>
#endif

static inline unsigned long __attribute_const__ ffz(unsigned long word)
{
	unsigned long result;

	__asm__("1:\n\t"
		"shlr	%1\n\t"
		"bt/s	1b\n\t"
		" add	#1, %0"
		: "=r" (result), "=r" (word)
		: "0" (~0L), "1" (word)
		: "t");
	return result;
}

/**
 * __ffs - find first bit in word.
 * @word: The word to search
 *
 * Undefined if no bit exists, so code should check against 0 first.
 */
static inline __attribute_const__ unsigned long __ffs(unsigned long word)
{
	unsigned long result;

	__asm__("1:\n\t"
		"shlr	%1\n\t"
		"bf/s	1b\n\t"
		" add	#1, %0"
		: "=r" (result), "=r" (word)
		: "0" (~0L), "1" (word)
		: "t");
	return result;
}

#include <asm-generic/bitops/ffs.h>
#include <asm-generic/bitops/hweight.h>
#include <asm-generic/bitops/lock.h>
#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/ext2-atomic.h>
#include <asm-generic/bitops/fls.h>
#include <asm-generic/bitops/__fls.h>
#include <asm-generic/bitops/fls64.h>

#include <asm-generic/bitops/le.h>

#endif /* __ASM_SH_BITOPS_H */
