// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/linkage.h>
#include <asm/unistd.h>
#include <asm/assembler.h>
#include <asm/nds32.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/current.h>
#include <asm/fpu.h>



#ifdef CONFIG_HWZOL
	.macro pop_zol
	mtusr	$r14, $LB
	mtusr	$r15, $LE
	mtusr	$r16, $LC
	.endm
#endif

	.macro	restore_user_regs_first
	setgie.d
	isb
#if defined(CONFIG_FPU)
	addi    $sp, $sp, OSP_OFFSET
	lmw.adm $r12, [$sp], $r25, #0x0
	sethi   $p0, hi20(has_fpu)
	lbsi 	$p0, [$p0+lo12(has_fpu)]
	beqz	$p0, 2f
	mtsr    $r25, $FUCOP_CTL
2:
#else
	addi	$sp, $sp, FUCOP_CTL_OFFSET
	lmw.adm $r12, [$sp], $r24, #0x0
#endif
	mtsr	$r12, $SP_USR
	mtsr	$r13, $IPC
#ifdef CONFIG_HWZOL
	pop_zol
#endif
	mtsr	$r19, $PSW
	mtsr	$r20, $IPSW
	mtsr    $r21, $P_IPSW
	mtsr	$r22, $P_IPC
	mtsr	$r23, $P_P0
	mtsr	$r24, $P_P1
	lmw.adm $sp, [$sp], $sp, #0xe
	.endm

	.macro	restore_user_regs_last
	pop	$p0
	cmovn	$sp, $p0, $p0

	iret
	nop

	.endm

	.macro	restore_user_regs
	restore_user_regs_first
	lmw.adm $r0, [$sp], $r25, #0x0
	addi	$sp, $sp, OSP_OFFSET
	restore_user_regs_last
	.endm

	.macro	fast_restore_user_regs
	restore_user_regs_first
	lmw.adm $r1, [$sp], $r25, #0x0
	addi	$sp, $sp, OSP_OFFSET-4
	restore_user_regs_last
	.endm

#ifdef CONFIG_PREEMPT
	.macro	preempt_stop
	.endm
#else
	.macro	preempt_stop
	setgie.d
	isb
	.endm
#define	resume_kernel	no_work_pending
#endif

ENTRY(ret_from_exception)
	preempt_stop
ENTRY(ret_from_intr)

/*
 * judge Kernel or user mode
 *
 */
	lwi	$p0, [$sp+(#IPSW_OFFSET)]	! Check if in nested interrupt
	andi	$p0, $p0, #PSW_mskINTL
	bnez	$p0, resume_kernel		! done with iret
	j	resume_userspace


/*
 * This is the fast syscall return path.  We do as little as
 * possible here, and this includes saving $r0 back into the SVC
 * stack.
 * fixed: tsk - $r25, syscall # - $r7, syscall table pointer - $r8
 */
ENTRY(ret_fast_syscall)
	gie_disable
	lwi	$r1, [tsk+#TSK_TI_FLAGS]
	andi	$p1, $r1, #_TIF_WORK_MASK
	bnez	$p1, fast_work_pending
	fast_restore_user_regs			! iret

/*
 * Ok, we need to do extra processing,
 * enter the slow path returning from syscall, while pending work.
 */
fast_work_pending:
	swi	$r0, [$sp+(#R0_OFFSET)]		! what is different from ret_from_exception
work_pending:
	andi	$p1, $r1, #_TIF_NEED_RESCHED
	bnez	$p1, work_resched

	andi	$p1, $r1, #_TIF_SIGPENDING|#_TIF_NOTIFY_RESUME
	beqz	$p1, no_work_pending

	move	$r0, $sp			! 'regs'
	gie_enable
	bal	do_notify_resume
	b       ret_slow_syscall
work_resched:
	bal	schedule			! path, return to user mode

/*
 * "slow" syscall return path.
 */
ENTRY(resume_userspace)
ENTRY(ret_slow_syscall)
	gie_disable
	lwi	$p0, [$sp+(#IPSW_OFFSET)]	! Check if in nested interrupt
	andi	$p0, $p0, #PSW_mskINTL
	bnez	$p0, no_work_pending		! done with iret
	lwi	$r1, [tsk+#TSK_TI_FLAGS]
	andi	$p1, $r1, #_TIF_WORK_MASK
	bnez	$p1, work_pending		! handle work_resched, sig_pend

no_work_pending:
#ifdef CONFIG_TRACE_IRQFLAGS
	lwi	$p0, [$sp+(#IPSW_OFFSET)]
	andi	$p0, $p0, #0x1
	la	$r10, __trace_hardirqs_off
	la	$r9, __trace_hardirqs_on
	cmovz	$r9, $p0, $r10
	jral	$r9
#endif
	restore_user_regs			! return from iret


/*
 * preemptive kernel
 */
#ifdef CONFIG_PREEMPT
resume_kernel:
	gie_disable
	lwi	$t0, [tsk+#TSK_TI_PREEMPT]
	bnez	$t0, no_work_pending
need_resched:
	lwi	$t0, [tsk+#TSK_TI_FLAGS]
	andi	$p1, $t0, #_TIF_NEED_RESCHED
	beqz	$p1, no_work_pending

	lwi	$t0, [$sp+(#IPSW_OFFSET)]	! Interrupts off?
	andi	$t0, $t0, #1
	beqz	$t0, no_work_pending

	jal	preempt_schedule_irq
	b	need_resched
#endif

/*
 * This is how we return from a fork.
 */
ENTRY(ret_from_fork)
	bal	schedule_tail
	beqz	$r6, 1f				! r6 stores fn for kernel thread
	move	$r0, $r7			! prepare kernel thread arg
	jral	$r6
1:
	lwi	$r1, [tsk+#TSK_TI_FLAGS]		! check for syscall tracing
	andi	$p1, $r1, #_TIF_WORK_SYSCALL_LEAVE	! are we tracing syscalls?
	beqz	$p1, ret_slow_syscall
	move    $r0, $sp
	bal	syscall_trace_leave
	b	ret_slow_syscall
