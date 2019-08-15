// SPDX-License-Identifier: GPL-2.0-only
// Miscellaneous Arm SMMU implementation and integration quirks
// Copyright (C) 2019 Arm Limited

#define pr_fmt(fmt) "arm-smmu: " fmt

#include "arm-smmu.h"


struct arm_smmu_device *arm_smmu_impl_init(struct arm_smmu_device *smmu)
{
	return smmu;
}
