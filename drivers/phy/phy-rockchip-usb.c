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
#include <linux/of_platform.h>
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
	int (*rk_usb_phy_power)(struct rockchip_usb_phy *phy, bool on);
};

static int rk32_usb_phy_power(struct rockchip_usb_phy *phy,
			      bool on)
{
	/* Power down usb phy analog blocks by set siddq 1 */
	bool siddq = !on;

	return regmap_write(phy->reg_base, phy->reg_offset,
			    SIDDQ_WRITE_ENA | (siddq ? SIDDQ_ON : SIDDQ_OFF));
}

static int rk33_usb_phy_power(struct rockchip_usb_phy *phy,
			      bool on)
{
	if (on)
		return regmap_write(phy->reg_base, phy->reg_offset, 0xffff0000);
	else
		return regmap_write(phy->reg_base, phy->reg_offset, 0xffff01d5);
}

static int rockchip_usb_phy_power_off(struct phy *_phy)
{
	struct rockchip_usb_phy *phy = phy_get_drvdata(_phy);
	int ret = 0;

	ret = phy->rk_usb_phy_power(phy, 0);
	if (ret)
		return ret;

	clk_disable_unprepare(phy->clk);
	if (ret)
		return ret;

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
	ret = phy->rk_usb_phy_power(phy, 1);
	if (ret)
		return ret;

	return 0;
}

static struct phy_ops ops = {
	.power_on	= rockchip_usb_phy_power_on,
	.power_off	= rockchip_usb_phy_power_off,
	.owner		= THIS_MODULE,
};

static const struct of_device_id rockchip_usb_phy_dt_ids[] = {
	{
		.compatible = "rockchip,rk3288-usb-phy",
		.data = (void *)rk32_usb_phy_power
	},
	{
		.compatible = "rockchip,rk3368-usb-phy",
		.data = (void *)rk33_usb_phy_power
	},
	{}
};

static int rockchip_usb_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_usb_phy *rk_phy;
	struct phy_provider *phy_provider;
	struct device_node *child;
	struct regmap *grf;
	const struct of_device_id *match;
	unsigned int reg_offset;
	int (*rk_usb_phy_power)(struct rockchip_usb_phy *phy, bool siddq);

	grf = syscon_regmap_lookup_by_phandle(dev->of_node, "rockchip,grf");
	if (IS_ERR(grf)) {
		dev_err(&pdev->dev, "Missing rockchip,grf property\n");
		return PTR_ERR(grf);
	}

	match = of_match_device(rockchip_usb_phy_dt_ids, &pdev->dev);
	if (match && match->data)
		rk_usb_phy_power = match->data;
	else
		return PTR_ERR(match);

	for_each_available_child_of_node(dev->of_node, child) {
		rk_phy = devm_kzalloc(dev, sizeof(*rk_phy), GFP_KERNEL);
		if (!rk_phy)
			return -ENOMEM;

		if (of_property_read_u32(child, "reg", &reg_offset)) {
			dev_err(dev, "missing reg property in node %s\n",
				child->name);
			return -EINVAL;
		}

		rk_phy->rk_usb_phy_power = rk_usb_phy_power;
		rk_phy->reg_offset = reg_offset;
		rk_phy->reg_base = grf;

		rk_phy->clk = of_clk_get_by_name(child, "phyclk");
		if (IS_ERR(rk_phy->clk))
			rk_phy->clk = NULL;

		rk_phy->phy = devm_phy_create(dev, child, &ops);
		if (IS_ERR(rk_phy->phy)) {
			dev_err(dev, "failed to create PHY\n");
			return PTR_ERR(rk_phy->phy);
		}
		phy_set_drvdata(rk_phy->phy, rk_phy);
	}

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider))
		return (long)phy_provider;
	else
		return 0;
}

MODULE_DEVICE_TABLE(of, rockchip_usb_phy_dt_ids);

static struct platform_driver rockchip_usb_driver = {
	.probe		= rockchip_usb_phy_probe,
	.driver		= {
		.name	= "rockchip-usb-phy",
		.owner	= THIS_MODULE,
		.of_match_table = rockchip_usb_phy_dt_ids,
	},
};

module_platform_driver(rockchip_usb_driver);

MODULE_AUTHOR("Yunzhi Li <lyz@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip USB 2.0 PHY driver");
MODULE_LICENSE("GPL v2");
