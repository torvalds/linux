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

static const struct iommufd_viommu_ops arm_vsmmu_ops = {
};

struct iommufd_viommu *arm_vsmmu_alloc(struct device *dev,
				       struct iommu_domain *parent,
				       struct iommufd_ctx *ictx,
				       unsigned int viommu_type)
{
	struct arm_smmu_device *smmu =
		iommu_get_iommu_dev(dev, struct arm_smmu_device, iommu);
	struct arm_smmu_master *master = dev_iommu_priv_get(dev);
	struct arm_smmu_domain *s2_parent = to_smmu_domain(parent);
	struct arm_vsmmu *vsmmu;

	if (viommu_type != IOMMU_VIOMMU_TYPE_ARM_SMMUV3)
		return ERR_PTR(-EOPNOTSUPP);

	if (!(smmu->features & ARM_SMMU_FEAT_NESTING))
		return ERR_PTR(-EOPNOTSUPP);

	if (s2_parent->smmu != master->smmu)
		return ERR_PTR(-EINVAL);

	/*
	 * Must support some way to prevent the VM from bypassing the cache
	 * because VFIO currently does not do any cache maintenance. canwbs
	 * indicates the device is fully coherent and no cache maintenance is
	 * ever required, even for PCI No-Snoop.
	 */
	if (!arm_smmu_master_canwbs(master))
		return ERR_PTR(-EOPNOTSUPP);

	vsmmu = iommufd_viommu_alloc(ictx, struct arm_vsmmu, core,
				     &arm_vsmmu_ops);
	if (IS_ERR(vsmmu))
		return ERR_CAST(vsmmu);

	vsmmu->smmu = smmu;
	vsmmu->s2_parent = s2_parent;
	/* FIXME Move VMID allocation from the S2 domain allocation to here */
	vsmmu->vmid = s2_parent->s2_cfg.vmid;

	return &vsmmu->core;
}
