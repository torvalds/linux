#include <asm/processor.h>
#include <asm/ppc_asm.h>
#include <asm/reg.h>
#include <asm/asm-offsets.h>
#include <asm/cputable.h>
#include <asm/thread_info.h>
#include <asm/page.h>

/*
 * load_up_altivec(unused, unused, tsk)
 * Disable VMX for the task which had it previously,
 * and save its vector registers in its thread_struct.
 * Enables the VMX for use in the kernel on return.
 * On SMP we know the VMX is free, since we give it up every
 * switch (ie, no lazy save of the vector registers).
 */
_GLOBAL(load_up_altivec)
	mfmsr	r5			/* grab the current MSR */
	oris	r5,r5,MSR_VEC@h
	MTMSRD(r5)			/* enable use of AltiVec now */
	isync

/*
 * For SMP, we don't do lazy VMX switching because it just gets too
 * horrendously complex, especially when a task switches from one CPU
 * to another.  Instead we call giveup_altvec in switch_to.
 * VRSAVE isn't dealt with here, that is done in the normal context
 * switch code. Note that we could rely on vrsave value to eventually
 * avoid saving all of the VREGs here...
 */
#ifndef CONFIG_SMP
	LOAD_REG_ADDRBASE(r3, last_task_used_altivec)
	toreal(r3)
	PPC_LL	r4,ADDROFF(last_task_used_altivec)(r3)
	PPC_LCMPI	0,r4,0
	beq	1f

	/* Save VMX state to last_task_used_altivec's THREAD struct */
	toreal(r4)
	addi	r4,r4,THREAD
	SAVE_32VRS(0,r5,r4)
	mfvscr	vr0
	li	r10,THREAD_VSCR
	stvx	vr0,r10,r4
	/* Disable VMX for last_task_used_altivec */
	PPC_LL	r5,PT_REGS(r4)
	toreal(r5)
	PPC_LL	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
	lis	r10,MSR_VEC@h
	andc	r4,r4,r10
	PPC_STL	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
1:
#endif /* CONFIG_SMP */

	/* Hack: if we get an altivec unavailable trap with VRSAVE
	 * set to all zeros, we assume this is a broken application
	 * that fails to set it properly, and thus we switch it to
	 * all 1's
	 */
	mfspr	r4,SPRN_VRSAVE
	cmpwi	0,r4,0
	bne+	1f
	li	r4,-1
	mtspr	SPRN_VRSAVE,r4
1:
	/* enable use of VMX after return */
#ifdef CONFIG_PPC32
	mfspr	r5,SPRN_SPRG_THREAD		/* current task's THREAD (phys) */
	oris	r9,r9,MSR_VEC@h
#else
	ld	r4,PACACURRENT(r13)
	addi	r5,r4,THREAD		/* Get THREAD */
	oris	r12,r12,MSR_VEC@h
	std	r12,_MSR(r1)
#endif
	li	r4,1
	li	r10,THREAD_VSCR
	stw	r4,THREAD_USED_VR(r5)
	lvx	vr0,r10,r5
	mtvscr	vr0
	REST_32VRS(0,r4,r5)
#ifndef CONFIG_SMP
	/* Update last_task_used_altivec to 'current' */
	subi	r4,r5,THREAD		/* Back to 'current' */
	fromreal(r4)
	PPC_STL	r4,ADDROFF(last_task_used_altivec)(r3)
#endif /* CONFIG_SMP */
	/* restore registers and return */
	blr

/*
 * giveup_altivec(tsk)
 * Disable VMX for the task given as the argument,
 * and save the vector registers in its thread_struct.
 * Enables the VMX for use in the kernel on return.
 */
_GLOBAL(giveup_altivec)
	mfmsr	r5
	oris	r5,r5,MSR_VEC@h
	SYNC
	MTMSRD(r5)			/* enable use of VMX now */
	isync
	PPC_LCMPI	0,r3,0
	beqlr-				/* if no previous owner, done */
	addi	r3,r3,THREAD		/* want THREAD of task */
	PPC_LL	r5,PT_REGS(r3)
	PPC_LCMPI	0,r5,0
	SAVE_32VRS(0,r4,r3)
	mfvscr	vr0
	li	r4,THREAD_VSCR
	stvx	vr0,r4,r3
	beq	1f
	PPC_LL	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
#ifdef CONFIG_VSX
BEGIN_FTR_SECTION
	lis	r3,(MSR_VEC|MSR_VSX)@h
FTR_SECTION_ELSE
	lis	r3,MSR_VEC@h
ALT_FTR_SECTION_END_IFSET(CPU_FTR_VSX)
#else
	lis	r3,MSR_VEC@h
#endif
	andc	r4,r4,r3		/* disable FP for previous task */
	PPC_STL	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
1:
#ifndef CONFIG_SMP
	li	r5,0
	LOAD_REG_ADDRBASE(r4,last_task_used_altivec)
	PPC_STL	r5,ADDROFF(last_task_used_altivec)(r4)
#endif /* CONFIG_SMP */
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

#ifndef CONFIG_SMP
	ld	r3,last_task_used_vsx@got(r2)
	ld	r4,0(r3)
	cmpdi	0,r4,0
	beq	1f
	/* Disable VSX for last_task_used_vsx */
	addi	r4,r4,THREAD
	ld	r5,PT_REGS(r4)
	ld	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
	lis	r6,MSR_VSX@h
	andc	r6,r4,r6
	std	r6,_MSR-STACK_FRAME_OVERHEAD(r5)
1:
#endif /* CONFIG_SMP */
	ld	r4,PACACURRENT(r13)
	addi	r4,r4,THREAD		/* Get THREAD */
	li	r6,1
	stw	r6,THREAD_USED_VSR(r4) /* ... also set thread used vsr */
	/* enable use of VSX after return */
	oris	r12,r12,MSR_VSX@h
	std	r12,_MSR(r1)
#ifndef CONFIG_SMP
	/* Update last_task_used_vsx to 'current' */
	ld	r4,PACACURRENT(r13)
	std	r4,0(r3)
#endif /* CONFIG_SMP */
	b	fast_exception_return

/*
 * __giveup_vsx(tsk)
 * Disable VSX for the task given as the argument.
 * Does NOT save vsx registers.
 * Enables the VSX for use in the kernel on return.
 */
_GLOBAL(__giveup_vsx)
	mfmsr	r5
	oris	r5,r5,MSR_VSX@h
	mtmsrd	r5			/* enable use of VSX now */
	isync

	cmpdi	0,r3,0
	beqlr-				/* if no previous owner, done */
	addi	r3,r3,THREAD		/* want THREAD of task */
	ld	r5,PT_REGS(r3)
	cmpdi	0,r5,0
	beq	1f
	ld	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
	lis	r3,MSR_VSX@h
	andc	r4,r4,r3		/* disable VSX for previous task */
	std	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
1:
#ifndef CONFIG_SMP
	li	r5,0
	ld	r4,last_task_used_vsx@got(r2)
	std	r5,0(r4)
#endif /* CONFIG_SMP */
	blr

#endif /* CONFIG_VSX */


/*
 * The routines below are in assembler so we can closely control the
 * usage of floating-point registers.  These routines must be called
 * with preempt disabled.
 */
#ifdef CONFIG_PPC32
	.data
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

	.section ".toc","aw"
fpzero:
	.tc	FD_0_0[TC],0
fpone:
	.tc	FD_3ff00000_0[TC],0x3ff0000000000000	/* 1.0 */
fphalf:
	.tc	FD_3fe00000_0[TC],0x3fe0000000000000	/* 0.5 */

#define LDCONST(fr, name)	\
	lfd	fr,name@toc(r2)
#endif

	.text
/*
 * Internal routine to enable floating point and set FPSCR to 0.
 * Don't call it from C; it doesn't use the normal calling convention.
 */
fpenable:
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
