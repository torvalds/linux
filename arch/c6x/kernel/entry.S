;
;  Port on Texas Instruments TMS320C6x architecture
;
;  Copyright (C) 2004-2011 Texas Instruments Incorporated
;  Author: Aurelien Jacquiot (aurelien.jacquiot@virtuallogix.com)
;  Updated for 2.6.34: Mark Salter <msalter@redhat.com>
;
;  This program is free software; you can redistribute it and/or modify
;  it under the terms of the GNU General Public License version 2 as
;  published by the Free Software Foundation.
;

#include <linux/sys.h>
#include <linux/linkage.h>
#include <asm/thread_info.h>
#include <asm/asm-offsets.h>
#include <asm/unistd.h>
#include <asm/errno.h>

; Registers naming
#define DP	B14
#define SP	B15

#ifndef CONFIG_PREEMPT
#define resume_kernel restore_all
#endif

	.altmacro

	.macro MASK_INT reg
	MVC	.S2	CSR,reg
	CLR	.S2	reg,0,0,reg
	MVC	.S2	reg,CSR
	.endm

	.macro UNMASK_INT reg
	MVC	.S2	CSR,reg
	SET	.S2	reg,0,0,reg
	MVC	.S2	reg,CSR
	.endm

	.macro GET_THREAD_INFO reg
	SHR	.S1X	SP,THREAD_SHIFT,reg
	SHL	.S1	reg,THREAD_SHIFT,reg
	.endm

	;;
	;;  This defines the normal kernel pt_regs layout.
	;;
	.macro SAVE_ALL __rp __tsr
	STW	.D2T2	B0,*SP--[2]		; save original B0
	MVKL	.S2	current_ksp,B0
	MVKH	.S2	current_ksp,B0
	LDW	.D2T2	*B0,B1			; KSP

	NOP	3
	STW	.D2T2	B1,*+SP[1]		; save original B1
	XOR	.D2	SP,B1,B0		; (SP ^ KSP)
	LDW	.D2T2	*+SP[1],B1		; restore B0/B1
	LDW	.D2T2	*++SP[2],B0
	SHR	.S2	B0,THREAD_SHIFT,B0	; 0 if already using kstack
  [B0]	STDW	.D2T2	SP:DP,*--B1[1]		; user: save user sp/dp kstack
  [B0]	MV	.S2	B1,SP			;    and switch to kstack
||[!B0] STDW	.D2T2	SP:DP,*--SP[1]		; kernel: save on current stack

	SUBAW	.D2	SP,2,SP

	ADD	.D1X	SP,-8,A15
 ||	STDW	.D2T1	A15:A14,*SP--[16]	; save A15:A14

	STDW	.D2T2	B13:B12,*SP--[1]
 ||	STDW	.D1T1	A13:A12,*A15--[1]
 ||	MVC	.S2	__rp,B13

	STDW	.D2T2	B11:B10,*SP--[1]
 ||	STDW	.D1T1	A11:A10,*A15--[1]
 ||	MVC	.S2	CSR,B12

	STDW	.D2T2	B9:B8,*SP--[1]
 ||	STDW	.D1T1	A9:A8,*A15--[1]
 ||	MVC	.S2	RILC,B11
	STDW	.D2T2	B7:B6,*SP--[1]
 ||	STDW	.D1T1	A7:A6,*A15--[1]
 ||	MVC	.S2	ILC,B10

	STDW	.D2T2	B5:B4,*SP--[1]
 ||	STDW	.D1T1	A5:A4,*A15--[1]

	STDW	.D2T2	B3:B2,*SP--[1]
 ||	STDW	.D1T1	A3:A2,*A15--[1]
 ||	MVC	.S2	__tsr,B5

	STDW	.D2T2	B1:B0,*SP--[1]
 ||	STDW	.D1T1	A1:A0,*A15--[1]
 ||	MV	.S1X	B5,A5

	STDW	.D2T2	B31:B30,*SP--[1]
 ||	STDW	.D1T1	A31:A30,*A15--[1]
	STDW	.D2T2	B29:B28,*SP--[1]
 ||	STDW	.D1T1	A29:A28,*A15--[1]
	STDW	.D2T2	B27:B26,*SP--[1]
 ||	STDW	.D1T1	A27:A26,*A15--[1]
	STDW	.D2T2	B25:B24,*SP--[1]
 ||	STDW	.D1T1	A25:A24,*A15--[1]
	STDW	.D2T2	B23:B22,*SP--[1]
 ||	STDW	.D1T1	A23:A22,*A15--[1]
	STDW	.D2T2	B21:B20,*SP--[1]
 ||	STDW	.D1T1	A21:A20,*A15--[1]
	STDW	.D2T2	B19:B18,*SP--[1]
 ||	STDW	.D1T1	A19:A18,*A15--[1]
	STDW	.D2T2	B17:B16,*SP--[1]
 ||	STDW	.D1T1	A17:A16,*A15--[1]

	STDW	.D2T2	B13:B12,*SP--[1]	; save PC and CSR

	STDW	.D2T2	B11:B10,*SP--[1]	; save RILC and ILC
	STDW	.D2T1	A5:A4,*SP--[1]		; save TSR and orig A4

	;; We left an unused word on the stack just above pt_regs.
	;; It is used to save whether or not this frame is due to
	;; a syscall. It is cleared here, but the syscall handler
	;; sets it to a non-zero value.
	MVK	.L2	0,B1
	STW	.D2T2	B1,*+SP(REGS__END+8)	; clear syscall flag
	.endm

	.macro RESTORE_ALL __rp __tsr
	LDDW	.D2T2	*++SP[1],B9:B8		; get TSR (B9)
	LDDW	.D2T2	*++SP[1],B11:B10	; get RILC (B11) and ILC (B10)
	LDDW	.D2T2	*++SP[1],B13:B12	; get PC (B13) and CSR (B12)

	ADDAW	.D1X	SP,30,A15

	LDDW	.D1T1	*++A15[1],A17:A16
 ||	LDDW	.D2T2	*++SP[1],B17:B16
	LDDW	.D1T1	*++A15[1],A19:A18
 ||	LDDW	.D2T2	*++SP[1],B19:B18
	LDDW	.D1T1	*++A15[1],A21:A20
 ||	LDDW	.D2T2	*++SP[1],B21:B20
	LDDW	.D1T1	*++A15[1],A23:A22
 ||	LDDW	.D2T2	*++SP[1],B23:B22
	LDDW	.D1T1	*++A15[1],A25:A24
 ||	LDDW	.D2T2	*++SP[1],B25:B24
	LDDW	.D1T1	*++A15[1],A27:A26
 ||	LDDW	.D2T2	*++SP[1],B27:B26
	LDDW	.D1T1	*++A15[1],A29:A28
 ||	LDDW	.D2T2	*++SP[1],B29:B28
	LDDW	.D1T1	*++A15[1],A31:A30
 ||	LDDW	.D2T2	*++SP[1],B31:B30

	LDDW	.D1T1	*++A15[1],A1:A0
 ||	LDDW	.D2T2	*++SP[1],B1:B0

	LDDW	.D1T1	*++A15[1],A3:A2
 ||	LDDW	.D2T2	*++SP[1],B3:B2
 ||	MVC	.S2	B9,__tsr
	LDDW	.D1T1	*++A15[1],A5:A4
 ||	LDDW	.D2T2	*++SP[1],B5:B4
 ||	MVC	.S2	B11,RILC
	LDDW	.D1T1	*++A15[1],A7:A6
 ||	LDDW	.D2T2	*++SP[1],B7:B6
 ||	MVC	.S2	B10,ILC

	LDDW	.D1T1	*++A15[1],A9:A8
 ||	LDDW	.D2T2	*++SP[1],B9:B8
 ||	MVC	.S2	B13,__rp

	LDDW	.D1T1	*++A15[1],A11:A10
 ||	LDDW	.D2T2	*++SP[1],B11:B10
 ||	MVC	.S2	B12,CSR

	LDDW	.D1T1	*++A15[1],A13:A12
 ||	LDDW	.D2T2	*++SP[1],B13:B12

	MV	.D2X	A15,SP
 ||	MVKL	.S1	current_ksp,A15
	MVKH	.S1	current_ksp,A15
 ||	ADDAW	.D1X	SP,6,A14
	STW	.D1T1	A14,*A15	; save kernel stack pointer

	LDDW	.D2T1	*++SP[1],A15:A14

	B	.S2	__rp		; return from interruption
	LDDW	.D2T2	*+SP[1],SP:DP
	NOP	4
	.endm

	.section .text

	;;
	;; Jump to schedule() then return to ret_from_exception
	;;
_reschedule:
#ifdef CONFIG_C6X_BIG_KERNEL
	MVKL	.S1	schedule,A0
	MVKH	.S1	schedule,A0
	B	.S2X	A0
#else
	B	.S1	schedule
#endif
	ADDKPC	.S2	ret_from_exception,B3,4

	;;
	;; Called before syscall handler when process is being debugged
	;;
tracesys_on:
#ifdef CONFIG_C6X_BIG_KERNEL
	MVKL	.S1	syscall_trace_entry,A0
	MVKH	.S1	syscall_trace_entry,A0
	B	.S2X	A0
#else
	B	.S1	syscall_trace_entry
#endif
	ADDKPC	.S2	ret_from_syscall_trace,B3,3
	ADD	.S1X	8,SP,A4

ret_from_syscall_trace:
	;; tracing returns (possibly new) syscall number
	MV	.D2X	A4,B0
 ||	MVK	.S2	__NR_syscalls,B1
	CMPLTU	.L2	B0,B1,B1

 [!B1]	BNOP	.S2	ret_from_syscall_function,5
 ||	MVK	.S1	-ENOSYS,A4

	;; reload syscall args from (possibly modified) stack frame
	;; and get syscall handler addr from sys_call_table:
	LDW	.D2T2	*+SP(REGS_B4+8),B4
 ||	MVKL	.S2	sys_call_table,B1
	LDW	.D2T1	*+SP(REGS_A6+8),A6
 ||	MVKH	.S2	sys_call_table,B1
	LDW	.D2T2	*+B1[B0],B0
 ||	MVKL	.S2	ret_from_syscall_function,B3
	LDW	.D2T2	*+SP(REGS_B6+8),B6
 ||	MVKH	.S2	ret_from_syscall_function,B3
	LDW	.D2T1	*+SP(REGS_A8+8),A8
	LDW	.D2T2	*+SP(REGS_B8+8),B8
	NOP
	; B0 = sys_call_table[__NR_*]
	BNOP	.S2	B0,5			; branch to syscall handler
 ||	LDW	.D2T1	*+SP(REGS_ORIG_A4+8),A4

syscall_exit_work:
	AND	.D1	_TIF_SYSCALL_TRACE,A2,A0
 [!A0]	BNOP	.S1	work_pending,5
 [A0]	B	.S2	syscall_trace_exit
	ADDKPC	.S2	resume_userspace,B3,1
	MVC	.S2	CSR,B1
	SET	.S2	B1,0,0,B1
	MVC	.S2	B1,CSR		; enable ints

work_pending:
	AND	.D1	_TIF_NEED_RESCHED,A2,A0
 [!A0]	BNOP	.S1	work_notifysig,5

work_resched:
#ifdef CONFIG_C6X_BIG_KERNEL
	MVKL	.S1	schedule,A1
	MVKH	.S1	schedule,A1
	B	.S2X	A1
#else
	B	.S2	schedule
#endif
	ADDKPC	.S2	work_rescheduled,B3,4
work_rescheduled:
	;; make sure we don't miss an interrupt setting need_resched or
	;; sigpending between sampling and the rti
	MASK_INT B2
	GET_THREAD_INFO A12
	LDW	.D1T1	*+A12(THREAD_INFO_FLAGS),A2
	MVK	.S1	_TIF_WORK_MASK,A1
	MVK	.S1	_TIF_NEED_RESCHED,A3
	NOP	2
	AND	.D1	A1,A2,A0
 ||	AND	.S1	A3,A2,A1
 [!A0]	BNOP	.S1	restore_all,5
 [A1]	BNOP	.S1	work_resched,5

work_notifysig:
	B	.S2	do_notify_resume
	LDW	.D2T1	*+SP(REGS__END+8),A6 ; syscall flag
	ADDKPC	.S2	resume_userspace,B3,1
	ADD	.S1X	8,SP,A4		; pt_regs pointer is first arg
	MV	.D2X	A2,B4		; thread_info flags is second arg

	;;
	;; On C64x+, the return way from exception and interrupt
	;; is a little bit different
	;;
ENTRY(ret_from_exception)
#ifdef CONFIG_PREEMPT
	MASK_INT B2
#endif

ENTRY(ret_from_interrupt)
	;;
	;; Check if we are comming from user mode.
	;;
	LDW	.D2T2	*+SP(REGS_TSR+8),B0
	MVK	.S2	0x40,B1
	NOP	3
	AND	.D2	B0,B1,B0
 [!B0]	BNOP	.S2	resume_kernel,5

resume_userspace:
	;; make sure we don't miss an interrupt setting need_resched or
	;; sigpending between sampling and the rti
	MASK_INT B2
	GET_THREAD_INFO A12
	LDW	.D1T1	*+A12(THREAD_INFO_FLAGS),A2
	MVK	.S1	_TIF_WORK_MASK,A1
	MVK	.S1	_TIF_NEED_RESCHED,A3
	NOP	2
	AND	.D1	A1,A2,A0
 [A0]	BNOP	.S1	work_pending,5
	BNOP	.S1	restore_all,5

	;;
	;; System call handling
	;; B0 = syscall number (in sys_call_table)
	;; A4,B4,A6,B6,A8,B8 = arguments of the syscall function
	;; A4 is the return value register
	;;
system_call_saved:
	MVK	.L2	1,B2
	STW	.D2T2	B2,*+SP(REGS__END+8)	; set syscall flag
	MVC	.S2	B2,ECR			; ack the software exception

	UNMASK_INT B2			; re-enable global IT

system_call_saved_noack:
	;; Check system call number
	MVK	.S2	__NR_syscalls,B1
#ifdef CONFIG_C6X_BIG_KERNEL
 ||	MVKL	.S1	sys_ni_syscall,A0
#endif
	CMPLTU	.L2	B0,B1,B1
#ifdef CONFIG_C6X_BIG_KERNEL
 ||	MVKH	.S1	sys_ni_syscall,A0
#endif

	;; Check for ptrace
	GET_THREAD_INFO A12

#ifdef CONFIG_C6X_BIG_KERNEL
 [!B1]	B	.S2X	A0
#else
 [!B1]	B	.S2	sys_ni_syscall
#endif
 [!B1]	ADDKPC	.S2	ret_from_syscall_function,B3,4

	;; Get syscall handler addr from sys_call_table
	;; call tracesys_on or call syscall handler
	LDW	.D1T1	*+A12(THREAD_INFO_FLAGS),A2
 ||	MVKL	.S2	sys_call_table,B1
	MVKH	.S2	sys_call_table,B1
	LDW	.D2T2	*+B1[B0],B0
	NOP	2
	; A2 = thread_info flags
	AND	.D1	_TIF_SYSCALL_TRACE,A2,A2
 [A2]	BNOP	.S1	tracesys_on,5
	;; B0 = _sys_call_table[__NR_*]
	B	.S2	B0
	ADDKPC	.S2	ret_from_syscall_function,B3,4

ret_from_syscall_function:
	STW	.D2T1	A4,*+SP(REGS_A4+8)	; save return value in A4
						; original A4 is in orig_A4
syscall_exit:
	;; make sure we don't miss an interrupt setting need_resched or
	;; sigpending between sampling and the rti
	MASK_INT B2
	LDW	.D1T1	*+A12(THREAD_INFO_FLAGS),A2
	MVK	.S1	_TIF_ALLWORK_MASK,A1
	NOP	3
	AND	.D1	A1,A2,A2 ; check for work to do
 [A2]	BNOP	.S1	syscall_exit_work,5

restore_all:
	RESTORE_ALL NRP,NTSR

	;;
	;; After a fork we jump here directly from resume,
	;; so that A4 contains the previous task structure.
	;;
ENTRY(ret_from_fork)
#ifdef CONFIG_C6X_BIG_KERNEL
	MVKL	.S1	schedule_tail,A0
	MVKH	.S1	schedule_tail,A0
	B	.S2X	A0
#else
	B	.S2	schedule_tail
#endif
	ADDKPC	.S2	ret_from_fork_2,B3,4
ret_from_fork_2:
	;; return 0 in A4 for child process
	GET_THREAD_INFO A12
	BNOP	.S2	syscall_exit,3
	MVK	.L2	0,B0
	STW	.D2T2	B0,*+SP(REGS_A4+8)
ENDPROC(ret_from_fork)

	;;
	;; These are the interrupt handlers, responsible for calling __do_IRQ()
	;; int6 is used for syscalls (see _system_call entry)
	;;
	.macro SAVE_ALL_INT
	SAVE_ALL IRP,ITSR
	.endm

	.macro CALL_INT int
#ifdef CONFIG_C6X_BIG_KERNEL
	MVKL	.S1	c6x_do_IRQ,A0
	MVKH	.S1	c6x_do_IRQ,A0
	BNOP	.S2X	A0,1
	MVK	.S1	int,A4
	ADDAW	.D2	SP,2,B4
	MVKL	.S2	ret_from_interrupt,B3
	MVKH	.S2	ret_from_interrupt,B3
#else
	CALLP   .S2	c6x_do_IRQ,B3
 ||	MVK	.S1	int,A4
 ||	ADDAW	.D2	SP,2,B4
	B	.S1	ret_from_interrupt
	NOP	5
#endif
	.endm

ENTRY(_int4_handler)
	SAVE_ALL_INT
	CALL_INT 4
ENDPROC(_int4_handler)

ENTRY(_int5_handler)
	SAVE_ALL_INT
	CALL_INT 5
ENDPROC(_int5_handler)

ENTRY(_int6_handler)
	SAVE_ALL_INT
	CALL_INT 6
ENDPROC(_int6_handler)

ENTRY(_int7_handler)
	SAVE_ALL_INT
	CALL_INT 7
ENDPROC(_int7_handler)

ENTRY(_int8_handler)
	SAVE_ALL_INT
	CALL_INT 8
ENDPROC(_int8_handler)

ENTRY(_int9_handler)
	SAVE_ALL_INT
	CALL_INT 9
ENDPROC(_int9_handler)

ENTRY(_int10_handler)
	SAVE_ALL_INT
	CALL_INT 10
ENDPROC(_int10_handler)

ENTRY(_int11_handler)
	SAVE_ALL_INT
	CALL_INT 11
ENDPROC(_int11_handler)

ENTRY(_int12_handler)
	SAVE_ALL_INT
	CALL_INT 12
ENDPROC(_int12_handler)

ENTRY(_int13_handler)
	SAVE_ALL_INT
	CALL_INT 13
ENDPROC(_int13_handler)

ENTRY(_int14_handler)
	SAVE_ALL_INT
	CALL_INT 14
ENDPROC(_int14_handler)

ENTRY(_int15_handler)
	SAVE_ALL_INT
	CALL_INT 15
ENDPROC(_int15_handler)

	;;
	;; Handler for uninitialized and spurious interrupts
	;;
ENTRY(_bad_interrupt)
	B	.S2	IRP
	NOP	5
ENDPROC(_bad_interrupt)

	;;
	;; Entry for NMI/exceptions/syscall
	;;
ENTRY(_nmi_handler)
	SAVE_ALL NRP,NTSR

	MVC	.S2	EFR,B2
	CMPEQ	.L2	1,B2,B2
 ||	MVC	.S2	TSR,B1
	CLR	.S2	B1,10,10,B1
	MVC	.S2	B1,TSR
#ifdef CONFIG_C6X_BIG_KERNEL
 [!B2]	MVKL	.S1	process_exception,A0
 [!B2]	MVKH	.S1	process_exception,A0
 [!B2]	B	.S2X	A0
#else
 [!B2]	B	.S2	process_exception
#endif
 [B2]	B	.S2	system_call_saved
 [!B2]	ADDAW	.D2	SP,2,B1
 [!B2]	MV	.D1X	B1,A4
	ADDKPC	.S2	ret_from_trap,B3,2

ret_from_trap:
	MV	.D2X	A4,B0
 [!B0]	BNOP	.S2	ret_from_exception,5

#ifdef CONFIG_C6X_BIG_KERNEL
	MVKL	.S2	system_call_saved_noack,B3
	MVKH	.S2	system_call_saved_noack,B3
#endif
	LDW	.D2T2	*+SP(REGS_B0+8),B0
	LDW	.D2T1	*+SP(REGS_A4+8),A4
	LDW	.D2T2	*+SP(REGS_B4+8),B4
	LDW	.D2T1	*+SP(REGS_A6+8),A6
	LDW	.D2T2	*+SP(REGS_B6+8),B6
	LDW	.D2T1	*+SP(REGS_A8+8),A8
#ifdef CONFIG_C6X_BIG_KERNEL
 ||	B	.S2	B3
#else
 ||	B	.S2	system_call_saved_noack
#endif
	LDW	.D2T2	*+SP(REGS_B8+8),B8
	NOP	4
ENDPROC(_nmi_handler)

	;;
	;; Jump to schedule() then return to ret_from_isr
	;;
#ifdef	CONFIG_PREEMPT
resume_kernel:
	GET_THREAD_INFO A12
	LDW	.D1T1	*+A12(THREAD_INFO_PREEMPT_COUNT),A1
	NOP	4
 [A1]	BNOP	.S2	restore_all,5

preempt_schedule:
	GET_THREAD_INFO A2
	LDW	.D1T1	*+A2(THREAD_INFO_FLAGS),A1
#ifdef CONFIG_C6X_BIG_KERNEL
	MVKL	.S2	preempt_schedule_irq,B0
	MVKH	.S2	preempt_schedule_irq,B0
	NOP	2
#else
	NOP	4
#endif
	AND	.D1	_TIF_NEED_RESCHED,A1,A1
 [!A1]	BNOP	.S2	restore_all,5
#ifdef CONFIG_C6X_BIG_KERNEL
	B	.S2	B0
#else
	B	.S2	preempt_schedule_irq
#endif
	ADDKPC	.S2	preempt_schedule,B3,4
#endif /* CONFIG_PREEMPT */

ENTRY(enable_exception)
	DINT
	MVC	.S2	TSR,B0
	MVC	.S2	B3,NRP
	MVK	.L2	0xc,B1
	OR	.D2	B0,B1,B0
	MVC	.S2	B0,TSR			;  Set GEE and XEN in TSR
	B	.S2	NRP
	NOP	5
ENDPROC(enable_exception)

ENTRY(sys_sigaltstack)
#ifdef CONFIG_C6X_BIG_KERNEL
	MVKL	.S1	do_sigaltstack,A0	; branch to do_sigaltstack
	MVKH	.S1	do_sigaltstack,A0
	B	.S2X	A0
#else
	B	.S2	do_sigaltstack
#endif
	LDW	.D2T1	*+SP(REGS_SP+8),A6
	NOP	4
ENDPROC(sys_sigaltstack)

	;; kernel_execve
ENTRY(kernel_execve)
	MVK	.S2	__NR_execve,B0
	SWE
	BNOP	.S2	B3,5
ENDPROC(kernel_execve)

	;;
	;; Special system calls
	;; return address is in B3
	;;
ENTRY(sys_clone)
	ADD	.D1X	SP,8,A4
#ifdef CONFIG_C6X_BIG_KERNEL
 ||	MVKL	.S1	sys_c6x_clone,A0
	MVKH	.S1	sys_c6x_clone,A0
	BNOP	.S2X	A0,5
#else
 ||	B	.S2	sys_c6x_clone
	NOP	5
#endif
ENDPROC(sys_clone)

ENTRY(sys_rt_sigreturn)
	ADD	.D1X	SP,8,A4
#ifdef CONFIG_C6X_BIG_KERNEL
 ||	MVKL	.S1	do_rt_sigreturn,A0
	MVKH	.S1	do_rt_sigreturn,A0
	BNOP	.S2X	A0,5
#else
 ||	B	.S2	do_rt_sigreturn
	NOP	5
#endif
ENDPROC(sys_rt_sigreturn)

ENTRY(sys_execve)
	ADDAW	.D2	SP,2,B6		; put regs addr in 4th parameter
					; & adjust regs stack addr
	LDW	.D2T2	*+SP(REGS_B4+8),B4

	;; c6x_execve(char *name, char **argv,
	;;            char **envp, struct pt_regs *regs)
#ifdef CONFIG_C6X_BIG_KERNEL
 ||	MVKL	.S1	sys_c6x_execve,A0
	MVKH	.S1	sys_c6x_execve,A0
	B	.S2X	A0
#else
 ||	B	.S2	sys_c6x_execve
#endif
	STW	.D2T2	B3,*SP--[2]
	ADDKPC	.S2	ret_from_c6x_execve,B3,3

ret_from_c6x_execve:
	LDW	.D2T2	*++SP[2],B3
	NOP	4
	BNOP	.S2	B3,5
ENDPROC(sys_execve)

ENTRY(sys_pread_c6x)
	MV	.D2X	A8,B7
#ifdef CONFIG_C6X_BIG_KERNEL
 ||	MVKL	.S1	sys_pread64,A0
	MVKH	.S1	sys_pread64,A0
	BNOP	.S2X	A0,5
#else
 ||	B	.S2	sys_pread64
	NOP	5
#endif
ENDPROC(sys_pread_c6x)

ENTRY(sys_pwrite_c6x)
	MV	.D2X	A8,B7
#ifdef CONFIG_C6X_BIG_KERNEL
 ||	MVKL	.S1	sys_pwrite64,A0
	MVKH	.S1	sys_pwrite64,A0
	BNOP	.S2X	A0,5
#else
 ||	B	.S2	sys_pwrite64
	NOP	5
#endif
ENDPROC(sys_pwrite_c6x)

;; On Entry
;;   A4 - path
;;   B4 - offset_lo (LE), offset_hi (BE)
;;   A6 - offset_lo (BE), offset_hi (LE)
ENTRY(sys_truncate64_c6x)
#ifdef CONFIG_CPU_BIG_ENDIAN
	MV	.S2	B4,B5
	MV	.D2X	A6,B4
#else
	MV	.D2X	A6,B5
#endif
#ifdef CONFIG_C6X_BIG_KERNEL
 ||	MVKL	.S1	sys_truncate64,A0
	MVKH	.S1	sys_truncate64,A0
	BNOP	.S2X	A0,5
#else
 ||	B	.S2	sys_truncate64
	NOP	5
#endif
ENDPROC(sys_truncate64_c6x)

;; On Entry
;;   A4 - fd
;;   B4 - offset_lo (LE), offset_hi (BE)
;;   A6 - offset_lo (BE), offset_hi (LE)
ENTRY(sys_ftruncate64_c6x)
#ifdef CONFIG_CPU_BIG_ENDIAN
	MV	.S2	B4,B5
	MV	.D2X	A6,B4
#else
	MV	.D2X	A6,B5
#endif
#ifdef CONFIG_C6X_BIG_KERNEL
 ||	MVKL	.S1	sys_ftruncate64,A0
	MVKH	.S1	sys_ftruncate64,A0
	BNOP	.S2X	A0,5
#else
 ||	B	.S2	sys_ftruncate64
	NOP	5
#endif
ENDPROC(sys_ftruncate64_c6x)

;; On Entry
;;   A4 - fd
;;   B4 - offset_lo (LE), offset_hi (BE)
;;   A6 - offset_lo (BE), offset_hi (LE)
;;   B6 - len_lo (LE), len_hi (BE)
;;   A8 - len_lo (BE), len_hi (LE)
;;   B8 - advice
ENTRY(sys_fadvise64_64_c6x)
#ifdef CONFIG_C6X_BIG_KERNEL
	MVKL	.S1	sys_fadvise64_64,A0
	MVKH	.S1	sys_fadvise64_64,A0
	BNOP	.S2X	A0,2
#else
	B	.S2	sys_fadvise64_64
	NOP	2
#endif
#ifdef CONFIG_CPU_BIG_ENDIAN
	MV	.L2	B4,B5
 ||	MV	.D2X	A6,B4
	MV	.L1	A8,A6
 ||	MV	.D1X	B6,A7
#else
	MV	.D2X	A6,B5
	MV	.L1	A8,A7
 ||	MV	.D1X	B6,A6
#endif
	MV	.L2	B8,B6
ENDPROC(sys_fadvise64_64_c6x)

;; On Entry
;;   A4 - fd
;;   B4 - mode
;;   A6 - offset_hi
;;   B6 - offset_lo
;;   A8 - len_hi
;;   B8 - len_lo
ENTRY(sys_fallocate_c6x)
#ifdef CONFIG_C6X_BIG_KERNEL
	MVKL	.S1	sys_fallocate,A0
	MVKH	.S1	sys_fallocate,A0
	BNOP	.S2X	A0,1
#else
	B	.S2	sys_fallocate
	NOP
#endif
	MV	.D1	A6,A7
	MV	.D1X	B6,A6
	MV	.D2X	A8,B7
	MV	.D2	B8,B6
ENDPROC(sys_fallocate_c6x)

	;; put this in .neardata for faster access when using DSBT mode
	.section .neardata,"aw",@progbits
	.global	current_ksp
	.hidden	current_ksp
current_ksp:
	.word	init_thread_union + THREAD_START_SP
