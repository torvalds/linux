// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2023, Linaro Limited
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,x1e80100-tcsr.h>

#include "clk-branch.h"
#include "clk-regmap.h"
#include "common.h"
#include "reset.h"

enum {
	DT_BI_TCXO_PAD,
};

static struct clk_branch tcsr_edp_clkref_en = {
	.halt_reg = 0x15130,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x15130,
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

static struct clk_branch tcsr_pcie_2l_4_clkref_en = {
	.halt_reg = 0x15100,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x15100,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "tcsr_pcie_2l_4_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_pcie_2l_5_clkref_en = {
	.halt_reg = 0x15104,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x15104,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "tcsr_pcie_2l_5_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_pcie_8l_clkref_en = {
	.halt_reg = 0x15108,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x15108,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "tcsr_pcie_8l_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_usb3_mp0_clkref_en = {
	.halt_reg = 0x1510c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x1510c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "tcsr_usb3_mp0_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_usb3_mp1_clkref_en = {
	.halt_reg = 0x15110,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x15110,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "tcsr_usb3_mp1_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_usb2_1_clkref_en = {
	.halt_reg = 0x15114,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x15114,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "tcsr_usb2_1_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_ufs_phy_clkref_en = {
	.halt_reg = 0x15118,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x15118,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "tcsr_ufs_phy_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_usb4_1_clkref_en = {
	.halt_reg = 0x15120,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x15120,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
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
	.halt_reg = 0x15124,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x15124,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "tcsr_usb4_2_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_usb2_2_clkref_en = {
	.halt_reg = 0x15128,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x15128,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "tcsr_usb2_2_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_pcie_4l_clkref_en = {
	.halt_reg = 0x1512c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x1512c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "tcsr_pcie_4l_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap *tcsr_cc_x1e80100_clocks[] = {
	[TCSR_EDP_CLKREF_EN] = &tcsr_edp_clkref_en.clkr,
	[TCSR_PCIE_2L_4_CLKREF_EN] = &tcsr_pcie_2l_4_clkref_en.clkr,
	[TCSR_PCIE_2L_5_CLKREF_EN] = &tcsr_pcie_2l_5_clkref_en.clkr,
	[TCSR_PCIE_8L_CLKREF_EN] = &tcsr_pcie_8l_clkref_en.clkr,
	[TCSR_USB3_MP0_CLKREF_EN] = &tcsr_usb3_mp0_clkref_en.clkr,
	[TCSR_USB3_MP1_CLKREF_EN] = &tcsr_usb3_mp1_clkref_en.clkr,
	[TCSR_USB2_1_CLKREF_EN] = &tcsr_usb2_1_clkref_en.clkr,
	[TCSR_UFS_PHY_CLKREF_EN] = &tcsr_ufs_phy_clkref_en.clkr,
	[TCSR_USB4_1_CLKREF_EN] = &tcsr_usb4_1_clkref_en.clkr,
	[TCSR_USB4_2_CLKREF_EN] = &tcsr_usb4_2_clkref_en.clkr,
	[TCSR_USB2_2_CLKREF_EN] = &tcsr_usb2_2_clkref_en.clkr,
	[TCSR_PCIE_4L_CLKREF_EN] = &tcsr_pcie_4l_clkref_en.clkr,
};

static const struct regmap_config tcsr_cc_x1e80100_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x2f000,
	.fast_io = true,
};

static const struct qcom_cc_desc tcsr_cc_x1e80100_desc = {
	.config = &tcsr_cc_x1e80100_regmap_config,
	.clks = tcsr_cc_x1e80100_clocks,
	.num_clks = ARRAY_SIZE(tcsr_cc_x1e80100_clocks),
};

static const struct of_device_id tcsr_cc_x1e80100_match_table[] = {
	{ .compatible = "qcom,x1e80100-tcsr" },
	{ }
};
MODULE_DEVICE_TABLE(of, tcsr_cc_x1e80100_match_table);

static int tcsr_cc_x1e80100_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &tcsr_cc_x1e80100_desc);
}

static struct platform_driver tcsr_cc_x1e80100_driver = {
	.probe = tcsr_cc_x1e80100_probe,
	.driver = {
		.name = "tcsrcc-x1e80100",
		.of_match_table = tcsr_cc_x1e80100_match_table,
	},
};

static int __init tcsr_cc_x1e80100_init(void)
{
	return platform_driver_register(&tcsr_cc_x1e80100_driver);
}
subsys_initcall(tcsr_cc_x1e80100_init);

static void __exit tcsr_cc_x1e80100_exit(void)
{
	platform_driver_unregister(&tcsr_cc_x1e80100_driver);
}
module_exit(tcsr_cc_x1e80100_exit);

MODULE_DESCRIPTION("QTI TCSR Clock Controller X1E80100 Driver");
MODULE_LICENSE("GPL");
