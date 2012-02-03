/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2009, 2010 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#ifndef _ASM_C6X_CACHEFLUSH_H
#define _ASM_C6X_CACHEFLUSH_H

#include <linux/spinlock.h>

#include <asm/setup.h>
#include <asm/cache.h>
#include <asm/mman.h>
#include <asm/page.h>
#include <asm/string.h>

/*
 * virtually-indexed cache management (our cache is physically indexed)
 */
#define flush_cache_all()			do {} while (0)
#define flush_cache_mm(mm)			do {} while (0)
#define flush_cache_dup_mm(mm)			do {} while (0)
#define flush_cache_range(mm, start, end)	do {} while (0)
#define flush_cache_page(vma, vmaddr, pfn)	do {} while (0)
#define flush_cache_vmap(start, end)		do {} while (0)
#define flush_cache_vunmap(start, end)		do {} while (0)
#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 0
#define flush_dcache_page(page)			do {} while (0)
#define flush_dcache_mmap_lock(mapping)		do {} while (0)
#define flush_dcache_mmap_unlock(mapping)	do {} while (0)

/*
 * physically-indexed cache management
 */
#define flush_icache_range(s, e)				  \
do {								  \
		L1D_cache_block_writeback((s), (e));		  \
		L1P_cache_block_invalidate((s), (e));		  \
} while (0)

#define flush_icache_page(vma, page)					  \
do {								  \
	if ((vma)->vm_flags & PROT_EXEC)				  \
		L1D_cache_block_writeback_invalidate(page_address(page),  \
			(unsigned long) page_address(page) + PAGE_SIZE)); \
		L1P_cache_block_invalidate(page_address(page),		  \
			(unsigned long) page_address(page) + PAGE_SIZE)); \
} while (0)


#define copy_to_user_page(vma, page, vaddr, dst, src, len) \
do {						     \
	memcpy(dst, src, len);			     \
	flush_icache_range((unsigned) (dst), (unsigned) (dst) + (len)); \
} while (0)

#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
	memcpy(dst, src, len)

#endif /* _ASM_C6X_CACHEFLUSH_H */
