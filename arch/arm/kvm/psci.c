/*
 * Copyright (C) 2012 - ARM Ltd
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

#include <linux/kvm_host.h>
#include <linux/wait.h>

#include <asm/cputype.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_psci.h>

/*
 * This is an implementation of the Power State Coordination Interface
 * as described in ARM document number ARM DEN 0022A.
 */

static void kvm_psci_vcpu_off(struct kvm_vcpu *vcpu)
{
	vcpu->arch.pause = true;
}

static unsigned long kvm_psci_vcpu_on(struct kvm_vcpu *source_vcpu)
{
	struct kvm *kvm = source_vcpu->kvm;
	struct kvm_vcpu *vcpu = NULL, *tmp;
	wait_queue_head_t *wq;
	unsigned long cpu_id;
	unsigned long mpidr;
	phys_addr_t target_pc;
	int i;

	cpu_id = *vcpu_reg(source_vcpu, 1);
	if (vcpu_mode_is_32bit(source_vcpu))
		cpu_id &= ~((u32) 0);

	kvm_for_each_vcpu(i, tmp, kvm) {
		mpidr = kvm_vcpu_get_mpidr(tmp);
		if ((mpidr & MPIDR_HWID_BITMASK) == (cpu_id & MPIDR_HWID_BITMASK)) {
			vcpu = tmp;
			break;
		}
	}

	/*
	 * Make sure the caller requested a valid CPU and that the CPU is
	 * turned off.
	 */
	if (!vcpu || !vcpu->arch.pause)
		return KVM_PSCI_RET_INVAL;

	target_pc = *vcpu_reg(source_vcpu, 2);

	kvm_reset_vcpu(vcpu);

	/* Gracefully handle Thumb2 entry point */
	if (vcpu_mode_is_32bit(vcpu) && (target_pc & 1)) {
		target_pc &= ~((phys_addr_t) 1);
		vcpu_set_thumb(vcpu);
	}

	/* Propagate caller endianness */
	if (kvm_vcpu_is_be(source_vcpu))
		kvm_vcpu_set_be(vcpu);

	*vcpu_pc(vcpu) = target_pc;
	vcpu->arch.pause = false;
	smp_mb();		/* Make sure the above is visible */

	wq = kvm_arch_vcpu_wq(vcpu);
	wake_up_interruptible(wq);

	return KVM_PSCI_RET_SUCCESS;
}

/**
 * kvm_psci_call - handle PSCI call if r0 value is in range
 * @vcpu: Pointer to the VCPU struct
 *
 * Handle PSCI calls from guests through traps from HVC instructions.
 * The calling convention is similar to SMC calls to the secure world where
 * the function number is placed in r0 and this function returns true if the
 * function number specified in r0 is withing the PSCI range, and false
 * otherwise.
 */
bool kvm_psci_call(struct kvm_vcpu *vcpu)
{
	unsigned long psci_fn = *vcpu_reg(vcpu, 0) & ~((u32) 0);
	unsigned long val;

	switch (psci_fn) {
	case KVM_PSCI_FN_CPU_OFF:
		kvm_psci_vcpu_off(vcpu);
		val = KVM_PSCI_RET_SUCCESS;
		break;
	case KVM_PSCI_FN_CPU_ON:
		val = kvm_psci_vcpu_on(vcpu);
		break;
	case KVM_PSCI_FN_CPU_SUSPEND:
	case KVM_PSCI_FN_MIGRATE:
		val = KVM_PSCI_RET_NI;
		break;

	default:
		return false;
	}

	*vcpu_reg(vcpu, 0) = val;
	return true;
}
