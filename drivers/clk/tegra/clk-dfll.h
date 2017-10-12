/*
 * clk-dfll.h - prototypes and macros for the Tegra DFLL clocksource driver
 * Copyright (C) 2013 NVIDIA Corporation.  All rights reserved.
 *
 * Aleksandr Frid <afrid@nvidia.com>
 * Paul Walmsley <pwalmsley@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __DRIVERS_CLK_TEGRA_CLK_DFLL_H
#define __DRIVERS_CLK_TEGRA_CLK_DFLL_H

#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/types.h>

/**
 * struct tegra_dfll_soc_data - SoC-specific hooks/integration for the DFLL driver
 * @dev: struct device * that holds the OPP table for the DFLL
 * @max_freq: maximum frequency supported on this SoC
 * @cvb: CPU frequency table for this SoC
 * @init_clock_trimmers: callback to initialize clock trimmers
 * @set_clock_trimmers_high: callback to tune clock trimmers for high voltage
 * @set_clock_trimmers_low: callback to tune clock trimmers for low voltage
 */
struct tegra_dfll_soc_data {
	struct device *dev;
	unsigned long max_freq;
	const struct cvb_table *cvb;

	void (*init_clock_trimmers)(void);
	void (*set_clock_trimmers_high)(void);
	void (*set_clock_trimmers_low)(void);
};

int tegra_dfll_register(struct platform_device *pdev,
			struct tegra_dfll_soc_data *soc);
struct tegra_dfll_soc_data *tegra_dfll_unregister(struct platform_device *pdev);
int tegra_dfll_runtime_suspend(struct device *dev);
int tegra_dfll_runtime_resume(struct device *dev);

#endif /* __DRIVERS_CLK_TEGRA_CLK_DFLL_H */
