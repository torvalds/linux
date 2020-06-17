// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2019 Arm Ltd.

#include <linux/arm-smccc.h>
#include <linux/kvm_host.h>

#include <asm/kvm_emulate.h>

#include <kvm/arm_hypercalls.h>
#include <kvm/arm_psci.h>

int kvm_hvc_call_handler(struct kvm_vcpu *vcpu)
{
	u32 func_id = smccc_get_function(vcpu);
	long val = SMCCC_RET_NOT_SUPPORTED;
	u32 feature;
	gpa_t gpa;

	switch (func_id) {
	case ARM_SMCCC_VERSION_FUNC_ID:
		val = ARM_SMCCC_VERSION_1_1;
		break;
	case ARM_SMCCC_ARCH_FEATURES_FUNC_ID:
		feature = smccc_get_arg1(vcpu);
		switch (feature) {
		case ARM_SMCCC_ARCH_WORKAROUND_1:
			switch (kvm_arm_harden_branch_predictor()) {
			case KVM_BP_HARDEN_UNKNOWN:
				break;
			case KVM_BP_HARDEN_WA_NEEDED:
				val = SMCCC_RET_SUCCESS;
				break;
			case KVM_BP_HARDEN_NOT_REQUIRED:
				val = SMCCC_RET_NOT_REQUIRED;
				break;
			}
			break;
		case ARM_SMCCC_ARCH_WORKAROUND_2:
			switch (kvm_arm_have_ssbd()) {
			case KVM_SSBD_FORCE_DISABLE:
			case KVM_SSBD_UNKNOWN:
				break;
			case KVM_SSBD_KERNEL:
				val = SMCCC_RET_SUCCESS;
				break;
			case KVM_SSBD_FORCE_ENABLE:
			case KVM_SSBD_MITIGATED:
				val = SMCCC_RET_NOT_REQUIRED;
				break;
			}
			break;
		case ARM_SMCCC_HV_PV_TIME_FEATURES:
			val = SMCCC_RET_SUCCESS;
			break;
		}
		break;
	case ARM_SMCCC_HV_PV_TIME_FEATURES:
		val = kvm_hypercall_pv_features(vcpu);
		break;
	case ARM_SMCCC_HV_PV_TIME_ST:
		gpa = kvm_init_stolen_time(vcpu);
		if (gpa != GPA_INVALID)
			val = gpa;
		break;
	default:
		return kvm_psci_call(vcpu);
	}

	smccc_set_retval(vcpu, val, 0, 0, 0);
	return 1;
}
