/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 * Copyright (C) IBM Corporation, 2012
 *
 * Author: Anton Blanchard <anton@au.ibm.com>
 */
#include <asm/ppc_asm.h>
#include <asm/vdso.h>

	.text
/*
 * Exact prototype of getcpu
 *
 * int __kernel_getcpu(unsigned *cpu, unsigned *node);
 *
 */
V_FUNCTION_BEGIN(__kernel_getcpu)
  .cfi_startproc
	mfspr	r5,SPRN_SPRG_VDSO_READ
	cmpwi	cr0,r3,0
	cmpwi	cr1,r4,0
	clrlwi  r6,r5,16
	rlwinm  r7,r5,16,31-15,31-0
	beq	cr0,1f
	stw	r6,0(r3)
1:	beq	cr1,2f
	stw	r7,0(r4)
2:	crclr	cr0*4+so
	li	r3,0			/* always success */
	blr
  .cfi_endproc
V_FUNCTION_END(__kernel_getcpu)
