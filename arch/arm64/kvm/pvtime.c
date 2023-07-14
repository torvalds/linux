// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2019 Arm Ltd.

#include <linux/arm-smccc.h>
#include <linux/kvm_host.h>
#include <linux/sched/stat.h>

#include <asm/kvm_mmu.h>
#include <asm/pvclock-abi.h>

#include <kvm/arm_hypercalls.h>

void kvm_update_stolen_time(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	u64 base = vcpu->arch.steal.base;
	u64 last_steal = vcpu->arch.steal.last_steal;
	u64 offset = offsetof(struct pvclock_vcpu_stolen_time, stolen_time);
	u64 steal = 0;
	int idx;

	if (base == INVALID_GPA)
		return;

	idx = srcu_read_lock(&kvm->srcu);
	if (!kvm_get_guest(kvm, base + offset, steal)) {
		steal = le64_to_cpu(steal);
		vcpu->arch.steal.last_steal = READ_ONCE(current->sched_info.run_delay);
		steal += vcpu->arch.steal.last_steal - last_steal;
		kvm_put_guest(kvm, base + offset, cpu_to_le64(steal));
	}
	srcu_read_unlock(&kvm->srcu, idx);
}

long kvm_hypercall_pv_features(struct kvm_vcpu *vcpu)
{
	u32 feature = smccc_get_arg1(vcpu);
	long val = SMCCC_RET_NOT_SUPPORTED;

	switch (feature) {
	case ARM_SMCCC_HV_PV_TIME_FEATURES:
	case ARM_SMCCC_HV_PV_TIME_ST:
		if (vcpu->arch.steal.base != INVALID_GPA)
			val = SMCCC_RET_SUCCESS;
		break;
	}

	return val;
}

gpa_t kvm_init_stolen_time(struct kvm_vcpu *vcpu)
{
	struct pvclock_vcpu_stolen_time init_values = {};
	struct kvm *kvm = vcpu->kvm;
	u64 base = vcpu->arch.steal.base;

	if (base == INVALID_GPA)
		return base;

	/*
	 * Start counting stolen time from the time the guest requests
	 * the feature enabled.
	 */
	vcpu->arch.steal.last_steal = current->sched_info.run_delay;
	kvm_write_guest_lock(kvm, base, &init_values, sizeof(init_values));

	return base;
}

bool kvm_arm_pvtime_supported(void)
{
	return !!sched_info_on();
}

int kvm_arm_pvtime_set_attr(struct kvm_vcpu *vcpu,
			    struct kvm_device_attr *attr)
{
	u64 __user *user = (u64 __user *)attr->addr;
	struct kvm *kvm = vcpu->kvm;
	u64 ipa;
	int ret = 0;
	int idx;

	if (!kvm_arm_pvtime_supported() ||
	    attr->attr != KVM_ARM_VCPU_PVTIME_IPA)
		return -ENXIO;

	if (get_user(ipa, user))
		return -EFAULT;
	if (!IS_ALIGNED(ipa, 64))
		return -EINVAL;
	if (vcpu->arch.steal.base != INVALID_GPA)
		return -EEXIST;

	/* Check the address is in a valid memslot */
	idx = srcu_read_lock(&kvm->srcu);
	if (kvm_is_error_hva(gfn_to_hva(kvm, ipa >> PAGE_SHIFT)))
		ret = -EINVAL;
	srcu_read_unlock(&kvm->srcu, idx);

	if (!ret)
		vcpu->arch.steal.base = ipa;

	return ret;
}

int kvm_arm_pvtime_get_attr(struct kvm_vcpu *vcpu,
			    struct kvm_device_attr *attr)
{
	u64 __user *user = (u64 __user *)attr->addr;
	u64 ipa;

	if (!kvm_arm_pvtime_supported() ||
	    attr->attr != KVM_ARM_VCPU_PVTIME_IPA)
		return -ENXIO;

	ipa = vcpu->arch.steal.base;

	if (put_user(ipa, user))
		return -EFAULT;
	return 0;
}

int kvm_arm_pvtime_has_attr(struct kvm_vcpu *vcpu,
			    struct kvm_device_attr *attr)
{
	switch (attr->attr) {
	case KVM_ARM_VCPU_PVTIME_IPA:
		if (kvm_arm_pvtime_supported())
			return 0;
	}
	return -ENXIO;
}
