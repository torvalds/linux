/*
 * Rockchip usb PHY driver
 *
 * Copyright (C) 2014 Yunzhi Li <lyz@rock-chips.com>
 * Copyright (C) 2014 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

/*
 * The higher 16-bit of this register is used for write protection
 * only if BIT(13 + 16) set to 1 the BIT(13) can be written.
 */
#define SIDDQ_WRITE_ENA	BIT(29)
#define SIDDQ_ON		BIT(13)
#define SIDDQ_OFF		(0 << 13)

struct rockchip_usb_phy {
	unsigned int	reg_offset;
	struct regmap	*reg_base;
	struct clk	*clk;
	struct phy	*phy;
};

static int rockchip_usb_phy_power(struct rockchip_usb_phy *phy,
					   bool siddq)
{
	return regmap_write(phy->reg_base, phy->reg_offset,
			    SIDDQ_WRITE_ENA | (siddq ? SIDDQ_ON : SIDDQ_OFF));
}

static int rockchip_usb_phy_power_off(struct phy *_phy)
{
	struct rockchip_usb_phy *phy = phy_get_drvdata(_phy);
	int ret = 0;

	/* Power down usb phy analog blocks by set siddq 1 */
	ret = rockchip_usb_phy_power(phy, 1);
	if (ret)
		return ret;

	clk_disable_unprepare(phy->clk);

	return 0;
}

static int rockchip_usb_phy_power_on(struct phy *_phy)
{
	struct rockchip_usb_phy *phy = phy_get_drvdata(_phy);
	int ret = 0;

	ret = clk_prepare_enable(phy->clk);
	if (ret)
		return ret;

	/* Power up usb phy analog blocks by set siddq 0 */
	ret = rockchip_usb_phy_power(phy, 0);
	if (ret) {
		clk_disable_unprepare(phy->clk);
		return ret;
	}

	return 0;
}

static const struct phy_ops ops = {
	.power_on	= rockchip_usb_phy_power_on,
	.power_off	= rockchip_usb_phy_power_off,
	.owner		= THIS_MODULE,
};

static int rockchip_usb_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_usb_phy *rk_phy;
	struct phy_provider *phy_provider;
	struct device_node *child;
	struct regmap *grf;
	unsigned int reg_offset;
	int err;

	grf = syscon_regmap_lookup_by_phandle(dev->of_node, "rockchip,grf");
	if (IS_ERR(grf)) {
		dev_err(&pdev->dev, "Missing rockchip,grf property\n");
		return PTR_ERR(grf);
	}

	for_each_available_child_of_node(dev->of_node, child) {
		rk_phy = devm_kzalloc(dev, sizeof(*rk_phy), GFP_KERNEL);
		if (!rk_phy) {
			err = -ENOMEM;
			goto put_child;
		}

		if (of_property_read_u32(child, "reg", &reg_offset)) {
			dev_err(dev, "missing reg property in node %s\n",
				child->name);
			err = -EINVAL;
			goto put_child;
		}

		rk_phy->reg_offset = reg_offset;
		rk_phy->reg_base = grf;

		rk_phy->clk = of_clk_get_by_name(child, "phyclk");
		if (IS_ERR(rk_phy->clk))
			rk_phy->clk = NULL;

		rk_phy->phy = devm_phy_create(dev, child, &ops);
		if (IS_ERR(rk_phy->phy)) {
			dev_err(dev, "failed to create PHY\n");
			err = PTR_ERR(rk_phy->phy);
			goto put_child;
		}
		phy_set_drvdata(rk_phy->phy, rk_phy);

		/* only power up usb phy when it use, so disable it when init*/
		err = rockchip_usb_phy_power(rk_phy, 1);
		if (err)
			goto put_child;
	}

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	return PTR_ERR_OR_ZERO(phy_provider);
put_child:
	of_node_put(child);
	return err;
}

static const struct of_device_id rockchip_usb_phy_dt_ids[] = {
	{ .compatible = "rockchip,rk3288-usb-phy" },
	{}
};

MODULE_DEVICE_TABLE(of, rockchip_usb_phy_dt_ids);

static struct platform_driver rockchip_usb_driver = {
	.probe		= rockchip_usb_phy_probe,
	.driver		= {
		.name	= "rockchip-usb-phy",
		.of_match_table = rockchip_usb_phy_dt_ids,
	},
};

module_platform_driver(rockchip_usb_driver);

MODULE_AUTHOR("Yunzhi Li <lyz@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip USB 2.0 PHY driver");
MODULE_LICENSE("GPL v2");
