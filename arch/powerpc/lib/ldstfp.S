/*
 * Floating-point, VMX/Altivec and VSX loads and stores
 * for use in instruction emulation.
 *
 * Copyright 2010 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <asm/processor.h>
#include <asm/ppc_asm.h>
#include <asm/ppc-opcode.h>
#include <asm/reg.h>
#include <asm/asm-offsets.h>
#include <linux/errno.h>

#ifdef CONFIG_PPC_FPU

#define STKFRM	(PPC_MIN_STKFRM + 16)

	.macro	inst32	op
reg = 0
	.rept	32
20:	\op	reg,0,r4
	b	3f
	EX_TABLE(20b,99f)
reg = reg + 1
	.endr
	.endm

/* Get the contents of frN into fr0; N is in r3. */
_GLOBAL(get_fpr)
	mflr	r0
	rlwinm	r3,r3,3,0xf8
	bcl	20,31,1f
	blr			/* fr0 is already in fr0 */
	nop
reg = 1
	.rept	31
	fmr	fr0,reg
	blr
reg = reg + 1
	.endr
1:	mflr	r5
	add	r5,r3,r5
	mtctr	r5
	mtlr	r0
	bctr

/* Put the contents of fr0 into frN; N is in r3. */
_GLOBAL(put_fpr)
	mflr	r0
	rlwinm	r3,r3,3,0xf8
	bcl	20,31,1f
	blr			/* fr0 is already in fr0 */
	nop
reg = 1
	.rept	31
	fmr	reg,fr0
	blr
reg = reg + 1
	.endr
1:	mflr	r5
	add	r5,r3,r5
	mtctr	r5
	mtlr	r0
	bctr

/* Load FP reg N from float at *p.  N is in r3, p in r4. */
_GLOBAL(do_lfs)
	PPC_STLU r1,-STKFRM(r1)
	mflr	r0
	PPC_STL	r0,STKFRM+PPC_LR_STKOFF(r1)
	mfmsr	r6
	ori	r7,r6,MSR_FP
	cmpwi	cr7,r3,0
	MTMSRD(r7)
	isync
	beq	cr7,1f
	stfd	fr0,STKFRM-16(r1)
1:	li	r9,-EFAULT
2:	lfs	fr0,0(r4)
	li	r9,0
3:	bl	put_fpr
	beq	cr7,4f
	lfd	fr0,STKFRM-16(r1)
4:	PPC_LL	r0,STKFRM+PPC_LR_STKOFF(r1)
	mtlr	r0
	MTMSRD(r6)
	isync
	mr	r3,r9
	addi	r1,r1,STKFRM
	blr
	EX_TABLE(2b,3b)

/* Load FP reg N from double at *p.  N is in r3, p in r4. */
_GLOBAL(do_lfd)
	PPC_STLU r1,-STKFRM(r1)
	mflr	r0
	PPC_STL	r0,STKFRM+PPC_LR_STKOFF(r1)
	mfmsr	r6
	ori	r7,r6,MSR_FP
	cmpwi	cr7,r3,0
	MTMSRD(r7)
	isync
	beq	cr7,1f
	stfd	fr0,STKFRM-16(r1)
1:	li	r9,-EFAULT
2:	lfd	fr0,0(r4)
	li	r9,0
3:	beq	cr7,4f
	bl	put_fpr
	lfd	fr0,STKFRM-16(r1)
4:	PPC_LL	r0,STKFRM+PPC_LR_STKOFF(r1)
	mtlr	r0
	MTMSRD(r6)
	isync
	mr	r3,r9
	addi	r1,r1,STKFRM
	blr
	EX_TABLE(2b,3b)

/* Store FP reg N to float at *p.  N is in r3, p in r4. */
_GLOBAL(do_stfs)
	PPC_STLU r1,-STKFRM(r1)
	mflr	r0
	PPC_STL	r0,STKFRM+PPC_LR_STKOFF(r1)
	mfmsr	r6
	ori	r7,r6,MSR_FP
	cmpwi	cr7,r3,0
	MTMSRD(r7)
	isync
	beq	cr7,1f
	stfd	fr0,STKFRM-16(r1)
	bl	get_fpr
1:	li	r9,-EFAULT
2:	stfs	fr0,0(r4)
	li	r9,0
3:	beq	cr7,4f
	lfd	fr0,STKFRM-16(r1)
4:	PPC_LL	r0,STKFRM+PPC_LR_STKOFF(r1)
	mtlr	r0
	MTMSRD(r6)
	isync
	mr	r3,r9
	addi	r1,r1,STKFRM
	blr
	EX_TABLE(2b,3b)

/* Store FP reg N to double at *p.  N is in r3, p in r4. */
_GLOBAL(do_stfd)
	PPC_STLU r1,-STKFRM(r1)
	mflr	r0
	PPC_STL	r0,STKFRM+PPC_LR_STKOFF(r1)
	mfmsr	r6
	ori	r7,r6,MSR_FP
	cmpwi	cr7,r3,0
	MTMSRD(r7)
	isync
	beq	cr7,1f
	stfd	fr0,STKFRM-16(r1)
	bl	get_fpr
1:	li	r9,-EFAULT
2:	stfd	fr0,0(r4)
	li	r9,0
3:	beq	cr7,4f
	lfd	fr0,STKFRM-16(r1)
4:	PPC_LL	r0,STKFRM+PPC_LR_STKOFF(r1)
	mtlr	r0
	MTMSRD(r6)
	isync
	mr	r3,r9
	addi	r1,r1,STKFRM
	blr
	EX_TABLE(2b,3b)

#ifdef CONFIG_ALTIVEC
/* Get the contents of vrN into v0; N is in r3. Doesn't touch r3 or r4. */
_GLOBAL(get_vr)
	mflr	r0
	rlwinm	r6,r3,3,0xf8
	bcl	20,31,1f
	blr			/* v0 is already in v0 */
	nop
reg = 1
	.rept	31
	vor	v0,reg,reg	/* assembler doesn't know vmr? */
	blr
reg = reg + 1
	.endr
1:	mflr	r5
	add	r5,r6,r5
	mtctr	r5
	mtlr	r0
	bctr

/* Put the contents of v0 into vrN; N is in r3. Doesn't touch r3 or r4. */
_GLOBAL(put_vr)
	mflr	r0
	rlwinm	r6,r3,3,0xf8
	bcl	20,31,1f
	blr			/* v0 is already in v0 */
	nop
reg = 1
	.rept	31
	vor	reg,v0,v0
	blr
reg = reg + 1
	.endr
1:	mflr	r5
	add	r5,r6,r5
	mtctr	r5
	mtlr	r0
	bctr

/* Load vector reg N from *p.  N is in r3, p in r4. */
_GLOBAL(do_lvx)
	PPC_STLU r1,-STKFRM(r1)
	mflr	r0
	PPC_STL	r0,STKFRM+PPC_LR_STKOFF(r1)
	mfmsr	r6
	oris	r7,r6,MSR_VEC@h
	cmpwi	cr7,r3,0
	li	r8,STKFRM-16
	MTMSRD(r7)
	isync
	beq	cr7,1f
	stvx	v0,r1,r8
1:	li	r9,-EFAULT
2:	lvx	v0,0,r4
	li	r9,0
3:	beq	cr7,4f
	bl	put_vr
	lvx	v0,r1,r8
4:	PPC_LL	r0,STKFRM+PPC_LR_STKOFF(r1)
	mtlr	r0
	MTMSRD(r6)
	isync
	mr	r3,r9
	addi	r1,r1,STKFRM
	blr
	EX_TABLE(2b,3b)

/* Store vector reg N to *p.  N is in r3, p in r4. */
_GLOBAL(do_stvx)
	PPC_STLU r1,-STKFRM(r1)
	mflr	r0
	PPC_STL	r0,STKFRM+PPC_LR_STKOFF(r1)
	mfmsr	r6
	oris	r7,r6,MSR_VEC@h
	cmpwi	cr7,r3,0
	li	r8,STKFRM-16
	MTMSRD(r7)
	isync
	beq	cr7,1f
	stvx	v0,r1,r8
	bl	get_vr
1:	li	r9,-EFAULT
2:	stvx	v0,0,r4
	li	r9,0
3:	beq	cr7,4f
	lvx	v0,r1,r8
4:	PPC_LL	r0,STKFRM+PPC_LR_STKOFF(r1)
	mtlr	r0
	MTMSRD(r6)
	isync
	mr	r3,r9
	addi	r1,r1,STKFRM
	blr
	EX_TABLE(2b,3b)
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
	EX_TABLE(2b,3b)
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
