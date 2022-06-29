// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) "smccc: KVM: " fmt

#include <linux/arm-smccc.h>
#include <linux/bitmap.h>
#include <linux/cache.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include <asm/hypervisor.h>

static DECLARE_BITMAP(__kvm_arm_hyp_services, ARM_SMCCC_KVM_NUM_FUNCS) __ro_after_init = { };

void __init kvm_init_hyp_services(void)
{
	struct arm_smccc_res res;
	u32 val[4];

	if (arm_smccc_1_1_get_conduit() != SMCCC_CONDUIT_HVC)
		return;

	arm_smccc_1_1_invoke(ARM_SMCCC_VENDOR_HYP_CALL_UID_FUNC_ID, &res);
	if (res.a0 != ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_0 ||
	    res.a1 != ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_1 ||
	    res.a2 != ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_2 ||
	    res.a3 != ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_3)
		return;

	memset(&res, 0, sizeof(res));
	arm_smccc_1_1_invoke(ARM_SMCCC_VENDOR_HYP_KVM_FEATURES_FUNC_ID, &res);

	val[0] = lower_32_bits(res.a0);
	val[1] = lower_32_bits(res.a1);
	val[2] = lower_32_bits(res.a2);
	val[3] = lower_32_bits(res.a3);

	bitmap_from_arr32(__kvm_arm_hyp_services, val, ARM_SMCCC_KVM_NUM_FUNCS);

	pr_info("hypervisor services detected (0x%08lx 0x%08lx 0x%08lx 0x%08lx)\n",
		 res.a3, res.a2, res.a1, res.a0);
}

bool kvm_arm_hyp_service_available(u32 func_id)
{
	if (func_id >= ARM_SMCCC_KVM_NUM_FUNCS)
		return false;

	return test_bit(func_id, __kvm_arm_hyp_services);
}
EXPORT_SYMBOL_GPL(kvm_arm_hyp_service_available);
