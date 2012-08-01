/*
 *  FPU support code, moved here from head.S so that it can be used
 *  by chips which use other head-whatever.S files.
 *
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *    Copyright (C) 1996 Cort Dougan <cort@cs.nmt.edu>
 *    Copyright (C) 1996 Paul Mackerras.
 *    Copyright (C) 1997 Dan Malek (dmalek@jlc.net).
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <asm/reg.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/cputable.h>
#include <asm/cache.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/ptrace.h>

#ifdef CONFIG_VSX
#define __REST_32FPVSRS(n,c,base)					\
BEGIN_FTR_SECTION							\
	b	2f;							\
END_FTR_SECTION_IFSET(CPU_FTR_VSX);					\
	REST_32FPRS(n,base);						\
	b	3f;							\
2:	REST_32VSRS(n,c,base);						\
3:

#define __SAVE_32FPVSRS(n,c,base)					\
BEGIN_FTR_SECTION							\
	b	2f;							\
END_FTR_SECTION_IFSET(CPU_FTR_VSX);					\
	SAVE_32FPRS(n,base);						\
	b	3f;							\
2:	SAVE_32VSRS(n,c,base);						\
3:
#else
#define __REST_32FPVSRS(n,b,base)	REST_32FPRS(n, base)
#define __SAVE_32FPVSRS(n,b,base)	SAVE_32FPRS(n, base)
#endif
#define REST_32FPVSRS(n,c,base) __REST_32FPVSRS(n,__REG_##c,__REG_##base)
#define SAVE_32FPVSRS(n,c,base) __SAVE_32FPVSRS(n,__REG_##c,__REG_##base)

/*
 * This task wants to use the FPU now.
 * On UP, disable FP for the task which had the FPU previously,
 * and save its floating-point registers in its thread_struct.
 * Load up this task's FP registers from its thread_struct,
 * enable the FPU for the current task and return to the task.
 */
_GLOBAL(load_up_fpu)
	mfmsr	r5
	ori	r5,r5,MSR_FP
#ifdef CONFIG_VSX
BEGIN_FTR_SECTION
	oris	r5,r5,MSR_VSX@h
END_FTR_SECTION_IFSET(CPU_FTR_VSX)
#endif
	SYNC
	MTMSRD(r5)			/* enable use of fpu now */
	isync
/*
 * For SMP, we don't do lazy FPU switching because it just gets too
 * horrendously complex, especially when a task switches from one CPU
 * to another.  Instead we call giveup_fpu in switch_to.
 */
#ifndef CONFIG_SMP
	LOAD_REG_ADDRBASE(r3, last_task_used_math)
	toreal(r3)
	PPC_LL	r4,ADDROFF(last_task_used_math)(r3)
	PPC_LCMPI	0,r4,0
	beq	1f
	toreal(r4)
	addi	r4,r4,THREAD		/* want last_task_used_math->thread */
	SAVE_32FPVSRS(0, R5, R4)
	mffs	fr0
	stfd	fr0,THREAD_FPSCR(r4)
	PPC_LL	r5,PT_REGS(r4)
	toreal(r5)
	PPC_LL	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
	li	r10,MSR_FP|MSR_FE0|MSR_FE1
	andc	r4,r4,r10		/* disable FP for previous task */
	PPC_STL	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
1:
#endif /* CONFIG_SMP */
	/* enable use of FP after return */
#ifdef CONFIG_PPC32
	mfspr	r5,SPRN_SPRG_THREAD		/* current task's THREAD (phys) */
	lwz	r4,THREAD_FPEXC_MODE(r5)
	ori	r9,r9,MSR_FP		/* enable FP for current */
	or	r9,r9,r4
#else
	ld	r4,PACACURRENT(r13)
	addi	r5,r4,THREAD		/* Get THREAD */
	lwz	r4,THREAD_FPEXC_MODE(r5)
	ori	r12,r12,MSR_FP
	or	r12,r12,r4
	std	r12,_MSR(r1)
#endif
	lfd	fr0,THREAD_FPSCR(r5)
	MTFSF_L(fr0)
	REST_32FPVSRS(0, R4, R5)
#ifndef CONFIG_SMP
	subi	r4,r5,THREAD
	fromreal(r4)
	PPC_STL	r4,ADDROFF(last_task_used_math)(r3)
#endif /* CONFIG_SMP */
	/* restore registers and return */
	/* we haven't used ctr or xer or lr */
	blr

/*
 * giveup_fpu(tsk)
 * Disable FP for the task given as the argument,
 * and save the floating-point registers in its thread_struct.
 * Enables the FPU for use in the kernel on return.
 */
_GLOBAL(giveup_fpu)
	mfmsr	r5
	ori	r5,r5,MSR_FP
#ifdef CONFIG_VSX
BEGIN_FTR_SECTION
	oris	r5,r5,MSR_VSX@h
END_FTR_SECTION_IFSET(CPU_FTR_VSX)
#endif
	SYNC_601
	ISYNC_601
	MTMSRD(r5)			/* enable use of fpu now */
	SYNC_601
	isync
	PPC_LCMPI	0,r3,0
	beqlr-				/* if no previous owner, done */
	addi	r3,r3,THREAD	        /* want THREAD of task */
	PPC_LL	r5,PT_REGS(r3)
	PPC_LCMPI	0,r5,0
	SAVE_32FPVSRS(0, R4 ,R3)
	mffs	fr0
	stfd	fr0,THREAD_FPSCR(r3)
	beq	1f
	PPC_LL	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
	li	r3,MSR_FP|MSR_FE0|MSR_FE1
#ifdef CONFIG_VSX
BEGIN_FTR_SECTION
	oris	r3,r3,MSR_VSX@h
END_FTR_SECTION_IFSET(CPU_FTR_VSX)
#endif
	andc	r4,r4,r3		/* disable FP for previous task */
	PPC_STL	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
1:
#ifndef CONFIG_SMP
	li	r5,0
	LOAD_REG_ADDRBASE(r4,last_task_used_math)
	PPC_STL	r5,ADDROFF(last_task_used_math)(r4)
#endif /* CONFIG_SMP */
	blr

/*
 * These are used in the alignment trap handler when emulating
 * single-precision loads and stores.
 */

_GLOBAL(cvt_fd)
	lfs	0,0(r3)
	stfd	0,0(r4)
	blr

_GLOBAL(cvt_df)
	lfd	0,0(r3)
	stfs	0,0(r4)
	blr
