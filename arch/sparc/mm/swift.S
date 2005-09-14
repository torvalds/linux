/* $Id: swift.S,v 1.9 2002/01/08 11:11:59 davem Exp $
 * swift.S: MicroSparc-II mmu/cache operations.
 *
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 */

#include <linux/config.h>
#include <asm/psr.h>
#include <asm/asi.h>
#include <asm/page.h>
#include <asm/pgtsrmmu.h>
#include <asm/asm-offsets.h>

	.text
	.align	4

#if 1	/* XXX screw this, I can't get the VAC flushes working
	 * XXX reliably... -DaveM
	 */
	.globl	swift_flush_cache_all, swift_flush_cache_mm
	.globl	swift_flush_cache_range, swift_flush_cache_page
	.globl	swift_flush_page_for_dma
	.globl	swift_flush_page_to_ram

swift_flush_cache_all:
swift_flush_cache_mm:
swift_flush_cache_range:
swift_flush_cache_page:
swift_flush_page_for_dma:
swift_flush_page_to_ram:
	sethi	%hi(0x2000), %o0
1:	subcc	%o0, 0x10, %o0
	add	%o0, %o0, %o1
	sta	%g0, [%o0] ASI_M_DATAC_TAG
	bne	1b
	 sta	%g0, [%o1] ASI_M_TXTC_TAG
	retl
	 nop
#else

	.globl	swift_flush_cache_all
swift_flush_cache_all:
	WINDOW_FLUSH(%g4, %g5)

	/* Just clear out all the tags. */
	sethi	%hi(16 * 1024), %o0
1:	subcc	%o0, 16, %o0
	sta	%g0, [%o0] ASI_M_TXTC_TAG
	bne	1b
	 sta	%g0, [%o0] ASI_M_DATAC_TAG
	retl
	 nop

	.globl	swift_flush_cache_mm
swift_flush_cache_mm:
	ld	[%o0 + AOFF_mm_context], %g2
	cmp	%g2, -1
	be	swift_flush_cache_mm_out
	WINDOW_FLUSH(%g4, %g5)
	rd	%psr, %g1
	andn	%g1, PSR_ET, %g3
	wr	%g3, 0x0, %psr
	nop
	nop
	mov	SRMMU_CTX_REG, %g7
	lda	[%g7] ASI_M_MMUREGS, %g5
	sta	%g2, [%g7] ASI_M_MMUREGS

#if 1
	sethi	%hi(0x2000), %o0
1:	subcc	%o0, 0x10, %o0
	sta	%g0, [%o0] ASI_M_FLUSH_CTX
	bne	1b
	 nop
#else
	clr	%o0
	or	%g0, 2048, %g7
	or	%g0, 2048, %o1
	add	%o1, 2048, %o2
	add	%o2, 2048, %o3
	mov	16, %o4
	add	%o4, 2048, %o5
	add	%o5, 2048, %g2
	add	%g2, 2048, %g3
1:	sta	%g0, [%o0      ] ASI_M_FLUSH_CTX
	sta	%g0, [%o0 + %o1] ASI_M_FLUSH_CTX
	sta	%g0, [%o0 + %o2] ASI_M_FLUSH_CTX
	sta	%g0, [%o0 + %o3] ASI_M_FLUSH_CTX
	sta	%g0, [%o0 + %o4] ASI_M_FLUSH_CTX
	sta	%g0, [%o0 + %o5] ASI_M_FLUSH_CTX
	sta	%g0, [%o0 + %g2] ASI_M_FLUSH_CTX
	sta	%g0, [%o0 + %g3] ASI_M_FLUSH_CTX
	subcc	%g7, 32, %g7
	bne	1b
	 add	%o0, 32, %o0
#endif

	mov	SRMMU_CTX_REG, %g7
	sta	%g5, [%g7] ASI_M_MMUREGS
	wr	%g1, 0x0, %psr
	nop
	nop
swift_flush_cache_mm_out:
	retl
	 nop

	.globl	swift_flush_cache_range
swift_flush_cache_range:
	ld	[%o0 + 0x0], %o0		/* XXX vma->vm_mm, GROSS XXX */
	sub	%o2, %o1, %o2
	sethi	%hi(4096), %o3
	cmp	%o2, %o3
	bgu	swift_flush_cache_mm
	 nop
	b	70f
	 nop

	.globl	swift_flush_cache_page
swift_flush_cache_page:
	ld	[%o0 + 0x0], %o0		/* XXX vma->vm_mm, GROSS XXX */
70:
	ld	[%o0 + AOFF_mm_context], %g2
	cmp	%g2, -1
	be	swift_flush_cache_page_out
	WINDOW_FLUSH(%g4, %g5)
	rd	%psr, %g1
	andn	%g1, PSR_ET, %g3
	wr	%g3, 0x0, %psr
	nop
	nop
	mov	SRMMU_CTX_REG, %g7
	lda	[%g7] ASI_M_MMUREGS, %g5
	sta	%g2, [%g7] ASI_M_MMUREGS

	andn	%o1, (PAGE_SIZE - 1), %o1
#if 1
	sethi	%hi(0x1000), %o0
1:	subcc	%o0, 0x10, %o0
	sta	%g0, [%o1 + %o0] ASI_M_FLUSH_PAGE
	bne	1b
	 nop
#else
	or	%g0, 512, %g7
	or	%g0, 512, %o0
	add	%o0, 512, %o2
	add	%o2, 512, %o3
	add	%o3, 512, %o4
	add	%o4, 512, %o5
	add	%o5, 512, %g3
	add	%g3, 512, %g4
1:	sta	%g0, [%o1      ] ASI_M_FLUSH_PAGE
	sta	%g0, [%o1 + %o0] ASI_M_FLUSH_PAGE
	sta	%g0, [%o1 + %o2] ASI_M_FLUSH_PAGE
	sta	%g0, [%o1 + %o3] ASI_M_FLUSH_PAGE
	sta	%g0, [%o1 + %o4] ASI_M_FLUSH_PAGE
	sta	%g0, [%o1 + %o5] ASI_M_FLUSH_PAGE
	sta	%g0, [%o1 + %g3] ASI_M_FLUSH_PAGE
	sta	%g0, [%o1 + %g4] ASI_M_FLUSH_PAGE
	subcc	%g7, 16, %g7
	bne	1b
	 add	%o1, 16, %o1
#endif

	mov	SRMMU_CTX_REG, %g7
	sta	%g5, [%g7] ASI_M_MMUREGS
	wr	%g1, 0x0, %psr
	nop
	nop
swift_flush_cache_page_out:
	retl
	 nop

	/* Swift is write-thru, however it is not
	 * I/O nor TLB-walk coherent.  Also it has
	 * caches which are virtually indexed and tagged.
	 */
	.globl	swift_flush_page_for_dma
	.globl	swift_flush_page_to_ram
swift_flush_page_for_dma:
swift_flush_page_to_ram:
	andn	%o0, (PAGE_SIZE - 1), %o1
#if 1
	sethi	%hi(0x1000), %o0
1:	subcc	%o0, 0x10, %o0
	sta	%g0, [%o1 + %o0] ASI_M_FLUSH_PAGE
	bne	1b
	 nop
#else
	or	%g0, 512, %g7
	or	%g0, 512, %o0
	add	%o0, 512, %o2
	add	%o2, 512, %o3
	add	%o3, 512, %o4
	add	%o4, 512, %o5
	add	%o5, 512, %g3
	add	%g3, 512, %g4
1:	sta	%g0, [%o1      ] ASI_M_FLUSH_PAGE
	sta	%g0, [%o1 + %o0] ASI_M_FLUSH_PAGE
	sta	%g0, [%o1 + %o2] ASI_M_FLUSH_PAGE
	sta	%g0, [%o1 + %o3] ASI_M_FLUSH_PAGE
	sta	%g0, [%o1 + %o4] ASI_M_FLUSH_PAGE
	sta	%g0, [%o1 + %o5] ASI_M_FLUSH_PAGE
	sta	%g0, [%o1 + %g3] ASI_M_FLUSH_PAGE
	sta	%g0, [%o1 + %g4] ASI_M_FLUSH_PAGE
	subcc	%g7, 16, %g7
	bne	1b
	 add	%o1, 16, %o1
#endif
	retl
	 nop
#endif

	.globl	swift_flush_sig_insns
swift_flush_sig_insns:
	flush	%o1
	retl
	 flush	%o1 + 4

	.globl	swift_flush_tlb_mm
	.globl	swift_flush_tlb_range
	.globl	swift_flush_tlb_all
swift_flush_tlb_range:
	ld	[%o0 + 0x00], %o0	/* XXX vma->vm_mm GROSS XXX */
swift_flush_tlb_mm:
	ld	[%o0 + AOFF_mm_context], %g2
	cmp	%g2, -1
	be	swift_flush_tlb_all_out
swift_flush_tlb_all:
	mov	0x400, %o1
	sta	%g0, [%o1] ASI_M_FLUSH_PROBE
swift_flush_tlb_all_out:
	retl
	 nop

	.globl	swift_flush_tlb_page
swift_flush_tlb_page:
	ld	[%o0 + 0x00], %o0	/* XXX vma->vm_mm GROSS XXX */
	mov	SRMMU_CTX_REG, %g1
	ld	[%o0 + AOFF_mm_context], %o3
	andn	%o1, (PAGE_SIZE - 1), %o1
	cmp	%o3, -1
	be	swift_flush_tlb_page_out
	 nop
#if 1
	mov	0x400, %o1
	sta	%g0, [%o1] ASI_M_FLUSH_PROBE	
#else
	lda	[%g1] ASI_M_MMUREGS, %g5
	sta	%o3, [%g1] ASI_M_MMUREGS
	sta	%g0, [%o1] ASI_M_FLUSH_PAGE	/* rem. virt. cache. prot. */
	sta	%g0, [%o1] ASI_M_FLUSH_PROBE
	sta	%g5, [%g1] ASI_M_MMUREGS
#endif
swift_flush_tlb_page_out:
	retl
	 nop
