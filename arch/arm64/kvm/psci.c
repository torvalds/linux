// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <linux/arm-smccc.h>
#include <linux/preempt.h>
#include <linux/kvm_host.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include <asm/cputype.h>
#include <asm/kvm_emulate.h>

#include <kvm/arm_psci.h>
#include <kvm/arm_hypercalls.h>

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

static unsigned long kvm_psci_vcpu_suspend(struct kvm_vcpu *vcpu)
{
	/*
	 * NOTE: For simplicity, we make VCPU suspend emulation to be
	 * same-as WFI (Wait-for-interrupt) emulation.
	 *
	 * This means for KVM the wakeup events are interrupts and
	 * this is consistent with intended use of StateID as described
	 * in section 5.4.1 of PSCI v0.2 specification (ARM DEN 0022A).
	 *
	 * Further, we also treat power-down request to be same as
	 * stand-by request as-per section 5.4.2 clause 3 of PSCI v0.2
	 * specification (ARM DEN 0022A). This means all suspend states
	 * for KVM will preserve the register state.
	 */
	kvm_vcpu_wfi(vcpu);

	return PSCI_RET_SUCCESS;
}

static inline bool kvm_psci_valid_affinity(struct kvm_vcpu *vcpu,
					   unsigned long affinity)
{
	return !(affinity & ~MPIDR_HWID_BITMASK);
}

static unsigned long kvm_psci_vcpu_on(struct kvm_vcpu *source_vcpu)
{
	struct vcpu_reset_state *reset_state;
	struct kvm *kvm = source_vcpu->kvm;
	struct kvm_vcpu *vcpu = NULL;
	int ret = PSCI_RET_SUCCESS;
	unsigned long cpu_id;

	cpu_id = smccc_get_arg1(source_vcpu);
	if (!kvm_psci_valid_affinity(source_vcpu, cpu_id))
		return PSCI_RET_INVALID_PARAMS;

	vcpu = kvm_mpidr_to_vcpu(kvm, cpu_id);

	/*
	 * Make sure the caller requested a valid CPU and that the CPU is
	 * turned off.
	 */
	if (!vcpu)
		return PSCI_RET_INVALID_PARAMS;

	spin_lock(&vcpu->arch.mp_state_lock);
	if (!kvm_arm_vcpu_stopped(vcpu)) {
		if (kvm_psci_version(source_vcpu) != KVM_ARM_PSCI_0_1)
			ret = PSCI_RET_ALREADY_ON;
		else
			ret = PSCI_RET_INVALID_PARAMS;

		goto out_unlock;
	}

	reset_state = &vcpu->arch.reset_state;

	reset_state->pc = smccc_get_arg2(source_vcpu);

	/* Propagate caller endianness */
	reset_state->be = kvm_vcpu_is_be(source_vcpu);

	/*
	 * NOTE: We always update r0 (or x0) because for PSCI v0.1
	 * the general purpose registers are undefined upon CPU_ON.
	 */
	reset_state->r0 = smccc_get_arg3(source_vcpu);

	reset_state->reset = true;
	kvm_make_request(KVM_REQ_VCPU_RESET, vcpu);

	/*
	 * Make sure the reset request is observed if the RUNNABLE mp_state is
	 * observed.
	 */
	smp_wmb();

	vcpu->arch.mp_state.mp_state = KVM_MP_STATE_RUNNABLE;
	kvm_vcpu_wake_up(vcpu);

out_unlock:
	spin_unlock(&vcpu->arch.mp_state_lock);
	return ret;
}

static unsigned long kvm_psci_vcpu_affinity_info(struct kvm_vcpu *vcpu)
{
	int matching_cpus = 0;
	unsigned long i, mpidr;
	unsigned long target_affinity;
	unsigned long target_affinity_mask;
	unsigned long lowest_affinity_level;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_vcpu *tmp;

	target_affinity = smccc_get_arg1(vcpu);
	lowest_affinity_level = smccc_get_arg2(vcpu);

	if (!kvm_psci_valid_affinity(vcpu, target_affinity))
		return PSCI_RET_INVALID_PARAMS;

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
		mpidr = kvm_vcpu_get_mpidr_aff(tmp);
		if ((mpidr & target_affinity_mask) == target_affinity) {
			matching_cpus++;
			if (!kvm_arm_vcpu_stopped(tmp))
				return PSCI_0_2_AFFINITY_LEVEL_ON;
		}
	}

	if (!matching_cpus)
		return PSCI_RET_INVALID_PARAMS;

	return PSCI_0_2_AFFINITY_LEVEL_OFF;
}

static void kvm_prepare_system_event(struct kvm_vcpu *vcpu, u32 type, u64 flags)
{
	unsigned long i;
	struct kvm_vcpu *tmp;

	/*
	 * The KVM ABI specifies that a system event exit may call KVM_RUN
	 * again and may perform shutdown/reboot at a later time that when the
	 * actual request is made.  Since we are implementing PSCI and a
	 * caller of PSCI reboot and shutdown expects that the system shuts
	 * down or reboots immediately, let's make sure that VCPUs are not run
	 * after this call is handled and before the VCPUs have been
	 * re-initialized.
	 */
	kvm_for_each_vcpu(i, tmp, vcpu->kvm) {
		spin_lock(&tmp->arch.mp_state_lock);
		WRITE_ONCE(tmp->arch.mp_state.mp_state, KVM_MP_STATE_STOPPED);
		spin_unlock(&tmp->arch.mp_state_lock);
	}
	kvm_make_all_cpus_request(vcpu->kvm, KVM_REQ_SLEEP);

	memset(&vcpu->run->system_event, 0, sizeof(vcpu->run->system_event));
	vcpu->run->system_event.type = type;
	vcpu->run->system_event.ndata = 1;
	vcpu->run->system_event.data[0] = flags;
	vcpu->run->exit_reason = KVM_EXIT_SYSTEM_EVENT;
}

static void kvm_psci_system_off(struct kvm_vcpu *vcpu)
{
	kvm_prepare_system_event(vcpu, KVM_SYSTEM_EVENT_SHUTDOWN, 0);
}

static void kvm_psci_system_reset(struct kvm_vcpu *vcpu)
{
	kvm_prepare_system_event(vcpu, KVM_SYSTEM_EVENT_RESET, 0);
}

static void kvm_psci_system_reset2(struct kvm_vcpu *vcpu)
{
	kvm_prepare_system_event(vcpu, KVM_SYSTEM_EVENT_RESET,
				 KVM_SYSTEM_EVENT_RESET_FLAG_PSCI_RESET2);
}

static void kvm_psci_system_suspend(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;

	memset(&run->system_event, 0, sizeof(vcpu->run->system_event));
	run->system_event.type = KVM_SYSTEM_EVENT_SUSPEND;
	run->exit_reason = KVM_EXIT_SYSTEM_EVENT;
}

static void kvm_psci_narrow_to_32bit(struct kvm_vcpu *vcpu)
{
	int i;

	/*
	 * Zero the input registers' upper 32 bits. They will be fully
	 * zeroed on exit, so we're fine changing them in place.
	 */
	for (i = 1; i < 4; i++)
		vcpu_set_reg(vcpu, i, lower_32_bits(vcpu_get_reg(vcpu, i)));
}

static unsigned long kvm_psci_check_allowed_function(struct kvm_vcpu *vcpu, u32 fn)
{
	/*
	 * Prevent 32 bit guests from calling 64 bit PSCI functions.
	 */
	if ((fn & PSCI_0_2_64BIT) && vcpu_mode_is_32bit(vcpu))
		return PSCI_RET_NOT_SUPPORTED;

	return 0;
}

static int kvm_psci_0_2_call(struct kvm_vcpu *vcpu)
{
	u32 psci_fn = smccc_get_function(vcpu);
	unsigned long val;
	int ret = 1;

	switch (psci_fn) {
	case PSCI_0_2_FN_PSCI_VERSION:
		/*
		 * Bits[31:16] = Major Version = 0
		 * Bits[15:0] = Minor Version = 2
		 */
		val = KVM_ARM_PSCI_0_2;
		break;
	case PSCI_0_2_FN_CPU_SUSPEND:
	case PSCI_0_2_FN64_CPU_SUSPEND:
		val = kvm_psci_vcpu_suspend(vcpu);
		break;
	case PSCI_0_2_FN_CPU_OFF:
		kvm_arm_vcpu_power_off(vcpu);
		val = PSCI_RET_SUCCESS;
		break;
	case PSCI_0_2_FN_CPU_ON:
		kvm_psci_narrow_to_32bit(vcpu);
		fallthrough;
	case PSCI_0_2_FN64_CPU_ON:
		val = kvm_psci_vcpu_on(vcpu);
		break;
	case PSCI_0_2_FN_AFFINITY_INFO:
		kvm_psci_narrow_to_32bit(vcpu);
		fallthrough;
	case PSCI_0_2_FN64_AFFINITY_INFO:
		val = kvm_psci_vcpu_affinity_info(vcpu);
		break;
	case PSCI_0_2_FN_MIGRATE_INFO_TYPE:
		/*
		 * Trusted OS is MP hence does not require migration
	         * or
		 * Trusted OS is not present
		 */
		val = PSCI_0_2_TOS_MP;
		break;
	case PSCI_0_2_FN_SYSTEM_OFF:
		kvm_psci_system_off(vcpu);
		/*
		 * We shouldn't be going back to guest VCPU after
		 * receiving SYSTEM_OFF request.
		 *
		 * If user space accidentally/deliberately resumes
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
	default:
		val = PSCI_RET_NOT_SUPPORTED;
		break;
	}

	smccc_set_retval(vcpu, val, 0, 0, 0);
	return ret;
}

static int kvm_psci_1_x_call(struct kvm_vcpu *vcpu, u32 minor)
{
	unsigned long val = PSCI_RET_NOT_SUPPORTED;
	u32 psci_fn = smccc_get_function(vcpu);
	struct kvm *kvm = vcpu->kvm;
	u32 arg;
	int ret = 1;

	switch(psci_fn) {
	case PSCI_0_2_FN_PSCI_VERSION:
		val = minor == 0 ? KVM_ARM_PSCI_1_0 : KVM_ARM_PSCI_1_1;
		break;
	case PSCI_1_0_FN_PSCI_FEATURES:
		arg = smccc_get_arg1(vcpu);
		val = kvm_psci_check_allowed_function(vcpu, arg);
		if (val)
			break;

		val = PSCI_RET_NOT_SUPPORTED;

		switch(arg) {
		case PSCI_0_2_FN_PSCI_VERSION:
		case PSCI_0_2_FN_CPU_SUSPEND:
		case PSCI_0_2_FN64_CPU_SUSPEND:
		case PSCI_0_2_FN_CPU_OFF:
		case PSCI_0_2_FN_CPU_ON:
		case PSCI_0_2_FN64_CPU_ON:
		case PSCI_0_2_FN_AFFINITY_INFO:
		case PSCI_0_2_FN64_AFFINITY_INFO:
		case PSCI_0_2_FN_MIGRATE_INFO_TYPE:
		case PSCI_0_2_FN_SYSTEM_OFF:
		case PSCI_0_2_FN_SYSTEM_RESET:
		case PSCI_1_0_FN_PSCI_FEATURES:
		case ARM_SMCCC_VERSION_FUNC_ID:
			val = 0;
			break;
		case PSCI_1_0_FN_SYSTEM_SUSPEND:
		case PSCI_1_0_FN64_SYSTEM_SUSPEND:
			if (test_bit(KVM_ARCH_FLAG_SYSTEM_SUSPEND_ENABLED, &kvm->arch.flags))
				val = 0;
			break;
		case PSCI_1_1_FN_SYSTEM_RESET2:
		case PSCI_1_1_FN64_SYSTEM_RESET2:
			if (minor >= 1)
				val = 0;
			break;
		}
		break;
	case PSCI_1_0_FN_SYSTEM_SUSPEND:
		kvm_psci_narrow_to_32bit(vcpu);
		fallthrough;
	case PSCI_1_0_FN64_SYSTEM_SUSPEND:
		/*
		 * Return directly to userspace without changing the vCPU's
		 * registers. Userspace depends on reading the SMCCC parameters
		 * to implement SYSTEM_SUSPEND.
		 */
		if (test_bit(KVM_ARCH_FLAG_SYSTEM_SUSPEND_ENABLED, &kvm->arch.flags)) {
			kvm_psci_system_suspend(vcpu);
			return 0;
		}
		break;
	case PSCI_1_1_FN_SYSTEM_RESET2:
		kvm_psci_narrow_to_32bit(vcpu);
		fallthrough;
	case PSCI_1_1_FN64_SYSTEM_RESET2:
		if (minor >= 1) {
			arg = smccc_get_arg1(vcpu);

			if (arg <= PSCI_1_1_RESET_TYPE_SYSTEM_WARM_RESET ||
			    arg >= PSCI_1_1_RESET_TYPE_VENDOR_START) {
				kvm_psci_system_reset2(vcpu);
				vcpu_set_reg(vcpu, 0, PSCI_RET_INTERNAL_FAILURE);
				return 0;
			}

			val = PSCI_RET_INVALID_PARAMS;
			break;
		}
		break;
	default:
		return kvm_psci_0_2_call(vcpu);
	}

	smccc_set_retval(vcpu, val, 0, 0, 0);
	return ret;
}

static int kvm_psci_0_1_call(struct kvm_vcpu *vcpu)
{
	u32 psci_fn = smccc_get_function(vcpu);
	unsigned long val;

	switch (psci_fn) {
	case KVM_PSCI_FN_CPU_OFF:
		kvm_arm_vcpu_power_off(vcpu);
		val = PSCI_RET_SUCCESS;
		break;
	case KVM_PSCI_FN_CPU_ON:
		val = kvm_psci_vcpu_on(vcpu);
		break;
	default:
		val = PSCI_RET_NOT_SUPPORTED;
		break;
	}

	smccc_set_retval(vcpu, val, 0, 0, 0);
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
	u32 psci_fn = smccc_get_function(vcpu);
	unsigned long val;

	val = kvm_psci_check_allowed_function(vcpu, psci_fn);
	if (val) {
		smccc_set_retval(vcpu, val, 0, 0, 0);
		return 1;
	}

	switch (kvm_psci_version(vcpu)) {
	case KVM_ARM_PSCI_1_1:
		return kvm_psci_1_x_call(vcpu, 1);
	case KVM_ARM_PSCI_1_0:
		return kvm_psci_1_x_call(vcpu, 0);
	case KVM_ARM_PSCI_0_2:
		return kvm_psci_0_2_call(vcpu);
	case KVM_ARM_PSCI_0_1:
		return kvm_psci_0_1_call(vcpu);
	default:
		return -EINVAL;
	}
}
