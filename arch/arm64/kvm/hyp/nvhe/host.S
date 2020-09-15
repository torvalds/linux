/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 - Google Inc
 * Author: Andrew Scull <ascull@google.com>
 */

#include <linux/linkage.h>

#include <asm/assembler.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_mmu.h>

	.text

SYM_FUNC_START(__hyp_do_panic)
	mov	lr, #(PSR_F_BIT | PSR_I_BIT | PSR_A_BIT | PSR_D_BIT |\
		      PSR_MODE_EL1h)
	msr	spsr_el2, lr
	ldr	lr, =panic
	msr	elr_el2, lr
	eret
	sb
SYM_FUNC_END(__hyp_do_panic)

.macro host_el1_sync_vect
	.align 7
.L__vect_start\@:
	stp	x0, x1, [sp, #-16]!
	mrs	x0, esr_el2
	lsr	x0, x0, #ESR_ELx_EC_SHIFT
	cmp	x0, #ESR_ELx_EC_HVC64
	ldp	x0, x1, [sp], #16
	b.ne	hyp_panic

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
	 * Shuffle the parameters before calling the function
	 * pointed to in x0. Assumes parameters in x[1,2,3].
	 */
	kern_hyp_va	x0
	str	lr, [sp, #-16]!
	mov	lr, x0
	mov	x0, x1
	mov	x1, x2
	mov	x2, x3
	blr	lr
	ldr	lr, [sp], #16

	eret
	sb
.L__vect_end\@:
.if ((.L__vect_end\@ - .L__vect_start\@) > 0x80)
	.error "host_el1_sync_vect larger than vector entry"
.endif
.endm

.macro invalid_host_vect
	.align 7
	b	hyp_panic
.endm

/*
 * The host vector does not use an ESB instruction in order to avoid consuming
 * SErrors that should only be consumed by the host. Guest entry is deferred by
 * __guest_enter if there are any pending asynchronous exceptions so hyp will
 * always return to the host without having consumerd host SErrors.
 *
 * CONFIG_KVM_INDIRECT_VECTORS is not applied to the host vectors because the
 * host knows about the EL2 vectors already, and there is no point in hiding
 * them.
 */
	.align 11
SYM_CODE_START(__kvm_hyp_host_vector)
	invalid_host_vect			// Synchronous EL2t
	invalid_host_vect			// IRQ EL2t
	invalid_host_vect			// FIQ EL2t
	invalid_host_vect			// Error EL2t

	invalid_host_vect			// Synchronous EL2h
	invalid_host_vect			// IRQ EL2h
	invalid_host_vect			// FIQ EL2h
	invalid_host_vect			// Error EL2h

	host_el1_sync_vect			// Synchronous 64-bit EL1
	invalid_host_vect			// IRQ 64-bit EL1
	invalid_host_vect			// FIQ 64-bit EL1
	invalid_host_vect			// Error 64-bit EL1

	invalid_host_vect			// Synchronous 32-bit EL1
	invalid_host_vect			// IRQ 32-bit EL1
	invalid_host_vect			// FIQ 32-bit EL1
	invalid_host_vect			// Error 32-bit EL1
SYM_CODE_END(__kvm_hyp_host_vector)
