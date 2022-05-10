// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2019 Arm Ltd.

#include <linux/arm-smccc.h>
#include <linux/kvm_host.h>

#include <asm/kvm_emulate.h>

#include <kvm/arm_hypercalls.h>
#include <kvm/arm_psci.h>

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
		cycles = systime_snapshot.cycles - vcpu_read_sys_reg(vcpu, CNTVOFF_EL2);
		break;
	case KVM_PTP_PHYS_COUNTER:
		cycles = systime_snapshot.cycles;
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

int kvm_hvc_call_handler(struct kvm_vcpu *vcpu)
{
	u32 func_id = smccc_get_function(vcpu);
	u64 val[4] = {SMCCC_RET_NOT_SUPPORTED};
	u32 feature;
	gpa_t gpa;

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
			val[0] = SMCCC_RET_SUCCESS;
			break;
		}
		break;
	case ARM_SMCCC_HV_PV_TIME_FEATURES:
		val[0] = kvm_hypercall_pv_features(vcpu);
		break;
	case ARM_SMCCC_HV_PV_TIME_ST:
		gpa = kvm_init_stolen_time(vcpu);
		if (gpa != GPA_INVALID)
			val[0] = gpa;
		break;
	case ARM_SMCCC_VENDOR_HYP_CALL_UID_FUNC_ID:
		val[0] = ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_0;
		val[1] = ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_1;
		val[2] = ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_2;
		val[3] = ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_3;
		break;
	case ARM_SMCCC_VENDOR_HYP_KVM_FEATURES_FUNC_ID:
		val[0] = BIT(ARM_SMCCC_KVM_FUNC_FEATURES);
		val[0] |= BIT(ARM_SMCCC_KVM_FUNC_PTP);
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

	smccc_set_retval(vcpu, val[0], val[1], val[2], val[3]);
	return 1;
}
