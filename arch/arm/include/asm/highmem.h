#ifndef _ASM_HIGHMEM_H
#define _ASM_HIGHMEM_H

#include <asm/kmap_types.h>

#define PKMAP_BASE		(PAGE_OFFSET - PMD_SIZE)
#define LAST_PKMAP		PTRS_PER_PTE
#define LAST_PKMAP_MASK		(LAST_PKMAP - 1)
#define PKMAP_NR(virt)		(((virt) - PKMAP_BASE) >> PAGE_SHIFT)
#define PKMAP_ADDR(nr)		(PKMAP_BASE + ((nr) << PAGE_SHIFT))

#define kmap_prot		PAGE_KERNEL

#define flush_cache_kmaps() \
	do { \
		if (cache_is_vivt()) \
			flush_cache_all(); \
	} while (0)

extern pte_t *pkmap_page_table;

extern void *kmap_high(struct page *page);
extern void kunmap_high(struct page *page);

/*
 * The reason for kmap_high_get() is to ensure that the currently kmap'd
 * page usage count does not decrease to zero while we're using its
 * existing virtual mapping in an atomic context.  With a VIVT cache this
 * is essential to do, but with a VIPT cache this is only an optimization
 * so not to pay the price of establishing a second mapping if an existing
 * one can be used.  However, on platforms without hardware TLB maintenance
 * broadcast, we simply cannot use ARCH_NEEDS_KMAP_HIGH_GET at all since
 * the locking involved must also disable IRQs which is incompatible with
 * the IPI mechanism used by global TLB operations.
 */
#define ARCH_NEEDS_KMAP_HIGH_GET
#if defined(CONFIG_SMP) && defined(CONFIG_CPU_TLB_V6)
#undef ARCH_NEEDS_KMAP_HIGH_GET
#if defined(CONFIG_HIGHMEM) && defined(CONFIG_CPU_CACHE_VIVT)
#error "The sum of features in your kernel config cannot be supported together"
#endif
#endif

/*
 * Needed to be able to broadcast the TLB invalidation for kmap.
 */
#ifdef CONFIG_ARM_ERRATA_798181
#undef ARCH_NEEDS_KMAP_HIGH_GET
#endif

#ifdef ARCH_NEEDS_KMAP_HIGH_GET
extern void *kmap_high_get(struct page *page);
#else
static inline void *kmap_high_get(struct page *page)
{
	return NULL;
}
#endif

/*
 * The following functions are already defined by <linux/highmem.h>
 * when CONFIG_HIGHMEM is not set.
 */
#ifdef CONFIG_HIGHMEM
extern void *kmap(struct page *page);
extern void kunmap(struct page *page);
extern void *kmap_atomic(struct page *page);
extern void __kunmap_atomic(void *kvaddr);
extern void *kmap_atomic_pfn(unsigned long pfn);
extern struct page *kmap_atomic_to_page(const void *ptr);
#endif

#endif
