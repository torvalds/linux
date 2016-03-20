/*
 * Low Level Interrupts/Traps/Exceptions(non-TLB) Handling for ARCompact ISA
 *
 * Copyright (C) 2014-15 Synopsys, Inc. (www.synopsys.com)
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * vineetg: May 2011
 *  -Userspace unaligned access emulation
 *
 * vineetg: Feb 2011 (ptrace low level code fixes)
 *  -traced syscall return code (r0) was not saved into pt_regs for restoring
 *   into user reg-file when traded task rets to user space.
 *  -syscalls needing arch-wrappers (mainly for passing sp as pt_regs)
 *   were not invoking post-syscall trace hook (jumping directly into
 *   ret_from_system_call)
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
 * Vineetg: March 2009 (Supporting 2 levels of Interrupts)
 *
 * Vineetg: Aug 28th 2008: Bug #94984
 *  -Zero Overhead Loop Context shd be cleared when entering IRQ/EXcp/Trap
 *   Normally CPU does this automatically, however when doing FAKE rtie,
 *   we need to explicitly do this. The problem in macros
 *   FAKE_RET_FROM_EXCPN and FAKE_RET_FROM_EXCPN_LOCK_IRQ was that this bit
 *   was being "CLEARED" rather then "SET". Since it is Loop INHIBIT Bit,
 *   setting it and not clearing it clears ZOL context
 *
 * Vineetg: May 16th, 2008
 *  - r25 now contains the Current Task when in kernel
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

#include <linux/errno.h>
#include <linux/linkage.h>	/* {EXTRY,EXIT} */
#include <asm/entry.h>
#include <asm/irqflags.h>

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
VECTOR   res_service             ; 0x0, Reset Vector	(0x0)
VECTOR   mem_service             ; 0x8, Mem exception   (0x1)
VECTOR   instr_service           ; 0x10, Instrn Error   (0x2)

; ******************** Device ISRs **********************
#ifdef CONFIG_ARC_IRQ3_LV2
VECTOR   handle_interrupt_level2
#else
VECTOR   handle_interrupt_level1
#endif

VECTOR   handle_interrupt_level1

#ifdef CONFIG_ARC_IRQ5_LV2
VECTOR   handle_interrupt_level2
#else
VECTOR   handle_interrupt_level1
#endif

#ifdef CONFIG_ARC_IRQ6_LV2
VECTOR   handle_interrupt_level2
#else
VECTOR   handle_interrupt_level1
#endif

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


;##################### Scratch Mem for IRQ stack switching #############

ARCFP_DATA int1_saved_reg
	.align 32
	.type   int1_saved_reg, @object
	.size   int1_saved_reg, 4
int1_saved_reg:
	.zero 4

/* Each Interrupt level needs its own scratch */
ARCFP_DATA int2_saved_reg
	.type   int2_saved_reg, @object
	.size   int2_saved_reg, 4
int2_saved_reg:
	.zero 4

; ---------------------------------------------
	.section .text, "ax",@progbits


reserved:
	flag 1		; Unexpected event, halt

;##################### Interrupt Handling ##############################

#ifdef CONFIG_ARC_COMPACT_IRQ_LEVELS
; ---------------------------------------------
;  Level 2 ISR: Can interrupt a Level 1 ISR
; ---------------------------------------------
ENTRY(handle_interrupt_level2)

	INTERRUPT_PROLOGUE 2

	;------------------------------------------------------
	; if L2 IRQ interrupted a L1 ISR, disable preemption
	;
	; This is to avoid a potential L1-L2-L1 scenario
	;  -L1 IRQ taken
	;  -L2 interrupts L1 (before L1 ISR could run)
	;  -preemption off IRQ, user task in syscall picked to run
	;  -RTIE to userspace
	;	Returns from L2 context fine
	;	But both L1 and L2 re-enabled, so another L1 can be taken
	;	while prev L1 is still unserviced
	;
	;------------------------------------------------------

	; L2 interrupting L1 implies both L2 and L1 active
	; However both A2 and A1 are NOT set in STATUS32, thus
	; need to check STATUS32_L2 to determine if L1 was active

	ld r9, [sp, PT_status32]        ; get statu32_l2 (saved in pt_regs)
	bbit0 r9, STATUS_A1_BIT, 1f     ; L1 not active when L2 IRQ, so normal

	; bump thread_info->preempt_count (Disable preemption)
	GET_CURR_THR_INFO_FROM_SP   r10
	ld      r9, [r10, THREAD_INFO_PREEMPT_COUNT]
	add     r9, r9, 1
	st      r9, [r10, THREAD_INFO_PREEMPT_COUNT]

1:
	;------------------------------------------------------
	; setup params for Linux common ISR and invoke it
	;------------------------------------------------------
	lr  r0, [icause2]
	and r0, r0, 0x1f

	bl.d  @arch_do_IRQ
	mov r1, sp

	mov r8,0x2
	sr r8, [AUX_IRQ_LV12]       ; clear bit in Sticky Status Reg

	b   ret_from_exception

END(handle_interrupt_level2)

#endif

; ---------------------------------------------
; User Mode Memory Bus Error Interrupt Handler
; (Kernel mode memory errors handled via seperate exception vectors)
; ---------------------------------------------
ENTRY(mem_service)

	INTERRUPT_PROLOGUE 2

	mov r0, ilink2
	mov r1, sp

	; User process needs to be killed with SIGBUS, but first need to get
	; out of the L2 interrupt context (drop to pure kernel mode) and jump
	; off to "C" code where SIGBUS in enqueued
	lr  r3, [status32]
	bclr r3, r3, STATUS_A2_BIT
	or  r3, r3, (STATUS_E1_MASK|STATUS_E2_MASK)
	sr  r3, [status32_l2]
	mov ilink2, 1f
	rtie
1:
	bl  do_memory_error
	b   ret_from_exception
END(mem_service)

; ---------------------------------------------
;  Level 1 ISR
; ---------------------------------------------
ENTRY(handle_interrupt_level1)

	INTERRUPT_PROLOGUE 1

	lr  r0, [icause1]
	and r0, r0, 0x1f

#ifdef CONFIG_TRACE_IRQFLAGS
	; icause1 needs to be read early, before calling tracing, which
	; can clobber scratch regs, hence use of stack to stash it
	push r0
	TRACE_ASM_IRQ_DISABLE
	pop  r0
#endif

	bl.d  @arch_do_IRQ
	mov r1, sp

	mov r8,0x1
	sr r8, [AUX_IRQ_LV12]       ; clear bit in Sticky Status Reg

	b   ret_from_exception
END(handle_interrupt_level1)

;################### Non TLB Exception Handling #############################

; ---------------------------------------------
; Protection Violation Exception Handler
; ---------------------------------------------

ENTRY(EV_TLBProtV)

	EXCEPTION_PROLOGUE

	lr  r2, [ecr]
	lr  r0, [efa]	; Faulting Data address (not part of pt_regs saved above)

	; Exception auto-disables further Intr/exceptions.
	; Re-enable them by pretending to return from exception
	; (so rest of handler executes in pure K mode)

	FAKE_RET_FROM_EXCPN

	mov   r1, sp	; Handle to pt_regs

	;------ (5) Type of Protection Violation? ----------
	;
	; ProtV Hardware Exception is triggered for Access Faults of 2 types
	;   -Access Violaton	: 00_23_(00|01|02|03)_00
	;			         x  r  w  r+w
	;   -Unaligned Access	: 00_23_04_00
	;
	bbit1 r2, ECR_C_BIT_PROTV_MISALIG_DATA, 4f

	;========= (6a) Access Violation Processing ========
	bl  do_page_fault
	b   ret_from_exception

	;========== (6b) Non aligned access ============
4:

	SAVE_CALLEE_SAVED_USER
	mov r2, sp              ; callee_regs

	bl  do_misaligned_access

	; TBD: optimize - do this only if a callee reg was involved
	; either a dst of emulated LD/ST or src with address-writeback
	RESTORE_CALLEE_SAVED_USER

	b   ret_from_exception

END(EV_TLBProtV)

; Wrapper for Linux page fault handler called from EV_TLBMiss*
; Very similar to ProtV handler case (6a) above, but avoids the extra checks
; for Misaligned access
;
ENTRY(call_do_page_fault)

	EXCEPTION_PROLOGUE
	lr  r0, [efa]	; Faulting Data address
	mov   r1, sp
	FAKE_RET_FROM_EXCPN

	mov blink, ret_from_exception
	b  do_page_fault

END(call_do_page_fault)

;############# Common Handlers for ARCompact and ARCv2 ##############

#include "entry.S"

;############# Return from Intr/Excp/Trap (ARC Specifics) ##############
;
; Restore the saved sys context (common exit-path for EXCPN/IRQ/Trap)
; IRQ shd definitely not happen between now and rtie
; All 2 entry points to here already disable interrupts

.Lrestore_regs:

	TRACE_ASM_IRQ_ENABLE

	lr	r10, [status32]

	; Restore REG File. In case multiple Events outstanding,
	; use the same priorty as rtie: EXCPN, L2 IRQ, L1 IRQ, None
	; Note that we use realtime STATUS32 (not pt_regs->status32) to
	; decide that.

	and.f	0, r10, (STATUS_A1_MASK|STATUS_A2_MASK)
	bz	.Lexcep_or_pure_K_ret

	; Returning from Interrupts (Level 1 or 2)

#ifdef CONFIG_ARC_COMPACT_IRQ_LEVELS

	; Level 2 interrupt return Path - from hardware standpoint
	bbit0  r10, STATUS_A2_BIT, not_level2_interrupt

	;------------------------------------------------------------------
	; However the context returning might not have taken L2 intr itself
	; e.g. Task'A' user-code -> L2 intr -> schedule -> 'B' user-code ret
	; Special considerations needed for the context which took L2 intr

	ld   r9, [sp, PT_event]        ; Ensure this is L2 intr context
	brne r9, event_IRQ2, 149f

	;------------------------------------------------------------------
	; if L2 IRQ interrupted an L1 ISR,  we'd disabled preemption earlier
	; so that sched doesn't move to new task, causing L1 to be delayed
	; undeterministically. Now that we've achieved that, let's reset
	; things to what they were, before returning from L2 context
	;----------------------------------------------------------------

	ld r9, [sp, PT_status32]       ; get statu32_l2 (saved in pt_regs)
	bbit0 r9, STATUS_A1_BIT, 149f  ; L1 not active when L2 IRQ, so normal

	; decrement thread_info->preempt_count (re-enable preemption)
	GET_CURR_THR_INFO_FROM_SP   r10
	ld      r9, [r10, THREAD_INFO_PREEMPT_COUNT]

	; paranoid check, given A1 was active when A2 happened, preempt count
	; must not be 0 because we would have incremented it.
	; If this does happen we simply HALT as it means a BUG !!!
	cmp     r9, 0
	bnz     2f
	flag 1

2:
	sub     r9, r9, 1
	st      r9, [r10, THREAD_INFO_PREEMPT_COUNT]

149:
	INTERRUPT_EPILOGUE 2	; return from level 2 interrupt
debug_marker_l2:
	rtie

not_level2_interrupt:

#endif

	INTERRUPT_EPILOGUE 1	; return from level 1 interrupt
debug_marker_l1:
	rtie

.Lexcep_or_pure_K_ret:

	;this case is for syscalls or Exceptions or pure kernel mode

	EXCEPTION_EPILOGUE
debug_marker_syscall:
	rtie

END(ret_from_exception)
