/*
 * Copyright (C) 2012,2013 NVIDIA CORPORATION. All rights reserved.
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
#include <linux/of_address.h>

#define PMC_CTRL		0x0
#define PMC_CTRL_INTR_LOW	(1 << 17)

static void __iomem *tegra_pmc_base;
static bool tegra_pmc_invert_interrupt;

static inline u32 tegra_pmc_readl(u32 reg)
{
	return readl(tegra_pmc_base + reg);
}

static inline void tegra_pmc_writel(u32 val, u32 reg)
{
	writel(val, tegra_pmc_base + reg);
}

static const struct of_device_id matches[] __initconst = {
	{ .compatible = "nvidia,tegra114-pmc" },
	{ .compatible = "nvidia,tegra30-pmc" },
	{ .compatible = "nvidia,tegra20-pmc" },
	{ }
};

static void tegra_pmc_parse_dt(void)
{
	struct device_node *np;

	np = of_find_matching_node(NULL, matches);
	BUG_ON(!np);

	tegra_pmc_base = of_iomap(np, 0);

	tegra_pmc_invert_interrupt = of_property_read_bool(np,
				     "nvidia,invert-interrupt");
}

void __init tegra_pmc_init(void)
{
	u32 val;

	tegra_pmc_parse_dt();

	val = tegra_pmc_readl(PMC_CTRL);
	if (tegra_pmc_invert_interrupt)
		val |= PMC_CTRL_INTR_LOW;
	else
		val &= ~PMC_CTRL_INTR_LOW;
	tegra_pmc_writel(val, PMC_CTRL);
}
