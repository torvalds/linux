// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2019 Arm Ltd.

#include <linux/arm-smccc.h>
#include <linux/kvm_host.h>

#include <asm/kvm_emulate.h>

#include <kvm/arm_hypercalls.h>
#include <kvm/arm_psci.h>

#define KVM_ARM_SMCCC_STD_FEATURES				\
	GENMASK(KVM_REG_ARM_STD_BMAP_BIT_COUNT - 1, 0)
#define KVM_ARM_SMCCC_STD_HYP_FEATURES				\
	GENMASK(KVM_REG_ARM_STD_HYP_BMAP_BIT_COUNT - 1, 0)
#define KVM_ARM_SMCCC_VENDOR_HYP_FEATURES			\
	GENMASK(KVM_REG_ARM_VENDOR_HYP_BMAP_BIT_COUNT - 1, 0)

static void kvm_ptp_get_time(struct kvm_vcpu *vcpu, u64 *val)
{
	struct system_time_snapshot systime_snapshot;
	u64 cycles = ~0UL;
	u32 feature;

	/*
	 * system time and counter value must captured at the same
	 * time to keep consistency and precision.
	 */
	ktime_get_snapshot(&systime_snapshot);

	/*
	 * This is only valid if the current clocksource is the
	 * architected counter, as this is the only one the guest
	 * can see.
	 */
	if (systime_snapshot.cs_id != CSID_ARM_ARCH_COUNTER)
		return;

	/*
	 * The guest selects one of the two reference counters
	 * (virtual or physical) with the first argument of the SMCCC
	 * call. In case the identifier is not supported, error out.
	 */
	feature = smccc_get_arg1(vcpu);
	switch (feature) {
	case KVM_PTP_VIRT_COUNTER:
		cycles = systime_snapshot.cycles - vcpu->kvm->arch.timer_data.voffset;
		break;
	case KVM_PTP_PHYS_COUNTER:
		cycles = systime_snapshot.cycles - vcpu->kvm->arch.timer_data.poffset;
		break;
	default:
		return;
	}

	/*
	 * This relies on the top bit of val[0] never being set for
	 * valid values of system time, because that is *really* far
	 * in the future (about 292 years from 1970, and at that stage
	 * nobody will give a damn about it).
	 */
	val[0] = upper_32_bits(systime_snapshot.real);
	val[1] = lower_32_bits(systime_snapshot.real);
	val[2] = upper_32_bits(cycles);
	val[3] = lower_32_bits(cycles);
}

static bool kvm_smccc_default_allowed(u32 func_id)
{
	switch (func_id) {
	/*
	 * List of function-ids that are not gated with the bitmapped
	 * feature firmware registers, and are to be allowed for
	 * servicing the call by default.
	 */
	case ARM_SMCCC_VERSION_FUNC_ID:
	case ARM_SMCCC_ARCH_FEATURES_FUNC_ID:
		return true;
	default:
		/* PSCI 0.2 and up is in the 0:0x1f range */
		if (ARM_SMCCC_OWNER_NUM(func_id) == ARM_SMCCC_OWNER_STANDARD &&
		    ARM_SMCCC_FUNC_NUM(func_id) <= 0x1f)
			return true;

		/*
		 * KVM's PSCI 0.1 doesn't comply with SMCCC, and has
		 * its own function-id base and range
		 */
		if (func_id >= KVM_PSCI_FN(0) && func_id <= KVM_PSCI_FN(3))
			return true;

		return false;
	}
}

static bool kvm_smccc_test_fw_bmap(struct kvm_vcpu *vcpu, u32 func_id)
{
	struct kvm_smccc_features *smccc_feat = &vcpu->kvm->arch.smccc_feat;

	switch (func_id) {
	case ARM_SMCCC_TRNG_VERSION:
	case ARM_SMCCC_TRNG_FEATURES:
	case ARM_SMCCC_TRNG_GET_UUID:
	case ARM_SMCCC_TRNG_RND32:
	case ARM_SMCCC_TRNG_RND64:
		return test_bit(KVM_REG_ARM_STD_BIT_TRNG_V1_0,
				&smccc_feat->std_bmap);
	case ARM_SMCCC_HV_PV_TIME_FEATURES:
	case ARM_SMCCC_HV_PV_TIME_ST:
		return test_bit(KVM_REG_ARM_STD_HYP_BIT_PV_TIME,
				&smccc_feat->std_hyp_bmap);
	case ARM_SMCCC_VENDOR_HYP_KVM_FEATURES_FUNC_ID:
	case ARM_SMCCC_VENDOR_HYP_CALL_UID_FUNC_ID:
		return test_bit(KVM_REG_ARM_VENDOR_HYP_BIT_FUNC_FEAT,
				&smccc_feat->vendor_hyp_bmap);
	case ARM_SMCCC_VENDOR_HYP_KVM_PTP_FUNC_ID:
		return test_bit(KVM_REG_ARM_VENDOR_HYP_BIT_PTP,
				&smccc_feat->vendor_hyp_bmap);
	default:
		return false;
	}
}

#define SMC32_ARCH_RANGE_BEGIN	ARM_SMCCC_VERSION_FUNC_ID
#define SMC32_ARCH_RANGE_END	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,		\
						   ARM_SMCCC_SMC_32,		\
						   0, ARM_SMCCC_FUNC_MASK)

#define SMC64_ARCH_RANGE_BEGIN	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,		\
						   ARM_SMCCC_SMC_64,		\
						   0, 0)
#define SMC64_ARCH_RANGE_END	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,		\
						   ARM_SMCCC_SMC_64,		\
						   0, ARM_SMCCC_FUNC_MASK)

static int kvm_smccc_filter_insert_reserved(struct kvm *kvm)
{
	int r;

	/*
	 * Prevent userspace from handling any SMCCC calls in the architecture
	 * range, avoiding the risk of misrepresenting Spectre mitigation status
	 * to the guest.
	 */
	r = mtree_insert_range(&kvm->arch.smccc_filter,
			       SMC32_ARCH_RANGE_BEGIN, SMC32_ARCH_RANGE_END,
			       xa_mk_value(KVM_SMCCC_FILTER_HANDLE),
			       GFP_KERNEL_ACCOUNT);
	if (r)
		goto out_destroy;

	r = mtree_insert_range(&kvm->arch.smccc_filter,
			       SMC64_ARCH_RANGE_BEGIN, SMC64_ARCH_RANGE_END,
			       xa_mk_value(KVM_SMCCC_FILTER_HANDLE),
			       GFP_KERNEL_ACCOUNT);
	if (r)
		goto out_destroy;

	return 0;
out_destroy:
	mtree_destroy(&kvm->arch.smccc_filter);
	return r;
}

static bool kvm_smccc_filter_configured(struct kvm *kvm)
{
	return !mtree_empty(&kvm->arch.smccc_filter);
}

static int kvm_smccc_set_filter(struct kvm *kvm, struct kvm_smccc_filter __user *uaddr)
{
	const void *zero_page = page_to_virt(ZERO_PAGE(0));
	struct kvm_smccc_filter filter;
	u32 start, end;
	int r;

	if (copy_from_user(&filter, uaddr, sizeof(filter)))
		return -EFAULT;

	if (memcmp(filter.pad, zero_page, sizeof(filter.pad)))
		return -EINVAL;

	start = filter.base;
	end = start + filter.nr_functions - 1;

	if (end < start || filter.action >= NR_SMCCC_FILTER_ACTIONS)
		return -EINVAL;

	mutex_lock(&kvm->arch.config_lock);

	if (kvm_vm_has_ran_once(kvm)) {
		r = -EBUSY;
		goto out_unlock;
	}

	if (!kvm_smccc_filter_configured(kvm)) {
		r = kvm_smccc_filter_insert_reserved(kvm);
		if (WARN_ON_ONCE(r))
			goto out_unlock;
	}

	r = mtree_insert_range(&kvm->arch.smccc_filter, start, end,
			       xa_mk_value(filter.action), GFP_KERNEL_ACCOUNT);
out_unlock:
	mutex_unlock(&kvm->arch.config_lock);
	return r;
}

static u8 kvm_smccc_filter_get_action(struct kvm *kvm, u32 func_id)
{
	unsigned long idx = func_id;
	void *val;

	if (!kvm_smccc_filter_configured(kvm))
		return KVM_SMCCC_FILTER_HANDLE;

	/*
	 * But where's the error handling, you say?
	 *
	 * mt_find() returns NULL if no entry was found, which just so happens
	 * to match KVM_SMCCC_FILTER_HANDLE.
	 */
	val = mt_find(&kvm->arch.smccc_filter, &idx, idx);
	return xa_to_value(val);
}

static u8 kvm_smccc_get_action(struct kvm_vcpu *vcpu, u32 func_id)
{
	/*
	 * Intervening actions in the SMCCC filter take precedence over the
	 * pseudo-firmware register bitmaps.
	 */
	u8 action = kvm_smccc_filter_get_action(vcpu->kvm, func_id);
	if (action != KVM_SMCCC_FILTER_HANDLE)
		return action;

	if (kvm_smccc_test_fw_bmap(vcpu, func_id) ||
	    kvm_smccc_default_allowed(func_id))
		return KVM_SMCCC_FILTER_HANDLE;

	return KVM_SMCCC_FILTER_DENY;
}

static void kvm_prepare_hypercall_exit(struct kvm_vcpu *vcpu, u32 func_id)
{
	u8 ec = ESR_ELx_EC(kvm_vcpu_get_esr(vcpu));
	struct kvm_run *run = vcpu->run;
	u64 flags = 0;

	if (ec == ESR_ELx_EC_SMC32 || ec == ESR_ELx_EC_SMC64)
		flags |= KVM_HYPERCALL_EXIT_SMC;

	if (!kvm_vcpu_trap_il_is32bit(vcpu))
		flags |= KVM_HYPERCALL_EXIT_16BIT;

	run->exit_reason = KVM_EXIT_HYPERCALL;
	run->hypercall = (typeof(run->hypercall)) {
		.nr	= func_id,
		.flags	= flags,
	};
}

int kvm_smccc_call_handler(struct kvm_vcpu *vcpu)
{
	struct kvm_smccc_features *smccc_feat = &vcpu->kvm->arch.smccc_feat;
	u32 func_id = smccc_get_function(vcpu);
	u64 val[4] = {SMCCC_RET_NOT_SUPPORTED};
	u32 feature;
	u8 action;
	gpa_t gpa;

	action = kvm_smccc_get_action(vcpu, func_id);
	switch (action) {
	case KVM_SMCCC_FILTER_HANDLE:
		break;
	case KVM_SMCCC_FILTER_DENY:
		goto out;
	case KVM_SMCCC_FILTER_FWD_TO_USER:
		kvm_prepare_hypercall_exit(vcpu, func_id);
		return 0;
	default:
		WARN_RATELIMIT(1, "Unhandled SMCCC filter action: %d\n", action);
		goto out;
	}

	switch (func_id) {
	case ARM_SMCCC_VERSION_FUNC_ID:
		val[0] = ARM_SMCCC_VERSION_1_1;
		break;
	case ARM_SMCCC_ARCH_FEATURES_FUNC_ID:
		feature = smccc_get_arg1(vcpu);
		switch (feature) {
		case ARM_SMCCC_ARCH_WORKAROUND_1:
			switch (arm64_get_spectre_v2_state()) {
			case SPECTRE_VULNERABLE:
				break;
			case SPECTRE_MITIGATED:
				val[0] = SMCCC_RET_SUCCESS;
				break;
			case SPECTRE_UNAFFECTED:
				val[0] = SMCCC_ARCH_WORKAROUND_RET_UNAFFECTED;
				break;
			}
			break;
		case ARM_SMCCC_ARCH_WORKAROUND_2:
			switch (arm64_get_spectre_v4_state()) {
			case SPECTRE_VULNERABLE:
				break;
			case SPECTRE_MITIGATED:
				/*
				 * SSBS everywhere: Indicate no firmware
				 * support, as the SSBS support will be
				 * indicated to the guest and the default is
				 * safe.
				 *
				 * Otherwise, expose a permanent mitigation
				 * to the guest, and hide SSBS so that the
				 * guest stays protected.
				 */
				if (cpus_have_final_cap(ARM64_SSBS))
					break;
				fallthrough;
			case SPECTRE_UNAFFECTED:
				val[0] = SMCCC_RET_NOT_REQUIRED;
				break;
			}
			break;
		case ARM_SMCCC_ARCH_WORKAROUND_3:
			switch (arm64_get_spectre_bhb_state()) {
			case SPECTRE_VULNERABLE:
				break;
			case SPECTRE_MITIGATED:
				val[0] = SMCCC_RET_SUCCESS;
				break;
			case SPECTRE_UNAFFECTED:
				val[0] = SMCCC_ARCH_WORKAROUND_RET_UNAFFECTED;
				break;
			}
			break;
		case ARM_SMCCC_HV_PV_TIME_FEATURES:
			if (test_bit(KVM_REG_ARM_STD_HYP_BIT_PV_TIME,
				     &smccc_feat->std_hyp_bmap))
				val[0] = SMCCC_RET_SUCCESS;
			break;
		}
		break;
	case ARM_SMCCC_HV_PV_TIME_FEATURES:
		val[0] = kvm_hypercall_pv_features(vcpu);
		break;
	case ARM_SMCCC_HV_PV_TIME_ST:
		gpa = kvm_init_stolen_time(vcpu);
		if (gpa != INVALID_GPA)
			val[0] = gpa;
		break;
	case ARM_SMCCC_VENDOR_HYP_CALL_UID_FUNC_ID:
		val[0] = ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_0;
		val[1] = ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_1;
		val[2] = ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_2;
		val[3] = ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_3;
		break;
	case ARM_SMCCC_VENDOR_HYP_KVM_FEATURES_FUNC_ID:
		val[0] = smccc_feat->vendor_hyp_bmap;
		break;
	case ARM_SMCCC_VENDOR_HYP_KVM_PTP_FUNC_ID:
		kvm_ptp_get_time(vcpu, val);
		break;
	case ARM_SMCCC_TRNG_VERSION:
	case ARM_SMCCC_TRNG_FEATURES:
	case ARM_SMCCC_TRNG_GET_UUID:
	case ARM_SMCCC_TRNG_RND32:
	case ARM_SMCCC_TRNG_RND64:
		return kvm_trng_call(vcpu);
	default:
		return kvm_psci_call(vcpu);
	}

out:
	smccc_set_retval(vcpu, val[0], val[1], val[2], val[3]);
	return 1;
}

static const u64 kvm_arm_fw_reg_ids[] = {
	KVM_REG_ARM_PSCI_VERSION,
	KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_1,
	KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_2,
	KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_3,
	KVM_REG_ARM_STD_BMAP,
	KVM_REG_ARM_STD_HYP_BMAP,
	KVM_REG_ARM_VENDOR_HYP_BMAP,
};

void kvm_arm_init_hypercalls(struct kvm *kvm)
{
	struct kvm_smccc_features *smccc_feat = &kvm->arch.smccc_feat;

	smccc_feat->std_bmap = KVM_ARM_SMCCC_STD_FEATURES;
	smccc_feat->std_hyp_bmap = KVM_ARM_SMCCC_STD_HYP_FEATURES;
	smccc_feat->vendor_hyp_bmap = KVM_ARM_SMCCC_VENDOR_HYP_FEATURES;

	mt_init(&kvm->arch.smccc_filter);
}

void kvm_arm_teardown_hypercalls(struct kvm *kvm)
{
	mtree_destroy(&kvm->arch.smccc_filter);
}

int kvm_arm_get_fw_num_regs(struct kvm_vcpu *vcpu)
{
	return ARRAY_SIZE(kvm_arm_fw_reg_ids);
}

int kvm_arm_copy_fw_reg_indices(struct kvm_vcpu *vcpu, u64 __user *uindices)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(kvm_arm_fw_reg_ids); i++) {
		if (put_user(kvm_arm_fw_reg_ids[i], uindices++))
			return -EFAULT;
	}

	return 0;
}

#define KVM_REG_FEATURE_LEVEL_MASK	GENMASK(3, 0)

/*
 * Convert the workaround level into an easy-to-compare number, where higher
 * values mean better protection.
 */
static int get_kernel_wa_level(u64 regid)
{
	switch (regid) {
	case KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_1:
		switch (arm64_get_spectre_v2_state()) {
		case SPECTRE_VULNERABLE:
			return KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_1_NOT_AVAIL;
		case SPECTRE_MITIGATED:
			return KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_1_AVAIL;
		case SPECTRE_UNAFFECTED:
			return KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_1_NOT_REQUIRED;
		}
		return KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_1_NOT_AVAIL;
	case KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_2:
		switch (arm64_get_spectre_v4_state()) {
		case SPECTRE_MITIGATED:
			/*
			 * As for the hypercall discovery, we pretend we
			 * don't have any FW mitigation if SSBS is there at
			 * all times.
			 */
			if (cpus_have_final_cap(ARM64_SSBS))
				return KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_2_NOT_AVAIL;
			fallthrough;
		case SPECTRE_UNAFFECTED:
			return KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_2_NOT_REQUIRED;
		case SPECTRE_VULNERABLE:
			return KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_2_NOT_AVAIL;
		}
		break;
	case KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_3:
		switch (arm64_get_spectre_bhb_state()) {
		case SPECTRE_VULNERABLE:
			return KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_3_NOT_AVAIL;
		case SPECTRE_MITIGATED:
			return KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_3_AVAIL;
		case SPECTRE_UNAFFECTED:
			return KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_3_NOT_REQUIRED;
		}
		return KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_3_NOT_AVAIL;
	}

	return -EINVAL;
}

int kvm_arm_get_fw_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	struct kvm_smccc_features *smccc_feat = &vcpu->kvm->arch.smccc_feat;
	void __user *uaddr = (void __user *)(long)reg->addr;
	u64 val;

	switch (reg->id) {
	case KVM_REG_ARM_PSCI_VERSION:
		val = kvm_psci_version(vcpu);
		break;
	case KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_1:
	case KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_2:
	case KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_3:
		val = get_kernel_wa_level(reg->id) & KVM_REG_FEATURE_LEVEL_MASK;
		break;
	case KVM_REG_ARM_STD_BMAP:
		val = READ_ONCE(smccc_feat->std_bmap);
		break;
	case KVM_REG_ARM_STD_HYP_BMAP:
		val = READ_ONCE(smccc_feat->std_hyp_bmap);
		break;
	case KVM_REG_ARM_VENDOR_HYP_BMAP:
		val = READ_ONCE(smccc_feat->vendor_hyp_bmap);
		break;
	default:
		return -ENOENT;
	}

	if (copy_to_user(uaddr, &val, KVM_REG_SIZE(reg->id)))
		return -EFAULT;

	return 0;
}

static int kvm_arm_set_fw_reg_bmap(struct kvm_vcpu *vcpu, u64 reg_id, u64 val)
{
	int ret = 0;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_smccc_features *smccc_feat = &kvm->arch.smccc_feat;
	unsigned long *fw_reg_bmap, fw_reg_features;

	switch (reg_id) {
	case KVM_REG_ARM_STD_BMAP:
		fw_reg_bmap = &smccc_feat->std_bmap;
		fw_reg_features = KVM_ARM_SMCCC_STD_FEATURES;
		break;
	case KVM_REG_ARM_STD_HYP_BMAP:
		fw_reg_bmap = &smccc_feat->std_hyp_bmap;
		fw_reg_features = KVM_ARM_SMCCC_STD_HYP_FEATURES;
		break;
	case KVM_REG_ARM_VENDOR_HYP_BMAP:
		fw_reg_bmap = &smccc_feat->vendor_hyp_bmap;
		fw_reg_features = KVM_ARM_SMCCC_VENDOR_HYP_FEATURES;
		break;
	default:
		return -ENOENT;
	}

	/* Check for unsupported bit */
	if (val & ~fw_reg_features)
		return -EINVAL;

	mutex_lock(&kvm->arch.config_lock);

	if (kvm_vm_has_ran_once(kvm) && val != *fw_reg_bmap) {
		ret = -EBUSY;
		goto out;
	}

	WRITE_ONCE(*fw_reg_bmap, val);
out:
	mutex_unlock(&kvm->arch.config_lock);
	return ret;
}

int kvm_arm_set_fw_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	void __user *uaddr = (void __user *)(long)reg->addr;
	u64 val;
	int wa_level;

	if (KVM_REG_SIZE(reg->id) != sizeof(val))
		return -ENOENT;
	if (copy_from_user(&val, uaddr, KVM_REG_SIZE(reg->id)))
		return -EFAULT;

	switch (reg->id) {
	case KVM_REG_ARM_PSCI_VERSION:
	{
		bool wants_02;

		wants_02 = vcpu_has_feature(vcpu, KVM_ARM_VCPU_PSCI_0_2);

		switch (val) {
		case KVM_ARM_PSCI_0_1:
			if (wants_02)
				return -EINVAL;
			vcpu->kvm->arch.psci_version = val;
			return 0;
		case KVM_ARM_PSCI_0_2:
		case KVM_ARM_PSCI_1_0:
		case KVM_ARM_PSCI_1_1:
			if (!wants_02)
				return -EINVAL;
			vcpu->kvm->arch.psci_version = val;
			return 0;
		}
		break;
	}

	case KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_1:
	case KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_3:
		if (val & ~KVM_REG_FEATURE_LEVEL_MASK)
			return -EINVAL;

		if (get_kernel_wa_level(reg->id) < val)
			return -EINVAL;

		return 0;

	case KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_2:
		if (val & ~(KVM_REG_FEATURE_LEVEL_MASK |
			    KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_2_ENABLED))
			return -EINVAL;

		/* The enabled bit must not be set unless the level is AVAIL. */
		if ((val & KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_2_ENABLED) &&
		    (val & KVM_REG_FEATURE_LEVEL_MASK) != KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_2_AVAIL)
			return -EINVAL;

		/*
		 * Map all the possible incoming states to the only two we
		 * really want to deal with.
		 */
		switch (val & KVM_REG_FEATURE_LEVEL_MASK) {
		case KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_2_NOT_AVAIL:
		case KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_2_UNKNOWN:
			wa_level = KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_2_NOT_AVAIL;
			break;
		case KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_2_AVAIL:
		case KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_2_NOT_REQUIRED:
			wa_level = KVM_REG_ARM_SMCCC_ARCH_WORKAROUND_2_NOT_REQUIRED;
			break;
		default:
			return -EINVAL;
		}

		/*
		 * We can deal with NOT_AVAIL on NOT_REQUIRED, but not the
		 * other way around.
		 */
		if (get_kernel_wa_level(reg->id) < wa_level)
			return -EINVAL;

		return 0;
	case KVM_REG_ARM_STD_BMAP:
	case KVM_REG_ARM_STD_HYP_BMAP:
	case KVM_REG_ARM_VENDOR_HYP_BMAP:
		return kvm_arm_set_fw_reg_bmap(vcpu, reg->id, val);
	default:
		return -ENOENT;
	}

	return -EINVAL;
}

int kvm_vm_smccc_has_attr(struct kvm *kvm, struct kvm_device_attr *attr)
{
	switch (attr->attr) {
	case KVM_ARM_VM_SMCCC_FILTER:
		return 0;
	default:
		return -ENXIO;
	}
}

int kvm_vm_smccc_set_attr(struct kvm *kvm, struct kvm_device_attr *attr)
{
	void __user *uaddr = (void __user *)attr->addr;

	switch (attr->attr) {
	case KVM_ARM_VM_SMCCC_FILTER:
		return kvm_smccc_set_filter(kvm, uaddr);
	default:
		return -ENXIO;
	}
}
