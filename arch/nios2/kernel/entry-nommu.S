/*
 * Copyright (C) 1999-2002, Greg Ungerer (gerg@snapgear.com)
 * Copyright (C) 1998  D. Jeff Dionne <jeff@lineo.ca>,
 *                     Kenneth Albanowski <kjahds@kjahds.com>,
 * Copyright (C) 2000  Lineo Inc. (www.lineo.com)
 * Copyright (C) 2004  Microtronix Datacom Ltd.
 * Copyright (C) 2013  Altera Corporation
 *
 * Based on entry.S from m68knommu.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
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
#include <asm/traps.h>
#include <asm/processor.h>

.macro GET_THREAD_INFO reg
.if THREAD_SIZE & 0xffff0000
	andhi	\reg, sp, %hi(~(THREAD_SIZE-1))
.else
	addi	\reg, r0, %lo(~(THREAD_SIZE-1))
	and	\reg, \reg, sp
.endif
.endm

.text
.set noat
.set nobreak

ENTRY(system_call)
/*	SAVE_ALL */
	rdctl	r10, status		/* enable intrs again */
	ori	r10, r10, STATUS_PIE
	wrctl	status, r10

	movi	r2, -ENOSYS
	stw	r2, PT_R2(sp)		/* default return value in r2 */
					/* original r2 is in orig_r2 */

	movui	r1, NR_syscalls
	bgeu	r3, r1, ret_from_exception
	slli	r1, r3, 2
	movhi	r11, %hiadj(sys_call_table)
	add	r1, r1, r11
	ldw	r1, %lo(sys_call_table)(r1)
	beq	r1, r0, ret_from_exception

	movi	r11, %lo(0xffffe000)	/* Get thread info pointer */
	and	r11, sp, r11
	ldw	r11, TI_FLAGS(r11)
	BTBNZ	r11, r11, TIF_SYSCALL_TRACE, 1f

	callr	r1
	stw	r2, PT_R2(sp)		/* save the return value */
	br	ret_from_exception
1:
	SAVE_SWITCH_STACK
	call	syscall_trace
	RESTORE_SWITCH_STACK
	/* wentao: restore r4-9, since they are trashed by syscall_trace */
	ldw	r4, PT_R4(sp)
	ldw	r5, PT_R5(sp)
	ldw	r6, PT_R6(sp)
	ldw	r7, PT_R7(sp)
	ldw	r8, PT_R8(sp)
	ldw	r9, PT_R9(sp)
	callr	r1
	stw	r2, PT_R2(sp)		/* save the return value */
	SAVE_SWITCH_STACK
	call	syscall_trace
	RESTORE_SWITCH_STACK

ret_from_exception:
	ldw	r1, PT_STATUS_EXTENSION(sp) /* check if returning to kernel */
	TSTBZ	r1, r1, PS_S, Luser_return /* if so, skip resched, signals */

restore_all:
	rdctl	r10, status			/* disable intrs */
	andi	r10, r10, %lo(~STATUS_PIE)
	wrctl	status, r10
	RESTORE_ALL
	eret

Luser_return:
	GET_THREAD_INFO	r24			/* get thread_info pointer */
	ldw	r10, TI_FLAGS(r24)		/* get thread_info->flags */
	ANDI32	r11, r10, _TIF_WORK_MASK
	beq	r11, r0, restore_all		/* Nothing to do */
	BTBZ	r1, r10, TIF_NEED_RESCHED, Lsignal_return

Lwork_resched:
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

/*
 * Handle software exceptions. Put here so external interrupts
 * can fall throught to ret_from_interrupt.
 */

software_exception:
	ldw	r24, -4(ea)	/* instruction that caused the exception */
	xorhi	r24, r24, 0x003b	/* upper half of trap opcode */
	xori	r24, r24, 0x683a	/* lower half of trap opcode */
	cmpeqi	r11, r24, 0x40	/* Check for imm=0x01 => breakpoint */
	bne	r11, r0, breakpoint
	bne	r24, r0, instruction_trap /* N - check for instruction trap */
	cmpeqi	r11, r2, TRAP_ID_SYSCALL	/* ? Is this a syscall */
	bne	r11, r0, system_call	/* Y - handle syscall */
	cmpeqi	r11, r2,63		/* ? Is this the old syscall number */
	bne	r11, r0, system_call /* Y - handle syscall to catch older apps*/
	br	restore_all	    /* N - everything else is ignored for now */

breakpoint:
	mov	r4, sp
	call	breakpoint_c
	br	restore_all

/*
 * This is the generic interrupt handler (for all hardware interrupt
 * sources). It figures out the vector number and calls the appropriate
 * interrupt service routine directly.
 */
ENTRY(inthandler)
	SAVE_ALL
	/*
	 * Test to see if the exception was a software exception or caused by an
	 * external interrupt, and vector accordingly.
	 */

	rdctl	r24, estatus
	andi	r24, r24, ESTATUS_EPIE
	beq	r24, r0, software_exception
	rdctl	r12, ipending
	rdctl	r9, ienable
	and	r12, r12, r9
	beq	r12, r0, software_exception

	movi	r24, -1
	stw	r24, PT_ORIG_R2(sp)

	/*
	 * Process an external hardware interrupt.
	 */

	addi	ea, ea, -4	/* re-issue the interrupted instruction */
	stw	ea, PT_EA(sp)
2:	movi	r4, %lo(-1) /* Start from bit position 0, highest priority */
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
	ldw	r4, PT_STATUS_EXTENSION(sp)
	TSTBZ	r4, r4, PS_S, Luser_return	/* Returning to user */

#ifdef CONFIG_PREEMPT
	GET_THREAD_INFO	r1
	ldw	r4, TI_PREEMPT_COUNT(r1)
	bne	r4, r0, restore_all

need_resched:
	ldw	r4, TI_FLAGS(r1)		/* ? Need resched set */
	BTBZ	r10, r4, TIF_NEED_RESCHED, restore_all
	ldw	r4, PT_ESTATUS(sp)	/* ? Interrupts off */
	andi	r10, r4, STATUS_PIE
	beq	r10, r0, restore_all
	movia	r4, PREEMPT_ACTIVE
	stw	r4, TI_PREEMPT_COUNT(r1)
	rdctl	r10, status		/* enable intrs again */
	ori	r10, r10, STATUS_PIE
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

	movia	r8, status_extension		/* save status extension */
	ldw	r7, 0(r8)
	stw	r7, TASK_THREAD + THREAD_KESR(r4)

	SAVE_SWITCH_STACK
	stw	sp, TASK_THREAD + THREAD_KSP(r4)/* save kernel stack pointer */
	ldw	sp, TASK_THREAD + THREAD_KSP(r5)/* restore new thread stack */
	movia	r24, _current_thread		/* save thread */
	GET_THREAD_INFO r1
	stw	r1, 0(r24)
	RESTORE_SWITCH_STACK

	ldw	r7, TASK_THREAD + THREAD_KESR(r5) /* restore extended status
						      reg */
	stw	r7, 0(r8)

	ldw	r7, TASK_THREAD +TH READ_KPSR(r5)/* restore thread status reg */
	wrctl	status, r7
	ret

ENTRY(ret_from_fork)
	call	schedule_tail
	br	ret_from_exception

ENTRY(sys_fork)
	mov	r4, sp
	SAVE_SWITCH_STACK
	call	nios2_vfork
	RESTORE_SWITCH_STACK
	ret

ENTRY(sys_vfork)
	mov	r4, sp
	SAVE_SWITCH_STACK
	call	nios2_vfork
	RESTORE_SWITCH_STACK
	ret

ENTRY(sys_execve)
	mov	r4, sp
	SAVE_SWITCH_STACK
	call	nios2_execve
	RESTORE_SWITCH_STACK
	ret

ENTRY(sys_clone)
	mov	r4, sp
	SAVE_SWITCH_STACK
	call	nios2_clone
	RESTORE_SWITCH_STACK
	ret

ENTRY(sys_sigsuspend)
	mov	r4, sp
	SAVE_SWITCH_STACK
	call	do_sigsuspend
	RESTORE_SWITCH_STACK
	ret

ENTRY(sys_rt_sigsuspend)
	mov	r4, sp
	SAVE_SWITCH_STACK
	call	do_rt_sigsuspend
	RESTORE_SWITCH_STACK
	ret

ENTRY(sys_sigreturn)
	mov	r4, sp
	SAVE_SWITCH_STACK
	call	do_sigreturn
	RESTORE_SWITCH_STACK
	ret

ENTRY(sys_sigaltstack)
	ldw	r4, PT_R4(sp)
	ldw	r5, PT_R5(sp)
	ldw	r6, PT_SP(sp)
	SAVE_SWITCH_STACK
	call	do_sigaltstack
	RESTORE_SWITCH_STACK
	ret

ENTRY(sys_rt_sigreturn)
	SAVE_SWITCH_STACK
	mov	r4, sp
	call	do_rt_sigreturn
	RESTORE_SWITCH_STACK
	ret
