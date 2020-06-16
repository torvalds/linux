/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hypersparc.S: High speed Hypersparc mmu/cache operations.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/ptrace.h>
#include <asm/psr.h>
#include <asm/asm-offsets.h>
#include <asm/asi.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/pgtsrmmu.h>
#include <linux/init.h>

	.text
	.align	4

	.globl	hypersparc_flush_cache_all, hypersparc_flush_cache_mm
	.globl	hypersparc_flush_cache_range, hypersparc_flush_cache_page
	.globl	hypersparc_flush_page_to_ram
	.globl	hypersparc_flush_page_for_dma, hypersparc_flush_sig_insns
	.globl	hypersparc_flush_tlb_all, hypersparc_flush_tlb_mm
	.globl	hypersparc_flush_tlb_range, hypersparc_flush_tlb_page

hypersparc_flush_cache_all:
	WINDOW_FLUSH(%g4, %g5)
	sethi	%hi(vac_cache_size), %g4
	ld	[%g4 + %lo(vac_cache_size)], %g5
	sethi	%hi(vac_line_size), %g1
	ld	[%g1 + %lo(vac_line_size)], %g2
1:	
	subcc	%g5, %g2, %g5			! hyper_flush_unconditional_combined
	bne	1b
	 sta	%g0, [%g5] ASI_M_FLUSH_CTX
	retl
	 sta	%g0, [%g0] ASI_M_FLUSH_IWHOLE	! hyper_flush_whole_icache

	/* We expand the window flush to get maximum performance. */
hypersparc_flush_cache_mm:
#ifndef CONFIG_SMP
	ld	[%o0 + AOFF_mm_context], %g1
	cmp	%g1, -1
	be	hypersparc_flush_cache_mm_out
#endif
	WINDOW_FLUSH(%g4, %g5)

	sethi	%hi(vac_line_size), %g1
	ld	[%g1 + %lo(vac_line_size)], %o1
	sethi	%hi(vac_cache_size), %g2
	ld	[%g2 + %lo(vac_cache_size)], %o0
	add	%o1, %o1, %g1
	add	%o1, %g1, %g2
	add	%o1, %g2, %g3
	add	%o1, %g3, %g4
	add	%o1, %g4, %g5
	add	%o1, %g5, %o4
	add	%o1, %o4, %o5

	/* BLAMMO! */
1:
	subcc	%o0, %o5, %o0				! hyper_flush_cache_user
	sta	%g0, [%o0 + %g0] ASI_M_FLUSH_USER
	sta	%g0, [%o0 + %o1] ASI_M_FLUSH_USER
	sta	%g0, [%o0 + %g1] ASI_M_FLUSH_USER
	sta	%g0, [%o0 + %g2] ASI_M_FLUSH_USER
	sta	%g0, [%o0 + %g3] ASI_M_FLUSH_USER
	sta	%g0, [%o0 + %g4] ASI_M_FLUSH_USER
	sta	%g0, [%o0 + %g5] ASI_M_FLUSH_USER
	bne	1b
	 sta	%g0, [%o0 + %o4] ASI_M_FLUSH_USER
hypersparc_flush_cache_mm_out:
	retl
	 nop

	/* The things we do for performance... */
hypersparc_flush_cache_range:
	ld	[%o0 + VMA_VM_MM], %o0
#ifndef CONFIG_SMP
	ld	[%o0 + AOFF_mm_context], %g1
	cmp	%g1, -1
	be	hypersparc_flush_cache_range_out
#endif
	WINDOW_FLUSH(%g4, %g5)

	sethi	%hi(vac_line_size), %g1
	ld	[%g1 + %lo(vac_line_size)], %o4
	sethi	%hi(vac_cache_size), %g2
	ld	[%g2 + %lo(vac_cache_size)], %o3

	/* Here comes the fun part... */
	add	%o2, (PAGE_SIZE - 1), %o2
	andn	%o1, (PAGE_SIZE - 1), %o1
	add	%o4, %o4, %o5
	andn	%o2, (PAGE_SIZE - 1), %o2
	add	%o4, %o5, %g1
	sub	%o2, %o1, %g4
	add	%o4, %g1, %g2
	sll	%o3, 2, %g5
	add	%o4, %g2, %g3
	cmp	%g4, %g5
	add	%o4, %g3, %g4
	blu	0f
	 add	%o4, %g4, %g5
	add	%o4, %g5, %g7

	/* Flush entire user space, believe it or not this is quicker
	 * than page at a time flushings for range > (cache_size<<2).
	 */
1:
	subcc	%o3, %g7, %o3
	sta	%g0, [%o3 + %g0] ASI_M_FLUSH_USER
	sta	%g0, [%o3 + %o4] ASI_M_FLUSH_USER
	sta	%g0, [%o3 + %o5] ASI_M_FLUSH_USER
	sta	%g0, [%o3 + %g1] ASI_M_FLUSH_USER
	sta	%g0, [%o3 + %g2] ASI_M_FLUSH_USER
	sta	%g0, [%o3 + %g3] ASI_M_FLUSH_USER
	sta	%g0, [%o3 + %g4] ASI_M_FLUSH_USER
	bne	1b
	 sta	%g0, [%o3 + %g5] ASI_M_FLUSH_USER
	retl
	 nop

	/* Below our threshold, flush one page at a time. */
0:
	ld	[%o0 + AOFF_mm_context], %o0
	mov	SRMMU_CTX_REG, %g7
	lda	[%g7] ASI_M_MMUREGS, %o3
	sta	%o0, [%g7] ASI_M_MMUREGS
	add	%o2, -PAGE_SIZE, %o0
1:
	or	%o0, 0x400, %g7
	lda	[%g7] ASI_M_FLUSH_PROBE, %g7
	orcc	%g7, 0, %g0
	be,a	3f
	 mov	%o0, %o2
	add	%o4, %g5, %g7
2:
	sub	%o2, %g7, %o2
	sta	%g0, [%o2 + %g0] ASI_M_FLUSH_PAGE
	sta	%g0, [%o2 + %o4] ASI_M_FLUSH_PAGE
	sta	%g0, [%o2 + %o5] ASI_M_FLUSH_PAGE
	sta	%g0, [%o2 + %g1] ASI_M_FLUSH_PAGE
	sta	%g0, [%o2 + %g2] ASI_M_FLUSH_PAGE
	sta	%g0, [%o2 + %g3] ASI_M_FLUSH_PAGE
	andcc	%o2, 0xffc, %g0
	sta	%g0, [%o2 + %g4] ASI_M_FLUSH_PAGE
	bne	2b
	 sta	%g0, [%o2 + %g5] ASI_M_FLUSH_PAGE
3:
	cmp	%o2, %o1
	bne	1b
	 add	%o2, -PAGE_SIZE, %o0
	mov	SRMMU_FAULT_STATUS, %g5
	lda	[%g5] ASI_M_MMUREGS, %g0
	mov	SRMMU_CTX_REG, %g7
	sta	%o3, [%g7] ASI_M_MMUREGS
hypersparc_flush_cache_range_out:
	retl
	 nop

	/* HyperSparc requires a valid mapping where we are about to flush
	 * in order to check for a physical tag match during the flush.
	 */
	/* Verified, my ass... */
hypersparc_flush_cache_page:
	ld	[%o0 + VMA_VM_MM], %o0
	ld	[%o0 + AOFF_mm_context], %g2
#ifndef CONFIG_SMP
	cmp	%g2, -1
	be	hypersparc_flush_cache_page_out
#endif
	WINDOW_FLUSH(%g4, %g5)

	sethi	%hi(vac_line_size), %g1
	ld	[%g1 + %lo(vac_line_size)], %o4
	mov	SRMMU_CTX_REG, %o3
	andn	%o1, (PAGE_SIZE - 1), %o1
	lda	[%o3] ASI_M_MMUREGS, %o2
	sta	%g2, [%o3] ASI_M_MMUREGS
	or	%o1, 0x400, %o5
	lda	[%o5] ASI_M_FLUSH_PROBE, %g1
	orcc	%g0, %g1, %g0
	be	2f
	 add	%o4, %o4, %o5
	sub	%o1, -PAGE_SIZE, %o1
	add	%o4, %o5, %g1
	add	%o4, %g1, %g2
	add	%o4, %g2, %g3
	add	%o4, %g3, %g4
	add	%o4, %g4, %g5
	add	%o4, %g5, %g7

	/* BLAMMO! */
1:
	sub	%o1, %g7, %o1
	sta	%g0, [%o1 + %g0] ASI_M_FLUSH_PAGE
	sta	%g0, [%o1 + %o4] ASI_M_FLUSH_PAGE
	sta	%g0, [%o1 + %o5] ASI_M_FLUSH_PAGE
	sta	%g0, [%o1 + %g1] ASI_M_FLUSH_PAGE
	sta	%g0, [%o1 + %g2] ASI_M_FLUSH_PAGE
	sta	%g0, [%o1 + %g3] ASI_M_FLUSH_PAGE
	andcc	%o1, 0xffc, %g0
	sta	%g0, [%o1 + %g4] ASI_M_FLUSH_PAGE
	bne	1b
	 sta	%g0, [%o1 + %g5] ASI_M_FLUSH_PAGE
2:
	mov	SRMMU_FAULT_STATUS, %g7
	mov	SRMMU_CTX_REG, %g4
	lda	[%g7] ASI_M_MMUREGS, %g0
	sta	%o2, [%g4] ASI_M_MMUREGS
hypersparc_flush_cache_page_out:
	retl
	 nop

hypersparc_flush_sig_insns:
	flush	%o1
	retl
	 flush	%o1 + 4

	/* HyperSparc is copy-back. */
hypersparc_flush_page_to_ram:
	sethi	%hi(vac_line_size), %g1
	ld	[%g1 + %lo(vac_line_size)], %o4
	andn	%o0, (PAGE_SIZE - 1), %o0
	add	%o4, %o4, %o5
	or	%o0, 0x400, %g7
	lda	[%g7] ASI_M_FLUSH_PROBE, %g5
	add	%o4, %o5, %g1
	orcc	%g5, 0, %g0
	be	2f
	 add	%o4, %g1, %g2
	add	%o4, %g2, %g3
	sub	%o0, -PAGE_SIZE, %o0
	add	%o4, %g3, %g4
	add	%o4, %g4, %g5
	add	%o4, %g5, %g7

	/* BLAMMO! */
1:
	sub	%o0, %g7, %o0
	sta	%g0, [%o0 + %g0] ASI_M_FLUSH_PAGE
	sta	%g0, [%o0 + %o4] ASI_M_FLUSH_PAGE
	sta	%g0, [%o0 + %o5] ASI_M_FLUSH_PAGE
	sta	%g0, [%o0 + %g1] ASI_M_FLUSH_PAGE
	sta	%g0, [%o0 + %g2] ASI_M_FLUSH_PAGE
	sta	%g0, [%o0 + %g3] ASI_M_FLUSH_PAGE
	andcc	%o0, 0xffc, %g0
	sta	%g0, [%o0 + %g4] ASI_M_FLUSH_PAGE
	bne	1b
	 sta	%g0, [%o0 + %g5] ASI_M_FLUSH_PAGE
2:
	mov	SRMMU_FAULT_STATUS, %g1
	retl
	 lda	[%g1] ASI_M_MMUREGS, %g0

	/* HyperSparc is IO cache coherent. */
hypersparc_flush_page_for_dma:
	retl
	 nop

	/* It was noted that at boot time a TLB flush all in a delay slot
	 * can deliver an illegal instruction to the processor if the timing
	 * is just right...
	 */
hypersparc_flush_tlb_all:
	mov	0x400, %g1
	sta	%g0, [%g1] ASI_M_FLUSH_PROBE
	retl
	 nop

hypersparc_flush_tlb_mm:
	mov	SRMMU_CTX_REG, %g1
	ld	[%o0 + AOFF_mm_context], %o1
	lda	[%g1] ASI_M_MMUREGS, %g5
#ifndef CONFIG_SMP
	cmp	%o1, -1
	be	hypersparc_flush_tlb_mm_out
#endif
	 mov	0x300, %g2
	sta	%o1, [%g1] ASI_M_MMUREGS
	sta	%g0, [%g2] ASI_M_FLUSH_PROBE
hypersparc_flush_tlb_mm_out:
	retl
	 sta	%g5, [%g1] ASI_M_MMUREGS

hypersparc_flush_tlb_range:
	ld	[%o0 + VMA_VM_MM], %o0
	mov	SRMMU_CTX_REG, %g1
	ld	[%o0 + AOFF_mm_context], %o3
	lda	[%g1] ASI_M_MMUREGS, %g5
#ifndef CONFIG_SMP
	cmp	%o3, -1
	be	hypersparc_flush_tlb_range_out
#endif
	 sethi	%hi(~((1 << PGDIR_SHIFT) - 1)), %o4
	sta	%o3, [%g1] ASI_M_MMUREGS
	and	%o1, %o4, %o1
	add	%o1, 0x200, %o1
	sta	%g0, [%o1] ASI_M_FLUSH_PROBE
1:
	sub	%o1, %o4, %o1
	cmp	%o1, %o2
	blu,a	1b
	 sta	%g0, [%o1] ASI_M_FLUSH_PROBE
hypersparc_flush_tlb_range_out:
	retl
	 sta	%g5, [%g1] ASI_M_MMUREGS

hypersparc_flush_tlb_page:
	ld	[%o0 + VMA_VM_MM], %o0
	mov	SRMMU_CTX_REG, %g1
	ld	[%o0 + AOFF_mm_context], %o3
	andn	%o1, (PAGE_SIZE - 1), %o1
#ifndef CONFIG_SMP
	cmp	%o3, -1
	be	hypersparc_flush_tlb_page_out
#endif
	 lda	[%g1] ASI_M_MMUREGS, %g5
	sta	%o3, [%g1] ASI_M_MMUREGS
	sta	%g0, [%o1] ASI_M_FLUSH_PROBE
hypersparc_flush_tlb_page_out:
	retl
	 sta	%g5, [%g1] ASI_M_MMUREGS

	__INIT
	
	/* High speed page clear/copy. */
hypersparc_bzero_1page:
/* NOTE: This routine has to be shorter than 40insns --jj */
	clr	%g1
	mov	32, %g2
	mov	64, %g3
	mov	96, %g4
	mov	128, %g5
	mov	160, %g7
	mov	192, %o2
	mov	224, %o3
	mov	16, %o1
1:
	stda	%g0, [%o0 + %g0] ASI_M_BFILL
	stda	%g0, [%o0 + %g2] ASI_M_BFILL
	stda	%g0, [%o0 + %g3] ASI_M_BFILL
	stda	%g0, [%o0 + %g4] ASI_M_BFILL
	stda	%g0, [%o0 + %g5] ASI_M_BFILL
	stda	%g0, [%o0 + %g7] ASI_M_BFILL
	stda	%g0, [%o0 + %o2] ASI_M_BFILL
	stda	%g0, [%o0 + %o3] ASI_M_BFILL
	subcc	%o1, 1, %o1
	bne	1b
	 add	%o0, 256, %o0

	retl
	 nop

hypersparc_copy_1page:
/* NOTE: This routine has to be shorter than 70insns --jj */
	sub	%o1, %o0, %o2		! difference
	mov	16, %g1
1:
	sta	%o0, [%o0 + %o2] ASI_M_BCOPY
	add	%o0, 32, %o0
	sta	%o0, [%o0 + %o2] ASI_M_BCOPY
	add	%o0, 32, %o0
	sta	%o0, [%o0 + %o2] ASI_M_BCOPY
	add	%o0, 32, %o0
	sta	%o0, [%o0 + %o2] ASI_M_BCOPY
	add	%o0, 32, %o0
	sta	%o0, [%o0 + %o2] ASI_M_BCOPY
	add	%o0, 32, %o0
	sta	%o0, [%o0 + %o2] ASI_M_BCOPY
	add	%o0, 32, %o0
	sta	%o0, [%o0 + %o2] ASI_M_BCOPY
	add	%o0, 32, %o0
	sta	%o0, [%o0 + %o2] ASI_M_BCOPY
	subcc	%g1, 1, %g1
	bne	1b
	 add	%o0, 32, %o0

	retl
	 nop

	.globl	hypersparc_setup_blockops
hypersparc_setup_blockops:
	sethi	%hi(bzero_1page), %o0
	or	%o0, %lo(bzero_1page), %o0
	sethi	%hi(hypersparc_bzero_1page), %o1
	or	%o1, %lo(hypersparc_bzero_1page), %o1
	sethi	%hi(hypersparc_copy_1page), %o2
	or	%o2, %lo(hypersparc_copy_1page), %o2
	ld	[%o1], %o4
1:
	add	%o1, 4, %o1
	st	%o4, [%o0]
	add	%o0, 4, %o0
	cmp	%o1, %o2
	bne	1b
	 ld	[%o1], %o4
	sethi	%hi(__copy_1page), %o0
	or	%o0, %lo(__copy_1page), %o0
	sethi	%hi(hypersparc_setup_blockops), %o2
	or	%o2, %lo(hypersparc_setup_blockops), %o2
	ld	[%o1], %o4
1:
	add	%o1, 4, %o1
	st	%o4, [%o0]
	add	%o0, 4, %o0
	cmp	%o1, %o2
	bne	1b
	 ld	[%o1], %o4
	sta	%g0, [%g0] ASI_M_FLUSH_IWHOLE
	retl
	 nop
