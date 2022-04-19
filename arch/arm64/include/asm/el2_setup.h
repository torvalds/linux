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
	msr	cnthctl_el2, x0
	msr	cntvoff_el2, xzr		// Clear virtual offset
.endm

.macro __init_el2_debug
	mrs	x1, id_aa64dfr0_el1
	sbfx	x0, x1, #ID_AA64DFR0_PMUVER_SHIFT, #4
	cmp	x0, #1
	b.lt	.Lskip_pmu_\@			// Skip if no PMU present
	mrs	x0, pmcr_el0			// Disable debug access traps
	ubfx	x0, x0, #11, #5			// to EL2 and allow access to
.Lskip_pmu_\@:
	csel	x2, xzr, x0, lt			// all PMU counters from EL1

	/* Statistical profiling */
	ubfx	x0, x1, #ID_AA64DFR0_PMSVER_SHIFT, #4
	cbz	x0, .Lskip_spe_\@		// Skip if SPE not present

	mrs_s	x0, SYS_PMBIDR_EL1              // If SPE available at EL2,
	and	x0, x0, #(1 << SYS_PMBIDR_EL1_P_SHIFT)
	cbnz	x0, .Lskip_spe_el2_\@		// then permit sampling of physical
	mov	x0, #(1 << SYS_PMSCR_EL2_PCT_SHIFT | \
		      1 << SYS_PMSCR_EL2_PA_SHIFT)
	msr_s	SYS_PMSCR_EL2, x0		// addresses and physical counter
.Lskip_spe_el2_\@:
	mov	x0, #(MDCR_EL2_E2PB_MASK << MDCR_EL2_E2PB_SHIFT)
	orr	x2, x2, x0			// If we don't have VHE, then
						// use EL1&0 translation.

.Lskip_spe_\@:
	/* Trace buffer */
	ubfx	x0, x1, #ID_AA64DFR0_TRBE_SHIFT, #4
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
	ubfx	x0, x1, #ID_AA64MMFR1_LOR_SHIFT, 4
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
	ubfx	x0, x0, #ID_AA64PFR0_GIC_SHIFT, #4
	cbz	x0, .Lskip_gicv3_\@

	mrs_s	x0, SYS_ICC_SRE_EL2
	orr	x0, x0, #ICC_SRE_EL2_SRE	// Set ICC_SRE_EL2.SRE==1
	orr	x0, x0, #ICC_SRE_EL2_ENABLE	// Set ICC_SRE_EL2.Enable==1
	msr_s	SYS_ICC_SRE_EL2, x0
	isb					// Make sure SRE is now set
	mrs_s	x0, SYS_ICC_SRE_EL2		// Read SRE back,
	tbz	x0, #0, .Lskip_gicv3_\@		// and check that it sticks
	msr_s	SYS_ICH_HCR_EL2, xzr		// Reset ICC_HCR_EL2 to defaults
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
.macro __init_el2_nvhe_cptr
	mov	x0, #0x33ff
	msr	cptr_el2, x0			// Disable copro. traps to EL2
.endm

/* SVE register access */
.macro __init_el2_nvhe_sve
	mrs	x1, id_aa64pfr0_el1
	ubfx	x1, x1, #ID_AA64PFR0_SVE_SHIFT, #4
	cbz	x1, .Lskip_sve_\@

	bic	x0, x0, #CPTR_EL2_TZ		// Also disable SVE traps
	msr	cptr_el2, x0			// Disable copro. traps to EL2
	isb
	mov	x1, #ZCR_ELx_LEN_MASK		// SVE: Enable full vector
	msr_s	SYS_ZCR_EL2, x1			// length for EL1.
.Lskip_sve_\@:
.endm

/* Disable any fine grained traps */
.macro __init_el2_fgt
	mrs	x1, id_aa64mmfr0_el1
	ubfx	x1, x1, #ID_AA64MMFR0_FGT_SHIFT, #4
	cbz	x1, .Lskip_fgt_\@

	mov	x0, xzr
	mrs	x1, id_aa64dfr0_el1
	ubfx	x1, x1, #ID_AA64DFR0_PMSVER_SHIFT, #4
	cmp	x1, #3
	b.lt	.Lset_fgt_\@
	/* Disable PMSNEVFR_EL1 read and write traps */
	orr	x0, x0, #(1 << 62)

.Lset_fgt_\@:
	msr_s	SYS_HDFGRTR_EL2, x0
	msr_s	SYS_HDFGWTR_EL2, x0
	msr_s	SYS_HFGRTR_EL2, xzr
	msr_s	SYS_HFGWTR_EL2, xzr
	msr_s	SYS_HFGITR_EL2, xzr

	mrs	x1, id_aa64pfr0_el1		// AMU traps UNDEF without AMU
	ubfx	x1, x1, #ID_AA64PFR0_AMU_SHIFT, #4
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
 * if VHE was not evailable. The kernel context will be upgraded to VHE
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
	__init_el2_nvhe_cptr
	__init_el2_nvhe_sve
	__init_el2_fgt
	__init_el2_nvhe_prepare_eret
.endm

#endif /* __ARM_KVM_INIT_H__ */
