/*
 * Versatile OF physmap driver add-on
 *
 * Copyright (c) 2016, Linaro Limited
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */
#include <linux/export.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/mtd/map.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/bitops.h>
#include "physmap_of_versatile.h"

static struct regmap *syscon_regmap;

enum versatile_flashprot {
	INTEGRATOR_AP_FLASHPROT,
	INTEGRATOR_CP_FLASHPROT,
	VERSATILE_FLASHPROT,
	REALVIEW_FLASHPROT,
};

static const struct of_device_id syscon_match[] = {
	{
		.compatible = "arm,integrator-ap-syscon",
		.data = (void *)INTEGRATOR_AP_FLASHPROT,
	},
	{
		.compatible = "arm,integrator-cp-syscon",
		.data = (void *)INTEGRATOR_CP_FLASHPROT,
	},
	{
		.compatible = "arm,core-module-versatile",
		.data = (void *)VERSATILE_FLASHPROT,
	},
	{
		.compatible = "arm,realview-eb-syscon",
		.data = (void *)REALVIEW_FLASHPROT,
	},
	{
		.compatible = "arm,realview-pb1176-syscon",
		.data = (void *)REALVIEW_FLASHPROT,
	},
	{
		.compatible = "arm,realview-pb11mp-syscon",
		.data = (void *)REALVIEW_FLASHPROT,
	},
	{
		.compatible = "arm,realview-pba8-syscon",
		.data = (void *)REALVIEW_FLASHPROT,
	},
	{
		.compatible = "arm,realview-pbx-syscon",
		.data = (void *)REALVIEW_FLASHPROT,
	},
	{},
};

/*
 * Flash protection handling for the Integrator/AP
 */
#define INTEGRATOR_SC_CTRLS_OFFSET	0x08
#define INTEGRATOR_SC_CTRLC_OFFSET	0x0C
#define INTEGRATOR_SC_CTRL_FLVPPEN	BIT(1)
#define INTEGRATOR_SC_CTRL_FLWP		BIT(2)

#define INTEGRATOR_EBI_CSR1_OFFSET	0x04
/* The manual says bit 2, the code says bit 3, trust the code */
#define INTEGRATOR_EBI_WRITE_ENABLE	BIT(3)
#define INTEGRATOR_EBI_LOCK_OFFSET	0x20
#define INTEGRATOR_EBI_LOCK_VAL		0xA05F

static const struct of_device_id ebi_match[] = {
	{ .compatible = "arm,external-bus-interface"},
	{ },
};

static int ap_flash_init(struct platform_device *pdev)
{
	struct device_node *ebi;
	void __iomem *ebi_base;
	u32 val;
	int ret;

	/* Look up the EBI */
	ebi = of_find_matching_node(NULL, ebi_match);
	if (!ebi) {
		return -ENODEV;
	}
	ebi_base = of_iomap(ebi, 0);
	if (!ebi_base)
		return -ENODEV;

	/* Clear VPP and write protection bits */
	ret = regmap_write(syscon_regmap,
		INTEGRATOR_SC_CTRLC_OFFSET,
		INTEGRATOR_SC_CTRL_FLVPPEN | INTEGRATOR_SC_CTRL_FLWP);
	if (ret)
		dev_err(&pdev->dev, "error clearing Integrator VPP/WP\n");

	/* Unlock the EBI */
	writel(INTEGRATOR_EBI_LOCK_VAL, ebi_base + INTEGRATOR_EBI_LOCK_OFFSET);

	/* Enable write cycles on the EBI, CSR1 (flash) */
	val = readl(ebi_base + INTEGRATOR_EBI_CSR1_OFFSET);
	val |= INTEGRATOR_EBI_WRITE_ENABLE;
	writel(val, ebi_base + INTEGRATOR_EBI_CSR1_OFFSET);

	/* Lock the EBI again */
	writel(0, ebi_base + INTEGRATOR_EBI_LOCK_OFFSET);
	iounmap(ebi_base);

	return 0;
}

static void ap_flash_set_vpp(struct map_info *map, int on)
{
	int ret;

	if (on) {
		ret = regmap_write(syscon_regmap,
			INTEGRATOR_SC_CTRLS_OFFSET,
			INTEGRATOR_SC_CTRL_FLVPPEN | INTEGRATOR_SC_CTRL_FLWP);
		if (ret)
			pr_err("error enabling AP VPP\n");
	} else {
		ret = regmap_write(syscon_regmap,
			INTEGRATOR_SC_CTRLC_OFFSET,
			INTEGRATOR_SC_CTRL_FLVPPEN | INTEGRATOR_SC_CTRL_FLWP);
		if (ret)
			pr_err("error disabling AP VPP\n");
	}
}

/*
 * Flash protection handling for the Integrator/CP
 */

#define INTCP_FLASHPROG_OFFSET		0x04
#define CINTEGRATOR_FLVPPEN		BIT(0)
#define CINTEGRATOR_FLWREN		BIT(1)
#define CINTEGRATOR_FLMASK		BIT(0)|BIT(1)

static void cp_flash_set_vpp(struct map_info *map, int on)
{
	int ret;

	if (on) {
		ret = regmap_update_bits(syscon_regmap,
				INTCP_FLASHPROG_OFFSET,
				CINTEGRATOR_FLMASK,
				CINTEGRATOR_FLVPPEN | CINTEGRATOR_FLWREN);
		if (ret)
			pr_err("error setting CP VPP\n");
	} else {
		ret = regmap_update_bits(syscon_regmap,
				INTCP_FLASHPROG_OFFSET,
				CINTEGRATOR_FLMASK,
				0);
		if (ret)
			pr_err("error setting CP VPP\n");
	}
}

/*
 * Flash protection handling for the Versatiles and RealViews
 */

#define VERSATILE_SYS_FLASH_OFFSET            0x4C

static void versatile_flash_set_vpp(struct map_info *map, int on)
{
	int ret;

	ret = regmap_update_bits(syscon_regmap, VERSATILE_SYS_FLASH_OFFSET,
				 0x01, !!on);
	if (ret)
		pr_err("error setting Versatile VPP\n");
}

int of_flash_probe_versatile(struct platform_device *pdev,
			     struct device_node *np,
			     struct map_info *map)
{
	struct device_node *sysnp;
	const struct of_device_id *devid;
	struct regmap *rmap;
	static enum versatile_flashprot versatile_flashprot;
	int ret;

	/* Not all flash chips use this protection line */
	if (!of_device_is_compatible(np, "arm,versatile-flash"))
		return 0;

	/* For first chip probed, look up the syscon regmap */
	if (!syscon_regmap) {
		sysnp = of_find_matching_node_and_match(NULL,
							syscon_match,
							&devid);
		if (!sysnp)
			return -ENODEV;

		versatile_flashprot = (enum versatile_flashprot)devid->data;
		rmap = syscon_node_to_regmap(sysnp);
		if (IS_ERR(rmap))
			return PTR_ERR(rmap);

		syscon_regmap = rmap;
	}

	switch (versatile_flashprot) {
	case INTEGRATOR_AP_FLASHPROT:
		ret = ap_flash_init(pdev);
		if (ret)
			return ret;
		map->set_vpp = ap_flash_set_vpp;
		dev_info(&pdev->dev, "Integrator/AP flash protection\n");
		break;
	case INTEGRATOR_CP_FLASHPROT:
		map->set_vpp = cp_flash_set_vpp;
		dev_info(&pdev->dev, "Integrator/CP flash protection\n");
		break;
	case VERSATILE_FLASHPROT:
	case REALVIEW_FLASHPROT:
		map->set_vpp = versatile_flash_set_vpp;
		dev_info(&pdev->dev, "versatile/realview flash protection\n");
		break;
	default:
		dev_info(&pdev->dev, "device marked as Versatile flash "
			 "but no system controller was found\n");
		break;
	}

	return 0;
}
