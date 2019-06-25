/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * clk-dfll.h - prototypes and macros for the Tegra DFLL clocksource driver
 * Copyright (C) 2013-2019 NVIDIA Corporation.  All rights reserved.
 *
 * Aleksandr Frid <afrid@nvidia.com>
 * Paul Walmsley <pwalmsley@nvidia.com>
 */

#ifndef __DRIVERS_CLK_TEGRA_CLK_DFLL_H
#define __DRIVERS_CLK_TEGRA_CLK_DFLL_H

#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/types.h>

#include "cvb.h"

/**
 * struct tegra_dfll_soc_data - SoC-specific hooks/integration for the DFLL driver
 * @dev: struct device * that holds the OPP table for the DFLL
 * @max_freq: maximum frequency supported on this SoC
 * @cvb: CPU frequency table for this SoC
 * @alignment: parameters of the regulator step and offset
 * @init_clock_trimmers: callback to initialize clock trimmers
 * @set_clock_trimmers_high: callback to tune clock trimmers for high voltage
 * @set_clock_trimmers_low: callback to tune clock trimmers for low voltage
 */
struct tegra_dfll_soc_data {
	struct device *dev;
	unsigned long max_freq;
	const struct cvb_table *cvb;
	struct rail_alignment alignment;

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
