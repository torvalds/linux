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
#if defined(CONFIG_PPC64)
V_FUNCTION_BEGIN(__kernel_getcpu)
  .cfi_startproc
	mfspr	r5,SPRN_SPRG_VDSO_READ
	PPC_LCMPI	cr0,r3,0
	PPC_LCMPI	cr1,r4,0
	clrlwi  r6,r5,16
	rlwinm  r7,r5,16,31-15,31-0
	beq	cr0,1f
	stw	r6,0(r3)
1:	crclr	cr0*4+so
	li	r3,0			/* always success */
	beqlr	cr1
	stw	r7,0(r4)
	blr
  .cfi_endproc
V_FUNCTION_END(__kernel_getcpu)
#elif !defined(CONFIG_SMP)
V_FUNCTION_BEGIN(__kernel_getcpu)
  .cfi_startproc
	cmpwi	cr0, r3, 0
	cmpwi	cr1, r4, 0
	li	r5, 0
	beq	cr0, 1f
	stw	r5, 0(r3)
1:	li	r3, 0			/* always success */
	crclr	cr0*4+so
	beqlr	cr1
	stw	r5, 0(r4)
	blr
  .cfi_endproc
V_FUNCTION_END(__kernel_getcpu)
#endif
