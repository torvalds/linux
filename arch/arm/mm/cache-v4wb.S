/*
 *  linux/arch/arm/mm/cache-v4wb.S
 *
 *  Copyright (C) 1997-2002 Russell king
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/memory.h>
#include <asm/page.h>
#include "proc-macros.S"

/*
 * The size of one data cache line.
 */
#define CACHE_DLINESIZE	32

/*
 * The total size of the data cache.
 */
#if defined(CONFIG_CPU_SA110)
# define CACHE_DSIZE	16384
#elif defined(CONFIG_CPU_SA1100)
# define CACHE_DSIZE	8192
#else
# error Unknown cache size
#endif

/*
 * This is the size at which it becomes more efficient to
 * clean the whole cache, rather than using the individual
 * cache line maintenance instructions.
 *
 *  Size  Clean (ticks) Dirty (ticks)
 *   4096   21  20  21    53  55  54
 *   8192   40  41  40   106 100 102
 *  16384   77  77  76   140 140 138
 *  32768  150 149 150   214 216 212 <---
 *  65536  296 297 296   351 358 361
 * 131072  591 591 591   656 657 651
 *  Whole  132 136 132   221 217 207 <---
 */
#define CACHE_DLIMIT	(CACHE_DSIZE * 4)

	.data
flush_base:
	.long	FLUSH_BASE
	.text

/*
 *	flush_icache_all()
 *
 *	Unconditionally clean and invalidate the entire icache.
 */
ENTRY(v4wb_flush_icache_all)
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0		@ invalidate I cache
	mov	pc, lr
ENDPROC(v4wb_flush_icache_all)

/*
 *	flush_user_cache_all()
 *
 *	Clean and invalidate all cache entries in a particular address
 *	space.
 */
ENTRY(v4wb_flush_user_cache_all)
	/* FALLTHROUGH */
/*
 *	flush_kern_cache_all()
 *
 *	Clean and invalidate the entire cache.
 */
ENTRY(v4wb_flush_kern_cache_all)
	mov	ip, #0
	mcr	p15, 0, ip, c7, c5, 0		@ invalidate I cache
__flush_whole_cache:
	ldr	r3, =flush_base
	ldr	r1, [r3, #0]
	eor	r1, r1, #CACHE_DSIZE
	str	r1, [r3, #0]
	add	r2, r1, #CACHE_DSIZE
1:	ldr	r3, [r1], #32
	cmp	r1, r2
	blo	1b
#ifdef FLUSH_BASE_MINICACHE
	add	r2, r2, #FLUSH_BASE_MINICACHE - FLUSH_BASE
	sub	r1, r2, #512			@ only 512 bytes
1:	ldr	r3, [r1], #32
	cmp	r1, r2
	blo	1b
#endif
	mcr	p15, 0, ip, c7, c10, 4		@ drain write buffer
	mov	pc, lr

/*
 *	flush_user_cache_range(start, end, flags)
 *
 *	Invalidate a range of cache entries in the specified
 *	address space.
 *
 *	- start - start address (inclusive, page aligned)
 *	- end	- end address (exclusive, page aligned)
 *	- flags	- vma_area_struct flags describing address space
 */
ENTRY(v4wb_flush_user_cache_range)
	mov	ip, #0
	sub	r3, r1, r0			@ calculate total size
	tst	r2, #VM_EXEC			@ executable region?
	mcrne	p15, 0, ip, c7, c5, 0		@ invalidate I cache

	cmp	r3, #CACHE_DLIMIT		@ total size >= limit?
	bhs	__flush_whole_cache		@ flush whole D cache

1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	mcr	p15, 0, r0, c7, c6, 1		@ invalidate D entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	tst	r2, #VM_EXEC
	mcrne	p15, 0, ip, c7, c10, 4		@ drain write buffer
	mov	pc, lr

/*
 *	flush_kern_dcache_area(void *addr, size_t size)
 *
 *	Ensure no D cache aliasing occurs, either with itself or
 *	the I cache
 *
 *	- addr	- kernel address
 *	- size	- region size
 */
ENTRY(v4wb_flush_kern_dcache_area)
	add	r1, r0, r1
	/* fall through */

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
ENTRY(v4wb_coherent_kern_range)
	/* fall through */

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
ENTRY(v4wb_coherent_user_range)
	bic	r0, r0, #CACHE_DLINESIZE - 1
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	mcr	p15, 0, r0, c7, c6, 1		@ invalidate D entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	mov	ip, #0
	mcr	p15, 0, ip, c7, c5, 0		@ invalidate I cache
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	mov	pc, lr


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
v4wb_dma_inv_range:
	tst	r0, #CACHE_DLINESIZE - 1
	bic	r0, r0, #CACHE_DLINESIZE - 1
	mcrne	p15, 0, r0, c7, c10, 1		@ clean D entry
	tst	r1, #CACHE_DLINESIZE - 1
	mcrne	p15, 0, r1, c7, c10, 1		@ clean D entry
1:	mcr	p15, 0, r0, c7, c6, 1		@ invalidate D entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer
	mov	pc, lr

/*
 *	dma_clean_range(start, end)
 *
 *	Clean (write back) the specified virtual address range.
 *
 *	- start  - virtual start address
 *	- end	 - virtual end address
 */
v4wb_dma_clean_range:
	bic	r0, r0, #CACHE_DLINESIZE - 1
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer
	mov	pc, lr

/*
 *	dma_flush_range(start, end)
 *
 *	Clean and invalidate the specified virtual address range.
 *
 *	- start  - virtual start address
 *	- end	 - virtual end address
 *
 *	This is actually the same as v4wb_coherent_kern_range()
 */
	.globl	v4wb_dma_flush_range
	.set	v4wb_dma_flush_range, v4wb_coherent_kern_range

/*
 *	dma_map_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(v4wb_dma_map_area)
	add	r1, r1, r0
	cmp	r2, #DMA_TO_DEVICE
	beq	v4wb_dma_clean_range
	bcs	v4wb_dma_inv_range
	b	v4wb_dma_flush_range
ENDPROC(v4wb_dma_map_area)

/*
 *	dma_unmap_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(v4wb_dma_unmap_area)
	mov	pc, lr
ENDPROC(v4wb_dma_unmap_area)

	__INITDATA

	.type	v4wb_cache_fns, #object
ENTRY(v4wb_cache_fns)
	.long	v4wb_flush_icache_all
	.long	v4wb_flush_kern_cache_all
	.long	v4wb_flush_user_cache_all
	.long	v4wb_flush_user_cache_range
	.long	v4wb_coherent_kern_range
	.long	v4wb_coherent_user_range
	.long	v4wb_flush_kern_dcache_area
	.long	v4wb_dma_map_area
	.long	v4wb_dma_unmap_area
	.long	v4wb_dma_flush_range
	.size	v4wb_cache_fns, . - v4wb_cache_fns
