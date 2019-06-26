/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Floating-point, VMX/Altivec and VSX loads and stores
 * for use in instruction emulation.
 *
 * Copyright 2010 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 */

#include <asm/processor.h>
#include <asm/ppc_asm.h>
#include <asm/ppc-opcode.h>
#include <asm/reg.h>
#include <asm/asm-offsets.h>
#include <asm/asm-compat.h>
#include <linux/errno.h>

#ifdef CONFIG_PPC_FPU

#define STKFRM	(PPC_MIN_STKFRM + 16)

/* Get the contents of frN into *p; N is in r3 and p is in r4. */
_GLOBAL(get_fpr)
	mflr	r0
	mfmsr	r6
	ori	r7, r6, MSR_FP
	MTMSRD(r7)
	isync
	rlwinm	r3,r3,3,0xf8
	bcl	20,31,1f
reg = 0
	.rept	32
	stfd	reg, 0(r4)
	b	2f
reg = reg + 1
	.endr
1:	mflr	r5
	add	r5,r3,r5
	mtctr	r5
	mtlr	r0
	bctr
2:	MTMSRD(r6)
	isync
	blr

/* Put the contents of *p into frN; N is in r3 and p is in r4. */
_GLOBAL(put_fpr)
	mflr	r0
	mfmsr	r6
	ori	r7, r6, MSR_FP
	MTMSRD(r7)
	isync
	rlwinm	r3,r3,3,0xf8
	bcl	20,31,1f
reg = 0
	.rept	32
	lfd	reg, 0(r4)
	b	2f
reg = reg + 1
	.endr
1:	mflr	r5
	add	r5,r3,r5
	mtctr	r5
	mtlr	r0
	bctr
2:	MTMSRD(r6)
	isync
	blr

#ifdef CONFIG_ALTIVEC
/* Get the contents of vrN into *p; N is in r3 and p is in r4. */
_GLOBAL(get_vr)
	mflr	r0
	mfmsr	r6
	oris	r7, r6, MSR_VEC@h
	MTMSRD(r7)
	isync
	rlwinm	r3,r3,3,0xf8
	bcl	20,31,1f
reg = 0
	.rept	32
	stvx	reg, 0, r4
	b	2f
reg = reg + 1
	.endr
1:	mflr	r5
	add	r5,r3,r5
	mtctr	r5
	mtlr	r0
	bctr
2:	MTMSRD(r6)
	isync
	blr

/* Put the contents of *p into vrN; N is in r3 and p is in r4. */
_GLOBAL(put_vr)
	mflr	r0
	mfmsr	r6
	oris	r7, r6, MSR_VEC@h
	MTMSRD(r7)
	isync
	rlwinm	r3,r3,3,0xf8
	bcl	20,31,1f
reg = 0
	.rept	32
	lvx	reg, 0, r4
	b	2f
reg = reg + 1
	.endr
1:	mflr	r5
	add	r5,r3,r5
	mtctr	r5
	mtlr	r0
	bctr
2:	MTMSRD(r6)
	isync
	blr
#endif /* CONFIG_ALTIVEC */

#ifdef CONFIG_VSX
/* Get the contents of vsN into vs0; N is in r3. */
_GLOBAL(get_vsr)
	mflr	r0
	rlwinm	r3,r3,3,0x1f8
	bcl	20,31,1f
	blr			/* vs0 is already in vs0 */
	nop
reg = 1
	.rept	63
	XXLOR(0,reg,reg)
	blr
reg = reg + 1
	.endr
1:	mflr	r5
	add	r5,r3,r5
	mtctr	r5
	mtlr	r0
	bctr

/* Put the contents of vs0 into vsN; N is in r3. */
_GLOBAL(put_vsr)
	mflr	r0
	rlwinm	r3,r3,3,0x1f8
	bcl	20,31,1f
	blr			/* v0 is already in v0 */
	nop
reg = 1
	.rept	63
	XXLOR(reg,0,0)
	blr
reg = reg + 1
	.endr
1:	mflr	r5
	add	r5,r3,r5
	mtctr	r5
	mtlr	r0
	bctr

/* Load VSX reg N from vector doubleword *p.  N is in r3, p in r4. */
_GLOBAL(load_vsrn)
	PPC_STLU r1,-STKFRM(r1)
	mflr	r0
	PPC_STL	r0,STKFRM+PPC_LR_STKOFF(r1)
	mfmsr	r6
	oris	r7,r6,MSR_VSX@h
	cmpwi	cr7,r3,0
	li	r8,STKFRM-16
	MTMSRD(r7)
	isync
	beq	cr7,1f
	STXVD2X(0,R1,R8)
1:	LXVD2X(0,R0,R4)
#ifdef __LITTLE_ENDIAN__
	XXSWAPD(0,0)
#endif
	beq	cr7,4f
	bl	put_vsr
	LXVD2X(0,R1,R8)
4:	PPC_LL	r0,STKFRM+PPC_LR_STKOFF(r1)
	mtlr	r0
	MTMSRD(r6)
	isync
	addi	r1,r1,STKFRM
	blr

/* Store VSX reg N to vector doubleword *p.  N is in r3, p in r4. */
_GLOBAL(store_vsrn)
	PPC_STLU r1,-STKFRM(r1)
	mflr	r0
	PPC_STL	r0,STKFRM+PPC_LR_STKOFF(r1)
	mfmsr	r6
	oris	r7,r6,MSR_VSX@h
	li	r8,STKFRM-16
	MTMSRD(r7)
	isync
	STXVD2X(0,R1,R8)
	bl	get_vsr
#ifdef __LITTLE_ENDIAN__
	XXSWAPD(0,0)
#endif
	STXVD2X(0,R0,R4)
	LXVD2X(0,R1,R8)
	PPC_LL	r0,STKFRM+PPC_LR_STKOFF(r1)
	mtlr	r0
	MTMSRD(r6)
	isync
	mr	r3,r9
	addi	r1,r1,STKFRM
	blr
#endif /* CONFIG_VSX */

/* Convert single-precision to double, without disturbing FPRs. */
/* conv_sp_to_dp(float *sp, double *dp) */
_GLOBAL(conv_sp_to_dp)
	mfmsr	r6
	ori	r7, r6, MSR_FP
	MTMSRD(r7)
	isync
	stfd	fr0, -16(r1)
	lfs	fr0, 0(r3)
	stfd	fr0, 0(r4)
	lfd	fr0, -16(r1)
	MTMSRD(r6)
	isync
	blr

/* Convert single-precision to double, without disturbing FPRs. */
/* conv_sp_to_dp(double *dp, float *sp) */
_GLOBAL(conv_dp_to_sp)
	mfmsr	r6
	ori	r7, r6, MSR_FP
	MTMSRD(r7)
	isync
	stfd	fr0, -16(r1)
	lfd	fr0, 0(r3)
	stfs	fr0, 0(r4)
	lfd	fr0, -16(r1)
	MTMSRD(r6)
	isync
	blr

#endif	/* CONFIG_PPC_FPU */
