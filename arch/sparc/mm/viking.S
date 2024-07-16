/* SPDX-License-Identifier: GPL-2.0 */
/*
 * viking.S: High speed Viking cache/mmu operations
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 1997,1998,1999  Jakub Jelinek  (jj@ultra.linux.cz)
 * Copyright (C) 1999  Pavel Semerad  (semerad@ss1000.ms.mff.cuni.cz)
 */

#include <asm/ptrace.h>
#include <asm/psr.h>
#include <asm/asm-offsets.h>
#include <asm/asi.h>
#include <asm/mxcc.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/pgtsrmmu.h>
#include <asm/viking.h>

#ifdef CONFIG_SMP
	.data
	.align	4
sun4dsmp_flush_tlb_spin:
	.word	0
#endif

	.text
	.align	4

	.globl	viking_flush_cache_all, viking_flush_cache_mm
	.globl	viking_flush_cache_range, viking_flush_cache_page
	.globl	viking_flush_page, viking_mxcc_flush_page
	.globl	viking_flush_page_for_dma, viking_flush_page_to_ram
	.globl	viking_flush_sig_insns
	.globl	viking_flush_tlb_all, viking_flush_tlb_mm
	.globl	viking_flush_tlb_range, viking_flush_tlb_page

viking_flush_page:
	sethi	%hi(PAGE_OFFSET), %g2
	sub	%o0, %g2, %g3
	srl	%g3, 12, %g1		! ppage >> 12

	clr	%o1			! set counter, 0 - 127
	sethi	%hi(PAGE_OFFSET + PAGE_SIZE - 0x80000000), %o3
	sethi	%hi(0x80000000), %o4
	sethi	%hi(VIKING_PTAG_VALID), %o5
	sethi	%hi(2*PAGE_SIZE), %o0
	sethi	%hi(PAGE_SIZE), %g7
	clr	%o2			! block counter, 0 - 3
5:
	sll	%o1, 5, %g4
	or	%g4, %o4, %g4		! 0x80000000 | (set << 5)

	sll	%o2, 26, %g5		! block << 26
6:
	or	%g5, %g4, %g5
	ldda	[%g5] ASI_M_DATAC_TAG, %g2
	cmp	%g3, %g1		! ptag == ppage?
	bne	7f
	 inc	%o2

	andcc	%g2, %o5, %g0		! ptag VALID?
	be	7f
	 add	%g4, %o3, %g2		! (PAGE_OFFSET + PAGE_SIZE) | (set << 5)
	ld	[%g2], %g3
	ld	[%g2 + %g7], %g3
	add	%g2, %o0, %g2
	ld	[%g2], %g3
	ld	[%g2 + %g7], %g3
	add	%g2, %o0, %g2
	ld	[%g2], %g3
	ld	[%g2 + %g7], %g3
	add	%g2, %o0, %g2
	ld	[%g2], %g3
	b	8f
	 ld	[%g2 + %g7], %g3

7:
	cmp	%o2, 3
	ble	6b
	 sll	%o2, 26, %g5			! block << 26

8:	inc	%o1
	cmp	%o1, 0x7f
	ble	5b
	 clr	%o2

9:	retl
	 nop

viking_mxcc_flush_page:
	sethi	%hi(PAGE_OFFSET), %g2
	sub	%o0, %g2, %g3
	sub	%g3, -PAGE_SIZE, %g3		! ppage + PAGE_SIZE
	sethi	%hi(MXCC_SRCSTREAM), %o3	! assume %hi(MXCC_SRCSTREAM) == %hi(MXCC_DESTSTREAM)
	mov	0x10, %g2			! set cacheable bit
	or	%o3, %lo(MXCC_SRCSTREAM), %o2
	or	%o3, %lo(MXCC_DESSTREAM), %o3
	sub	%g3, MXCC_STREAM_SIZE, %g3
6:
	stda	%g2, [%o2] ASI_M_MXCC
	stda	%g2, [%o3] ASI_M_MXCC
	andncc	%g3, PAGE_MASK, %g0
	bne	6b
	 sub	%g3, MXCC_STREAM_SIZE, %g3

9:	retl
	 nop

viking_flush_cache_page:
viking_flush_cache_range:
#ifndef CONFIG_SMP
	ld	[%o0 + VMA_VM_MM], %o0
#endif
viking_flush_cache_mm:
#ifndef CONFIG_SMP
	ld	[%o0 + AOFF_mm_context], %g1
	cmp	%g1, -1
	bne	viking_flush_cache_all
	 nop
	b,a	viking_flush_cache_out
#endif
viking_flush_cache_all:
	WINDOW_FLUSH(%g4, %g5)
viking_flush_cache_out:
	retl
	 nop

viking_flush_tlb_all:
	mov	0x400, %g1
	retl
	 sta	%g0, [%g1] ASI_M_FLUSH_PROBE

viking_flush_tlb_mm:
	mov	SRMMU_CTX_REG, %g1
	ld	[%o0 + AOFF_mm_context], %o1
	lda	[%g1] ASI_M_MMUREGS, %g5
#ifndef CONFIG_SMP
	cmp	%o1, -1
	be	1f
#endif
	mov	0x300, %g2
	sta	%o1, [%g1] ASI_M_MMUREGS
	sta	%g0, [%g2] ASI_M_FLUSH_PROBE
	retl
	 sta	%g5, [%g1] ASI_M_MMUREGS
#ifndef CONFIG_SMP
1:	retl
	 nop
#endif

viking_flush_tlb_range:
	ld	[%o0 + VMA_VM_MM], %o0
	mov	SRMMU_CTX_REG, %g1
	ld	[%o0 + AOFF_mm_context], %o3
	lda	[%g1] ASI_M_MMUREGS, %g5
#ifndef CONFIG_SMP
	cmp	%o3, -1
	be	2f
#endif
	sethi	%hi(~((1 << PGDIR_SHIFT) - 1)), %o4
	sta	%o3, [%g1] ASI_M_MMUREGS
	and	%o1, %o4, %o1
	add	%o1, 0x200, %o1
	sta	%g0, [%o1] ASI_M_FLUSH_PROBE
1:	sub	%o1, %o4, %o1
	cmp	%o1, %o2
	blu,a	1b
	 sta	%g0, [%o1] ASI_M_FLUSH_PROBE
	retl
	 sta	%g5, [%g1] ASI_M_MMUREGS
#ifndef CONFIG_SMP
2:	retl
	 nop
#endif

viking_flush_tlb_page:
	ld	[%o0 + VMA_VM_MM], %o0
	mov	SRMMU_CTX_REG, %g1
	ld	[%o0 + AOFF_mm_context], %o3
	lda	[%g1] ASI_M_MMUREGS, %g5
#ifndef CONFIG_SMP
	cmp	%o3, -1
	be	1f
#endif
	and	%o1, PAGE_MASK, %o1
	sta	%o3, [%g1] ASI_M_MMUREGS
	sta	%g0, [%o1] ASI_M_FLUSH_PROBE
	retl
	 sta	%g5, [%g1] ASI_M_MMUREGS
#ifndef CONFIG_SMP
1:	retl
	 nop
#endif

viking_flush_page_to_ram:
viking_flush_page_for_dma:
viking_flush_sig_insns:
	retl
	 nop

#ifdef CONFIG_SMP
	.globl	sun4dsmp_flush_tlb_all, sun4dsmp_flush_tlb_mm
	.globl	sun4dsmp_flush_tlb_range, sun4dsmp_flush_tlb_page
sun4dsmp_flush_tlb_all:
	sethi	%hi(sun4dsmp_flush_tlb_spin), %g3
1:	ldstub	[%g3 + %lo(sun4dsmp_flush_tlb_spin)], %g5
	tst	%g5
	bne	2f
	 mov	0x400, %g1
	sta	%g0, [%g1] ASI_M_FLUSH_PROBE
	retl
	 stb	%g0, [%g3 + %lo(sun4dsmp_flush_tlb_spin)]
2:	tst	%g5
	bne,a	2b
	 ldub	[%g3 + %lo(sun4dsmp_flush_tlb_spin)], %g5
	b,a	1b

sun4dsmp_flush_tlb_mm:
	sethi	%hi(sun4dsmp_flush_tlb_spin), %g3
1:	ldstub	[%g3 + %lo(sun4dsmp_flush_tlb_spin)], %g5
	tst	%g5
	bne	2f
	 mov	SRMMU_CTX_REG, %g1
	ld	[%o0 + AOFF_mm_context], %o1
	lda	[%g1] ASI_M_MMUREGS, %g5
	mov	0x300, %g2
	sta	%o1, [%g1] ASI_M_MMUREGS
	sta	%g0, [%g2] ASI_M_FLUSH_PROBE
	sta	%g5, [%g1] ASI_M_MMUREGS
	retl
	 stb	%g0, [%g3 + %lo(sun4dsmp_flush_tlb_spin)]
2:	tst	%g5
	bne,a	2b
	 ldub	[%g3 + %lo(sun4dsmp_flush_tlb_spin)], %g5
	b,a	1b

sun4dsmp_flush_tlb_range:
	sethi	%hi(sun4dsmp_flush_tlb_spin), %g3
1:	ldstub	[%g3 + %lo(sun4dsmp_flush_tlb_spin)], %g5
	tst	%g5
	bne	3f
	 mov	SRMMU_CTX_REG, %g1
	ld	[%o0 + VMA_VM_MM], %o0
	ld	[%o0 + AOFF_mm_context], %o3
	lda	[%g1] ASI_M_MMUREGS, %g5
	sethi	%hi(~((1 << PGDIR_SHIFT) - 1)), %o4
	sta	%o3, [%g1] ASI_M_MMUREGS
	and	%o1, %o4, %o1
	add	%o1, 0x200, %o1
	sta	%g0, [%o1] ASI_M_FLUSH_PROBE
2:	sub	%o1, %o4, %o1
	cmp	%o1, %o2
	blu,a	2b
	 sta	%g0, [%o1] ASI_M_FLUSH_PROBE
	sta	%g5, [%g1] ASI_M_MMUREGS
	retl
	 stb	%g0, [%g3 + %lo(sun4dsmp_flush_tlb_spin)]
3:	tst	%g5
	bne,a	3b
	 ldub	[%g3 + %lo(sun4dsmp_flush_tlb_spin)], %g5
	b,a	1b

sun4dsmp_flush_tlb_page:
	sethi	%hi(sun4dsmp_flush_tlb_spin), %g3
1:	ldstub	[%g3 + %lo(sun4dsmp_flush_tlb_spin)], %g5
	tst	%g5
	bne	2f
	 mov	SRMMU_CTX_REG, %g1
	ld	[%o0 + VMA_VM_MM], %o0
	ld	[%o0 + AOFF_mm_context], %o3
	lda	[%g1] ASI_M_MMUREGS, %g5
	and	%o1, PAGE_MASK, %o1
	sta	%o3, [%g1] ASI_M_MMUREGS
	sta	%g0, [%o1] ASI_M_FLUSH_PROBE
	sta	%g5, [%g1] ASI_M_MMUREGS
	retl
	 stb	%g0, [%g3 + %lo(sun4dsmp_flush_tlb_spin)]
2:	tst	%g5
	bne,a	2b
	 ldub	[%g3 + %lo(sun4dsmp_flush_tlb_spin)], %g5
	b,a	1b
	 nop
#endif
