/* entry-table.S: main trap vector tables and exception jump table
 *
 * Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <linux/sys.h>
#include <linux/linkage.h>
#include <asm/spr-regs.h>

###############################################################################
#
# Declare the main trap and vector tables
#
# There are six tables:
#
# (1) The trap table for debug mode
# (2) The trap table for kernel mode
# (3) The trap table for user mode
#
#     The CPU jumps to an appropriate slot in the appropriate table to perform
#     exception processing. We have three different tables for the three
#     different CPU modes because there is no hardware differentiation between
#     stack pointers for these three modes, and so we have to invent one when
#     crossing mode boundaries.
#
# (4) The exception handler vector table
#
#     The user and kernel trap tables use the same prologue for normal
#     exception processing. The prologue then jumps to the handler in this
#     table, as indexed by the exception ID from the TBR.
#
# (5) The fixup table for kernel-trap single-step
# (6) The fixup table for user-trap single-step
#
#     Due to the way single-stepping works on this CPU (single-step is not
#     disabled when crossing exception boundaries, only when in debug mode),
#     we have to catch the single-step event in break.S and jump to the fixup
#     routine pointed to by this table.
#
# The linker script places the user mode and kernel mode trap tables on to
# the same 8Kb page, so that break.S can be more efficient when performing
# single-step bypass management
#
###############################################################################

	# trap table for entry from debug mode
	.section	.trap.break,"ax"
	.balign		256*16
	.globl		__entry_breaktrap_table
__entry_breaktrap_table:

	# trap table for entry from user mode
	.section	.trap.user,"ax"
	.balign		256*16
	.globl		__entry_usertrap_table
__entry_usertrap_table:

	# trap table for entry from kernel mode
	.section	.trap.kernel,"ax"
	.balign		256*16
	.globl		__entry_kerneltrap_table
__entry_kerneltrap_table:

	# exception handler jump table
	.section	.trap.vector,"ax"
	.balign		256*4
	.globl		__entry_vector_table
__entry_vector_table:

	# trap fixup table for single-stepping in user mode
	.section	.trap.fixup.user,"a"
	.balign		256*4
	.globl		__break_usertrap_fixup_table
__break_usertrap_fixup_table:

	# trap fixup table for single-stepping in user mode
	.section	.trap.fixup.kernel,"a"
	.balign		256*4
	.globl		__break_kerneltrap_fixup_table
__break_kerneltrap_fixup_table:

	# handler declaration for a software or program interrupt
.macro VECTOR_SOFTPROG tbr_tt, vec
	.section .trap.user
	.org		\tbr_tt
	bra		__entry_uspace_softprog_interrupt
	.section .trap.fixup.user
	.org		\tbr_tt >> 2
	.long		__break_step_uspace_softprog_interrupt
	.section .trap.kernel
	.org		\tbr_tt
	bra		__entry_kernel_softprog_interrupt
	.section .trap.fixup.kernel
	.org		\tbr_tt >> 2
	.long		__break_step_kernel_softprog_interrupt
	.section .trap.vector
	.org		\tbr_tt >> 2
	.long		\vec
.endm

	# handler declaration for a maskable external interrupt
.macro VECTOR_IRQ tbr_tt, vec
	.section .trap.user
	.org		\tbr_tt
	bra		__entry_uspace_external_interrupt
	.section .trap.fixup.user
	.org		\tbr_tt >> 2
	.long		__break_step_uspace_external_interrupt
	.section .trap.kernel
	.org		\tbr_tt
	# deal with virtual interrupt disablement
	beq		icc2,#0,__entry_kernel_external_interrupt_virtually_disabled
	bra		__entry_kernel_external_interrupt
	.section .trap.fixup.kernel
	.org		\tbr_tt >> 2
	.long		__break_step_kernel_external_interrupt
	.section .trap.vector
	.org		\tbr_tt >> 2
	.long		\vec
.endm

	# handler declaration for an NMI external interrupt
.macro VECTOR_NMI tbr_tt, vec
	.section .trap.user
	.org		\tbr_tt
	break
	break
	break
	break
	.section .trap.kernel
	.org		\tbr_tt
	break
	break
	break
	break
	.section .trap.vector
	.org		\tbr_tt >> 2
	.long		\vec
.endm

	# handler declaration for an MMU only software or program interrupt
.macro VECTOR_SP_MMU tbr_tt, vec
#ifdef CONFIG_MMU
 	VECTOR_SOFTPROG	\tbr_tt, \vec
#else
	VECTOR_NMI	\tbr_tt, 0
#endif
.endm


###############################################################################
#
# specification of the vectors
# - note: each macro inserts code into multiple sections
#
###############################################################################
	VECTOR_SP_MMU	TBR_TT_INSTR_MMU_MISS,	__entry_insn_mmu_miss
	VECTOR_SOFTPROG	TBR_TT_INSTR_ACC_ERROR,	__entry_insn_access_error
	VECTOR_SOFTPROG	TBR_TT_INSTR_ACC_EXCEP,	__entry_insn_access_exception
	VECTOR_SOFTPROG	TBR_TT_PRIV_INSTR,	__entry_privileged_instruction
	VECTOR_SOFTPROG	TBR_TT_ILLEGAL_INSTR,	__entry_illegal_instruction
	VECTOR_SOFTPROG	TBR_TT_FP_EXCEPTION,	__entry_media_exception
	VECTOR_SOFTPROG	TBR_TT_MP_EXCEPTION,	__entry_media_exception
	VECTOR_SOFTPROG	TBR_TT_DATA_ACC_ERROR,	__entry_data_access_error
	VECTOR_SP_MMU	TBR_TT_DATA_MMU_MISS,	__entry_data_mmu_miss
	VECTOR_SOFTPROG	TBR_TT_DATA_ACC_EXCEP,	__entry_data_access_exception
	VECTOR_SOFTPROG	TBR_TT_DATA_STR_ERROR,	__entry_data_store_error
	VECTOR_SOFTPROG	TBR_TT_DIVISION_EXCEP,	__entry_division_exception

#ifdef CONFIG_MMU
	.section .trap.user
	.org		TBR_TT_INSTR_TLB_MISS
	.globl		__trap_user_insn_tlb_miss
__trap_user_insn_tlb_miss:
	movsg		ear0,gr28			/* faulting address */
	movsg		scr0,gr31			/* get mapped PTD coverage start address */
	xor.p		gr28,gr31,gr31			/* compare addresses */
	bra		__entry_user_insn_tlb_miss

	.org		TBR_TT_DATA_TLB_MISS
	.globl		__trap_user_data_tlb_miss
__trap_user_data_tlb_miss:
	movsg		ear0,gr28			/* faulting address */
	movsg		scr1,gr31			/* get mapped PTD coverage start address */
	xor.p		gr28,gr31,gr31			/* compare addresses */
	bra		__entry_user_data_tlb_miss

	.section .trap.kernel
	.org		TBR_TT_INSTR_TLB_MISS
	.globl		__trap_kernel_insn_tlb_miss
__trap_kernel_insn_tlb_miss:
	movsg		ear0,gr29			/* faulting address */
	movsg		scr0,gr31			/* get mapped PTD coverage start address */
	xor.p		gr29,gr31,gr31			/* compare addresses */
	bra		__entry_kernel_insn_tlb_miss

	.org		TBR_TT_DATA_TLB_MISS
	.globl		__trap_kernel_data_tlb_miss
__trap_kernel_data_tlb_miss:
	movsg		ear0,gr29			/* faulting address */
	movsg		scr1,gr31			/* get mapped PTD coverage start address */
	xor.p		gr29,gr31,gr31			/* compare addresses */
	bra		__entry_kernel_data_tlb_miss

	.section .trap.fixup.user
	.org		TBR_TT_INSTR_TLB_MISS >> 2
	.globl		__trap_fixup_user_insn_tlb_miss
__trap_fixup_user_insn_tlb_miss:
	.long		__break_user_insn_tlb_miss
	.org		TBR_TT_DATA_TLB_MISS >> 2
	.globl		__trap_fixup_user_data_tlb_miss
__trap_fixup_user_data_tlb_miss:
	.long		__break_user_data_tlb_miss

	.section .trap.fixup.kernel
	.org		TBR_TT_INSTR_TLB_MISS >> 2
	.globl		__trap_fixup_kernel_insn_tlb_miss
__trap_fixup_kernel_insn_tlb_miss:
	.long		__break_kernel_insn_tlb_miss
	.org		TBR_TT_DATA_TLB_MISS >> 2
	.globl		__trap_fixup_kernel_data_tlb_miss
__trap_fixup_kernel_data_tlb_miss:
	.long		__break_kernel_data_tlb_miss

	.section .trap.vector
	.org		TBR_TT_INSTR_TLB_MISS >> 2
	.long		__entry_insn_mmu_fault
	.org		TBR_TT_DATA_TLB_MISS >> 2
	.long		__entry_data_mmu_fault
#endif

	VECTOR_SP_MMU	TBR_TT_DATA_DAT_EXCEP,	__entry_data_dat_fault
	VECTOR_NMI	TBR_TT_DECREMENT_TIMER,	__entry_do_NMI
	VECTOR_SOFTPROG	TBR_TT_COMPOUND_EXCEP,	__entry_compound_exception
	VECTOR_IRQ	TBR_TT_INTERRUPT_1,	__entry_do_IRQ
	VECTOR_IRQ	TBR_TT_INTERRUPT_2,	__entry_do_IRQ
	VECTOR_IRQ	TBR_TT_INTERRUPT_3,	__entry_do_IRQ
	VECTOR_IRQ	TBR_TT_INTERRUPT_4,	__entry_do_IRQ
	VECTOR_IRQ	TBR_TT_INTERRUPT_5,	__entry_do_IRQ
	VECTOR_IRQ	TBR_TT_INTERRUPT_6,	__entry_do_IRQ
	VECTOR_IRQ	TBR_TT_INTERRUPT_7,	__entry_do_IRQ
	VECTOR_IRQ	TBR_TT_INTERRUPT_8,	__entry_do_IRQ
	VECTOR_IRQ	TBR_TT_INTERRUPT_9,	__entry_do_IRQ
	VECTOR_IRQ	TBR_TT_INTERRUPT_10,	__entry_do_IRQ
	VECTOR_IRQ	TBR_TT_INTERRUPT_11,	__entry_do_IRQ
	VECTOR_IRQ	TBR_TT_INTERRUPT_12,	__entry_do_IRQ
	VECTOR_IRQ	TBR_TT_INTERRUPT_13,	__entry_do_IRQ
	VECTOR_IRQ	TBR_TT_INTERRUPT_14,	__entry_do_IRQ
	VECTOR_NMI	TBR_TT_INTERRUPT_15,	__entry_do_NMI

	# miscellaneous user mode entry points
	.section	.trap.user
	.org		TBR_TT_TRAP0
	.rept		127
	bra		__entry_uspace_softprog_interrupt
	.long		0,0,0
	.endr
	.org		TBR_TT_BREAK
	bra		__entry_break
	.long		0,0,0

	.section	.trap.fixup.user
	.org		TBR_TT_TRAP0 >> 2
	.rept		127
	.long		__break_step_uspace_softprog_interrupt
	.endr
	.org		TBR_TT_BREAK >> 2
	.long		0

	# miscellaneous kernel mode entry points
	.section	.trap.kernel
	.org		TBR_TT_TRAP0
	bra		__entry_kernel_softprog_interrupt
	.org		TBR_TT_TRAP1
	bra		__entry_kernel_softprog_interrupt

	# trap #2 in kernel - reenable interrupts
	.org		TBR_TT_TRAP2
	bra		__entry_kernel_external_interrupt_virtual_reenable

	# miscellaneous kernel traps
	.org		TBR_TT_TRAP3
	.rept		124
	bra		__entry_kernel_softprog_interrupt
	.long		0,0,0
	.endr
	.org		TBR_TT_BREAK
	bra		__entry_break
	.long		0,0,0

	.section	.trap.fixup.kernel
	.org		TBR_TT_TRAP0 >> 2
	.long		__break_step_kernel_softprog_interrupt
	.long		__break_step_kernel_softprog_interrupt
	.long		__break_step_kernel_external_interrupt_virtual_reenable
	.rept		124
	.long		__break_step_kernel_softprog_interrupt
	.endr
	.org		TBR_TT_BREAK >> 2
	.long		0

	# miscellaneous debug mode entry points
	.section	.trap.break
	.org		TBR_TT_BREAK
	movsg		bpcsr,gr30
	jmpl		@(gr30,gr0)

	# miscellaneous vectors
	.section	.trap.vector
	.org		TBR_TT_TRAP0 >> 2
	.long		system_call
	.rept		119
	.long		__entry_unsupported_trap
	.endr

	# userspace atomic op emulation, traps 120-126
	.rept		7
	.long		__entry_atomic_op
	.endr
	
	.org		TBR_TT_BREAK >> 2
	.long		__entry_debug_exception
