// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 Free Electrons
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>

struct sun6i_drc {
	struct clk		*bus_clk;
	struct clk		*mod_clk;
	struct reset_control	*reset;
};

static int sun6i_drc_bind(struct device *dev, struct device *master,
			 void *data)
{
	struct sun6i_drc *drc;
	int ret;

	drc = devm_kzalloc(dev, sizeof(*drc), GFP_KERNEL);
	if (!drc)
		return -ENOMEM;
	dev_set_drvdata(dev, drc);

	drc->reset = devm_reset_control_get(dev, NULL);
	if (IS_ERR(drc->reset)) {
		dev_err(dev, "Couldn't get our reset line\n");
		return PTR_ERR(drc->reset);
	}

	ret = reset_control_deassert(drc->reset);
	if (ret) {
		dev_err(dev, "Couldn't deassert our reset line\n");
		return ret;
	}

	drc->bus_clk = devm_clk_get(dev, "ahb");
	if (IS_ERR(drc->bus_clk)) {
		dev_err(dev, "Couldn't get our bus clock\n");
		ret = PTR_ERR(drc->bus_clk);
		goto err_assert_reset;
	}
	clk_prepare_enable(drc->bus_clk);

	drc->mod_clk = devm_clk_get(dev, "mod");
	if (IS_ERR(drc->mod_clk)) {
		dev_err(dev, "Couldn't get our mod clock\n");
		ret = PTR_ERR(drc->mod_clk);
		goto err_disable_bus_clk;
	}

	ret = clk_set_rate_exclusive(drc->mod_clk, 300000000);
	if (ret) {
		dev_err(dev, "Couldn't set the module clock frequency\n");
		goto err_disable_bus_clk;
	}

	clk_prepare_enable(drc->mod_clk);

	return 0;

err_disable_bus_clk:
	clk_disable_unprepare(drc->bus_clk);
err_assert_reset:
	reset_control_assert(drc->reset);
	return ret;
}

static void sun6i_drc_unbind(struct device *dev, struct device *master,
			    void *data)
{
	struct sun6i_drc *drc = dev_get_drvdata(dev);

	clk_rate_exclusive_put(drc->mod_clk);
	clk_disable_unprepare(drc->mod_clk);
	clk_disable_unprepare(drc->bus_clk);
	reset_control_assert(drc->reset);
}

static const struct component_ops sun6i_drc_ops = {
	.bind	= sun6i_drc_bind,
	.unbind	= sun6i_drc_unbind,
};

static int sun6i_drc_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &sun6i_drc_ops);
}

static int sun6i_drc_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sun6i_drc_ops);

	return 0;
}

static const struct of_device_id sun6i_drc_of_table[] = {
	{ .compatible = "allwinner,sun6i-a31-drc" },
	{ .compatible = "allwinner,sun6i-a31s-drc" },
	{ .compatible = "allwinner,sun8i-a23-drc" },
	{ .compatible = "allwinner,sun8i-a33-drc" },
	{ .compatible = "allwinner,sun9i-a80-drc" },
	{ }
};
MODULE_DEVICE_TABLE(of, sun6i_drc_of_table);

static struct platform_driver sun6i_drc_platform_driver = {
	.probe		= sun6i_drc_probe,
	.remove		= sun6i_drc_remove,
	.driver		= {
		.name		= "sun6i-drc",
		.of_match_table	= sun6i_drc_of_table,
	},
};
module_platform_driver(sun6i_drc_platform_driver);

MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_DESCRIPTION("Allwinner A31 Dynamic Range Control (DRC) Driver");
MODULE_LICENSE("GPL");
