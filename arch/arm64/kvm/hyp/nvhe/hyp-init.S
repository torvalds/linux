/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <linux/arm-smccc.h>
#include <linux/linkage.h>

#include <asm/alternative.h>
#include <asm/assembler.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_mmu.h>
#include <asm/pgtable-hwdef.h>
#include <asm/sysreg.h>
#include <asm/virt.h>

	.text
	.pushsection	.hyp.idmap.text, "ax"

	.align	11

SYM_CODE_START(__kvm_hyp_init)
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
	 * x0: SMCCC function ID
	 * x1: HYP pgd
	 * x2: per-CPU offset
	 * x3: HYP stack
	 * x4: HYP vectors
	 */
__do_hyp_init:
	/* Check for a stub HVC call */
	cmp	x0, #HVC_STUB_HCALL_NR
	b.lo	__kvm_handle_stub_hvc

	/* Set tpidr_el2 for use by HYP to free a register */
	msr	tpidr_el2, x2

	mov	x2, #KVM_HOST_SMCCC_FUNC(__kvm_hyp_init)
	cmp	x0, x2
	b.eq	1f
	mov	x0, #SMCCC_RET_NOT_SUPPORTED
	eret

1:	phys_to_ttbr x0, x1
alternative_if ARM64_HAS_CNP
	orr	x0, x0, #TTBR_CNP_BIT
alternative_else_nop_endif
	msr	ttbr0_el2, x0

	mrs	x0, tcr_el1
	mov_q	x1, TCR_EL2_MASK
	and	x0, x0, x1
	mov	x1, #TCR_EL2_RES1
	orr	x0, x0, x1

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
	ldr_l	x1, idmap_t0sz
	bfi	x0, x1, TCR_T0SZ_OFFSET, TCR_TxSZ_WIDTH

	/*
	 * Set the PS bits in TCR_EL2.
	 */
	tcr_compute_pa_size x0, #TCR_EL2_PS_SHIFT, x1, x2

	msr	tcr_el2, x0

	mrs	x0, mair_el1
	msr	mair_el2, x0
	isb

	/* Invalidate the stale TLBs from Bootloader */
	tlbi	alle2
	dsb	sy

	/*
	 * Preserve all the RES1 bits while setting the default flags,
	 * as well as the EE bit on BE. Drop the A flag since the compiler
	 * is allowed to generate unaligned accesses.
	 */
	mov_q	x0, (SCTLR_EL2_RES1 | (SCTLR_ELx_FLAGS & ~SCTLR_ELx_A))
CPU_BE(	orr	x0, x0, #SCTLR_ELx_EE)
alternative_if ARM64_HAS_ADDRESS_AUTH
	mov_q	x1, (SCTLR_ELx_ENIA | SCTLR_ELx_ENIB | \
		     SCTLR_ELx_ENDA | SCTLR_ELx_ENDB)
	orr	x0, x0, x1
alternative_else_nop_endif
	msr	sctlr_el2, x0
	isb

	/* Set the stack and new vectors */
	mov	sp, x3
	msr	vbar_el2, x4

	/* Hello, World! */
	mov	x0, #SMCCC_RET_SUCCESS
	eret
SYM_CODE_END(__kvm_hyp_init)

SYM_CODE_START(__kvm_handle_stub_hvc)
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

	/*
	 * Set the HVC_RESET_VECTORS return code before entering the common
	 * path so that we do not clobber x0-x2 in case we are coming via
	 * HVC_SOFT_RESTART.
	 */
	mov	x0, xzr
reset:
	/* Reset kvm back to the hyp stub. */
	mrs	x5, sctlr_el2
	mov_q	x6, SCTLR_ELx_FLAGS
	bic	x5, x5, x6		// Clear SCTL_M and etc
	pre_disable_mmu_workaround
	msr	sctlr_el2, x5
	isb

	/* Install stub vectors */
	adr_l	x5, __hyp_stub_vectors
	msr	vbar_el2, x5
	eret

1:	/* Bad stub call */
	mov_q	x0, HVC_STUB_ERR
	eret

SYM_CODE_END(__kvm_handle_stub_hvc)

	.popsection
