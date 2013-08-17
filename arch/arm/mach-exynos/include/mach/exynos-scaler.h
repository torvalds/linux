/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Header file for exynos scaler support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __EXYNOS_SCALER_H
#define __EXYNOS_SCALER_H __FILE__

struct exynos_scaler_platdata {
	int use_pclk;
	unsigned int clk_rate;
};

extern struct exynos_scaler_platdata exynos5_scaler_pd;
extern struct exynos_scaler_platdata exynos5410_scaler_pd;
#endif
