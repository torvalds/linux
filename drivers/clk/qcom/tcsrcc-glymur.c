// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,glymur-tcsr.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "common.h"
#include "gdsc.h"
#include "reset.h"

enum {
	DT_BI_TCXO_PAD,
};

static struct clk_branch tcsr_edp_clkref_en = {
	.halt_reg = 0x1c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x1c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_edp_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_pcie_1_clkref_en = {
	.halt_reg = 0x4,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_pcie_1_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_pcie_2_clkref_en = {
	.halt_reg = 0x8,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_pcie_2_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_pcie_3_clkref_en = {
	.halt_reg = 0x10,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x10,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_pcie_3_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_pcie_4_clkref_en = {
	.halt_reg = 0x14,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x14,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_pcie_4_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_usb2_1_clkref_en = {
	.halt_reg = 0x28,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x28,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_usb2_1_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_usb2_2_clkref_en = {
	.halt_reg = 0x2c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x2c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_usb2_2_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_usb2_3_clkref_en = {
	.halt_reg = 0x30,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x30,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_usb2_3_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_usb2_4_clkref_en = {
	.halt_reg = 0x44,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x44,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_usb2_4_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_usb3_0_clkref_en = {
	.halt_reg = 0x20,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x20,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_usb3_0_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_usb3_1_clkref_en = {
	.halt_reg = 0x24,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x24,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_usb3_1_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_usb4_1_clkref_en = {
	.halt_reg = 0x0,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_usb4_1_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_usb4_2_clkref_en = {
	.halt_reg = 0x18,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x18,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_usb4_2_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap *tcsr_cc_glymur_clocks[] = {
	[TCSR_EDP_CLKREF_EN] = &tcsr_edp_clkref_en.clkr,
	[TCSR_PCIE_1_CLKREF_EN] = &tcsr_pcie_1_clkref_en.clkr,
	[TCSR_PCIE_2_CLKREF_EN] = &tcsr_pcie_2_clkref_en.clkr,
	[TCSR_PCIE_3_CLKREF_EN] = &tcsr_pcie_3_clkref_en.clkr,
	[TCSR_PCIE_4_CLKREF_EN] = &tcsr_pcie_4_clkref_en.clkr,
	[TCSR_USB2_1_CLKREF_EN] = &tcsr_usb2_1_clkref_en.clkr,
	[TCSR_USB2_2_CLKREF_EN] = &tcsr_usb2_2_clkref_en.clkr,
	[TCSR_USB2_3_CLKREF_EN] = &tcsr_usb2_3_clkref_en.clkr,
	[TCSR_USB2_4_CLKREF_EN] = &tcsr_usb2_4_clkref_en.clkr,
	[TCSR_USB3_0_CLKREF_EN] = &tcsr_usb3_0_clkref_en.clkr,
	[TCSR_USB3_1_CLKREF_EN] = &tcsr_usb3_1_clkref_en.clkr,
	[TCSR_USB4_1_CLKREF_EN] = &tcsr_usb4_1_clkref_en.clkr,
	[TCSR_USB4_2_CLKREF_EN] = &tcsr_usb4_2_clkref_en.clkr,
};

static const struct regmap_config tcsr_cc_glymur_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x44,
	.fast_io = true,
};

static const struct qcom_cc_desc tcsr_cc_glymur_desc = {
	.config = &tcsr_cc_glymur_regmap_config,
	.clks = tcsr_cc_glymur_clocks,
	.num_clks = ARRAY_SIZE(tcsr_cc_glymur_clocks),
};

static const struct of_device_id tcsr_cc_glymur_match_table[] = {
	{ .compatible = "qcom,glymur-tcsr" },
	{ }
};
MODULE_DEVICE_TABLE(of, tcsr_cc_glymur_match_table);

static int tcsr_cc_glymur_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &tcsr_cc_glymur_desc);
}

static struct platform_driver tcsr_cc_glymur_driver = {
	.probe = tcsr_cc_glymur_probe,
	.driver = {
		.name = "tcsrcc-glymur",
		.of_match_table = tcsr_cc_glymur_match_table,
	},
};

static int __init tcsr_cc_glymur_init(void)
{
	return platform_driver_register(&tcsr_cc_glymur_driver);
}
subsys_initcall(tcsr_cc_glymur_init);

static void __exit tcsr_cc_glymur_exit(void)
{
	platform_driver_unregister(&tcsr_cc_glymur_driver);
}
module_exit(tcsr_cc_glymur_exit);

MODULE_DESCRIPTION("QTI TCSRCC GLYMUR Driver");
MODULE_LICENSE("GPL");
