/*
 *  linux/arch/arm/mm/cache-v4.S
 *
 *  Copyright (C) 1997-2002 Russell king
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/page.h>
#include "proc-macros.S"

/*
 *	flush_icache_all()
 *
 *	Unconditionally clean and invalidate the entire icache.
 */
ENTRY(v4_flush_icache_all)
	mov	pc, lr
ENDPROC(v4_flush_icache_all)

/*
 *	flush_user_cache_all()
 *
 *	Invalidate all cache entries in a particular address
 *	space.
 *
 *	- mm	- mm_struct describing address space
 */
ENTRY(v4_flush_user_cache_all)
	/* FALLTHROUGH */
/*
 *	flush_kern_cache_all()
 *
 *	Clean and invalidate the entire cache.
 */
ENTRY(v4_flush_kern_cache_all)
#ifdef CONFIG_CPU_CP15
	mov	r0, #0
	mcr	p15, 0, r0, c7, c7, 0		@ flush ID cache
	mov	pc, lr
#else
	/* FALLTHROUGH */
#endif

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
ENTRY(v4_flush_user_cache_range)
#ifdef CONFIG_CPU_CP15
	mov	ip, #0
	mcr	p15, 0, ip, c7, c7, 0		@ flush ID cache
	mov	pc, lr
#else
	/* FALLTHROUGH */
#endif

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
ENTRY(v4_coherent_kern_range)
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
ENTRY(v4_coherent_user_range)
	mov	r0, #0
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
ENTRY(v4_flush_kern_dcache_area)
	/* FALLTHROUGH */

/*
 *	dma_flush_range(start, end)
 *
 *	Clean and invalidate the specified virtual address range.
 *
 *	- start  - virtual start address
 *	- end	 - virtual end address
 */
ENTRY(v4_dma_flush_range)
#ifdef CONFIG_CPU_CP15
	mov	r0, #0
	mcr	p15, 0, r0, c7, c7, 0		@ flush ID cache
#endif
	mov	pc, lr

/*
 *	dma_unmap_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(v4_dma_unmap_area)
	teq	r2, #DMA_TO_DEVICE
	bne	v4_dma_flush_range
	/* FALLTHROUGH */

/*
 *	dma_map_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(v4_dma_map_area)
	mov	pc, lr
ENDPROC(v4_dma_unmap_area)
ENDPROC(v4_dma_map_area)

	.globl	v4_flush_kern_cache_louis
	.equ	v4_flush_kern_cache_louis, v4_flush_kern_cache_all

	__INITDATA

	@ define struct cpu_cache_fns (see <asm/cacheflush.h> and proc-macros.S)
	define_cache_functions v4
