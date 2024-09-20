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

static void __debug_save_trace(u64 *trfcr_el1)
{
	*trfcr_el1 = 0;

	/* Check if the TRBE is enabled */
	if (!(read_sysreg_s(SYS_TRBLIMITR_EL1) & TRBLIMITR_EL1_E))
		return;
	/*
	 * Prohibit trace generation while we are in guest.
	 * Since access to TRFCR_EL1 is trapped, the guest can't
	 * modify the filtering set by the host.
	 */
	*trfcr_el1 = read_sysreg_el1(SYS_TRFCR);
	write_sysreg_el1(0, SYS_TRFCR);
	isb();
	/* Drain the trace buffer to memory */
	tsb_csync();
}

static void __debug_restore_trace(u64 trfcr_el1)
{
	if (!trfcr_el1)
		return;

	/* Restore trace filter controls */
	write_sysreg_el1(trfcr_el1, SYS_TRFCR);
}

void __debug_save_host_buffers_nvhe(struct kvm_vcpu *vcpu)
{
	/* Disable and flush SPE data generation */
	if (vcpu_get_flag(vcpu, DEBUG_STATE_SAVE_SPE))
		__debug_save_spe(host_data_ptr(host_debug_state.pmscr_el1));
	/* Disable and flush Self-Hosted Trace generation */
	if (vcpu_get_flag(vcpu, DEBUG_STATE_SAVE_TRBE))
		__debug_save_trace(host_data_ptr(host_debug_state.trfcr_el1));
}

void __debug_switch_to_guest(struct kvm_vcpu *vcpu)
{
	__debug_switch_to_guest_common(vcpu);
}

void __debug_restore_host_buffers_nvhe(struct kvm_vcpu *vcpu)
{
	if (vcpu_get_flag(vcpu, DEBUG_STATE_SAVE_SPE))
		__debug_restore_spe(*host_data_ptr(host_debug_state.pmscr_el1));
	if (vcpu_get_flag(vcpu, DEBUG_STATE_SAVE_TRBE))
		__debug_restore_trace(*host_data_ptr(host_debug_state.trfcr_el1));
}

void __debug_switch_to_host(struct kvm_vcpu *vcpu)
{
	__debug_switch_to_host_common(vcpu);
}

u64 __kvm_get_mdcr_el2(void)
{
	return read_sysreg(mdcr_el2);
}
