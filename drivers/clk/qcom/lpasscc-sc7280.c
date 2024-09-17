// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <linux/pm_clock.h>
#include <linux/pm_runtime.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,lpass-sc7280.h>

#include "clk-regmap.h"
#include "clk-branch.h"
#include "common.h"

static struct clk_branch lpass_top_cc_lpi_q6_axim_hs_clk = {
	.halt_reg = 0x0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "lpass_top_cc_lpi_q6_axim_hs_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_qdsp6ss_core_clk = {
	.halt_reg = 0x20,
	/* CLK_OFF would not toggle until LPASS is out of reset */
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x20,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "lpass_qdsp6ss_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_qdsp6ss_xo_clk = {
	.halt_reg = 0x38,
	/* CLK_OFF would not toggle until LPASS is out of reset */
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x38,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "lpass_qdsp6ss_xo_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_qdsp6ss_sleep_clk = {
	.halt_reg = 0x3c,
	/* CLK_OFF would not toggle until LPASS is out of reset */
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x3c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "lpass_qdsp6ss_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct regmap_config lpass_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.fast_io	= true,
};

static struct clk_regmap *lpass_cc_top_sc7280_clocks[] = {
	[LPASS_TOP_CC_LPI_Q6_AXIM_HS_CLK] =
				&lpass_top_cc_lpi_q6_axim_hs_clk.clkr,
};

static const struct qcom_cc_desc lpass_cc_top_sc7280_desc = {
	.config = &lpass_regmap_config,
	.clks = lpass_cc_top_sc7280_clocks,
	.num_clks = ARRAY_SIZE(lpass_cc_top_sc7280_clocks),
};

static struct clk_regmap *lpass_qdsp6ss_sc7280_clocks[] = {
	[LPASS_QDSP6SS_XO_CLK] = &lpass_qdsp6ss_xo_clk.clkr,
	[LPASS_QDSP6SS_SLEEP_CLK] = &lpass_qdsp6ss_sleep_clk.clkr,
	[LPASS_QDSP6SS_CORE_CLK] = &lpass_qdsp6ss_core_clk.clkr,
};

static const struct qcom_cc_desc lpass_qdsp6ss_sc7280_desc = {
	.config = &lpass_regmap_config,
	.clks = lpass_qdsp6ss_sc7280_clocks,
	.num_clks = ARRAY_SIZE(lpass_qdsp6ss_sc7280_clocks),
};

static int lpass_cc_sc7280_probe(struct platform_device *pdev)
{
	const struct qcom_cc_desc *desc;
	int ret;

	pm_runtime_enable(&pdev->dev);
	ret = pm_clk_create(&pdev->dev);
	if (ret)
		goto disable_pm_runtime;

	ret = pm_clk_add(&pdev->dev, "iface");
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to acquire iface clock\n");
		goto err_destroy_pm_clk;
	}

	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret)
		goto err_destroy_pm_clk;

	if (!of_property_read_bool(pdev->dev.of_node, "qcom,adsp-pil-mode")) {
		lpass_regmap_config.name = "qdsp6ss";
		lpass_regmap_config.max_register = 0x3f;
		desc = &lpass_qdsp6ss_sc7280_desc;

		ret = qcom_cc_probe_by_index(pdev, 0, desc);
		if (ret)
			goto err_put_rpm;
	}

	lpass_regmap_config.name = "top_cc";
	lpass_regmap_config.max_register = 0x4;
	desc = &lpass_cc_top_sc7280_desc;

	ret = qcom_cc_probe_by_index(pdev, 1, desc);
	if (ret)
		goto err_put_rpm;

	pm_runtime_put(&pdev->dev);

	return 0;

err_put_rpm:
	pm_runtime_put_sync(&pdev->dev);
err_destroy_pm_clk:
	pm_clk_destroy(&pdev->dev);

disable_pm_runtime:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static const struct of_device_id lpass_cc_sc7280_match_table[] = {
	{ .compatible = "qcom,sc7280-lpasscc" },
	{ }
};
MODULE_DEVICE_TABLE(of, lpass_cc_sc7280_match_table);

static struct platform_driver lpass_cc_sc7280_driver = {
	.probe		= lpass_cc_sc7280_probe,
	.driver		= {
		.name	= "sc7280-lpasscc",
		.of_match_table = lpass_cc_sc7280_match_table,
	},
};

static int __init lpass_cc_sc7280_init(void)
{
	return platform_driver_register(&lpass_cc_sc7280_driver);
}
subsys_initcall(lpass_cc_sc7280_init);

static void __exit lpass_cc_sc7280_exit(void)
{
	platform_driver_unregister(&lpass_cc_sc7280_driver);
}
module_exit(lpass_cc_sc7280_exit);

MODULE_DESCRIPTION("QTI LPASS_CC SC7280 Driver");
MODULE_LICENSE("GPL v2");
