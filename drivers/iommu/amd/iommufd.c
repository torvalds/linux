// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 */

#include <linux/iommu.h>

#include "iommufd.h"
#include "amd_iommu.h"
#include "amd_iommu_types.h"

static const struct iommufd_viommu_ops amd_viommu_ops;

void *amd_iommufd_hw_info(struct device *dev, u32 *length, enum iommu_hw_info_type *type)
{
	struct iommu_hw_info_amd *hwinfo;

	if (*type != IOMMU_HW_INFO_TYPE_DEFAULT &&
	    *type != IOMMU_HW_INFO_TYPE_AMD)
		return ERR_PTR(-EOPNOTSUPP);

	hwinfo = kzalloc(sizeof(*hwinfo), GFP_KERNEL);
	if (!hwinfo)
		return ERR_PTR(-ENOMEM);

	*length = sizeof(*hwinfo);
	*type = IOMMU_HW_INFO_TYPE_AMD;

	hwinfo->efr = amd_iommu_efr;
	hwinfo->efr2 = amd_iommu_efr2;

	return hwinfo;
}

size_t amd_iommufd_get_viommu_size(struct device *dev, enum iommu_viommu_type viommu_type)
{
	return VIOMMU_STRUCT_SIZE(struct amd_iommu_viommu, core);
}

int amd_iommufd_viommu_init(struct iommufd_viommu *viommu, struct iommu_domain *parent,
			    const struct iommu_user_data *user_data)
{
	unsigned long flags;
	struct protection_domain *pdom = to_pdomain(parent);
	struct amd_iommu_viommu *aviommu = container_of(viommu, struct amd_iommu_viommu, core);

	xa_init_flags(&aviommu->gdomid_array, XA_FLAGS_ALLOC1);
	aviommu->parent = pdom;

	viommu->ops = &amd_viommu_ops;

	spin_lock_irqsave(&pdom->lock, flags);
	list_add(&aviommu->pdom_list, &pdom->viommu_list);
	spin_unlock_irqrestore(&pdom->lock, flags);

	return 0;
}

static void amd_iommufd_viommu_destroy(struct iommufd_viommu *viommu)
{
	unsigned long flags;
	struct amd_iommu_viommu *aviommu = container_of(viommu, struct amd_iommu_viommu, core);
	struct protection_domain *pdom = aviommu->parent;

	spin_lock_irqsave(&pdom->lock, flags);
	list_del(&aviommu->pdom_list);
	spin_unlock_irqrestore(&pdom->lock, flags);
	xa_destroy(&aviommu->gdomid_array);
}

/*
 * See include/linux/iommufd.h
 * struct iommufd_viommu_ops - vIOMMU specific operations
 */
static const struct iommufd_viommu_ops amd_viommu_ops = {
	.destroy = amd_iommufd_viommu_destroy,
};
