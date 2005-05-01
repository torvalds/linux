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
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/cputable.h>
#include <asm/cache.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/offsets.h>

/*
 * This task wants to use the FPU now.
 * On UP, disable FP for the task which had the FPU previously,
 * and save its floating-point registers in its thread_struct.
 * Load up this task's FP registers from its thread_struct,
 * enable the FPU for the current task and return to the task.
 */
	.globl	load_up_fpu
load_up_fpu:
	mfmsr	r5
	ori	r5,r5,MSR_FP
#ifdef CONFIG_PPC64BRIDGE
	clrldi	r5,r5,1			/* turn off 64-bit mode */
#endif /* CONFIG_PPC64BRIDGE */
	SYNC
	MTMSRD(r5)			/* enable use of fpu now */
	isync
/*
 * For SMP, we don't do lazy FPU switching because it just gets too
 * horrendously complex, especially when a task switches from one CPU
 * to another.  Instead we call giveup_fpu in switch_to.
 */
#ifndef CONFIG_SMP
	tophys(r6,0)			/* get __pa constant */
	addis	r3,r6,last_task_used_math@ha
	lwz	r4,last_task_used_math@l(r3)
	cmpwi	0,r4,0
	beq	1f
	add	r4,r4,r6
	addi	r4,r4,THREAD		/* want last_task_used_math->thread */
	SAVE_32FPRS(0, r4)
	mffs	fr0
	stfd	fr0,THREAD_FPSCR-4(r4)
	lwz	r5,PT_REGS(r4)
	add	r5,r5,r6
	lwz	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
	li	r10,MSR_FP|MSR_FE0|MSR_FE1
	andc	r4,r4,r10		/* disable FP for previous task */
	stw	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
1:
#endif /* CONFIG_SMP */
	/* enable use of FP after return */
	mfspr	r5,SPRN_SPRG3		/* current task's THREAD (phys) */
	lwz	r4,THREAD_FPEXC_MODE(r5)
	ori	r9,r9,MSR_FP		/* enable FP for current */
	or	r9,r9,r4
	lfd	fr0,THREAD_FPSCR-4(r5)
	mtfsf	0xff,fr0
	REST_32FPRS(0, r5)
#ifndef CONFIG_SMP
	subi	r4,r5,THREAD
	sub	r4,r4,r6
	stw	r4,last_task_used_math@l(r3)
#endif /* CONFIG_SMP */
	/* restore registers and return */
	/* we haven't used ctr or xer or lr */
	b	fast_exception_return

/*
 * FP unavailable trap from kernel - print a message, but let
 * the task use FP in the kernel until it returns to user mode.
 */
 	.globl	KernelFP
KernelFP:
	lwz	r3,_MSR(r1)
	ori	r3,r3,MSR_FP
	stw	r3,_MSR(r1)		/* enable use of FP after return */
	lis	r3,86f@h
	ori	r3,r3,86f@l
	mr	r4,r2			/* current */
	lwz	r5,_NIP(r1)
	bl	printk
	b	ret_from_except
86:	.string	"floating point used in kernel (task=%p, pc=%x)\n"
	.align	4,0

/*
 * giveup_fpu(tsk)
 * Disable FP for the task given as the argument,
 * and save the floating-point registers in its thread_struct.
 * Enables the FPU for use in the kernel on return.
 */
	.globl	giveup_fpu
giveup_fpu:
	mfmsr	r5
	ori	r5,r5,MSR_FP
	SYNC_601
	ISYNC_601
	MTMSRD(r5)			/* enable use of fpu now */
	SYNC_601
	isync
	cmpwi	0,r3,0
	beqlr-				/* if no previous owner, done */
	addi	r3,r3,THREAD	        /* want THREAD of task */
	lwz	r5,PT_REGS(r3)
	cmpwi	0,r5,0
	SAVE_32FPRS(0, r3)
	mffs	fr0
	stfd	fr0,THREAD_FPSCR-4(r3)
	beq	1f
	lwz	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
	li	r3,MSR_FP|MSR_FE0|MSR_FE1
	andc	r4,r4,r3		/* disable FP for previous task */
	stw	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
1:
#ifndef CONFIG_SMP
	li	r5,0
	lis	r4,last_task_used_math@ha
	stw	r5,last_task_used_math@l(r4)
#endif /* CONFIG_SMP */
	blr
