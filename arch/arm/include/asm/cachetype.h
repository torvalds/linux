/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_ARM_CACHETYPE_H
#define __ASM_ARM_CACHETYPE_H

#define CACHEID_VIVT			(1 << 0)
#define CACHEID_VIPT_NONALIASING	(1 << 1)
#define CACHEID_VIPT_ALIASING		(1 << 2)
#define CACHEID_VIPT			(CACHEID_VIPT_ALIASING|CACHEID_VIPT_NONALIASING)
#define CACHEID_ASID_TAGGED		(1 << 3)
#define CACHEID_VIPT_I_ALIASING		(1 << 4)
#define CACHEID_PIPT			(1 << 5)

extern unsigned int cacheid;

#define cache_is_vivt()			cacheid_is(CACHEID_VIVT)
#define cache_is_vipt()			cacheid_is(CACHEID_VIPT)
#define cache_is_vipt_nonaliasing()	cacheid_is(CACHEID_VIPT_NONALIASING)
#define cache_is_vipt_aliasing()	cacheid_is(CACHEID_VIPT_ALIASING)
#define icache_is_vivt_asid_tagged()	cacheid_is(CACHEID_ASID_TAGGED)
#define icache_is_vipt_aliasing()	cacheid_is(CACHEID_VIPT_I_ALIASING)
#define icache_is_pipt()		cacheid_is(CACHEID_PIPT)

#define cpu_dcache_is_aliasing()	(cache_is_vivt() || cache_is_vipt_aliasing())

/*
 * __LINUX_ARM_ARCH__ is the minimum supported CPU architecture
 * Mask out support which will never be present on newer CPUs.
 * - v6+ is never VIVT
 * - v7+ VIPT never aliases on D-side
 */
#if __LINUX_ARM_ARCH__ >= 7
#define __CACHEID_ARCH_MIN	(CACHEID_VIPT_NONALIASING |\
				 CACHEID_ASID_TAGGED |\
				 CACHEID_VIPT_I_ALIASING |\
				 CACHEID_PIPT)
#elif __LINUX_ARM_ARCH__ >= 6
#define	__CACHEID_ARCH_MIN	(~CACHEID_VIVT)
#else
#define __CACHEID_ARCH_MIN	(~0)
#endif

/*
 * Mask out support which isn't configured
 */
#if defined(CONFIG_CPU_CACHE_VIVT) && !defined(CONFIG_CPU_CACHE_VIPT)
#define __CACHEID_ALWAYS	(CACHEID_VIVT)
#define __CACHEID_NEVER		(~CACHEID_VIVT)
#elif !defined(CONFIG_CPU_CACHE_VIVT) && defined(CONFIG_CPU_CACHE_VIPT)
#define __CACHEID_ALWAYS	(0)
#define __CACHEID_NEVER		(CACHEID_VIVT)
#else
#define __CACHEID_ALWAYS	(0)
#define __CACHEID_NEVER		(0)
#endif

static inline unsigned int __attribute__((pure)) cacheid_is(unsigned int mask)
{
	return (__CACHEID_ALWAYS & mask) |
	       (~__CACHEID_NEVER & __CACHEID_ARCH_MIN & mask & cacheid);
}

#define CSSELR_ICACHE	1
#define CSSELR_DCACHE	0

#define CSSELR_L1	(0 << 1)
#define CSSELR_L2	(1 << 1)
#define CSSELR_L3	(2 << 1)
#define CSSELR_L4	(3 << 1)
#define CSSELR_L5	(4 << 1)
#define CSSELR_L6	(5 << 1)
#define CSSELR_L7	(6 << 1)

#ifndef CONFIG_CPU_V7M
static inline void set_csselr(unsigned int cache_selector)
{
	asm volatile("mcr p15, 2, %0, c0, c0, 0" : : "r" (cache_selector));
}

static inline unsigned int read_ccsidr(void)
{
	unsigned int val;

	asm volatile("mrc p15, 1, %0, c0, c0, 0" : "=r" (val));
	return val;
}
#else /* CONFIG_CPU_V7M */
#include <linux/io.h>
#include "asm/v7m.h"

static inline void set_csselr(unsigned int cache_selector)
{
	writel(cache_selector, BASEADDR_V7M_SCB + V7M_SCB_CTR);
}

static inline unsigned int read_ccsidr(void)
{
	return readl(BASEADDR_V7M_SCB + V7M_SCB_CCSIDR);
}
#endif

#endif
