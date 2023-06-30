// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2023, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>

#include <soc/tegra/fuse.h>
#include <soc/tegra/common.h>

#include "fuse.h"

#define FUSE_SKU_INFO	0x10

#define ERD_ERR_CONFIG 0x120c
#define ERD_MASK_INBAND_ERR 0x1

#define PMC_STRAPPING_OPT_A_RAM_CODE_SHIFT	4
#define PMC_STRAPPING_OPT_A_RAM_CODE_MASK_LONG	\
	(0xf << PMC_STRAPPING_OPT_A_RAM_CODE_SHIFT)
#define PMC_STRAPPING_OPT_A_RAM_CODE_MASK_SHORT	\
	(0x3 << PMC_STRAPPING_OPT_A_RAM_CODE_SHIFT)

static void __iomem *apbmisc_base;
static bool long_ram_code;
static u32 strapping;
static u32 chipid;

u32 tegra_read_chipid(void)
{
	WARN(!chipid, "Tegra APB MISC not yet available\n");

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

u8 tegra_get_platform(void)
{
	return (tegra_read_chipid() >> 20) & 0xf;
}

bool tegra_is_silicon(void)
{
	switch (tegra_get_chip_id()) {
	case TEGRA194:
	case TEGRA234:
	case TEGRA264:
		if (tegra_get_platform() == 0)
			return true;

		return false;
	}

	/*
	 * Chips prior to Tegra194 have a different way of determining whether
	 * they are silicon or not. Since we never supported simulation on the
	 * older Tegra chips, don't bother extracting the information and just
	 * report that we're running on silicon.
	 */
	return true;
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
EXPORT_SYMBOL_GPL(tegra_read_ram_code);

/*
 * The function sets ERD(Error Response Disable) bit.
 * This allows to mask inband errors and always send an
 * OKAY response from CBB to the master which caused error.
 */
int tegra194_miscreg_mask_serror(void)
{
	if (!apbmisc_base)
		return -EPROBE_DEFER;

	if (!of_machine_is_compatible("nvidia,tegra194")) {
		WARN(1, "Only supported for Tegra194 devices!\n");
		return -EOPNOTSUPP;
	}

	writel_relaxed(ERD_MASK_INBAND_ERR,
		       apbmisc_base + ERD_ERR_CONFIG);

	return 0;
}
EXPORT_SYMBOL(tegra194_miscreg_mask_serror);

static const struct of_device_id apbmisc_match[] __initconst = {
	{ .compatible = "nvidia,tegra20-apbmisc", },
	{ .compatible = "nvidia,tegra186-misc", },
	{ .compatible = "nvidia,tegra194-misc", },
	{ .compatible = "nvidia,tegra234-misc", },
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
	tegra_sku_info.platform = tegra_get_platform();
}

void __init tegra_init_apbmisc(void)
{
	void __iomem *strapping_base;
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
			goto put;
		}

		if (of_address_to_resource(np, 1, &straps) < 0) {
			pr_err("failed to get strapping options registers\n");
			goto put;
		}
	}

	apbmisc_base = ioremap(apbmisc.start, resource_size(&apbmisc));
	if (!apbmisc_base) {
		pr_err("failed to map APBMISC registers\n");
	} else {
		chipid = readl_relaxed(apbmisc_base + 4);
	}

	strapping_base = ioremap(straps.start, resource_size(&straps));
	if (!strapping_base) {
		pr_err("failed to map strapping options registers\n");
	} else {
		strapping = readl_relaxed(strapping_base);
		iounmap(strapping_base);
	}

	long_ram_code = of_property_read_bool(np, "nvidia,long-ram-code");

put:
	of_node_put(np);
}
