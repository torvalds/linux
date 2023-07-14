/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/arch/arm/mm/cache-v7m.S
 *
 *  Based on linux/arch/arm/mm/cache-v7.S
 *
 *  Copyright (C) 2001 Deep Blue Solutions Ltd.
 *  Copyright (C) 2005 ARM Ltd.
 *
 *  This is the "shell" of the ARMv7M processor support.
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/errno.h>
#include <asm/unwind.h>
#include <asm/v7m.h>

#include "proc-macros.S"

.arch armv7-m

/* Generic V7M read/write macros for memory mapped cache operations */
.macro v7m_cache_read, rt, reg
	movw	\rt, #:lower16:BASEADDR_V7M_SCB + \reg
	movt	\rt, #:upper16:BASEADDR_V7M_SCB + \reg
	ldr     \rt, [\rt]
.endm

.macro v7m_cacheop, rt, tmp, op, c = al
	movw\c	\tmp, #:lower16:BASEADDR_V7M_SCB + \op
	movt\c	\tmp, #:upper16:BASEADDR_V7M_SCB + \op
	str\c	\rt, [\tmp]
.endm


.macro	read_ccsidr, rt
	v7m_cache_read \rt, V7M_SCB_CCSIDR
.endm

.macro read_clidr, rt
	v7m_cache_read \rt, V7M_SCB_CLIDR
.endm

.macro	write_csselr, rt, tmp
	v7m_cacheop \rt, \tmp, V7M_SCB_CSSELR
.endm

/*
 * dcisw: Invalidate data cache by set/way
 */
.macro dcisw, rt, tmp
	v7m_cacheop \rt, \tmp, V7M_SCB_DCISW
.endm

/*
 * dccisw: Clean and invalidate data cache by set/way
 */
.macro dccisw, rt, tmp
	v7m_cacheop \rt, \tmp, V7M_SCB_DCCISW
.endm

/*
 * dccimvac: Clean and invalidate data cache line by MVA to PoC.
 */
.irp    c,,eq,ne,cs,cc,mi,pl,vs,vc,hi,ls,ge,lt,gt,le,hs,lo
.macro dccimvac\c, rt, tmp
	v7m_cacheop \rt, \tmp, V7M_SCB_DCCIMVAC, \c
.endm
.endr

/*
 * dcimvac: Invalidate data cache line by MVA to PoC
 */
.irp    c,,eq,ne,cs,cc,mi,pl,vs,vc,hi,ls,ge,lt,gt,le,hs,lo
.macro dcimvac\c, rt, tmp
	v7m_cacheop \rt, \tmp, V7M_SCB_DCIMVAC, \c
.endm
.endr

/*
 * dccmvau: Clean data cache line by MVA to PoU
 */
.macro dccmvau, rt, tmp
	v7m_cacheop \rt, \tmp, V7M_SCB_DCCMVAU
.endm

/*
 * dccmvac: Clean data cache line by MVA to PoC
 */
.macro dccmvac,  rt, tmp
	v7m_cacheop \rt, \tmp, V7M_SCB_DCCMVAC
.endm

/*
 * icimvau: Invalidate instruction caches by MVA to PoU
 */
.macro icimvau, rt, tmp
	v7m_cacheop \rt, \tmp, V7M_SCB_ICIMVAU
.endm

/*
 * Invalidate the icache, inner shareable if SMP, invalidate BTB for UP.
 * rt data ignored by ICIALLU(IS), so can be used for the address
 */
.macro invalidate_icache, rt
	v7m_cacheop \rt, \rt, V7M_SCB_ICIALLU
	mov \rt, #0
.endm

/*
 * Invalidate the BTB, inner shareable if SMP.
 * rt data ignored by BPIALL, so it can be used for the address
 */
.macro invalidate_bp, rt
	v7m_cacheop \rt, \rt, V7M_SCB_BPIALL
	mov \rt, #0
.endm

ENTRY(v7m_invalidate_l1)
	mov	r0, #0

	write_csselr r0, r1
	read_ccsidr r0

	movw	r1, #0x7fff
	and	r2, r1, r0, lsr #13

	movw	r1, #0x3ff

	and	r3, r1, r0, lsr #3      @ NumWays - 1
	add	r2, r2, #1              @ NumSets

	and	r0, r0, #0x7
	add	r0, r0, #4      @ SetShift

	clz	r1, r3          @ WayShift
	add	r4, r3, #1      @ NumWays
1:	sub	r2, r2, #1      @ NumSets--
	mov	r3, r4          @ Temp = NumWays
2:	subs	r3, r3, #1      @ Temp--
	mov	r5, r3, lsl r1
	mov	r6, r2, lsl r0
	orr	r5, r5, r6      @ Reg = (Temp<<WayShift)|(NumSets<<SetShift)
	dcisw	r5, r6
	bgt	2b
	cmp	r2, #0
	bgt	1b
	dsb	st
	isb
	ret	lr
ENDPROC(v7m_invalidate_l1)

/*
 *	v7m_flush_icache_all()
 *
 *	Flush the whole I-cache.
 *
 *	Registers:
 *	r0 - set to 0
 */
ENTRY(v7m_flush_icache_all)
	invalidate_icache r0
	ret	lr
ENDPROC(v7m_flush_icache_all)

/*
 *	v7m_flush_dcache_all()
 *
 *	Flush the whole D-cache.
 *
 *	Corrupted registers: r0-r7, r9-r11
 */
ENTRY(v7m_flush_dcache_all)
	dmb					@ ensure ordering with previous memory accesses
	read_clidr r0
	mov	r3, r0, lsr #23			@ move LoC into position
	ands	r3, r3, #7 << 1			@ extract LoC*2 from clidr
	beq	finished			@ if loc is 0, then no need to clean
start_flush_levels:
	mov	r10, #0				@ start clean at cache level 0
flush_levels:
	add	r2, r10, r10, lsr #1		@ work out 3x current cache level
	mov	r1, r0, lsr r2			@ extract cache type bits from clidr
	and	r1, r1, #7			@ mask of the bits for current cache only
	cmp	r1, #2				@ see what cache we have at this level
	blt	skip				@ skip if no cache, or just i-cache
#ifdef CONFIG_PREEMPTION
	save_and_disable_irqs_notrace r9	@ make cssr&csidr read atomic
#endif
	write_csselr r10, r1			@ set current cache level
	isb					@ isb to sych the new cssr&csidr
	read_ccsidr r1				@ read the new csidr
#ifdef CONFIG_PREEMPTION
	restore_irqs_notrace r9
#endif
	and	r2, r1, #7			@ extract the length of the cache lines
	add	r2, r2, #4			@ add 4 (line length offset)
	movw	r4, #0x3ff
	ands	r4, r4, r1, lsr #3		@ find maximum number on the way size
	clz	r5, r4				@ find bit position of way size increment
	movw	r7, #0x7fff
	ands	r7, r7, r1, lsr #13		@ extract max number of the index size
loop1:
	mov	r9, r7				@ create working copy of max index
loop2:
	lsl	r6, r4, r5
	orr	r11, r10, r6			@ factor way and cache number into r11
	lsl	r6, r9, r2
	orr	r11, r11, r6			@ factor index number into r11
	dccisw	r11, r6				@ clean/invalidate by set/way
	subs	r9, r9, #1			@ decrement the index
	bge	loop2
	subs	r4, r4, #1			@ decrement the way
	bge	loop1
skip:
	add	r10, r10, #2			@ increment cache number
	cmp	r3, r10
	bgt	flush_levels
finished:
	mov	r10, #0				@ switch back to cache level 0
	write_csselr r10, r3			@ select current cache level in cssr
	dsb	st
	isb
	ret	lr
ENDPROC(v7m_flush_dcache_all)

/*
 *	v7m_flush_cache_all()
 *
 *	Flush the entire cache system.
 *  The data cache flush is now achieved using atomic clean / invalidates
 *  working outwards from L1 cache. This is done using Set/Way based cache
 *  maintenance instructions.
 *  The instruction cache can still be invalidated back to the point of
 *  unification in a single instruction.
 *
 */
ENTRY(v7m_flush_kern_cache_all)
	stmfd	sp!, {r4-r7, r9-r11, lr}
	bl	v7m_flush_dcache_all
	invalidate_icache r0
	ldmfd	sp!, {r4-r7, r9-r11, lr}
	ret	lr
ENDPROC(v7m_flush_kern_cache_all)

/*
 *	v7m_flush_cache_all()
 *
 *	Flush all TLB entries in a particular address space
 *
 *	- mm    - mm_struct describing address space
 */
ENTRY(v7m_flush_user_cache_all)
	/*FALLTHROUGH*/

/*
 *	v7m_flush_cache_range(start, end, flags)
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
ENTRY(v7m_flush_user_cache_range)
	ret	lr
ENDPROC(v7m_flush_user_cache_all)
ENDPROC(v7m_flush_user_cache_range)

/*
 *	v7m_coherent_kern_range(start,end)
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
ENTRY(v7m_coherent_kern_range)
	/* FALLTHROUGH */

/*
 *	v7m_coherent_user_range(start,end)
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
ENTRY(v7m_coherent_user_range)
 UNWIND(.fnstart		)
	dcache_line_size r2, r3
	sub	r3, r2, #1
	bic	r12, r0, r3
1:
/*
 * We use open coded version of dccmvau otherwise USER() would
 * point at movw instruction.
 */
	dccmvau	r12, r3
	add	r12, r12, r2
	cmp	r12, r1
	blo	1b
	dsb	ishst
	icache_line_size r2, r3
	sub	r3, r2, #1
	bic	r12, r0, r3
2:
	icimvau r12, r3
	add	r12, r12, r2
	cmp	r12, r1
	blo	2b
	invalidate_bp r0
	dsb	ishst
	isb
	ret	lr
 UNWIND(.fnend		)
ENDPROC(v7m_coherent_kern_range)
ENDPROC(v7m_coherent_user_range)

/*
 *	v7m_flush_kern_dcache_area(void *addr, size_t size)
 *
 *	Ensure that the data held in the page kaddr is written back
 *	to the page in question.
 *
 *	- addr	- kernel address
 *	- size	- region size
 */
ENTRY(v7m_flush_kern_dcache_area)
	dcache_line_size r2, r3
	add	r1, r0, r1
	sub	r3, r2, #1
	bic	r0, r0, r3
1:
	dccimvac r0, r3		@ clean & invalidate D line / unified line
	add	r0, r0, r2
	cmp	r0, r1
	blo	1b
	dsb	st
	ret	lr
ENDPROC(v7m_flush_kern_dcache_area)

/*
 *	v7m_dma_inv_range(start,end)
 *
 *	Invalidate the data cache within the specified region; we will
 *	be performing a DMA operation in this region and we want to
 *	purge old data in the cache.
 *
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
v7m_dma_inv_range:
	dcache_line_size r2, r3
	sub	r3, r2, #1
	tst	r0, r3
	bic	r0, r0, r3
	dccimvacne r0, r3
	addne	r0, r0, r2
	subne	r3, r2, #1	@ restore r3, corrupted by v7m's dccimvac
	tst	r1, r3
	bic	r1, r1, r3
	dccimvacne r1, r3
	cmp	r0, r1
1:
	dcimvaclo r0, r3
	addlo	r0, r0, r2
	cmplo	r0, r1
	blo	1b
	dsb	st
	ret	lr
ENDPROC(v7m_dma_inv_range)

/*
 *	v7m_dma_clean_range(start,end)
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
v7m_dma_clean_range:
	dcache_line_size r2, r3
	sub	r3, r2, #1
	bic	r0, r0, r3
1:
	dccmvac r0, r3			@ clean D / U line
	add	r0, r0, r2
	cmp	r0, r1
	blo	1b
	dsb	st
	ret	lr
ENDPROC(v7m_dma_clean_range)

/*
 *	v7m_dma_flush_range(start,end)
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
ENTRY(v7m_dma_flush_range)
	dcache_line_size r2, r3
	sub	r3, r2, #1
	bic	r0, r0, r3
1:
	dccimvac r0, r3			 @ clean & invalidate D / U line
	add	r0, r0, r2
	cmp	r0, r1
	blo	1b
	dsb	st
	ret	lr
ENDPROC(v7m_dma_flush_range)

/*
 *	dma_map_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(v7m_dma_map_area)
	add	r1, r1, r0
	teq	r2, #DMA_FROM_DEVICE
	beq	v7m_dma_inv_range
	b	v7m_dma_clean_range
ENDPROC(v7m_dma_map_area)

/*
 *	dma_unmap_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(v7m_dma_unmap_area)
	add	r1, r1, r0
	teq	r2, #DMA_TO_DEVICE
	bne	v7m_dma_inv_range
	ret	lr
ENDPROC(v7m_dma_unmap_area)

	.globl	v7m_flush_kern_cache_louis
	.equ	v7m_flush_kern_cache_louis, v7m_flush_kern_cache_all

	__INITDATA

	@ define struct cpu_cache_fns (see <asm/cacheflush.h> and proc-macros.S)
	define_cache_functions v7m
