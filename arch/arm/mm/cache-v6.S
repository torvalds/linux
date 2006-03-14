/*
 *  linux/arch/arm/mm/cache-v6.S
 *
 *  Copyright (C) 2001 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This is the "shell" of the ARMv6 processor support.
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>

#include "proc-macros.S"

#define HARVARD_CACHE
#define CACHE_LINE_SIZE		32
#define D_CACHE_LINE_SIZE	32
#define BTB_FLUSH_SIZE		8

/*
 *	v6_flush_cache_all()
 *
 *	Flush the entire cache.
 *
 *	It is assumed that:
 */
ENTRY(v6_flush_kern_cache_all)
	mov	r0, #0
#ifdef HARVARD_CACHE
	mcr	p15, 0, r0, c7, c14, 0		@ D cache clean+invalidate
	mcr	p15, 0, r0, c7, c5, 0		@ I+BTB cache invalidate
#else
	mcr	p15, 0, r0, c7, c15, 0		@ Cache clean+invalidate
#endif
	mov	pc, lr

/*
 *	v6_flush_cache_all()
 *
 *	Flush all TLB entries in a particular address space
 *
 *	- mm    - mm_struct describing address space
 */
ENTRY(v6_flush_user_cache_all)
	/*FALLTHROUGH*/

/*
 *	v6_flush_cache_range(start, end, flags)
 *
 *	Flush a range of TLB entries in the specified address space.
 *
 *	- start - start address (may not be aligned)
 *	- end   - end address (exclusive, may not be aligned)
 *	- flags	- vm_area_struct flags describing address space
 *
 *	It is assumed that:
 *	- we have a VIPT cache.
 */
ENTRY(v6_flush_user_cache_range)
	mov	pc, lr

/*
 *	v6_coherent_kern_range(start,end)
 *
 *	Ensure that the I and D caches are coherent within specified
 *	region.  This is typically used when code has been written to
 *	a memory region, and will be executed.
 *
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 *
 *	It is assumed that:
 *	- the Icache does not read data from the write buffer
 */
ENTRY(v6_coherent_kern_range)
	/* FALLTHROUGH */

/*
 *	v6_coherent_user_range(start,end)
 *
 *	Ensure that the I and D caches are coherent within specified
 *	region.  This is typically used when code has been written to
 *	a memory region, and will be executed.
 *
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 *
 *	It is assumed that:
 *	- the Icache does not read data from the write buffer
 */
ENTRY(v6_coherent_user_range)

#ifdef HARVARD_CACHE
	bic	r0, r0, #CACHE_LINE_SIZE - 1
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D line
	add	r0, r0, #CACHE_LINE_SIZE
	cmp	r0, r1
	blo	1b
#endif
	mov	r0, #0
#ifdef HARVARD_CACHE
	mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer
	mcr	p15, 0, r0, c7, c5, 0		@ I+BTB cache invalidate
#else
	mcr	p15, 0, r0, c7, c5, 6		@ invalidate BTB
#endif
	mov	pc, lr

/*
 *	v6_flush_kern_dcache_page(kaddr)
 *
 *	Ensure that the data held in the page kaddr is written back
 *	to the page in question.
 *
 *	- kaddr   - kernel address (guaranteed to be page aligned)
 */
ENTRY(v6_flush_kern_dcache_page)
	add	r1, r0, #PAGE_SZ
1:
#ifdef HARVARD_CACHE
	mcr	p15, 0, r0, c7, c14, 1		@ clean & invalidate D line
#else
	mcr	p15, 0, r0, c7, c15, 1		@ clean & invalidate unified line
#endif	
	add	r0, r0, #D_CACHE_LINE_SIZE
	cmp	r0, r1
	blo	1b
#ifdef HARVARD_CACHE
	mov	r0, #0
	mcr	p15, 0, r0, c7, c10, 4
#endif
	mov	pc, lr


/*
 *	v6_dma_inv_range(start,end)
 *
 *	Invalidate the data cache within the specified region; we will
 *	be performing a DMA operation in this region and we want to
 *	purge old data in the cache.
 *
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
ENTRY(v6_dma_inv_range)
	tst	r0, #D_CACHE_LINE_SIZE - 1
	bic	r0, r0, #D_CACHE_LINE_SIZE - 1
#ifdef HARVARD_CACHE
	mcrne	p15, 0, r0, c7, c10, 1		@ clean D line
#else
	mcrne	p15, 0, r0, c7, c11, 1		@ clean unified line
#endif
	tst	r1, #D_CACHE_LINE_SIZE - 1
	bic	r1, r1, #D_CACHE_LINE_SIZE - 1
#ifdef HARVARD_CACHE
	mcrne	p15, 0, r1, c7, c14, 1		@ clean & invalidate D line
#else
	mcrne	p15, 0, r1, c7, c15, 1		@ clean & invalidate unified line
#endif
1:
#ifdef HARVARD_CACHE
	mcr	p15, 0, r0, c7, c6, 1		@ invalidate D line
#else
	mcr	p15, 0, r0, c7, c7, 1		@ invalidate unified line
#endif
	add	r0, r0, #D_CACHE_LINE_SIZE
	cmp	r0, r1
	blo	1b
	mov	r0, #0
	mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer
	mov	pc, lr

/*
 *	v6_dma_clean_range(start,end)
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
ENTRY(v6_dma_clean_range)
	bic	r0, r0, #D_CACHE_LINE_SIZE - 1
1:
#ifdef HARVARD_CACHE
	mcr	p15, 0, r0, c7, c10, 1		@ clean D line
#else
	mcr	p15, 0, r0, c7, c11, 1		@ clean unified line
#endif
	add	r0, r0, #D_CACHE_LINE_SIZE
	cmp	r0, r1
	blo	1b
	mov	r0, #0
	mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer
	mov	pc, lr

/*
 *	v6_dma_flush_range(start,end)
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
ENTRY(v6_dma_flush_range)
	bic	r0, r0, #D_CACHE_LINE_SIZE - 1
1:
#ifdef HARVARD_CACHE
	mcr	p15, 0, r0, c7, c14, 1		@ clean & invalidate D line
#else
	mcr	p15, 0, r0, c7, c15, 1		@ clean & invalidate line
#endif
	add	r0, r0, #D_CACHE_LINE_SIZE
	cmp	r0, r1
	blo	1b
	mov	r0, #0
	mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer
	mov	pc, lr

	__INITDATA

	.type	v6_cache_fns, #object
ENTRY(v6_cache_fns)
	.long	v6_flush_kern_cache_all
	.long	v6_flush_user_cache_all
	.long	v6_flush_user_cache_range
	.long	v6_coherent_kern_range
	.long	v6_coherent_user_range
	.long	v6_flush_kern_dcache_page
	.long	v6_dma_inv_range
	.long	v6_dma_clean_range
	.long	v6_dma_flush_range
	.size	v6_cache_fns, . - v6_cache_fns
