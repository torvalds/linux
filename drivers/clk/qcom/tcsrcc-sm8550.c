// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2022, Linaro Limited
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,sm8550-tcsr.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "common.h"
#include "reset.h"

enum {
	DT_BI_TCXO_PAD,
};

static struct clk_branch tcsr_pcie_0_clkref_en = {
	.halt_reg = 0x15100,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x15100,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "tcsr_pcie_0_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_pcie_1_clkref_en = {
	.halt_reg = 0x15114,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x15114,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "tcsr_pcie_1_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_ufs_clkref_en = {
	.halt_reg = 0x15110,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x15110,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "tcsr_ufs_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_ufs_pad_clkref_en = {
	.halt_reg = 0x15104,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x15104,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "tcsr_ufs_pad_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_usb2_clkref_en = {
	.halt_reg = 0x15118,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x15118,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "tcsr_usb2_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_usb3_clkref_en = {
	.halt_reg = 0x15108,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x15108,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "tcsr_usb3_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap *tcsr_cc_sm8550_clocks[] = {
	[TCSR_PCIE_0_CLKREF_EN] = &tcsr_pcie_0_clkref_en.clkr,
	[TCSR_PCIE_1_CLKREF_EN] = &tcsr_pcie_1_clkref_en.clkr,
	[TCSR_UFS_CLKREF_EN] = &tcsr_ufs_clkref_en.clkr,
	[TCSR_UFS_PAD_CLKREF_EN] = &tcsr_ufs_pad_clkref_en.clkr,
	[TCSR_USB2_CLKREF_EN] = &tcsr_usb2_clkref_en.clkr,
	[TCSR_USB3_CLKREF_EN] = &tcsr_usb3_clkref_en.clkr,
};

static const struct regmap_config tcsr_cc_sm8550_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x2f000,
	.fast_io = true,
};

static const struct qcom_cc_desc tcsr_cc_sm8550_desc = {
	.config = &tcsr_cc_sm8550_regmap_config,
	.clks = tcsr_cc_sm8550_clocks,
	.num_clks = ARRAY_SIZE(tcsr_cc_sm8550_clocks),
};

static const struct of_device_id tcsr_cc_sm8550_match_table[] = {
	{ .compatible = "qcom,sm8550-tcsr" },
	{ }
};
MODULE_DEVICE_TABLE(of, tcsr_cc_sm8550_match_table);

static int tcsr_cc_sm8550_probe(struct platform_device *pdev)
{
	struct regmap *regmap;

	regmap = qcom_cc_map(pdev, &tcsr_cc_sm8550_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return qcom_cc_really_probe(pdev, &tcsr_cc_sm8550_desc, regmap);
}

static struct platform_driver tcsr_cc_sm8550_driver = {
	.probe = tcsr_cc_sm8550_probe,
	.driver = {
		.name = "tcsr_cc-sm8550",
		.of_match_table = tcsr_cc_sm8550_match_table,
	},
};

static int __init tcsr_cc_sm8550_init(void)
{
	return platform_driver_register(&tcsr_cc_sm8550_driver);
}
subsys_initcall(tcsr_cc_sm8550_init);

static void __exit tcsr_cc_sm8550_exit(void)
{
	platform_driver_unregister(&tcsr_cc_sm8550_driver);
}
module_exit(tcsr_cc_sm8550_exit);

MODULE_DESCRIPTION("QTI TCSRCC SM8550 Driver");
MODULE_LICENSE("GPL");
