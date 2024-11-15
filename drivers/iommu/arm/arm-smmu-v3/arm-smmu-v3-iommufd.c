// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES
 */

#include <uapi/linux/iommufd.h>

#include "arm-smmu-v3.h"

void *arm_smmu_hw_info(struct device *dev, u32 *length, u32 *type)
{
	struct arm_smmu_master *master = dev_iommu_priv_get(dev);
	struct iommu_hw_info_arm_smmuv3 *info;
	u32 __iomem *base_idr;
	unsigned int i;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	base_idr = master->smmu->base + ARM_SMMU_IDR0;
	for (i = 0; i <= 5; i++)
		info->idr[i] = readl_relaxed(base_idr + i);
	info->iidr = readl_relaxed(master->smmu->base + ARM_SMMU_IIDR);
	info->aidr = readl_relaxed(master->smmu->base + ARM_SMMU_AIDR);

	*length = sizeof(*info);
	*type = IOMMU_HW_INFO_TYPE_ARM_SMMUV3;

	return info;
}
