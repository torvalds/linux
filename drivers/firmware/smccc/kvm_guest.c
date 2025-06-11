// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) "smccc: KVM: " fmt

#include <linux/arm-smccc.h>
#include <linux/bitmap.h>
#include <linux/cache.h>
#include <linux/kernel.h>
#include <linux/memblock.h>
#include <linux/string.h>

#include <uapi/linux/psci.h>

#include <asm/hypervisor.h>

static DECLARE_BITMAP(__kvm_arm_hyp_services, ARM_SMCCC_KVM_NUM_FUNCS) __ro_after_init = { };

void __init kvm_init_hyp_services(void)
{
	uuid_t kvm_uuid = ARM_SMCCC_VENDOR_HYP_UID_KVM;
	struct arm_smccc_res res;
	u32 val[4];

	if (!arm_smccc_hypervisor_has_uuid(&kvm_uuid))
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

	kvm_arch_init_hyp_services();
}

bool kvm_arm_hyp_service_available(u32 func_id)
{
	if (func_id >= ARM_SMCCC_KVM_NUM_FUNCS)
		return false;

	return test_bit(func_id, __kvm_arm_hyp_services);
}
EXPORT_SYMBOL_GPL(kvm_arm_hyp_service_available);

#ifdef CONFIG_ARM64
void  __init kvm_arm_target_impl_cpu_init(void)
{
	int i;
	u32 ver;
	u64 max_cpus;
	struct arm_smccc_res res;
	struct target_impl_cpu *target;

	if (!kvm_arm_hyp_service_available(ARM_SMCCC_KVM_FUNC_DISCOVER_IMPL_VER) ||
	    !kvm_arm_hyp_service_available(ARM_SMCCC_KVM_FUNC_DISCOVER_IMPL_CPUS))
		return;

	arm_smccc_1_1_invoke(ARM_SMCCC_VENDOR_HYP_KVM_DISCOVER_IMPL_VER_FUNC_ID,
			     0, &res);
	if (res.a0 != SMCCC_RET_SUCCESS)
		return;

	/* Version info is in lower 32 bits and is in SMMCCC_VERSION format */
	ver = lower_32_bits(res.a1);
	if (PSCI_VERSION_MAJOR(ver) != 1) {
		pr_warn("Unsupported target CPU implementation version v%d.%d\n",
			PSCI_VERSION_MAJOR(ver), PSCI_VERSION_MINOR(ver));
		return;
	}

	if (!res.a2) {
		pr_warn("No target implementation CPUs specified\n");
		return;
	}

	max_cpus = res.a2;
	target = memblock_alloc(sizeof(*target) * max_cpus,  __alignof__(*target));
	if (!target) {
		pr_warn("Not enough memory for struct target_impl_cpu\n");
		return;
	}

	for (i = 0; i < max_cpus; i++) {
		arm_smccc_1_1_invoke(ARM_SMCCC_VENDOR_HYP_KVM_DISCOVER_IMPL_CPUS_FUNC_ID,
				     i, 0, 0, &res);
		if (res.a0 != SMCCC_RET_SUCCESS) {
			pr_warn("Discovering target implementation CPUs failed\n");
			goto mem_free;
		}
		target[i].midr = res.a1;
		target[i].revidr = res.a2;
		target[i].aidr = res.a3;
	}

	if (!cpu_errata_set_target_impl(max_cpus, target)) {
		pr_warn("Failed to set target implementation CPUs\n");
		goto mem_free;
	}

	pr_info("Number of target implementation CPUs is %lld\n", max_cpus);
	return;

mem_free:
	memblock_free(target, sizeof(*target) * max_cpus);
}
#endif
