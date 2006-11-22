/*
 *  linux/arch/sh/entry.S
 *
 *  Copyright (C) 1999, 2000, 2002  Niibe Yutaka
 *  Copyright (C) 2003 - 2006  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */
#include <linux/sys.h>
#include <linux/errno.h>
#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/cpu/mmu_context.h>
#include <asm/unistd.h>

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
#if defined(CONFIG_KGDB_NMI)
NMI_VEC = 0x1c0			! Must catch early for debounce
#endif

/* Offsets to the stack */
OFF_R0  =  0		/* Return value. New ABI also arg4 */
OFF_R1  =  4     	/* New ABI: arg5 */
OFF_R2  =  8     	/* New ABI: arg6 */
OFF_R3  =  12     	/* New ABI: syscall_nr */
OFF_R4  =  16     	/* New ABI: arg0 */
OFF_R5  =  20     	/* New ABI: arg1 */
OFF_R6  =  24     	/* New ABI: arg2 */
OFF_R7  =  28     	/* New ABI: arg3 */
OFF_SP	=  (15*4)
OFF_PC  =  (16*4)
OFF_SR	=  (16*4+8)
OFF_TRA	=  (16*4+6*4)


#define k0	r0
#define k1	r1
#define k2	r2
#define k3	r3
#define k4	r4

#define g_imask		r6	/* r6_bank1 */
#define k_g_imask	r6_bank	/* r6_bank1 */
#define current		r7	/* r7_bank1 */

/*
 * Kernel mode register usage:
 *	k0	scratch
 *	k1	scratch
 *	k2	scratch (Exception code)
 *	k3	scratch (Return address)
 *	k4	scratch
 *	k5	reserved
 *	k6	Global Interrupt Mask (0--15 << 4)
 *	k7	CURRENT_THREAD_INFO (pointer to current thread info)
 */

!
! TLB Miss / Initial Page write exception handling
!			_and_
! TLB hits, but the access violate the protection.
! It can be valid access, such as stack grow and/or C-O-W.
!
!
! Find the pmd/pte entry and loadtlb
! If it's not found, cause address error (SEGV)
!
! Although this could be written in assembly language (and it'd be faster),
! this first version depends *much* on C implementation.
!

#define CLI()				\
	stc	sr, r0;			\
	or	#0xf0, r0;		\
	ldc	r0, sr

#define STI()				\
	mov.l	__INV_IMASK, r11;	\
	stc	sr, r10;		\
	and	r11, r10;		\
	stc	k_g_imask, r11;		\
	or	r11, r10;		\
	ldc	r10, sr

#if defined(CONFIG_PREEMPT)
#  define preempt_stop()	CLI()
#else
#  define preempt_stop()
#  define resume_kernel		restore_all
#endif

#if defined(CONFIG_MMU)
	.align	2
ENTRY(tlb_miss_load)
	bra	call_dpf
	 mov	#0, r5

	.align	2
ENTRY(tlb_miss_store)
	bra	call_dpf
	 mov	#1, r5

	.align	2
ENTRY(initial_page_write)
	bra	call_dpf
	 mov	#1, r5

	.align	2
ENTRY(tlb_protection_violation_load)
	bra	call_dpf
	 mov	#0, r5

	.align	2
ENTRY(tlb_protection_violation_store)
	bra	call_dpf
	 mov	#1, r5

call_dpf:
	mov.l	1f, r0
	mov	r5, r8
	mov.l	@r0, r6
	mov	r6, r9
	mov.l	2f, r0
	sts	pr, r10
	jsr	@r0
	 mov	r15, r4
	!
	tst	r0, r0
	bf/s	0f
	 lds	r10, pr
	rts
	 nop
0:	STI()
	mov.l	3f, r0
	mov	r9, r6
	mov	r8, r5
	jmp	@r0
	 mov	r15, r4

	.align 2
1:	.long	MMU_TEA
2:	.long	__do_page_fault
3:	.long	do_page_fault

	.align	2
ENTRY(address_error_load)
	bra	call_dae
	 mov	#0,r5		! writeaccess = 0

	.align	2
ENTRY(address_error_store)
	bra	call_dae
	 mov	#1,r5		! writeaccess = 1

	.align	2
call_dae:
	mov.l	1f, r0
	mov.l	@r0, r6		! address
	mov.l	2f, r0
	jmp	@r0
	 mov	r15, r4		! regs

	.align 2
1:	.long	MMU_TEA
2:	.long   do_address_error
#endif /* CONFIG_MMU */

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

#if defined(CONFIG_SH_STANDARD_BIOS)
	/* Unwind the stack and jmp to the debug entry */
debug_kernel_fw:
	mov.l	@r15+, r0
	mov.l	@r15+, r1
	mov.l	@r15+, r2
	mov.l	@r15+, r3
	mov.l	@r15+, r4
	mov.l	@r15+, r5
	mov.l	@r15+, r6
	mov.l	@r15+, r7
	stc	sr, r8
	mov.l	1f, r9			! BL =1, RB=1, IMASK=0x0F
	or	r9, r8
	ldc	r8, sr			! here, change the register bank
	mov.l	@r15+, r8
	mov.l	@r15+, r9
	mov.l	@r15+, r10
	mov.l	@r15+, r11
	mov.l	@r15+, r12
	mov.l	@r15+, r13
	mov.l	@r15+, r14
	mov.l	@r15+, k0
	ldc.l	@r15+, spc
	lds.l	@r15+, pr
	mov.l	@r15+, k1
	ldc.l	@r15+, gbr
	lds.l	@r15+, mach
	lds.l	@r15+, macl
	mov	k0, r15
	!
	mov.l	2f, k0
	mov.l	@k0, k0
	jmp	@k0
	 ldc	k1, ssr
	.align	2
1:	.long	0x300000f0
2:	.long	gdb_vbr_vector
#endif /* CONFIG_SH_STANDARD_BIOS */

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
	STI()
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
	bt/s	resume_kernel	! Yes, it's from kernel, go back soon
	 GET_THREAD_INFO(r8)

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

	STI()
	mov.l	2f, r0
	jsr	@r0
	 nop
	mov	#0, r0
	mov.l	r0, @(TI_PRE_COUNT,r8)
	CLI()

	bra	need_resched
	 nop
noresched:
	bra	restore_all
	 nop

	.align 2
1:	.long	PREEMPT_ACTIVE
2:	.long	schedule
#endif

ENTRY(resume_userspace)
	! r8: current_thread_info
	CLI()
	mov.l	@(TI_FLAGS,r8), r0		! current_thread_info->flags
	tst	#_TIF_WORK_MASK, r0
	bt/s	restore_all
	 tst	#_TIF_NEED_RESCHED, r0

	.align	2
work_pending:
	! r0: current_thread_info->flags
	! r8: current_thread_info
	! t:  result of "tst	#_TIF_NEED_RESCHED, r0"
	bf/s	work_resched
	 tst	#(_TIF_SIGPENDING | _TIF_RESTORE_SIGMASK), r0
work_notifysig:
	bt/s	restore_all
	 mov	r15, r4
	mov	r12, r5		! set arg1(save_r0)
	mov	r0, r6
	mov.l	2f, r1
	mova	restore_all, r0
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
	CLI()
	!
	mov.l	@(TI_FLAGS,r8), r0		! current_thread_info->flags
	tst	#_TIF_WORK_MASK, r0
	bt	restore_all
	bra	work_pending
	 tst	#_TIF_NEED_RESCHED, r0

	.align	2
1:	.long	schedule
2:	.long	do_notify_resume

	.align	2
syscall_exit_work:
	! r0: current_thread_info->flags
	! r8: current_thread_info
	tst	#_TIF_SYSCALL_TRACE, r0
	bt/s	work_pending
	 tst	#_TIF_NEED_RESCHED, r0
	STI()
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
	mov.l	1f, r9
	mov.l	@r9, r8		! Read from TRA (Trap Address) Register
	!
	! Is the trap argument >= 0x20? (TRA will be >= 0x80)
	mov	#0x7f, r9
	cmp/hi	r9, r8
	bt/s	0f
	 mov	#OFF_TRA, r9
	add	r15, r9
	!
	mov.l	r8, @r9			! set TRA value to tra
	STI()
	!   	    	    Call the system call handler through the table.
	!   	    	    First check for bad syscall number
	mov	r3, r9
	mov.l	2f, r8			! Number of syscalls
	cmp/hs	r8, r9
	bf/s	good_system_call
	 GET_THREAD_INFO(r8)
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
	CLI()
	!
	GET_THREAD_INFO(r8)
	mov.l	@(TI_FLAGS,r8), r0		! current_thread_info->flags
	tst	#_TIF_ALLWORK_MASK, r0
	bf	syscall_exit_work
restore_all:
	mov.l	@r15+, r0
	mov.l	@r15+, r1
	mov.l	@r15+, r2
	mov.l	@r15+, r3
	mov.l	@r15+, r4
	mov.l	@r15+, r5
	mov.l	@r15+, r6
	mov.l	@r15+, r7
	!
	stc	sr, r8
	mov.l	7f, r9
	or	r9, r8			! BL =1, RB=1
	ldc	r8, sr			! here, change the register bank
	!
	mov.l	@r15+, r8
	mov.l	@r15+, r9
	mov.l	@r15+, r10
	mov.l	@r15+, r11
	mov.l	@r15+, r12
	mov.l	@r15+, r13
	mov.l	@r15+, r14
	mov.l	@r15+, k4		! original stack pointer
	ldc.l	@r15+, spc
	lds.l	@r15+, pr
	mov.l	@r15+, k3		! original SR
	ldc.l	@r15+, gbr
	lds.l	@r15+, mach
	lds.l	@r15+, macl
	add	#4, r15			! Skip syscall number
	!
#ifdef CONFIG_SH_DSP
	mov.l	@r15+, k0		! DSP mode marker
	mov.l	5f, k1
	cmp/eq	k0, k1			! Do we have a DSP stack frame?
	bf	skip_restore

	stc	sr, k0			! Enable CPU DSP mode
	or	k1, k0			! (within kernel it may be disabled)
	ldc	k0, sr
	mov	r2, k0			! Backup r2

	! Restore DSP registers from stack
	mov	r15, r2
	movs.l	@r2+, a1
	movs.l	@r2+, a0g
	movs.l	@r2+, a1g
	movs.l	@r2+, m0
	movs.l	@r2+, m1
	mov	r2, r15

	lds.l	@r15+, a0
	lds.l	@r15+, x0
	lds.l	@r15+, x1
	lds.l	@r15+, y0
	lds.l	@r15+, y1
	lds.l	@r15+, dsr
	ldc.l	@r15+, rs
	ldc.l	@r15+, re
	ldc.l	@r15+, mod

	mov	k0, r2			! Restore r2
skip_restore:
#endif
	!
	! Calculate new SR value
	mov	k3, k2			! original SR value
	mov.l	9f, k1
	and	k1, k2			! Mask orignal SR value
	!
	mov	k3, k0			! Calculate IMASK-bits
	shlr2	k0
	and	#0x3c, k0
	cmp/eq	#0x3c, k0
	bt/s	6f
	 shll2	k0
	mov	g_imask, k0
	!
6:	or	k0, k2			! Set the IMASK-bits
	ldc	k2, ssr
	!
#if defined(CONFIG_KGDB_NMI)
	! Clear in_nmi
	mov.l	6f, k0
	mov	#0, k1
	mov.b	k1, @k0
#endif
	mov.l	@r15+, k2		! restore EXPEVT
	mov	k4, r15
	rte
	 nop

	.align	2
1:	.long	TRA
2:	.long	NR_syscalls
3:	.long	sys_call_table
4:	.long	do_syscall_trace
5:	.long	0x00001000	! DSP
7:	.long	0x30000000
9:
__INV_IMASK:
	.long	0xffffff0f	! ~(IMASK)

! Exception Vector Base
!
!	Should be aligned page boundary.
!
	.balign 	4096,0,4096
ENTRY(vbr_base)
	.long	0
!
	.balign 	256,0,256
general_exception:
	mov.l	1f, k2
	mov.l	2f, k3
	bra	handle_exception
	 mov.l	@k2, k2
	.align	2
1:	.long	EXPEVT
2:	.long	ret_from_exception
!
!
	.balign 	1024,0,1024
tlb_miss:
	mov.l	1f, k2
	mov.l	4f, k3
	bra	handle_exception
	 mov.l	@k2, k2
!
	.balign 	512,0,512
interrupt:
	mov.l	2f, k2
	mov.l	3f, k3
#if defined(CONFIG_KGDB_NMI)
	! Debounce (filter nested NMI)
	mov.l	@k2, k0
	mov.l	5f, k1
	cmp/eq	k1, k0
	bf	0f
	mov.l	6f, k1
	tas.b	@k1
	bt	0f
	rte
	 nop
	.align	2
5:	.long	NMI_VEC
6:	.long	in_nmi
0:
#endif /* defined(CONFIG_KGDB_NMI) */
	bra	handle_exception
	 mov	#-1, k2		! interrupt exception marker

	.align	2
1:	.long	EXPEVT
2:	.long	INTEVT
3:	.long	ret_from_irq
4:	.long	ret_from_exception

!
!
	.align	2
ENTRY(handle_exception)
	! Using k0, k1 for scratch registers (r0_bank1, r1_bank),
	! save all registers onto stack.
	!
	stc	ssr, k0		! Is it from kernel space?
	shll	k0		! Check MD bit (bit30) by shifting it into...
	shll	k0		!       ...the T bit
	bt/s	1f		! It's a kernel to kernel transition.
	 mov	r15, k0		! save original stack to k0
	/* User space to kernel */
	mov	#(THREAD_SIZE >> 8), k1
	shll8	k1		! k1 := THREAD_SIZE
	add	current, k1
	mov	k1, r15		! change to kernel stack
	!
1:	mov.l	2f, k1
	!
#ifdef CONFIG_SH_DSP
	mov.l	r2, @-r15		! Save r2, we need another reg
	stc	sr, k4
	mov.l	1f, r2
	tst	r2, k4			! Check if in DSP mode
	mov.l	@r15+, r2		! Restore r2 now
	bt/s	skip_save
	 mov	#0, k4			! Set marker for no stack frame

	mov	r2, k4			! Backup r2 (in k4) for later

	! Save DSP registers on stack
	stc.l	mod, @-r15
	stc.l	re, @-r15
	stc.l	rs, @-r15
	sts.l	dsr, @-r15
	sts.l	y1, @-r15
	sts.l	y0, @-r15
	sts.l	x1, @-r15
	sts.l	x0, @-r15
	sts.l	a0, @-r15

	! GAS is broken, does not generate correct "movs.l Ds,@-As" instr.

	! FIXME: Make sure that this is still the case with newer toolchains,
	! as we're not at all interested in supporting ancient toolchains at
	! this point. -- PFM.

	mov	r15, r2
	.word	0xf653			! movs.l	a1, @-r2
	.word	0xf6f3			! movs.l	a0g, @-r2
	.word	0xf6d3			! movs.l	a1g, @-r2
	.word	0xf6c3			! movs.l	m0, @-r2
	.word	0xf6e3			! movs.l	m1, @-r2
	mov	r2, r15

	mov	k4, r2			! Restore r2
	mov.l	1f, k4			! Force DSP stack frame
skip_save:
	mov.l	k4, @-r15		! Push DSP mode marker onto stack
#endif
	! Save the user registers on the stack.
	mov.l	k2, @-r15	! EXPEVT

 	mov	#-1, k4
	mov.l	k4, @-r15	! set TRA (default: -1)
	!
	sts.l	macl, @-r15
	sts.l	mach, @-r15
	stc.l	gbr, @-r15
	stc.l	ssr, @-r15
	sts.l	pr, @-r15
	stc.l	spc, @-r15
	!
	lds	k3, pr		! Set the return address to pr
	!
	mov.l	k0, @-r15	! save orignal stack
	mov.l	r14, @-r15
	mov.l	r13, @-r15
	mov.l	r12, @-r15
	mov.l	r11, @-r15
	mov.l	r10, @-r15
	mov.l	r9, @-r15
	mov.l	r8, @-r15
	!
	stc	sr, r8		! Back to normal register bank, and
	or	k1, r8		! Block all interrupts
	mov.l	3f, k1
	and	k1, r8		! ...
	ldc	r8, sr		! ...changed here.
	!
	mov.l	r7, @-r15
	mov.l	r6, @-r15
	mov.l	r5, @-r15
	mov.l	r4, @-r15
	mov.l	r3, @-r15
	mov.l	r2, @-r15
	mov.l	r1, @-r15
	mov.l	r0, @-r15

	/*
	 * This gets a bit tricky.. in the INTEVT case we don't want to use
	 * the VBR offset as a destination in the jump call table, since all
	 * of the destinations are the same. In this case, (interrupt) sets
	 * a marker in r2 (now r2_bank since SR.RB changed), which we check
	 * to determine the exception type. For all other exceptions, we
	 * forcibly read EXPEVT from memory and fix up the jump address, in
	 * the interrupt exception case we jump to do_IRQ() and defer the
	 * INTEVT read until there. As a bonus, we can also clean up the SR.RB
	 * checks that do_IRQ() was doing..
	 */
	stc	r2_bank, r8
	cmp/pz	r8
	bf	interrupt_exception
	shlr2	r8
	shlr	r8
	mov.l	4f, r9
	add	r8, r9
	mov.l	@r9, r9
	jmp	@r9
	 nop
	rts
	 nop

	.align	2
1:	.long	0x00001000	! DSP=1
2:	.long	0x000080f0	! FD=1, IMASK=15
3:	.long	0xcfffffff	! RB=0, BL=0
4:	.long	exception_handling_table

interrupt_exception:
	mov.l	1f, r9
	jmp	@r9
	 nop
	rts
	 nop

	.align 2
1:	.long	do_IRQ

	.align	2
ENTRY(exception_none)
	rts
	 nop
