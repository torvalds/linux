/*
 * arch/arm/mach-tegra/cpuidle.c
 *
 * CPU idle driver for Tegra CPUs
 *
 * Copyright (c) 2010-2012, NVIDIA Corporation.
 * Copyright (c) 2011 Google, Inc.
 * Author: Colin Cross <ccross@android.com>
 *         Gary King <gking@nvidia.com>
 *
 * Rework for 3.3 by Peter De Schrijver <pdeschrijver@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include "fuse.h"
#include "cpuidle.h"

void __init tegra_cpuidle_init(void)
{
	switch (tegra_chip_id) {
	case TEGRA20:
		if (IS_ENABLED(CONFIG_ARCH_TEGRA_2x_SOC))
			tegra20_cpuidle_init();
		break;
	case TEGRA30:
		if (IS_ENABLED(CONFIG_ARCH_TEGRA_3x_SOC))
			tegra30_cpuidle_init();
		break;
	case TEGRA114:
	case TEGRA124:
		if (IS_ENABLED(CONFIG_ARCH_TEGRA_114_SOC) ||
		    IS_ENABLED(CONFIG_ARCH_TEGRA_124_SOC))
			tegra114_cpuidle_init();
		break;
	}
}

void tegra_cpuidle_pcie_irqs_in_use(void)
{
	switch (tegra_chip_id) {
	case TEGRA20:
		if (IS_ENABLED(CONFIG_ARCH_TEGRA_2x_SOC))
			tegra20_cpuidle_pcie_irqs_in_use();
		break;
	}
}
