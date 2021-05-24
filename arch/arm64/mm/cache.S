/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Cache maintenance
 *
 * Copyright (C) 2001 Deep Blue Solutions Ltd.
 * Copyright (C) 2012 ARM Ltd.
 */

#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/cpufeature.h>
#include <asm/alternative.h>
#include <asm/asm-uaccess.h>

/*
 *	__flush_cache_range(start,end) [fixup]
 *
 *	Ensure that the I and D caches are coherent within specified region.
 *	This is typically used when code has been written to a memory region,
 *	and will be executed.
 *
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 *	- fixup   - optional label to branch to on user fault
 */
.macro	__flush_cache_range, fixup
alternative_if ARM64_HAS_CACHE_IDC
	dsb     ishst
	b       .Ldc_skip_\@
alternative_else_nop_endif
	mov     x2, x0
	mov     x3, x1
	dcache_by_line_op cvau, ish, x2, x3, x4, x5, \fixup
.Ldc_skip_\@:
alternative_if ARM64_HAS_CACHE_DIC
	isb
	b	.Lic_skip_\@
alternative_else_nop_endif
	invalidate_icache_by_line x0, x1, x2, x3, \fixup
.Lic_skip_\@:
.endm

/*
 *	__flush_icache_range(start,end)
 *
 *	Ensure that the I and D caches are coherent within specified region.
 *	This is typically used when code has been written to a memory region,
 *	and will be executed.
 *
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
SYM_FUNC_START(__flush_icache_range)
	__flush_cache_range
	ret
SYM_FUNC_END(__flush_icache_range)

/*
 *	__flush_cache_user_range(start,end)
 *
 *	Ensure that the I and D caches are coherent within specified region.
 *	This is typically used when code has been written to a memory region,
 *	and will be executed.
 *
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
SYM_FUNC_START(__flush_cache_user_range)
	uaccess_ttbr0_enable x2, x3, x4

	__flush_cache_range 2f
	mov	x0, xzr
1:
	uaccess_ttbr0_disable x1, x2
	ret
2:
	mov	x0, #-EFAULT
	b	1b
SYM_FUNC_END(__flush_cache_user_range)

/*
 *	invalidate_icache_range(start,end)
 *
 *	Ensure that the I cache is invalid within specified region.
 *
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
SYM_FUNC_START(invalidate_icache_range)
alternative_if ARM64_HAS_CACHE_DIC
	isb
	ret
alternative_else_nop_endif

	invalidate_icache_by_line x0, x1, x2, x3
	ret
SYM_FUNC_END(invalidate_icache_range)

/*
 *	__flush_dcache_area(start, end)
 *
 *	Ensure that any D-cache lines for the interval [start, end)
 *	are cleaned and invalidated to the PoC.
 *
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
SYM_FUNC_START_PI(__flush_dcache_area)
	dcache_by_line_op civac, sy, x0, x1, x2, x3
	ret
SYM_FUNC_END_PI(__flush_dcache_area)

/*
 *	__clean_dcache_area_pou(kaddr, size)
 *
 * 	Ensure that any D-cache lines for the interval [kaddr, kaddr+size)
 * 	are cleaned to the PoU.
 *
 *	- kaddr   - kernel address
 *	- size    - size in question
 */
SYM_FUNC_START(__clean_dcache_area_pou)
alternative_if ARM64_HAS_CACHE_IDC
	dsb	ishst
	ret
alternative_else_nop_endif
	add	x1, x0, x1
	dcache_by_line_op cvau, ish, x0, x1, x2, x3
	ret
SYM_FUNC_END(__clean_dcache_area_pou)

/*
 *	__inval_dcache_area(start, end)
 *
 * 	Ensure that any D-cache lines for the interval [start, end)
 * 	are invalidated. Any partial lines at the ends of the interval are
 *	also cleaned to PoC to prevent data loss.
 *
 *	- start   - kernel start address of region
 *	- end     - kernel end address of region
 */
SYM_FUNC_START_LOCAL(__dma_inv_area)
SYM_FUNC_START_PI(__inval_dcache_area)
	/* FALLTHROUGH */

/*
 *	__dma_inv_area(start, end)
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
	dcache_line_size x2, x3
	sub	x3, x2, #1
	tst	x1, x3				// end cache line aligned?
	bic	x1, x1, x3
	b.eq	1f
	dc	civac, x1			// clean & invalidate D / U line
1:	tst	x0, x3				// start cache line aligned?
	bic	x0, x0, x3
	b.eq	2f
	dc	civac, x0			// clean & invalidate D / U line
	b	3f
2:	dc	ivac, x0			// invalidate D / U line
3:	add	x0, x0, x2
	cmp	x0, x1
	b.lo	2b
	dsb	sy
	ret
SYM_FUNC_END_PI(__inval_dcache_area)
SYM_FUNC_END(__dma_inv_area)

/*
 *	__clean_dcache_area_poc(start, end)
 *
 * 	Ensure that any D-cache lines for the interval [start, end)
 * 	are cleaned to the PoC.
 *
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
SYM_FUNC_START_LOCAL(__dma_clean_area)
SYM_FUNC_START_PI(__clean_dcache_area_poc)
	/* FALLTHROUGH */

/*
 *	__dma_clean_area(start, end)
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
	dcache_by_line_op cvac, sy, x0, x1, x2, x3
	ret
SYM_FUNC_END_PI(__clean_dcache_area_poc)
SYM_FUNC_END(__dma_clean_area)

/*
 *	__clean_dcache_area_pop(start, end)
 *
 * 	Ensure that any D-cache lines for the interval [start, end)
 * 	are cleaned to the PoP.
 *
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
SYM_FUNC_START_PI(__clean_dcache_area_pop)
	alternative_if_not ARM64_HAS_DCPOP
	b	__clean_dcache_area_poc
	alternative_else_nop_endif
	dcache_by_line_op cvap, sy, x0, x1, x2, x3
	ret
SYM_FUNC_END_PI(__clean_dcache_area_pop)

/*
 *	__dma_flush_area(start, size)
 *
 *	clean & invalidate D / U line
 *
 *	- start   - virtual start address of region
 *	- size    - size in question
 */
SYM_FUNC_START_PI(__dma_flush_area)
	add	x1, x0, x1
	dcache_by_line_op civac, sy, x0, x1, x2, x3
	ret
SYM_FUNC_END_PI(__dma_flush_area)

/*
 *	__dma_map_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
SYM_FUNC_START_PI(__dma_map_area)
	add	x1, x0, x1
	cmp	w2, #DMA_FROM_DEVICE
	b.eq	__dma_inv_area
	b	__dma_clean_area
SYM_FUNC_END_PI(__dma_map_area)

/*
 *	__dma_unmap_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
SYM_FUNC_START_PI(__dma_unmap_area)
	add	x1, x0, x1
	cmp	w2, #DMA_TO_DEVICE
	b.ne	__dma_inv_area
	ret
SYM_FUNC_END_PI(__dma_unmap_area)
