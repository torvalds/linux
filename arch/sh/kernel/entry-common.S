/* $Id: entry.S,v 1.37 2004/06/11 13:02:46 doyu Exp $
 *
 *  linux/arch/sh/entry.S
 *
 *  Copyright (C) 1999, 2000, 2002  Niibe Yutaka
 *  Copyright (C) 2003  Paul Mundt
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

#if defined(CONFIG_SH_STANDARD_BIOS) || defined(CONFIG_SH_KGDB)
! Handle kernel debug if either kgdb (SW) or gdb-stub (FW) is present.
! If both are configured, handle the debug traps (breakpoints) in SW,
! but still allow BIOS traps to FW.

	.align	2
debug_kernel:
#if defined(CONFIG_SH_STANDARD_BIOS) && defined(CONFIG_SH_KGDB)
	/* Force BIOS call to FW (debug_trap put TRA in r8) */
	mov	r8,r0
	shlr2	r0
	cmp/eq	#0x3f,r0
	bt	debug_kernel_fw
#endif /* CONFIG_SH_STANDARD_BIOS && CONFIG_SH_KGDB */

debug_enter:		
#if defined(CONFIG_SH_KGDB)
	/* Jump to kgdb, pass stacked regs as arg */
debug_kernel_sw:
	mov.l	3f, r0
	jmp	@r0
	 mov	r15, r4
	.align	2
3:	.long	kgdb_handle_exception
#endif /* CONFIG_SH_KGDB */

#endif /* CONFIG_SH_STANDARD_BIOS || CONFIG_SH_KGDB */


	.align	2
debug_trap:	
#if defined(CONFIG_SH_STANDARD_BIOS) || defined(CONFIG_SH_KGDB)
	mov	#OFF_SR, r0
	mov.l	@(r0,r15), r0		! get status register
	shll	r0
	shll	r0			! kernel space?
	bt/s	debug_kernel
#endif
	 mov.l	@r15, r0		! Restore R0 value
	mov.l	1f, r8
	jmp	@r8
	 nop

	.align	2
ENTRY(exception_error)
	!
	sti
	mov.l	2f, r0
	jmp	@r0
	 nop

!
	.align	2
1:	.long	break_point_trap_software
2:	.long	do_exception_error

	.align	2
ret_from_exception:
	preempt_stop()
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

	mov.l	1f, r0
	mov.l	r0, @(TI_PRE_COUNT,r8)

	sti
	mov.l	2f, r0
	jsr	@r0
	 nop
	mov	#0, r0
	mov.l	r0, @(TI_PRE_COUNT,r8)
	cli

	bra	need_resched
	 nop
noresched:
	bra	__restore_all
	 nop

	.align 2
1:	.long	PREEMPT_ACTIVE
2:	.long	schedule
#endif

ENTRY(resume_userspace)
	! r8: current_thread_info
	cli
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
#ifndef CONFIG_PREEMPT
	! gUSA handling
	mov.l	@(OFF_SP,r15), r0	! get user space stack pointer
	mov	r0, r1
	shll	r0
	bf/s	1f
	 shll	r0
	bf/s	1f
	 mov	#OFF_PC, r0
	! 				  SP >= 0xc0000000 : gUSA mark
	mov.l	@(r0,r15), r2		! get user space PC (program counter)
	mov.l	@(OFF_R0,r15), r3	! end point
	cmp/hs	r3, r2			! r2 >= r3? 
	bt	1f
	add	r3, r1			! rewind point #2
	mov.l	r1, @(r0,r15)		! reset PC to rewind point #2
	!
1:
#endif
	mov.l	1f, r1
	jsr	@r1				! schedule
	 nop
	cli
	!
	mov.l	@(TI_FLAGS,r8), r0		! current_thread_info->flags
	tst	#_TIF_WORK_MASK, r0
	bt	__restore_all
	bra	work_pending
	 tst	#_TIF_NEED_RESCHED, r0

	.align	2
1:	.long	schedule
2:	.long	do_notify_resume
3:	.long	restore_all

	.align	2
syscall_exit_work:
	! r0: current_thread_info->flags
	! r8: current_thread_info
	tst	#_TIF_SYSCALL_TRACE, r0
	bt/s	work_pending
	 tst	#_TIF_NEED_RESCHED, r0
	sti
	! XXX setup arguments...
	mov.l	4f, r0			! do_syscall_trace
	jsr	@r0
	 nop
	bra	resume_userspace
	 nop

	.align	2
syscall_trace_entry:
	!                     	Yes it is traced.
	! XXX setup arguments...
	mov.l	4f, r11		! Call do_syscall_trace which notifies
	jsr	@r11	    	! superior (will chomp R[0-7])
	 nop
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
	!   	    	    Arrange for do_syscall_trace to be called
	!   	    	    again as the system call returns.
	mov.l	2f, r10			! Number of syscalls
	cmp/hs	r10, r3
	bf	syscall_call
	mov	#-ENOSYS, r0
	bra	syscall_exit
	 mov.l	r0, @(OFF_R0,r15)	! Return value

__restore_all:
	mov.l	1f,r0
	jmp	@r0
	 nop

	.align	2
1:	.long	restore_all

/*
 * Syscall interface:
 *
 *	Syscall #: R3
 *	Arguments #0 to #3: R4--R7
 *	Arguments #4 to #6: R0, R1, R2
 *	TRA: (number of arguments + 0x10) x 4
 *
 * This code also handles delegating other traps to the BIOS/gdb stub
 * according to:
 *
 * Trap number
 * (TRA>>2) 	    Purpose
 * -------- 	    -------
 * 0x0-0xf  	    old syscall ABI
 * 0x10-0x1f  	    new syscall ABI
 * 0x20-0xff  	    delegated through debug_trap to BIOS/gdb stub.
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
	!
ENTRY(system_call)
#if !defined(CONFIG_CPU_SH2)
	mov.l	1f, r9
	mov.l	@r9, r8		! Read from TRA (Trap Address) Register
#endif
	!
	! Is the trap argument >= 0x20? (TRA will be >= 0x80)
	mov	#0x7f, r9
	cmp/hi	r9, r8
	bt/s	0f
	 mov	#OFF_TRA, r9
	add	r15, r9
	!
	mov.l	r8, @r9			! set TRA value to tra
	sti
	!   	    	    Call the system call handler through the table.
	!   	    	    First check for bad syscall number
	mov	r3, r9
	mov.l	2f, r8			! Number of syscalls
	cmp/hs	r8, r9
	get_current_thread_info r8, r10
	bf	good_system_call
syscall_badsys:			! Bad syscall number
	mov	#-ENOSYS, r0
	bra	resume_userspace
	 mov.l	r0, @(OFF_R0,r15)	! Return value
	!
0:
	bra	debug_trap
	 nop
	!
good_system_call:		! Good syscall number
	mov.l	@(TI_FLAGS,r8), r8
	mov	#_TIF_SYSCALL_TRACE, r10
	tst	r10, r8
	bf	syscall_trace_entry
	!
syscall_call:
	shll2	r9		! x4
	mov.l	3f, r8		! Load the address of sys_call_table
	add	r8, r9
	mov.l	@r9, r8
	jsr	@r8	    	! jump to specific syscall handler
	 nop
	mov.l	@(OFF_R0,r15), r12		! save r0
	mov.l	r0, @(OFF_R0,r15)		! save the return value
	!
syscall_exit:
	cli
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
4:	.long	do_syscall_trace
