// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,nord-tcsrcc.h>

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

static struct clk_branch tcsr_dp_rx_0_clkref_en = {
	.halt_reg = 0xa008,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0xa008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_dp_rx_0_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_dp_rx_1_clkref_en = {
	.halt_reg = 0xb008,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0xb008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_dp_rx_1_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_dp_tx_0_clkref_en = {
	.halt_reg = 0xc008,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0xc008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_dp_tx_0_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_dp_tx_1_clkref_en = {
	.halt_reg = 0xd008,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0xd008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_dp_tx_1_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_dp_tx_2_clkref_en = {
	.halt_reg = 0xe008,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0xe008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_dp_tx_2_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_dp_tx_3_clkref_en = {
	.halt_reg = 0xf008,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0xf008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_dp_tx_3_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_pcie_clkref_en = {
	.halt_reg = 0x8,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_pcie_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_ufs_clkref_en = {
	.halt_reg = 0x3008,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x3008,
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

static struct clk_branch tcsr_usb2_0_clkref_en = {
	.halt_reg = 0x4008,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x4008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_usb2_0_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_usb2_1_clkref_en = {
	.halt_reg = 0x5008,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x5008,
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
	.halt_reg = 0x6008,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x6008,
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

static struct clk_branch tcsr_usb3_0_clkref_en = {
	.halt_reg = 0x8008,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x8008,
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
	.halt_reg = 0x7008,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x7008,
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

static struct clk_branch tcsr_ux_sgmii_0_clkref_en = {
	.halt_reg = 0x1008,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x1008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_ux_sgmii_0_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch tcsr_ux_sgmii_1_clkref_en = {
	.halt_reg = 0x2008,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x2008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "tcsr_ux_sgmii_1_clkref_en",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO_PAD,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap *tcsr_cc_nord_clocks[] = {
	[TCSR_DP_RX_0_CLKREF_EN] = &tcsr_dp_rx_0_clkref_en.clkr,
	[TCSR_DP_RX_1_CLKREF_EN] = &tcsr_dp_rx_1_clkref_en.clkr,
	[TCSR_DP_TX_0_CLKREF_EN] = &tcsr_dp_tx_0_clkref_en.clkr,
	[TCSR_DP_TX_1_CLKREF_EN] = &tcsr_dp_tx_1_clkref_en.clkr,
	[TCSR_DP_TX_2_CLKREF_EN] = &tcsr_dp_tx_2_clkref_en.clkr,
	[TCSR_DP_TX_3_CLKREF_EN] = &tcsr_dp_tx_3_clkref_en.clkr,
	[TCSR_PCIE_CLKREF_EN] = &tcsr_pcie_clkref_en.clkr,
	[TCSR_UFS_CLKREF_EN] = &tcsr_ufs_clkref_en.clkr,
	[TCSR_USB2_0_CLKREF_EN] = &tcsr_usb2_0_clkref_en.clkr,
	[TCSR_USB2_1_CLKREF_EN] = &tcsr_usb2_1_clkref_en.clkr,
	[TCSR_USB2_2_CLKREF_EN] = &tcsr_usb2_2_clkref_en.clkr,
	[TCSR_USB3_0_CLKREF_EN] = &tcsr_usb3_0_clkref_en.clkr,
	[TCSR_USB3_1_CLKREF_EN] = &tcsr_usb3_1_clkref_en.clkr,
	[TCSR_UX_SGMII_0_CLKREF_EN] = &tcsr_ux_sgmii_0_clkref_en.clkr,
	[TCSR_UX_SGMII_1_CLKREF_EN] = &tcsr_ux_sgmii_1_clkref_en.clkr,
};

static const struct regmap_config tcsr_cc_nord_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xf008,
	.fast_io = true,
};

static const struct qcom_cc_desc tcsr_cc_nord_desc = {
	.config = &tcsr_cc_nord_regmap_config,
	.clks = tcsr_cc_nord_clocks,
	.num_clks = ARRAY_SIZE(tcsr_cc_nord_clocks),
};

static const struct of_device_id tcsr_cc_nord_match_table[] = {
	{ .compatible = "qcom,nord-tcsrcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, tcsr_cc_nord_match_table);

static int tcsr_cc_nord_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &tcsr_cc_nord_desc);
}

static struct platform_driver tcsr_cc_nord_driver = {
	.probe = tcsr_cc_nord_probe,
	.driver = {
		.name = "tcsrcc-nord",
		.of_match_table = tcsr_cc_nord_match_table,
	},
};

module_platform_driver(tcsr_cc_nord_driver);

MODULE_DESCRIPTION("QTI TCSRCC NORD Driver");
MODULE_LICENSE("GPL");
