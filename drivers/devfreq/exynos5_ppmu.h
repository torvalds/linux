/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS5 - PPMU support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __DEVFREQ_EXYNOS5_PPMU_H
#define __DEVFREQ_EXYNOS5_PPMU_H __FILE__

enum exynos_ppmu_sets {
	PPMU_SET_DDR,
	PPMU_SET_RIGHT,
	PPMU_SET_CPU,
};

struct exynos5_ppmu_handle;

struct exynos5_ppmu_handle *exynos5_ppmu_get(void);
void exynos5_ppmu_put(struct exynos5_ppmu_handle *handle);
int exynos5_ppmu_get_busy(struct exynos5_ppmu_handle *handle,
	enum exynos_ppmu_sets filter);

#endif /* __DEVFREQ_EXYNOS5_PPMU_H */

