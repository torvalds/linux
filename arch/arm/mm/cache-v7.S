/*
 *  linux/arch/arm/mm/cache-v7.S
 *
 *  Copyright (C) 2001 Deep Blue Solutions Ltd.
 *  Copyright (C) 2005 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This is the "shell" of the ARMv7 processor support.
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/unwind.h>

#include "proc-macros.S"

/*
 *	v7_flush_icache_all()
 *
 *	Flush the whole I-cache.
 *
 *	Registers:
 *	r0 - set to 0
 */
ENTRY(v7_flush_icache_all)
	mov	r0, #0
	ALT_SMP(mcr	p15, 0, r0, c7, c1, 0)		@ invalidate I-cache inner shareable
	ALT_UP(mcr	p15, 0, r0, c7, c5, 0)		@ I+BTB cache invalidate
	mov	pc, lr
ENDPROC(v7_flush_icache_all)

/*
 *	v7_flush_dcache_all()
 *
 *	Flush the whole D-cache.
 *
 *	Corrupted registers: r0-r7, r9-r11 (r6 only in Thumb mode)
 *
 *	- mm    - mm_struct describing address space
 */
ENTRY(v7_flush_dcache_all)
	dmb					@ ensure ordering with previous memory accesses
	mrc	p15, 1, r0, c0, c0, 1		@ read clidr
	ands	r3, r0, #0x7000000		@ extract loc from clidr
	mov	r3, r3, lsr #23			@ left align loc bit field
	beq	finished			@ if loc is 0, then no need to clean
	mov	r10, #0				@ start clean at cache level 0
loop1:
	add	r2, r10, r10, lsr #1		@ work out 3x current cache level
	mov	r1, r0, lsr r2			@ extract cache type bits from clidr
	and	r1, r1, #7			@ mask of the bits for current cache only
	cmp	r1, #2				@ see what cache we have at this level
	blt	skip				@ skip if no cache, or just i-cache
	mcr	p15, 2, r10, c0, c0, 0		@ select current cache level in cssr
	isb					@ isb to sych the new cssr&csidr
	mrc	p15, 1, r1, c0, c0, 0		@ read the new csidr
	and	r2, r1, #7			@ extract the length of the cache lines
	add	r2, r2, #4			@ add 4 (line length offset)
	ldr	r4, =0x3ff
	ands	r4, r4, r1, lsr #3		@ find maximum number on the way size
	clz	r5, r4				@ find bit position of way size increment
	ldr	r7, =0x7fff
	ands	r7, r7, r1, lsr #13		@ extract max number of the index size
loop2:
	mov	r9, r4				@ create working copy of max way size
loop3:
 ARM(	orr	r11, r10, r9, lsl r5	)	@ factor way and cache number into r11
 THUMB(	lsl	r6, r9, r5		)
 THUMB(	orr	r11, r10, r6		)	@ factor way and cache number into r11
 ARM(	orr	r11, r11, r7, lsl r2	)	@ factor index number into r11
 THUMB(	lsl	r6, r7, r2		)
 THUMB(	orr	r11, r11, r6		)	@ factor index number into r11
	mcr	p15, 0, r11, c7, c14, 2		@ clean & invalidate by set/way
	subs	r9, r9, #1			@ decrement the way
	bge	loop3
	subs	r7, r7, #1			@ decrement the index
	bge	loop2
skip:
	add	r10, r10, #2			@ increment cache number
	cmp	r3, r10
	bgt	loop1
finished:
	mov	r10, #0				@ swith back to cache level 0
	mcr	p15, 2, r10, c0, c0, 0		@ select current cache level in cssr
	dsb
	isb
	mov	pc, lr
ENDPROC(v7_flush_dcache_all)

/*
 *	v7_flush_cache_all()
 *
 *	Flush the entire cache system.
 *  The data cache flush is now achieved using atomic clean / invalidates
 *  working outwards from L1 cache. This is done using Set/Way based cache
 *  maintainance instructions.
 *  The instruction cache can still be invalidated back to the point of
 *  unification in a single instruction.
 *
 */
ENTRY(v7_flush_kern_cache_all)
 ARM(	stmfd	sp!, {r4-r5, r7, r9-r11, lr}	)
 THUMB(	stmfd	sp!, {r4-r7, r9-r11, lr}	)
	bl	v7_flush_dcache_all
	mov	r0, #0
	ALT_SMP(mcr	p15, 0, r0, c7, c1, 0)	@ invalidate I-cache inner shareable
	ALT_UP(mcr	p15, 0, r0, c7, c5, 0)	@ I+BTB cache invalidate
 ARM(	ldmfd	sp!, {r4-r5, r7, r9-r11, lr}	)
 THUMB(	ldmfd	sp!, {r4-r7, r9-r11, lr}	)
	mov	pc, lr
ENDPROC(v7_flush_kern_cache_all)

/*
 *	v7_flush_cache_all()
 *
 *	Flush all TLB entries in a particular address space
 *
 *	- mm    - mm_struct describing address space
 */
ENTRY(v7_flush_user_cache_all)
	/*FALLTHROUGH*/

/*
 *	v7_flush_cache_range(start, end, flags)
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
ENTRY(v7_flush_user_cache_range)
	mov	pc, lr
ENDPROC(v7_flush_user_cache_all)
ENDPROC(v7_flush_user_cache_range)

/*
 *	v7_coherent_kern_range(start,end)
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
ENTRY(v7_coherent_kern_range)
	/* FALLTHROUGH */

/*
 *	v7_coherent_user_range(start,end)
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
ENTRY(v7_coherent_user_range)
 UNWIND(.fnstart		)
	dcache_line_size r2, r3
	sub	r3, r2, #1
	bic	r0, r0, r3
1:
 USER(	mcr	p15, 0, r0, c7, c11, 1	)	@ clean D line to the point of unification
	dsb
 USER(	mcr	p15, 0, r0, c7, c5, 1	)	@ invalidate I line
	add	r0, r0, r2
2:
	cmp	r0, r1
	blo	1b
	mov	r0, #0
	ALT_SMP(mcr	p15, 0, r0, c7, c1, 6)	@ invalidate BTB Inner Shareable
	ALT_UP(mcr	p15, 0, r0, c7, c5, 6)	@ invalidate BTB
	dsb
	isb
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
ENDPROC(v7_coherent_kern_range)
ENDPROC(v7_coherent_user_range)

/*
 *	v7_flush_kern_dcache_area(void *addr, size_t size)
 *
 *	Ensure that the data held in the page kaddr is written back
 *	to the page in question.
 *
 *	- addr	- kernel address
 *	- size	- region size
 */
ENTRY(v7_flush_kern_dcache_area)
	dcache_line_size r2, r3
	add	r1, r0, r1
1:
	mcr	p15, 0, r0, c7, c14, 1		@ clean & invalidate D line / unified line
	add	r0, r0, r2
	cmp	r0, r1
	blo	1b
	dsb
	mov	pc, lr
ENDPROC(v7_flush_kern_dcache_area)

/*
 *	v7_dma_inv_range(start,end)
 *
 *	Invalidate the data cache within the specified region; we will
 *	be performing a DMA operation in this region and we want to
 *	purge old data in the cache.
 *
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
v7_dma_inv_range:
	dcache_line_size r2, r3
	sub	r3, r2, #1
	tst	r0, r3
	bic	r0, r0, r3
	mcrne	p15, 0, r0, c7, c14, 1		@ clean & invalidate D / U line

	tst	r1, r3
	bic	r1, r1, r3
	mcrne	p15, 0, r1, c7, c14, 1		@ clean & invalidate D / U line
1:
	mcr	p15, 0, r0, c7, c6, 1		@ invalidate D / U line
	add	r0, r0, r2
	cmp	r0, r1
	blo	1b
	dsb
	mov	pc, lr
ENDPROC(v7_dma_inv_range)

/*
 *	v7_dma_clean_range(start,end)
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
v7_dma_clean_range:
	dcache_line_size r2, r3
	sub	r3, r2, #1
	bic	r0, r0, r3
1:
	mcr	p15, 0, r0, c7, c10, 1		@ clean D / U line
	add	r0, r0, r2
	cmp	r0, r1
	blo	1b
	dsb
	mov	pc, lr
ENDPROC(v7_dma_clean_range)

/*
 *	v7_dma_flush_range(start,end)
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
ENTRY(v7_dma_flush_range)
	dcache_line_size r2, r3
	sub	r3, r2, #1
	bic	r0, r0, r3
1:
	mcr	p15, 0, r0, c7, c14, 1		@ clean & invalidate D / U line
	add	r0, r0, r2
	cmp	r0, r1
	blo	1b
	dsb
	mov	pc, lr
ENDPROC(v7_dma_flush_range)

/*
 *	dma_map_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(v7_dma_map_area)
	add	r1, r1, r0
	teq	r2, #DMA_FROM_DEVICE
	beq	v7_dma_inv_range
	b	v7_dma_clean_range
ENDPROC(v7_dma_map_area)

/*
 *	dma_unmap_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(v7_dma_unmap_area)
	add	r1, r1, r0
	teq	r2, #DMA_TO_DEVICE
	bne	v7_dma_inv_range
	mov	pc, lr
ENDPROC(v7_dma_unmap_area)

	__INITDATA

	.type	v7_cache_fns, #object
ENTRY(v7_cache_fns)
	.long	v7_flush_icache_all
	.long	v7_flush_kern_cache_all
	.long	v7_flush_user_cache_all
	.long	v7_flush_user_cache_range
	.long	v7_coherent_kern_range
	.long	v7_coherent_user_range
	.long	v7_flush_kern_dcache_area
	.long	v7_dma_map_area
	.long	v7_dma_unmap_area
	.long	v7_dma_flush_range
	.size	v7_cache_fns, . - v7_cache_fns
