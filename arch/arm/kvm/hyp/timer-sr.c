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
		timer->cntv_ctl = read_sysreg(CNTV_CTL);
		timer->cntv_cval = read_sysreg(CNTV_CVAL);
	}

	/* Disable the virtual timer */
	write_sysreg(0, CNTV_CTL);

	/* Allow physical timer/counter access for the host */
	val = read_sysreg(CNTHCTL);
	val |= CNTHCTL_EL1PCTEN | CNTHCTL_EL1PCEN;
	write_sysreg(val, CNTHCTL);

	/* Clear cntvoff for the host */
	write_sysreg(0, CNTVOFF);
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
	val = read_sysreg(CNTHCTL);
	val &= ~CNTHCTL_EL1PCEN;
	val |= CNTHCTL_EL1PCTEN;
	write_sysreg(val, CNTHCTL);

	if (kvm->arch.timer.enabled) {
		write_sysreg(kvm->arch.timer.cntvoff, CNTVOFF);
		write_sysreg(timer->cntv_cval, CNTV_CVAL);
		isb();
		write_sysreg(timer->cntv_ctl, CNTV_CTL);
	}
}
