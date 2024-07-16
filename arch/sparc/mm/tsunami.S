/* SPDX-License-Identifier: GPL-2.0 */
/*
 * tsunami.S: High speed MicroSparc-I mmu/cache operations.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/ptrace.h>
#include <asm/asm-offsets.h>
#include <asm/psr.h>
#include <asm/asi.h>
#include <asm/page.h>
#include <asm/pgtsrmmu.h>

	.text
	.align	4

	.globl	tsunami_flush_cache_all, tsunami_flush_cache_mm
	.globl	tsunami_flush_cache_range, tsunami_flush_cache_page
	.globl	tsunami_flush_page_to_ram, tsunami_flush_page_for_dma
	.globl	tsunami_flush_sig_insns
	.globl	tsunami_flush_tlb_all, tsunami_flush_tlb_mm
	.globl	tsunami_flush_tlb_range, tsunami_flush_tlb_page

	/* Sliiick... */
tsunami_flush_cache_page:
tsunami_flush_cache_range:
	ld	[%o0 + VMA_VM_MM], %o0
tsunami_flush_cache_mm:
	ld	[%o0 + AOFF_mm_context], %g2
	cmp	%g2, -1
	be	tsunami_flush_cache_out
tsunami_flush_cache_all:
	WINDOW_FLUSH(%g4, %g5)
tsunami_flush_page_for_dma:
	sta	%g0, [%g0] ASI_M_IC_FLCLEAR
	sta	%g0, [%g0] ASI_M_DC_FLCLEAR
tsunami_flush_cache_out:
tsunami_flush_page_to_ram:
	retl
	 nop

tsunami_flush_sig_insns:
	flush	%o1
	retl
	 flush	%o1 + 4

	/* More slick stuff... */
tsunami_flush_tlb_range:
	ld	[%o0 + VMA_VM_MM], %o0
tsunami_flush_tlb_mm:
	ld	[%o0 + AOFF_mm_context], %g2
	cmp	%g2, -1
	be	tsunami_flush_tlb_out
tsunami_flush_tlb_all:
	 mov	0x400, %o1
	sta	%g0, [%o1] ASI_M_FLUSH_PROBE
	nop
	nop
	nop
	nop
	nop
tsunami_flush_tlb_out:
	retl
	 nop

	/* This one can be done in a fine grained manner... */
tsunami_flush_tlb_page:
	ld	[%o0 + VMA_VM_MM], %o0
	mov	SRMMU_CTX_REG, %g1
	ld	[%o0 + AOFF_mm_context], %o3
	andn	%o1, (PAGE_SIZE - 1), %o1
	cmp	%o3, -1
	be	tsunami_flush_tlb_page_out
	 lda	[%g1] ASI_M_MMUREGS, %g5
	sta	%o3, [%g1] ASI_M_MMUREGS
	sta	%g0, [%o1] ASI_M_FLUSH_PROBE
	nop
	nop
	nop
	nop
	nop
tsunami_flush_tlb_page_out:
	retl
	 sta	%g5, [%g1] ASI_M_MMUREGS

#define MIRROR_BLOCK(dst, src, offset, t0, t1, t2, t3) \
	ldd	[src + offset + 0x18], t0; \
	std	t0, [dst + offset + 0x18]; \
	ldd	[src + offset + 0x10], t2; \
	std	t2, [dst + offset + 0x10]; \
	ldd	[src + offset + 0x08], t0; \
	std	t0, [dst + offset + 0x08]; \
	ldd	[src + offset + 0x00], t2; \
	std	t2, [dst + offset + 0x00];

tsunami_copy_1page:
/* NOTE: This routine has to be shorter than 70insns --jj */
	or	%g0, (PAGE_SIZE >> 8), %g1
1:
	MIRROR_BLOCK(%o0, %o1, 0x00, %o2, %o3, %o4, %o5)
	MIRROR_BLOCK(%o0, %o1, 0x20, %o2, %o3, %o4, %o5)
	MIRROR_BLOCK(%o0, %o1, 0x40, %o2, %o3, %o4, %o5)
	MIRROR_BLOCK(%o0, %o1, 0x60, %o2, %o3, %o4, %o5)
	MIRROR_BLOCK(%o0, %o1, 0x80, %o2, %o3, %o4, %o5)
	MIRROR_BLOCK(%o0, %o1, 0xa0, %o2, %o3, %o4, %o5)
	MIRROR_BLOCK(%o0, %o1, 0xc0, %o2, %o3, %o4, %o5)
	MIRROR_BLOCK(%o0, %o1, 0xe0, %o2, %o3, %o4, %o5)
	subcc	%g1, 1, %g1
	add	%o0, 0x100, %o0
	bne	1b
	 add	%o1, 0x100, %o1

	.globl	tsunami_setup_blockops
tsunami_setup_blockops:
	sethi	%hi(__copy_1page), %o0
	or	%o0, %lo(__copy_1page), %o0
	sethi	%hi(tsunami_copy_1page), %o1
	or	%o1, %lo(tsunami_copy_1page), %o1
	sethi	%hi(tsunami_setup_blockops), %o2
	or	%o2, %lo(tsunami_setup_blockops), %o2
	ld	[%o1], %o4
1:	add	%o1, 4, %o1
	st	%o4, [%o0]
	add	%o0, 4, %o0
	cmp	%o1, %o2
	bne	1b
	ld	[%o1], %o4
	sta	%g0, [%g0] ASI_M_IC_FLCLEAR
	sta	%g0, [%g0] ASI_M_DC_FLCLEAR
	retl
	 nop
