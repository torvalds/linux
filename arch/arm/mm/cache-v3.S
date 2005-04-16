/*
 *  linux/arch/arm/mm/cache-v3.S
 *
 *  Copyright (C) 1997-2002 Russell king
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/hardware.h>
#include <asm/page.h>
#include "proc-macros.S"

/*
 *	flush_user_cache_all()
 *
 *	Invalidate all cache entries in a particular address
 *	space.
 *
 *	- mm	- mm_struct describing address space
 */
ENTRY(v3_flush_user_cache_all)
	/* FALLTHROUGH */
/*
 *	flush_kern_cache_all()
 *
 *	Clean and invalidate the entire cache.
 */
ENTRY(v3_flush_kern_cache_all)
	/* FALLTHROUGH */

/*
 *	flush_user_cache_range(start, end, flags)
 *
 *	Invalidate a range of cache entries in the specified
 *	address space.
 *
 *	- start - start address (may not be aligned)
 *	- end	- end address (exclusive, may not be aligned)
 *	- flags	- vma_area_struct flags describing address space
 */
ENTRY(v3_flush_user_cache_range)
	mov	ip, #0
	mcreq	p15, 0, ip, c7, c0, 0		@ flush ID cache
	mov	pc, lr

/*
 *	coherent_kern_range(start, end)
 *
 *	Ensure coherency between the Icache and the Dcache in the
 *	region described by start.  If you have non-snooping
 *	Harvard caches, you need to implement this function.
 *
 *	- start  - virtual start address
 *	- end	 - virtual end address
 */
ENTRY(v3_coherent_kern_range)
	/* FALLTHROUGH */

/*
 *	coherent_user_range(start, end)
 *
 *	Ensure coherency between the Icache and the Dcache in the
 *	region described by start.  If you have non-snooping
 *	Harvard caches, you need to implement this function.
 *
 *	- start  - virtual start address
 *	- end	 - virtual end address
 */
ENTRY(v3_coherent_user_range)
	mov	pc, lr

/*
 *	flush_kern_dcache_page(void *page)
 *
 *	Ensure no D cache aliasing occurs, either with itself or
 *	the I cache
 *
 *	- addr	- page aligned address
 */
ENTRY(v3_flush_kern_dcache_page)
	/* FALLTHROUGH */

/*
 *	dma_inv_range(start, end)
 *
 *	Invalidate (discard) the specified virtual address range.
 *	May not write back any entries.  If 'start' or 'end'
 *	are not cache line aligned, those lines must be written
 *	back.
 *
 *	- start  - virtual start address
 *	- end	 - virtual end address
 */
ENTRY(v3_dma_inv_range)
	/* FALLTHROUGH */

/*
 *	dma_flush_range(start, end)
 *
 *	Clean and invalidate the specified virtual address range.
 *
 *	- start  - virtual start address
 *	- end	 - virtual end address
 */
ENTRY(v3_dma_flush_range)
	mov	r0, #0
	mcr	p15, 0, r0, c7, c0, 0		@ flush ID cache
	/* FALLTHROUGH */

/*
 *	dma_clean_range(start, end)
 *
 *	Clean (write back) the specified virtual address range.
 *
 *	- start  - virtual start address
 *	- end	 - virtual end address
 */
ENTRY(v3_dma_clean_range)
	mov	pc, lr

	__INITDATA

	.type	v3_cache_fns, #object
ENTRY(v3_cache_fns)
	.long	v3_flush_kern_cache_all
	.long	v3_flush_user_cache_all
	.long	v3_flush_user_cache_range
	.long	v3_coherent_kern_range
	.long	v3_coherent_user_range
	.long	v3_flush_kern_dcache_page
	.long	v3_dma_inv_range
	.long	v3_dma_clean_range
	.long	v3_dma_flush_range
	.size	v3_cache_fns, . - v3_cache_fns
