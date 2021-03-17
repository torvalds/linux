// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas R-Car USB2.0 clock selector
 *
 * Copyright (C) 2017 Renesas Electronics Corp.
 *
 * Based on renesas-cpg-mssr.c
 *
 * Copyright (C) 2015 Glider bvba
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/slab.h>

#define USB20_CLKSET0		0x00
#define CLKSET0_INTCLK_EN	BIT(11)
#define CLKSET0_PRIVATE		BIT(0)
#define CLKSET0_EXTAL_ONLY	(CLKSET0_INTCLK_EN | CLKSET0_PRIVATE)

static const struct clk_bulk_data rcar_usb2_clocks[] = {
	{ .id = "ehci_ohci", },
	{ .id = "hs-usb-if", },
};

struct usb2_clock_sel_priv {
	void __iomem *base;
	struct clk_hw hw;
	struct clk_bulk_data clks[ARRAY_SIZE(rcar_usb2_clocks)];
	struct reset_control *rsts;
	bool extal;
	bool xtal;
};
#define to_priv(_hw)	container_of(_hw, struct usb2_clock_sel_priv, hw)

static void usb2_clock_sel_enable_extal_only(struct usb2_clock_sel_priv *priv)
{
	u16 val = readw(priv->base + USB20_CLKSET0);

	pr_debug("%s: enter %d %d %x\n", __func__,
		 priv->extal, priv->xtal, val);

	if (priv->extal && !priv->xtal && val != CLKSET0_EXTAL_ONLY)
		writew(CLKSET0_EXTAL_ONLY, priv->base + USB20_CLKSET0);
}

static void usb2_clock_sel_disable_extal_only(struct usb2_clock_sel_priv *priv)
{
	if (priv->extal && !priv->xtal)
		writew(CLKSET0_PRIVATE, priv->base + USB20_CLKSET0);
}

static int usb2_clock_sel_enable(struct clk_hw *hw)
{
	struct usb2_clock_sel_priv *priv = to_priv(hw);
	int ret;

	ret = reset_control_deassert(priv->rsts);
	if (ret)
		return ret;

	ret = clk_bulk_prepare_enable(ARRAY_SIZE(priv->clks), priv->clks);
	if (ret) {
		reset_control_assert(priv->rsts);
		return ret;
	}

	usb2_clock_sel_enable_extal_only(priv);

	return 0;
}

static void usb2_clock_sel_disable(struct clk_hw *hw)
{
	struct usb2_clock_sel_priv *priv = to_priv(hw);

	usb2_clock_sel_disable_extal_only(priv);

	clk_bulk_disable_unprepare(ARRAY_SIZE(priv->clks), priv->clks);
	reset_control_assert(priv->rsts);
}

/*
 * This module seems a mux, but this driver assumes a gate because
 * ehci/ohci platform drivers don't support clk_set_parent() for now.
 * If this driver acts as a gate, ehci/ohci-platform drivers don't need
 * any modification.
 */
static const struct clk_ops usb2_clock_sel_clock_ops = {
	.enable = usb2_clock_sel_enable,
	.disable = usb2_clock_sel_disable,
};

static const struct of_device_id rcar_usb2_clock_sel_match[] = {
	{ .compatible = "renesas,rcar-gen3-usb2-clock-sel" },
	{ }
};

static int rcar_usb2_clock_sel_suspend(struct device *dev)
{
	struct usb2_clock_sel_priv *priv = dev_get_drvdata(dev);

	usb2_clock_sel_disable_extal_only(priv);
	pm_runtime_put(dev);

	return 0;
}

static int rcar_usb2_clock_sel_resume(struct device *dev)
{
	struct usb2_clock_sel_priv *priv = dev_get_drvdata(dev);

	pm_runtime_get_sync(dev);
	usb2_clock_sel_enable_extal_only(priv);

	return 0;
}

static int rcar_usb2_clock_sel_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct usb2_clock_sel_priv *priv = platform_get_drvdata(pdev);

	of_clk_del_provider(dev->of_node);
	clk_hw_unregister(&priv->hw);
	pm_runtime_put(dev);
	pm_runtime_disable(dev);

	return 0;
}

static int rcar_usb2_clock_sel_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct usb2_clock_sel_priv *priv;
	struct clk *clk;
	struct clk_init_data init;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	memcpy(priv->clks, rcar_usb2_clocks, sizeof(priv->clks));
	ret = devm_clk_bulk_get(dev, ARRAY_SIZE(priv->clks), priv->clks);
	if (ret < 0)
		return ret;

	priv->rsts = devm_reset_control_array_get(dev, true, false);
	if (IS_ERR(priv->rsts))
		return PTR_ERR(priv->rsts);

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	clk = devm_clk_get(dev, "usb_extal");
	if (!IS_ERR(clk) && !clk_prepare_enable(clk)) {
		priv->extal = !!clk_get_rate(clk);
		clk_disable_unprepare(clk);
	}
	clk = devm_clk_get(dev, "usb_xtal");
	if (!IS_ERR(clk) && !clk_prepare_enable(clk)) {
		priv->xtal = !!clk_get_rate(clk);
		clk_disable_unprepare(clk);
	}

	if (!priv->extal && !priv->xtal) {
		dev_err(dev, "This driver needs usb_extal or usb_xtal\n");
		return -ENOENT;
	}

	platform_set_drvdata(pdev, priv);
	dev_set_drvdata(dev, priv);

	init.name = "rcar_usb2_clock_sel";
	init.ops = &usb2_clock_sel_clock_ops;
	init.flags = 0;
	init.parent_names = NULL;
	init.num_parents = 0;
	priv->hw.init = &init;

	clk = clk_register(NULL, &priv->hw);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	return of_clk_add_hw_provider(np, of_clk_hw_simple_get, &priv->hw);
}

static const struct dev_pm_ops rcar_usb2_clock_sel_pm_ops = {
	.suspend	= rcar_usb2_clock_sel_suspend,
	.resume		= rcar_usb2_clock_sel_resume,
};

static struct platform_driver rcar_usb2_clock_sel_driver = {
	.driver		= {
		.name	= "rcar-usb2-clock-sel",
		.of_match_table = rcar_usb2_clock_sel_match,
		.pm	= &rcar_usb2_clock_sel_pm_ops,
	},
	.probe		= rcar_usb2_clock_sel_probe,
	.remove		= rcar_usb2_clock_sel_remove,
};
builtin_platform_driver(rcar_usb2_clock_sel_driver);

MODULE_DESCRIPTION("Renesas R-Car USB2 clock selector Driver");
MODULE_LICENSE("GPL v2");
