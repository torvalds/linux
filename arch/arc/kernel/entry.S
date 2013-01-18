/*
 * Low Level Interrupts/Traps/Exceptions(non-TLB) Handling for ARC
 *
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * vineetg: Nov 2010:
 *  -Vector table jumps (@8 bytes) converted into branches (@4 bytes)
 *  -To maintain the slot size of 8 bytes/vector, added nop, which is
 *   not executed at runtime.
 *
 * vineetg: Nov 2009 (Everything needed for TIF_RESTORE_SIGMASK)
 *  -do_signal()invoked upon TIF_RESTORE_SIGMASK as well
 *  -Wrappers for sys_{,rt_}sigsuspend() nolonger needed as they don't
 *   need ptregs anymore
 *
 * Vineetg: Oct 2009
 *  -In a rare scenario, Process gets a Priv-V exception and gets scheduled
 *   out. Since we don't do FAKE RTIE for Priv-V, CPU excpetion state remains
 *   active (AE bit enabled).  This causes a double fault for a subseq valid
 *   exception. Thus FAKE RTIE needed in low level Priv-Violation handler.
 *   Instr Error could also cause similar scenario, so same there as well.
 *
 * Vineetg: Aug 28th 2008: Bug #94984
 *  -Zero Overhead Loop Context shd be cleared when entering IRQ/EXcp/Trap
 *   Normally CPU does this automatically, however when doing FAKE rtie,
 *   we need to explicitly do this. The problem in macros
 *   FAKE_RET_FROM_EXCPN and FAKE_RET_FROM_EXCPN_LOCK_IRQ was that this bit
 *   was being "CLEARED" rather then "SET". Since it is Loop INHIBIT Bit,
 *   setting it and not clearing it clears ZOL context
 *
 * Vineetg: Dec 22, 2007
 *    Minor Surgery of Low Level ISR to make it SMP safe
 *    - MMU_SCRATCH0 Reg used for freeing up r9 in Level 1 ISR
 *    - _current_task is made an array of NR_CPUS
 *    - Access of _current_task wrapped inside a macro so that if hardware
 *       team agrees for a dedicated reg, no other code is touched
 *
 * Amit Bhor, Rahul Trivedi, Kanika Nema, Sameer Dhavale : Codito Tech 2004
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
 *  Interrupt link register (ilink1)    r29
 *  Interrupt link register (ilink2)    r30
 *  Branch link register (blink)        r31
 *------------------------------------------------------------------
 */

	.cpu A7

;############################ Vector Table #################################

.macro VECTOR  lbl
#if 1   /* Just in case, build breaks */
	j   \lbl
#else
	b   \lbl
	nop
#endif
.endm

	.section .vector, "ax",@progbits
	.align 4

/* Each entry in the vector table must occupy 2 words. Since it is a jump
 * across sections (.vector to .text) we are gauranteed that 'j somewhere'
 * will use the 'j limm' form of the intrsuction as long as somewhere is in
 * a section other than .vector.
 */

; ********* Critical System Events **********************
VECTOR   res_service             ; 0x0, Restart Vector  (0x0)
VECTOR   mem_service             ; 0x8, Mem exception   (0x1)
VECTOR   instr_service           ; 0x10, Instrn Error   (0x2)

; ******************** Device ISRs **********************
VECTOR   handle_interrupt_level1

VECTOR   handle_interrupt_level1

VECTOR   handle_interrupt_level1

VECTOR   handle_interrupt_level1

.rept   25
VECTOR   handle_interrupt_level1 ; Other devices
.endr

/* FOR ARC600: timer = 0x3, uart = 0x8, emac = 0x10 */

; ******************** Exceptions **********************
VECTOR   EV_MachineCheck         ; 0x100, Fatal Machine check   (0x20)
VECTOR   EV_TLBMissI             ; 0x108, Intruction TLB miss   (0x21)
VECTOR   EV_TLBMissD             ; 0x110, Data TLB miss         (0x22)
VECTOR   EV_TLBProtV             ; 0x118, Protection Violation  (0x23)
				 ;         or Misaligned Access
VECTOR   EV_PrivilegeV           ; 0x120, Privilege Violation   (0x24)
VECTOR   EV_Trap                 ; 0x128, Trap exception        (0x25)
VECTOR   EV_Extension            ; 0x130, Extn Intruction Excp  (0x26)

.rept   24
VECTOR   reserved                ; Reserved Exceptions
.endr

#include <linux/linkage.h>   /* ARC_{EXTRY,EXIT} */
#include <asm/entry.h>       /* SAVE_ALL_{INT1,INT2,TRAP...} */
#include <asm/errno.h>
#include <asm/arcregs.h>
#include <asm/irqflags.h>

;##################### Scratch Mem for IRQ stack switching #############

	.section .data		; NOT .global
	.align 32
	.type   int1_saved_reg, @object
	.size   int1_saved_reg, 4
int1_saved_reg:
	.zero 4

; ---------------------------------------------
	.section .text, "ax",@progbits

res_service:		; processor restart
	flag    0x1     ; not implemented
	nop
	nop

reserved:		; processor restart
	rtie            ; jump to processor initializations

;##################### Interrupt Handling ##############################

; ---------------------------------------------
;  Level 1 ISR
; ---------------------------------------------
ARC_ENTRY handle_interrupt_level1

	/* free up r9 as scratchpad */
	st   r9, [@int1_saved_reg]

	;Which mode (user/kernel) was the system in when intr occured
	lr  r9, [status32_l1]

	SWITCH_TO_KERNEL_STK
	SAVE_ALL_INT1

	lr  r0, [icause1]
	and r0, r0, 0x1f

	bl.d  @arch_do_IRQ
	mov r1, sp

	mov r8,0x1
	sr r8, [AUX_IRQ_LV12]       ; clear bit in Sticky Status Reg

	b   ret_from_exception
ARC_EXIT handle_interrupt_level1

;################### Non TLB Exception Handling #############################

; ---------------------------------------------
; Instruction Error Exception Handler
; ---------------------------------------------

ARC_ENTRY instr_service

	EXCPN_PROLOG_FREEUP_REG r9

	lr  r9, [erstatus]

	SWITCH_TO_KERNEL_STK
	SAVE_ALL_SYS

	lr  r0, [ecr]
	lr  r1, [efa]

	mov r2, sp

	FAKE_RET_FROM_EXCPN r9

	bl  do_insterror_or_kprobe
	b   ret_from_exception
ARC_EXIT instr_service

; ---------------------------------------------
; Memory Error Exception Handler
; ---------------------------------------------

ARC_ENTRY mem_service

	EXCPN_PROLOG_FREEUP_REG r9

	lr  r9, [erstatus]

	SWITCH_TO_KERNEL_STK
	SAVE_ALL_SYS

	lr  r0, [ecr]
	lr  r1, [efa]
	mov r2, sp
	bl  do_memory_error
	b   ret_from_exception
ARC_EXIT mem_service

; ---------------------------------------------
; Machine Check Exception Handler
; ---------------------------------------------

ARC_ENTRY EV_MachineCheck

	EXCPN_PROLOG_FREEUP_REG r9
	lr  r9, [erstatus]

	SWITCH_TO_KERNEL_STK
	SAVE_ALL_SYS

	lr  r0, [ecr]
	lr  r1, [efa]
	mov r2, sp

	brne    r0, 0x200100, 1f
	bl      do_tlb_overlap_fault
	b       ret_from_exception

1:
	; DEAD END: can't do much, display Regs and HALT
	SAVE_CALLEE_SAVED_USER

	GET_CURR_TASK_FIELD_PTR   TASK_THREAD, r10
	st  sp, [r10, THREAD_CALLEE_REG]

	j  do_machine_check_fault

ARC_EXIT EV_MachineCheck

; ---------------------------------------------
; Protection Violation Exception Handler
; ---------------------------------------------

ARC_ENTRY EV_TLBProtV

	EXCPN_PROLOG_FREEUP_REG r9

	;Which mode (user/kernel) was the system in when Exception occured
	lr  r9, [erstatus]

	SWITCH_TO_KERNEL_STK
	SAVE_ALL_SYS

	;---------(3) Save some more regs-----------------
	;  vineetg: Mar 6th: Random Seg Fault issue #1
	;  ecr and efa were not saved in case an Intr sneaks in
	;  after fake rtie
	;
	lr  r3, [ecr]
	lr  r4, [efa]

	; --------(4) Return from CPU Exception Mode ---------
	;  Fake a rtie, but rtie to next label
	;  That way, subsequently, do_page_fault ( ) executes in pure kernel
	;  mode with further Exceptions enabled

	FAKE_RET_FROM_EXCPN r9

	;------ (5) Type of Protection Violation? ----------
	;
	; ProtV Hardware Exception is triggered for Access Faults of 2 types
	;   -Access Violaton (WRITE to READ ONLY Page) - for linux COW
	;   -Unaligned Access (READ/WRITE on odd boundary)
	;
	cmp r3, 0x230400    ; Misaligned data access ?
	beq 4f

	;========= (6a) Access Violation Processing ========
	cmp r3, 0x230100
	mov r1, 0x0              ; if LD exception ? write = 0
	mov.ne r1, 0x1           ; else write = 1

	mov r2, r4              ; faulting address
	mov r0, sp              ; pt_regs
	bl  do_page_fault
	b   ret_from_exception

	;========== (6b) Non aligned access ============
4:
	mov r0, r3              ; cause code
	mov r1, r4              ; faulting address
	mov r2, sp              ; pt_regs

	bl  do_misaligned_access
	b   ret_from_exception

ARC_EXIT EV_TLBProtV

; ---------------------------------------------
; Privilege Violation Exception Handler
; ---------------------------------------------
ARC_ENTRY EV_PrivilegeV

	EXCPN_PROLOG_FREEUP_REG r9

	lr  r9, [erstatus]

	SWITCH_TO_KERNEL_STK
	SAVE_ALL_SYS

	lr  r0, [ecr]
	lr  r1, [efa]
	mov r2, sp

	FAKE_RET_FROM_EXCPN r9

	bl  do_privilege_fault
	b   ret_from_exception
ARC_EXIT EV_PrivilegeV

; ---------------------------------------------
; Extension Instruction Exception Handler
; ---------------------------------------------
ARC_ENTRY EV_Extension

	EXCPN_PROLOG_FREEUP_REG r9
	lr  r9, [erstatus]

	SWITCH_TO_KERNEL_STK
	SAVE_ALL_SYS

	lr  r0, [ecr]
	lr  r1, [efa]
	mov r2, sp
	bl  do_extension_fault
	b   ret_from_exception
ARC_EXIT EV_Extension

;################### Break Point TRAP ##########################

	; ======= (5b) Trap is due to Break-Point =========

trap_with_param:

	;make sure orig_r8 is a positive value
	st  NR_syscalls + 2, [sp, PT_orig_r8]

	mov r0, r12
	lr  r1, [efa]
	mov r2, sp

	; Now that we have read EFA, its safe to do "fake" rtie
	;   and get out of CPU exception mode
	FAKE_RET_FROM_EXCPN r11

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

;##################### Trap Handling ##############################
;
; EV_Trap caused by TRAP_S and TRAP0 instructions.
;------------------------------------------------------------------
;   (1) System Calls
;       :parameters in r0-r7.
;       :r8 has the system call number
;   (2) Break Points
;------------------------------------------------------------------

ARC_ENTRY EV_Trap

	; Need at least 1 reg to code the early exception prolog
	EXCPN_PROLOG_FREEUP_REG r9

	;Which mode (user/kernel) was the system in when intr occured
	lr  r9, [erstatus]

	SWITCH_TO_KERNEL_STK
	SAVE_ALL_TRAP

	;------- (4) What caused the Trap --------------
	lr     r12, [ecr]
	and.f  0, r12, ECR_PARAM_MASK
	bnz    trap_with_param

	; ======= (5a) Trap is due to System Call ========

	; Before doing anything, return from CPU Exception Mode
	FAKE_RET_FROM_EXCPN r11

	;============ This is normal System Call case ==========
	; Sys-call num shd not exceed the total system calls avail
	cmp     r8,  NR_syscalls
	mov.hi  r0, -ENOSYS
	bhi     ret_from_system_call

	; Offset into the syscall_table and call handler
	ld.as   r9,[sys_call_table, r8]
	jl      [r9]        ; Entry into Sys Call Handler

	; fall through to ret_from_system_call
ARC_EXIT EV_Trap

ARC_ENTRY ret_from_system_call

	st  r0, [sp, PT_r0]     ; sys call return value in pt_regs

	; fall through yet again to ret_from_exception

;############# Return from Intr/Excp/Trap (Linux Specifics) ##############
;
; If ret to user mode do we need to handle signals, schedule() et al.

ARC_ENTRY ret_from_exception

	; Pre-{IRQ,Trap,Exception} K/U mode from pt_regs->status32
	ld  r8, [sp, PT_status32]   ; returning to User/Kernel Mode

#ifdef CONFIG_PREEMPT
	bbit0  r8, STATUS_U_BIT, resume_kernel_mode
#else
	bbit0  r8, STATUS_U_BIT, restore_regs
#endif

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
	bz     restore_regs

	; --- (Slow Path #1) task preemption ---
	bbit0  r9, TIF_NEED_RESCHED, .Lchk_pend_signals
	mov    blink, resume_user_mode_begin  ; tail-call to U mode ret chks
	b      @schedule 	; BTST+Bnz causes relo error in link

.Lchk_pend_signals:
	IRQ_ENABLE	r10

	; --- (Slow Path #2) pending signal  ---
	mov r0, sp	; pt_regs for arg to do_signal()/do_notify_resume()

	bbit0  r9, TIF_SIGPENDING, .Lchk_notify_resume

	; save CALLEE Regs.
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

	; unwind SP for cheap discard of Callee saved Regs
	DISCARD_CALLEE_SAVED_USER

	b      resume_user_mode_begin	; loop back to start of U mode ret

	; --- (Slow Path #3) notify_resume ---
.Lchk_notify_resume:
	btst   r9, TIF_NOTIFY_RESUME
	blnz   @do_notify_resume
	b      resume_user_mode_begin	; unconditionally back to U mode ret chks
					; for single exit point from this block

#ifdef CONFIG_PREEMPT

resume_kernel_mode:

	; Can't preempt if preemption disabled
	GET_CURR_THR_INFO_FROM_SP   r10
	ld  r8, [r10, THREAD_INFO_PREEMPT_COUNT]
	brne  r8, 0, restore_regs

	; check if this task's NEED_RESCHED flag set
	ld  r9, [r10, THREAD_INFO_FLAGS]
	bbit0  r9, TIF_NEED_RESCHED, restore_regs

	IRQ_DISABLE	r9

	; Invoke PREEMPTION
	bl      preempt_schedule_irq

	; preempt_schedule_irq() always returns with IRQ disabled
#endif

	; fall through

;############# Return from Intr/Excp/Trap (ARC Specifics) ##############
;
; Restore the saved sys context (common exit-path for EXCPN/IRQ/Trap)
; IRQ shd definitely not happen between now and rtie

restore_regs :

	; Disable Interrupts while restoring reg-file back
	; XXX can this be optimised out
	IRQ_DISABLE_SAVE    r9, r10	;@r10 has prisitine (pre-disable) copy

	; Restore REG File. In case multiple Events outstanding,
	; use the same priorty as rtie: EXCPN, L2 IRQ, L1 IRQ, None
	; Note that we use realtime STATUS32 (not pt_regs->status32) to
	; decide that.

	; if Returning from Exception
	bbit0  r10, STATUS_AE_BIT, not_exception
	RESTORE_ALL_SYS
	rtie

	; Not Exception so maybe Interrupts (Level 1 or 2)

not_exception:

	bbit0  r10, STATUS_A1_BIT, not_level1_interrupt

	;return from level 1

	RESTORE_ALL_INT1
debug_marker_l1:
	rtie

not_level1_interrupt:

	;this case is for syscalls or Exceptions (with fake rtie)

	RESTORE_ALL_SYS
debug_marker_syscall:
	rtie

ARC_EXIT ret_from_exception

ARC_ENTRY ret_from_fork
	; when the forked child comes here from the __switch_to function
	; r0 has the last task pointer.
	; put last task in scheduler queue
	bl   @schedule_tail

	; If kernel thread, jump to it's entry-point
	ld   r9, [sp, PT_status32]
	brne r9, 0, 1f

	jl.d [r14]
	mov  r0, r13		; arg to payload

1:
	; special case of kernel_thread entry point returning back due to
	; kernel_execve() - pretend return from syscall to ret to userland
	b    ret_from_exception
ARC_EXIT ret_from_fork

;################### Special Sys Call Wrappers ##########################

; TBD: call do_fork directly from here
ARC_ENTRY sys_fork_wrapper
	SAVE_CALLEE_SAVED_USER
	bl  @sys_fork
	DISCARD_CALLEE_SAVED_USER

	b ret_from_system_call
ARC_EXIT sys_fork_wrapper

ARC_ENTRY sys_vfork_wrapper
	SAVE_CALLEE_SAVED_USER
	bl  @sys_vfork
	DISCARD_CALLEE_SAVED_USER

	b ret_from_system_call
ARC_EXIT sys_vfork_wrapper

ARC_ENTRY sys_clone_wrapper
	SAVE_CALLEE_SAVED_USER
	bl  @sys_clone
	DISCARD_CALLEE_SAVED_USER

	b ret_from_system_call
ARC_EXIT sys_clone_wrapper
