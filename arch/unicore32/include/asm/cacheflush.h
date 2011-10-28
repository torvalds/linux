/*
 * linux/arch/unicore32/include/asm/cacheflush.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __UNICORE_CACHEFLUSH_H__
#define __UNICORE_CACHEFLUSH_H__

#include <linux/mm.h>

#include <asm/shmparam.h>

#define CACHE_COLOUR(vaddr)	((vaddr & (SHMLBA - 1)) >> PAGE_SHIFT)

/*
 * This flag is used to indicate that the page pointed to by a pte is clean
 * and does not require cleaning before returning it to the user.
 */
#define PG_dcache_clean PG_arch_1

/*
 *	MM Cache Management
 *	===================
 *
 *	The arch/unicore32/mm/cache.S files implement these methods.
 *
 *	Start addresses are inclusive and end addresses are exclusive;
 *	start addresses should be rounded down, end addresses up.
 *
 *	See Documentation/cachetlb.txt for more information.
 *	Please note that the implementation of these, and the required
 *	effects are cache-type (VIVT/VIPT/PIPT) specific.
 *
 *	flush_icache_all()
 *
 *		Unconditionally clean and invalidate the entire icache.
 *		Currently only needed for cache-v6.S and cache-v7.S, see
 *		__flush_icache_all for the generic implementation.
 *
 *	flush_kern_all()
 *
 *		Unconditionally clean and invalidate the entire cache.
 *
 *	flush_user_all()
 *
 *		Clean and invalidate all user space cache entries
 *		before a change of page tables.
 *
 *	flush_user_range(start, end, flags)
 *
 *		Clean and invalidate a range of cache entries in the
 *		specified address space before a change of page tables.
 *		- start - user start address (inclusive, page aligned)
 *		- end   - user end address   (exclusive, page aligned)
 *		- flags - vma->vm_flags field
 *
 *	coherent_kern_range(start, end)
 *
 *		Ensure coherency between the Icache and the Dcache in the
 *		region described by start, end.  If you have non-snooping
 *		Harvard caches, you need to implement this function.
 *		- start  - virtual start address
 *		- end    - virtual end address
 *
 *	coherent_user_range(start, end)
 *
 *		Ensure coherency between the Icache and the Dcache in the
 *		region described by start, end.  If you have non-snooping
 *		Harvard caches, you need to implement this function.
 *		- start  - virtual start address
 *		- end    - virtual end address
 *
 *	flush_kern_dcache_area(kaddr, size)
 *
 *		Ensure that the data held in page is written back.
 *		- kaddr  - page address
 *		- size   - region size
 *
 *	DMA Cache Coherency
 *	===================
 *
 *	dma_flush_range(start, end)
 *
 *		Clean and invalidate the specified virtual address range.
 *		- start  - virtual start address
 *		- end    - virtual end address
 */

extern void __cpuc_flush_icache_all(void);
extern void __cpuc_flush_kern_all(void);
extern void __cpuc_flush_user_all(void);
extern void __cpuc_flush_user_range(unsigned long, unsigned long, unsigned int);
extern void __cpuc_coherent_kern_range(unsigned long, unsigned long);
extern void __cpuc_coherent_user_range(unsigned long, unsigned long);
extern void __cpuc_flush_dcache_area(void *, size_t);
extern void __cpuc_flush_kern_dcache_area(void *addr, size_t size);

/*
 * These are private to the dma-mapping API.  Do not use directly.
 * Their sole purpose is to ensure that data held in the cache
 * is visible to DMA, or data written by DMA to system memory is
 * visible to the CPU.
 */
extern void __cpuc_dma_clean_range(unsigned long, unsigned long);
extern void __cpuc_dma_flush_range(unsigned long, unsigned long);

/*
 * Copy user data from/to a page which is mapped into a different
 * processes address space.  Really, we want to allow our "user
 * space" model to handle this.
 */
extern void copy_to_user_page(struct vm_area_struct *, struct page *,
	unsigned long, void *, const void *, unsigned long);
#define copy_from_user_page(vma, page, vaddr, dst, src, len)	\
	do {							\
		memcpy(dst, src, len);				\
	} while (0)

/*
 * Convert calls to our calling convention.
 */
/* Invalidate I-cache */
static inline void __flush_icache_all(void)
{
	asm("movc	p0.c5, %0, #20;\n"
	    "nop; nop; nop; nop; nop; nop; nop; nop\n"
	    :
	    : "r" (0));
}

#define flush_cache_all()		__cpuc_flush_kern_all()

extern void flush_cache_mm(struct mm_struct *mm);
extern void flush_cache_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end);
extern void flush_cache_page(struct vm_area_struct *vma,
		unsigned long user_addr, unsigned long pfn);

#define flush_cache_dup_mm(mm) flush_cache_mm(mm)

/*
 * flush_cache_user_range is used when we want to ensure that the
 * Harvard caches are synchronised for the user space address range.
 * This is used for the UniCore private sys_cacheflush system call.
 */
#define flush_cache_user_range(vma, start, end) \
	__cpuc_coherent_user_range((start) & PAGE_MASK, PAGE_ALIGN(end))

/*
 * Perform necessary cache operations to ensure that data previously
 * stored within this range of addresses can be executed by the CPU.
 */
#define flush_icache_range(s, e)	__cpuc_coherent_kern_range(s, e)

/*
 * Perform necessary cache operations to ensure that the TLB will
 * see data written in the specified area.
 */
#define clean_dcache_area(start, size)	cpu_dcache_clean_area(start, size)

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

#define flush_dcache_mmap_lock(mapping)			\
	spin_lock_irq(&(mapping)->tree_lock)
#define flush_dcache_mmap_unlock(mapping)		\
	spin_unlock_irq(&(mapping)->tree_lock)

#define flush_icache_user_range(vma, page, addr, len)	\
	flush_dcache_page(page)

/*
 * We don't appear to need to do anything here.  In fact, if we did, we'd
 * duplicate cache flushing elsewhere performed by flush_dcache_page().
 */
#define flush_icache_page(vma, page)	do { } while (0)

/*
 * flush_cache_vmap() is used when creating mappings (eg, via vmap,
 * vmalloc, ioremap etc) in kernel space for pages.  On non-VIPT
 * caches, since the direct-mappings of these pages may contain cached
 * data, we need to do a full cache flush to ensure that writebacks
 * don't corrupt data placed into these pages via the new mappings.
 */
static inline void flush_cache_vmap(unsigned long start, unsigned long end)
{
}

static inline void flush_cache_vunmap(unsigned long start, unsigned long end)
{
}

#endif
