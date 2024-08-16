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
#include <linux/clk.h>
#include <linux/soc/qcom/smem.h>

#include <dt-bindings/clock/qcom,apss-ipq.h>
#include <dt-bindings/arm/qcom,ids.h>

#include "common.h"
#include "clk-regmap.h"
#include "clk-branch.h"
#include "clk-alpha-pll.h"
#include "clk-rcg.h"

enum {
	P_XO,
	P_GPLL0,
	P_APSS_PLL_EARLY,
};

static const struct clk_parent_data parents_apcs_alias0_clk_src[] = {
	{ .fw_name = "xo" },
	{ .fw_name = "gpll0" },
	{ .fw_name = "pll" },
};

static const struct parent_map parents_apcs_alias0_clk_src_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 4 },
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
			.flags = CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
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

static int cpu_clk_notifier_fn(struct notifier_block *nb, unsigned long action,
				void *data)
{
	struct clk_hw *hw;
	u8 index;
	int err;

	if (action == PRE_RATE_CHANGE)
		index = P_GPLL0;
	else if (action == POST_RATE_CHANGE || action == ABORT_RATE_CHANGE)
		index = P_APSS_PLL_EARLY;
	else
		return NOTIFY_OK;

	hw = &apcs_alias0_clk_src.clkr.hw;
	err = clk_rcg2_mux_closest_ops.set_parent(hw, index);

	return notifier_from_errno(err);
}

static int apss_ipq6018_probe(struct platform_device *pdev)
{
	struct clk_hw *hw = &apcs_alias0_clk_src.clkr.hw;
	struct notifier_block *cpu_clk_notifier;
	struct regmap *regmap;
	u32 soc_id;
	int ret;

	ret = qcom_smem_get_soc_id(&soc_id);
	if (ret)
		return ret;

	regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!regmap)
		return -ENODEV;

	ret = qcom_cc_really_probe(&pdev->dev, &apss_ipq6018_desc, regmap);
	if (ret)
		return ret;

	switch (soc_id) {
	/* Only below variants of IPQ53xx support scaling */
	case QCOM_ID_IPQ5332:
	case QCOM_ID_IPQ5322:
	case QCOM_ID_IPQ5300:
		cpu_clk_notifier = devm_kzalloc(&pdev->dev,
						sizeof(*cpu_clk_notifier),
						GFP_KERNEL);
		if (!cpu_clk_notifier)
			return -ENOMEM;

		cpu_clk_notifier->notifier_call = cpu_clk_notifier_fn;

		ret = devm_clk_notifier_register(&pdev->dev, hw->clk, cpu_clk_notifier);
		if (ret)
			return ret;
		break;
	default:
		break;
	}

	return 0;
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
