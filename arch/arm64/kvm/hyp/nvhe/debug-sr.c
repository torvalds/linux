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

static void __debug_save_spe(u64 *pmscr_el1)
{
	u64 reg;

	/* Clear pmscr in case of early return */
	*pmscr_el1 = 0;

	/*
	 * At this point, we know that this CPU implements
	 * SPE and is available to the host.
	 * Check if the host is actually using it ?
	 */
	reg = read_sysreg_s(SYS_PMBLIMITR_EL1);
	if (!(reg & BIT(PMBLIMITR_EL1_E_SHIFT)))
		return;

	/* Yes; save the control register and disable data generation */
	*pmscr_el1 = read_sysreg_el1(SYS_PMSCR);
	write_sysreg_el1(0, SYS_PMSCR);
	isb();

	/* Now drain all buffered data to memory */
	psb_csync();
}

static void __debug_restore_spe(u64 pmscr_el1)
{
	if (!pmscr_el1)
		return;

	/* The host page table is installed, but not yet synchronised */
	isb();

	/* Re-enable data generation */
	write_sysreg_el1(pmscr_el1, SYS_PMSCR);
}

static void __trace_do_switch(u64 *saved_trfcr, u64 new_trfcr)
{
	*saved_trfcr = read_sysreg_el1(SYS_TRFCR);
	write_sysreg_el1(new_trfcr, SYS_TRFCR);
}

static bool __trace_needs_drain(void)
{
	if (is_protected_kvm_enabled() && host_data_test_flag(HAS_TRBE))
		return read_sysreg_s(SYS_TRBLIMITR_EL1) & TRBLIMITR_EL1_E;

	return host_data_test_flag(TRBE_ENABLED);
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

	if (__trace_needs_drain()) {
		isb();
		tsb_csync();
	}
}

static void __trace_switch_to_host(void)
{
	__trace_do_switch(host_data_ptr(trfcr_while_in_guest),
			  *host_data_ptr(host_debug_state.trfcr_el1));
}

static void __debug_save_brbe(u64 *brbcr_el1)
{
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

static void __debug_restore_brbe(u64 brbcr_el1)
{
	if (!brbcr_el1)
		return;

	/* Restore BRBE controls */
	write_sysreg_el1(brbcr_el1, SYS_BRBCR);
}

void __debug_save_host_buffers_nvhe(struct kvm_vcpu *vcpu)
{
	/* Disable and flush SPE data generation */
	if (host_data_test_flag(HAS_SPE))
		__debug_save_spe(host_data_ptr(host_debug_state.pmscr_el1));

	/* Disable BRBE branch records */
	if (host_data_test_flag(HAS_BRBE))
		__debug_save_brbe(host_data_ptr(host_debug_state.brbcr_el1));

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
		__debug_restore_spe(*host_data_ptr(host_debug_state.pmscr_el1));
	if (host_data_test_flag(HAS_BRBE))
		__debug_restore_brbe(*host_data_ptr(host_debug_state.brbcr_el1));
	if (__trace_needs_switch())
		__trace_switch_to_host();
}

void __debug_switch_to_host(struct kvm_vcpu *vcpu)
{
	__debug_switch_to_host_common(vcpu);
}
