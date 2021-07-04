// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm APCS clock controller driver
 *
 * Copyright (c) 2017, Linaro Limited
 * Author: Georgi Djakov <georgi.djakov@linaro.org>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "clk-regmap.h"
#include "clk-regmap-mux-div.h"

static const u32 gpll0_a53cc_map[] = { 4, 5 };

static const struct clk_parent_data pdata[] = {
	{ .fw_name = "aux", .name = "gpll0_vote", },
	{ .fw_name = "pll", .name = "a53pll", },
};

/*
 * We use the notifier function for switching to a temporary safe configuration
 * (mux and divider), while the A53 PLL is reconfigured.
 */
static int a53cc_notifier_cb(struct notifier_block *nb, unsigned long event,
			     void *data)
{
	int ret = 0;
	struct clk_regmap_mux_div *md = container_of(nb,
						     struct clk_regmap_mux_div,
						     clk_nb);
	if (event == PRE_RATE_CHANGE)
		/* set the mux and divider to safe frequency (400mhz) */
		ret = mux_div_set_src_div(md, 4, 3);

	return notifier_from_errno(ret);
}

static int qcom_apcs_msm8916_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device *parent = dev->parent;
	struct clk_regmap_mux_div *a53cc;
	struct regmap *regmap;
	struct clk_init_data init = { };
	int ret = -ENODEV;

	regmap = dev_get_regmap(parent, NULL);
	if (!regmap) {
		dev_err(dev, "failed to get regmap: %d\n", ret);
		return ret;
	}

	a53cc = devm_kzalloc(dev, sizeof(*a53cc), GFP_KERNEL);
	if (!a53cc)
		return -ENOMEM;

	init.name = "a53mux";
	init.parent_data = pdata;
	init.num_parents = ARRAY_SIZE(pdata);
	init.ops = &clk_regmap_mux_div_ops;
	init.flags = CLK_IS_CRITICAL | CLK_SET_RATE_PARENT;

	a53cc->clkr.hw.init = &init;
	a53cc->clkr.regmap = regmap;
	a53cc->reg_offset = 0x50;
	a53cc->hid_width = 5;
	a53cc->hid_shift = 0;
	a53cc->src_width = 3;
	a53cc->src_shift = 8;
	a53cc->parent_map = gpll0_a53cc_map;

	a53cc->pclk = devm_clk_get(parent, NULL);
	if (IS_ERR(a53cc->pclk)) {
		ret = PTR_ERR(a53cc->pclk);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to get clk: %d\n", ret);
		return ret;
	}

	a53cc->clk_nb.notifier_call = a53cc_notifier_cb;
	ret = clk_notifier_register(a53cc->pclk, &a53cc->clk_nb);
	if (ret) {
		dev_err(dev, "failed to register clock notifier: %d\n", ret);
		return ret;
	}

	ret = devm_clk_register_regmap(dev, &a53cc->clkr);
	if (ret) {
		dev_err(dev, "failed to register regmap clock: %d\n", ret);
		goto err;
	}

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get,
					  &a53cc->clkr.hw);
	if (ret) {
		dev_err(dev, "failed to add clock provider: %d\n", ret);
		goto err;
	}

	platform_set_drvdata(pdev, a53cc);

	return 0;

err:
	clk_notifier_unregister(a53cc->pclk, &a53cc->clk_nb);
	return ret;
}

static int qcom_apcs_msm8916_clk_remove(struct platform_device *pdev)
{
	struct clk_regmap_mux_div *a53cc = platform_get_drvdata(pdev);

	clk_notifier_unregister(a53cc->pclk, &a53cc->clk_nb);

	return 0;
}

static struct platform_driver qcom_apcs_msm8916_clk_driver = {
	.probe = qcom_apcs_msm8916_clk_probe,
	.remove = qcom_apcs_msm8916_clk_remove,
	.driver = {
		.name = "qcom-apcs-msm8916-clk",
	},
};
module_platform_driver(qcom_apcs_msm8916_clk_driver);

MODULE_AUTHOR("Georgi Djakov <georgi.djakov@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Qualcomm MSM8916 APCS clock driver");
