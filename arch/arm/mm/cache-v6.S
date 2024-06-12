/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/arch/arm/mm/cache-v6.S
 *
 *  Copyright (C) 2001 Deep Blue Solutions Ltd.
 *
 *  This is the "shell" of the ARMv6 processor support.
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/cfi_types.h>
#include <asm/assembler.h>
#include <asm/errno.h>
#include <asm/unwind.h>

#include "proc-macros.S"

#define HARVARD_CACHE
#define CACHE_LINE_SIZE		32
#define D_CACHE_LINE_SIZE	32
#define BTB_FLUSH_SIZE		8

.arch armv6

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
SYM_TYPED_FUNC_START(v6_flush_icache_all)
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
	ret	lr
SYM_FUNC_END(v6_flush_icache_all)

/*
 *	v6_flush_cache_all()
 *
 *	Flush the entire cache.
 *
 *	It is assumed that:
 */
SYM_TYPED_FUNC_START(v6_flush_kern_cache_all)
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
	ret	lr
SYM_FUNC_END(v6_flush_kern_cache_all)

/*
 *	v6_flush_cache_all()
 *
 *	Flush all TLB entries in a particular address space
 *
 *	- mm    - mm_struct describing address space
 */
SYM_TYPED_FUNC_START(v6_flush_user_cache_all)
	ret	lr
SYM_FUNC_END(v6_flush_user_cache_all)

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
SYM_TYPED_FUNC_START(v6_flush_user_cache_range)
	ret	lr
SYM_FUNC_END(v6_flush_user_cache_range)

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
SYM_TYPED_FUNC_START(v6_coherent_kern_range)
#ifdef CONFIG_CFI_CLANG /* Fallthrough if !CFI */
	b	v6_coherent_user_range
#endif
SYM_FUNC_END(v6_coherent_kern_range)

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
SYM_TYPED_FUNC_START(v6_coherent_user_range)
 UNWIND(.fnstart		)
#ifdef HARVARD_CACHE
	bic	r0, r0, #CACHE_LINE_SIZE - 1
1:
 USER(	mcr	p15, 0, r0, c7, c10, 1	)	@ clean D line
	add	r0, r0, #CACHE_LINE_SIZE
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
	ret	lr

/*
 * Fault handling for the cache operation above. If the virtual address in r0
 * isn't mapped, fail with -EFAULT.
 */
9001:
	mov	r0, #-EFAULT
	ret	lr
 UNWIND(.fnend		)
SYM_FUNC_END(v6_coherent_user_range)

/*
 *	v6_flush_kern_dcache_area(void *addr, size_t size)
 *
 *	Ensure that the data held in the page kaddr is written back
 *	to the page in question.
 *
 *	- addr	- kernel address
 *	- size	- region size
 */
SYM_TYPED_FUNC_START(v6_flush_kern_dcache_area)
	add	r1, r0, r1
	bic	r0, r0, #D_CACHE_LINE_SIZE - 1
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
	ret	lr
SYM_FUNC_END(v6_flush_kern_dcache_area)

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
	ret	lr

/*
 *	v6_dma_clean_range(start,end)
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
v6_dma_clean_range:
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
	ret	lr

/*
 *	v6_dma_flush_range(start,end)
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
SYM_TYPED_FUNC_START(v6_dma_flush_range)
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
	ret	lr
SYM_FUNC_END(v6_dma_flush_range)

/*
 *	dma_map_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
SYM_TYPED_FUNC_START(v6_dma_map_area)
	add	r1, r1, r0
	teq	r2, #DMA_FROM_DEVICE
	beq	v6_dma_inv_range
	b	v6_dma_clean_range
SYM_FUNC_END(v6_dma_map_area)

/*
 *	dma_unmap_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
SYM_TYPED_FUNC_START(v6_dma_unmap_area)
	add	r1, r1, r0
	teq	r2, #DMA_TO_DEVICE
	bne	v6_dma_inv_range
	ret	lr
SYM_FUNC_END(v6_dma_unmap_area)
