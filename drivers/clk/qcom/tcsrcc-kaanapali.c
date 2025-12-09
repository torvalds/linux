// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,sm8750-tcsr.h>

#include "clk-branch.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "common.h"

enum {
	DT_BI_TCXO_PAD,
};

static struct clk_branch tcsr_pcie_0_clkref_en = {
	.halt_reg = 0x15044,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x15044,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_pcie_0_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_usb3_clkref_en = {
	.halt_reg = 0x1504c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x1504c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_usb3_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_ufs_clkref_en = {
	.halt_reg = 0x15054,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x15054,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_ufs_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_usb2_clkref_en = {
	.halt_reg = 0x1505c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x1505c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_usb2_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap *tcsr_cc_kaanapali_clocks[] = {
	[TCSR_PCIE_0_CLKREF_EN] = &tcsr_pcie_0_clkref_en.clkr,
	[TCSR_UFS_CLKREF_EN] = &tcsr_ufs_clkref_en.clkr,
	[TCSR_USB2_CLKREF_EN] = &tcsr_usb2_clkref_en.clkr,
	[TCSR_USB3_CLKREF_EN] = &tcsr_usb3_clkref_en.clkr,
};

static const struct regmap_config tcsr_cc_kaanapali_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x3d000,
	.fast_io = true,
};

static const struct qcom_cc_desc tcsr_cc_kaanapali_desc = {
	.config = &tcsr_cc_kaanapali_regmap_config,
	.clks = tcsr_cc_kaanapali_clocks,
	.num_clks = ARRAY_SIZE(tcsr_cc_kaanapali_clocks),
};

static const struct of_device_id tcsr_cc_kaanapali_match_table[] = {
	{ .compatible = "qcom,kaanapali-tcsr" },
	{ }
};
MODULE_DEVICE_TABLE(of, tcsr_cc_kaanapali_match_table);

static int tcsr_cc_kaanapali_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &tcsr_cc_kaanapali_desc);
}

static struct platform_driver tcsr_cc_kaanapali_driver = {
	.probe = tcsr_cc_kaanapali_probe,
	.driver = {
		.name = "tcsr_cc-kaanapali",
		.of_match_table = tcsr_cc_kaanapali_match_table,
	},
};

static int __init tcsr_cc_kaanapali_init(void)
{
	return platform_driver_register(&tcsr_cc_kaanapali_driver);
}
subsys_initcall(tcsr_cc_kaanapali_init);

static void __exit tcsr_cc_kaanapali_exit(void)
{
	platform_driver_unregister(&tcsr_cc_kaanapali_driver);
}
module_exit(tcsr_cc_kaanapali_exit);

MODULE_DESCRIPTION("QTI TCSR_CC Kaanapali Driver");
MODULE_LICENSE("GPL");
