/*
 * Copyright (C) 2015 Samsung Electronics Co.Ltd
 * Authors: Marek Szyprowski <m.szyprowski@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
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
