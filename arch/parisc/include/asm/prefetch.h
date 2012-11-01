/*
 * include/asm-parisc/prefetch.h
 *
 * PA 2.0 defines data prefetch instructions on page 6-11 of the Kane book.
 * In addition, many implementations do hardware prefetching of both
 * instructions and data.
 *
 * PA7300LC (page 14-4 of the ERS) also implements prefetching by a load
 * to gr0 but not in a way that Linux can use.  If the load would cause an
 * interruption (eg due to prefetching 0), it is suppressed on PA2.0
 * processors, but not on 7300LC.
 *
 */

#ifndef __ASM_PARISC_PREFETCH_H
#define __ASM_PARISC_PREFETCH_H

#ifndef __ASSEMBLY__
#ifdef CONFIG_PREFETCH

#define ARCH_HAS_PREFETCH
static inline void prefetch(const void *addr)
{
	__asm__(
#ifndef CONFIG_PA20
		/* Need to avoid prefetch of NULL on PA7300LC */
		"	extrw,u,= %0,31,32,%%r0\n"
#endif
		"	ldw 0(%0), %%r0" : : "r" (addr));
}

/* LDD is a PA2.0 addition. */
#ifdef CONFIG_PA20
#define ARCH_HAS_PREFETCHW
static inline void prefetchw(const void *addr)
{
	__asm__("ldd 0(%0), %%r0" : : "r" (addr));
}
#endif /* CONFIG_PA20 */

#endif /* CONFIG_PREFETCH */
#endif /* __ASSEMBLY__ */

#endif /* __ASM_PARISC_PROCESSOR_H */
