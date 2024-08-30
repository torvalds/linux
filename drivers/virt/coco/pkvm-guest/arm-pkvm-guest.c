// SPDX-License-Identifier: GPL-2.0-only
/*
 * Support for the hypercall interface exposed to protected guests by
 * pKVM.
 *
 * Author: Will Deacon <will@kernel.org>
 * Copyright (C) 2024 Google LLC
 */

#include <linux/arm-smccc.h>
#include <linux/array_size.h>
#include <linux/mm.h>

#include <asm/hypervisor.h>

static size_t pkvm_granule;

void pkvm_init_hyp_services(void)
{
	int i;
	struct arm_smccc_res res;
	const u32 funcs[] = {
		ARM_SMCCC_KVM_FUNC_HYP_MEMINFO,
	};

	for (i = 0; i < ARRAY_SIZE(funcs); ++i) {
		if (!kvm_arm_hyp_service_available(funcs[i]))
			return;
	}

	arm_smccc_1_1_invoke(ARM_SMCCC_VENDOR_HYP_KVM_HYP_MEMINFO_FUNC_ID,
			     0, 0, 0, &res);
	if (res.a0 > PAGE_SIZE) /* Includes error codes */
		return;

	pkvm_granule = res.a0;
}
