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

	/* SPE present on this CPU? */
	if (!cpuid_feature_extract_unsigned_field(read_sysreg(id_aa64dfr0_el1),
						  ID_AA64DFR0_PMSVER_SHIFT))
		return;

	/* Yes; is it owned by EL3? */
	reg = read_sysreg_s(SYS_PMBIDR_EL1);
	if (reg & BIT(SYS_PMBIDR_EL1_P_SHIFT))
		return;

	/* No; is the host actually using the thing? */
	reg = read_sysreg_s(SYS_PMBLIMITR_EL1);
	if (!(reg & BIT(SYS_PMBLIMITR_EL1_E_SHIFT)))
		return;

	/* Yes; save the control register and disable data generation */
	*pmscr_el1 = read_sysreg_s(SYS_PMSCR_EL1);
	write_sysreg_s(0, SYS_PMSCR_EL1);
	isb();

	/* Now drain all buffered data to memory */
	psb_csync();
	dsb(nsh);
}

static void __debug_restore_spe(u64 pmscr_el1)
{
	if (!pmscr_el1)
		return;

	/* The host page table is installed, but not yet synchronised */
	isb();

	/* Re-enable data generation */
	write_sysreg_s(pmscr_el1, SYS_PMSCR_EL1);
}

void __debug_save_host_buffers_nvhe(struct kvm_vcpu *vcpu)
{
	/* Disable and flush SPE data generation */
	__debug_save_spe(&vcpu->arch.host_debug_state.pmscr_el1);
}

void __debug_switch_to_guest(struct kvm_vcpu *vcpu)
{
	__debug_switch_to_guest_common(vcpu);
}

void __debug_restore_host_buffers_nvhe(struct kvm_vcpu *vcpu)
{
	__debug_restore_spe(vcpu->arch.host_debug_state.pmscr_el1);
}

void __debug_switch_to_host(struct kvm_vcpu *vcpu)
{
	__debug_switch_to_host_common(vcpu);
}

u32 __kvm_get_mdcr_el2(void)
{
	return read_sysreg(mdcr_el2);
}
