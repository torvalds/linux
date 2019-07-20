/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 */

#include <linux/linkage.h>
#include <asm/vfpmacros.h>

	.text
	.pushsection	.hyp.text, "ax"

/* void __vfp_save_state(struct vfp_hard_struct *vfp); */
ENTRY(__vfp_save_state)
	push	{r4, r5}
	VFPFMRX	r1, FPEXC

	@ Make sure *really* VFP is enabled so we can touch the registers.
	orr	r5, r1, #FPEXC_EN
	tst	r5, #FPEXC_EX		@ Check for VFP Subarchitecture
	bic	r5, r5, #FPEXC_EX	@ FPEXC_EX disable
	VFPFMXR	FPEXC, r5
	isb

	VFPFMRX	r2, FPSCR
	beq	1f

	@ If FPEXC_EX is 0, then FPINST/FPINST2 reads are upredictable, so
	@ we only need to save them if FPEXC_EX is set.
	VFPFMRX r3, FPINST
	tst	r5, #FPEXC_FP2V
	VFPFMRX r4, FPINST2, ne		@ vmrsne
1:
	VFPFSTMIA r0, r5		@ Save VFP registers
	stm	r0, {r1-r4}		@ Save FPEXC, FPSCR, FPINST, FPINST2
	pop	{r4, r5}
	bx	lr
ENDPROC(__vfp_save_state)

/* void __vfp_restore_state(struct vfp_hard_struct *vfp);
 * Assume FPEXC_EN is on and FPEXC_EX is off */
ENTRY(__vfp_restore_state)
	VFPFLDMIA r0, r1		@ Load VFP registers
	ldm	r0, {r0-r3}		@ Load FPEXC, FPSCR, FPINST, FPINST2

	VFPFMXR FPSCR, r1
	tst	r0, #FPEXC_EX		@ Check for VFP Subarchitecture
	beq	1f
	VFPFMXR FPINST, r2
	tst	r0, #FPEXC_FP2V
	VFPFMXR FPINST2, r3, ne
1:
	VFPFMXR FPEXC, r0		@ FPEXC	(last, in case !EN)
	bx	lr
ENDPROC(__vfp_restore_state)

	.popsection
