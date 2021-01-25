/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/include/asm/cacheflush.h
 *
 * Copyright (C) 1999-2002 Russell King.
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_CACHEFLUSH_H
#define __ASM_CACHEFLUSH_H

#include <linux/kgdb.h>
#include <linux/mm.h>

/*
 * This flag is used to indicate that the page pointed to by a pte is clean
 * and does not require cleaning before returning it to the user.
 */
#define PG_dcache_clean PG_arch_1

/*
 *	MM Cache Management
 *	===================
 *
 *	The arch/arm64/mm/cache.S implements these methods.
 *
 *	Start addresses are inclusive and end addresses are exclusive; start
 *	addresses should be rounded down, end addresses up.
 *
 *	See Documentation/core-api/cachetlb.rst for more information. Please note that
 *	the implementation assumes non-aliasing VIPT D-cache and (aliasing)
 *	VIPT I-cache.
 *
 *	flush_icache_range(start, end)
 *
 *		Ensure coherency between the I-cache and the D-cache in the
 *		region described by start, end.
 *		- start  - virtual start address
 *		- end    - virtual end address
 *
 *	invalidate_icache_range(start, end)
 *
 *		Invalidate the I-cache in the region described by start, end.
 *		- start  - virtual start address
 *		- end    - virtual end address
 *
 *	__flush_cache_user_range(start, end)
 *
 *		Ensure coherency between the I-cache and the D-cache in the
 *		region described by start, end.
 *		- start  - virtual start address
 *		- end    - virtual end address
 *
 *	__flush_dcache_area(kaddr, size)
 *
 *		Ensure that the data held in page is written back.
 *		- kaddr  - page address
 *		- size   - region size
 */
extern void __flush_icache_range(unsigned long start, unsigned long end);
extern int  invalidate_icache_range(unsigned long start, unsigned long end);
extern void __flush_dcache_area(void *addr, size_t len);
extern void __inval_dcache_area(void *addr, size_t len);
extern void __clean_dcache_area_poc(void *addr, size_t len);
extern void __clean_dcache_area_pop(void *addr, size_t len);
extern void __clean_dcache_area_pou(void *addr, size_t len);
extern long __flush_cache_user_range(unsigned long start, unsigned long end);
extern void sync_icache_aliases(void *kaddr, unsigned long len);

static inline void flush_icache_range(unsigned long start, unsigned long end)
{
	__flush_icache_range(start, end);

	/*
	 * IPI all online CPUs so that they undergo a context synchronization
	 * event and are forced to refetch the new instructions.
	 */

	/*
	 * KGDB performs cache maintenance with interrupts disabled, so we
	 * will deadlock trying to IPI the secondary CPUs. In theory, we can
	 * set CACHE_FLUSH_IS_SAFE to 0 to avoid this known issue, but that
	 * just means that KGDB will elide the maintenance altogether! As it
	 * turns out, KGDB uses IPIs to round-up the secondary CPUs during
	 * the patching operation, so we don't need extra IPIs here anyway.
	 * In which case, add a KGDB-specific bodge and return early.
	 */
	if (in_dbg_master())
		return;

	kick_all_cpus_sync();
}
#define flush_icache_range flush_icache_range

/*
 * Cache maintenance functions used by the DMA API. No to be used directly.
 */
extern void __dma_map_area(const void *, size_t, int);
extern void __dma_unmap_area(const void *, size_t, int);
extern void __dma_flush_area(const void *, size_t);

/*
 * Copy user data from/to a page which is mapped into a different
 * processes address space.  Really, we want to allow our "user
 * space" model to handle this.
 */
extern void copy_to_user_page(struct vm_area_struct *, struct page *,
	unsigned long, void *, const void *, unsigned long);
#define copy_to_user_page copy_to_user_page

/*
 * flush_dcache_page is used when the kernel has written to the page
 * cache page at virtual address page->virtual.
 *
 * If this page isn't mapped (ie, page_mapping == NULL), or it might
 * have userspace mappings, then we _must_ always clean + invalidate
 * the dcache entries associated with the kernel mapping.
 *
 * Otherwise we can defer the operation, and clean the cache when we are
 * about to change to user space.  This is the same method as used on SPARC64.
 * See update_mmu_cache for the user space part.
 */
#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 1
extern void flush_dcache_page(struct page *);

static __always_inline void __flush_icache_all(void)
{
	if (cpus_have_const_cap(ARM64_HAS_CACHE_DIC))
		return;

	asm("ic	ialluis");
	dsb(ish);
}

int set_memory_valid(unsigned long addr, int numpages, int enable);

int set_direct_map_invalid_noflush(struct page *page);
int set_direct_map_default_noflush(struct page *page);
bool kernel_page_present(struct page *page);

#include <asm-generic/cacheflush.h>

#endif /* __ASM_CACHEFLUSH_H */
