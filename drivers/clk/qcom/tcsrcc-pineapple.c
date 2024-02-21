// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,tcsrcc-pineapple.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "common.h"
#include "reset.h"
#include "vdd-level.h"

static struct clk_branch tcsr_pcie_0_clkref_en = {
	.halt_reg = 0xb1100,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0xb1100,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "tcsr_pcie_0_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_pcie_1_clkref_en = {
	.halt_reg = 0xb1114,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0xb1114,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "tcsr_pcie_1_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_ufs_clkref_en = {
	.halt_reg = 0xb1110,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0xb1110,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "tcsr_ufs_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_ufs_pad_clkref_en = {
	.halt_reg = 0xb1104,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0xb1104,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "tcsr_ufs_pad_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_usb2_clkref_en = {
	.halt_reg = 0xb1118,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0xb1118,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "tcsr_usb2_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_usb3_clkref_en = {
	.halt_reg = 0xb1108,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0xb1108,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "tcsr_usb3_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap *tcsr_cc_pineapple_clocks[] = {
	[TCSR_PCIE_0_CLKREF_EN] = &tcsr_pcie_0_clkref_en.clkr,
	[TCSR_PCIE_1_CLKREF_EN] = &tcsr_pcie_1_clkref_en.clkr,
	[TCSR_UFS_CLKREF_EN] = &tcsr_ufs_clkref_en.clkr,
	[TCSR_UFS_PAD_CLKREF_EN] = &tcsr_ufs_pad_clkref_en.clkr,
	[TCSR_USB2_CLKREF_EN] = &tcsr_usb2_clkref_en.clkr,
	[TCSR_USB3_CLKREF_EN] = &tcsr_usb3_clkref_en.clkr,
};

static const struct regmap_config tcsr_cc_pineapple_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xbb000,
	.fast_io = true,
};

static const struct qcom_cc_desc tcsr_cc_pineapple_desc = {
	.config = &tcsr_cc_pineapple_regmap_config,
	.clks = tcsr_cc_pineapple_clocks,
	.num_clks = ARRAY_SIZE(tcsr_cc_pineapple_clocks),
};

static const struct of_device_id tcsr_cc_pineapple_match_table[] = {
	{ .compatible = "qcom,pineapple-tcsrcc" },
	{ .compatible = "qcom,volcano-tcsrcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, tcsr_cc_pineapple_match_table);

static int tcsr_cc_volcano_fixup(struct platform_device *pdev)
{
	const char *compat = NULL;
	int compatlen = 0;

	compat = of_get_property(pdev->dev.of_node, "compatible", &compatlen);
	if (!compat || compatlen <= 0)
		return -EINVAL;

	if (strcmp(compat, "qcom,volcano-tcsrcc"))
		return 0;

	tcsr_ufs_clkref_en.halt_reg = 0xb1118;
	tcsr_ufs_clkref_en.clkr.enable_reg = 0xb1118;
	tcsr_cc_pineapple_clocks[TCSR_USB2_CLKREF_EN] = NULL;
	tcsr_cc_pineapple_clocks[TCSR_USB3_CLKREF_EN] = NULL;

	return 0;
}

static int tcsr_cc_pineapple_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret;

	regmap = qcom_cc_map(pdev, &tcsr_cc_pineapple_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ret = tcsr_cc_volcano_fixup(pdev);
	if (ret)
		return ret;

	ret = qcom_cc_really_probe(pdev, &tcsr_cc_pineapple_desc, regmap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register TCSR CC clocks\n");
		return ret;
	}

	dev_info(&pdev->dev, "Registered TCSR CC clocks\n");

	return ret;
}

static void tcsr_cc_pineapple_sync_state(struct device *dev)
{
	qcom_cc_sync_state(dev, &tcsr_cc_pineapple_desc);
}

static struct platform_driver tcsr_cc_pineapple_driver = {
	.probe = tcsr_cc_pineapple_probe,
	.driver = {
		.name = "tcsr_cc-pineapple",
		.of_match_table = tcsr_cc_pineapple_match_table,
		.sync_state = tcsr_cc_pineapple_sync_state,
	},
};

static int __init tcsr_cc_pineapple_init(void)
{
	return platform_driver_register(&tcsr_cc_pineapple_driver);
}
subsys_initcall(tcsr_cc_pineapple_init);

static void __exit tcsr_cc_pineapple_exit(void)
{
	platform_driver_unregister(&tcsr_cc_pineapple_driver);
}
module_exit(tcsr_cc_pineapple_exit);

MODULE_DESCRIPTION("QTI TCSR_CC PINEAPPLE Driver");
MODULE_LICENSE("GPL v2");
