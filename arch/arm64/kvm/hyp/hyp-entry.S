/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015-2018 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <linux/arm-smccc.h>
#include <linux/linkage.h>

#include <asm/alternative.h>
#include <asm/assembler.h>
#include <asm/cpufeature.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_asm.h>
#include <asm/mmu.h>
#include <asm/spectre.h>

.macro save_caller_saved_regs_vect
	/* x0 and x1 were saved in the vector entry */
	stp	x2, x3,   [sp, #-16]!
	stp	x4, x5,   [sp, #-16]!
	stp	x6, x7,   [sp, #-16]!
	stp	x8, x9,   [sp, #-16]!
	stp	x10, x11, [sp, #-16]!
	stp	x12, x13, [sp, #-16]!
	stp	x14, x15, [sp, #-16]!
	stp	x16, x17, [sp, #-16]!
.endm

.macro restore_caller_saved_regs_vect
	ldp	x16, x17, [sp], #16
	ldp	x14, x15, [sp], #16
	ldp	x12, x13, [sp], #16
	ldp	x10, x11, [sp], #16
	ldp	x8, x9,   [sp], #16
	ldp	x6, x7,   [sp], #16
	ldp	x4, x5,   [sp], #16
	ldp	x2, x3,   [sp], #16
	ldp	x0, x1,   [sp], #16
.endm

	.text

el1_sync:				// Guest trapped into EL2

	mrs	x0, esr_el2
	ubfx	x0, x0, #ESR_ELx_EC_SHIFT, #ESR_ELx_EC_WIDTH
	cmp	x0, #ESR_ELx_EC_HVC64
	ccmp	x0, #ESR_ELx_EC_HVC32, #4, ne
	b.ne	el1_trap

	/*
	 * Fastest possible path for ARM_SMCCC_ARCH_WORKAROUND_1.
	 * The workaround has already been applied on the host,
	 * so let's quickly get back to the guest. We don't bother
	 * restoring x1, as it can be clobbered anyway.
	 */
	ldr	x1, [sp]				// Guest's x0
	eor	w1, w1, #ARM_SMCCC_ARCH_WORKAROUND_1
	cbz	w1, wa_epilogue

	/* ARM_SMCCC_ARCH_WORKAROUND_2 handling */
	eor	w1, w1, #(ARM_SMCCC_ARCH_WORKAROUND_1 ^ \
			  ARM_SMCCC_ARCH_WORKAROUND_2)
	cbz	w1, wa_epilogue

	eor	w1, w1, #(ARM_SMCCC_ARCH_WORKAROUND_2 ^ \
			  ARM_SMCCC_ARCH_WORKAROUND_3)
	cbnz	w1, el1_trap

wa_epilogue:
	mov	x0, xzr
	add	sp, sp, #16
	eret
	sb

el1_trap:
	get_vcpu_ptr	x1, x0
	mov	x0, #ARM_EXCEPTION_TRAP
	b	__guest_exit

el1_irq:
el1_fiq:
	get_vcpu_ptr	x1, x0
	mov	x0, #ARM_EXCEPTION_IRQ
	b	__guest_exit

el1_error:
	get_vcpu_ptr	x1, x0
	mov	x0, #ARM_EXCEPTION_EL1_SERROR
	b	__guest_exit

el2_sync:
	/* Check for illegal exception return */
	mrs	x0, spsr_el2
	tbnz	x0, #20, 1f

	save_caller_saved_regs_vect
	stp     x29, x30, [sp, #-16]!
	bl	kvm_unexpected_el2_exception
	ldp     x29, x30, [sp], #16
	restore_caller_saved_regs_vect

	eret

1:
	/* Let's attempt a recovery from the illegal exception return */
	get_vcpu_ptr	x1, x0
	mov	x0, #ARM_EXCEPTION_IL
	b	__guest_exit


el2_error:
	save_caller_saved_regs_vect
	stp     x29, x30, [sp, #-16]!

	bl	kvm_unexpected_el2_exception

	ldp     x29, x30, [sp], #16
	restore_caller_saved_regs_vect

	eret
	sb

.macro invalid_vector	label, target = __guest_exit_panic
	.align	2
SYM_CODE_START_LOCAL(\label)
	b \target
SYM_CODE_END(\label)
.endm

	/* None of these should ever happen */
	invalid_vector	el2t_sync_invalid
	invalid_vector	el2t_irq_invalid
	invalid_vector	el2t_fiq_invalid
	invalid_vector	el2t_error_invalid
	invalid_vector	el2h_irq_invalid
	invalid_vector	el2h_fiq_invalid

	.ltorg

	.align 11

.macro check_preamble_length start, end
/* kvm_patch_vector_branch() generates code that jumps over the preamble. */
.if ((\end-\start) != KVM_VECTOR_PREAMBLE)
	.error "KVM vector preamble length mismatch"
.endif
.endm

.macro valid_vect target
	.align 7
661:
	esb
	stp	x0, x1, [sp, #-16]!
662:
	b	\target

check_preamble_length 661b, 662b
.endm

.macro invalid_vect target
	.align 7
661:
	nop
	stp	x0, x1, [sp, #-16]!
662:
	b	\target

check_preamble_length 661b, 662b
.endm

SYM_CODE_START(__kvm_hyp_vector)
	invalid_vect	el2t_sync_invalid	// Synchronous EL2t
	invalid_vect	el2t_irq_invalid	// IRQ EL2t
	invalid_vect	el2t_fiq_invalid	// FIQ EL2t
	invalid_vect	el2t_error_invalid	// Error EL2t

	valid_vect	el2_sync		// Synchronous EL2h
	invalid_vect	el2h_irq_invalid	// IRQ EL2h
	invalid_vect	el2h_fiq_invalid	// FIQ EL2h
	valid_vect	el2_error		// Error EL2h

	valid_vect	el1_sync		// Synchronous 64-bit EL1
	valid_vect	el1_irq			// IRQ 64-bit EL1
	valid_vect	el1_fiq			// FIQ 64-bit EL1
	valid_vect	el1_error		// Error 64-bit EL1

	valid_vect	el1_sync		// Synchronous 32-bit EL1
	valid_vect	el1_irq			// IRQ 32-bit EL1
	valid_vect	el1_fiq			// FIQ 32-bit EL1
	valid_vect	el1_error		// Error 32-bit EL1
SYM_CODE_END(__kvm_hyp_vector)

.macro spectrev2_smccc_wa1_smc
	sub	sp, sp, #(8 * 4)
	stp	x2, x3, [sp, #(8 * 0)]
	stp	x0, x1, [sp, #(8 * 2)]
	alternative_cb spectre_bhb_patch_wa3
	/* Patched to mov WA3 when supported */
	mov	w0, #ARM_SMCCC_ARCH_WORKAROUND_1
	alternative_cb_end
	smc	#0
	ldp	x2, x3, [sp, #(8 * 0)]
	add	sp, sp, #(8 * 2)
.endm

.macro hyp_ventry	indirect, spectrev2
	.align	7
1:	esb
	.if \spectrev2 != 0
	spectrev2_smccc_wa1_smc
	.else
	stp	x0, x1, [sp, #-16]!
	mitigate_spectre_bhb_loop	x0
	.endif
	.if \indirect != 0
	alternative_cb  kvm_patch_vector_branch
	/*
	 * For ARM64_SPECTRE_V3A configurations, these NOPs get replaced with:
	 *
	 * movz	x0, #(addr & 0xffff)
	 * movk	x0, #((addr >> 16) & 0xffff), lsl #16
	 * movk	x0, #((addr >> 32) & 0xffff), lsl #32
	 * br	x0
	 *
	 * Where:
	 * addr = kern_hyp_va(__kvm_hyp_vector) + vector-offset + KVM_VECTOR_PREAMBLE.
	 * See kvm_patch_vector_branch for details.
	 */
	nop
	nop
	nop
	nop
	alternative_cb_end
	.endif
	b	__kvm_hyp_vector + (1b - 0b + KVM_VECTOR_PREAMBLE)
.endm

.macro generate_vectors	indirect, spectrev2
0:
	.rept 16
	hyp_ventry	\indirect, \spectrev2
	.endr
	.org 0b + SZ_2K		// Safety measure
.endm

	.align	11
SYM_CODE_START(__bp_harden_hyp_vecs)
	generate_vectors indirect = 0, spectrev2 = 1 // HYP_VECTOR_SPECTRE_DIRECT
	generate_vectors indirect = 1, spectrev2 = 0 // HYP_VECTOR_INDIRECT
	generate_vectors indirect = 1, spectrev2 = 1 // HYP_VECTOR_SPECTRE_INDIRECT
1:	.org __bp_harden_hyp_vecs + __BP_HARDEN_HYP_VECS_SZ
	.org 1b
SYM_CODE_END(__bp_harden_hyp_vecs)
