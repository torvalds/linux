/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/export.h>
#include <linux/linkage.h>
#include <asm/processor.h>
#include <asm/ppc_asm.h>
#include <asm/reg.h>
#include <asm/asm-offsets.h>
#include <asm/cputable.h>
#include <asm/thread_info.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/asm-compat.h>

/*
 * Load state from memory into VMX registers including VSCR.
 * Assumes the caller has enabled VMX in the MSR.
 */
_GLOBAL(load_vr_state)
	li	r4,VRSTATE_VSCR
	lvx	v0,r4,r3
	mtvscr	v0
	REST_32VRS(0,r4,r3)
	blr
EXPORT_SYMBOL(load_vr_state)
_ASM_NOKPROBE_SYMBOL(load_vr_state); /* used by restore_math */

/*
 * Store VMX state into memory, including VSCR.
 * Assumes the caller has enabled VMX in the MSR.
 */
_GLOBAL(store_vr_state)
	SAVE_32VRS(0, r4, r3)
	mfvscr	v0
	li	r4, VRSTATE_VSCR
	stvx	v0, r4, r3
	lvx	v0, 0, r3
	blr
EXPORT_SYMBOL(store_vr_state)

/*
 * Disable VMX for the task which had it previously,
 * and save its vector registers in its thread_struct.
 * Enables the VMX for use in the kernel on return.
 * On SMP we know the VMX is free, since we give it up every
 * switch (ie, no lazy save of the vector registers).
 *
 * Note that on 32-bit this can only use registers that will be
 * restored by fast_exception_return, i.e. r3 - r6, r10 and r11.
 */
_GLOBAL(load_up_altivec)
	mfmsr	r5			/* grab the current MSR */
#ifdef CONFIG_PPC_BOOK3S_64
	/* interrupt doesn't set MSR[RI] and HPT can fault on current access */
	ori	r5,r5,MSR_RI
#endif
	oris	r5,r5,MSR_VEC@h
	MTMSRD(r5)			/* enable use of AltiVec now */
	isync

	/*
	 * While userspace in general ignores VRSAVE, glibc uses it as a boolean
	 * to optimise userspace context save/restore. Whenever we take an
	 * altivec unavailable exception we must set VRSAVE to something non
	 * zero. Set it to all 1s. See also the programming note in the ISA.
	 */
	mfspr	r4,SPRN_VRSAVE
	cmpwi	0,r4,0
	bne+	1f
	li	r4,-1
	mtspr	SPRN_VRSAVE,r4
1:
	/* enable use of VMX after return */
#ifdef CONFIG_PPC32
	addi	r5,r2,THREAD
	oris	r9,r9,MSR_VEC@h
#else
	ld	r4,PACACURRENT(r13)
	addi	r5,r4,THREAD		/* Get THREAD */
	oris	r12,r12,MSR_VEC@h
	std	r12,_MSR(r1)
#ifdef CONFIG_PPC_BOOK3S_64
	li	r4,0
	stb	r4,PACASRR_VALID(r13)
#endif
#endif
	li	r4,1
	stb	r4,THREAD_LOAD_VEC(r5)
	addi	r6,r5,THREAD_VRSTATE
	li	r10,VRSTATE_VSCR
	stw	r4,THREAD_USED_VR(r5)
	lvx	v0,r10,r6
	mtvscr	v0
	REST_32VRS(0,r4,r6)
	/* restore registers and return */
	blr
_ASM_NOKPROBE_SYMBOL(load_up_altivec)

/*
 * save_altivec(tsk)
 * Save the vector registers to its thread_struct
 */
_GLOBAL(save_altivec)
	addi	r3,r3,THREAD		/* want THREAD of task */
	PPC_LL	r7,THREAD_VRSAVEAREA(r3)
	PPC_LL	r5,PT_REGS(r3)
	PPC_LCMPI	0,r7,0
	bne	2f
	addi	r7,r3,THREAD_VRSTATE
2:	SAVE_32VRS(0,r4,r7)
	mfvscr	v0
	li	r4,VRSTATE_VSCR
	stvx	v0,r4,r7
	lvx	v0,0,r7
	blr

#ifdef CONFIG_VSX

#ifdef CONFIG_PPC32
#error This asm code isn't ready for 32-bit kernels
#endif

/*
 * load_up_vsx(unused, unused, tsk)
 * Disable VSX for the task which had it previously,
 * and save its vector registers in its thread_struct.
 * Reuse the fp and vsx saves, but first check to see if they have
 * been saved already.
 */
_GLOBAL(load_up_vsx)
/* Load FP and VSX registers if they haven't been done yet */
	andi.	r5,r12,MSR_FP
	beql+	load_up_fpu		/* skip if already loaded */
	andis.	r5,r12,MSR_VEC@h
	beql+	load_up_altivec		/* skip if already loaded */

#ifdef CONFIG_PPC_BOOK3S_64
	/* interrupt doesn't set MSR[RI] and HPT can fault on current access */
	li	r5,MSR_RI
	mtmsrd	r5,1
#endif

	ld	r4,PACACURRENT(r13)
	addi	r4,r4,THREAD		/* Get THREAD */
	li	r6,1
	stw	r6,THREAD_USED_VSR(r4) /* ... also set thread used vsr */
	/* enable use of VSX after return */
	oris	r12,r12,MSR_VSX@h
	std	r12,_MSR(r1)
	li	r4,0
	stb	r4,PACASRR_VALID(r13)
	b	fast_interrupt_return_srr

#endif /* CONFIG_VSX */


/*
 * The routines below are in assembler so we can closely control the
 * usage of floating-point registers.  These routines must be called
 * with preempt disabled.
 */
	.data
#ifdef CONFIG_PPC32
fpzero:
	.long	0
fpone:
	.long	0x3f800000	/* 1.0 in single-precision FP */
fphalf:
	.long	0x3f000000	/* 0.5 in single-precision FP */

#define LDCONST(fr, name)	\
	lis	r11,name@ha;	\
	lfs	fr,name@l(r11)
#else

fpzero:
	.quad	0
fpone:
	.quad	0x3ff0000000000000	/* 1.0 */
fphalf:
	.quad	0x3fe0000000000000	/* 0.5 */

#ifdef CONFIG_PPC_KERNEL_PCREL
#define LDCONST(fr, name)		\
	pla	r11,name@pcrel;		\
	lfd	fr,0(r11)
#else
#define LDCONST(fr, name)		\
	addis	r11,r2,name@toc@ha;	\
	lfd	fr,name@toc@l(r11)
#endif
#endif
	.text
/*
 * Internal routine to enable floating point and set FPSCR to 0.
 * Don't call it from C; it doesn't use the normal calling convention.
 */
SYM_FUNC_START_LOCAL(fpenable)
#ifdef CONFIG_PPC32
	stwu	r1,-64(r1)
#else
	stdu	r1,-64(r1)
#endif
	mfmsr	r10
	ori	r11,r10,MSR_FP
	mtmsr	r11
	isync
	stfd	fr0,24(r1)
	stfd	fr1,16(r1)
	stfd	fr31,8(r1)
	LDCONST(fr1, fpzero)
	mffs	fr31
	MTFSF_L(fr1)
	blr
SYM_FUNC_END(fpenable)

fpdisable:
	mtlr	r12
	MTFSF_L(fr31)
	lfd	fr31,8(r1)
	lfd	fr1,16(r1)
	lfd	fr0,24(r1)
	mtmsr	r10
	isync
	addi	r1,r1,64
	blr

/*
 * Vector add, floating point.
 */
_GLOBAL(vaddfp)
	mflr	r12
	bl	fpenable
	li	r0,4
	mtctr	r0
	li	r6,0
1:	lfsx	fr0,r4,r6
	lfsx	fr1,r5,r6
	fadds	fr0,fr0,fr1
	stfsx	fr0,r3,r6
	addi	r6,r6,4
	bdnz	1b
	b	fpdisable

/*
 * Vector subtract, floating point.
 */
_GLOBAL(vsubfp)
	mflr	r12
	bl	fpenable
	li	r0,4
	mtctr	r0
	li	r6,0
1:	lfsx	fr0,r4,r6
	lfsx	fr1,r5,r6
	fsubs	fr0,fr0,fr1
	stfsx	fr0,r3,r6
	addi	r6,r6,4
	bdnz	1b
	b	fpdisable

/*
 * Vector multiply and add, floating point.
 */
_GLOBAL(vmaddfp)
	mflr	r12
	bl	fpenable
	stfd	fr2,32(r1)
	li	r0,4
	mtctr	r0
	li	r7,0
1:	lfsx	fr0,r4,r7
	lfsx	fr1,r5,r7
	lfsx	fr2,r6,r7
	fmadds	fr0,fr0,fr2,fr1
	stfsx	fr0,r3,r7
	addi	r7,r7,4
	bdnz	1b
	lfd	fr2,32(r1)
	b	fpdisable

/*
 * Vector negative multiply and subtract, floating point.
 */
_GLOBAL(vnmsubfp)
	mflr	r12
	bl	fpenable
	stfd	fr2,32(r1)
	li	r0,4
	mtctr	r0
	li	r7,0
1:	lfsx	fr0,r4,r7
	lfsx	fr1,r5,r7
	lfsx	fr2,r6,r7
	fnmsubs	fr0,fr0,fr2,fr1
	stfsx	fr0,r3,r7
	addi	r7,r7,4
	bdnz	1b
	lfd	fr2,32(r1)
	b	fpdisable

/*
 * Vector reciprocal estimate.  We just compute 1.0/x.
 * r3 -> destination, r4 -> source.
 */
_GLOBAL(vrefp)
	mflr	r12
	bl	fpenable
	li	r0,4
	LDCONST(fr1, fpone)
	mtctr	r0
	li	r6,0
1:	lfsx	fr0,r4,r6
	fdivs	fr0,fr1,fr0
	stfsx	fr0,r3,r6
	addi	r6,r6,4
	bdnz	1b
	b	fpdisable

/*
 * Vector reciprocal square-root estimate, floating point.
 * We use the frsqrte instruction for the initial estimate followed
 * by 2 iterations of Newton-Raphson to get sufficient accuracy.
 * r3 -> destination, r4 -> source.
 */
_GLOBAL(vrsqrtefp)
	mflr	r12
	bl	fpenable
	stfd	fr2,32(r1)
	stfd	fr3,40(r1)
	stfd	fr4,48(r1)
	stfd	fr5,56(r1)
	li	r0,4
	LDCONST(fr4, fpone)
	LDCONST(fr5, fphalf)
	mtctr	r0
	li	r6,0
1:	lfsx	fr0,r4,r6
	frsqrte	fr1,fr0		/* r = frsqrte(s) */
	fmuls	fr3,fr1,fr0	/* r * s */
	fmuls	fr2,fr1,fr5	/* r * 0.5 */
	fnmsubs	fr3,fr1,fr3,fr4	/* 1 - s * r * r */
	fmadds	fr1,fr2,fr3,fr1	/* r = r + 0.5 * r * (1 - s * r * r) */
	fmuls	fr3,fr1,fr0	/* r * s */
	fmuls	fr2,fr1,fr5	/* r * 0.5 */
	fnmsubs	fr3,fr1,fr3,fr4	/* 1 - s * r * r */
	fmadds	fr1,fr2,fr3,fr1	/* r = r + 0.5 * r * (1 - s * r * r) */
	stfsx	fr1,r3,r6
	addi	r6,r6,4
	bdnz	1b
	lfd	fr5,56(r1)
	lfd	fr4,48(r1)
	lfd	fr3,40(r1)
	lfd	fr2,32(r1)
	b	fpdisable
