/* 
 *  Copyright (C) 1999, 2000, 2002  Niibe Yutaka
 *  Copyright (C) 2003 - 2008  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
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

#if defined(CONFIG_PREEMPT)
#  define preempt_stop()	cli
#else
#  define preempt_stop()
#  define resume_kernel		__restore_all
#endif


	.align	2
ENTRY(exception_error)
	!
#ifdef CONFIG_TRACE_IRQFLAGS
	mov.l	2f, r0
	jsr	@r0
	 nop
#endif
	sti
	mov.l	1f, r0
	jmp	@r0
	 nop

	.align	2
1:	.long	do_exception_error
#ifdef CONFIG_TRACE_IRQFLAGS
2:	.long	trace_hardirqs_on
#endif

	.align	2
ret_from_exception:
	preempt_stop()
#ifdef CONFIG_TRACE_IRQFLAGS
	mov.l	4f, r0
	jsr	@r0
	 nop
#endif
ENTRY(ret_from_irq)
	!
	mov	#OFF_SR, r0
	mov.l	@(r0,r15), r0	! get status register
	shll	r0
	shll	r0		! kernel space?
	get_current_thread_info r8, r0
	bt	resume_kernel	! Yes, it's from kernel, go back soon

#ifdef CONFIG_PREEMPT
	bra	resume_userspace
	 nop
ENTRY(resume_kernel)
	cli
	mov.l	@(TI_PRE_COUNT,r8), r0	! current_thread_info->preempt_count
	tst	r0, r0
	bf	noresched
need_resched:
	mov.l	@(TI_FLAGS,r8), r0	! current_thread_info->flags
	tst	#_TIF_NEED_RESCHED, r0	! need_resched set?
	bt	noresched

	mov	#OFF_SR, r0
	mov.l	@(r0,r15), r0		! get status register
	and	#0xf0, r0		! interrupts off (exception path)?
	cmp/eq	#0xf0, r0
	bt	noresched
	mov.l	3f, r0
	jsr	@r0			! call preempt_schedule_irq
	 nop
	bra	need_resched
	 nop

noresched:
	bra	__restore_all
	 nop

	.align 2
1:	.long	PREEMPT_ACTIVE
2:	.long	schedule
3:	.long	preempt_schedule_irq
#endif

ENTRY(resume_userspace)
	! r8: current_thread_info
	cli
#ifdef CONFIG_TRACE_IRQFLAGS
	mov.l	5f, r0
	jsr	@r0
	 nop
#endif
	mov.l	@(TI_FLAGS,r8), r0		! current_thread_info->flags
	tst	#_TIF_WORK_MASK, r0
	bt/s	__restore_all
	 tst	#_TIF_NEED_RESCHED, r0

	.align	2
work_pending:
	! r0: current_thread_info->flags
	! r8: current_thread_info
	! t:  result of "tst	#_TIF_NEED_RESCHED, r0"
	bf/s	work_resched
	 tst	#(_TIF_SIGPENDING | _TIF_RESTORE_SIGMASK), r0
work_notifysig:
	bt/s	__restore_all
	 mov	r15, r4
	mov	r12, r5		! set arg1(save_r0)
	mov	r0, r6
	mov.l	2f, r1
	mov.l	3f, r0
	jmp	@r1
	 lds	r0, pr
work_resched:
	mov.l	1f, r1
	jsr	@r1				! schedule
	 nop
	cli
#ifdef CONFIG_TRACE_IRQFLAGS
	mov.l	5f, r0
	jsr	@r0
	 nop
#endif
	!
	mov.l	@(TI_FLAGS,r8), r0		! current_thread_info->flags
	tst	#_TIF_WORK_MASK, r0
	bt	__restore_all
	bra	work_pending
	 tst	#_TIF_NEED_RESCHED, r0

	.align	2
1:	.long	schedule
2:	.long	do_notify_resume
3:	.long	resume_userspace
#ifdef CONFIG_TRACE_IRQFLAGS
4:	.long	trace_hardirqs_on
5:	.long	trace_hardirqs_off
#endif

	.align	2
syscall_exit_work:
	! r0: current_thread_info->flags
	! r8: current_thread_info
	tst	#_TIF_WORK_SYSCALL_MASK, r0
	bt/s	work_pending
	 tst	#_TIF_NEED_RESCHED, r0
#ifdef CONFIG_TRACE_IRQFLAGS
	mov.l	5f, r0
	jsr	@r0
	 nop
#endif
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
	!   	    	    	used by the system call handler directly
	!   	    	    	from the kernel stack anyway, so don't need
	!   	    	    	to be reloaded here.)  This allows the parent
	!   	    	    	to rewrite system calls and args on the fly.
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
	mov.l	1f, r0
	jmp	@r0
	 nop

	.align	2
1:	.long	restore_all

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
	bra	__restore_all
	 nop

	.align	2
1:	.long	debug_trap_table

/*
 * Syscall interface:
 *
 *	Syscall #: R3
 *	Arguments #0 to #3: R4--R7
 *	Arguments #4 to #6: R0, R1, R2
 *	TRA: (number of arguments + ABI revision) x 4
 *
 * This code also handles delegating other traps to the BIOS/gdb stub
 * according to:
 *
 * Trap number
 * (TRA>>2)	Purpose
 * --------	-------
 * 0x00-0x0f	original SH-3/4 syscall ABI (not in general use).
 * 0x10-0x1f	general SH-3/4 syscall ABI.
 * 0x20-0x2f	syscall ABI for SH-2 parts.
 * 0x30-0x3f	debug traps used by the kernel.
 * 0x40-0xff	Not supported by all parts, so left unhandled.
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
1:	.long	schedule_tail

/*
 * The poorly named main trapa decode and dispatch routine, for
 * system calls and debug traps through their respective jump tables.
 */
ENTRY(system_call)
#if !defined(CONFIG_CPU_SH2)
	mov.l	1f, r9
	mov.l	@r9, r8		! Read from TRA (Trap Address) Register
#endif
	/*
	 * Check the trap type
	 */
	mov	#((0x20 << 2) - 1), r9
	cmp/hi	r9, r8
	bt/s	debug_trap		! it's a debug trap..
	 mov	#OFF_TRA, r9
	add	r15, r9
	mov.l	r8, @r9			! set TRA value to tra
#ifdef CONFIG_TRACE_IRQFLAGS
	mov.l	5f, r10
	jsr	@r10
	 nop
#endif
	sti

	!
	get_current_thread_info r8, r10
	mov.l	@(TI_FLAGS,r8), r8
	mov	#_TIF_WORK_SYSCALL_MASK, r10
	tst	r10, r8
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
	jsr	@r8	    	! jump to specific syscall handler
	 nop
	mov.l	@(OFF_R0,r15), r12		! save r0
	mov.l	r0, @(OFF_R0,r15)		! save the return value
	!
syscall_exit:
	cli
#ifdef CONFIG_TRACE_IRQFLAGS
	mov.l	6f, r0
	jsr	@r0
	 nop
#endif
	!
	get_current_thread_info r8, r0
	mov.l	@(TI_FLAGS,r8), r0		! current_thread_info->flags
	tst	#_TIF_ALLWORK_MASK, r0
	bf	syscall_exit_work
	bra	__restore_all
	 nop
	.align	2
#if !defined(CONFIG_CPU_SH2)
1:	.long	TRA
#endif
2:	.long	NR_syscalls
3:	.long	sys_call_table
#ifdef CONFIG_TRACE_IRQFLAGS
5:	.long	trace_hardirqs_on
6:	.long	trace_hardirqs_off
#endif
7:	.long	do_syscall_trace_enter
8:	.long	do_syscall_trace_leave
