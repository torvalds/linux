/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Copyright (c) 2021, Microsoft Corporation.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/kvm_asm.h>

.macro invalid_vector	label
SYM_CODE_START_LOCAL(\label)
	.align 7
	b	\label
SYM_CODE_END(\label)
.endm

.macro el1_sync_vector
SYM_CODE_START_LOCAL(el1_sync)
	.align 7
	cmp	x0, #HVC_SET_VECTORS	/* Called from hibernate */
	b.ne	1f
	msr	vbar_el2, x1
	mov	x0, xzr
	eret
1:	cmp	x0, #HVC_SOFT_RESTART	/* Called from kexec */
	b.ne	2f
	mov	x0, x2
	mov	x2, x4
	mov	x4, x1
	mov	x1, x3
	br	x4
2:	/* Unexpected argument, set an error */
	mov_q	x0, HVC_STUB_ERR
	eret
SYM_CODE_END(el1_sync)
.endm

SYM_CODE_START(trans_pgd_stub_vectors)
	invalid_vector	hyp_stub_el2t_sync_invalid	// Synchronous EL2t
	invalid_vector	hyp_stub_el2t_irq_invalid	// IRQ EL2t
	invalid_vector	hyp_stub_el2t_fiq_invalid	// FIQ EL2t
	invalid_vector	hyp_stub_el2t_error_invalid	// Error EL2t

	invalid_vector	hyp_stub_el2h_sync_invalid	// Synchronous EL2h
	invalid_vector	hyp_stub_el2h_irq_invalid	// IRQ EL2h
	invalid_vector	hyp_stub_el2h_fiq_invalid	// FIQ EL2h
	invalid_vector	hyp_stub_el2h_error_invalid	// Error EL2h

	el1_sync_vector					// Synchronous 64-bit EL1
	invalid_vector	hyp_stub_el1_irq_invalid	// IRQ 64-bit EL1
	invalid_vector	hyp_stub_el1_fiq_invalid	// FIQ 64-bit EL1
	invalid_vector	hyp_stub_el1_error_invalid	// Error 64-bit EL1

	invalid_vector	hyp_stub_32b_el1_sync_invalid	// Synchronous 32-bit EL1
	invalid_vector	hyp_stub_32b_el1_irq_invalid	// IRQ 32-bit EL1
	invalid_vector	hyp_stub_32b_el1_fiq_invalid	// FIQ 32-bit EL1
	invalid_vector	hyp_stub_32b_el1_error_invalid	// Error 32-bit EL1
	.align 11
SYM_INNER_LABEL(__trans_pgd_stub_vectors_end, SYM_L_LOCAL)
SYM_CODE_END(trans_pgd_stub_vectors)

# Check the trans_pgd_stub_vectors didn't overflow
.org . - (__trans_pgd_stub_vectors_end - trans_pgd_stub_vectors) + SZ_2K
