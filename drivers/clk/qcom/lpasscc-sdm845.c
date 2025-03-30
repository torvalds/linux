// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,lpass-sdm845.h>

#include "clk-regmap.h"
#include "clk-branch.h"
#include "common.h"

static struct clk_branch lpass_q6ss_ahbm_aon_clk = {
	.halt_reg = 0x12000,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x12000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "lpass_q6ss_ahbm_aon_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_q6ss_ahbs_aon_clk = {
	.halt_reg = 0x1f000,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x1f000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "lpass_q6ss_ahbs_aon_clk",
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

static struct clk_regmap *lpass_cc_sdm845_clocks[] = {
	[LPASS_Q6SS_AHBM_AON_CLK] = &lpass_q6ss_ahbm_aon_clk.clkr,
	[LPASS_Q6SS_AHBS_AON_CLK] = &lpass_q6ss_ahbs_aon_clk.clkr,
};

static const struct qcom_cc_desc lpass_cc_sdm845_desc = {
	.config = &lpass_regmap_config,
	.clks = lpass_cc_sdm845_clocks,
	.num_clks = ARRAY_SIZE(lpass_cc_sdm845_clocks),
};

static struct clk_regmap *lpass_qdsp6ss_sdm845_clocks[] = {
	[LPASS_QDSP6SS_XO_CLK] = &lpass_qdsp6ss_xo_clk.clkr,
	[LPASS_QDSP6SS_SLEEP_CLK] = &lpass_qdsp6ss_sleep_clk.clkr,
	[LPASS_QDSP6SS_CORE_CLK] = &lpass_qdsp6ss_core_clk.clkr,
};

static const struct qcom_cc_desc lpass_qdsp6ss_sdm845_desc = {
	.config = &lpass_regmap_config,
	.clks = lpass_qdsp6ss_sdm845_clocks,
	.num_clks = ARRAY_SIZE(lpass_qdsp6ss_sdm845_clocks),
};

static int lpass_cc_sdm845_probe(struct platform_device *pdev)
{
	const struct qcom_cc_desc *desc;
	int ret;

	lpass_regmap_config.name = "cc";
	desc = &lpass_cc_sdm845_desc;

	ret = qcom_cc_probe_by_index(pdev, 0, desc);
	if (ret)
		return ret;

	lpass_regmap_config.name = "qdsp6ss";
	desc = &lpass_qdsp6ss_sdm845_desc;

	return qcom_cc_probe_by_index(pdev, 1, desc);
}

static const struct of_device_id lpass_cc_sdm845_match_table[] = {
	{ .compatible = "qcom,sdm845-lpasscc" },
	{ }
};
MODULE_DEVICE_TABLE(of, lpass_cc_sdm845_match_table);

static struct platform_driver lpass_cc_sdm845_driver = {
	.probe		= lpass_cc_sdm845_probe,
	.driver		= {
		.name	= "sdm845-lpasscc",
		.of_match_table = lpass_cc_sdm845_match_table,
	},
};

static int __init lpass_cc_sdm845_init(void)
{
	return platform_driver_register(&lpass_cc_sdm845_driver);
}
subsys_initcall(lpass_cc_sdm845_init);

static void __exit lpass_cc_sdm845_exit(void)
{
	platform_driver_unregister(&lpass_cc_sdm845_driver);
}
module_exit(lpass_cc_sdm845_exit);

MODULE_DESCRIPTION("QTI LPASS_CC SDM845 Driver");
MODULE_LICENSE("GPL v2");
