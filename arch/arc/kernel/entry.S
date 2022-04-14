/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Common Low Level Interrupts/Traps/Exceptions(non-TLB) Handling for ARC
 * (included from entry-<isa>.S
 *
 * Copyright (C) 2014-15 Synopsys, Inc. (www.synopsys.com)
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

/*------------------------------------------------------------------
 *    Function                            ABI
 *------------------------------------------------------------------
 *
 *  Arguments                           r0 - r7
 *  Caller Saved Registers              r0 - r12
 *  Callee Saved Registers              r13- r25
 *  Global Pointer (gp)                 r26
 *  Frame Pointer (fp)                  r27
 *  Stack Pointer (sp)                  r28
 *  Branch link register (blink)        r31
 *------------------------------------------------------------------
 */

;################### Special Sys Call Wrappers ##########################

ENTRY(sys_clone_wrapper)
	SAVE_CALLEE_SAVED_USER
	bl  @sys_clone
	DISCARD_CALLEE_SAVED_USER

	GET_CURR_THR_INFO_FLAGS   r10
	btst r10, TIF_SYSCALL_TRACE
	bnz  tracesys_exit

	b .Lret_from_system_call
END(sys_clone_wrapper)

ENTRY(sys_clone3_wrapper)
	SAVE_CALLEE_SAVED_USER
	bl  @sys_clone3
	DISCARD_CALLEE_SAVED_USER

	GET_CURR_THR_INFO_FLAGS   r10
	btst r10, TIF_SYSCALL_TRACE
	bnz  tracesys_exit

	b .Lret_from_system_call
END(sys_clone3_wrapper)

ENTRY(ret_from_fork)
	; when the forked child comes here from the __switch_to function
	; r0 has the last task pointer.
	; put last task in scheduler queue
	jl   @schedule_tail

	ld   r9, [sp, PT_status32]
	brne r9, 0, 1f

	jl.d [r14]		; kernel thread entry point
	mov  r0, r13		; (see PF_KTHREAD block in copy_thread)

1:
	; Return to user space
	; 1. Any forked task (Reach here via BRne above)
	; 2. First ever init task (Reach here via return from JL above)
	;    This is the historic "kernel_execve" use-case, to return to init
	;    user mode, in a round about way since that is always done from
	;    a kernel thread which is executed via JL above but always returns
	;    out whenever kernel_execve (now inline do_fork()) is involved
	b    ret_from_exception
END(ret_from_fork)

;################### Non TLB Exception Handling #############################

; ---------------------------------------------
; Instruction Error Exception Handler
; ---------------------------------------------

ENTRY(instr_service)

	EXCEPTION_PROLOGUE

	lr  r0, [efa]
	mov r1, sp

	FAKE_RET_FROM_EXCPN

	bl  do_insterror_or_kprobe
	b   ret_from_exception
END(instr_service)

; ---------------------------------------------
; Machine Check Exception Handler
; ---------------------------------------------

ENTRY(EV_MachineCheck)

	EXCEPTION_PROLOGUE

	lr  r2, [ecr]
	lr  r0, [efa]
	mov r1, sp

	; hardware auto-disables MMU, re-enable it to allow kernel vaddr
	; access for say stack unwinding of modules for crash dumps
	lr	r3, [ARC_REG_PID]
	or	r3, r3, MMU_ENABLE
	sr	r3, [ARC_REG_PID]

	lsr  	r3, r2, 8
	bmsk 	r3, r3, 7
	brne    r3, ECR_C_MCHK_DUP_TLB, 1f

	bl      do_tlb_overlap_fault
	b       ret_from_exception

1:
	; DEAD END: can't do much, display Regs and HALT
	SAVE_CALLEE_SAVED_USER

	GET_CURR_TASK_FIELD_PTR   TASK_THREAD, r10
	st  sp, [r10, THREAD_CALLEE_REG]

	j  do_machine_check_fault

END(EV_MachineCheck)

; ---------------------------------------------
; Privilege Violation Exception Handler
; ---------------------------------------------
ENTRY(EV_PrivilegeV)

	EXCEPTION_PROLOGUE

	lr  r0, [efa]
	mov r1, sp

	FAKE_RET_FROM_EXCPN

	bl  do_privilege_fault
	b   ret_from_exception
END(EV_PrivilegeV)

; ---------------------------------------------
; Extension Instruction Exception Handler
; ---------------------------------------------
ENTRY(EV_Extension)

	EXCEPTION_PROLOGUE

	lr  r0, [efa]
	mov r1, sp

	FAKE_RET_FROM_EXCPN

	bl  do_extension_fault
	b   ret_from_exception
END(EV_Extension)

;################ Trap Handling (Syscall, Breakpoint) ##################

; ---------------------------------------------
; syscall Tracing
; ---------------------------------------------
tracesys:
	; save EFA in case tracer wants the PC of traced task
	; using ERET won't work since next-PC has already committed
	GET_CURR_TASK_FIELD_PTR   TASK_THREAD, r11
	st  r12, [r11, THREAD_FAULT_ADDR]	; thread.fault_address

	; PRE Sys Call Ptrace hook
	mov r0, sp			; pt_regs needed
	bl  @syscall_trace_entry

	; Tracing code now returns the syscall num (orig or modif)
	mov r8, r0

	; Do the Sys Call as we normally would.
	; Validate the Sys Call number
	cmp     r8,  NR_syscalls - 1
	mov.hi  r0, -ENOSYS
	bhi     tracesys_exit

	; Restore the sys-call args. Mere invocation of the hook abv could have
	; clobbered them (since they are in scratch regs). The tracer could also
	; have deliberately changed the syscall args: r0-r7
	ld  r0, [sp, PT_r0]
	ld  r1, [sp, PT_r1]
	ld  r2, [sp, PT_r2]
	ld  r3, [sp, PT_r3]
	ld  r4, [sp, PT_r4]
	ld  r5, [sp, PT_r5]
	ld  r6, [sp, PT_r6]
	ld  r7, [sp, PT_r7]
	ld.as   r9, [sys_call_table, r8]
	jl      [r9]        ; Entry into Sys Call Handler

tracesys_exit:
	st  r0, [sp, PT_r0]     ; sys call return value in pt_regs

	;POST Sys Call Ptrace Hook
	mov r0, sp		; pt_regs needed
	bl  @syscall_trace_exit
	b   ret_from_exception ; NOT ret_from_system_call at is saves r0 which
	; we'd done before calling post hook above

; ---------------------------------------------
; Breakpoint TRAP
; ---------------------------------------------
trap_with_param:
	mov r0, r12	; EFA in case ptracer/gdb wants stop_pc
	mov r1, sp

	; Save callee regs in case gdb wants to have a look
	; SP will grow up by size of CALLEE Reg-File
	; NOTE: clobbers r12
	SAVE_CALLEE_SAVED_USER

	; save location of saved Callee Regs @ thread_struct->pc
	GET_CURR_TASK_FIELD_PTR   TASK_THREAD, r10
	st  sp, [r10, THREAD_CALLEE_REG]

	; Call the trap handler
	bl  do_non_swi_trap

	; unwind stack to discard Callee saved Regs
	DISCARD_CALLEE_SAVED_USER

	b   ret_from_exception

; ---------------------------------------------
; syscall TRAP
; ABI: (r0-r7) upto 8 args, (r8) syscall number
; ---------------------------------------------

ENTRY(EV_Trap)

	EXCEPTION_PROLOGUE

	lr  r12, [efa]

	FAKE_RET_FROM_EXCPN

	;============ TRAP 1   :breakpoints
	; Check ECR for trap with arg (PROLOGUE ensures r10 has ECR)
	bmsk.f 0, r10, 7
	bnz    trap_with_param

	;============ TRAP  (no param): syscall top level

	; If syscall tracing ongoing, invoke pre-post-hooks
	GET_CURR_THR_INFO_FLAGS   r10
	btst r10, TIF_SYSCALL_TRACE
	bnz tracesys  ; this never comes back

	;============ Normal syscall case

	; syscall num shd not exceed the total system calls avail
	cmp     r8,  NR_syscalls - 1
	mov.hi  r0, -ENOSYS
	bhi     .Lret_from_system_call

	; Offset into the syscall_table and call handler
	ld.as   r9,[sys_call_table, r8]
	jl      [r9]        ; Entry into Sys Call Handler

.Lret_from_system_call:

	st  r0, [sp, PT_r0]     ; sys call return value in pt_regs

	; fall through to ret_from_exception
END(EV_Trap)

;############# Return from Intr/Excp/Trap (Linux Specifics) ##############
;
; If ret to user mode do we need to handle signals, schedule() et al.

ENTRY(ret_from_exception)

	; Pre-{IRQ,Trap,Exception} K/U mode from pt_regs->status32
	ld  r8, [sp, PT_status32]   ; returning to User/Kernel Mode

	bbit0  r8, STATUS_U_BIT, resume_kernel_mode

	; Before returning to User mode check-for-and-complete any pending work
	; such as rescheduling/signal-delivery etc.
resume_user_mode_begin:

	; Disable IRQs to ensures that chk for pending work itself is atomic
	; (and we don't end up missing a NEED_RESCHED/SIGPENDING due to an
	; interim IRQ).
	IRQ_DISABLE	r10

	; Fast Path return to user mode if no pending work
	GET_CURR_THR_INFO_FLAGS   r9
	and.f  0,  r9, _TIF_WORK_MASK
	bz     .Lrestore_regs

	; --- (Slow Path #1) task preemption ---
	bbit0  r9, TIF_NEED_RESCHED, .Lchk_pend_signals
	mov    blink, resume_user_mode_begin  ; tail-call to U mode ret chks
	j      @schedule 	; BTST+Bnz causes relo error in link

.Lchk_pend_signals:
	IRQ_ENABLE	r10

	; --- (Slow Path #2) pending signal  ---
	mov r0, sp	; pt_regs for arg to do_signal()/do_notify_resume()

	GET_CURR_THR_INFO_FLAGS   r9
	bbit0  r9, TIF_SIGPENDING, .Lchk_notify_resume

	; Normal Trap/IRQ entry only saves Scratch (caller-saved) regs
	; in pt_reg since the "C" ABI (kernel code) will automatically
	; save/restore callee-saved regs.
	;
	; However, here we need to explicitly save callee regs because
	; (i)  If this signal causes coredump - full regfile needed
	; (ii) If signal is SIGTRAP/SIGSTOP, task is being traced thus
	;      tracer might call PEEKUSR(CALLEE reg)
	;
	; NOTE: SP will grow up by size of CALLEE Reg-File
	SAVE_CALLEE_SAVED_USER		; clobbers r12

	; save location of saved Callee Regs @ thread_struct->callee
	GET_CURR_TASK_FIELD_PTR   TASK_THREAD, r10
	st  sp, [r10, THREAD_CALLEE_REG]

	bl  @do_signal

	; Ideally we want to discard the Callee reg above, however if this was
	; a tracing signal, tracer could have done a POKEUSR(CALLEE reg)
	RESTORE_CALLEE_SAVED_USER

	b      resume_user_mode_begin	; loop back to start of U mode ret

	; --- (Slow Path #3) notify_resume ---
.Lchk_notify_resume:
	btst   r9, TIF_NOTIFY_RESUME
	blnz   @do_notify_resume
	b      resume_user_mode_begin	; unconditionally back to U mode ret chks
					; for single exit point from this block

resume_kernel_mode:

	; Disable Interrupts from this point on
	; CONFIG_PREEMPTION: This is a must for preempt_schedule_irq()
	; !CONFIG_PREEMPTION: To ensure restore_regs is intr safe
	IRQ_DISABLE	r9

#ifdef CONFIG_PREEMPTION

	; Can't preempt if preemption disabled
	GET_CURR_THR_INFO_FROM_SP   r10
	ld  r8, [r10, THREAD_INFO_PREEMPT_COUNT]
	brne  r8, 0, .Lrestore_regs

	; check if this task's NEED_RESCHED flag set
	ld  r9, [r10, THREAD_INFO_FLAGS]
	bbit0  r9, TIF_NEED_RESCHED, .Lrestore_regs

	; Invoke PREEMPTION
	jl      preempt_schedule_irq

	; preempt_schedule_irq() always returns with IRQ disabled
#endif

	b	.Lrestore_regs

##### DONT ADD CODE HERE - .Lrestore_regs actually follows in entry-<isa>.S

