// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include <linux/module.h>

#include <dt-bindings/clock/qcom,apss-ipq.h>

#include "common.h"
#include "clk-regmap.h"
#include "clk-branch.h"
#include "clk-alpha-pll.h"
#include "clk-rcg.h"

enum {
	P_XO,
	P_APSS_PLL_EARLY,
};

static const struct clk_parent_data parents_apcs_alias0_clk_src[] = {
	{ .fw_name = "xo" },
	{ .fw_name = "pll" },
};

static const struct parent_map parents_apcs_alias0_clk_src_map[] = {
	{ P_XO, 0 },
	{ P_APSS_PLL_EARLY, 5 },
};

static struct clk_rcg2 apcs_alias0_clk_src = {
	.cmd_rcgr = 0x0050,
	.hid_width = 5,
	.parent_map = parents_apcs_alias0_clk_src_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "apcs_alias0_clk_src",
		.parent_data = parents_apcs_alias0_clk_src,
		.num_parents = ARRAY_SIZE(parents_apcs_alias0_clk_src),
		.ops = &clk_rcg2_mux_closest_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_branch apcs_alias0_core_clk = {
	.halt_reg = 0x0058,
	.clkr = {
		.enable_reg = 0x0058,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "apcs_alias0_core_clk",
			.parent_hws = (const struct clk_hw *[]){
				&apcs_alias0_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static const struct regmap_config apss_ipq6018_regmap_config = {
	.reg_bits       = 32,
	.reg_stride     = 4,
	.val_bits       = 32,
	.max_register   = 0x1000,
	.fast_io        = true,
};

static struct clk_regmap *apss_ipq6018_clks[] = {
	[APCS_ALIAS0_CLK_SRC] = &apcs_alias0_clk_src.clkr,
	[APCS_ALIAS0_CORE_CLK] = &apcs_alias0_core_clk.clkr,
};

static const struct qcom_cc_desc apss_ipq6018_desc = {
	.config = &apss_ipq6018_regmap_config,
	.clks = apss_ipq6018_clks,
	.num_clks = ARRAY_SIZE(apss_ipq6018_clks),
};

static int apss_ipq6018_probe(struct platform_device *pdev)
{
	struct regmap *regmap;

	regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!regmap)
		return -ENODEV;

	return qcom_cc_really_probe(pdev, &apss_ipq6018_desc, regmap);
}

static struct platform_driver apss_ipq6018_driver = {
	.probe = apss_ipq6018_probe,
	.driver = {
		.name   = "qcom,apss-ipq6018-clk",
	},
};

module_platform_driver(apss_ipq6018_driver);

MODULE_DESCRIPTION("QCOM APSS IPQ 6018 CLK Driver");
MODULE_LICENSE("GPL v2");
