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
#include <asm/unwind.h>

#include "proc-macros.S"

#define HARVARD_CACHE
#define CACHE_LINE_SIZE		32
#define D_CACHE_LINE_SIZE	32
#define BTB_FLUSH_SIZE		8

/*
 *	v6_flush_icache_all()
 *
 *	Flush the whole I-cache.
 *
 *	ARM1136 erratum 411920 - Invalidate Instruction Cache operation can fail.
 *	This erratum is present in 1136, 1156 and 1176. It does not affect the
 *	MPCore.
 *
 *	Registers:
 *	r0 - set to 0
 *	r1 - corrupted
 */
ENTRY(v6_flush_icache_all)
	mov	r0, #0
#ifdef CONFIG_ARM_ERRATA_411920
	mrs	r1, cpsr
	cpsid	ifa				@ disable interrupts
	mcr	p15, 0, r0, c7, c5, 0		@ invalidate entire I-cache
	mcr	p15, 0, r0, c7, c5, 0		@ invalidate entire I-cache
	mcr	p15, 0, r0, c7, c5, 0		@ invalidate entire I-cache
	mcr	p15, 0, r0, c7, c5, 0		@ invalidate entire I-cache
	msr	cpsr_cx, r1			@ restore interrupts
	.rept	11				@ ARM Ltd recommends at least
	nop					@ 11 NOPs
	.endr
#else
	mcr	p15, 0, r0, c7, c5, 0		@ invalidate I-cache
#endif
	mov	pc, lr
ENDPROC(v6_flush_icache_all)

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
#ifndef CONFIG_ARM_ERRATA_411920
	mcr	p15, 0, r0, c7, c5, 0		@ I+BTB cache invalidate
#else
	b	v6_flush_icache_all
#endif
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
 UNWIND(.fnstart		)
#ifdef HARVARD_CACHE
	bic	r0, r0, #CACHE_LINE_SIZE - 1
1:
 USER(	mcr	p15, 0, r0, c7, c10, 1	)	@ clean D line
	add	r0, r0, #CACHE_LINE_SIZE
2:
	cmp	r0, r1
	blo	1b
#endif
	mov	r0, #0
#ifdef HARVARD_CACHE
	mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer
#ifndef CONFIG_ARM_ERRATA_411920
	mcr	p15, 0, r0, c7, c5, 0		@ I+BTB cache invalidate
#else
	b	v6_flush_icache_all
#endif
#else
	mcr	p15, 0, r0, c7, c5, 6		@ invalidate BTB
#endif
	mov	pc, lr

/*
 * Fault handling for the cache operation above. If the virtual address in r0
 * isn't mapped, just try the next page.
 */
9001:
	mov	r0, r0, lsr #12
	mov	r0, r0, lsl #12
	add	r0, r0, #4096
	b	2b
 UNWIND(.fnend		)
ENDPROC(v6_coherent_user_range)
ENDPROC(v6_coherent_kern_range)

/*
 *	v6_flush_kern_dcache_area(void *addr, size_t size)
 *
 *	Ensure that the data held in the page kaddr is written back
 *	to the page in question.
 *
 *	- addr	- kernel address
 *	- size	- region size
 */
ENTRY(v6_flush_kern_dcache_area)
	add	r1, r0, r1
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
v6_dma_inv_range:
#ifdef CONFIG_DMA_CACHE_RWFO
	ldrb	r2, [r0]			@ read for ownership
	strb	r2, [r0]			@ write for ownership
#endif
	tst	r0, #D_CACHE_LINE_SIZE - 1
	bic	r0, r0, #D_CACHE_LINE_SIZE - 1
#ifdef HARVARD_CACHE
	mcrne	p15, 0, r0, c7, c10, 1		@ clean D line
#else
	mcrne	p15, 0, r0, c7, c11, 1		@ clean unified line
#endif
	tst	r1, #D_CACHE_LINE_SIZE - 1
#ifdef CONFIG_DMA_CACHE_RWFO
	ldrneb	r2, [r1, #-1]			@ read for ownership
	strneb	r2, [r1, #-1]			@ write for ownership
#endif
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
#ifdef CONFIG_DMA_CACHE_RWFO
	ldrlo	r2, [r0]			@ read for ownership
	strlo	r2, [r0]			@ write for ownership
#endif
	blo	1b
	mov	r0, #0
	mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer
	mov	pc, lr

/*
 *	v6_dma_clean_range(start,end)
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
v6_dma_clean_range:
	bic	r0, r0, #D_CACHE_LINE_SIZE - 1
1:
#ifdef CONFIG_DMA_CACHE_RWFO
	ldr	r2, [r0]			@ read for ownership
#endif
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
#ifdef CONFIG_DMA_CACHE_RWFO
	ldrb	r2, [r0]		@ read for ownership
	strb	r2, [r0]		@ write for ownership
#endif
	bic	r0, r0, #D_CACHE_LINE_SIZE - 1
1:
#ifdef HARVARD_CACHE
	mcr	p15, 0, r0, c7, c14, 1		@ clean & invalidate D line
#else
	mcr	p15, 0, r0, c7, c15, 1		@ clean & invalidate line
#endif
	add	r0, r0, #D_CACHE_LINE_SIZE
	cmp	r0, r1
#ifdef CONFIG_DMA_CACHE_RWFO
	ldrlob	r2, [r0]			@ read for ownership
	strlob	r2, [r0]			@ write for ownership
#endif
	blo	1b
	mov	r0, #0
	mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer
	mov	pc, lr

/*
 *	dma_map_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(v6_dma_map_area)
	add	r1, r1, r0
	teq	r2, #DMA_FROM_DEVICE
	beq	v6_dma_inv_range
#ifndef CONFIG_DMA_CACHE_RWFO
	b	v6_dma_clean_range
#else
	teq	r2, #DMA_TO_DEVICE
	beq	v6_dma_clean_range
	b	v6_dma_flush_range
#endif
ENDPROC(v6_dma_map_area)

/*
 *	dma_unmap_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(v6_dma_unmap_area)
#ifndef CONFIG_DMA_CACHE_RWFO
	add	r1, r1, r0
	teq	r2, #DMA_TO_DEVICE
	bne	v6_dma_inv_range
#endif
	mov	pc, lr
ENDPROC(v6_dma_unmap_area)

	__INITDATA

	.type	v6_cache_fns, #object
ENTRY(v6_cache_fns)
	.long	v6_flush_icache_all
	.long	v6_flush_kern_cache_all
	.long	v6_flush_user_cache_all
	.long	v6_flush_user_cache_range
	.long	v6_coherent_kern_range
	.long	v6_coherent_user_range
	.long	v6_flush_kern_dcache_area
	.long	v6_dma_map_area
	.long	v6_dma_unmap_area
	.long	v6_dma_flush_range
	.size	v6_cache_fns, . - v6_cache_fns
