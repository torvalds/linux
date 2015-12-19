/*
 * Cache maintenance
 *
 * Copyright (C) 2001 Deep Blue Solutions Ltd.
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/cpufeature.h>
#include <asm/alternative.h>

#include "proc-macros.S"

/*
 *	flush_icache_range(start,end)
 *
 *	Ensure that the I and D caches are coherent within specified region.
 *	This is typically used when code has been written to a memory region,
 *	and will be executed.
 *
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
ENTRY(flush_icache_range)
	/* FALLTHROUGH */

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
ENTRY(__flush_cache_user_range)
	dcache_line_size x2, x3
	sub	x3, x2, #1
	bic	x4, x0, x3
1:
USER(9f, dc	cvau, x4	)		// clean D line to PoU
	add	x4, x4, x2
	cmp	x4, x1
	b.lo	1b
	dsb	ish

	icache_line_size x2, x3
	sub	x3, x2, #1
	bic	x4, x0, x3
1:
USER(9f, ic	ivau, x4	)		// invalidate I line PoU
	add	x4, x4, x2
	cmp	x4, x1
	b.lo	1b
	dsb	ish
	isb
	mov	x0, #0
	ret
9:
	mov	x0, #-EFAULT
	ret
ENDPROC(flush_icache_range)
ENDPROC(__flush_cache_user_range)

/*
 *	__flush_dcache_area(kaddr, size)
 *
 *	Ensure that the data held in the page kaddr is written back to the
 *	page in question.
 *
 *	- kaddr   - kernel address
 *	- size    - size in question
 */
ENTRY(__flush_dcache_area)
	dcache_line_size x2, x3
	add	x1, x0, x1
	sub	x3, x2, #1
	bic	x0, x0, x3
1:	dc	civac, x0			// clean & invalidate D line / unified line
	add	x0, x0, x2
	cmp	x0, x1
	b.lo	1b
	dsb	sy
	ret
ENDPIPROC(__flush_dcache_area)

/*
 *	__inval_cache_range(start, end)
 *	- start   - start address of region
 *	- end     - end address of region
 */
ENTRY(__inval_cache_range)
	/* FALLTHROUGH */

/*
 *	__dma_inv_range(start, end)
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
__dma_inv_range:
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
ENDPIPROC(__inval_cache_range)
ENDPROC(__dma_inv_range)

/*
 *	__dma_clean_range(start, end)
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
__dma_clean_range:
	dcache_line_size x2, x3
	sub	x3, x2, #1
	bic	x0, x0, x3
1:
alternative_if_not ARM64_WORKAROUND_CLEAN_CACHE
	dc	cvac, x0
alternative_else
	dc	civac, x0
alternative_endif
	add	x0, x0, x2
	cmp	x0, x1
	b.lo	1b
	dsb	sy
	ret
ENDPROC(__dma_clean_range)

/*
 *	__dma_flush_range(start, end)
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
ENTRY(__dma_flush_range)
	dcache_line_size x2, x3
	sub	x3, x2, #1
	bic	x0, x0, x3
1:	dc	civac, x0			// clean & invalidate D / U line
	add	x0, x0, x2
	cmp	x0, x1
	b.lo	1b
	dsb	sy
	ret
ENDPIPROC(__dma_flush_range)

/*
 *	__dma_map_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(__dma_map_area)
	add	x1, x1, x0
	cmp	w2, #DMA_FROM_DEVICE
	b.eq	__dma_inv_range
	b	__dma_clean_range
ENDPIPROC(__dma_map_area)

/*
 *	__dma_unmap_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(__dma_unmap_area)
	add	x1, x1, x0
	cmp	w2, #DMA_TO_DEVICE
	b.ne	__dma_inv_range
	ret
ENDPIPROC(__dma_unmap_area)
