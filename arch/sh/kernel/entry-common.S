/* SPDX-License-Identifier: GPL-2.0
 *
 *  Copyright (C) 1999, 2000, 2002  Niibe Yutaka
 *  Copyright (C) 2003 - 2008  Paul Mundt
 */

! NOTE:
! GNU as (as of 2.9.1) changes bf/s into bt/s and bra, when the address
! to be jumped is too far, but it causes illegal slot exception.

/*	
 * entry.S contains the system-call and fault low-level handling routines.
 * This also contains the timer-interrupt handler, as well as all interrupts
 * and faults that can result in a task-switch.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call.
 *
 * NOTE: This code uses a convention that instructions in the delay slot
 * of a transfer-control instruction are indented by an extra space, thus:
 *
 *    jmp	@k0	    ! control-transfer instruction
 *     ldc	k1, ssr     ! delay slot
 *
 * Stack layout in 'ret_from_syscall':
 * 	ptrace needs to have all regs on the stack.
 *	if the order here is changed, it needs to be
 *	updated in ptrace.c and ptrace.h
 *
 *	r0
 *      ...
 *	r15 = stack pointer
 *	spc
 *	pr
 *	ssr
 *	gbr
 *	mach
 *	macl
 *	syscall #
 *
 */
#include <asm/dwarf.h>

#if defined(CONFIG_PREEMPTION)
#  define preempt_stop()	cli ; TRACE_IRQS_OFF
#else
#  define preempt_stop()
#  define resume_kernel		__restore_all
#endif


	.align	2
ENTRY(exception_error)
	!
	TRACE_IRQS_ON
	sti
	mov.l	1f, r0
	jmp	@r0
	 nop

	.align	2
1:	.long	do_exception_error

	.align	2
ret_from_exception:
	CFI_STARTPROC simple
	CFI_DEF_CFA r14, 0
	CFI_REL_OFFSET 17, 64
	CFI_REL_OFFSET 15, 60
	CFI_REL_OFFSET 14, 56
	CFI_REL_OFFSET 13, 52
	CFI_REL_OFFSET 12, 48
	CFI_REL_OFFSET 11, 44
	CFI_REL_OFFSET 10, 40
	CFI_REL_OFFSET 9, 36
	CFI_REL_OFFSET 8, 32
	preempt_stop()
ENTRY(ret_from_irq)
	!
	mov	#OFF_SR, r0
	mov.l	@(r0,r15), r0	! get status register
	shll	r0
	shll	r0		! kernel space?
	get_current_thread_info r8, r0
	bt	resume_kernel	! Yes, it's from kernel, go back soon

#ifdef CONFIG_PREEMPTION
	bra	resume_userspace
	 nop
ENTRY(resume_kernel)
	cli
	TRACE_IRQS_OFF
	mov.l	@(TI_PRE_COUNT,r8), r0	! current_thread_info->preempt_count
	tst	r0, r0
	bf	noresched
need_resched:
	mov.l	@(TI_FLAGS,r8), r0	! current_thread_info->flags
	tst	#_TIF_NEED_RESCHED, r0	! need_resched set?
	bt	noresched

	mov	#OFF_SR, r0
	mov.l	@(r0,r15), r0		! get status register
	shlr	r0
	and	#(0xf0>>1), r0		! interrupts off (exception path)?
	cmp/eq	#(0xf0>>1), r0
	bt	noresched
	mov.l	1f, r0
	jsr	@r0			! call preempt_schedule_irq
	 nop
	bra	need_resched
	 nop

noresched:
	bra	__restore_all
	 nop

	.align 2
1:	.long	preempt_schedule_irq
#endif

ENTRY(resume_userspace)
	! r8: current_thread_info
	cli
	TRACE_IRQS_OFF
	mov.l	@(TI_FLAGS,r8), r0		! current_thread_info->flags
	tst	#(_TIF_WORK_MASK & 0xff), r0
	bt/s	__restore_all
	 tst	#_TIF_NEED_RESCHED, r0

	.align	2
work_pending:
	! r0: current_thread_info->flags
	! r8: current_thread_info
	! t:  result of "tst	#_TIF_NEED_RESCHED, r0"
	bf/s	work_resched
	 tst	#(_TIF_SIGPENDING | _TIF_NOTIFY_RESUME), r0
work_notifysig:
	bt/s	__restore_all
	 mov	r15, r4
	mov	r12, r5		! set arg1(save_r0)
	mov	r0, r6
	sti
	mov.l	2f, r1
	mov.l	3f, r0
	jmp	@r1
	 lds	r0, pr
work_resched:
	mov.l	1f, r1
	jsr	@r1				! schedule
	 nop
	cli
	TRACE_IRQS_OFF
	!
	mov.l	@(TI_FLAGS,r8), r0		! current_thread_info->flags
	tst	#(_TIF_WORK_MASK & 0xff), r0
	bt	__restore_all
	bra	work_pending
	 tst	#_TIF_NEED_RESCHED, r0

	.align	2
1:	.long	schedule
2:	.long	do_notify_resume
3:	.long	resume_userspace

	.align	2
syscall_exit_work:
	! r0: current_thread_info->flags
	! r8: current_thread_info
	tst	#(_TIF_WORK_SYSCALL_MASK & 0xff), r0
	bt/s	work_pending
	 tst	#_TIF_NEED_RESCHED, r0
	TRACE_IRQS_ON
	sti
	mov	r15, r4
	mov.l	8f, r0			! do_syscall_trace_leave
	jsr	@r0
	 nop
	bra	resume_userspace
	 nop

	.align	2
syscall_trace_entry:
	!                     	Yes it is traced.
	mov     r15, r4
	mov.l	7f, r11		! Call do_syscall_trace_enter which notifies
	jsr	@r11	    	! superior (will chomp R[0-7])
	 nop
	mov.l	r0, @(OFF_R0,r15)	! Save return value
	!			Reload R0-R4 from kernel stack, where the
	!   	    	    	parent may have modified them using
	!   	    	    	ptrace(POKEUSR).  (Note that R0-R2 are
	!   	    	    	reloaded from the kernel stack by syscall_call
	!   	    	    	below, so don't need to be reloaded here.)
	!   	    	    	This allows the parent to rewrite system calls
	!   	    	    	and args on the fly.
	mov.l	@(OFF_R4,r15), r4   ! arg0
	mov.l	@(OFF_R5,r15), r5
	mov.l	@(OFF_R6,r15), r6
	mov.l	@(OFF_R7,r15), r7   ! arg3
	mov.l	@(OFF_R3,r15), r3   ! syscall_nr
	!
	mov.l	2f, r10			! Number of syscalls
	cmp/hs	r10, r3
	bf	syscall_call
	mov	#-ENOSYS, r0
	bra	syscall_exit
	 mov.l	r0, @(OFF_R0,r15)	! Return value

__restore_all:
	mov	#OFF_SR, r0
	mov.l	@(r0,r15), r0	! get status register

	shlr2	r0
	and	#0x3c, r0
	cmp/eq	#0x3c, r0
	bt	1f
	TRACE_IRQS_ON
	bra	2f
	 nop
1:
	TRACE_IRQS_OFF
2:
	mov.l	3f, r0
	jmp	@r0
	 nop

	.align	2
3:	.long	restore_all

	.align	2
syscall_badsys:			! Bad syscall number
	get_current_thread_info r8, r0
	mov	#-ENOSYS, r0
	bra	resume_userspace
	 mov.l	r0, @(OFF_R0,r15)	! Return value

/*
 * The main debug trap handler.
 *
 * r8=TRA (not the trap number!)
 *
 * Note: This assumes that the trapa value is left in its original
 * form (without the shlr2 shift) so the calculation for the jump
 * call table offset remains a simple in place mask.
 */
debug_trap:
	mov	r8, r0
	and	#(0xf << 2), r0
	mov.l	1f, r8
	add	r0, r8
	mov.l	@r8, r8
	jsr	@r8
	 nop
	bra	ret_from_exception
	 nop
	CFI_ENDPROC

	.align	2
1:	.long	debug_trap_table

/*
 * Syscall interface:
 *
 *	Syscall #: R3
 *	Arguments #0 to #3: R4--R7
 *	Arguments #4 to #6: R0, R1, R2
 *	TRA: See following table.
 *
 * (TRA>>2)	Purpose
 * --------	-------
 * 0x00-0x0f	original SH-3/4 syscall ABI (not in general use).
 * 0x10-0x1f	general SH-3/4 syscall ABI.
 *      0x1f	unified SH-2/3/4 syscall ABI (preferred).
 * 0x20-0x2f	original SH-2 syscall ABI.
 * 0x30-0x3f	debug traps used by the kernel.
 * 0x40-0xff	Not supported by all parts, so left unhandled.
 *
 * For making system calls, any trap number in the range for the
 * given cpu model may be used, but the unified trap number 0x1f is
 * preferred for compatibility with all models.
 *
 * The low bits of the trap number were once documented as matching
 * the number of arguments, but they were never actually used as such
 * by the kernel. SH-2 originally used its own separate trap range
 * because several hardware exceptions fell in the range used for the
 * SH-3/4 syscall ABI.
 *
 * This code also handles delegating other traps to the BIOS/gdb stub.
 *
 * Note: When we're first called, the TRA value must be shifted
 * right 2 bits in order to get the value that was used as the "trapa"
 * argument.
 */

	.align	2
	.globl	ret_from_fork
ret_from_fork:
	mov.l	1f, r8
	jsr	@r8
	 mov	r0, r4
	bra	syscall_exit
	 nop

	.align	2
	.globl	ret_from_kernel_thread
ret_from_kernel_thread:
	mov.l	1f, r8
	jsr	@r8
	 mov	r0, r4
	mov.l	@(OFF_R5,r15), r5   ! fn
	jsr	@r5
	 mov.l	@(OFF_R4,r15), r4   ! arg
	bra	syscall_exit
	 nop

	.align	2
1:	.long	schedule_tail

/*
 * The poorly named main trapa decode and dispatch routine, for
 * system calls and debug traps through their respective jump tables.
 */
ENTRY(system_call)
	setup_frame_reg
#if !defined(CONFIG_CPU_SH2)
	mov.l	1f, r9
	mov.l	@r9, r8		! Read from TRA (Trap Address) Register
#endif

	mov	#OFF_TRA, r10
	add	r15, r10
	mov.l	r8, @r10		! set TRA value to tra

	/*
	 * Check the trap type
	 */
	mov	#((0x20 << 2) - 1), r9
	cmp/hi	r9, r8
	bt/s	debug_trap		! it's a debug trap..
	 nop

	TRACE_IRQS_ON
	sti

	!
	get_current_thread_info r8, r10
	mov.l	@(TI_FLAGS,r8), r8
	mov	#(_TIF_WORK_SYSCALL_MASK & 0xff), r10
	mov	#(_TIF_WORK_SYSCALL_MASK >> 8), r9
	tst	r10, r8
	shll8	r9
	bf	syscall_trace_entry
	tst	r9, r8
	bf	syscall_trace_entry
	!
	mov.l	2f, r8			! Number of syscalls
	cmp/hs	r8, r3
	bt	syscall_badsys
	!
syscall_call:
	shll2	r3		! x4
	mov.l	3f, r8		! Load the address of sys_call_table
	add	r8, r3
	mov.l	@r3, r8
	mov.l	@(OFF_R2,r15), r2
	mov.l	@(OFF_R1,r15), r1
	mov.l	@(OFF_R0,r15), r0
	mov.l	r2, @-r15
	mov.l	r1, @-r15
	mov.l	r0, @-r15
	jsr	@r8	    	! jump to specific syscall handler
	 nop
	add	#12, r15
	mov.l	@(OFF_R0,r15), r12		! save r0
	mov.l	r0, @(OFF_R0,r15)		! save the return value
	!
syscall_exit:
	cli
	TRACE_IRQS_OFF
	!
	get_current_thread_info r8, r0
	mov.l	@(TI_FLAGS,r8), r0		! current_thread_info->flags
	tst	#(_TIF_ALLWORK_MASK & 0xff), r0
	mov	#(_TIF_ALLWORK_MASK >> 8), r1
	bf	syscall_exit_work
	shlr8	r0
	tst	r0, r1
	bf	syscall_exit_work
	bra	__restore_all
	 nop
	.align	2
#if !defined(CONFIG_CPU_SH2)
1:	.long	TRA
#endif
2:	.long	NR_syscalls
3:	.long	sys_call_table
7:	.long	do_syscall_trace_enter
8:	.long	do_syscall_trace_leave
