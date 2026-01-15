/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 */

#ifndef AMD_IOMMUFD_H
#define AMD_IOMMUFD_H

#if IS_ENABLED(CONFIG_AMD_IOMMU_IOMMUFD)
void *amd_iommufd_hw_info(struct device *dev, u32 *length, u32 *type);
#else
#define amd_iommufd_hw_info NULL
#endif /* CONFIG_AMD_IOMMU_IOMMUFD */

#endif /* AMD_IOMMUFD_H */
