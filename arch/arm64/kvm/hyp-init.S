/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <linux/linkage.h>

#include <asm/assembler.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_mmu.h>
#include <asm/pgtable-hwdef.h>
#include <asm/sysreg.h>
#include <asm/virt.h>

	.text
	.pushsection	.hyp.idmap.text, "ax"

	.align	11

ENTRY(__kvm_hyp_init)
	ventry	__invalid		// Synchronous EL2t
	ventry	__invalid		// IRQ EL2t
	ventry	__invalid		// FIQ EL2t
	ventry	__invalid		// Error EL2t

	ventry	__invalid		// Synchronous EL2h
	ventry	__invalid		// IRQ EL2h
	ventry	__invalid		// FIQ EL2h
	ventry	__invalid		// Error EL2h

	ventry	__do_hyp_init		// Synchronous 64-bit EL1
	ventry	__invalid		// IRQ 64-bit EL1
	ventry	__invalid		// FIQ 64-bit EL1
	ventry	__invalid		// Error 64-bit EL1

	ventry	__invalid		// Synchronous 32-bit EL1
	ventry	__invalid		// IRQ 32-bit EL1
	ventry	__invalid		// FIQ 32-bit EL1
	ventry	__invalid		// Error 32-bit EL1

__invalid:
	b	.

	/*
	 * x0: HYP pgd
	 * x1: HYP stack
	 * x2: HYP vectors
	 * x3: per-CPU offset
	 */
__do_hyp_init:
	/* Check for a stub HVC call */
	cmp	x0, #HVC_STUB_HCALL_NR
	b.lo	__kvm_handle_stub_hvc

	phys_to_ttbr x4, x0
alternative_if ARM64_HAS_CNP
	orr	x4, x4, #TTBR_CNP_BIT
alternative_else_nop_endif
	msr	ttbr0_el2, x4

	mrs	x4, tcr_el1
	ldr	x5, =TCR_EL2_MASK
	and	x4, x4, x5
	mov	x5, #TCR_EL2_RES1
	orr	x4, x4, x5

	/*
	 * The ID map may be configured to use an extended virtual address
	 * range. This is only the case if system RAM is out of range for the
	 * currently configured page size and VA_BITS, in which case we will
	 * also need the extended virtual range for the HYP ID map, or we won't
	 * be able to enable the EL2 MMU.
	 *
	 * However, at EL2, there is only one TTBR register, and we can't switch
	 * between translation tables *and* update TCR_EL2.T0SZ at the same
	 * time. Bottom line: we need to use the extended range with *both* our
	 * translation tables.
	 *
	 * So use the same T0SZ value we use for the ID map.
	 */
	ldr_l	x5, idmap_t0sz
	bfi	x4, x5, TCR_T0SZ_OFFSET, TCR_TxSZ_WIDTH

	/*
	 * Set the PS bits in TCR_EL2.
	 */
	tcr_compute_pa_size x4, #TCR_EL2_PS_SHIFT, x5, x6

	msr	tcr_el2, x4

	mrs	x4, mair_el1
	msr	mair_el2, x4
	isb

	/* Invalidate the stale TLBs from Bootloader */
	tlbi	alle2
	dsb	sy

	/*
	 * Preserve all the RES1 bits while setting the default flags,
	 * as well as the EE bit on BE. Drop the A flag since the compiler
	 * is allowed to generate unaligned accesses.
	 */
	ldr	x4, =(SCTLR_EL2_RES1 | (SCTLR_ELx_FLAGS & ~SCTLR_ELx_A))
CPU_BE(	orr	x4, x4, #SCTLR_ELx_EE)
	msr	sctlr_el2, x4
	isb

	/* Set the stack and new vectors */
	kern_hyp_va	x1
	mov	sp, x1
	msr	vbar_el2, x2

	/* Set tpidr_el2 for use by HYP */
	msr	tpidr_el2, x3

	/* Hello, World! */
	eret
ENDPROC(__kvm_hyp_init)

ENTRY(__kvm_handle_stub_hvc)
	cmp	x0, #HVC_SOFT_RESTART
	b.ne	1f

	/* This is where we're about to jump, staying at EL2 */
	msr	elr_el2, x1
	mov	x0, #(PSR_F_BIT | PSR_I_BIT | PSR_A_BIT | PSR_D_BIT | PSR_MODE_EL2h)
	msr	spsr_el2, x0

	/* Shuffle the arguments, and don't come back */
	mov	x0, x2
	mov	x1, x3
	mov	x2, x4
	b	reset

1:	cmp	x0, #HVC_RESET_VECTORS
	b.ne	1f
reset:
	/*
	 * Reset kvm back to the hyp stub. Do not clobber x0-x4 in
	 * case we coming via HVC_SOFT_RESTART.
	 */
	mrs	x5, sctlr_el2
	ldr	x6, =SCTLR_ELx_FLAGS
	bic	x5, x5, x6		// Clear SCTL_M and etc
	pre_disable_mmu_workaround
	msr	sctlr_el2, x5
	isb

	/* Install stub vectors */
	adr_l	x5, __hyp_stub_vectors
	msr	vbar_el2, x5
	mov	x0, xzr
	eret

1:	/* Bad stub call */
	ldr	x0, =HVC_STUB_ERR
	eret

ENDPROC(__kvm_handle_stub_hvc)

	.ltorg

	.popsection
