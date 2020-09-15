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
#include <asm/kvm_mmu.h>
#include <asm/mmu.h>

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

.macro do_el2_call
	/*
	 * Shuffle the parameters before calling the function
	 * pointed to in x0. Assumes parameters in x[1,2,3].
	 */
	str	lr, [sp, #-16]!
	mov	lr, x0
	mov	x0, x1
	mov	x1, x2
	mov	x2, x3
	blr	lr
	ldr	lr, [sp], #16
.endm

el1_sync:				// Guest trapped into EL2

	mrs	x0, esr_el2
	lsr	x0, x0, #ESR_ELx_EC_SHIFT
	cmp	x0, #ESR_ELx_EC_HVC64
	ccmp	x0, #ESR_ELx_EC_HVC32, #4, ne
	b.ne	el1_trap

#ifdef __KVM_NVHE_HYPERVISOR__
	mrs	x1, vttbr_el2		// If vttbr is valid, the guest
	cbnz	x1, el1_hvc_guest	// called HVC

	/* Here, we're pretty sure the host called HVC. */
	ldp	x0, x1, [sp], #16

	/* Check for a stub HVC call */
	cmp	x0, #HVC_STUB_HCALL_NR
	b.hs	1f

	/*
	 * Compute the idmap address of __kvm_handle_stub_hvc and
	 * jump there. Since we use kimage_voffset, do not use the
	 * HYP VA for __kvm_handle_stub_hvc, but the kernel VA instead
	 * (by loading it from the constant pool).
	 *
	 * Preserve x0-x4, which may contain stub parameters.
	 */
	ldr	x5, =__kvm_handle_stub_hvc
	ldr_l	x6, kimage_voffset

	/* x5 = __pa(x5) */
	sub	x5, x5, x6
	br	x5

1:
	/*
	 * Perform the EL2 call
	 */
	kern_hyp_va	x0
	do_el2_call

	eret
	sb
#endif /* __KVM_NVHE_HYPERVISOR__ */

el1_hvc_guest:
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
	cbnz	w1, el1_trap

alternative_cb	arm64_enable_wa2_handling
	b	wa2_end
alternative_cb_end
	get_vcpu_ptr	x2, x0
	ldr	x0, [x2, #VCPU_WORKAROUND_FLAGS]

	// Sanitize the argument and update the guest flags
	ldr	x1, [sp, #8]			// Guest's x1
	clz	w1, w1				// Murphy's device:
	lsr	w1, w1, #5			// w1 = !!w1 without using
	eor	w1, w1, #1			// the flags...
	bfi	x0, x1, #VCPU_WORKAROUND_2_FLAG_SHIFT, #1
	str	x0, [x2, #VCPU_WORKAROUND_FLAGS]

	/* Check that we actually need to perform the call */
	hyp_ldr_this_cpu x0, arm64_ssbd_callback_required, x2
	cbz	x0, wa2_end

	mov	w0, #ARM_SMCCC_ARCH_WORKAROUND_2
	smc	#0

	/* Don't leak data from the SMC call */
	mov	x3, xzr
wa2_end:
	mov	x2, xzr
	mov	x1, xzr

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

#ifdef __KVM_NVHE_HYPERVISOR__
SYM_FUNC_START(__hyp_do_panic)
	mov	lr, #(PSR_F_BIT | PSR_I_BIT | PSR_A_BIT | PSR_D_BIT |\
		      PSR_MODE_EL1h)
	msr	spsr_el2, lr
	ldr	lr, =panic
	msr	elr_el2, lr
	eret
	sb
SYM_FUNC_END(__hyp_do_panic)
#endif

SYM_CODE_START(__hyp_panic)
	get_host_ctxt x0, x1
	b	hyp_panic
SYM_CODE_END(__hyp_panic)

.macro invalid_vector	label, target = __hyp_panic
	.align	2
SYM_CODE_START(\label)
	b \target
SYM_CODE_END(\label)
.endm

	/* None of these should ever happen */
	invalid_vector	el2t_sync_invalid
	invalid_vector	el2t_irq_invalid
	invalid_vector	el2t_fiq_invalid
	invalid_vector	el2t_error_invalid
	invalid_vector	el2h_sync_invalid
	invalid_vector	el2h_irq_invalid
	invalid_vector	el2h_fiq_invalid
	invalid_vector	el1_fiq_invalid

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
	b	\target
	nop
662:
	ldp	x0, x1, [sp], #16
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
	invalid_vect	el1_fiq_invalid		// FIQ 64-bit EL1
	valid_vect	el1_error		// Error 64-bit EL1

	valid_vect	el1_sync		// Synchronous 32-bit EL1
	valid_vect	el1_irq			// IRQ 32-bit EL1
	invalid_vect	el1_fiq_invalid		// FIQ 32-bit EL1
	valid_vect	el1_error		// Error 32-bit EL1
SYM_CODE_END(__kvm_hyp_vector)

#ifdef CONFIG_RANDOMIZE_BASE
.macro hyp_ventry
	.align 7
1:	esb
	.rept 26
	nop
	.endr
/*
 * The default sequence is to directly branch to the KVM vectors,
 * using the computed offset. This applies for VHE as well as
 * !ARM64_HARDEN_EL2_VECTORS. The first vector must always run the preamble.
 *
 * For ARM64_HARDEN_EL2_VECTORS configurations, this gets replaced
 * with:
 *
 * stp	x0, x1, [sp, #-16]!
 * movz	x0, #(addr & 0xffff)
 * movk	x0, #((addr >> 16) & 0xffff), lsl #16
 * movk	x0, #((addr >> 32) & 0xffff), lsl #32
 * br	x0
 *
 * Where:
 * addr = kern_hyp_va(__kvm_hyp_vector) + vector-offset + KVM_VECTOR_PREAMBLE.
 * See kvm_patch_vector_branch for details.
 */
alternative_cb	kvm_patch_vector_branch
	stp	x0, x1, [sp, #-16]!
	b	__kvm_hyp_vector + (1b - 0b + KVM_VECTOR_PREAMBLE)
	nop
	nop
	nop
alternative_cb_end
.endm

.macro generate_vectors
0:
	.rept 16
	hyp_ventry
	.endr
	.org 0b + SZ_2K		// Safety measure
.endm

	.align	11
SYM_CODE_START(__bp_harden_hyp_vecs)
	.rept BP_HARDEN_EL2_SLOTS
	generate_vectors
	.endr
1:	.org __bp_harden_hyp_vecs + __BP_HARDEN_HYP_VECS_SZ
	.org 1b
SYM_CODE_END(__bp_harden_hyp_vecs)
#endif
