#ifndef __ASM_ARM_CACHETYPE_H
#define __ASM_ARM_CACHETYPE_H

#define CACHEID_VIVT			(1 << 0)
#define CACHEID_VIPT_NONALIASING	(1 << 1)
#define CACHEID_VIPT_ALIASING		(1 << 2)
#define CACHEID_VIPT			(CACHEID_VIPT_ALIASING|CACHEID_VIPT_NONALIASING)
#define CACHEID_ASID_TAGGED		(1 << 3)

extern unsigned int cacheid;

#define cache_is_vivt()			cacheid_is(CACHEID_VIVT)
#define cache_is_vipt()			cacheid_is(CACHEID_VIPT)
#define cache_is_vipt_nonaliasing()	cacheid_is(CACHEID_VIPT_NONALIASING)
#define cache_is_vipt_aliasing()	cacheid_is(CACHEID_VIPT_ALIASING)
#define icache_is_vivt_asid_tagged()	cacheid_is(CACHEID_ASID_TAGGED)

/*
 * __LINUX_ARM_ARCH__ is the minimum supported CPU architecture
 * Mask out support which will never be present on newer CPUs.
 * - v6+ is never VIVT
 * - v7+ VIPT never aliases
 */
#if __LINUX_ARM_ARCH__ >= 7
#define __CACHEID_ARCH_MIN	(CACHEID_VIPT_NONALIASING | CACHEID_ASID_TAGGED)
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

#endif
