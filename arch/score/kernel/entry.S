/*
 * arch/score/kernel/entry.S
 *
 * Score Processor version.
 *
 * Copyright (C) 2009 Sunplus Core Technology Co., Ltd.
 *  Chen Liqin <liqin.chen@sunplusct.com>
 *  Lennox Wu <lennox.wu@sunplusct.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/linkage.h>

#include <asm/asmmacro.h>
#include <asm/thread_info.h>
#include <asm/unistd.h>

/*
 * disable interrupts.
 */
.macro	disable_irq
	mfcr	r8, cr0
	srli	r8, r8, 1
	slli	r8, r8, 1
	mtcr	r8, cr0
	nop
	nop
	nop
	nop
	nop
.endm

/*
 * enable interrupts.
 */
.macro	enable_irq
	mfcr	r8, cr0
	ori	r8, 1
	mtcr	r8, cr0
	nop
	nop
	nop
	nop
	nop
.endm

__INIT
ENTRY(debug_exception_vector)
	nop!
	nop!
	nop!
	nop!
	nop!
	nop!
	nop!
	nop!

ENTRY(general_exception_vector)			# should move to addr 0x200
	j	general_exception
	nop!
	nop!
	nop!
	nop!
	nop!
	nop!

ENTRY(interrupt_exception_vector)		# should move to addr 0x210
	j	interrupt_exception
	nop!
	nop!
	nop!
	nop!
	nop!
	nop!

	.section ".text", "ax"
	.align	2;
general_exception:
	mfcr	r31, cr2
	nop
	la	r30, exception_handlers
	andi	r31, 0x1f			# get ecr.exc_code
	slli	r31, r31, 2
	add	r30, r30, r31
	lw	r30, [r30]
	br	r30

interrupt_exception:
	SAVE_ALL
	mfcr	r4, cr2
	nop
	lw	r16, [r28, TI_REGS]
	sw	r0, [r28, TI_REGS]
	la	r3, ret_from_irq
	srli	r4, r4, 18			# get ecr.ip[7:2], interrupt No.
	mv	r5, r0
	j	do_IRQ

ENTRY(handle_nmi)				# NMI #1
	SAVE_ALL
	mv	r4, r0
	la	r8, nmi_exception_handler
	brl	r8
	j	restore_all

ENTRY(handle_adelinsn)				# AdEL-instruction #2
	SAVE_ALL
	mfcr	r8, cr6
	nop
	nop
	sw	r8, [r0, PT_EMA]
	mv	r4, r0
	la	r8, do_adelinsn
	brl	r8
	mv	r4, r0
	j	ret_from_exception
	nop

ENTRY(handle_ibe)				# BusEL-instruction #5
	SAVE_ALL
	mv	r4, r0
	la	r8, do_be
	brl	r8
	mv	r4, r0
	j	ret_from_exception
	nop

ENTRY(handle_pel)				# P-EL #6
	SAVE_ALL
	mv	r4, r0
	la	r8, do_pel
	brl	r8
	mv	r4, r0
	j	ret_from_exception
	nop

ENTRY(handle_ccu)				# CCU #8
	SAVE_ALL
	mv	r4, r0
	la	r8, do_ccu
	brl	r8
	mv	r4, r0
	j	ret_from_exception
	nop

ENTRY(handle_ri)				# RI #9
	SAVE_ALL
	mv	r4, r0
	la	r8, do_ri
	brl	r8
	mv	r4, r0
	j	ret_from_exception
	nop

ENTRY(handle_tr)				# Trap #10
	SAVE_ALL
	mv	r4, r0
	la	r8, do_tr
	brl	r8
	mv	r4, r0
	j	ret_from_exception
	nop

ENTRY(handle_adedata)				# AdES-instruction #12
	SAVE_ALL
	mfcr	r8, cr6
	nop
	nop
	sw	r8, [r0, PT_EMA]
	mv	r4, r0
	la	r8, do_adedata
	brl	r8
	mv	r4, r0
	j	ret_from_exception
	nop

ENTRY(handle_cee)				# CeE #16
	SAVE_ALL
	mv	r4, r0
	la	r8, do_cee
	brl	r8
	mv	r4, r0
	j	ret_from_exception
	nop

ENTRY(handle_cpe)				# CpE #17
	SAVE_ALL
	mv	r4, r0
	la	r8, do_cpe
	brl	r8
	mv	r4, r0
	j	ret_from_exception
	nop

ENTRY(handle_dbe)				# BusEL-data #18
	SAVE_ALL
	mv	r4, r0
	la	r8, do_be
	brl	r8
	mv	r4, r0
	j	ret_from_exception
	nop

ENTRY(handle_reserved)				# others
	SAVE_ALL
	mv	r4, r0
	la	r8, do_reserved
	brl	r8
	mv	r4, r0
	j	ret_from_exception
	nop

#ifndef CONFIG_PREEMPT
#define resume_kernel	restore_all
#else
#define __ret_from_irq	ret_from_exception
#endif

	.align	2
#ifndef CONFIG_PREEMPT
ENTRY(ret_from_exception)
	disable_irq			# preempt stop
	nop
	j	__ret_from_irq
	nop
#endif

ENTRY(ret_from_irq)
	sw	r16, [r28, TI_REGS]

ENTRY(__ret_from_irq)
	lw	r8, [r0, PT_PSR]	# returning to kernel mode?
	andri.c	r8, r8, KU_USER
	beq	resume_kernel

resume_userspace:
	disable_irq
	lw	r6, [r28, TI_FLAGS]	# current->work
	li	r8, _TIF_WORK_MASK
	and.c	r8, r8, r6		# ignoring syscall_trace
	bne	work_pending
	nop
	j	restore_all
	nop

#ifdef CONFIG_PREEMPT
resume_kernel:
	disable_irq
	lw	r8, [r28, TI_PRE_COUNT]
	cmpz.c	r8
	bne	r8, restore_all
need_resched:
	lw	r8, [r28, TI_FLAGS]
	andri.c	r9, r8, _TIF_NEED_RESCHED
	beq	restore_all
	lw	r8, [r28, PT_PSR]		# Interrupts off?
	andri.c	r8, r8, 1
	beq	restore_all
	bl	preempt_schedule_irq
	nop
	j	need_resched
	nop
#endif

ENTRY(ret_from_kernel_thread)
	bl	schedule_tail			# r4=struct task_struct *prev
	nop
	mv	r4, r13
	brl	r12
	j	syscall_exit

ENTRY(ret_from_fork)
	bl	schedule_tail			# r4=struct task_struct *prev

ENTRY(syscall_exit)
	nop
	disable_irq
	lw	r6, [r28, TI_FLAGS]		# current->work
	li	r8, _TIF_WORK_MASK
	and.c	r8, r6, r8
	bne	syscall_exit_work

ENTRY(restore_all)					# restore full frame
	RESTORE_ALL_AND_RET

work_pending:
	andri.c	r8, r6, _TIF_NEED_RESCHED # r6 is preloaded with TI_FLAGS
	beq	work_notifysig
work_resched:
	bl	schedule
	nop
	disable_irq
	lw	r6, [r28, TI_FLAGS]
	li	r8, _TIF_WORK_MASK
	and.c	r8, r6, r8	# is there any work to be done
				# other than syscall tracing?
	beq	restore_all
	andri.c	r8, r6, _TIF_NEED_RESCHED
	bne	work_resched

work_notifysig:
	mv	r4, r0
	li	r5, 0
	bl	do_notify_resume	# r6 already loaded
	nop
	j	resume_userspace
	nop

ENTRY(syscall_exit_work)
	li	r8, _TIF_SYSCALL_TRACE
	and.c	r8, r8, r6		# r6 is preloaded with TI_FLAGS
	beq	work_pending		# trace bit set?
	nop
	enable_irq
	mv	r4, r0
	li	r5, 1
	bl	do_syscall_trace
	nop
	b	resume_userspace
	nop

.macro	save_context	reg
	sw	r12, [\reg, THREAD_REG12];
	sw	r13, [\reg, THREAD_REG13];
	sw	r14, [\reg, THREAD_REG14];
	sw	r15, [\reg, THREAD_REG15];
	sw	r16, [\reg, THREAD_REG16];
	sw	r17, [\reg, THREAD_REG17];
	sw	r18, [\reg, THREAD_REG18];
	sw	r19, [\reg, THREAD_REG19];
	sw	r20, [\reg, THREAD_REG20];
	sw	r21, [\reg, THREAD_REG21];
	sw	r29, [\reg, THREAD_REG29];
	sw	r2, [\reg, THREAD_REG2];
	sw	r0, [\reg, THREAD_REG0]
.endm

.macro	restore_context	reg
	lw	r12, [\reg, THREAD_REG12];
	lw	r13, [\reg, THREAD_REG13];
	lw	r14, [\reg, THREAD_REG14];
	lw	r15, [\reg, THREAD_REG15];
	lw	r16, [\reg, THREAD_REG16];
	lw	r17, [\reg, THREAD_REG17];
	lw	r18, [\reg, THREAD_REG18];
	lw	r19, [\reg, THREAD_REG19];
	lw	r20, [\reg, THREAD_REG20];
	lw	r21, [\reg, THREAD_REG21];
	lw	r29, [\reg, THREAD_REG29];
	lw	r0, [\reg, THREAD_REG0];
	lw	r2, [\reg, THREAD_REG2];
	lw	r3, [\reg, THREAD_REG3]
.endm

/*
 * task_struct *resume(task_struct *prev, task_struct *next,
 *			struct thread_info *next_ti)
 */
ENTRY(resume)
	mfcr	r9, cr0
	nop
	nop
	sw	r9, [r4, THREAD_PSR]
	save_context	r4
	sw	r3, [r4, THREAD_REG3]

	mv	r28, r6
	restore_context	r5
	mv	r8, r6
	addi	r8, KERNEL_STACK_SIZE
	subi	r8, 32
	la 	r9, kernelsp;
	sw 	r8, [r9];

	mfcr	r9, cr0
	ldis	r7, 0x00ff
	nop
	and	r9, r9, r7
	lw	r6, [r5, THREAD_PSR]
	not	r7, r7
	and	r6, r6, r7
	or	r6, r6, r9
	mtcr	r6, cr0
	nop; nop; nop; nop; nop
	br	r3

ENTRY(handle_sys)
	SAVE_ALL
	sw	r8, [r0, 16]		# argument 5 from user r8
	sw	r9, [r0, 20]		# argument 6 from user r9
	enable_irq

	sw	r4, [r0, PT_ORIG_R4]	#for restart syscall
	sw	r7, [r0, PT_ORIG_R7]	#for restart syscall
	sw	r27, [r0, PT_IS_SYSCALL] # it from syscall

	lw	r9, [r0, PT_EPC]	# skip syscall on return
	addi	r9, 4
	sw	r9, [r0, PT_EPC]

	cmpi.c	r27, __NR_syscalls 	# check syscall number
	bgeu	illegal_syscall

	slli	r8, r27, 2		# get syscall routine
	la	r11, sys_call_table
	add	r11, r11, r8
	lw	r10, [r11]		# get syscall entry

	cmpz.c	r10
	beq	illegal_syscall

	lw	r8, [r28, TI_FLAGS]
	li	r9, _TIF_SYSCALL_TRACE
	and.c	r8, r8, r9
	bne	syscall_trace_entry

	brl	r10			# Do The Real system call

	cmpi.c	r4, 0
	blt	1f
	ldi	r8, 0
	sw	r8, [r0, PT_R7]
	b 2f
1:
	cmpi.c	r4, -MAX_ERRNO - 1
	ble	2f
	ldi	r8, 0x1;
	sw	r8, [r0, PT_R7]
	neg	r4, r4
2:
	sw	r4, [r0, PT_R4]		# save result

syscall_return:
	disable_irq
	lw	r6, [r28, TI_FLAGS]	# current->work
	li	r8, _TIF_WORK_MASK
	and.c	r8, r6, r8
	bne	syscall_return_work
	j	restore_all

syscall_return_work:
	j	syscall_exit_work

syscall_trace_entry:
	mv	r16, r10
	mv	r4, r0
	li	r5, 0
	bl	do_syscall_trace

	mv	r8, r16
	lw	r4, [r0, PT_R4]		# Restore argument registers
	lw	r5, [r0, PT_R5]
	lw	r6, [r0, PT_R6]
	lw	r7, [r0, PT_R7]
	brl	r8

	li	r8, -MAX_ERRNO - 1
	sw	r8, [r0, PT_R7]		# set error flag

	neg	r4, r4			# error
	sw	r4, [r0, PT_R0]		# set flag for syscall
					# restarting
1:	sw	r4, [r0, PT_R2]		# result
	j	syscall_exit

illegal_syscall:
	ldi	r4, -ENOSYS		# error
	sw	r4, [r0, PT_ORIG_R4]
	sw	r4, [r0, PT_R4]
	ldi	r9, 1			# set error flag
	sw	r9, [r0, PT_R7]
	j	syscall_return

ENTRY(sys_rt_sigreturn)
	mv	r4, r0
	la	r8, score_rt_sigreturn
	br	r8
