/*
 * Copyright (C) 2012 NVIDIA CORPORATION. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/of.h>

#include "iomap.h"

#define PMC_CTRL		0x0
#define PMC_CTRL_INTR_LOW	(1 << 17)

static inline u32 tegra_pmc_readl(u32 reg)
{
	return readl(IO_ADDRESS(TEGRA_PMC_BASE + reg));
}

static inline void tegra_pmc_writel(u32 val, u32 reg)
{
	writel(val, IO_ADDRESS(TEGRA_PMC_BASE + reg));
}

#ifdef CONFIG_OF
static const struct of_device_id matches[] __initconst = {
	{ .compatible = "nvidia,tegra20-pmc" },
	{ }
};
#endif

void __init tegra_pmc_init(void)
{
	/*
	 * For now, Harmony is the only board that uses the PMC, and it wants
	 * the signal inverted. Seaboard would too if it used the PMC.
	 * Hopefully by the time other boards want to use the PMC, everything
	 * will be device-tree, or they also want it inverted.
	 */
	bool invert_interrupt = true;
	u32 val;

#ifdef CONFIG_OF
	if (of_have_populated_dt()) {
		struct device_node *np;

		invert_interrupt = false;

		np = of_find_matching_node(NULL, matches);
		if (np) {
			if (of_find_property(np, "nvidia,invert-interrupt",
						NULL))
				invert_interrupt = true;
		}
	}
#endif

	val = tegra_pmc_readl(PMC_CTRL);
	if (invert_interrupt)
		val |= PMC_CTRL_INTR_LOW;
	else
		val &= ~PMC_CTRL_INTR_LOW;
	tegra_pmc_writel(val, PMC_CTRL);
}
