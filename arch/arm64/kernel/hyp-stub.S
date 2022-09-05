/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Hypervisor stub
 *
 * Copyright (C) 2012 ARM Ltd.
 * Author:	Marc Zyngier <marc.zyngier@arm.com>
 */

#include <linux/init.h>
#include <linux/linkage.h>

#include <asm/assembler.h>
#include <asm/el2_setup.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_asm.h>
#include <asm/ptrace.h>
#include <asm/virt.h>

// Warning, hardcoded register allocation
// This will clobber x1 and x2, and expect x1 to contain
// the id register value as read from the HW
.macro __check_override idreg, fld, width, pass, fail
	ubfx	x1, x1, #\fld, #\width
	cbz	x1, \fail

	adr_l	x1, \idreg\()_override
	ldr	x2, [x1, FTR_OVR_VAL_OFFSET]
	ldr	x1, [x1, FTR_OVR_MASK_OFFSET]
	ubfx	x2, x2, #\fld, #\width
	ubfx	x1, x1, #\fld, #\width
	cmp	x1, xzr
	and	x2, x2, x1
	csinv	x2, x2, xzr, ne
	cbnz	x2, \pass
	b	\fail
.endm

.macro check_override idreg, fld, pass, fail
	mrs	x1, \idreg\()_el1
	__check_override \idreg \fld 4 \pass \fail
.endm

	.text
	.pushsection	.hyp.text, "ax"

	.align 11

SYM_CODE_START(__hyp_stub_vectors)
	ventry	el2_sync_invalid		// Synchronous EL2t
	ventry	el2_irq_invalid			// IRQ EL2t
	ventry	el2_fiq_invalid			// FIQ EL2t
	ventry	el2_error_invalid		// Error EL2t

	ventry	elx_sync			// Synchronous EL2h
	ventry	el2_irq_invalid			// IRQ EL2h
	ventry	el2_fiq_invalid			// FIQ EL2h
	ventry	el2_error_invalid		// Error EL2h

	ventry	elx_sync			// Synchronous 64-bit EL1
	ventry	el1_irq_invalid			// IRQ 64-bit EL1
	ventry	el1_fiq_invalid			// FIQ 64-bit EL1
	ventry	el1_error_invalid		// Error 64-bit EL1

	ventry	el1_sync_invalid		// Synchronous 32-bit EL1
	ventry	el1_irq_invalid			// IRQ 32-bit EL1
	ventry	el1_fiq_invalid			// FIQ 32-bit EL1
	ventry	el1_error_invalid		// Error 32-bit EL1
SYM_CODE_END(__hyp_stub_vectors)

	.align 11

SYM_CODE_START_LOCAL(elx_sync)
	cmp	x0, #HVC_SET_VECTORS
	b.ne	1f
	msr	vbar_el2, x1
	b	9f

1:	cmp	x0, #HVC_FINALISE_EL2
	b.eq	__finalise_el2

2:	cmp	x0, #HVC_SOFT_RESTART
	b.ne	3f
	mov	x0, x2
	mov	x2, x4
	mov	x4, x1
	mov	x1, x3
	br	x4				// no return

3:	cmp	x0, #HVC_RESET_VECTORS
	beq	9f				// Nothing to reset!

	/* Someone called kvm_call_hyp() against the hyp-stub... */
	mov_q	x0, HVC_STUB_ERR
	eret

9:	mov	x0, xzr
	eret
SYM_CODE_END(elx_sync)

SYM_CODE_START_LOCAL(__finalise_el2)
	check_override id_aa64pfr0 ID_AA64PFR0_EL1_SVE_SHIFT .Linit_sve .Lskip_sve

.Linit_sve:	/* SVE register access */
	mrs	x0, cptr_el2			// Disable SVE traps
	bic	x0, x0, #CPTR_EL2_TZ
	msr	cptr_el2, x0
	isb
	mov	x1, #ZCR_ELx_LEN_MASK		// SVE: Enable full vector
	msr_s	SYS_ZCR_EL2, x1			// length for EL1.

.Lskip_sve:
	check_override id_aa64pfr1 ID_AA64PFR1_EL1_SME_SHIFT .Linit_sme .Lskip_sme

.Linit_sme:	/* SME register access and priority mapping */
	mrs	x0, cptr_el2			// Disable SME traps
	bic	x0, x0, #CPTR_EL2_TSM
	msr	cptr_el2, x0
	isb

	mrs	x1, sctlr_el2
	orr	x1, x1, #SCTLR_ELx_ENTP2	// Disable TPIDR2 traps
	msr	sctlr_el2, x1
	isb

	mov	x0, #0				// SMCR controls

	// Full FP in SM?
	mrs_s	x1, SYS_ID_AA64SMFR0_EL1
	__check_override id_aa64smfr0 ID_AA64SMFR0_EL1_FA64_SHIFT 1 .Linit_sme_fa64 .Lskip_sme_fa64

.Linit_sme_fa64:
	orr	x0, x0, SMCR_ELx_FA64_MASK
.Lskip_sme_fa64:

	orr	x0, x0, #SMCR_ELx_LEN_MASK	// Enable full SME vector
	msr_s	SYS_SMCR_EL2, x0		// length for EL1.

	mrs_s	x1, SYS_SMIDR_EL1		// Priority mapping supported?
	ubfx    x1, x1, #SMIDR_EL1_SMPS_SHIFT, #1
	cbz     x1, .Lskip_sme

	msr_s	SYS_SMPRIMAP_EL2, xzr		// Make all priorities equal

	mrs	x1, id_aa64mmfr1_el1		// HCRX_EL2 present?
	ubfx	x1, x1, #ID_AA64MMFR1_EL1_HCX_SHIFT, #4
	cbz	x1, .Lskip_sme

	mrs_s	x1, SYS_HCRX_EL2
	orr	x1, x1, #HCRX_EL2_SMPME_MASK	// Enable priority mapping
	msr_s	SYS_HCRX_EL2, x1

.Lskip_sme:

	// nVHE? No way! Give me the real thing!
	// Sanity check: MMU *must* be off
	mrs	x1, sctlr_el2
	tbnz	x1, #0, 1f

	// Needs to be VHE capable, obviously
	check_override id_aa64mmfr1 ID_AA64MMFR1_EL1_VH_SHIFT 2f 1f

1:	mov_q	x0, HVC_STUB_ERR
	eret
2:
	// Engage the VHE magic!
	mov_q	x0, HCR_HOST_VHE_FLAGS
	msr	hcr_el2, x0
	isb

	// Use the EL1 allocated stack, per-cpu offset
	mrs	x0, sp_el1
	mov	sp, x0
	mrs	x0, tpidr_el1
	msr	tpidr_el2, x0

	// FP configuration, vectors
	mrs_s	x0, SYS_CPACR_EL12
	msr	cpacr_el1, x0
	mrs_s	x0, SYS_VBAR_EL12
	msr	vbar_el1, x0

	// Use EL2 translations for SPE & TRBE and disable access from EL1
	mrs	x0, mdcr_el2
	bic	x0, x0, #(MDCR_EL2_E2PB_MASK << MDCR_EL2_E2PB_SHIFT)
	bic	x0, x0, #(MDCR_EL2_E2TB_MASK << MDCR_EL2_E2TB_SHIFT)
	msr	mdcr_el2, x0

	// Transfer the MM state from EL1 to EL2
	mrs_s	x0, SYS_TCR_EL12
	msr	tcr_el1, x0
	mrs_s	x0, SYS_TTBR0_EL12
	msr	ttbr0_el1, x0
	mrs_s	x0, SYS_TTBR1_EL12
	msr	ttbr1_el1, x0
	mrs_s	x0, SYS_MAIR_EL12
	msr	mair_el1, x0
	isb

	// Hack the exception return to stay at EL2
	mrs	x0, spsr_el1
	and	x0, x0, #~PSR_MODE_MASK
	mov	x1, #PSR_MODE_EL2h
	orr	x0, x0, x1
	msr	spsr_el1, x0

	b	enter_vhe
SYM_CODE_END(__finalise_el2)

	// At the point where we reach enter_vhe(), we run with
	// the MMU off (which is enforced by __finalise_el2()).
	// We thus need to be in the idmap, or everything will
	// explode when enabling the MMU.

	.pushsection	.idmap.text, "ax"

SYM_CODE_START_LOCAL(enter_vhe)
	// Invalidate TLBs before enabling the MMU
	tlbi	vmalle1
	dsb	nsh
	isb

	// Enable the EL2 S1 MMU, as set up from EL1
	mrs_s	x0, SYS_SCTLR_EL12
	set_sctlr_el1	x0

	// Disable the EL1 S1 MMU for a good measure
	mov_q	x0, INIT_SCTLR_EL1_MMU_OFF
	msr_s	SYS_SCTLR_EL12, x0

	mov	x0, xzr

	eret
SYM_CODE_END(enter_vhe)

	.popsection

.macro invalid_vector	label
SYM_CODE_START_LOCAL(\label)
	b \label
SYM_CODE_END(\label)
.endm

	invalid_vector	el2_sync_invalid
	invalid_vector	el2_irq_invalid
	invalid_vector	el2_fiq_invalid
	invalid_vector	el2_error_invalid
	invalid_vector	el1_sync_invalid
	invalid_vector	el1_irq_invalid
	invalid_vector	el1_fiq_invalid
	invalid_vector	el1_error_invalid

	.popsection

/*
 * __hyp_set_vectors: Call this after boot to set the initial hypervisor
 * vectors as part of hypervisor installation.  On an SMP system, this should
 * be called on each CPU.
 *
 * x0 must be the physical address of the new vector table, and must be
 * 2KB aligned.
 *
 * Before calling this, you must check that the stub hypervisor is installed
 * everywhere, by waiting for any secondary CPUs to be brought up and then
 * checking that is_hyp_mode_available() is true.
 *
 * If not, there is a pre-existing hypervisor, some CPUs failed to boot, or
 * something else went wrong... in such cases, trying to install a new
 * hypervisor is unlikely to work as desired.
 *
 * When you call into your shiny new hypervisor, sp_el2 will contain junk,
 * so you will need to set that to something sensible at the new hypervisor's
 * initialisation entry point.
 */

SYM_FUNC_START(__hyp_set_vectors)
	mov	x1, x0
	mov	x0, #HVC_SET_VECTORS
	hvc	#0
	ret
SYM_FUNC_END(__hyp_set_vectors)

SYM_FUNC_START(__hyp_reset_vectors)
	mov	x0, #HVC_RESET_VECTORS
	hvc	#0
	ret
SYM_FUNC_END(__hyp_reset_vectors)

/*
 * Entry point to finalise EL2 and switch to VHE if deemed capable
 *
 * w0: boot mode, as returned by init_kernel_el()
 */
SYM_FUNC_START(finalise_el2)
	// Need to have booted at EL2
	cmp	w0, #BOOT_CPU_MODE_EL2
	b.ne	1f

	// and still be at EL1
	mrs	x0, CurrentEL
	cmp	x0, #CurrentEL_EL1
	b.ne	1f

	mov	x0, #HVC_FINALISE_EL2
	hvc	#0
1:
	ret
SYM_FUNC_END(finalise_el2)
