/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/arch/arm/mm/cache-v4.S
 *
 *  Copyright (C) 1997-2002 Russell king
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/cfi_types.h>
#include <asm/assembler.h>
#include <asm/page.h>
#include "proc-macros.S"

/*
 *	flush_icache_all()
 *
 *	Unconditionally clean and invalidate the entire icache.
 */
SYM_TYPED_FUNC_START(v4_flush_icache_all)
	ret	lr
SYM_FUNC_END(v4_flush_icache_all)

/*
 *	flush_user_cache_all()
 *
 *	Invalidate all cache entries in a particular address
 *	space.
 *
 *	- mm	- mm_struct describing address space
 */
SYM_FUNC_ALIAS(v4_flush_user_cache_all, v4_flush_kern_cache_all)

/*
 *	flush_kern_cache_all()
 *
 *	Clean and invalidate the entire cache.
 */
SYM_TYPED_FUNC_START(v4_flush_kern_cache_all)
#ifdef CONFIG_CPU_CP15
	mov	r0, #0
	mcr	p15, 0, r0, c7, c7, 0		@ flush ID cache
	ret	lr
#else
	ret	lr
#endif
SYM_FUNC_END(v4_flush_kern_cache_all)

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
SYM_TYPED_FUNC_START(v4_flush_user_cache_range)
#ifdef CONFIG_CPU_CP15
	mov	ip, #0
	mcr	p15, 0, ip, c7, c7, 0		@ flush ID cache
	ret	lr
#else
	ret	lr
#endif
SYM_FUNC_END(v4_flush_user_cache_range)

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
SYM_TYPED_FUNC_START(v4_coherent_kern_range)
	ret	lr
SYM_FUNC_END(v4_coherent_kern_range)

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
SYM_TYPED_FUNC_START(v4_coherent_user_range)
	mov	r0, #0
	ret	lr
SYM_FUNC_END(v4_coherent_user_range)

/*
 *	flush_kern_dcache_area(void *addr, size_t size)
 *
 *	Ensure no D cache aliasing occurs, either with itself or
 *	the I cache
 *
 *	- addr	- kernel address
 *	- size	- region size
 */
SYM_TYPED_FUNC_START(v4_flush_kern_dcache_area)
#ifdef CONFIG_CFI_CLANG /* Fallthrough if !CFI */
	b	v4_dma_flush_range
#endif
SYM_FUNC_END(v4_flush_kern_dcache_area)

/*
 *	dma_flush_range(start, end)
 *
 *	Clean and invalidate the specified virtual address range.
 *
 *	- start  - virtual start address
 *	- end	 - virtual end address
 */
SYM_TYPED_FUNC_START(v4_dma_flush_range)
#ifdef CONFIG_CPU_CP15
	mov	r0, #0
	mcr	p15, 0, r0, c7, c7, 0		@ flush ID cache
#endif
	ret	lr
SYM_FUNC_END(v4_dma_flush_range)

/*
 *	dma_unmap_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
SYM_TYPED_FUNC_START(v4_dma_unmap_area)
	teq	r2, #DMA_TO_DEVICE
	bne	v4_dma_flush_range
	ret	lr
SYM_FUNC_END(v4_dma_unmap_area)

/*
 *	dma_map_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
SYM_TYPED_FUNC_START(v4_dma_map_area)
	ret	lr
SYM_FUNC_END(v4_dma_map_area)
