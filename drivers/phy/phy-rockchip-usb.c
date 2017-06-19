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

static int enable_usb_uart;

#define HIWORD_UPDATE(val, mask) \
		((val) | (mask) << 16)

#define UOC_CON0_SIDDQ BIT(13)

struct rockchip_usb_phys {
	int reg;
	const char *pll_name;
};

struct rockchip_usb_phy_base;
struct rockchip_usb_phy_pdata {
	struct rockchip_usb_phys *phys;
	int (*init_usb_uart)(struct regmap *grf);
	int usb_uart_phy;
};

struct rockchip_usb_phy_base {
	struct device *dev;
	struct regmap *reg_base;
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
	bool		uart_enabled;
	struct reset_control *reset;
	struct regulator *vbus;
};

static int rockchip_usb_phy_power(struct rockchip_usb_phy *phy,
					   bool siddq)
{
	u32 val = HIWORD_UPDATE(siddq ? UOC_CON0_SIDDQ : 0, UOC_CON0_SIDDQ);

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

	if (phy->vbus)
		regulator_disable(phy->vbus);

	/* Power down usb phy analog blocks by set siddq 1 */
	rockchip_usb_phy_power(phy, 1);
}

static int rockchip_usb_phy480m_enable(struct clk_hw *hw)
{
	struct rockchip_usb_phy *phy = container_of(hw,
						    struct rockchip_usb_phy,
						    clk480m_hw);

	/* Power up usb phy analog blocks by set siddq 0 */
	return rockchip_usb_phy_power(phy, 0);
}

static int rockchip_usb_phy480m_is_enabled(struct clk_hw *hw)
{
	struct rockchip_usb_phy *phy = container_of(hw,
						    struct rockchip_usb_phy,
						    clk480m_hw);
	int ret;
	u32 val;

	ret = regmap_read(phy->base->reg_base, phy->reg_offset, &val);
	if (ret < 0)
		return ret;

	return (val & UOC_CON0_SIDDQ) ? 0 : 1;
}

static const struct clk_ops rockchip_usb_phy480m_ops = {
	.enable = rockchip_usb_phy480m_enable,
	.disable = rockchip_usb_phy480m_disable,
	.is_enabled = rockchip_usb_phy480m_is_enabled,
	.recalc_rate = rockchip_usb_phy480m_recalc_rate,
};

static int rockchip_usb_phy_power_off(struct phy *_phy)
{
	struct rockchip_usb_phy *phy = phy_get_drvdata(_phy);

	if (phy->uart_enabled)
		return -EBUSY;

	clk_disable_unprepare(phy->clk480m);

	return 0;
}

static int rockchip_usb_phy_power_on(struct phy *_phy)
{
	struct rockchip_usb_phy *phy = phy_get_drvdata(_phy);

	if (phy->uart_enabled)
		return -EBUSY;

	if (phy->vbus) {
		int ret;

		ret = regulator_enable(phy->vbus);
		if (ret)
			return ret;
	}

	return clk_prepare_enable(phy->clk480m);
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

	if (!rk_phy->uart_enabled) {
		of_clk_del_provider(rk_phy->np);
		clk_unregister(rk_phy->clk480m);
	}

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

	if (enable_usb_uart && base->pdata->usb_uart_phy == i) {
		dev_dbg(base->dev, "phy%d used as uart output\n", i);
		rk_phy->uart_enabled = true;
	} else {
		if (rk_phy->clk) {
			clk_name = __clk_get_name(rk_phy->clk);
			init.flags = 0;
			init.parent_names = &clk_name;
			init.num_parents = 1;
		} else {
			init.flags = 0;
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
	}

	err = devm_add_action_or_reset(base->dev, rockchip_usb_phy_action,
				       rk_phy);
	if (err)
		return err;

	rk_phy->phy = devm_phy_create(base->dev, child, &ops);
	if (IS_ERR(rk_phy->phy)) {
		dev_err(base->dev, "failed to create PHY\n");
		return PTR_ERR(rk_phy->phy);
	}
	phy_set_drvdata(rk_phy->phy, rk_phy);

	rk_phy->vbus = devm_regulator_get_optional(&rk_phy->phy->dev, "vbus");
	if (IS_ERR(rk_phy->vbus)) {
		if (PTR_ERR(rk_phy->vbus) == -EPROBE_DEFER)
			return PTR_ERR(rk_phy->vbus);
		rk_phy->vbus = NULL;
	}

	/*
	 * When acting as uart-pipe, just keep clock on otherwise
	 * only power up usb phy when it use, so disable it when init
	 */
	if (rk_phy->uart_enabled)
		return clk_prepare_enable(rk_phy->clk);
	else
		return rockchip_usb_phy_power(rk_phy, 1);

err_clk_prov:
	if (!rk_phy->uart_enabled)
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
};

static const struct rockchip_usb_phy_pdata rk3188_pdata = {
	.phys = (struct rockchip_usb_phys[]){
		{ .reg = 0x10c, .pll_name = "sclk_otgphy0_480m" },
		{ .reg = 0x11c, .pll_name = "sclk_otgphy1_480m" },
		{ /* sentinel */ }
	},
};

#define RK3288_UOC0_CON0				0x320
#define RK3288_UOC0_CON0_COMMON_ON_N			BIT(0)
#define RK3288_UOC0_CON0_DISABLE			BIT(4)

#define RK3288_UOC0_CON2				0x328
#define RK3288_UOC0_CON2_SOFT_CON_SEL			BIT(2)

#define RK3288_UOC0_CON3				0x32c
#define RK3288_UOC0_CON3_UTMI_SUSPENDN			BIT(0)
#define RK3288_UOC0_CON3_UTMI_OPMODE_NODRIVING		(1 << 1)
#define RK3288_UOC0_CON3_UTMI_OPMODE_MASK		(3 << 1)
#define RK3288_UOC0_CON3_UTMI_XCVRSEELCT_FSTRANSC	(1 << 3)
#define RK3288_UOC0_CON3_UTMI_XCVRSEELCT_MASK		(3 << 3)
#define RK3288_UOC0_CON3_UTMI_TERMSEL_FULLSPEED		BIT(5)
#define RK3288_UOC0_CON3_BYPASSDMEN			BIT(6)
#define RK3288_UOC0_CON3_BYPASSSEL			BIT(7)

/*
 * Enable the bypass of uart2 data through the otg usb phy.
 * Original description in the TRM.
 * 1. Disable the OTG block by setting OTGDISABLE0 to 1’b1.
 * 2. Disable the pull-up resistance on the D+ line by setting
 *    OPMODE0[1:0] to 2’b01.
 * 3. To ensure that the XO, Bias, and PLL blocks are powered down in Suspend
 *    mode, set COMMONONN to 1’b1.
 * 4. Place the USB PHY in Suspend mode by setting SUSPENDM0 to 1’b0.
 * 5. Set BYPASSSEL0 to 1’b1.
 * 6. To transmit data, controls BYPASSDMEN0, and BYPASSDMDATA0.
 * To receive data, monitor FSVPLUS0.
 *
 * The actual code in the vendor kernel does some things differently.
 */
static int __init rk3288_init_usb_uart(struct regmap *grf)
{
	u32 val;
	int ret;

	/*
	 * COMMON_ON and DISABLE settings are described in the TRM,
	 * but were not present in the original code.
	 * Also disable the analog phy components to save power.
	 */
	val = HIWORD_UPDATE(RK3288_UOC0_CON0_COMMON_ON_N
				| RK3288_UOC0_CON0_DISABLE
				| UOC_CON0_SIDDQ,
			    RK3288_UOC0_CON0_COMMON_ON_N
				| RK3288_UOC0_CON0_DISABLE
				| UOC_CON0_SIDDQ);
	ret = regmap_write(grf, RK3288_UOC0_CON0, val);
	if (ret)
		return ret;

	val = HIWORD_UPDATE(RK3288_UOC0_CON2_SOFT_CON_SEL,
			    RK3288_UOC0_CON2_SOFT_CON_SEL);
	ret = regmap_write(grf, RK3288_UOC0_CON2, val);
	if (ret)
		return ret;

	val = HIWORD_UPDATE(RK3288_UOC0_CON3_UTMI_OPMODE_NODRIVING
				| RK3288_UOC0_CON3_UTMI_XCVRSEELCT_FSTRANSC
				| RK3288_UOC0_CON3_UTMI_TERMSEL_FULLSPEED,
			    RK3288_UOC0_CON3_UTMI_SUSPENDN
				| RK3288_UOC0_CON3_UTMI_OPMODE_MASK
				| RK3288_UOC0_CON3_UTMI_XCVRSEELCT_MASK
				| RK3288_UOC0_CON3_UTMI_TERMSEL_FULLSPEED);
	ret = regmap_write(grf, RK3288_UOC0_CON3, val);
	if (ret)
		return ret;

	val = HIWORD_UPDATE(RK3288_UOC0_CON3_BYPASSSEL
				| RK3288_UOC0_CON3_BYPASSDMEN,
			    RK3288_UOC0_CON3_BYPASSSEL
				| RK3288_UOC0_CON3_BYPASSDMEN);
	ret = regmap_write(grf, RK3288_UOC0_CON3, val);
	if (ret)
		return ret;

	return 0;
}

static const struct rockchip_usb_phy_pdata rk3288_pdata = {
	.phys = (struct rockchip_usb_phys[]){
		{ .reg = 0x320, .pll_name = "sclk_otgphy0_480m" },
		{ .reg = 0x334, .pll_name = "sclk_otgphy1_480m" },
		{ .reg = 0x348, .pll_name = "sclk_otgphy2_480m" },
		{ /* sentinel */ }
	},
	.init_usb_uart = rk3288_init_usb_uart,
	.usb_uart_phy = 0,
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

#ifndef MODULE
static int __init rockchip_init_usb_uart(void)
{
	const struct of_device_id *match;
	const struct rockchip_usb_phy_pdata *data;
	struct device_node *np;
	struct regmap *grf;
	int ret;

	if (!enable_usb_uart)
		return 0;

	np = of_find_matching_node_and_match(NULL, rockchip_usb_phy_dt_ids,
					     &match);
	if (!np) {
		pr_err("%s: failed to find usbphy node\n", __func__);
		return -ENOTSUPP;
	}

	pr_debug("%s: using settings for %s\n", __func__, match->compatible);
	data = match->data;

	if (!data->init_usb_uart) {
		pr_err("%s: usb-uart not available on %s\n",
		       __func__, match->compatible);
		return -ENOTSUPP;
	}

	grf = ERR_PTR(-ENODEV);
	if (np->parent)
		grf = syscon_node_to_regmap(np->parent);
	if (IS_ERR(grf))
		grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(grf)) {
		pr_err("%s: Missing rockchip,grf property, %lu\n",
		       __func__, PTR_ERR(grf));
		return PTR_ERR(grf);
	}

	ret = data->init_usb_uart(grf);
	if (ret) {
		pr_err("%s: could not init usb_uart, %d\n", __func__, ret);
		enable_usb_uart = 0;
		return ret;
	}

	return 0;
}
early_initcall(rockchip_init_usb_uart);

static int __init rockchip_usb_uart(char *buf)
{
	enable_usb_uart = true;
	return 0;
}
early_param("rockchip.usb_uart", rockchip_usb_uart);
#endif

MODULE_AUTHOR("Yunzhi Li <lyz@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip USB 2.0 PHY driver");
MODULE_LICENSE("GPL v2");
