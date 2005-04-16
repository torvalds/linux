/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 2000, 2001, 2003 Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2001 MIPS Technologies, Inc.
 */
#include <linux/config.h>

#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>
#include <asm/isadep.h>
#include <asm/thread_info.h>
#include <asm/war.h>

#ifdef CONFIG_PREEMPT
	.macro	preempt_stop reg=t0
	.endm
#else
	.macro	preempt_stop reg=t0
	local_irq_disable \reg
	.endm
#define resume_kernel	restore_all
#endif

	.text
	.align	5
FEXPORT(ret_from_exception)
	preempt_stop
FEXPORT(ret_from_irq)
	LONG_L	t0, PT_STATUS(sp)		# returning to kernel mode?
	andi	t0, t0, KU_USER
	beqz	t0, resume_kernel

FEXPORT(resume_userspace)
	local_irq_disable	t0	# make sure we dont miss an
					# interrupt setting need_resched
					# between sampling and return
	LONG_L	a2, TI_FLAGS($28)	# current->work
	andi	a2, _TIF_WORK_MASK	# (ignoring syscall_trace)
	bnez	a2, work_pending
	j	restore_all

#ifdef CONFIG_PREEMPT
ENTRY(resume_kernel)
	lw	t0, TI_PRE_COUNT($28)
	bnez	t0, restore_all
need_resched:
	LONG_L	t0, TI_FLAGS($28)
	andi	t1, t0, _TIF_NEED_RESCHED
	beqz	t1, restore_all
	LONG_L	t0, PT_STATUS(sp)		# Interrupts off?
	andi	t0, 1
	beqz	t0, restore_all
	li	t0, PREEMPT_ACTIVE
	sw	t0, TI_PRE_COUNT($28)
	local_irq_enable t0
	jal	schedule
	sw	zero, TI_PRE_COUNT($28)
	local_irq_disable t0
	b	need_resched
#endif

FEXPORT(ret_from_fork)
	jal	schedule_tail		# a0 = task_t *prev

FEXPORT(syscall_exit)
	local_irq_disable		# make sure need_resched and
					# signals dont change between
					# sampling and return
	LONG_L	a2, TI_FLAGS($28)	# current->work
	li	t0, _TIF_ALLWORK_MASK
	and	t0, a2, t0
	bnez	t0, syscall_exit_work

FEXPORT(restore_all)			# restore full frame
	.set	noat
	RESTORE_TEMP
	RESTORE_AT
	RESTORE_STATIC
FEXPORT(restore_partial)		# restore partial frame
	RESTORE_SOME
	RESTORE_SP_AND_RET
	.set	at

FEXPORT(work_pending)
	andi	t0, a2, _TIF_NEED_RESCHED
	beqz	t0, work_notifysig
work_resched:
	jal	schedule

	local_irq_disable t0		# make sure need_resched and
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
	j	restore_all

FEXPORT(syscall_exit_work_partial)
	SAVE_STATIC
FEXPORT(syscall_exit_work)
	LONG_L	t0, TI_FLAGS($28)
	li	t1, _TIF_SYSCALL_TRACE | _TIF_SYSCALL_AUDIT
	and	t0, t1
	beqz	t0, work_pending	# trace bit is set
	local_irq_enable		# could let do_syscall_trace()
					# call schedule() instead
	move	a0, sp
	li	a1, 1
	jal	do_syscall_trace
	b	resume_userspace

/*
 * Common spurious interrupt handler.
 */
	.text
	.align  5
LEAF(spurious_interrupt)
	/*
	 * Someone tried to fool us by sending an interrupt but we
	 * couldn't find a cause for it.
	 */
#ifdef CONFIG_SMP
	lui     t1, %hi(irq_err_count)
1:	ll      t0, %lo(irq_err_count)(t1)
	addiu   t0, 1
	sc      t0, %lo(irq_err_count)(t1)
#if R10000_LLSC_WAR
	beqzl	t0, 1b
#else
	beqz	t0, 1b
#endif
#else
	lui     t1, %hi(irq_err_count)
	lw      t0, %lo(irq_err_count)(t1)
	addiu   t0, 1
	sw      t0, %lo(irq_err_count)(t1)
#endif
	j	ret_from_irq
	END(spurious_interrupt)
