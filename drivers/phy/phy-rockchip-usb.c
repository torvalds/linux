/*
 * Rockchip usb PHY driver
 *
 * Copyright (C) 2014 Roy Li <lyz@rock-chips.com>
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

#define ROCKCHIP_RK3288_UOC(n)	(0x320 + n * 0x14)

#define SIDDQ_MSK		(1 << (13 + 16))
#define SIDDQ_ON		(1 << 13)
#define SIDDQ_OFF		(0 << 13)

enum rk3288_phy_id {
	RK3288_OTG,
	RK3288_HOST0,
	RK3288_HOST1,
	RK3288_NUM_PHYS,
};

struct rockchip_usb_phy {
	struct regmap *reg_base;
	unsigned int reg_offset;
	struct clk *clk;
	struct phy *phy;
};

static int rockchip_usb_phy_power(struct rockchip_usb_phy *phy,
					   bool siddq)
{
	return regmap_write(phy->reg_base, phy->reg_offset,
			    SIDDQ_MSK | (siddq ? SIDDQ_ON : SIDDQ_OFF));
}

static int rockchip_usb_phy_power_off(struct phy *_phy)
{
	struct rockchip_usb_phy *phy = phy_get_drvdata(_phy);
	int ret = 0;

	/* Power down usb phy analog blocks by set siddq 1*/
	ret = rockchip_usb_phy_power(phy, 1);
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

	/* Power up usb phy analog blocks by set siddq 0*/
	ret = rockchip_usb_phy_power(phy, 0);
	if (ret)
		return ret;

	return 0;
}

static struct phy *rockchip_usb_phy_xlate(struct device *dev,
					struct of_phandle_args *args)
{
	struct rockchip_usb_phy *phy_array = dev_get_drvdata(dev);

	if (WARN_ON(args->args[0] == 0 || args->args[0] >= RK3288_NUM_PHYS))
		return ERR_PTR(-ENODEV);

	return (phy_array + args->args[0])->phy;
}

static struct phy_ops ops = {
	.power_on	= rockchip_usb_phy_power_on,
	.power_off	= rockchip_usb_phy_power_off,
	.owner		= THIS_MODULE,
};

static int rockchip_usb_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_usb_phy *rk_phy;
	struct rockchip_usb_phy *phy_array;
	struct phy_provider *phy_provider;
	struct regmap *grf;
	char clk_name[16];
	int i;

	grf = syscon_regmap_lookup_by_phandle(dev->of_node, "rockchip,grf");
	if (IS_ERR(grf)) {
		dev_err(&pdev->dev, "Missing rockchip,grf property\n");
		return PTR_ERR(grf);
	}

	phy_array = devm_kzalloc(dev, RK3288_NUM_PHYS * sizeof(*rk_phy),
				 GFP_KERNEL);
	if (!phy_array)
		return -ENOMEM;

	for (i = 0; i < RK3288_NUM_PHYS; i++) {
		rk_phy = &phy_array[i];

		rk_phy->reg_base = grf;

		rk_phy->reg_offset = ROCKCHIP_RK3288_UOC(i);

		snprintf(clk_name, sizeof(clk_name), "usbphy%d", i);
		rk_phy->clk = devm_clk_get(dev, clk_name);
		if (IS_ERR(rk_phy->clk)) {
			dev_warn(dev, "failed to get clock %s\n", clk_name);
			rk_phy->clk = NULL;
		}

		rk_phy->phy = devm_phy_create(dev, NULL, &ops, NULL);
		if (IS_ERR(rk_phy->phy)) {
			dev_err(dev, "failed to create PHY %d\n", i);
			return PTR_ERR(rk_phy->phy);
		}
		phy_set_drvdata(rk_phy->phy, rk_phy);
	}

	platform_set_drvdata(pdev, phy_array);

	phy_provider = devm_of_phy_provider_register(dev,
						     rockchip_usb_phy_xlate);
	return PTR_ERR_OR_ZERO(phy_provider);
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
		.owner	= THIS_MODULE,
		.of_match_table = rockchip_usb_phy_dt_ids,
	},
};

module_platform_driver(rockchip_usb_driver);

MODULE_AUTHOR("Roy Li <lyz@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip USB 2.0 PHY driver");
MODULE_LICENSE("GPL v2");
