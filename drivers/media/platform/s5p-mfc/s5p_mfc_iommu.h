/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2015 Samsung Electronics Co.Ltd
 * Authors: Marek Szyprowski <m.szyprowski@samsung.com>
 */

#ifndef S5P_MFC_IOMMU_H_
#define S5P_MFC_IOMMU_H_

#if defined(CONFIG_EXYNOS_IOMMU)

static inline bool exynos_is_iommu_available(struct device *dev)
{
	return dev->archdata.iommu != NULL;
}

#else

static inline bool exynos_is_iommu_available(struct device *dev)
{
	return false;
}

#endif

#endif /* S5P_MFC_IOMMU_H_ */
