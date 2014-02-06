/*  Copyright (C) 2014 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Adopted from dwmac-sti.c
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_net.h>
#include <linux/of_platform.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/stmmac.h>

#define SYSMGR_EMACGRP_CTRL_PHYSEL_ENUM_GMII_MII 0x0
#define SYSMGR_EMACGRP_CTRL_PHYSEL_ENUM_RGMII 0x1
#define SYSMGR_EMACGRP_CTRL_PHYSEL_ENUM_RMII 0x2
#define SYSMGR_EMACGRP_CTRL_PHYSEL_WIDTH 2
#define SYSMGR_EMACGRP_CTRL_PHYSEL_MASK 0x00000003

struct socfpga_dwmac {
	int	interface;
	u32	reg_offset;
	struct	device *dev;
	struct regmap *sys_mgr_base_addr;
	struct	device_node *dwmac_np;
};

static int socfpga_dwmac_parse_data(struct socfpga_dwmac *dwmac, struct device *dev)
{
	struct device_node *np	= dev->of_node;
	struct device_node *stmmac_np;
	struct regmap *sys_mgr_base_addr;
	u32 reg_offset;
	int ret;

	stmmac_np = of_get_next_available_child(np, NULL);
	if (!stmmac_np) {
		dev_info(dev, "No dwmac node found\n");
		return -EINVAL;
	}

	if (!of_device_is_compatible(stmmac_np, "snps,dwmac")) {
		dev_info(dev, "dwmac node isn't compatible with snps,dwmac\n");
		return -EINVAL;
	}

	dwmac->interface = of_get_phy_mode(stmmac_np);
	of_node_put(stmmac_np);

	sys_mgr_base_addr = syscon_regmap_lookup_by_phandle(np, "altr,sysmgr-syscon");
	if (IS_ERR(sys_mgr_base_addr)) {
		dev_info(dev, "No sysmgr-syscon node found\n");
		return PTR_ERR(sys_mgr_base_addr);
	}

	ret = of_property_read_u32_index(np, "altr,sysmgr-syscon", 1, &reg_offset);
	if (ret) {
		dev_info(dev, "Could not reg_offset into sysmgr-syscon!\n");
		return -EINVAL;
	}

	dwmac->reg_offset = reg_offset;
	dwmac->sys_mgr_base_addr = sys_mgr_base_addr;
	dwmac->dwmac_np = stmmac_np;
	dwmac->dev = dev;

	return 0;
}

static int socfpga_dwmac_setup(struct socfpga_dwmac *dwmac)
{
	struct regmap *sys_mgr_base_addr = dwmac->sys_mgr_base_addr;
	int phymode = dwmac->interface;
	u32 reg_offset = dwmac->reg_offset;
	u32 ctrl, val, shift = 0;

	if (of_machine_is_compatible("altr,socfpga-vt"))
		return 0;

	switch (phymode) {
	case PHY_INTERFACE_MODE_RGMII:
		val = SYSMGR_EMACGRP_CTRL_PHYSEL_ENUM_RGMII;
		break;
	case PHY_INTERFACE_MODE_MII:
	case PHY_INTERFACE_MODE_GMII:
		val = SYSMGR_EMACGRP_CTRL_PHYSEL_ENUM_GMII_MII;
		break;
	default:
		dev_err(dwmac->dev, "bad phy mode %d\n", phymode);
		return -EINVAL;
	}

	regmap_read(sys_mgr_base_addr, reg_offset, &ctrl);
	ctrl &= ~(SYSMGR_EMACGRP_CTRL_PHYSEL_MASK << shift);
	ctrl |= val << shift;

	regmap_write(sys_mgr_base_addr, reg_offset, ctrl);
	return 0;
}

static int socfpga_dwmac_probe(struct platform_device *pdev)
{
	struct device		*dev = &pdev->dev;
	struct device_node	*node = dev->of_node;
	int			ret = -ENOMEM;
	struct socfpga_dwmac	*dwmac;

	dwmac = devm_kzalloc(dev, sizeof(*dwmac), GFP_KERNEL);
	if (!dwmac)
		return -ENOMEM;

	ret = socfpga_dwmac_parse_data(dwmac, dev);
	if (ret) {
		dev_err(dev, "Unable to parse OF data\n");
		return ret;
	}

	ret = socfpga_dwmac_setup(dwmac);
	if (ret) {
		dev_err(dev, "couldn't setup SoC glue (%d)\n", ret);
		return ret;
	}

	if (node) {
		ret = of_platform_populate(node, NULL, NULL, dev);
		if (ret) {
			dev_err(dev, "failed to add dwmac core\n");
			return ret;
		}
	} else {
		dev_err(dev, "no device node, failed to add dwmac core\n");
		return -ENODEV;
	}

	platform_set_drvdata(pdev, dwmac);

	return 0;
}

static int socfpga_dwmac_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id socfpga_dwmac_match[] = {
	{ .compatible = "altr,socfpga-stmmac" },
	{},
};
MODULE_DEVICE_TABLE(of, socfpga_dwmac_match);

static struct platform_driver socfpga_dwmac_driver = {
	.probe		= socfpga_dwmac_probe,
	.remove		= socfpga_dwmac_remove,
	.driver		= {
		.name	= "socfpga-dwmac",
		.of_match_table = of_match_ptr(socfpga_dwmac_match),
	},
};

module_platform_driver(socfpga_dwmac_driver);

MODULE_ALIAS("platform:socfpga-dwmac");
MODULE_AUTHOR("Dinh Nguyen <dinguyen@altera.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Altera SOCFPGA DWMAC Glue Layer");
