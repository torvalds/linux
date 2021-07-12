// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm SDX55 APCS clock controller driver
 *
 * Copyright (c) 2020, Linaro Limited
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/cpu.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "clk-regmap.h"
#include "clk-regmap-mux-div.h"

static const u32 apcs_mux_clk_parent_map[] = { 0, 1, 5 };

static const struct clk_parent_data pdata[] = {
	{ .fw_name = "ref" },
	{ .fw_name = "aux" },
	{ .fw_name = "pll" },
};

/*
 * We use the notifier function for switching to a temporary safe configuration
 * (mux and divider), while the A7 PLL is reconfigured.
 */
static int a7cc_notifier_cb(struct notifier_block *nb, unsigned long event,
			    void *data)
{
	int ret = 0;
	struct clk_regmap_mux_div *md = container_of(nb,
						     struct clk_regmap_mux_div,
						     clk_nb);
	if (event == PRE_RATE_CHANGE)
		/* set the mux and divider to safe frequency (400mhz) */
		ret = mux_div_set_src_div(md, 1, 2);

	return notifier_from_errno(ret);
}

static int qcom_apcs_sdx55_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device *parent = dev->parent;
	struct device *cpu_dev;
	struct clk_regmap_mux_div *a7cc;
	struct regmap *regmap;
	struct clk_init_data init = { };
	int ret;

	regmap = dev_get_regmap(parent, NULL);
	if (!regmap) {
		dev_err(dev, "Failed to get parent regmap\n");
		return -ENODEV;
	}

	a7cc = devm_kzalloc(dev, sizeof(*a7cc), GFP_KERNEL);
	if (!a7cc)
		return -ENOMEM;

	init.name = "a7mux";
	init.parent_data = pdata;
	init.num_parents = ARRAY_SIZE(pdata);
	init.ops = &clk_regmap_mux_div_ops;

	a7cc->clkr.hw.init = &init;
	a7cc->clkr.regmap = regmap;
	a7cc->reg_offset = 0x8;
	a7cc->hid_width = 5;
	a7cc->hid_shift = 0;
	a7cc->src_width = 3;
	a7cc->src_shift = 8;
	a7cc->parent_map = apcs_mux_clk_parent_map;

	a7cc->pclk = devm_clk_get(parent, "pll");
	if (IS_ERR(a7cc->pclk))
		return dev_err_probe(dev, PTR_ERR(a7cc->pclk),
				     "Failed to get PLL clk\n");

	a7cc->clk_nb.notifier_call = a7cc_notifier_cb;
	ret = clk_notifier_register(a7cc->pclk, &a7cc->clk_nb);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to register clock notifier\n");

	ret = devm_clk_register_regmap(dev, &a7cc->clkr);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to register regmap clock\n");
		goto err;
	}

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get,
					  &a7cc->clkr.hw);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to add clock provider\n");
		goto err;
	}

	platform_set_drvdata(pdev, a7cc);

	/*
	 * Attach the power domain to cpudev. Since there is no dedicated driver
	 * for CPUs and the SDX55 platform lacks hardware specific CPUFreq
	 * driver, there seems to be no better place to do this. So do it here!
	 */
	cpu_dev = get_cpu_device(0);
	dev_pm_domain_attach(cpu_dev, true);

	return 0;

err:
	clk_notifier_unregister(a7cc->pclk, &a7cc->clk_nb);
	return ret;
}

static int qcom_apcs_sdx55_clk_remove(struct platform_device *pdev)
{
	struct device *cpu_dev = get_cpu_device(0);
	struct clk_regmap_mux_div *a7cc = platform_get_drvdata(pdev);

	clk_notifier_unregister(a7cc->pclk, &a7cc->clk_nb);
	dev_pm_domain_detach(cpu_dev, true);

	return 0;
}

static struct platform_driver qcom_apcs_sdx55_clk_driver = {
	.probe = qcom_apcs_sdx55_clk_probe,
	.remove = qcom_apcs_sdx55_clk_remove,
	.driver = {
		.name = "qcom-sdx55-acps-clk",
	},
};
module_platform_driver(qcom_apcs_sdx55_clk_driver);

MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Qualcomm SDX55 APCS clock driver");
