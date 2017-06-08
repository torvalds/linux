/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  vineetg: May 2011: for Non-aliasing VIPT D-cache following can be NOPs
 *   -flush_cache_dup_mm (fork)
 *   -likewise for flush_cache_mm (exit/execve)
 *   -likewise for flush_cache_{range,page} (munmap, exit, COW-break)
 *
 *  vineetg: April 2008
 *   -Added a critical CacheLine flush to copy_to_user_page( ) which
 *     was causing gdbserver to not setup breakpoints consistently
 */

#ifndef _ASM_CACHEFLUSH_H
#define _ASM_CACHEFLUSH_H

#include <linux/mm.h>
#include <asm/shmparam.h>

/*
 * Semantically we need this because icache doesn't snoop dcache/dma.
 * However ARC Cache flush requires paddr as well as vaddr, latter not available
 * in the flush_icache_page() API. So we no-op it but do the equivalent work
 * in update_mmu_cache()
 */
#define flush_icache_page(vma, page)

void flush_cache_all(void);

void flush_icache_range(unsigned long kstart, unsigned long kend);
void __sync_icache_dcache(phys_addr_t paddr, unsigned long vaddr, int len);
void __inv_icache_page(phys_addr_t paddr, unsigned long vaddr);
void __flush_dcache_page(phys_addr_t paddr, unsigned long vaddr);

#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 1

void flush_dcache_page(struct page *page);

void dma_cache_wback_inv(phys_addr_t start, unsigned long sz);
void dma_cache_inv(phys_addr_t start, unsigned long sz);
void dma_cache_wback(phys_addr_t start, unsigned long sz);

#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)

/* TBD: optimize this */
#define flush_cache_vmap(start, end)		flush_cache_all()
#define flush_cache_vunmap(start, end)		flush_cache_all()

#define flush_cache_dup_mm(mm)			/* called on fork (VIVT only) */

#ifndef CONFIG_ARC_CACHE_VIPT_ALIASING

#define flush_cache_mm(mm)			/* called on munmap/exit */
#define flush_cache_range(mm, u_vstart, u_vend)
#define flush_cache_page(vma, u_vaddr, pfn)	/* PF handling/COW-break */

#else	/* VIPT aliasing dcache */

/* To clear out stale userspace mappings */
void flush_cache_mm(struct mm_struct *mm);
void flush_cache_range(struct vm_area_struct *vma,
	unsigned long start,unsigned long end);
void flush_cache_page(struct vm_area_struct *vma,
	unsigned long user_addr, unsigned long page);

/*
 * To make sure that userspace mapping is flushed to memory before
 * get_user_pages() uses a kernel mapping to access the page
 */
#define ARCH_HAS_FLUSH_ANON_PAGE
void flush_anon_page(struct vm_area_struct *vma,
	struct page *page, unsigned long u_vaddr);

#endif	/* CONFIG_ARC_CACHE_VIPT_ALIASING */

/*
 * A new pagecache page has PG_arch_1 clear - thus dcache dirty by default
 * This works around some PIO based drivers which don't call flush_dcache_page
 * to record that they dirtied the dcache
 */
#define PG_dc_clean	PG_arch_1

#define CACHE_COLORS_NUM	4
#define CACHE_COLORS_MSK	(CACHE_COLORS_NUM - 1)
#define CACHE_COLOR(addr)	(((unsigned long)(addr) >> (PAGE_SHIFT)) & CACHE_COLORS_MSK)

/*
 * Simple wrapper over config option
 * Bootup code ensures that hardware matches kernel configuration
 */
static inline int cache_is_vipt_aliasing(void)
{
	return IS_ENABLED(CONFIG_ARC_CACHE_VIPT_ALIASING);
}

/*
 * checks if two addresses (after page aligning) index into same cache set
 */
#define addr_not_cache_congruent(addr1, addr2)				\
({									\
	cache_is_vipt_aliasing() ? 					\
		(CACHE_COLOR(addr1) != CACHE_COLOR(addr2)) : 0;		\
})

#define copy_to_user_page(vma, page, vaddr, dst, src, len)		\
do {									\
	memcpy(dst, src, len);						\
	if (vma->vm_flags & VM_EXEC)					\
		__sync_icache_dcache((unsigned long)(dst), vaddr, len);	\
} while (0)

#define copy_from_user_page(vma, page, vaddr, dst, src, len)		\
	memcpy(dst, src, len);						\

#endif
