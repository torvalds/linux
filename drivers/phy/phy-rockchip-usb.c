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
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/gpio/consumer.h>
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
#include <linux/delay.h>

/*
 * The higher 16-bit of this register is used for write protection
 * only if BIT(13 + 16) set to 1 the BIT(13) can be written.
 */
#define SIDDQ_WRITE_ENA	BIT(29)
#define SIDDQ_ON		BIT(13)
#define SIDDQ_OFF		(0 << 13)

#define USB2_PHY_WRITE_ENA	(0xffff << 16)
#define USB2_PHY_SUSPEND	(0x5 << 0 | 0xd << 4 | 0x1 << 8)
#define USB2_PHY_RESUME	(0)

#define UTMI_SEL_GRF_WR_ENA	(0x3 << 16)
#define UTMI_SEL_GRF_SUSPEND	(0x1 << 0)
#define UTMI_SEL_GRF_RESUME	(0x2 << 0)

struct rockchip_usb_phys {
	int reg;
	const char *pll_name;
};

struct rockchip_usb_phy_pdata {
	struct rockchip_usb_phys *phys;
	unsigned int phy_pw_on;
	unsigned int phy_pw_off;
	bool siddq_ctl;
};

struct rockchip_usb_phy_base {
	struct device *dev;
	struct regmap *reg_base;
	struct gpio_desc *vbus_drv_gpio;
	const struct rockchip_usb_phy_pdata *pdata;
};

struct rockchip_usb_phy {
	struct rockchip_usb_phy_base *base;
	struct device_node *np;
	unsigned int	reg_offset;
	struct clk	*clk;
	struct clk      *clk480m;
	struct clk_hw	clk480m_hw;
	struct phy	*phy;
	struct reset_control *reset;
};

static int rockchip_usb_phy_power(struct rockchip_usb_phy *phy,
					   bool off)
{
	unsigned int val;

	val = !off ? phy->base->pdata->phy_pw_on : phy->base->pdata->phy_pw_off;
	return regmap_write(phy->base->reg_base, phy->reg_offset, val);
}

static unsigned long rockchip_usb_phy480m_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	return 480000000;
}

static void rockchip_usb_phy480m_disable(struct clk_hw *hw)
{
	struct rockchip_usb_phy *phy = container_of(hw,
						    struct rockchip_usb_phy,
						    clk480m_hw);

	/* Power down usb phy analog blocks by set siddq 1 */
	if (phy->base->pdata->siddq_ctl)
		rockchip_usb_phy_power(phy, 1);
}

static int rockchip_usb_phy480m_enable(struct clk_hw *hw)
{
	int ret = 0;
	struct rockchip_usb_phy *phy = container_of(hw,
						    struct rockchip_usb_phy,
						    clk480m_hw);

	/* Power up usb phy analog blocks by set siddq 0 */
	if (phy->base->pdata->siddq_ctl)
		ret = rockchip_usb_phy_power(phy, 0);

	return ret;
}

static int rockchip_usb_phy480m_is_enabled(struct clk_hw *hw)
{
	struct rockchip_usb_phy *phy = container_of(hw,
						    struct rockchip_usb_phy,
						    clk480m_hw);
	int ret = 1;
	u32 val;

	if (phy->base->pdata->siddq_ctl) {
		ret = regmap_read(phy->base->reg_base, phy->reg_offset, &val);
		if (ret < 0)
			return ret;

		ret = (val & SIDDQ_ON) ? 0 : 1;
	}

	return ret;
}

static const struct clk_ops rockchip_usb_phy480m_ops = {
	.enable = rockchip_usb_phy480m_enable,
	.disable = rockchip_usb_phy480m_disable,
	.is_enabled = rockchip_usb_phy480m_is_enabled,
	.recalc_rate = rockchip_usb_phy480m_recalc_rate,
};

static int rockchip_usb_phy_power_off(struct phy *_phy)
{
	int ret = 0;
	struct rockchip_usb_phy *phy = phy_get_drvdata(_phy);

	if (!phy->base->pdata->siddq_ctl) {
		ret = rockchip_usb_phy_power(phy, 1);
		if (ret)
			return ret;
	}

	clk_disable_unprepare(phy->clk480m);
	return 0;
}

static int rockchip_usb_phy_power_on(struct phy *_phy)
{
	int ret = 0;
	struct rockchip_usb_phy *phy = phy_get_drvdata(_phy);

	ret = clk_prepare_enable(phy->clk480m);
	if (ret)
		return ret;

	if (!phy->base->pdata->siddq_ctl)
		ret = rockchip_usb_phy_power(phy, 0);

	return ret;
}

static int rockchip_usb_phy_reset(struct phy *_phy)
{
	struct rockchip_usb_phy *phy = phy_get_drvdata(_phy);

	if (phy->reset) {
		reset_control_assert(phy->reset);
		udelay(10);
		reset_control_deassert(phy->reset);
	}

	return 0;
}

static const struct phy_ops ops = {
	.power_on	= rockchip_usb_phy_power_on,
	.power_off	= rockchip_usb_phy_power_off,
	.reset		= rockchip_usb_phy_reset,
	.owner		= THIS_MODULE,
};

static void rockchip_usb_phy_action(void *data)
{
	struct rockchip_usb_phy *rk_phy = data;

	of_clk_del_provider(rk_phy->np);
	clk_unregister(rk_phy->clk480m);

	if (rk_phy->clk)
		clk_put(rk_phy->clk);
}

static int rockchip_usb_phy_init(struct rockchip_usb_phy_base *base,
				 struct device_node *child)
{
	struct rockchip_usb_phy *rk_phy;
	unsigned int reg_offset;
	const char *clk_name;
	struct clk_init_data init;
	int err, i;

	rk_phy = devm_kzalloc(base->dev, sizeof(*rk_phy), GFP_KERNEL);
	if (!rk_phy)
		return -ENOMEM;

	rk_phy->base = base;
	rk_phy->np = child;

	if (of_property_read_u32(child, "reg", &reg_offset)) {
		dev_err(base->dev, "missing reg property in node %s\n",
			child->name);
		return -EINVAL;
	}

	rk_phy->reset = of_reset_control_get(child, "phy-reset");
	if (IS_ERR(rk_phy->reset))
		rk_phy->reset = NULL;

	rk_phy->reg_offset = reg_offset;

	rk_phy->clk = of_clk_get_by_name(child, "phyclk");
	if (IS_ERR(rk_phy->clk))
		rk_phy->clk = NULL;

	i = 0;
	init.name = NULL;
	while (base->pdata->phys[i].reg) {
		if (base->pdata->phys[i].reg == reg_offset) {
			init.name = base->pdata->phys[i].pll_name;
			break;
		}
		i++;
	}

	if (!init.name) {
		dev_err(base->dev, "phy data not found\n");
		return -EINVAL;
	}

	if (rk_phy->clk) {
		clk_name = __clk_get_name(rk_phy->clk);
		init.flags = 0;
		init.parent_names = &clk_name;
		init.num_parents = 1;
	} else {
		init.flags = CLK_IS_ROOT;
		init.parent_names = NULL;
		init.num_parents = 0;
	}

	init.ops = &rockchip_usb_phy480m_ops;
	rk_phy->clk480m_hw.init = &init;

	rk_phy->clk480m = clk_register(base->dev, &rk_phy->clk480m_hw);
	if (IS_ERR(rk_phy->clk480m)) {
		err = PTR_ERR(rk_phy->clk480m);
		goto err_clk;
	}

	err = of_clk_add_provider(child, of_clk_src_simple_get,
				  rk_phy->clk480m);
	if (err < 0)
		goto err_clk_prov;

	err = devm_add_action(base->dev, rockchip_usb_phy_action, rk_phy);
	if (err)
		goto err_devm_action;

	rk_phy->phy = devm_phy_create(base->dev, child, &ops);
	if (IS_ERR(rk_phy->phy)) {
		dev_err(base->dev, "failed to create PHY\n");
		return PTR_ERR(rk_phy->phy);
	}
	phy_set_drvdata(rk_phy->phy, rk_phy);

	/* only power up usb phy when it use, so disable it when init*/
	return rockchip_usb_phy_power(rk_phy, 1);

err_devm_action:
	of_clk_del_provider(child);
err_clk_prov:
	clk_unregister(rk_phy->clk480m);
err_clk:
	if (rk_phy->clk)
		clk_put(rk_phy->clk);
	return err;
}

static const struct rockchip_usb_phy_pdata rk3066a_pdata = {
	.phys = (struct rockchip_usb_phys[]){
		{ .reg = 0x17c, .pll_name = "sclk_otgphy0_480m" },
		{ .reg = 0x188, .pll_name = "sclk_otgphy1_480m" },
		{ /* sentinel */ }
	},
	.phy_pw_on  = SIDDQ_WRITE_ENA | SIDDQ_OFF,
	.phy_pw_off = SIDDQ_WRITE_ENA | SIDDQ_ON,
	.siddq_ctl  = true,
};

static const struct rockchip_usb_phy_pdata rk3188_pdata = {
	.phys = (struct rockchip_usb_phys[]){
		{ .reg = 0x10c, .pll_name = "sclk_otgphy0_480m" },
		{ .reg = 0x11c, .pll_name = "sclk_otgphy1_480m" },
		{ /* sentinel */ }
	},
	.phy_pw_on  = SIDDQ_WRITE_ENA | SIDDQ_OFF,
	.phy_pw_off = SIDDQ_WRITE_ENA | SIDDQ_ON,
	.siddq_ctl  = true,
};

static const struct rockchip_usb_phy_pdata rk3288_pdata = {
	.phys = (struct rockchip_usb_phys[]){
		{ .reg = 0x320, .pll_name = "sclk_otgphy0_480m" },
		{ .reg = 0x334, .pll_name = "sclk_otgphy1_480m" },
		{ .reg = 0x348, .pll_name = "sclk_otgphy2_480m" },
		{ /* sentinel */ }
	},
	.phy_pw_on  = SIDDQ_WRITE_ENA | SIDDQ_OFF,
	.phy_pw_off = SIDDQ_WRITE_ENA | SIDDQ_ON,
	.siddq_ctl  = true,
};

static const struct rockchip_usb_phy_pdata rk336x_pdata = {
	.phys = (struct rockchip_usb_phys[]){
		{ .reg = 0x700, .pll_name = "sclk_otgphy0_480m" },
		{ .reg = 0x728, .pll_name = "sclk_otgphy1_480m" },
		{ /* sentinel */ }
	},
	.phy_pw_on  = USB2_PHY_WRITE_ENA | USB2_PHY_RESUME,
	.phy_pw_off = USB2_PHY_WRITE_ENA | USB2_PHY_SUSPEND,
	.siddq_ctl  = false,
};

static const struct rockchip_usb_phy_pdata rk3399_pdata = {
	.phys = (struct rockchip_usb_phys[]){
		{ .reg = 0xe458, .pll_name = "sclk_otgphy0_480m" },
		{ .reg = 0xe468, .pll_name = "sclk_otgphy1_480m" },
		{ /* sentinel */ }
	},
	.phy_pw_on  = UTMI_SEL_GRF_WR_ENA | UTMI_SEL_GRF_RESUME,
	.phy_pw_off = UTMI_SEL_GRF_WR_ENA | UTMI_SEL_GRF_SUSPEND,
	.siddq_ctl  = false,
};

static int rockchip_usb_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_usb_phy_base *phy_base;
	struct phy_provider *phy_provider;
	const struct of_device_id *match;
	struct device_node *child;
	int err;

	phy_base = devm_kzalloc(dev, sizeof(*phy_base), GFP_KERNEL);
	if (!phy_base)
		return -ENOMEM;

	match = of_match_device(dev->driver->of_match_table, dev);
	if (!match || !match->data) {
		dev_err(dev, "missing phy data\n");
		return -EINVAL;
	}

	phy_base->pdata = match->data;

	phy_base->dev = dev;
	phy_base->reg_base = ERR_PTR(-ENODEV);
	if (dev->parent && dev->parent->of_node)
		phy_base->reg_base = syscon_node_to_regmap(
						dev->parent->of_node);
	if (IS_ERR(phy_base->reg_base))
		phy_base->reg_base = syscon_regmap_lookup_by_phandle(
						dev->of_node, "rockchip,grf");
	if (IS_ERR(phy_base->reg_base)) {
		dev_err(&pdev->dev, "Missing rockchip,grf property\n");
		return PTR_ERR(phy_base->reg_base);
	}

	/* Request the vbus_drv GPIO asserted */
	phy_base->vbus_drv_gpio =
		devm_gpiod_get_optional(dev, "vbus_drv", GPIOD_OUT_HIGH);
	if (!phy_base->vbus_drv_gpio)
		dev_info(&pdev->dev, "vbus_drv is not assigned!\n");
	else if (IS_ERR(phy_base->vbus_drv_gpio))
		return PTR_ERR(phy_base->vbus_drv_gpio);

	for_each_available_child_of_node(dev->of_node, child) {
		err = rockchip_usb_phy_init(phy_base, child);
		if (err) {
			of_node_put(child);
			return err;
		}
	}

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id rockchip_usb_phy_dt_ids[] = {
	{ .compatible = "rockchip,rk3066a-usb-phy", .data = &rk3066a_pdata },
	{ .compatible = "rockchip,rk3188-usb-phy", .data = &rk3188_pdata },
	{ .compatible = "rockchip,rk3288-usb-phy", .data = &rk3288_pdata },
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
