/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ARCv2 ISA based core Low Level Intr/Traps/Exceptions(non-TLB) Handling
 *
 * Copyright (C) 2013 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/linkage.h>   /* ARC_{EXTRY,EXIT} */
#include <asm/entry.h>       /* SAVE_ALL_{INT1,INT2,TRAP...} */
#include <asm/errno.h>
#include <asm/arcregs.h>
#include <asm/irqflags.h>
#include <asm/mmu.h>

; A maximum number of supported interrupts in the core interrupt controller.
; This number is not equal to the maximum interrupt number (256) because
; first 16 lines are reserved for exceptions and are not configurable.
#define NR_CPU_IRQS	240

	.cpu HS

#define VECTOR	.word

;############################ Vector Table #################################

	.section .vector,"a",@progbits
	.align 4

# Initial 16 slots are Exception Vectors
VECTOR	res_service		; Reset Vector
VECTOR	mem_service		; Mem exception
VECTOR	instr_service		; Instrn Error
VECTOR	EV_MachineCheck		; Fatal Machine check
VECTOR	EV_TLBMissI		; Intruction TLB miss
VECTOR	EV_TLBMissD		; Data TLB miss
VECTOR	EV_TLBProtV		; Protection Violation
VECTOR	EV_PrivilegeV		; Privilege Violation
VECTOR	EV_SWI			; Software Breakpoint
VECTOR	EV_Trap			; Trap exception
VECTOR	EV_Extension		; Extn Instruction Exception
VECTOR	EV_DivZero		; Divide by Zero
VECTOR	EV_DCError		; Data Cache Error
VECTOR	EV_Misaligned		; Misaligned Data Access
VECTOR	reserved		; Reserved slots
VECTOR	reserved		; Reserved slots

# Begin Interrupt Vectors
VECTOR	handle_interrupt	; (16) Timer0
VECTOR	handle_interrupt	; unused (Timer1)
VECTOR	handle_interrupt	; unused (WDT)
VECTOR	handle_interrupt	; (19) Inter core Interrupt (IPI)
VECTOR	handle_interrupt	; (20) perf Interrupt
VECTOR	handle_interrupt	; (21) Software Triggered Intr (Self IPI)
VECTOR	handle_interrupt	; unused
VECTOR	handle_interrupt	; (23) unused
# End of fixed IRQs

.rept NR_CPU_IRQS - 8
	VECTOR	handle_interrupt
.endr

	.section .text, "ax",@progbits

reserved:
	flag 1		; Unexpected event, halt

;##################### Interrupt Handling ##############################

ENTRY(handle_interrupt)

	INTERRUPT_PROLOGUE

	# irq control APIs local_irq_save/restore/disable/enable fiddle with
	# global interrupt enable bits in STATUS32 (.IE for 1 prio, .E[] for 2 prio)
	# However a taken interrupt doesn't clear these bits. Thus irqs_disabled()
	# query in hard ISR path would return false (since .IE is set) which would
	# trips genirq interrupt handling asserts.
	#
	# So do a "soft" disable of interrutps here.
	#
	# Note this disable is only for consistent book-keeping as further interrupts
	# will be disabled anyways even w/o this. Hardware tracks active interrupts
	# seperately in AUX_IRQ_ACT.active and will not take new interrupts
	# unless this one returns (or higher prio becomes pending in 2-prio scheme)

	IRQ_DISABLE

	; icause is banked: one per priority level
	; so a higher prio interrupt taken here won't clobber prev prio icause
	lr  r0, [ICAUSE]
	mov   blink, ret_from_exception

	b.d  arch_do_IRQ
	mov r1, sp

END(handle_interrupt)

;################### Non TLB Exception Handling #############################

ENTRY(EV_SWI)
	; TODO: implement this
	EXCEPTION_PROLOGUE
	b   ret_from_exception
END(EV_SWI)

ENTRY(EV_DivZero)
	; TODO: implement this
	EXCEPTION_PROLOGUE
	b   ret_from_exception
END(EV_DivZero)

ENTRY(EV_DCError)
	; TODO: implement this
	EXCEPTION_PROLOGUE
	b   ret_from_exception
END(EV_DCError)

; ---------------------------------------------
; Memory Error Exception Handler
;   - Unlike ARCompact, handles Bus errors for both User/Kernel mode,
;     Instruction fetch or Data access, under a single Exception Vector
; ---------------------------------------------

ENTRY(mem_service)

	EXCEPTION_PROLOGUE

	lr  r0, [efa]
	mov r1, sp

	FAKE_RET_FROM_EXCPN

	bl  do_memory_error
	b   ret_from_exception
END(mem_service)

ENTRY(EV_Misaligned)

	EXCEPTION_PROLOGUE

	lr  r0, [efa]	; Faulting Data address
	mov r1, sp

	FAKE_RET_FROM_EXCPN

	SAVE_CALLEE_SAVED_USER
	mov r2, sp              ; callee_regs

	bl  do_misaligned_access

	; TBD: optimize - do this only if a callee reg was involved
	; either a dst of emulated LD/ST or src with address-writeback
	RESTORE_CALLEE_SAVED_USER

	b   ret_from_exception
END(EV_Misaligned)

; ---------------------------------------------
; Protection Violation Exception Handler
; ---------------------------------------------

ENTRY(EV_TLBProtV)

	EXCEPTION_PROLOGUE

	lr  r0, [efa]	; Faulting Data address
	mov r1, sp	; pt_regs

	FAKE_RET_FROM_EXCPN

	mov blink, ret_from_exception
	b   do_page_fault

END(EV_TLBProtV)

; From Linux standpoint Slow Path I/D TLB Miss is same a ProtV as they
; need to call do_page_fault().
; ECR in pt_regs provides whether access was R/W/X

.global        call_do_page_fault
.set call_do_page_fault, EV_TLBProtV

;############# Common Handlers for ARCompact and ARCv2 ##############

#include "entry.S"

;############# Return from Intr/Excp/Trap (ARCv2 ISA Specifics) ##############
;
; Restore the saved sys context (common exit-path for EXCPN/IRQ/Trap)
; IRQ shd definitely not happen between now and rtie
; All 2 entry points to here already disable interrupts

.Lrestore_regs:
restore_regs:

	# Interrpts are actually disabled from this point on, but will get
	# reenabled after we return from interrupt/exception.
	# But irq tracer needs to be told now...
	TRACE_ASM_IRQ_ENABLE

	ld	r0, [sp, PT_status32]	; U/K mode at time of entry
	lr	r10, [AUX_IRQ_ACT]

	bmsk	r11, r10, 15		; extract AUX_IRQ_ACT.active
	breq	r11, 0, .Lexcept_ret	; No intr active, ret from Exception

;####### Return from Intr #######

.Lisr_ret:

debug_marker_l1:
	; bbit1.nt r0, STATUS_DE_BIT, .Lintr_ret_to_delay_slot
	btst	r0, STATUS_DE_BIT		; Z flag set if bit clear
	bnz	.Lintr_ret_to_delay_slot	; branch if STATUS_DE_BIT set

	; Handle special case #1: (Entry via Exception, Return via IRQ)
	;
	; Exception in U mode, preempted in kernel, Intr taken (K mode), orig
	; task now returning to U mode (riding the Intr)
	; AUX_IRQ_ACTIVE won't have U bit set (since intr in K mode), hence SP
	; won't be switched to correct U mode value (from AUX_SP)
	; So force AUX_IRQ_ACT.U for such a case

	btst	r0, STATUS_U_BIT		; Z flag set if K (Z clear for U)
	bset.nz	r11, r11, AUX_IRQ_ACT_BIT_U	; NZ means U
	sr	r11, [AUX_IRQ_ACT]

	INTERRUPT_EPILOGUE
	rtie

;####### Return from Exception / pure kernel mode #######

.Lexcept_ret:	; Expects r0 has PT_status32

debug_marker_syscall:
	EXCEPTION_EPILOGUE
	rtie

;####### Return from Intr to insn in delay slot #######

; Handle special case #2: (Entry via Exception in Delay Slot, Return via IRQ)
;
; Intr returning to a Delay Slot (DS) insn
; (since IRQ NOT allowed in DS in ARCv2, this can only happen if orig
; entry was via Exception in DS which got preempted in kernel).
;
; IRQ RTIE won't reliably restore DE bit and/or BTA, needs workaround
;
; Solution is to drop out of interrupt context into pure kernel mode
; and return from pure kernel mode which does right things for delay slot

.Lintr_ret_to_delay_slot:
debug_marker_ds:

	ld	r2, [@intr_to_DE_cnt]
	add	r2, r2, 1
	st	r2, [@intr_to_DE_cnt]

	; drop out of interrupt context (clear AUX_IRQ_ACT.active)
	bmskn	r11, r10, 15
	sr	r11, [AUX_IRQ_ACT]
	b	.Lexcept_ret

END(ret_from_exception)
