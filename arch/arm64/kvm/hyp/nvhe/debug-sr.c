// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <hyp/debug-sr.h>

#include <linux/compiler.h>
#include <linux/kvm_host.h>

#include <asm/debug-monitors.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>

static void __debug_save_spe(void)
{
	u64 *pmscr_el1, *pmblimitr_el1;

	pmscr_el1 = host_data_ptr(host_debug_state.pmscr_el1);
	pmblimitr_el1 = host_data_ptr(host_debug_state.pmblimitr_el1);

	/*
	 * At this point, we know that this CPU implements
	 * SPE and is available to the host.
	 * Check if the host is actually using it ?
	 */
	*pmblimitr_el1 = read_sysreg_s(SYS_PMBLIMITR_EL1);
	if (!(*pmblimitr_el1 & BIT(PMBLIMITR_EL1_E_SHIFT)))
		return;

	/* Yes; save the control register and disable data generation */
	*pmscr_el1 = read_sysreg_el1(SYS_PMSCR);
	write_sysreg_el1(0, SYS_PMSCR);
	isb();

	/* Now drain all buffered data to memory */
	psb_csync();
	dsb(nsh);

	/* And disable the profiling buffer */
	write_sysreg_s(0, SYS_PMBLIMITR_EL1);
	isb();
}

static void __debug_restore_spe(void)
{
	u64 pmblimitr_el1 = *host_data_ptr(host_debug_state.pmblimitr_el1);

	if (!(pmblimitr_el1 & BIT(PMBLIMITR_EL1_E_SHIFT)))
		return;

	/* The host page table is installed, but not yet synchronised */
	isb();

	/* Re-enable the profiling buffer. */
	write_sysreg_s(pmblimitr_el1, SYS_PMBLIMITR_EL1);
	isb();

	/* Re-enable data generation */
	write_sysreg_el1(*host_data_ptr(host_debug_state.pmscr_el1), SYS_PMSCR);
}

static void __trace_do_switch(u64 *saved_trfcr, u64 new_trfcr)
{
	*saved_trfcr = read_sysreg_el1(SYS_TRFCR);
	write_sysreg_el1(new_trfcr, SYS_TRFCR);
}

static void __trace_drain_and_disable(void)
{
	u64 *trblimitr_el1 = host_data_ptr(host_debug_state.trblimitr_el1);
	bool needs_drain = is_protected_kvm_enabled() ?
			   host_data_test_flag(HAS_TRBE) :
			   host_data_test_flag(TRBE_ENABLED);

	if (!needs_drain) {
		*trblimitr_el1 = 0;
		return;
	}

	*trblimitr_el1 = read_sysreg_s(SYS_TRBLIMITR_EL1);
	if (*trblimitr_el1 & TRBLIMITR_EL1_E) {
		/*
		 * The host has enabled the Trace Buffer Unit so we have
		 * to beat the CPU with a stick until it stops accessing
		 * memory.
		 */

		/* First, ensure that our prior write to TRFCR has stuck. */
		isb();

		/* Now synchronise with the trace and drain the buffer. */
		tsb_csync();
		dsb(nsh);

		/*
		 * With no more trace being generated, we can disable the
		 * Trace Buffer Unit.
		 */
		write_sysreg_s(0, SYS_TRBLIMITR_EL1);
		if (cpus_have_final_cap(ARM64_WORKAROUND_2064142)) {
			/*
			 * Some CPUs are so good, we have to drain 'em
			 * twice.
			 */
			tsb_csync();
			dsb(nsh);
		}

		/*
		 * Ensure that the Trace Buffer Unit is disabled before
		 * we start mucking with the stage-2 and trap
		 * configuration.
		 */
		isb();
	}
}

static bool __trace_needs_switch(void)
{
	return host_data_test_flag(TRBE_ENABLED) ||
	       host_data_test_flag(EL1_TRACING_CONFIGURED);
}

static void __trace_switch_to_guest(void)
{
	/* Unsupported with TRBE so disable */
	if (host_data_test_flag(TRBE_ENABLED))
		*host_data_ptr(trfcr_while_in_guest) = 0;

	__trace_do_switch(host_data_ptr(host_debug_state.trfcr_el1),
			  *host_data_ptr(trfcr_while_in_guest));
	__trace_drain_and_disable();
}

static void __trace_switch_to_host(void)
{
	u64 trblimitr_el1 = *host_data_ptr(host_debug_state.trblimitr_el1);

	if (trblimitr_el1 & TRBLIMITR_EL1_E) {
		/* Re-enable the Trace Buffer Unit for the host. */
		write_sysreg_s(trblimitr_el1, SYS_TRBLIMITR_EL1);
		isb();
		if (cpus_have_final_cap(ARM64_WORKAROUND_2038923)) {
			/*
			 * Make sure the unit is re-enabled before we
			 * poke TRFCR.
			 */
			isb();
		}
	}

	__trace_do_switch(host_data_ptr(trfcr_while_in_guest),
			  *host_data_ptr(host_debug_state.trfcr_el1));
}

static void __debug_save_brbe(void)
{
	u64 *brbcr_el1 = host_data_ptr(host_debug_state.brbcr_el1);

	*brbcr_el1 = 0;

	/* Check if the BRBE is enabled */
	if (!(read_sysreg_el1(SYS_BRBCR) & (BRBCR_ELx_E0BRE | BRBCR_ELx_ExBRE)))
		return;

	/*
	 * Prohibit branch record generation while we are in guest.
	 * Since access to BRBCR_EL1 is trapped, the guest can't
	 * modify the filtering set by the host.
	 */
	*brbcr_el1 = read_sysreg_el1(SYS_BRBCR);
	write_sysreg_el1(0, SYS_BRBCR);
}

static void __debug_restore_brbe(void)
{
	u64 brbcr_el1 = *host_data_ptr(host_debug_state.brbcr_el1);

	if (!brbcr_el1)
		return;

	/* Restore BRBE controls */
	write_sysreg_el1(brbcr_el1, SYS_BRBCR);
}

void __debug_save_host_buffers_nvhe(struct kvm_vcpu *vcpu)
{
	/* Disable and flush SPE data generation */
	if (host_data_test_flag(HAS_SPE))
		__debug_save_spe();

	/* Disable BRBE branch records */
	if (host_data_test_flag(HAS_BRBE))
		__debug_save_brbe();

	if (__trace_needs_switch())
		__trace_switch_to_guest();
}

void __debug_switch_to_guest(struct kvm_vcpu *vcpu)
{
	__debug_switch_to_guest_common(vcpu);
}

void __debug_restore_host_buffers_nvhe(struct kvm_vcpu *vcpu)
{
	if (host_data_test_flag(HAS_SPE))
		__debug_restore_spe();
	if (host_data_test_flag(HAS_BRBE))
		__debug_restore_brbe();
	if (__trace_needs_switch())
		__trace_switch_to_host();
}

void __debug_switch_to_host(struct kvm_vcpu *vcpu)
{
	__debug_switch_to_host_common(vcpu);
}
