/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#ifndef __ARM_KVM_INIT_H__
#define __ARM_KVM_INIT_H__

#ifndef __ASSEMBLY__
#error Assembly-only header
#endif

#include <asm/kvm_arm.h>
#include <asm/ptrace.h>
#include <asm/sysreg.h>
#include <linux/irqchip/arm-gic-v3.h>

.macro __init_el2_sctlr
	mov_q	x0, INIT_SCTLR_EL2_MMU_OFF
	msr	sctlr_el2, x0
	isb
.endm

/*
 * Allow Non-secure EL1 and EL0 to access physical timer and counter.
 * This is not necessary for VHE, since the host kernel runs in EL2,
 * and EL0 accesses are configured in the later stage of boot process.
 * Note that when HCR_EL2.E2H == 1, CNTHCTL_EL2 has the same bit layout
 * as CNTKCTL_EL1, and CNTKCTL_EL1 accessing instructions are redefined
 * to access CNTHCTL_EL2. This allows the kernel designed to run at EL1
 * to transparently mess with the EL0 bits via CNTKCTL_EL1 access in
 * EL2.
 */
.macro __init_el2_timers
	mov	x0, #3				// Enable EL1 physical timers
	mrs	x1, hcr_el2
	and	x1, x1, #HCR_E2H
	cbz	x1, .LnVHE_\@
	lsl	x0, x0, #10
.LnVHE_\@:
	msr	cnthctl_el2, x0
	msr	cntvoff_el2, xzr		// Clear virtual offset
.endm

.macro __init_el2_debug
	mrs	x1, id_aa64dfr0_el1
	sbfx	x0, x1, #ID_AA64DFR0_EL1_PMUVer_SHIFT, #4
	cmp	x0, #1
	b.lt	.Lskip_pmu_\@			// Skip if no PMU present
	mrs	x0, pmcr_el0			// Disable debug access traps
	ubfx	x0, x0, #11, #5			// to EL2 and allow access to
.Lskip_pmu_\@:
	csel	x2, xzr, x0, lt			// all PMU counters from EL1

	/* Statistical profiling */
	ubfx	x0, x1, #ID_AA64DFR0_EL1_PMSVer_SHIFT, #4
	cbz	x0, .Lskip_spe_\@		// Skip if SPE not present

	mrs_s	x0, SYS_PMBIDR_EL1              // If SPE available at EL2,
	and	x0, x0, #(1 << PMBIDR_EL1_P_SHIFT)
	cbnz	x0, .Lskip_spe_el2_\@		// then permit sampling of physical
	mov	x0, #(1 << PMSCR_EL2_PCT_SHIFT | \
		      1 << PMSCR_EL2_PA_SHIFT)
	msr_s	SYS_PMSCR_EL2, x0		// addresses and physical counter
.Lskip_spe_el2_\@:
	mov	x0, #(MDCR_EL2_E2PB_MASK << MDCR_EL2_E2PB_SHIFT)
	orr	x2, x2, x0			// If we don't have VHE, then
						// use EL1&0 translation.

.Lskip_spe_\@:
	/* Trace buffer */
	ubfx	x0, x1, #ID_AA64DFR0_EL1_TraceBuffer_SHIFT, #4
	cbz	x0, .Lskip_trace_\@		// Skip if TraceBuffer is not present

	mrs_s	x0, SYS_TRBIDR_EL1
	and	x0, x0, TRBIDR_PROG
	cbnz	x0, .Lskip_trace_\@		// If TRBE is available at EL2

	mov	x0, #(MDCR_EL2_E2TB_MASK << MDCR_EL2_E2TB_SHIFT)
	orr	x2, x2, x0			// allow the EL1&0 translation
						// to own it.

.Lskip_trace_\@:
	msr	mdcr_el2, x2			// Configure debug traps
.endm

/* LORegions */
.macro __init_el2_lor
	mrs	x1, id_aa64mmfr1_el1
	ubfx	x0, x1, #ID_AA64MMFR1_EL1_LO_SHIFT, 4
	cbz	x0, .Lskip_lor_\@
	msr_s	SYS_LORC_EL1, xzr
.Lskip_lor_\@:
.endm

/* Stage-2 translation */
.macro __init_el2_stage2
	msr	vttbr_el2, xzr
.endm

/* GICv3 system register access */
.macro __init_el2_gicv3
	mrs	x0, id_aa64pfr0_el1
	ubfx	x0, x0, #ID_AA64PFR0_EL1_GIC_SHIFT, #4
	cbz	x0, .Lskip_gicv3_\@

	mrs_s	x0, SYS_ICC_SRE_EL2
	orr	x0, x0, #ICC_SRE_EL2_SRE	// Set ICC_SRE_EL2.SRE==1
	orr	x0, x0, #ICC_SRE_EL2_ENABLE	// Set ICC_SRE_EL2.Enable==1
	msr_s	SYS_ICC_SRE_EL2, x0
	isb					// Make sure SRE is now set
	mrs_s	x0, SYS_ICC_SRE_EL2		// Read SRE back,
	tbz	x0, #0, .Lskip_gicv3_\@		// and check that it sticks
	msr_s	SYS_ICH_HCR_EL2, xzr		// Reset ICH_HCR_EL2 to defaults
.Lskip_gicv3_\@:
.endm

.macro __init_el2_hstr
	msr	hstr_el2, xzr			// Disable CP15 traps to EL2
.endm

/* Virtual CPU ID registers */
.macro __init_el2_nvhe_idregs
	mrs	x0, midr_el1
	mrs	x1, mpidr_el1
	msr	vpidr_el2, x0
	msr	vmpidr_el2, x1
.endm

/* Coprocessor traps */
.macro __init_el2_cptr
	mrs	x1, hcr_el2
	and	x1, x1, #HCR_E2H
	cbz	x1, .LnVHE_\@
	mov	x0, #(CPACR_EL1_FPEN_EL1EN | CPACR_EL1_FPEN_EL0EN)
	b	.Lset_cptr_\@
.LnVHE_\@:
	mov	x0, #0x33ff
.Lset_cptr_\@:
	msr	cptr_el2, x0			// Disable copro. traps to EL2
.endm

/* Disable any fine grained traps */
.macro __init_el2_fgt
	mrs	x1, id_aa64mmfr0_el1
	ubfx	x1, x1, #ID_AA64MMFR0_EL1_FGT_SHIFT, #4
	cbz	x1, .Lskip_fgt_\@

	mov	x0, xzr
	mrs	x1, id_aa64dfr0_el1
	ubfx	x1, x1, #ID_AA64DFR0_EL1_PMSVer_SHIFT, #4
	cmp	x1, #3
	b.lt	.Lset_debug_fgt_\@
	/* Disable PMSNEVFR_EL1 read and write traps */
	orr	x0, x0, #(1 << 62)

.Lset_debug_fgt_\@:
	msr_s	SYS_HDFGRTR_EL2, x0
	msr_s	SYS_HDFGWTR_EL2, x0

	mov	x0, xzr
	mrs	x1, id_aa64pfr1_el1
	ubfx	x1, x1, #ID_AA64PFR1_EL1_SME_SHIFT, #4
	cbz	x1, .Lset_fgt_\@

	/* Disable nVHE traps of TPIDR2 and SMPRI */
	orr	x0, x0, #HFGxTR_EL2_nSMPRI_EL1_MASK
	orr	x0, x0, #HFGxTR_EL2_nTPIDR2_EL0_MASK

.Lset_fgt_\@:
	msr_s	SYS_HFGRTR_EL2, x0
	msr_s	SYS_HFGWTR_EL2, x0
	msr_s	SYS_HFGITR_EL2, xzr

	mrs	x1, id_aa64pfr0_el1		// AMU traps UNDEF without AMU
	ubfx	x1, x1, #ID_AA64PFR0_EL1_AMU_SHIFT, #4
	cbz	x1, .Lskip_fgt_\@

	msr_s	SYS_HAFGRTR_EL2, xzr
.Lskip_fgt_\@:
.endm

.macro __init_el2_nvhe_prepare_eret
	mov	x0, #INIT_PSTATE_EL1
	msr	spsr_el2, x0
.endm

/**
 * Initialize EL2 registers to sane values. This should be called early on all
 * cores that were booted in EL2. Note that everything gets initialised as
 * if VHE was not available. The kernel context will be upgraded to VHE
 * if possible later on in the boot process
 *
 * Regs: x0, x1 and x2 are clobbered.
 */
.macro init_el2_state
	__init_el2_sctlr
	__init_el2_timers
	__init_el2_debug
	__init_el2_lor
	__init_el2_stage2
	__init_el2_gicv3
	__init_el2_hstr
	__init_el2_nvhe_idregs
	__init_el2_cptr
	__init_el2_fgt
.endm

#ifndef __KVM_NVHE_HYPERVISOR__
// This will clobber tmp1 and tmp2, and expect tmp1 to contain
// the id register value as read from the HW
.macro __check_override idreg, fld, width, pass, fail, tmp1, tmp2
	ubfx	\tmp1, \tmp1, #\fld, #\width
	cbz	\tmp1, \fail

	adr_l	\tmp1, \idreg\()_override
	ldr	\tmp2, [\tmp1, FTR_OVR_VAL_OFFSET]
	ldr	\tmp1, [\tmp1, FTR_OVR_MASK_OFFSET]
	ubfx	\tmp2, \tmp2, #\fld, #\width
	ubfx	\tmp1, \tmp1, #\fld, #\width
	cmp	\tmp1, xzr
	and	\tmp2, \tmp2, \tmp1
	csinv	\tmp2, \tmp2, xzr, ne
	cbnz	\tmp2, \pass
	b	\fail
.endm

// This will clobber tmp1 and tmp2
.macro check_override idreg, fld, pass, fail, tmp1, tmp2
	mrs	\tmp1, \idreg\()_el1
	__check_override \idreg \fld 4 \pass \fail \tmp1 \tmp2
.endm
#else
// This will clobber tmp
.macro __check_override idreg, fld, width, pass, fail, tmp, ignore
	ldr_l	\tmp, \idreg\()_el1_sys_val
	ubfx	\tmp, \tmp, #\fld, #\width
	cbnz	\tmp, \pass
	b	\fail
.endm

.macro check_override idreg, fld, pass, fail, tmp, ignore
	__check_override \idreg \fld 4 \pass \fail \tmp \ignore
.endm
#endif

.macro finalise_el2_state
	check_override id_aa64pfr0, ID_AA64PFR0_EL1_SVE_SHIFT, .Linit_sve_\@, .Lskip_sve_\@, x1, x2

.Linit_sve_\@:	/* SVE register access */
	mrs	x0, cptr_el2			// Disable SVE traps
	mrs	x1, hcr_el2
	and	x1, x1, #HCR_E2H
	cbz	x1, .Lcptr_nvhe_\@

	// VHE case
	orr	x0, x0, #(CPACR_EL1_ZEN_EL1EN | CPACR_EL1_ZEN_EL0EN)
	b	.Lset_cptr_\@

.Lcptr_nvhe_\@: // nVHE case
	bic	x0, x0, #CPTR_EL2_TZ
.Lset_cptr_\@:
	msr	cptr_el2, x0
	isb
	mov	x1, #ZCR_ELx_LEN_MASK		// SVE: Enable full vector
	msr_s	SYS_ZCR_EL2, x1			// length for EL1.

.Lskip_sve_\@:
	check_override id_aa64pfr1, ID_AA64PFR1_EL1_SME_SHIFT, .Linit_sme_\@, .Lskip_sme_\@, x1, x2

.Linit_sme_\@:	/* SME register access and priority mapping */
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
	__check_override id_aa64smfr0, ID_AA64SMFR0_EL1_FA64_SHIFT, 1, .Linit_sme_fa64_\@, .Lskip_sme_fa64_\@, x1, x2

.Linit_sme_fa64_\@:
	orr	x0, x0, SMCR_ELx_FA64_MASK
.Lskip_sme_fa64_\@:

	// ZT0 available?
	mrs_s	x1, SYS_ID_AA64SMFR0_EL1
	__check_override id_aa64smfr0, ID_AA64SMFR0_EL1_SMEver_SHIFT, 4, .Linit_sme_zt0_\@, .Lskip_sme_zt0_\@, x1, x2
.Linit_sme_zt0_\@:
	orr	x0, x0, SMCR_ELx_EZT0_MASK
.Lskip_sme_zt0_\@:

	orr	x0, x0, #SMCR_ELx_LEN_MASK	// Enable full SME vector
	msr_s	SYS_SMCR_EL2, x0		// length for EL1.

	mrs_s	x1, SYS_SMIDR_EL1		// Priority mapping supported?
	ubfx    x1, x1, #SMIDR_EL1_SMPS_SHIFT, #1
	cbz     x1, .Lskip_sme_\@

	msr_s	SYS_SMPRIMAP_EL2, xzr		// Make all priorities equal

	mrs	x1, id_aa64mmfr1_el1		// HCRX_EL2 present?
	ubfx	x1, x1, #ID_AA64MMFR1_EL1_HCX_SHIFT, #4
	cbz	x1, .Lskip_sme_\@

	mrs_s	x1, SYS_HCRX_EL2
	orr	x1, x1, #HCRX_EL2_SMPME_MASK	// Enable priority mapping
	msr_s	SYS_HCRX_EL2, x1
.Lskip_sme_\@:
.endm

#endif /* __ARM_KVM_INIT_H__ */
