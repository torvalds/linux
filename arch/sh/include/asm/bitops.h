#ifndef __ASM_SH_BITOPS_H
#define __ASM_SH_BITOPS_H

#ifdef __KERNEL__

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
#else
#include <asm-generic/bitops/atomic.h>
#include <asm-generic/bitops/non-atomic.h>
#endif

#ifdef CONFIG_SUPERH32
static inline unsigned long ffz(unsigned long word)
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
static inline unsigned long __ffs(unsigned long word)
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
#else
static inline unsigned long ffz(unsigned long word)
{
	unsigned long result, __d2, __d3;

        __asm__("gettr  tr0, %2\n\t"
                "pta    $+32, tr0\n\t"
                "andi   %1, 1, %3\n\t"
                "beq    %3, r63, tr0\n\t"
                "pta    $+4, tr0\n"
                "0:\n\t"
                "shlri.l        %1, 1, %1\n\t"
                "addi   %0, 1, %0\n\t"
                "andi   %1, 1, %3\n\t"
                "beqi   %3, 1, tr0\n"
                "1:\n\t"
                "ptabs  %2, tr0\n\t"
                : "=r" (result), "=r" (word), "=r" (__d2), "=r" (__d3)
                : "0" (0L), "1" (word));

	return result;
}

#include <asm-generic/bitops/__ffs.h>
#endif

#include <asm-generic/bitops/find.h>
#include <asm-generic/bitops/ffs.h>
#include <asm-generic/bitops/hweight.h>
#include <asm-generic/bitops/lock.h>
#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/le.h>
#include <asm-generic/bitops/ext2-atomic.h>
#include <asm-generic/bitops/fls.h>
#include <asm-generic/bitops/__fls.h>
#include <asm-generic/bitops/fls64.h>

#endif /* __KERNEL__ */

#endif /* __ASM_SH_BITOPS_H */
