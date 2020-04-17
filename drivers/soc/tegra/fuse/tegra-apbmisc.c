// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>

#include <soc/tegra/fuse.h>
#include <soc/tegra/common.h>

#include "fuse.h"

#define FUSE_SKU_INFO	0x10

#define PMC_STRAPPING_OPT_A_RAM_CODE_SHIFT	4
#define PMC_STRAPPING_OPT_A_RAM_CODE_MASK_LONG	\
	(0xf << PMC_STRAPPING_OPT_A_RAM_CODE_SHIFT)
#define PMC_STRAPPING_OPT_A_RAM_CODE_MASK_SHORT	\
	(0x3 << PMC_STRAPPING_OPT_A_RAM_CODE_SHIFT)

static bool long_ram_code;
static u32 strapping;
static u32 chipid;

u32 tegra_read_chipid(void)
{
	WARN(!chipid, "Tegra ABP MISC not yet available\n");

	return chipid;
}

u8 tegra_get_chip_id(void)
{
	return (tegra_read_chipid() >> 8) & 0xff;
}

u8 tegra_get_major_rev(void)
{
	return (tegra_read_chipid() >> 4) & 0xf;
}

u8 tegra_get_minor_rev(void)
{
	return (tegra_read_chipid() >> 16) & 0xf;
}

u32 tegra_read_straps(void)
{
	WARN(!chipid, "Tegra ABP MISC not yet available\n");

	return strapping;
}

u32 tegra_read_ram_code(void)
{
	u32 straps = tegra_read_straps();

	if (long_ram_code)
		straps &= PMC_STRAPPING_OPT_A_RAM_CODE_MASK_LONG;
	else
		straps &= PMC_STRAPPING_OPT_A_RAM_CODE_MASK_SHORT;

	return straps >> PMC_STRAPPING_OPT_A_RAM_CODE_SHIFT;
}

static const struct of_device_id apbmisc_match[] __initconst = {
	{ .compatible = "nvidia,tegra20-apbmisc", },
	{ .compatible = "nvidia,tegra186-misc", },
	{ .compatible = "nvidia,tegra194-misc", },
	{},
};

void __init tegra_init_revision(void)
{
	u8 chip_id, minor_rev;

	chip_id = tegra_get_chip_id();
	minor_rev = tegra_get_minor_rev();

	switch (minor_rev) {
	case 1:
		tegra_sku_info.revision = TEGRA_REVISION_A01;
		break;
	case 2:
		tegra_sku_info.revision = TEGRA_REVISION_A02;
		break;
	case 3:
		if (chip_id == TEGRA20 && (tegra_fuse_read_spare(18) ||
					   tegra_fuse_read_spare(19)))
			tegra_sku_info.revision = TEGRA_REVISION_A03p;
		else
			tegra_sku_info.revision = TEGRA_REVISION_A03;
		break;
	case 4:
		tegra_sku_info.revision = TEGRA_REVISION_A04;
		break;
	default:
		tegra_sku_info.revision = TEGRA_REVISION_UNKNOWN;
	}

	tegra_sku_info.sku_id = tegra_fuse_read_early(FUSE_SKU_INFO);
}

void __init tegra_init_apbmisc(void)
{
	void __iomem *apbmisc_base, *strapping_base;
	struct resource apbmisc, straps;
	struct device_node *np;

	np = of_find_matching_node(NULL, apbmisc_match);
	if (!np) {
		/*
		 * Fall back to legacy initialization for 32-bit ARM only. All
		 * 64-bit ARM device tree files for Tegra are required to have
		 * an APBMISC node.
		 *
		 * This is for backwards-compatibility with old device trees
		 * that didn't contain an APBMISC node.
		 */
		if (IS_ENABLED(CONFIG_ARM) && soc_is_tegra()) {
			/* APBMISC registers (chip revision, ...) */
			apbmisc.start = 0x70000800;
			apbmisc.end = 0x70000863;
			apbmisc.flags = IORESOURCE_MEM;

			/* strapping options */
			if (of_machine_is_compatible("nvidia,tegra124")) {
				straps.start = 0x7000e864;
				straps.end = 0x7000e867;
			} else {
				straps.start = 0x70000008;
				straps.end = 0x7000000b;
			}

			straps.flags = IORESOURCE_MEM;

			pr_warn("Using APBMISC region %pR\n", &apbmisc);
			pr_warn("Using strapping options registers %pR\n",
				&straps);
		} else {
			/*
			 * At this point we're not running on Tegra, so play
			 * nice with multi-platform kernels.
			 */
			return;
		}
	} else {
		/*
		 * Extract information from the device tree if we've found a
		 * matching node.
		 */
		if (of_address_to_resource(np, 0, &apbmisc) < 0) {
			pr_err("failed to get APBMISC registers\n");
			return;
		}

		if (of_address_to_resource(np, 1, &straps) < 0) {
			pr_err("failed to get strapping options registers\n");
			return;
		}
	}

	apbmisc_base = ioremap(apbmisc.start, resource_size(&apbmisc));
	if (!apbmisc_base) {
		pr_err("failed to map APBMISC registers\n");
	} else {
		chipid = readl_relaxed(apbmisc_base + 4);
		iounmap(apbmisc_base);
	}

	strapping_base = ioremap(straps.start, resource_size(&straps));
	if (!strapping_base) {
		pr_err("failed to map strapping options registers\n");
	} else {
		strapping = readl_relaxed(strapping_base);
		iounmap(strapping_base);
	}

	long_ram_code = of_property_read_bool(np, "nvidia,long-ram-code");
}
