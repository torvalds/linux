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

/*
 * Semantically we need this because icache doesn't snoop dcache/dma.
 * However ARC Cache flush requires paddr as well as vaddr, latter not available
 * in the flush_icache_page() API. So we no-op it but do the equivalent work
 * in update_mmu_cache()
 */
#define flush_icache_page(vma, page)

void flush_cache_all(void);

void flush_icache_range(unsigned long start, unsigned long end);
void flush_icache_range_vaddr(unsigned long paddr, unsigned long u_vaddr,
				     int len);
void __inv_icache_page(unsigned long paddr, unsigned long vaddr);

#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 1

void flush_dcache_page(struct page *page);

void dma_cache_wback_inv(unsigned long start, unsigned long sz);
void dma_cache_inv(unsigned long start, unsigned long sz);
void dma_cache_wback(unsigned long start, unsigned long sz);

#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)

/* TBD: optimize this */
#define flush_cache_vmap(start, end)		flush_cache_all()
#define flush_cache_vunmap(start, end)		flush_cache_all()

/*
 * VM callbacks when entire/range of user-space V-P mappings are
 * torn-down/get-invalidated
 *
 * Currently we don't support D$ aliasing configs for our VIPT caches
 * NOPS for VIPT Cache with non-aliasing D$ configurations only
 */
#define flush_cache_dup_mm(mm)			/* called on fork */
#define flush_cache_mm(mm)			/* called on munmap/exit */
#define flush_cache_range(mm, u_vstart, u_vend)
#define flush_cache_page(vma, u_vaddr, pfn)	/* PF handling/COW-break */

#define copy_to_user_page(vma, page, vaddr, dst, src, len)		\
do {									\
	memcpy(dst, src, len);						\
	if (vma->vm_flags & VM_EXEC)					\
		flush_icache_range_vaddr((unsigned long)(dst), vaddr, len);\
} while (0)

#define copy_from_user_page(vma, page, vaddr, dst, src, len)		\
	memcpy(dst, src, len);						\

#endif
