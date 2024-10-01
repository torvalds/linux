/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _ARM_SMMU_QCOM_H
#define _ARM_SMMU_QCOM_H

struct qcom_smmu {
	struct arm_smmu_device smmu;
	const struct qcom_smmu_config *cfg;
	bool bypass_quirk;
	u8 bypass_cbndx;
	u32 stall_enabled;
};

#ifdef CONFIG_ARM_SMMU_QCOM_DEBUG
void qcom_smmu_tlb_sync_debug(struct arm_smmu_device *smmu);
const void *qcom_smmu_impl_data(struct arm_smmu_device *smmu);
#else
static inline void qcom_smmu_tlb_sync_debug(struct arm_smmu_device *smmu) { }
static inline const void *qcom_smmu_impl_data(struct arm_smmu_device *smmu)
{
	return NULL;
}
#endif

#endif /* _ARM_SMMU_QCOM_H */
