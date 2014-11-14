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

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/cpufeature.h>
#include <asm/alternative-asm.h>

#include "proc-macros.S"

/*
 *	__flush_dcache_all()
 *
 *	Flush the whole D-cache.
 *
 *	Corrupted registers: x0-x7, x9-x11
 */
__flush_dcache_all:
	dmb	sy				// ensure ordering with previous memory accesses
	mrs	x0, clidr_el1			// read clidr
	and	x3, x0, #0x7000000		// extract loc from clidr
	lsr	x3, x3, #23			// left align loc bit field
	cbz	x3, finished			// if loc is 0, then no need to clean
	mov	x10, #0				// start clean at cache level 0
loop1:
	add	x2, x10, x10, lsr #1		// work out 3x current cache level
	lsr	x1, x0, x2			// extract cache type bits from clidr
	and	x1, x1, #7			// mask of the bits for current cache only
	cmp	x1, #2				// see what cache we have at this level
	b.lt	skip				// skip if no cache, or just i-cache
	save_and_disable_irqs x9		// make CSSELR and CCSIDR access atomic
	msr	csselr_el1, x10			// select current cache level in csselr
	isb					// isb to sych the new cssr&csidr
	mrs	x1, ccsidr_el1			// read the new ccsidr
	restore_irqs x9
	and	x2, x1, #7			// extract the length of the cache lines
	add	x2, x2, #4			// add 4 (line length offset)
	mov	x4, #0x3ff
	and	x4, x4, x1, lsr #3		// find maximum number on the way size
	clz	w5, w4				// find bit position of way size increment
	mov	x7, #0x7fff
	and	x7, x7, x1, lsr #13		// extract max number of the index size
loop2:
	mov	x9, x4				// create working copy of max way size
loop3:
	lsl	x6, x9, x5
	orr	x11, x10, x6			// factor way and cache number into x11
	lsl	x6, x7, x2
	orr	x11, x11, x6			// factor index number into x11
	dc	cisw, x11			// clean & invalidate by set/way
	subs	x9, x9, #1			// decrement the way
	b.ge	loop3
	subs	x7, x7, #1			// decrement the index
	b.ge	loop2
skip:
	add	x10, x10, #2			// increment cache number
	cmp	x3, x10
	b.gt	loop1
finished:
	mov	x10, #0				// swith back to cache level 0
	msr	csselr_el1, x10			// select current cache level in csselr
	dsb	sy
	isb
	ret
ENDPROC(__flush_dcache_all)

/*
 *	flush_cache_all()
 *
 *	Flush the entire cache system.  The data cache flush is now achieved
 *	using atomic clean / invalidates working outwards from L1 cache. This
 *	is done using Set/Way based cache maintainance instructions.  The
 *	instruction cache can still be invalidated back to the point of
 *	unification in a single instruction.
 */
ENTRY(flush_cache_all)
	mov	x12, lr
	bl	__flush_dcache_all
	mov	x0, #0
	ic	ialluis				// I+BTB cache invalidate
	ret	x12
ENDPROC(flush_cache_all)

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
9:						// ignore any faulting cache operation
	dsb	ish
	isb
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
ENDPROC(__flush_dcache_area)

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
ENDPROC(__inval_cache_range)
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
1:	alternative_insn "dc cvac, x0", "dc civac, x0", ARM64_WORKAROUND_CLEAN_CACHE
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
ENDPROC(__dma_flush_range)

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
ENDPROC(__dma_map_area)

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
ENDPROC(__dma_unmap_area)
