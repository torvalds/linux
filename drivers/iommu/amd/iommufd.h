/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 */

#ifndef AMD_IOMMUFD_H
#define AMD_IOMMUFD_H

#if IS_ENABLED(CONFIG_AMD_IOMMU_IOMMUFD)
void *amd_iommufd_hw_info(struct device *dev, u32 *length, enum iommu_hw_info_type *type);
size_t amd_iommufd_get_viommu_size(struct device *dev, enum iommu_viommu_type viommu_type);
int amd_iommufd_viommu_init(struct iommufd_viommu *viommu, struct iommu_domain *parent,
			    const struct iommu_user_data *user_data);
#else
#define amd_iommufd_hw_info NULL
#define amd_iommufd_viommu_init NULL
#define amd_iommufd_get_viommu_size NULL
#endif /* CONFIG_AMD_IOMMU_IOMMUFD */

#endif /* AMD_IOMMUFD_H */
