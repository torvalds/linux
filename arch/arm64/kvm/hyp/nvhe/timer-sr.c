// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012-2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <clocksource/arm_arch_timer.h>
#include <linux/compiler.h>
#include <linux/kvm_host.h>

#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>

void __kvm_timer_set_cntvoff(u64 cntvoff)
{
	write_sysreg(cntvoff, cntvoff_el2);
}

/*
 * Should only be called on non-VHE or hVHE setups.
 * VHE systems use EL2 timers and configure EL1 timers in kvm_timer_init_vhe().
 */
void __timer_disable_traps(struct kvm_vcpu *vcpu)
{
	u64 val, shift = 0;

	if (has_hvhe())
		shift = 10;

	/* Allow physical timer/counter access for the host */
	val = read_sysreg(cnthctl_el2);
	val |= (CNTHCTL_EL1PCTEN | CNTHCTL_EL1PCEN) << shift;
	write_sysreg(val, cnthctl_el2);
}

/*
 * Should only be called on non-VHE or hVHE setups.
 * VHE systems use EL2 timers and configure EL1 timers in kvm_timer_init_vhe().
 */
void __timer_enable_traps(struct kvm_vcpu *vcpu)
{
	u64 clr = 0, set = 0;

	/*
	 * Disallow physical timer access for the guest
	 * Physical counter access is allowed if no offset is enforced
	 * or running protected (we don't offset anything in this case).
	 */
	clr = CNTHCTL_EL1PCEN;
	if (is_protected_kvm_enabled() ||
	    !kern_hyp_va(vcpu->kvm)->arch.timer_data.poffset)
		set |= CNTHCTL_EL1PCTEN;
	else
		clr |= CNTHCTL_EL1PCTEN;

	if (has_hvhe()) {
		clr <<= 10;
		set <<= 10;
	}

	sysreg_clear_set(cnthctl_el2, clr, set);
}
