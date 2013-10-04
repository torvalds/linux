/*
 * linux/arch/nios2/kernel/entry.S
 *
 * Copyright (C) 2013  Altera Corporation
 * Copyright (C) 2009, Wind River Systems Inc
 *
 * Implemented by fredrik.markstrom@gmail.com and ivarholmqvist@gmail.com
 *
 * Based on:
 *
 * linux/arch/nios2/kernel/entry.S
 *
 *  Copyright (C) 1999-2002, Greg Ungerer (gerg@snapgear.com)
 *  Copyright (C) 1998  D. Jeff Dionne <jeff@lineo.ca>,
 *                      Kenneth Albanowski <kjahds@kjahds.com>,
 *  Copyright (C) 2000  Lineo Inc. (www.lineo.com)
 *  Copyright (C) 2004  Microtronix Datacom Ltd.
 *
 * Based on:
 *
 *  linux/arch/m68knommu/kernel/entry.S
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Linux/m68k support by Hamish Macdonald
 *
 * 68060 fixes by Jesper Skov
 * ColdFire support by Greg Ungerer (gerg@snapgear.com)
 * 5307 fixes by David W. Miller
 * linux 2.4 support David McCullough <davidm@snapgear.com>
 */

#include <linux/sys.h>
#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/asm-macros.h>
#include <asm/thread_info.h>
#include <asm/errno.h>
#include <asm/setup.h>
#include <asm/entry.h>
#include <asm/unistd.h>
#include <asm/processor.h>

.macro GET_THREAD_INFO reg
.if THREAD_SIZE & 0xffff0000
	andhi	\reg, sp, %hi(~(THREAD_SIZE-1))
.else
	addi	\reg, r0, %lo(~(THREAD_SIZE-1))
	and	\reg, \reg, sp
.endif
.endm

/* FIXME: Lots of these exceptions need to be handled */
.section .rodata
.align 4
exception_table:
	.word unhandled_exception	/* 0 - Reset */
	.word unhandled_exception	/* 1 - Processor-only Reset */
	.word external_interrupt	/* 2 - Interrupt */
	.word handle_trap		/* 3 - Trap Instruction */

	.word instruction_trap		/* 4 - Unimplemented instruction */
	.word handle_illegal		/* 5 - Illegal instruction */
	.word handle_unaligned		/* 6 - Misaligned data access */
	.word handle_unaligned		/* 7 - Misaligned destination address */

	.word handle_diverror		/* 8 - Division error */
	.word protection_exception_ba	/* 9 - Supervisor-only instr. address */
	.word protection_exception_instr /* 10 - Supervisor only instruction */
	.word protection_exception_ba	/* 11 - Supervisor only data address */

	.word unhandled_exception	/* 12 - Double TLB miss (data) */
	.word protection_exception_pte	/* 13 - TLB permission violation (x) */
	.word protection_exception_pte	/* 14 - TLB permission violation (r) */
	.word protection_exception_pte	/* 15 - TLB permission violation (w) */

	.word unhandled_exception	/* 16 - MPU region violation */

trap_table:
	.word	handle_system_call	/* 0  */
	.word	instruction_trap	/* 1  */
	.word	instruction_trap	/* 2  */
	.word	instruction_trap	/* 3  */
	.word	instruction_trap	/* 4  */
	.word	instruction_trap	/* 5  */
	.word	instruction_trap	/* 6  */
	.word	instruction_trap	/* 7  */
	.word	instruction_trap	/* 8  */
	.word	instruction_trap	/* 9  */
	.word	instruction_trap	/* 10 */
	.word	instruction_trap	/* 11 */
	.word	instruction_trap	/* 12 */
	.word	instruction_trap	/* 13 */
	.word	instruction_trap	/* 14 */
	.word	instruction_trap	/* 15 */
	.word	instruction_trap	/* 16 */
	.word	instruction_trap	/* 17 */
	.word	instruction_trap	/* 18 */
	.word	instruction_trap	/* 19 */
	.word	instruction_trap	/* 20 */
	.word	instruction_trap	/* 21 */
	.word	instruction_trap	/* 22 */
	.word	instruction_trap	/* 23 */
	.word	instruction_trap	/* 24 */
	.word	instruction_trap	/* 25 */
	.word	instruction_trap	/* 26 */
	.word	instruction_trap	/* 27 */
	.word	instruction_trap	/* 28 */
	.word	instruction_trap	/* 29 */
#ifdef CONFIG_KGDB
	.word	handle_kgdb_breakpoint	/* 30 KGDB breakpoint */
#else
	.word	instruction_trap	// 30
#endif
	.word	handle_breakpoint	// 31

.text
.set noat
.set nobreak

ENTRY(inthandler)
	SAVE_ALL
	/* Clear EH bit before we get a new excpetion in the kernel
	 * and after we have saved it to the exception frame. This is done
	 * wheter it's trap, tlb-miss or interrupt. If we don't do this
	 * estatus is not updated the next exception.
	 */
	rdctl	r24, status
	movi	r9, %lo(~STATUS_EH)
	and	r24, r24, r9
	wrctl	status, r24

	/* Read cause and vector and branch to the associated handler */
	mov	r4, sp
	rdctl	r5, exception
	movia	r9, exception_table
	add	r24, r9, r5
	ldw	r24, 0(r24)
	jmp	r24


/***********************************************************************
 * Handle traps
 ***********************************************************************
 */
ENTRY(handle_trap)
	ldw	r24, -4(ea)	/* instruction that caused the exception */
	srli	r24, r24, 4
	andi	r24, r24, 0x7c
	movia	r9,trap_table
	add	r24, r24, r9
	ldw	r24, 0(r24)
	jmp	r24


/***********************************************************************
 * Handle system calls
 ***********************************************************************
 */
ENTRY(handle_system_call)
	/* Enable interrupts */
	rdctl	r10, status
	ori	r10, r10, STATUS_PIE
	wrctl	status, r10

	/* Reload registers destroyed by common code. */
	ldw	r4, PT_R4(sp)
	ldw	r5, PT_R5(sp)

	/* Check that the requested system call is within limits */
	movui	r1, NR_syscalls
	bgeu	r2, r1, ret_invsyscall
	slli	r1, r2, 2
	movhi	r11, %hiadj(sys_call_table)
	add	r1, r1, r11
	ldw	r1, %lo(sys_call_table)(r1)
	beq	r1, r0, ret_invsyscall

	/* Get thread info pointer */
	movi	r11, %lo(0xfffff000)
	and	r11, sp, r11
	ldw	r11, TI_FLAGS(r11)

	/* If someone is ptrace:ing us, take the long way. */
	BTBNZ	r11, r11, TIF_SYSCALL_TRACE, traced_system_call

	/* Execute the system call */
	callr	r1

	/* If the syscall returns a negative result:
	 *   Set r7 to 1 to indicate error,
	 *   Negate r2 to get a positive error code
	 * If the syscall returns zero or a positive value:
	 *   Set r7 to 0.
	 * The sigreturn system calls will skip the code below by
	 * adding to register ra. To avoid destroying registers
	 *
	 * FIXME: We probably need an orig_r7
	 */
translate_rc_and_ret:
	movi	r1, 0
	bge	r2, zero, 3f
	sub	r2, zero, r2
	movi	r1, 1
3:
	stw	r2, PT_R2(sp)
	stw	r1, PT_R7(sp)
end_translate_rc_and_ret:

ret_from_exception:
	ldw	r1, PT_ESTATUS(sp)
	/* if so, skip resched, signals */
	TSTBNZ	r1, r1, ESTATUS_EU, Luser_return

restore_all:
	rdctl	r10, status			/* disable intrs */
	andi	r10, r10, %lo(~STATUS_PIE)
	wrctl	status, r10
	RESTORE_ALL
	eret

	/* If the syscall number was invalid return ENOSYS */
ret_invsyscall:
	movi	r2, -ENOSYS
	br	translate_rc_and_ret

	/* This implements the same as above, except it calls
	 * syscall_trace before and after the syscall in order
	 * for utilities like strace and gdb to work.
	 */
traced_system_call:
	SAVE_SWITCH_STACK
	call	syscall_trace
	RESTORE_SWITCH_STACK

	/* r2 and r7-r9 might be destroyed  by syscall_trace and we need to
	 * restore them before calling our syscall.
	 */
	ldw	r2, PT_R2(sp)
	ldw	r4, PT_R4(sp)
	ldw	r5, PT_R5(sp)
	ldw	r6, PT_R6(sp)
	ldw	r7, PT_R7(sp)
	ldw	r8, PT_R8(sp)
	ldw	r9, PT_R9(sp)

	/* Fetch the syscall function, we don't need to check the boundaries
	 * since this is already done.
	 */
	slli	r1, r2, 2
	movhi	r11,%hiadj(sys_call_table)
	add	r1, r1, r11
	ldw	r1, %lo(sys_call_table)(r1)

	callr	r1

	/* If the syscall returns a negative result:
	 *   Set r7 to 1 to indicate error,
	 *   Negate r2 to get a positive error code
	 * If the syscall returns zero or a positive value:
	 *   Set r7 to 0.
	 * The sigreturn system calls will skip the code below by
	 * adding to register ra. To avoid destroying registers
	 *
	 * FIXME: We probably need an orig_r7
	 */
translate_rc_and_ret2:
	movi	r1, 0
	bge	r2, zero, 4f
	sub	r2, zero, r2
	movi	r1, 1
4:
	stw	r2, PT_R2(sp)
	stw	r1, PT_R7(sp)
end_translate_rc_and_ret2:
	SAVE_SWITCH_STACK
	call	syscall_trace
	RESTORE_SWITCH_STACK
	br	ret_from_exception

Luser_return:
	GET_THREAD_INFO	r11			/* get thread_info pointer */
	ldw	r10, TI_FLAGS(r11)		/* get thread_info->flags */
	ANDI32	r11, r10, _TIF_WORK_MASK
	beq	r11, r0, restore_all		/* Nothing to do */
	BTBZ	r1, r10, TIF_NEED_RESCHED, Lsignal_return

	/* Reschedule work */
	call	schedule
	br	ret_from_exception

Lsignal_return:
	ANDI32	r1, r10, _TIF_SIGPENDING | _TIF_NOTIFY_RESUME
	beq	r1, r0, restore_all
	mov	r4, sp			/* pt_regs */
	SAVE_SWITCH_STACK
	mov	r5, r0			/* oldset = 0 */
	movi	r6, 1			/* in_syscall = 1 */
	call	do_notify_resume
	RESTORE_SWITCH_STACK
	br	restore_all



/***********************************************************************
 * Handle external interrupts.
 ***********************************************************************
 */
/*
 * This is the generic interrupt handler (for all hardware interrupt
 * sources). It figures out the vector number and calls the appropriate
 * interrupt service routine directly.
 */
external_interrupt:
	rdctl	r12, ipending
	rdctl	r9, ienable
	and	r12, r12, r9
	/* skip if no interrupt is pending */
	beq	r12, r0, ret_from_interrupt

	movi	r24, -1
	stw	r24, PT_ORIG_R2(sp)

	/*
	 * Process an external hardware interrupt.
	 */

	addi	ea, ea, -4	/* re-issue the interrupted instruction */
	stw	ea, PT_EA(sp)
2:	movi	r4, %lo(-1)	/* Start from bit position 0,
					highest priority */
				/* This is the IRQ # for handler call */
1:	andi	r10, r12, 1	/* Isolate bit we are interested in */
	srli	r12, r12, 1	/* shift count is costly without hardware
					multiplier */
	addi	r4, r4, 1
	beq	r10, r0, 1b
	mov	r5, sp		/* Setup pt_regs pointer for handler call */
	call	do_IRQ
	rdctl	r12, ipending	/* check again if irq still pending */
	rdctl	r9, ienable	/* Isolate possible interrupts */
	and	r12, r12, r9
	bne	r12, r0, 2b
	/* br	ret_from_interrupt */ /* fall throught to ret_from_interrupt */

ENTRY(ret_from_interrupt)
	ldw	r1, PT_ESTATUS(sp)	/* check if returning to kernel */
	TSTBNZ	r1, r1, ESTATUS_EU, Luser_return

#ifdef CONFIG_PREEMPT
	GET_THREAD_INFO	r1
	ldw	r4, TI_PREEMPT_COUNT(r1)
	bne	r4, r0, restore_all

need_resched:
	ldw	r4, TI_FLAGS(r1)		/* ? Need resched set */
	BTBZ	r10, r4, TIF_NEED_RESCHED, restore_all
	ldw	r4, PT_ESTATUS(sp)	/* ? Interrupts off */
	andi	r10, r4, ESTATUS_EPIE
	beq	r10, r0, restore_all
	movia	r4, PREEMPT_ACTIVE
	stw	r4, TI_PREEMPT_COUNT(r1)
	rdctl	r10, status		/* enable intrs again */
	ori	r10, r10 ,STATUS_PIE
	wrctl	status, r10
	PUSH	r1
	call	schedule
	POP	r1
	mov	r4, r0
	stw	r4, TI_PREEMPT_COUNT(r1)
	rdctl	r10, status		/* disable intrs */
	andi	r10, r10, %lo(~STATUS_PIE)
	wrctl	status, r10
	br	need_resched
#else
	br	restore_all
#endif

/***********************************************************************
 * Syscalls implemented in assembly
 ***********************************************************************
 */

ENTRY(sys_nios2cmpxchg)

	/* r4 pointer to exchange variable */
	/* r5 old value */
	/* r6 new value */
	/* r8 - old interrupt status (assert this to enabled?) */
	/* r9 - temp */
	/* Disable interrupts (keep old status in r8) */
	rdctl	r8, status
	andi	r9, r8, %lo(~STATUS_PIE)
	wrctl	status, r9

	/* Make sure we skip the r2/r7 translation code when we return. */
	addi	ra, ra, (end_translate_rc_and_ret - translate_rc_and_ret)

ldw1:	ldw	r2, 0(r4)
	bne	r2, r5, .L6

	/* We had a match, store the new value */
stw1:	stw	r6, 0(r4)
.L6:
	/* Reenable interrupts */
	wrctl	status, r8

	/* Store return value */
	stw	r2, PT_R2(sp)
	/* Indicate everything is ok */
	movi	r2, 0
	stw	r2, PT_R7(sp)
	ret
fault:
	movi	r2, EFAULT
	stw	r2, PT_R2(sp)
	movi	r2, 1
	stw	r2, PT_R7(sp)
	wrctl	status, r8
	ret

	/* setup the exception table */
	.section __ex_table, "a"
	.word ldw1, fault
	.word stw1, fault
	.previous



/***********************************************************************
 * A few syscall wrappers
 ***********************************************************************
 */

ENTRY(__sys_fork)
	SAVE_SWITCH_STACK
	call sys_fork
	RESTORE_SWITCH_STACK
	ret

ENTRY(__sys_vfork)
	SAVE_SWITCH_STACK
	call sys_vfork
	RESTORE_SWITCH_STACK
	ret

ENTRY(__sys_clone)
	mov	r4,sp
	SAVE_SWITCH_STACK
	call	nios2_clone
	RESTORE_SWITCH_STACK
	ret

ENTRY(sys_sigreturn)
	mov	r4, sp
	SAVE_SWITCH_STACK
	call	do_sigreturn
	RESTORE_SWITCH_STACK
	addi	ra, ra, (end_translate_rc_and_ret - translate_rc_and_ret)
	ret

ENTRY(sys_rt_sigreturn)
	SAVE_SWITCH_STACK
	mov	r4, sp
	call	do_rt_sigreturn
	RESTORE_SWITCH_STACK
	addi	ra, ra, (end_translate_rc_and_ret - translate_rc_and_ret)
	ret

/***********************************************************************
 * A few other wrappers and stubs
 ***********************************************************************
 */
protection_exception_pte:
	rdctl	r6, pteaddr
	slli	r6, r6, 10
	call	do_page_fault
	br	ret_from_exception

protection_exception_ba:
	rdctl	r6, badaddr
	call	do_page_fault
	br	ret_from_exception

protection_exception_instr:
	call	handle_supervisor_instr
	br	ret_from_exception

handle_breakpoint:
	call	breakpoint_c
	br	ret_from_exception

#ifdef CONFIG_ALIGNMENT_TRAP
handle_unaligned:
	SAVE_SWITCH_STACK
	call	handle_unaligned_c
	RESTORE_SWITCH_STACK
	br	ret_from_exception
#else
handle_unaligned:
	call	handle_unaligned_c
	br	ret_from_exception
#endif

handle_illegal:
	call	handle_illegal_c
	br	ret_from_exception

handle_diverror:
	call	handle_diverror_c
	br	ret_from_exception

#ifdef CONFIG_KGDB
handle_kgdb_breakpoint:
	call	kgdb_breakpoint_c
	br	ret_from_exception
#endif

/*
 * Beware - when entering resume, prev (the current task) is
 * in r4, next (the new task) is in r5, don't change these
 * registers.
 */
ENTRY(resume)

	rdctl	r7, status			/* save thread status reg */
	stw	r7, TASK_THREAD + THREAD_KPSR(r4)

	andi	r7, r7, %lo(~STATUS_PIE)	/* disable interrupts */
	wrctl	status, r7

	SAVE_SWITCH_STACK
	stw	sp, TASK_THREAD + THREAD_KSP(r4)/* save kernel stack pointer */
	ldw	sp, TASK_THREAD + THREAD_KSP(r5)/* restore new thread stack */
	movia	r24, _current_thread		/* save thread */
	GET_THREAD_INFO r1
	stw	r1, 0(r24)
	RESTORE_SWITCH_STACK

	ldw	r7, TASK_THREAD + THREAD_KPSR(r5)/* restore thread status reg */
	wrctl	status, r7
	ret

ENTRY(ret_from_fork)
	call	schedule_tail
	br	ret_from_exception

ENTRY(ret_from_kernel_thread)
	call	schedule_tail
	mov	r4,r17	/* arg */
	callr	r16	/* function */
	br	ret_from_exception
