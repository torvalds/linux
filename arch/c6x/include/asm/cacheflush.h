/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2009, 2010 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
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

#include <asm-generic/cacheflush.h>

#endif /* _ASM_C6X_CACHEFLUSH_H */
