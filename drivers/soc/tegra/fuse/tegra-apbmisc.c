/*
 * Copyright (c) 2014, NVIDIA CORPORATION.  All rights reserved.
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
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>

#include <soc/tegra/fuse.h>

#include "fuse.h"

#define APBMISC_BASE	0x70000800
#define APBMISC_SIZE	0x64
#define FUSE_SKU_INFO	0x10

static void __iomem *apbmisc_base;
static void __iomem *strapping_base;

u32 tegra_read_chipid(void)
{
	return readl_relaxed(apbmisc_base + 4);
}

u8 tegra_get_chip_id(void)
{
	if (!apbmisc_base) {
		WARN(1, "Tegra Chip ID not yet available\n");
		return 0;
	}

	return (tegra_read_chipid() >> 8) & 0xff;
}

u32 tegra_read_straps(void)
{
	if (strapping_base)
		return readl_relaxed(strapping_base);
	else
		return 0;
}

static const struct of_device_id apbmisc_match[] __initconst = {
	{ .compatible = "nvidia,tegra20-apbmisc", },
	{},
};

void __init tegra_init_revision(void)
{
	u32 id, chip_id, minor_rev;
	int rev;

	id = tegra_read_chipid();
	chip_id = (id >> 8) & 0xff;
	minor_rev = (id >> 16) & 0xf;

	switch (minor_rev) {
	case 1:
		rev = TEGRA_REVISION_A01;
		break;
	case 2:
		rev = TEGRA_REVISION_A02;
		break;
	case 3:
		if (chip_id == TEGRA20 && (tegra20_spare_fuse_early(18) ||
					   tegra20_spare_fuse_early(19)))
			rev = TEGRA_REVISION_A03p;
		else
			rev = TEGRA_REVISION_A03;
		break;
	case 4:
		rev = TEGRA_REVISION_A04;
		break;
	default:
		rev = TEGRA_REVISION_UNKNOWN;
	}

	tegra_sku_info.revision = rev;

	if (chip_id == TEGRA20)
		tegra_sku_info.sku_id = tegra20_fuse_early(FUSE_SKU_INFO);
	else
		tegra_sku_info.sku_id = tegra30_fuse_readl(FUSE_SKU_INFO);
}

void __init tegra_init_apbmisc(void)
{
	struct device_node *np;

	np = of_find_matching_node(NULL, apbmisc_match);
	apbmisc_base = of_iomap(np, 0);
	if (!apbmisc_base) {
		pr_warn("ioremap tegra apbmisc failed. using %08x instead\n",
			APBMISC_BASE);
		apbmisc_base = ioremap(APBMISC_BASE, APBMISC_SIZE);
	}

	strapping_base = of_iomap(np, 1);
	if (!strapping_base)
		pr_err("ioremap tegra strapping_base failed\n");
}
