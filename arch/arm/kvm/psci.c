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

#define AFFINITY_MASK(level)	~((0x1UL << ((level) * MPIDR_LEVEL_BITS)) - 1)

static unsigned long psci_affinity_mask(unsigned long affinity_level)
{
	if (affinity_level <= 3)
		return MPIDR_HWID_BITMASK & AFFINITY_MASK(affinity_level);

	return 0;
}

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
	unsigned long context_id;
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
	if (!vcpu)
		return PSCI_RET_INVALID_PARAMS;
	if (!vcpu->arch.pause) {
		if (kvm_psci_version(source_vcpu) != KVM_ARM_PSCI_0_1)
			return PSCI_RET_ALREADY_ON;
		else
			return PSCI_RET_INVALID_PARAMS;
	}

	target_pc = *vcpu_reg(source_vcpu, 2);
	context_id = *vcpu_reg(source_vcpu, 3);

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
	/*
	 * NOTE: We always update r0 (or x0) because for PSCI v0.1
	 * the general puspose registers are undefined upon CPU_ON.
	 */
	*vcpu_reg(vcpu, 0) = context_id;
	vcpu->arch.pause = false;
	smp_mb();		/* Make sure the above is visible */

	wq = kvm_arch_vcpu_wq(vcpu);
	wake_up_interruptible(wq);

	return PSCI_RET_SUCCESS;
}

static unsigned long kvm_psci_vcpu_affinity_info(struct kvm_vcpu *vcpu)
{
	int i;
	unsigned long mpidr;
	unsigned long target_affinity;
	unsigned long target_affinity_mask;
	unsigned long lowest_affinity_level;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_vcpu *tmp;

	target_affinity = *vcpu_reg(vcpu, 1);
	lowest_affinity_level = *vcpu_reg(vcpu, 2);

	/* Determine target affinity mask */
	target_affinity_mask = psci_affinity_mask(lowest_affinity_level);
	if (!target_affinity_mask)
		return PSCI_RET_INVALID_PARAMS;

	/* Ignore other bits of target affinity */
	target_affinity &= target_affinity_mask;

	/*
	 * If one or more VCPU matching target affinity are running
	 * then ON else OFF
	 */
	kvm_for_each_vcpu(i, tmp, kvm) {
		mpidr = kvm_vcpu_get_mpidr(tmp);
		if (((mpidr & target_affinity_mask) == target_affinity) &&
		    !tmp->arch.pause) {
			return PSCI_0_2_AFFINITY_LEVEL_ON;
		}
	}

	return PSCI_0_2_AFFINITY_LEVEL_OFF;
}

static void kvm_prepare_system_event(struct kvm_vcpu *vcpu, u32 type)
{
	memset(&vcpu->run->system_event, 0, sizeof(vcpu->run->system_event));
	vcpu->run->system_event.type = type;
	vcpu->run->exit_reason = KVM_EXIT_SYSTEM_EVENT;
}

static void kvm_psci_system_off(struct kvm_vcpu *vcpu)
{
	kvm_prepare_system_event(vcpu, KVM_SYSTEM_EVENT_SHUTDOWN);
}

static void kvm_psci_system_reset(struct kvm_vcpu *vcpu)
{
	kvm_prepare_system_event(vcpu, KVM_SYSTEM_EVENT_RESET);
}

int kvm_psci_version(struct kvm_vcpu *vcpu)
{
	if (test_bit(KVM_ARM_VCPU_PSCI_0_2, vcpu->arch.features))
		return KVM_ARM_PSCI_0_2;

	return KVM_ARM_PSCI_0_1;
}

static int kvm_psci_0_2_call(struct kvm_vcpu *vcpu)
{
	int ret = 1;
	unsigned long psci_fn = *vcpu_reg(vcpu, 0) & ~((u32) 0);
	unsigned long val;

	switch (psci_fn) {
	case PSCI_0_2_FN_PSCI_VERSION:
		/*
		 * Bits[31:16] = Major Version = 0
		 * Bits[15:0] = Minor Version = 2
		 */
		val = 2;
		break;
	case PSCI_0_2_FN_CPU_OFF:
		kvm_psci_vcpu_off(vcpu);
		val = PSCI_RET_SUCCESS;
		break;
	case PSCI_0_2_FN_CPU_ON:
	case PSCI_0_2_FN64_CPU_ON:
		val = kvm_psci_vcpu_on(vcpu);
		break;
	case PSCI_0_2_FN_AFFINITY_INFO:
	case PSCI_0_2_FN64_AFFINITY_INFO:
		val = kvm_psci_vcpu_affinity_info(vcpu);
		break;
	case PSCI_0_2_FN_MIGRATE:
	case PSCI_0_2_FN64_MIGRATE:
		val = PSCI_RET_NOT_SUPPORTED;
		break;
	case PSCI_0_2_FN_MIGRATE_INFO_TYPE:
		/*
		 * Trusted OS is MP hence does not require migration
	         * or
		 * Trusted OS is not present
		 */
		val = PSCI_0_2_TOS_MP;
		break;
	case PSCI_0_2_FN_MIGRATE_INFO_UP_CPU:
	case PSCI_0_2_FN64_MIGRATE_INFO_UP_CPU:
		val = PSCI_RET_NOT_SUPPORTED;
		break;
	case PSCI_0_2_FN_SYSTEM_OFF:
		kvm_psci_system_off(vcpu);
		/*
		 * We should'nt be going back to guest VCPU after
		 * receiving SYSTEM_OFF request.
		 *
		 * If user space accidently/deliberately resumes
		 * guest VCPU after SYSTEM_OFF request then guest
		 * VCPU should see internal failure from PSCI return
		 * value. To achieve this, we preload r0 (or x0) with
		 * PSCI return value INTERNAL_FAILURE.
		 */
		val = PSCI_RET_INTERNAL_FAILURE;
		ret = 0;
		break;
	case PSCI_0_2_FN_SYSTEM_RESET:
		kvm_psci_system_reset(vcpu);
		/*
		 * Same reason as SYSTEM_OFF for preloading r0 (or x0)
		 * with PSCI return value INTERNAL_FAILURE.
		 */
		val = PSCI_RET_INTERNAL_FAILURE;
		ret = 0;
		break;
	case PSCI_0_2_FN_CPU_SUSPEND:
	case PSCI_0_2_FN64_CPU_SUSPEND:
		val = PSCI_RET_NOT_SUPPORTED;
		break;
	default:
		return -EINVAL;
	}

	*vcpu_reg(vcpu, 0) = val;
	return ret;
}

static int kvm_psci_0_1_call(struct kvm_vcpu *vcpu)
{
	unsigned long psci_fn = *vcpu_reg(vcpu, 0) & ~((u32) 0);
	unsigned long val;

	switch (psci_fn) {
	case KVM_PSCI_FN_CPU_OFF:
		kvm_psci_vcpu_off(vcpu);
		val = PSCI_RET_SUCCESS;
		break;
	case KVM_PSCI_FN_CPU_ON:
		val = kvm_psci_vcpu_on(vcpu);
		break;
	case KVM_PSCI_FN_CPU_SUSPEND:
	case KVM_PSCI_FN_MIGRATE:
		val = PSCI_RET_NOT_SUPPORTED;
		break;
	default:
		return -EINVAL;
	}

	*vcpu_reg(vcpu, 0) = val;
	return 1;
}

/**
 * kvm_psci_call - handle PSCI call if r0 value is in range
 * @vcpu: Pointer to the VCPU struct
 *
 * Handle PSCI calls from guests through traps from HVC instructions.
 * The calling convention is similar to SMC calls to the secure world
 * where the function number is placed in r0.
 *
 * This function returns: > 0 (success), 0 (success but exit to user
 * space), and < 0 (errors)
 *
 * Errors:
 * -EINVAL: Unrecognized PSCI function
 */
int kvm_psci_call(struct kvm_vcpu *vcpu)
{
	switch (kvm_psci_version(vcpu)) {
	case KVM_ARM_PSCI_0_2:
		return kvm_psci_0_2_call(vcpu);
	case KVM_ARM_PSCI_0_1:
		return kvm_psci_0_1_call(vcpu);
	default:
		return -EINVAL;
	};
}
