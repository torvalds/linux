// SPDX-License-Identifier: GPL-2.0-only
/*
 * Debug and Guest Debug support
 *
 * Copyright (C) 2015 - Linaro Ltd
 * Authors: Alex Bennée <alex.bennee@linaro.org>
 * 	    Oliver Upton <oliver.upton@linux.dev>
 */

#include <linux/kvm_host.h>
#include <linux/hw_breakpoint.h>

#include <asm/debug-monitors.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_emulate.h>

/**
 * kvm_arm_setup_mdcr_el2 - configure vcpu mdcr_el2 value
 *
 * @vcpu:	the vcpu pointer
 *
 * This ensures we will trap access to:
 *  - Performance monitors (MDCR_EL2_TPM/MDCR_EL2_TPMCR)
 *  - Debug ROM Address (MDCR_EL2_TDRA)
 *  - OS related registers (MDCR_EL2_TDOSA)
 *  - Statistical profiler (MDCR_EL2_TPMS/MDCR_EL2_E2PB)
 *  - Self-hosted Trace Filter controls (MDCR_EL2_TTRF)
 *  - Self-hosted Trace (MDCR_EL2_TTRF/MDCR_EL2_E2TB)
 */
static void kvm_arm_setup_mdcr_el2(struct kvm_vcpu *vcpu)
{
	preempt_disable();

	/*
	 * This also clears MDCR_EL2_E2PB_MASK and MDCR_EL2_E2TB_MASK
	 * to disable guest access to the profiling and trace buffers
	 */
	vcpu->arch.mdcr_el2 = FIELD_PREP(MDCR_EL2_HPMN,
					 *host_data_ptr(nr_event_counters));
	vcpu->arch.mdcr_el2 |= (MDCR_EL2_TPM |
				MDCR_EL2_TPMS |
				MDCR_EL2_TTRF |
				MDCR_EL2_TPMCR |
				MDCR_EL2_TDRA |
				MDCR_EL2_TDOSA);

	/* Is the VM being debugged by userspace? */
	if (vcpu->guest_debug)
		/* Route all software debug exceptions to EL2 */
		vcpu->arch.mdcr_el2 |= MDCR_EL2_TDE;

	/*
	 * Trap debug registers if the guest doesn't have ownership of them.
	 */
	if (!kvm_guest_owns_debug_regs(vcpu))
		vcpu->arch.mdcr_el2 |= MDCR_EL2_TDA;

	/* Write MDCR_EL2 directly if we're already at EL2 */
	if (has_vhe())
		write_sysreg(vcpu->arch.mdcr_el2, mdcr_el2);

	preempt_enable();
}

void kvm_init_host_debug_data(void)
{
	u64 dfr0 = read_sysreg(id_aa64dfr0_el1);

	if (cpuid_feature_extract_signed_field(dfr0, ID_AA64DFR0_EL1_PMUVer_SHIFT) > 0)
		*host_data_ptr(nr_event_counters) = FIELD_GET(ARMV8_PMU_PMCR_N,
							      read_sysreg(pmcr_el0));

	*host_data_ptr(debug_brps) = SYS_FIELD_GET(ID_AA64DFR0_EL1, BRPs, dfr0);
	*host_data_ptr(debug_wrps) = SYS_FIELD_GET(ID_AA64DFR0_EL1, WRPs, dfr0);

	if (has_vhe())
		return;

	if (cpuid_feature_extract_unsigned_field(dfr0, ID_AA64DFR0_EL1_PMSVer_SHIFT) &&
	    !(read_sysreg_s(SYS_PMBIDR_EL1) & PMBIDR_EL1_P))
		host_data_set_flag(HAS_SPE);

	if (cpuid_feature_extract_unsigned_field(dfr0, ID_AA64DFR0_EL1_TraceFilt_SHIFT)) {
		/* Force disable trace in protected mode in case of no TRBE */
		if (is_protected_kvm_enabled())
			host_data_set_flag(EL1_TRACING_CONFIGURED);

		if (cpuid_feature_extract_unsigned_field(dfr0, ID_AA64DFR0_EL1_TraceBuffer_SHIFT) &&
		    !(read_sysreg_s(SYS_TRBIDR_EL1) & TRBIDR_EL1_P))
			host_data_set_flag(HAS_TRBE);
	}
}

/*
 * Configures the 'external' MDSCR_EL1 value for the guest, i.e. when the host
 * has taken over MDSCR_EL1.
 *
 *  - Userspace is single-stepping the guest, and MDSCR_EL1.SS is forced to 1.
 *
 *  - Userspace is using the breakpoint/watchpoint registers to debug the
 *    guest, and MDSCR_EL1.MDE is forced to 1.
 *
 *  - The guest has enabled the OS Lock, and KVM is forcing MDSCR_EL1.MDE to 0,
 *    masking all debug exceptions affected by the OS Lock.
 */
static void setup_external_mdscr(struct kvm_vcpu *vcpu)
{
	/*
	 * Use the guest's MDSCR_EL1 as a starting point, since there are
	 * several other features controlled by MDSCR_EL1 that are not relevant
	 * to the host.
	 *
	 * Clear the bits that KVM may use which also satisfies emulation of
	 * the OS Lock as MDSCR_EL1.MDE is cleared.
	 */
	u64 mdscr = vcpu_read_sys_reg(vcpu, MDSCR_EL1) & ~(MDSCR_EL1_SS |
							   MDSCR_EL1_MDE |
							   MDSCR_EL1_KDE);

	if (vcpu->guest_debug & KVM_GUESTDBG_SINGLESTEP)
		mdscr |= MDSCR_EL1_SS;

	if (vcpu->guest_debug & KVM_GUESTDBG_USE_HW)
		mdscr |= MDSCR_EL1_MDE | MDSCR_EL1_KDE;

	vcpu->arch.external_mdscr_el1 = mdscr;
}

void kvm_vcpu_load_debug(struct kvm_vcpu *vcpu)
{
	u64 mdscr;

	/* Must be called before kvm_vcpu_load_vhe() */
	KVM_BUG_ON(vcpu_get_flag(vcpu, SYSREGS_ON_CPU), vcpu->kvm);

	/*
	 * Determine which of the possible debug states we're in:
	 *
	 *  - VCPU_DEBUG_HOST_OWNED: KVM has taken ownership of the guest's
	 *    breakpoint/watchpoint registers, or needs to use MDSCR_EL1 to do
	 *    software step or emulate the effects of the OS Lock being enabled.
	 *
	 *  - VCPU_DEBUG_GUEST_OWNED: The guest has debug exceptions enabled, and
	 *    the breakpoint/watchpoint registers need to be loaded eagerly.
	 *
	 *  - VCPU_DEBUG_FREE: Neither of the above apply, no breakpoint/watchpoint
	 *    context needs to be loaded on the CPU.
	 */
	if (vcpu->guest_debug || kvm_vcpu_os_lock_enabled(vcpu)) {
		vcpu->arch.debug_owner = VCPU_DEBUG_HOST_OWNED;
		setup_external_mdscr(vcpu);

		/*
		 * Steal the guest's single-step state machine if userspace wants
		 * single-step the guest.
		 */
		if (vcpu->guest_debug & KVM_GUESTDBG_SINGLESTEP) {
			if (*vcpu_cpsr(vcpu) & DBG_SPSR_SS)
				vcpu_clear_flag(vcpu, GUEST_SS_ACTIVE_PENDING);
			else
				vcpu_set_flag(vcpu, GUEST_SS_ACTIVE_PENDING);

			if (!vcpu_get_flag(vcpu, HOST_SS_ACTIVE_PENDING))
				*vcpu_cpsr(vcpu) |= DBG_SPSR_SS;
			else
				*vcpu_cpsr(vcpu) &= ~DBG_SPSR_SS;
		}
	} else {
		mdscr = vcpu_read_sys_reg(vcpu, MDSCR_EL1);

		if (mdscr & (MDSCR_EL1_KDE | MDSCR_EL1_MDE))
			vcpu->arch.debug_owner = VCPU_DEBUG_GUEST_OWNED;
		else
			vcpu->arch.debug_owner = VCPU_DEBUG_FREE;
	}

	kvm_arm_setup_mdcr_el2(vcpu);
}

void kvm_vcpu_put_debug(struct kvm_vcpu *vcpu)
{
	if (likely(!(vcpu->guest_debug & KVM_GUESTDBG_SINGLESTEP)))
		return;

	/*
	 * Save the host's software step state and restore the guest's before
	 * potentially returning to userspace.
	 */
	if (!(*vcpu_cpsr(vcpu) & DBG_SPSR_SS))
		vcpu_set_flag(vcpu, HOST_SS_ACTIVE_PENDING);
	else
		vcpu_clear_flag(vcpu, HOST_SS_ACTIVE_PENDING);

	if (vcpu_get_flag(vcpu, GUEST_SS_ACTIVE_PENDING))
		*vcpu_cpsr(vcpu) &= ~DBG_SPSR_SS;
	else
		*vcpu_cpsr(vcpu) |= DBG_SPSR_SS;
}

/*
 * Updates ownership of the debug registers after a trapped guest access to a
 * breakpoint/watchpoint register. Host ownership of the debug registers is of
 * strictly higher priority, and it is the responsibility of the VMM to emulate
 * guest debug exceptions in this configuration.
 */
void kvm_debug_set_guest_ownership(struct kvm_vcpu *vcpu)
{
	if (kvm_host_owns_debug_regs(vcpu))
		return;

	vcpu->arch.debug_owner = VCPU_DEBUG_GUEST_OWNED;
	kvm_arm_setup_mdcr_el2(vcpu);
}

void kvm_debug_handle_oslar(struct kvm_vcpu *vcpu, u64 val)
{
	if (val & OSLAR_EL1_OSLK)
		__vcpu_sys_reg(vcpu, OSLSR_EL1) |= OSLSR_EL1_OSLK;
	else
		__vcpu_sys_reg(vcpu, OSLSR_EL1) &= ~OSLSR_EL1_OSLK;

	preempt_disable();
	kvm_arch_vcpu_put(vcpu);
	kvm_arch_vcpu_load(vcpu, smp_processor_id());
	preempt_enable();
}

void kvm_enable_trbe(void)
{
	if (has_vhe() || is_protected_kvm_enabled() ||
	    WARN_ON_ONCE(preemptible()))
		return;

	host_data_set_flag(TRBE_ENABLED);
}
EXPORT_SYMBOL_GPL(kvm_enable_trbe);

void kvm_disable_trbe(void)
{
	if (has_vhe() || is_protected_kvm_enabled() ||
	    WARN_ON_ONCE(preemptible()))
		return;

	host_data_clear_flag(TRBE_ENABLED);
}
EXPORT_SYMBOL_GPL(kvm_disable_trbe);

void kvm_tracing_set_el1_configuration(u64 trfcr_while_in_guest)
{
	if (is_protected_kvm_enabled() || WARN_ON_ONCE(preemptible()))
		return;

	if (has_vhe()) {
		write_sysreg_s(trfcr_while_in_guest, SYS_TRFCR_EL12);
		return;
	}

	*host_data_ptr(trfcr_while_in_guest) = trfcr_while_in_guest;
	if (read_sysreg_s(SYS_TRFCR_EL1) != trfcr_while_in_guest)
		host_data_set_flag(EL1_TRACING_CONFIGURED);
	else
		host_data_clear_flag(EL1_TRACING_CONFIGURED);
}
EXPORT_SYMBOL_GPL(kvm_tracing_set_el1_configuration);
