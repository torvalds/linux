/*
 *  FPU support code, moved here from head.S so that it can be used
 *  by chips which use other head-whatever.S files.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <asm/reg.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/cputable.h>
#include <asm/cache.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>

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
	SAVE_32FPRS(0, r4)
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
	mfspr	r5,SPRN_SPRG3		/* current task's THREAD (phys) */
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
	mtfsf	0xff,fr0
	REST_32FPRS(0, r5)
#ifndef CONFIG_SMP
	subi	r4,r5,THREAD
	fromreal(r4)
	PPC_STL	r4,ADDROFF(last_task_used_math)(r3)
#endif /* CONFIG_SMP */
	/* restore registers and return */
	/* we haven't used ctr or xer or lr */
	b	fast_exception_return

/*
 * giveup_fpu(tsk)
 * Disable FP for the task given as the argument,
 * and save the floating-point registers in its thread_struct.
 * Enables the FPU for use in the kernel on return.
 */
_GLOBAL(giveup_fpu)
	mfmsr	r5
	ori	r5,r5,MSR_FP
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
	SAVE_32FPRS(0, r3)
	mffs	fr0
	stfd	fr0,THREAD_FPSCR(r3)
	beq	1f
	PPC_LL	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
	li	r3,MSR_FP|MSR_FE0|MSR_FE1
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
 * We restore and save the fpscr so the task gets the same result
 * and exceptions as if the cpu had performed the load or store.
 */

_GLOBAL(cvt_fd)
	lfd	0,THREAD_FPSCR(r5)	/* load up fpscr value */
	mtfsf	0xff,0
	lfs	0,0(r3)
	stfd	0,0(r4)
	mffs	0
	stfd	0,THREAD_FPSCR(r5)	/* save new fpscr value */
	blr

_GLOBAL(cvt_df)
	lfd	0,THREAD_FPSCR(r5)	/* load up fpscr value */
	mtfsf	0xff,0
	lfd	0,0(r3)
	stfs	0,0(r4)
	mffs	0
	stfd	0,THREAD_FPSCR(r5)	/* save new fpscr value */
	blr
