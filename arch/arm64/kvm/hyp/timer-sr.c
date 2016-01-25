/*
 * Copyright (C) 2012-2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <clocksource/arm_arch_timer.h>
#include <linux/compiler.h>
#include <linux/kvm_host.h>

#include <asm/kvm_mmu.h>

#include "hyp.h"

/* vcpu is already in the HYP VA space */
void __hyp_text __timer_save_state(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = kern_hyp_va(vcpu->kvm);
	struct arch_timer_cpu *timer = &vcpu->arch.timer_cpu;
	u64 val;

	if (kvm->arch.timer.enabled) {
		timer->cntv_ctl = read_sysreg(cntv_ctl_el0);
		timer->cntv_cval = read_sysreg(cntv_cval_el0);
	}

	/* Disable the virtual timer */
	write_sysreg(0, cntv_ctl_el0);

	/* Allow physical timer/counter access for the host */
	val = read_sysreg(cnthctl_el2);
	val |= CNTHCTL_EL1PCTEN | CNTHCTL_EL1PCEN;
	write_sysreg(val, cnthctl_el2);

	/* Clear cntvoff for the host */
	write_sysreg(0, cntvoff_el2);
}

void __hyp_text __timer_restore_state(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = kern_hyp_va(vcpu->kvm);
	struct arch_timer_cpu *timer = &vcpu->arch.timer_cpu;
	u64 val;

	/*
	 * Disallow physical timer access for the guest
	 * Physical counter access is allowed
	 */
	val = read_sysreg(cnthctl_el2);
	val &= ~CNTHCTL_EL1PCEN;
	val |= CNTHCTL_EL1PCTEN;
	write_sysreg(val, cnthctl_el2);

	if (kvm->arch.timer.enabled) {
		write_sysreg(kvm->arch.timer.cntvoff, cntvoff_el2);
		write_sysreg(timer->cntv_cval, cntv_cval_el0);
		isb();
		write_sysreg(timer->cntv_ctl, cntv_ctl_el0);
	}
}
