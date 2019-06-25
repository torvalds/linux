// SPDX-License-Identifier: GPL-2.0-or-later
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
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include <soc/tegra/fuse.h>

#include "cpuidle.h"

void __init tegra_cpuidle_init(void)
{
	switch (tegra_get_chip_id()) {
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
	switch (tegra_get_chip_id()) {
	case TEGRA20:
		if (IS_ENABLED(CONFIG_ARCH_TEGRA_2x_SOC))
			tegra20_cpuidle_pcie_irqs_in_use();
		break;
	}
}
