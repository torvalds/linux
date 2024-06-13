/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <linux/arm-smccc.h>
#include <linux/linkage.h>

#include <asm/alternative.h>
#include <asm/assembler.h>
#include <asm/el2_setup.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_mmu.h>
#include <asm/pgtable-hwdef.h>
#include <asm/sysreg.h>
#include <asm/virt.h>

	.text
	.pushsection	.idmap.text, "ax"

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
	 * Only uses x0..x3 so as to not clobber callee-saved SMCCC registers.
	 *
	 * x0: SMCCC function ID
	 * x1: struct kvm_nvhe_init_params PA
	 */
__do_hyp_init:
	/* Check for a stub HVC call */
	cmp	x0, #HVC_STUB_HCALL_NR
	b.lo	__kvm_handle_stub_hvc

	mov	x3, #KVM_HOST_SMCCC_FUNC(__kvm_hyp_init)
	cmp	x0, x3
	b.eq	1f

	mov	x0, #SMCCC_RET_NOT_SUPPORTED
	eret

1:	mov	x0, x1
	mov	x3, lr
	bl	___kvm_hyp_init			// Clobbers x0..x2
	mov	lr, x3

	/* Hello, World! */
	mov	x0, #SMCCC_RET_SUCCESS
	eret
SYM_CODE_END(__kvm_hyp_init)

/*
 * Initialize the hypervisor in EL2.
 *
 * Only uses x0..x2 so as to not clobber callee-saved SMCCC registers
 * and leave x3 for the caller.
 *
 * x0: struct kvm_nvhe_init_params PA
 */
SYM_CODE_START_LOCAL(___kvm_hyp_init)
	ldr	x1, [x0, #NVHE_INIT_TPIDR_EL2]
	msr	tpidr_el2, x1

	ldr	x1, [x0, #NVHE_INIT_STACK_HYP_VA]
	mov	sp, x1

	ldr	x1, [x0, #NVHE_INIT_MAIR_EL2]
	msr	mair_el2, x1

	ldr	x1, [x0, #NVHE_INIT_HCR_EL2]
	msr	hcr_el2, x1

	mrs	x1, ID_AA64MMFR0_EL1
	and	x1, x1, #(0xf << ID_AA64MMFR0_EL1_FGT_SHIFT)
	cbz	x1, 1f
	ldr	x1, [x0, #NVHE_INIT_HFGWTR_EL2]
	msr_s	SYS_HFGWTR_EL2, x1

1:
	ldr	x1, [x0, #NVHE_INIT_VTTBR]
	msr	vttbr_el2, x1

	ldr	x1, [x0, #NVHE_INIT_VTCR]
	msr	vtcr_el2, x1

	ldr	x1, [x0, #NVHE_INIT_PGD_PA]
	phys_to_ttbr x2, x1
alternative_if ARM64_HAS_CNP
	orr	x2, x2, #TTBR_CNP_BIT
alternative_else_nop_endif
	msr	ttbr0_el2, x2

	/*
	 * Set the PS bits in TCR_EL2.
	 */
	ldr	x0, [x0, #NVHE_INIT_TCR_EL2]
	tcr_compute_pa_size x0, #TCR_EL2_PS_SHIFT, x1, x2
	msr	tcr_el2, x0

	isb

	/* Invalidate the stale TLBs from Bootloader */
	tlbi	alle2
	tlbi	vmalls12e1
	dsb	sy

	mov_q	x0, INIT_SCTLR_EL2_MMU_ON
alternative_if ARM64_HAS_ADDRESS_AUTH
	mov_q	x1, (SCTLR_ELx_ENIA | SCTLR_ELx_ENIB | \
		     SCTLR_ELx_ENDA | SCTLR_ELx_ENDB)
	orr	x0, x0, x1
alternative_else_nop_endif
	msr	sctlr_el2, x0
	isb

	/* Set the host vector */
	ldr	x0, =__kvm_hyp_host_vector
	msr	vbar_el2, x0

	ret
SYM_CODE_END(___kvm_hyp_init)

/*
 * PSCI CPU_ON entry point
 *
 * x0: struct kvm_nvhe_init_params PA
 */
SYM_CODE_START(kvm_hyp_cpu_entry)
	mov	x1, #1				// is_cpu_on = true
	b	__kvm_hyp_init_cpu
SYM_CODE_END(kvm_hyp_cpu_entry)

/*
 * PSCI CPU_SUSPEND / SYSTEM_SUSPEND entry point
 *
 * x0: struct kvm_nvhe_init_params PA
 */
SYM_CODE_START(kvm_hyp_cpu_resume)
	mov	x1, #0				// is_cpu_on = false
	b	__kvm_hyp_init_cpu
SYM_CODE_END(kvm_hyp_cpu_resume)

/*
 * Common code for CPU entry points. Initializes EL2 state and
 * installs the hypervisor before handing over to a C handler.
 *
 * x0: struct kvm_nvhe_init_params PA
 * x1: bool is_cpu_on
 */
SYM_CODE_START_LOCAL(__kvm_hyp_init_cpu)
	mov	x28, x0				// Stash arguments
	mov	x29, x1

	/* Check that the core was booted in EL2. */
	mrs	x0, CurrentEL
	cmp	x0, #CurrentEL_EL2
	b.eq	2f

	/* The core booted in EL1. KVM cannot be initialized on it. */
1:	wfe
	wfi
	b	1b

2:	msr	SPsel, #1			// We want to use SP_EL{1,2}

	/* Initialize EL2 CPU state to sane values. */
	init_el2_state				// Clobbers x0..x2
	finalise_el2_state

	/* Enable MMU, set vectors and stack. */
	mov	x0, x28
	bl	___kvm_hyp_init			// Clobbers x0..x2

	/* Leave idmap. */
	mov	x0, x29
	ldr	x1, =kvm_host_psci_cpu_entry
	br	x1
SYM_CODE_END(__kvm_hyp_init_cpu)

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
	mov_q	x5, INIT_SCTLR_EL2_MMU_OFF
	pre_disable_mmu_workaround
	msr	sctlr_el2, x5
	isb

alternative_if ARM64_KVM_PROTECTED_MODE
	mov_q	x5, HCR_HOST_NVHE_FLAGS
	msr	hcr_el2, x5
alternative_else_nop_endif

	/* Install stub vectors */
	adr_l	x5, __hyp_stub_vectors
	msr	vbar_el2, x5
	eret

1:	/* Bad stub call */
	mov_q	x0, HVC_STUB_ERR
	eret

SYM_CODE_END(__kvm_handle_stub_hvc)

SYM_FUNC_START(__pkvm_init_switch_pgd)
	/* Turn the MMU off */
	pre_disable_mmu_workaround
	mrs	x2, sctlr_el2
	bic	x3, x2, #SCTLR_ELx_M
	msr	sctlr_el2, x3
	isb

	tlbi	alle2

	/* Install the new pgtables */
	ldr	x3, [x0, #NVHE_INIT_PGD_PA]
	phys_to_ttbr x4, x3
alternative_if ARM64_HAS_CNP
	orr	x4, x4, #TTBR_CNP_BIT
alternative_else_nop_endif
	msr	ttbr0_el2, x4

	/* Set the new stack pointer */
	ldr	x0, [x0, #NVHE_INIT_STACK_HYP_VA]
	mov	sp, x0

	/* And turn the MMU back on! */
	set_sctlr_el2	x2
	ret	x1
SYM_FUNC_END(__pkvm_init_switch_pgd)

	.popsection
