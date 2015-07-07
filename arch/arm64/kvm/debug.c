/*
 * Debug and Guest Debug support
 *
 * Copyright (C) 2015 - Linaro Ltd
 * Author: Alex Bennée <alex.bennee@linaro.org>
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

#include <linux/kvm_host.h>

#include <asm/kvm_arm.h>

static DEFINE_PER_CPU(u32, mdcr_el2);

/**
 * kvm_arm_init_debug - grab what we need for debug
 *
 * Currently the sole task of this function is to retrieve the initial
 * value of mdcr_el2 so we can preserve MDCR_EL2.HPMN which has
 * presumably been set-up by some knowledgeable bootcode.
 *
 * It is called once per-cpu during CPU hyp initialisation.
 */

void kvm_arm_init_debug(void)
{
	__this_cpu_write(mdcr_el2, kvm_call_hyp(__kvm_get_mdcr_el2));
}


/**
 * kvm_arm_setup_debug - set up debug related stuff
 *
 * @vcpu:	the vcpu pointer
 *
 * This is called before each entry into the hypervisor to setup any
 * debug related registers. Currently this just ensures we will trap
 * access to:
 *  - Performance monitors (MDCR_EL2_TPM/MDCR_EL2_TPMCR)
 *  - Debug ROM Address (MDCR_EL2_TDRA)
 *  - OS related registers (MDCR_EL2_TDOSA)
 *
 * Additionally, KVM only traps guest accesses to the debug registers if
 * the guest is not actively using them (see the KVM_ARM64_DEBUG_DIRTY
 * flag on vcpu->arch.debug_flags).  Since the guest must not interfere
 * with the hardware state when debugging the guest, we must ensure that
 * trapping is enabled whenever we are debugging the guest using the
 * debug registers.
 */

void kvm_arm_setup_debug(struct kvm_vcpu *vcpu)
{
	bool trap_debug = !(vcpu->arch.debug_flags & KVM_ARM64_DEBUG_DIRTY);

	vcpu->arch.mdcr_el2 = __this_cpu_read(mdcr_el2) & MDCR_EL2_HPMN_MASK;
	vcpu->arch.mdcr_el2 |= (MDCR_EL2_TPM |
				MDCR_EL2_TPMCR |
				MDCR_EL2_TDRA |
				MDCR_EL2_TDOSA);

	/* Trap on access to debug registers? */
	if (trap_debug)
		vcpu->arch.mdcr_el2 |= MDCR_EL2_TDA;

}

void kvm_arm_clear_debug(struct kvm_vcpu *vcpu)
{
	/* Nothing to do yet */
}
