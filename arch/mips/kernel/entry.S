/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 2000, 2001, 2003 Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2001 MIPS Technologies, Inc.
 */

#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>
#include <asm/isadep.h>
#include <asm/thread_info.h>
#include <asm/war.h>
#ifdef CONFIG_MIPS_MT_SMTC
#include <asm/mipsmtregs.h>
#endif

#ifndef CONFIG_PREEMPT
#define resume_kernel	restore_all
#else
#define __ret_from_irq	ret_from_exception
#endif

	.text
	.align	5
#ifndef CONFIG_PREEMPT
FEXPORT(ret_from_exception)
	local_irq_disable			# preempt stop
	b	__ret_from_irq
#endif
FEXPORT(ret_from_irq)
	LONG_S	s0, TI_REGS($28)
FEXPORT(__ret_from_irq)
	LONG_L	t0, PT_STATUS(sp)		# returning to kernel mode?
	andi	t0, t0, KU_USER
	beqz	t0, resume_kernel

resume_userspace:
	local_irq_disable		# make sure we dont miss an
					# interrupt setting need_resched
					# between sampling and return
	LONG_L	a2, TI_FLAGS($28)	# current->work
	andi	t0, a2, _TIF_WORK_MASK	# (ignoring syscall_trace)
	bnez	t0, work_pending
	j	restore_all

#ifdef CONFIG_PREEMPT
resume_kernel:
	local_irq_disable
	lw	t0, TI_PRE_COUNT($28)
	bnez	t0, restore_all
need_resched:
	LONG_L	t0, TI_FLAGS($28)
	andi	t1, t0, _TIF_NEED_RESCHED
	beqz	t1, restore_all
	LONG_L	t0, PT_STATUS(sp)		# Interrupts off?
	andi	t0, 1
	beqz	t0, restore_all
	jal	preempt_schedule_irq
	b	need_resched
#endif

FEXPORT(ret_from_fork)
	jal	schedule_tail		# a0 = struct task_struct *prev

FEXPORT(syscall_exit)
	local_irq_disable		# make sure need_resched and
					# signals dont change between
					# sampling and return
	LONG_L	a2, TI_FLAGS($28)	# current->work
	li	t0, _TIF_ALLWORK_MASK
	and	t0, a2, t0
	bnez	t0, syscall_exit_work

restore_all:				# restore full frame
#ifdef CONFIG_MIPS_MT_SMTC
#ifdef CONFIG_MIPS_MT_SMTC_IM_BACKSTOP
/* Re-arm any temporarily masked interrupts not explicitly "acked" */
	mfc0	v0, CP0_TCSTATUS
	ori	v1, v0, TCSTATUS_IXMT
	mtc0	v1, CP0_TCSTATUS
	andi	v0, TCSTATUS_IXMT
	_ehb
	mfc0	t0, CP0_TCCONTEXT
	DMT	9				# dmt t1
	jal	mips_ihb
	mfc0	t2, CP0_STATUS
	andi	t3, t0, 0xff00
	or	t2, t2, t3
	mtc0	t2, CP0_STATUS
	_ehb
	andi	t1, t1, VPECONTROL_TE
	beqz	t1, 1f
	EMT
1:
	mfc0	v1, CP0_TCSTATUS
	/* We set IXMT above, XOR should clear it here */
	xori	v1, v1, TCSTATUS_IXMT
	or	v1, v0, v1
	mtc0	v1, CP0_TCSTATUS
	_ehb
	xor	t0, t0, t3
	mtc0	t0, CP0_TCCONTEXT
#endif /* CONFIG_MIPS_MT_SMTC_IM_BACKSTOP */
/* Detect and execute deferred IPI "interrupts" */
	LONG_L	s0, TI_REGS($28)
	LONG_S	sp, TI_REGS($28)
	jal	deferred_smtc_ipi
	LONG_S	s0, TI_REGS($28)
#endif /* CONFIG_MIPS_MT_SMTC */
	.set	noat
	RESTORE_TEMP
	RESTORE_AT
	RESTORE_STATIC
restore_partial:		# restore partial frame
#ifdef CONFIG_TRACE_IRQFLAGS
	SAVE_STATIC
	SAVE_AT
	SAVE_TEMP
	LONG_L	v0, PT_STATUS(sp)
#if defined(CONFIG_CPU_R3000) || defined(CONFIG_CPU_TX39XX)
	and	v0, ST0_IEP
#else
	and	v0, ST0_IE
#endif
	beqz	v0, 1f
	jal	trace_hardirqs_on
	b	2f
1:	jal	trace_hardirqs_off
2:
	RESTORE_TEMP
	RESTORE_AT
	RESTORE_STATIC
#endif
	RESTORE_SOME
	RESTORE_SP_AND_RET
	.set	at

work_pending:
	andi	t0, a2, _TIF_NEED_RESCHED # a2 is preloaded with TI_FLAGS
	beqz	t0, work_notifysig
work_resched:
	jal	schedule

	local_irq_disable		# make sure need_resched and
					# signals dont change between
					# sampling and return
	LONG_L	a2, TI_FLAGS($28)
	andi	t0, a2, _TIF_WORK_MASK	# is there any work to be done
					# other than syscall tracing?
	beqz	t0, restore_all
	andi	t0, a2, _TIF_NEED_RESCHED
	bnez	t0, work_resched

work_notifysig:				# deal with pending signals and
					# notify-resume requests
	move	a0, sp
	li	a1, 0
	jal	do_notify_resume	# a2 already loaded
	j	resume_userspace

FEXPORT(syscall_exit_partial)
	local_irq_disable		# make sure need_resched doesn't
					# change between and return
	LONG_L	a2, TI_FLAGS($28)	# current->work
	li	t0, _TIF_ALLWORK_MASK
	and	t0, a2
	beqz	t0, restore_partial
	SAVE_STATIC
syscall_exit_work:
	LONG_L	t0, PT_STATUS(sp)		# returning to kernel mode?
	andi	t0, t0, KU_USER
	beqz	t0, resume_kernel
	li	t0, _TIF_WORK_SYSCALL_EXIT
	and	t0, a2			# a2 is preloaded with TI_FLAGS
	beqz	t0, work_pending	# trace bit set?
	local_irq_enable		# could let syscall_trace_leave()
					# call schedule() instead
	move	a0, sp
	jal	syscall_trace_leave
	b	resume_userspace

#if defined(CONFIG_CPU_MIPSR2) || defined(CONFIG_MIPS_MT)

/*
 * MIPS32R2 Instruction Hazard Barrier - must be called
 *
 * For C code use the inline version named instruction_hazard().
 */
LEAF(mips_ihb)
	.set	mips32r2
	jr.hb	ra
	nop
	END(mips_ihb)

#endif /* CONFIG_CPU_MIPSR2 or CONFIG_MIPS_MT */
