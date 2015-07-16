/*
 * hibernate_asm.S:  Hibernaton support specific for sparc64.
 *
 * Copyright (C) 2013 Kirill V Tkhai (tkhai@yandex.ru)
 */

#include <linux/linkage.h>

#include <asm/asm-offsets.h>
#include <asm/cpudata.h>
#include <asm/page.h>

ENTRY(swsusp_arch_suspend)
	save	%sp, -128, %sp
	save	%sp, -128, %sp
	flushw

	setuw	saved_context, %g3

	/* Save window regs */
	rdpr	%cwp, %g2
	stx	%g2, [%g3 + SC_REG_CWP]
	rdpr	%wstate, %g2
	stx	%g2, [%g3 + SC_REG_WSTATE]
	stx	%fp, [%g3 + SC_REG_FP]

	/* Save state regs */
	rdpr	%tick, %g2
	stx	%g2, [%g3 + SC_REG_TICK]
	rdpr	%pstate, %g2
	stx	%g2, [%g3 + SC_REG_PSTATE]

	/* Save global regs */
	stx	%g4, [%g3 + SC_REG_G4]
	stx	%g5, [%g3 + SC_REG_G5]
	stx	%g6, [%g3 + SC_REG_G6]

	call	swsusp_save
	 nop

	mov	%o0, %i0
	restore

	mov	%o0, %i0
	ret
	 restore

ENTRY(swsusp_arch_resume)
	/* Write restore_pblist to %l0 */
	sethi	%hi(restore_pblist), %l0
	ldx	[%l0 + %lo(restore_pblist)], %l0

	call	__flush_tlb_all
	 nop

	/* Write PAGE_OFFSET to %g7 */
	sethi	%hi(PAGE_OFFSET), %g7
	ldx	[%g7 + %lo(PAGE_OFFSET)], %g7

	setuw	(PAGE_SIZE-8), %g3

	/* Use MMU Bypass */
	rd	%asi, %g1
	wr	%g0, ASI_PHYS_USE_EC, %asi

	ba	fill_itlb
	 nop

pbe_loop:
	cmp	%l0, %g0
	be	restore_ctx
	 sub	%l0, %g7, %l0

	ldxa	[%l0    ] %asi, %l1 /* address */
	ldxa	[%l0 + 8] %asi, %l2 /* orig_address */

	/* phys addr */
	sub	%l1, %g7, %l1
	sub	%l2, %g7, %l2

	mov	%g3, %l3 /* PAGE_SIZE-8 */
copy_loop:
	ldxa	[%l1 + %l3] ASI_PHYS_USE_EC, %g2
	stxa	%g2, [%l2 + %l3] ASI_PHYS_USE_EC
	cmp	%l3, %g0
	bne	copy_loop
	 sub	%l3, 8, %l3

	/* next pbe */
	ba	pbe_loop
	 ldxa	[%l0 + 16] %asi, %l0

restore_ctx:
	setuw	saved_context, %g3

	/* Restore window regs */
	wrpr    %g0, 0, %canrestore
	wrpr    %g0, 0, %otherwin
	wrpr	%g0, 6, %cansave
	wrpr    %g0, 0, %cleanwin

	ldxa	[%g3 + SC_REG_CWP] %asi, %g2
	wrpr	%g2, %cwp
	ldxa	[%g3 + SC_REG_WSTATE] %asi, %g2
	wrpr	%g2, %wstate
	ldxa	[%g3 + SC_REG_FP] %asi, %fp

	/* Restore state regs */
	ldxa	[%g3 + SC_REG_PSTATE] %asi, %g2
	wrpr	%g2, %pstate
	ldxa	[%g3 + SC_REG_TICK] %asi, %g2
	wrpr	%g2, %tick

	/* Restore global regs */
	ldxa	[%g3 + SC_REG_G4] %asi, %g4
	ldxa	[%g3 + SC_REG_G5] %asi, %g5
	ldxa	[%g3 + SC_REG_G6] %asi, %g6

	wr	%g1, %g0, %asi

	restore
	restore

	wrpr	%g0, 14, %pil

	retl
	 mov	%g0, %o0

fill_itlb:
	ba	pbe_loop
	 wrpr	%g0, 15, %pil
