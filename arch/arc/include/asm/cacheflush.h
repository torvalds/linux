/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
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

void flush_cache_all(void);

void flush_icache_range(unsigned long kstart, unsigned long kend);
void __sync_icache_dcache(phys_addr_t paddr, unsigned long vaddr, int len);
void __inv_icache_pages(phys_addr_t paddr, unsigned long vaddr, unsigned nr);
void __flush_dcache_pages(phys_addr_t paddr, unsigned long vaddr, unsigned nr);

#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 1

void flush_dcache_page(struct page *page);
void flush_dcache_folio(struct folio *folio);
#define flush_dcache_folio flush_dcache_folio

void dma_cache_wback_inv(phys_addr_t start, unsigned long sz);
void dma_cache_inv(phys_addr_t start, unsigned long sz);
void dma_cache_wback(phys_addr_t start, unsigned long sz);

#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)

/* TBD: optimize this */
#define flush_cache_vmap(start, end)		flush_cache_all()
#define flush_cache_vmap_early(start, end)	do { } while (0)
#define flush_cache_vunmap(start, end)		flush_cache_all()

#define flush_cache_dup_mm(mm)			/* called on fork (VIVT only) */

#define flush_cache_mm(mm)			/* called on munmap/exit */
#define flush_cache_range(mm, u_vstart, u_vend)
#define flush_cache_page(vma, u_vaddr, pfn)	/* PF handling/COW-break */

/*
 * A new pagecache page has PG_arch_1 clear - thus dcache dirty by default
 * This works around some PIO based drivers which don't call flush_dcache_page
 * to record that they dirtied the dcache
 */
#define PG_dc_clean	PG_arch_1

#define copy_to_user_page(vma, page, vaddr, dst, src, len)		\
do {									\
	memcpy(dst, src, len);						\
	if (vma->vm_flags & VM_EXEC)					\
		__sync_icache_dcache((unsigned long)(dst), vaddr, len);	\
} while (0)

#define copy_from_user_page(vma, page, vaddr, dst, src, len)		\
	memcpy(dst, src, len);						\

#endif
