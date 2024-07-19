// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/phy.h>
#include <linux/mdio.h>
#include <linux/clk.h>
#include <linux/gpio/consumer.h>

#include <dt-bindings/clock/qcom,qca8k-nsscc.h>
#include <dt-bindings/reset/qcom,qca8k-nsscc.h>

#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "common.h"
#include "reset.h"

#define QCA8K_CLK_REG_BASE		0x800000
#define QCA8K_HIGH_ADDR_PREFIX		0x18
#define QCA8K_LOW_ADDR_PREFIX		0x10
#define QCA8K_CFG_PAGE_REG		0xc
#define QCA8K_CLK_REG_MASK		GENMASK(4, 0)
#define QCA8K_CLK_PHY_ADDR_MASK		GENMASK(7, 5)
#define QCA8K_CLK_PAGE_MASK		GENMASK(23, 8)
#define QCA8K_REG_DATA_UPPER_16_BITS	BIT(1)

enum {
	DT_XO,
	DT_UNIPHY0_RX_CLK,
	DT_UNIPHY0_TX_CLK,
	DT_UNIPHY1_RX_CLK,
	DT_UNIPHY1_TX_CLK,
	DT_UNIPHY1_RX312P5M_CLK,
	DT_UNIPHY1_TX312P5M_CLK,
};

enum {
	P_XO,
	P_UNIPHY0_RX,
	P_UNIPHY0_TX,
	P_UNIPHY1_RX,
	P_UNIPHY1_TX,
	P_UNIPHY1_RX312P5M,
	P_UNIPHY1_TX312P5M,
	P_MAC4_RX_DIV,
	P_MAC4_TX_DIV,
	P_MAC5_RX_DIV,
	P_MAC5_TX_DIV,
};

static const struct clk_parent_data nss_cc_uniphy1_tx312p5m_data[] = {
	{ .index = DT_XO },
	{ .index = DT_UNIPHY1_TX312P5M_CLK },
};

static const struct parent_map nss_cc_uniphy1_tx312p5m_map[] = {
	{ P_XO, 0 },
	{ P_UNIPHY1_TX312P5M, 1 },
};

static struct clk_rcg2 nss_cc_switch_core_clk_src = {
	.cmd_rcgr = 0x0,
	.hid_width = 5,
	.parent_map = nss_cc_uniphy1_tx312p5m_map,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nss_cc_switch_core_clk_src",
		.parent_data = nss_cc_uniphy1_tx312p5m_data,
		.num_parents = ARRAY_SIZE(nss_cc_uniphy1_tx312p5m_data),
		.ops = &clk_rcg2_mux_closest_ops,
	},
};

static struct clk_branch nss_cc_switch_core_clk = {
	.halt_reg = 0x8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_switch_core_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_switch_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_apb_bridge_clk = {
	.halt_reg = 0x10,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_apb_bridge_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_switch_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static const struct clk_parent_data nss_cc_uniphy1_tx_data[] = {
	{ .index = DT_XO },
	{ .index = DT_UNIPHY1_TX_CLK },
};

static const struct parent_map nss_cc_uniphy1_tx_map[] = {
	{ P_XO, 0 },
	{ P_UNIPHY1_TX, 2 },
};

static struct clk_rcg2 nss_cc_mac0_tx_clk_src = {
	.cmd_rcgr = 0x14,
	.hid_width = 5,
	.parent_map = nss_cc_uniphy1_tx_map,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nss_cc_mac0_tx_clk_src",
		.parent_data = nss_cc_uniphy1_tx_data,
		.num_parents = ARRAY_SIZE(nss_cc_uniphy1_tx_data),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_mux_closest_ops,
	},
};

static struct clk_regmap_div nss_cc_mac0_tx_div_clk_src = {
	.reg = 0x1c,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac0_tx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac0_tx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_regmap_div_ops,
		},
	},
};

static struct clk_branch nss_cc_mac0_tx_clk = {
	.halt_reg = 0x20,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x20,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac0_tx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac0_tx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mac0_tx_srds1_clk = {
	.halt_reg = 0x24,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x24,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac0_tx_srds1_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac0_tx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static const struct clk_parent_data nss_cc_uniphy1_rx_tx_data[] = {
	{ .index = DT_XO },
	{ .index = DT_UNIPHY1_RX_CLK },
	{ .index = DT_UNIPHY1_TX_CLK },
};

static const struct parent_map nss_cc_uniphy1_rx_tx_map[] = {
	{ P_XO, 0 },
	{ P_UNIPHY1_RX, 1 },
	{ P_UNIPHY1_TX, 2 },
};

static struct clk_rcg2 nss_cc_mac0_rx_clk_src = {
	.cmd_rcgr = 0x28,
	.hid_width = 5,
	.parent_map = nss_cc_uniphy1_rx_tx_map,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nss_cc_mac0_rx_clk_src",
		.parent_data = nss_cc_uniphy1_rx_tx_data,
		.num_parents = ARRAY_SIZE(nss_cc_uniphy1_rx_tx_data),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_mux_closest_ops,
	},
};

static struct clk_regmap_div nss_cc_mac0_rx_div_clk_src = {
	.reg = 0x30,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac0_rx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac0_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_regmap_div_ops,
		},
	},
};

static struct clk_branch nss_cc_mac0_rx_clk = {
	.halt_reg = 0x34,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x34,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac0_rx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac0_rx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mac0_rx_srds1_clk = {
	.halt_reg = 0x3c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac0_rx_srds1_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac0_rx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static const struct clk_parent_data nss_cc_uniphy1_rx_tx312p5m_data[] = {
	{ .index = DT_XO },
	{ .index = DT_UNIPHY1_TX312P5M_CLK },
	{ .index = DT_UNIPHY1_RX312P5M_CLK },
};

static const struct parent_map nss_cc_uniphy1_rx_tx312p5m_map[] = {
	{ P_XO, 0 },
	{ P_UNIPHY1_TX312P5M, 6 },
	{ P_UNIPHY1_RX312P5M, 7 },
};

static const struct freq_conf ftbl_nss_cc_mac1_tx_clk_src_25[] = {
	C(P_UNIPHY1_TX312P5M, 12.5, 0, 0),
	C(P_UNIPHY1_RX312P5M, 12.5, 0, 0),
};

static const struct freq_conf ftbl_nss_cc_mac1_tx_clk_src_125[] = {
	C(P_UNIPHY1_TX312P5M, 2.5, 0, 0),
	C(P_UNIPHY1_RX312P5M, 2.5, 0, 0),
};

static const struct freq_conf ftbl_nss_cc_mac1_tx_clk_src_312p5[] = {
	C(P_UNIPHY1_TX312P5M, 1, 0, 0),
	C(P_UNIPHY1_RX312P5M, 1, 0, 0),
};

static const struct freq_multi_tbl ftbl_nss_cc_mac1_tx_clk_src[] = {
	FM(25000000, ftbl_nss_cc_mac1_tx_clk_src_25),
	FMS(50000000, P_XO, 1, 0, 0),
	FM(125000000, ftbl_nss_cc_mac1_tx_clk_src_125),
	FM(312500000, ftbl_nss_cc_mac1_tx_clk_src_312p5),
	{ }
};

static struct clk_rcg2 nss_cc_mac1_tx_clk_src = {
	.cmd_rcgr = 0x40,
	.freq_multi_tbl = ftbl_nss_cc_mac1_tx_clk_src,
	.hid_width = 5,
	.parent_map = nss_cc_uniphy1_rx_tx312p5m_map,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nss_cc_mac1_tx_clk_src",
		.parent_data = nss_cc_uniphy1_rx_tx312p5m_data,
		.num_parents = ARRAY_SIZE(nss_cc_uniphy1_rx_tx312p5m_data),
		.ops = &clk_rcg2_fm_ops,
	},
};

static struct clk_regmap_div nss_cc_mac1_tx_div_clk_src = {
	.reg = 0x48,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac1_tx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac1_tx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_regmap_div_ops,
		},
	},
};

static struct clk_regmap_div nss_cc_mac1_srds1_ch0_xgmii_rx_div_clk_src = {
	.reg = 0x4c,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac1_srds1_ch0_xgmii_rx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac1_tx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_regmap_div_ops,
		},
	},
};

static struct clk_branch nss_cc_mac1_srds1_ch0_rx_clk = {
	.halt_reg = 0x50,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x50,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac1_srds1_ch0_rx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac1_tx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mac1_tx_clk = {
	.halt_reg = 0x54,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x54,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac1_tx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac1_tx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mac1_gephy0_tx_clk = {
	.halt_reg = 0x58,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x58,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac1_gephy0_tx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac1_tx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mac1_srds1_ch0_xgmii_rx_clk = {
	.halt_reg = 0x5c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac1_srds1_ch0_xgmii_rx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac1_srds1_ch0_xgmii_rx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static const struct clk_parent_data nss_cc_uniphy1_tx312p5m_prx_data[] = {
	{ .index = DT_XO },
	{ .index = DT_UNIPHY1_TX312P5M_CLK },
};

static const struct parent_map nss_cc_uniphy1_tx312p5m_prx_map[] = {
	{ P_XO, 0 },
	{ P_UNIPHY1_TX312P5M, 6 },
};

static const struct freq_tbl ftbl_nss_cc_mac1_rx_clk_src[] = {
	F(25000000, P_UNIPHY1_TX312P5M, 12.5, 0, 0),
	F(50000000, P_XO, 1, 0, 0),
	F(125000000, P_UNIPHY1_TX312P5M, 2.5, 0, 0),
	F(312500000, P_UNIPHY1_TX312P5M, 1, 0, 0),
	{ }
};

static struct clk_rcg2 nss_cc_mac1_rx_clk_src = {
	.cmd_rcgr = 0x60,
	.freq_tbl = ftbl_nss_cc_mac1_rx_clk_src,
	.hid_width = 5,
	.parent_map = nss_cc_uniphy1_tx312p5m_prx_map,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nss_cc_mac1_rx_clk_src",
		.parent_data = nss_cc_uniphy1_tx312p5m_prx_data,
		.num_parents = ARRAY_SIZE(nss_cc_uniphy1_tx312p5m_prx_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div nss_cc_mac1_rx_div_clk_src = {
	.reg = 0x68,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac1_rx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac1_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_regmap_div_ops,
		},
	},
};

static struct clk_regmap_div nss_cc_mac1_srds1_ch0_xgmii_tx_div_clk_src = {
	.reg = 0x6c,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac1_srds1_ch0_xgmii_tx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac1_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_regmap_div_ops,
		},
	},
};

static struct clk_branch nss_cc_mac1_srds1_ch0_tx_clk = {
	.halt_reg = 0x70,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x70,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac1_srds1_ch0_tx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac1_rx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mac1_rx_clk = {
	.halt_reg = 0x74,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x74,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac1_rx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac1_rx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mac1_gephy0_rx_clk = {
	.halt_reg = 0x78,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x78,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac1_gephy0_rx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac1_rx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mac1_srds1_ch0_xgmii_tx_clk = {
	.halt_reg = 0x7c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x7c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac1_srds1_ch0_xgmii_tx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac1_srds1_ch0_xgmii_tx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_rcg2 nss_cc_mac2_tx_clk_src = {
	.cmd_rcgr = 0x80,
	.freq_multi_tbl = ftbl_nss_cc_mac1_tx_clk_src,
	.hid_width = 5,
	.parent_map = nss_cc_uniphy1_rx_tx312p5m_map,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nss_cc_mac2_tx_clk_src",
		.parent_data = nss_cc_uniphy1_rx_tx312p5m_data,
		.num_parents = ARRAY_SIZE(nss_cc_uniphy1_rx_tx312p5m_data),
		.ops = &clk_rcg2_fm_ops,
	},
};

static struct clk_regmap_div nss_cc_mac2_tx_div_clk_src = {
	.reg = 0x88,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac2_tx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac2_tx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_regmap_div_ops,
		},
	},
};

static struct clk_regmap_div nss_cc_mac2_srds1_ch1_xgmii_rx_div_clk_src = {
	.reg = 0x8c,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac2_srds1_ch1_xgmii_rx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac2_tx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_regmap_div_ops,
		},
	},
};

static struct clk_branch nss_cc_mac2_srds1_ch1_rx_clk = {
	.halt_reg = 0x90,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x90,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac2_srds1_ch1_rx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac2_tx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mac2_tx_clk = {
	.halt_reg = 0x94,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x94,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac2_tx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac2_tx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mac2_gephy1_tx_clk = {
	.halt_reg = 0x98,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x98,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac2_gephy1_tx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac2_tx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mac2_srds1_ch1_xgmii_rx_clk = {
	.halt_reg = 0x9c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac2_srds1_ch1_xgmii_rx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac2_srds1_ch1_xgmii_rx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_rcg2 nss_cc_mac2_rx_clk_src = {
	.cmd_rcgr = 0xa0,
	.freq_tbl = ftbl_nss_cc_mac1_rx_clk_src,
	.hid_width = 5,
	.parent_map = nss_cc_uniphy1_tx312p5m_prx_map,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nss_cc_mac2_rx_clk_src",
		.parent_data = nss_cc_uniphy1_tx312p5m_prx_data,
		.num_parents = ARRAY_SIZE(nss_cc_uniphy1_tx312p5m_prx_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div nss_cc_mac2_rx_div_clk_src = {
	.reg = 0xa8,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac2_rx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac2_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_regmap_div_ops,
		},
	},
};

static struct clk_regmap_div nss_cc_mac2_srds1_ch1_xgmii_tx_div_clk_src = {
	.reg = 0xac,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac2_srds1_ch1_xgmii_tx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac2_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_regmap_div_ops,
		},
	},
};

static struct clk_branch nss_cc_mac2_srds1_ch1_tx_clk = {
	.halt_reg = 0xb0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac2_srds1_ch1_tx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac2_rx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mac2_rx_clk = {
	.halt_reg = 0xb4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac2_rx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac2_rx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mac2_gephy1_rx_clk = {
	.halt_reg = 0xb8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac2_gephy1_rx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac2_rx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mac2_srds1_ch1_xgmii_tx_clk = {
	.halt_reg = 0xbc,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xbc,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac2_srds1_ch1_xgmii_tx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac2_srds1_ch1_xgmii_tx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_rcg2 nss_cc_mac3_tx_clk_src = {
	.cmd_rcgr = 0xc0,
	.freq_multi_tbl = ftbl_nss_cc_mac1_tx_clk_src,
	.hid_width = 5,
	.parent_map = nss_cc_uniphy1_rx_tx312p5m_map,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nss_cc_mac3_tx_clk_src",
		.parent_data = nss_cc_uniphy1_rx_tx312p5m_data,
		.num_parents = ARRAY_SIZE(nss_cc_uniphy1_rx_tx312p5m_data),
		.ops = &clk_rcg2_fm_ops,
	},
};

static struct clk_regmap_div nss_cc_mac3_tx_div_clk_src = {
	.reg = 0xc8,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac3_tx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac3_tx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_regmap_div_ops,
		},
	},
};

static struct clk_regmap_div nss_cc_mac3_srds1_ch2_xgmii_rx_div_clk_src = {
	.reg = 0xcc,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac3_srds1_ch2_xgmii_rx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac3_tx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_regmap_div_ops,
		},
	},
};

static struct clk_branch nss_cc_mac3_srds1_ch2_rx_clk = {
	.halt_reg = 0xd0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xd0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac3_srds1_ch2_rx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac3_tx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mac3_tx_clk = {
	.halt_reg = 0xd4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xd4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac3_tx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac3_tx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mac3_gephy2_tx_clk = {
	.halt_reg = 0xd8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xd8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac3_gephy2_tx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac3_tx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mac3_srds1_ch2_xgmii_rx_clk = {
	.halt_reg = 0xdc,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xdc,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac3_srds1_ch2_xgmii_rx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac3_srds1_ch2_xgmii_rx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_rcg2 nss_cc_mac3_rx_clk_src = {
	.cmd_rcgr = 0xe0,
	.freq_tbl = ftbl_nss_cc_mac1_rx_clk_src,
	.hid_width = 5,
	.parent_map = nss_cc_uniphy1_tx312p5m_prx_map,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nss_cc_mac3_rx_clk_src",
		.parent_data = nss_cc_uniphy1_tx312p5m_prx_data,
		.num_parents = ARRAY_SIZE(nss_cc_uniphy1_tx312p5m_prx_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div nss_cc_mac3_rx_div_clk_src = {
	.reg = 0xe8,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac3_rx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac3_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_regmap_div_ops,
		},
	},
};

static struct clk_regmap_div nss_cc_mac3_srds1_ch2_xgmii_tx_div_clk_src = {
	.reg = 0xec,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac3_srds1_ch2_xgmii_tx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac3_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_regmap_div_ops,
		},
	},
};

static struct clk_branch nss_cc_mac3_srds1_ch2_tx_clk = {
	.halt_reg = 0xf0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac3_srds1_ch2_tx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac3_rx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mac3_rx_clk = {
	.halt_reg = 0xf4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac3_rx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac3_rx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mac3_gephy2_rx_clk = {
	.halt_reg = 0xf8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac3_gephy2_rx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac3_rx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mac3_srds1_ch2_xgmii_tx_clk = {
	.halt_reg = 0xfc,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xfc,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac3_srds1_ch2_xgmii_tx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac3_srds1_ch2_xgmii_tx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static const struct clk_parent_data nss_cc_uniphy0_rx_uniphy1_rx_tx312p5m_data[] = {
	{ .index = DT_XO },
	{ .index = DT_UNIPHY0_RX_CLK },
	{ .index = DT_UNIPHY1_TX312P5M_CLK },
	{ .index = DT_UNIPHY1_RX312P5M_CLK },
};

static const struct parent_map nss_cc_uniphy0_rx_uniphy1_rx_tx312p5m_map[] = {
	{ P_XO, 0 },
	{ P_UNIPHY0_RX, 1 },
	{ P_UNIPHY1_TX312P5M, 3 },
	{ P_UNIPHY1_RX312P5M, 7 },
};

static const struct freq_conf ftbl_nss_cc_mac4_tx_clk_src_25[] = {
	C(P_UNIPHY0_RX, 12.5, 0, 0),
	C(P_UNIPHY0_RX, 5, 0, 0),
	C(P_UNIPHY1_TX312P5M, 12.5, 0, 0),
	C(P_UNIPHY1_RX312P5M, 12.5, 0, 0),
};

static const struct freq_conf ftbl_nss_cc_mac4_tx_clk_src_125[] = {
	C(P_UNIPHY0_RX, 1, 0, 0),
	C(P_UNIPHY0_RX, 2.5, 0, 0),
	C(P_UNIPHY1_TX312P5M, 2.5, 0, 0),
	C(P_UNIPHY1_RX312P5M, 2.5, 0, 0),
};

static const struct freq_conf ftbl_nss_cc_mac4_tx_clk_src_312p5[] = {
	C(P_UNIPHY0_RX, 1, 0, 0),
	C(P_UNIPHY1_TX312P5M, 1, 0, 0),
	C(P_UNIPHY1_RX312P5M, 1, 0, 0),
};

static const struct freq_multi_tbl ftbl_nss_cc_mac4_tx_clk_src[] = {
	FM(25000000, ftbl_nss_cc_mac4_tx_clk_src_25),
	FMS(50000000, P_XO, 1, 0, 0),
	FM(125000000, ftbl_nss_cc_mac4_tx_clk_src_125),
	FM(312500000, ftbl_nss_cc_mac4_tx_clk_src_312p5),
	{ }
};

static struct clk_rcg2 nss_cc_mac4_tx_clk_src = {
	.cmd_rcgr = 0x100,
	.freq_multi_tbl = ftbl_nss_cc_mac4_tx_clk_src,
	.hid_width = 5,
	.parent_map = nss_cc_uniphy0_rx_uniphy1_rx_tx312p5m_map,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nss_cc_mac4_tx_clk_src",
		.parent_data = nss_cc_uniphy0_rx_uniphy1_rx_tx312p5m_data,
		.num_parents = ARRAY_SIZE(nss_cc_uniphy0_rx_uniphy1_rx_tx312p5m_data),
		.ops = &clk_rcg2_fm_ops,
	},
};

static struct clk_regmap_div nss_cc_mac4_tx_div_clk_src = {
	.reg = 0x108,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac4_tx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac4_tx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_regmap_div_ops,
		},
	},
};

static struct clk_regmap_div nss_cc_mac4_srds1_ch3_xgmii_rx_div_clk_src = {
	.reg = 0x10c,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac4_srds1_ch3_xgmii_rx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac4_tx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_regmap_div_ops,
		},
	},
};

static struct clk_branch nss_cc_mac4_srds1_ch3_rx_clk = {
	.halt_reg = 0x110,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x110,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac4_srds1_ch3_rx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac4_tx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mac4_tx_clk = {
	.halt_reg = 0x114,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x114,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac4_tx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac4_tx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mac4_gephy3_tx_clk = {
	.halt_reg = 0x118,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x118,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac4_gephy3_tx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac4_tx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mac4_srds1_ch3_xgmii_rx_clk = {
	.halt_reg = 0x11c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x11c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac4_srds1_ch3_xgmii_rx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac4_srds1_ch3_xgmii_rx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static const struct clk_parent_data nss_cc_uniphy0_tx_uniphy1_tx312p5m_data[] = {
	{ .index = DT_XO },
	{ .index = DT_UNIPHY0_TX_CLK },
	{ .index = DT_UNIPHY1_TX312P5M_CLK },
};

static const struct parent_map nss_cc_uniphy0_tx_uniphy1_tx312p5m_map[] = {
	{ P_XO, 0 },
	{ P_UNIPHY0_TX, 2 },
	{ P_UNIPHY1_TX312P5M, 3 },
};

static const struct freq_conf ftbl_nss_cc_mac4_rx_clk_src_25[] = {
	C(P_UNIPHY0_TX, 12.5, 0, 0),
	C(P_UNIPHY0_TX, 5, 0, 0),
	C(P_UNIPHY1_TX312P5M, 12.5, 0, 0),
};

static const struct freq_conf ftbl_nss_cc_mac4_rx_clk_src_125[] = {
	C(P_UNIPHY0_TX, 1, 0, 0),
	C(P_UNIPHY0_TX, 2.5, 0, 0),
	C(P_UNIPHY1_TX312P5M, 2.5, 0, 0),
};

static const struct freq_conf ftbl_nss_cc_mac4_rx_clk_src_312p5[] = {
	C(P_UNIPHY0_TX, 1, 0, 0),
	C(P_UNIPHY1_TX312P5M, 1, 0, 0),
};

static const struct freq_multi_tbl ftbl_nss_cc_mac4_rx_clk_src[] = {
	FM(25000000, ftbl_nss_cc_mac4_rx_clk_src_25),
	FMS(50000000, P_XO, 1, 0, 0),
	FM(125000000, ftbl_nss_cc_mac4_rx_clk_src_125),
	FM(312500000, ftbl_nss_cc_mac4_rx_clk_src_312p5),
	{ }
};

static struct clk_rcg2 nss_cc_mac4_rx_clk_src = {
	.cmd_rcgr = 0x120,
	.freq_multi_tbl = ftbl_nss_cc_mac4_rx_clk_src,
	.hid_width = 5,
	.parent_map = nss_cc_uniphy0_tx_uniphy1_tx312p5m_map,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nss_cc_mac4_rx_clk_src",
		.parent_data = nss_cc_uniphy0_tx_uniphy1_tx312p5m_data,
		.num_parents = ARRAY_SIZE(nss_cc_uniphy0_tx_uniphy1_tx312p5m_data),
		.ops = &clk_rcg2_fm_ops,
	},
};

static struct clk_regmap_div nss_cc_mac4_rx_div_clk_src = {
	.reg = 0x128,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac4_rx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac4_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_regmap_div_ops,
		},
	},
};

static struct clk_regmap_div nss_cc_mac4_srds1_ch3_xgmii_tx_div_clk_src = {
	.reg = 0x12c,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac4_srds1_ch3_xgmii_tx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac4_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_regmap_div_ops,
		},
	},
};

static struct clk_branch nss_cc_mac4_srds1_ch3_tx_clk = {
	.halt_reg = 0x130,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x130,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac4_srds1_ch3_tx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac4_rx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mac4_rx_clk = {
	.halt_reg = 0x134,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x134,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac4_rx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac4_rx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mac4_gephy3_rx_clk = {
	.halt_reg = 0x138,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x138,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac4_gephy3_rx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac4_rx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mac4_srds1_ch3_xgmii_tx_clk = {
	.halt_reg = 0x13c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac4_srds1_ch3_xgmii_tx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac4_srds1_ch3_xgmii_tx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static const struct clk_parent_data nss_cc_uniphy0_tx_data[] = {
	{ .index = DT_XO },
	{ .index = DT_UNIPHY0_TX_CLK },
};

static const struct parent_map nss_cc_uniphy0_tx_map[] = {
	{ P_XO, 0 },
	{ P_UNIPHY0_TX, 2 },
};

static struct clk_rcg2 nss_cc_mac5_tx_clk_src = {
	.cmd_rcgr = 0x140,
	.hid_width = 5,
	.parent_map = nss_cc_uniphy0_tx_map,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nss_cc_mac5_tx_clk_src",
		.parent_data = nss_cc_uniphy0_tx_data,
		.num_parents = ARRAY_SIZE(nss_cc_uniphy0_tx_data),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_mux_closest_ops,
	},
};

static struct clk_regmap_div nss_cc_mac5_tx_div_clk_src = {
	.reg = 0x148,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac5_tx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac5_tx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_regmap_div_ops,
		},
	},
};

static struct clk_branch nss_cc_mac5_tx_clk = {
	.halt_reg = 0x14c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x14c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac5_tx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac5_tx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static const struct clk_parent_data nss_cc_uniphy0_rx_tx_data[] = {
	{ .index = DT_XO },
	{ .index = DT_UNIPHY0_RX_CLK },
	{ .index = DT_UNIPHY0_TX_CLK },
};

static const struct parent_map nss_cc_uniphy0_rx_tx_map[] = {
	{ P_XO, 0 },
	{ P_UNIPHY0_RX, 1 },
	{ P_UNIPHY0_TX, 2 },
};

static struct clk_rcg2 nss_cc_mac5_rx_clk_src = {
	.cmd_rcgr = 0x154,
	.hid_width = 5,
	.parent_map = nss_cc_uniphy0_rx_tx_map,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nss_cc_mac5_rx_clk_src",
		.parent_data = nss_cc_uniphy0_rx_tx_data,
		.num_parents = ARRAY_SIZE(nss_cc_uniphy0_rx_tx_data),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_mux_closest_ops,
	},
};

static struct clk_regmap_div nss_cc_mac5_rx_div_clk_src = {
	.reg = 0x15c,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac5_rx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac5_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_regmap_div_ops,
		},
	},
};

static struct clk_branch nss_cc_mac5_rx_clk = {
	.halt_reg = 0x160,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x160,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac5_rx_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac5_rx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static const struct parent_map nss_cc_mac4_rx_div_mac5_tx_div_map[] = {
	{ P_MAC4_RX_DIV, 0 },
	{ P_MAC5_TX_DIV, 1 },
};

static struct clk_regmap_mux nss_cc_mac5_tx_srds0_clk_src = {
	.reg = 0x300,
	.shift = 0,
	.width = 1,
	.parent_map = nss_cc_mac4_rx_div_mac5_tx_div_map,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac5_tx_srds0_clk_src",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac4_rx_div_clk_src.clkr.hw,
				&nss_cc_mac5_tx_div_clk_src.clkr.hw,
			},
			.num_parents = 2,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_branch nss_cc_mac5_tx_srds0_clk = {
	.halt_reg = 0x150,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x150,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac5_tx_srds0_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac5_tx_srds0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static const struct parent_map nss_cc_mac4_tx_div_mac5_rx_div_map[] = {
	{ P_MAC4_TX_DIV, 0 },
	{ P_MAC5_RX_DIV, 1 },
};

static struct clk_regmap_mux nss_cc_mac5_rx_srds0_clk_src = {
	.reg = 0x300,
	.shift = 1,
	.width = 1,
	.parent_map = nss_cc_mac4_tx_div_mac5_rx_div_map,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac5_rx_srds0_clk_src",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac4_tx_div_clk_src.clkr.hw,
				&nss_cc_mac5_rx_div_clk_src.clkr.hw,
			},
			.num_parents = 2,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_branch nss_cc_mac5_rx_srds0_clk = {
	.halt_reg = 0x164,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x164,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mac5_rx_srds0_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_mac5_rx_srds0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static const struct parent_map nss_cc_uniphy1_tx312p5m_map2[] = {
	{ P_XO, 0 },
	{ P_UNIPHY1_TX312P5M, 2 },
};

static const struct freq_tbl ftbl_nss_cc_ahb_clk_src[] = {
	F(50000000, P_XO, 1, 0, 0),
	F(104170000, P_UNIPHY1_TX312P5M, 3, 0, 0),
	{ }
};

static struct clk_rcg2 nss_cc_ahb_clk_src = {
	.cmd_rcgr = 0x168,
	.freq_tbl = ftbl_nss_cc_ahb_clk_src,
	.hid_width = 5,
	.parent_map = nss_cc_uniphy1_tx312p5m_map2,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nss_cc_ahb_clk_src",
		.parent_data = nss_cc_uniphy1_tx312p5m_data,
		.num_parents = ARRAY_SIZE(nss_cc_uniphy1_tx312p5m_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch nss_cc_ahb_clk = {
	.halt_reg = 0x170,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x170,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_ahb_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_sec_ctrl_ahb_clk = {
	.halt_reg = 0x174,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x174,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_sec_ctrl_ahb_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_tlmm_clk = {
	.halt_reg = 0x178,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x178,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_tlmm_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_tlmm_ahb_clk = {
	.halt_reg = 0x190,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x190,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_tlmm_ahb_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_cnoc_ahb_clk = {
	.halt_reg = 0x194,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x194,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_cnoc_ahb_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mdio_ahb_clk = {
	.halt_reg = 0x198,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x198,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mdio_ahb_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_mdio_master_ahb_clk = {
	.halt_reg = 0x19c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x19c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_mdio_master_ahb_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static const struct clk_parent_data nss_cc_xo_data[] = {
	{ .index = DT_XO },
};

static const struct parent_map nss_cc_xo_map[] = {
	{ P_XO, 0 },
};

static const struct freq_tbl ftbl_nss_cc_sys_clk_src[] = {
	F(25000000, P_XO, 2, 0, 0),
	{ }
};

static struct clk_rcg2 nss_cc_sys_clk_src = {
	.cmd_rcgr = 0x1a0,
	.freq_tbl = ftbl_nss_cc_sys_clk_src,
	.hid_width = 5,
	.parent_map = nss_cc_xo_map,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nss_cc_sys_clk_src",
		.parent_data = nss_cc_xo_data,
		.num_parents = ARRAY_SIZE(nss_cc_xo_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch nss_cc_srds0_sys_clk = {
	.halt_reg = 0x1a8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1a8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_srds0_sys_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_sys_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_srds1_sys_clk = {
	.halt_reg = 0x1ac,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1ac,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_srds1_sys_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_sys_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_gephy0_sys_clk = {
	.halt_reg = 0x1b0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1b0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_gephy0_sys_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_sys_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_gephy1_sys_clk = {
	.halt_reg = 0x1b4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1b4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_gephy1_sys_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_sys_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_gephy2_sys_clk = {
	.halt_reg = 0x1b8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1b8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_gephy2_sys_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_sys_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_branch nss_cc_gephy3_sys_clk = {
	.halt_reg = 0x1bc,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1bc,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nss_cc_gephy3_sys_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&nss_cc_sys_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.ops = &clk_branch2_prepare_ops,
		},
	},
};

static struct clk_regmap *nss_cc_qca8k_clocks[] = {
	[NSS_CC_SWITCH_CORE_CLK_SRC] = &nss_cc_switch_core_clk_src.clkr,
	[NSS_CC_SWITCH_CORE_CLK] = &nss_cc_switch_core_clk.clkr,
	[NSS_CC_APB_BRIDGE_CLK] = &nss_cc_apb_bridge_clk.clkr,
	[NSS_CC_MAC0_TX_CLK_SRC] = &nss_cc_mac0_tx_clk_src.clkr,
	[NSS_CC_MAC0_TX_DIV_CLK_SRC] = &nss_cc_mac0_tx_div_clk_src.clkr,
	[NSS_CC_MAC0_TX_CLK] = &nss_cc_mac0_tx_clk.clkr,
	[NSS_CC_MAC0_TX_SRDS1_CLK] = &nss_cc_mac0_tx_srds1_clk.clkr,
	[NSS_CC_MAC0_RX_CLK_SRC] = &nss_cc_mac0_rx_clk_src.clkr,
	[NSS_CC_MAC0_RX_DIV_CLK_SRC] = &nss_cc_mac0_rx_div_clk_src.clkr,
	[NSS_CC_MAC0_RX_CLK] = &nss_cc_mac0_rx_clk.clkr,
	[NSS_CC_MAC0_RX_SRDS1_CLK] = &nss_cc_mac0_rx_srds1_clk.clkr,
	[NSS_CC_MAC1_TX_CLK_SRC] = &nss_cc_mac1_tx_clk_src.clkr,
	[NSS_CC_MAC1_TX_DIV_CLK_SRC] = &nss_cc_mac1_tx_div_clk_src.clkr,
	[NSS_CC_MAC1_SRDS1_CH0_XGMII_RX_DIV_CLK_SRC] =
		&nss_cc_mac1_srds1_ch0_xgmii_rx_div_clk_src.clkr,
	[NSS_CC_MAC1_SRDS1_CH0_RX_CLK] = &nss_cc_mac1_srds1_ch0_rx_clk.clkr,
	[NSS_CC_MAC1_TX_CLK] = &nss_cc_mac1_tx_clk.clkr,
	[NSS_CC_MAC1_GEPHY0_TX_CLK] = &nss_cc_mac1_gephy0_tx_clk.clkr,
	[NSS_CC_MAC1_SRDS1_CH0_XGMII_RX_CLK] = &nss_cc_mac1_srds1_ch0_xgmii_rx_clk.clkr,
	[NSS_CC_MAC1_RX_CLK_SRC] = &nss_cc_mac1_rx_clk_src.clkr,
	[NSS_CC_MAC1_RX_DIV_CLK_SRC] = &nss_cc_mac1_rx_div_clk_src.clkr,
	[NSS_CC_MAC1_SRDS1_CH0_XGMII_TX_DIV_CLK_SRC] =
		&nss_cc_mac1_srds1_ch0_xgmii_tx_div_clk_src.clkr,
	[NSS_CC_MAC1_SRDS1_CH0_TX_CLK] = &nss_cc_mac1_srds1_ch0_tx_clk.clkr,
	[NSS_CC_MAC1_RX_CLK] = &nss_cc_mac1_rx_clk.clkr,
	[NSS_CC_MAC1_GEPHY0_RX_CLK] = &nss_cc_mac1_gephy0_rx_clk.clkr,
	[NSS_CC_MAC1_SRDS1_CH0_XGMII_TX_CLK] = &nss_cc_mac1_srds1_ch0_xgmii_tx_clk.clkr,
	[NSS_CC_MAC2_TX_CLK_SRC] = &nss_cc_mac2_tx_clk_src.clkr,
	[NSS_CC_MAC2_TX_DIV_CLK_SRC] = &nss_cc_mac2_tx_div_clk_src.clkr,
	[NSS_CC_MAC2_SRDS1_CH1_XGMII_RX_DIV_CLK_SRC] =
		&nss_cc_mac2_srds1_ch1_xgmii_rx_div_clk_src.clkr,
	[NSS_CC_MAC2_SRDS1_CH1_RX_CLK] = &nss_cc_mac2_srds1_ch1_rx_clk.clkr,
	[NSS_CC_MAC2_TX_CLK] = &nss_cc_mac2_tx_clk.clkr,
	[NSS_CC_MAC2_GEPHY1_TX_CLK] = &nss_cc_mac2_gephy1_tx_clk.clkr,
	[NSS_CC_MAC2_SRDS1_CH1_XGMII_RX_CLK] = &nss_cc_mac2_srds1_ch1_xgmii_rx_clk.clkr,
	[NSS_CC_MAC2_RX_CLK_SRC] = &nss_cc_mac2_rx_clk_src.clkr,
	[NSS_CC_MAC2_RX_DIV_CLK_SRC] = &nss_cc_mac2_rx_div_clk_src.clkr,
	[NSS_CC_MAC2_SRDS1_CH1_XGMII_TX_DIV_CLK_SRC] =
		&nss_cc_mac2_srds1_ch1_xgmii_tx_div_clk_src.clkr,
	[NSS_CC_MAC2_SRDS1_CH1_TX_CLK] = &nss_cc_mac2_srds1_ch1_tx_clk.clkr,
	[NSS_CC_MAC2_RX_CLK] = &nss_cc_mac2_rx_clk.clkr,
	[NSS_CC_MAC2_GEPHY1_RX_CLK] = &nss_cc_mac2_gephy1_rx_clk.clkr,
	[NSS_CC_MAC2_SRDS1_CH1_XGMII_TX_CLK] = &nss_cc_mac2_srds1_ch1_xgmii_tx_clk.clkr,
	[NSS_CC_MAC3_TX_CLK_SRC] = &nss_cc_mac3_tx_clk_src.clkr,
	[NSS_CC_MAC3_TX_DIV_CLK_SRC] = &nss_cc_mac3_tx_div_clk_src.clkr,
	[NSS_CC_MAC3_SRDS1_CH2_XGMII_RX_DIV_CLK_SRC] =
		&nss_cc_mac3_srds1_ch2_xgmii_rx_div_clk_src.clkr,
	[NSS_CC_MAC3_SRDS1_CH2_RX_CLK] = &nss_cc_mac3_srds1_ch2_rx_clk.clkr,
	[NSS_CC_MAC3_TX_CLK] = &nss_cc_mac3_tx_clk.clkr,
	[NSS_CC_MAC3_GEPHY2_TX_CLK] = &nss_cc_mac3_gephy2_tx_clk.clkr,
	[NSS_CC_MAC3_SRDS1_CH2_XGMII_RX_CLK] = &nss_cc_mac3_srds1_ch2_xgmii_rx_clk.clkr,
	[NSS_CC_MAC3_RX_CLK_SRC] = &nss_cc_mac3_rx_clk_src.clkr,
	[NSS_CC_MAC3_RX_DIV_CLK_SRC] = &nss_cc_mac3_rx_div_clk_src.clkr,
	[NSS_CC_MAC3_SRDS1_CH2_XGMII_TX_DIV_CLK_SRC] =
		&nss_cc_mac3_srds1_ch2_xgmii_tx_div_clk_src.clkr,
	[NSS_CC_MAC3_SRDS1_CH2_TX_CLK] = &nss_cc_mac3_srds1_ch2_tx_clk.clkr,
	[NSS_CC_MAC3_RX_CLK] = &nss_cc_mac3_rx_clk.clkr,
	[NSS_CC_MAC3_GEPHY2_RX_CLK] = &nss_cc_mac3_gephy2_rx_clk.clkr,
	[NSS_CC_MAC3_SRDS1_CH2_XGMII_TX_CLK] = &nss_cc_mac3_srds1_ch2_xgmii_tx_clk.clkr,
	[NSS_CC_MAC4_TX_CLK_SRC] = &nss_cc_mac4_tx_clk_src.clkr,
	[NSS_CC_MAC4_TX_DIV_CLK_SRC] = &nss_cc_mac4_tx_div_clk_src.clkr,
	[NSS_CC_MAC4_SRDS1_CH3_XGMII_RX_DIV_CLK_SRC] =
		&nss_cc_mac4_srds1_ch3_xgmii_rx_div_clk_src.clkr,
	[NSS_CC_MAC4_SRDS1_CH3_RX_CLK] = &nss_cc_mac4_srds1_ch3_rx_clk.clkr,
	[NSS_CC_MAC4_TX_CLK] = &nss_cc_mac4_tx_clk.clkr,
	[NSS_CC_MAC4_GEPHY3_TX_CLK] = &nss_cc_mac4_gephy3_tx_clk.clkr,
	[NSS_CC_MAC4_SRDS1_CH3_XGMII_RX_CLK] = &nss_cc_mac4_srds1_ch3_xgmii_rx_clk.clkr,
	[NSS_CC_MAC4_RX_CLK_SRC] = &nss_cc_mac4_rx_clk_src.clkr,
	[NSS_CC_MAC4_RX_DIV_CLK_SRC] = &nss_cc_mac4_rx_div_clk_src.clkr,
	[NSS_CC_MAC4_SRDS1_CH3_XGMII_TX_DIV_CLK_SRC] =
		&nss_cc_mac4_srds1_ch3_xgmii_tx_div_clk_src.clkr,
	[NSS_CC_MAC4_SRDS1_CH3_TX_CLK] = &nss_cc_mac4_srds1_ch3_tx_clk.clkr,
	[NSS_CC_MAC4_RX_CLK] = &nss_cc_mac4_rx_clk.clkr,
	[NSS_CC_MAC4_GEPHY3_RX_CLK] = &nss_cc_mac4_gephy3_rx_clk.clkr,
	[NSS_CC_MAC4_SRDS1_CH3_XGMII_TX_CLK] = &nss_cc_mac4_srds1_ch3_xgmii_tx_clk.clkr,
	[NSS_CC_MAC5_TX_CLK_SRC] = &nss_cc_mac5_tx_clk_src.clkr,
	[NSS_CC_MAC5_TX_DIV_CLK_SRC] = &nss_cc_mac5_tx_div_clk_src.clkr,
	[NSS_CC_MAC5_TX_SRDS0_CLK] = &nss_cc_mac5_tx_srds0_clk.clkr,
	[NSS_CC_MAC5_TX_CLK] = &nss_cc_mac5_tx_clk.clkr,
	[NSS_CC_MAC5_RX_CLK_SRC] = &nss_cc_mac5_rx_clk_src.clkr,
	[NSS_CC_MAC5_RX_DIV_CLK_SRC] = &nss_cc_mac5_rx_div_clk_src.clkr,
	[NSS_CC_MAC5_RX_SRDS0_CLK] = &nss_cc_mac5_rx_srds0_clk.clkr,
	[NSS_CC_MAC5_RX_CLK] = &nss_cc_mac5_rx_clk.clkr,
	[NSS_CC_MAC5_TX_SRDS0_CLK_SRC] = &nss_cc_mac5_tx_srds0_clk_src.clkr,
	[NSS_CC_MAC5_RX_SRDS0_CLK_SRC] = &nss_cc_mac5_rx_srds0_clk_src.clkr,
	[NSS_CC_AHB_CLK_SRC] = &nss_cc_ahb_clk_src.clkr,
	[NSS_CC_AHB_CLK] = &nss_cc_ahb_clk.clkr,
	[NSS_CC_SEC_CTRL_AHB_CLK] = &nss_cc_sec_ctrl_ahb_clk.clkr,
	[NSS_CC_TLMM_CLK] = &nss_cc_tlmm_clk.clkr,
	[NSS_CC_TLMM_AHB_CLK] = &nss_cc_tlmm_ahb_clk.clkr,
	[NSS_CC_CNOC_AHB_CLK] = &nss_cc_cnoc_ahb_clk.clkr,
	[NSS_CC_MDIO_AHB_CLK] = &nss_cc_mdio_ahb_clk.clkr,
	[NSS_CC_MDIO_MASTER_AHB_CLK] = &nss_cc_mdio_master_ahb_clk.clkr,
	[NSS_CC_SYS_CLK_SRC] = &nss_cc_sys_clk_src.clkr,
	[NSS_CC_SRDS0_SYS_CLK] = &nss_cc_srds0_sys_clk.clkr,
	[NSS_CC_SRDS1_SYS_CLK] = &nss_cc_srds1_sys_clk.clkr,
	[NSS_CC_GEPHY0_SYS_CLK] = &nss_cc_gephy0_sys_clk.clkr,
	[NSS_CC_GEPHY1_SYS_CLK] = &nss_cc_gephy1_sys_clk.clkr,
	[NSS_CC_GEPHY2_SYS_CLK] = &nss_cc_gephy2_sys_clk.clkr,
	[NSS_CC_GEPHY3_SYS_CLK] = &nss_cc_gephy3_sys_clk.clkr,
};

static const struct qcom_reset_map nss_cc_qca8k_resets[] = {
	[NSS_CC_SWITCH_CORE_ARES] = { 0xc, 2 },
	[NSS_CC_APB_BRIDGE_ARES] = { 0x10, 2 },
	[NSS_CC_MAC0_TX_ARES] = { 0x20, 2 },
	[NSS_CC_MAC0_TX_SRDS1_ARES] = { 0x24, 2 },
	[NSS_CC_MAC0_RX_ARES] = { 0x34, 2 },
	[NSS_CC_MAC0_RX_SRDS1_ARES] = { 0x3c, 2 },
	[NSS_CC_MAC1_SRDS1_CH0_RX_ARES] = { 0x50, 2 },
	[NSS_CC_MAC1_TX_ARES] = { 0x54, 2 },
	[NSS_CC_MAC1_GEPHY0_TX_ARES] = { 0x58, 2 },
	[NSS_CC_MAC1_SRDS1_CH0_XGMII_RX_ARES] = { 0x5c, 2 },
	[NSS_CC_MAC1_SRDS1_CH0_TX_ARES] = { 0x70, 2 },
	[NSS_CC_MAC1_RX_ARES] = { 0x74, 2 },
	[NSS_CC_MAC1_GEPHY0_RX_ARES] = { 0x78, 2 },
	[NSS_CC_MAC1_SRDS1_CH0_XGMII_TX_ARES] = { 0x7c, 2 },
	[NSS_CC_MAC2_SRDS1_CH1_RX_ARES] = { 0x90, 2 },
	[NSS_CC_MAC2_TX_ARES] = { 0x94, 2 },
	[NSS_CC_MAC2_GEPHY1_TX_ARES] = { 0x98, 2 },
	[NSS_CC_MAC2_SRDS1_CH1_XGMII_RX_ARES] = { 0x9c, 2 },
	[NSS_CC_MAC2_SRDS1_CH1_TX_ARES] = { 0xb0, 2 },
	[NSS_CC_MAC2_RX_ARES] = { 0xb4, 2 },
	[NSS_CC_MAC2_GEPHY1_RX_ARES] = { 0xb8, 2 },
	[NSS_CC_MAC2_SRDS1_CH1_XGMII_TX_ARES] = { 0xbc, 2 },
	[NSS_CC_MAC3_SRDS1_CH2_RX_ARES] = { 0xd0, 2 },
	[NSS_CC_MAC3_TX_ARES] = { 0xd4, 2 },
	[NSS_CC_MAC3_GEPHY2_TX_ARES] = { 0xd8, 2 },
	[NSS_CC_MAC3_SRDS1_CH2_XGMII_RX_ARES] = { 0xdc, 2 },
	[NSS_CC_MAC3_SRDS1_CH2_TX_ARES] = { 0xf0, 2 },
	[NSS_CC_MAC3_RX_ARES] = { 0xf4, 2 },
	[NSS_CC_MAC3_GEPHY2_RX_ARES] = { 0xf8, 2 },
	[NSS_CC_MAC3_SRDS1_CH2_XGMII_TX_ARES] = { 0xfc, 2 },
	[NSS_CC_MAC4_SRDS1_CH3_RX_ARES] = { 0x110, 2 },
	[NSS_CC_MAC4_TX_ARES] = { 0x114, 2 },
	[NSS_CC_MAC4_GEPHY3_TX_ARES] = { 0x118, 2 },
	[NSS_CC_MAC4_SRDS1_CH3_XGMII_RX_ARES] = { 0x11c, 2 },
	[NSS_CC_MAC4_SRDS1_CH3_TX_ARES] = { 0x130, 2 },
	[NSS_CC_MAC4_RX_ARES] = { 0x134, 2 },
	[NSS_CC_MAC4_GEPHY3_RX_ARES] = { 0x138, 2 },
	[NSS_CC_MAC4_SRDS1_CH3_XGMII_TX_ARES] = { 0x13c, 2 },
	[NSS_CC_MAC5_TX_ARES] = { 0x14c, 2 },
	[NSS_CC_MAC5_TX_SRDS0_ARES] = { 0x150, 2 },
	[NSS_CC_MAC5_RX_ARES] = { 0x160, 2 },
	[NSS_CC_MAC5_RX_SRDS0_ARES] = { 0x164, 2 },
	[NSS_CC_AHB_ARES] = { 0x170, 2 },
	[NSS_CC_SEC_CTRL_AHB_ARES] = { 0x174, 2 },
	[NSS_CC_TLMM_ARES] = { 0x178, 2 },
	[NSS_CC_TLMM_AHB_ARES] = { 0x190, 2 },
	[NSS_CC_CNOC_AHB_ARES] = { 0x194, 2 }, /* reset CNOC AHB & APB */
	[NSS_CC_MDIO_AHB_ARES] = { 0x198, 2 },
	[NSS_CC_MDIO_MASTER_AHB_ARES] = { 0x19c, 2 },
	[NSS_CC_SRDS0_SYS_ARES] = { 0x1a8, 2 },
	[NSS_CC_SRDS1_SYS_ARES] = { 0x1ac, 2 },
	[NSS_CC_GEPHY0_SYS_ARES] = { 0x1b0, 2 },
	[NSS_CC_GEPHY1_SYS_ARES] = { 0x1b4, 2 },
	[NSS_CC_GEPHY2_SYS_ARES] = { 0x1b8, 2 },
	[NSS_CC_GEPHY3_SYS_ARES] = { 0x1bc, 2 },
	[NSS_CC_SEC_CTRL_ARES] = { 0x1c8, 2 },
	[NSS_CC_SEC_CTRL_SENSE_ARES] = { 0x1d0, 2 },
	[NSS_CC_SLEEP_ARES] = { 0x1e0, 2 },
	[NSS_CC_DEBUG_ARES] = { 0x1e8, 2 },
	[NSS_CC_GEPHY0_ARES] = { 0x304, 0 },
	[NSS_CC_GEPHY1_ARES] = { 0x304, 1 },
	[NSS_CC_GEPHY2_ARES] = { 0x304, 2 },
	[NSS_CC_GEPHY3_ARES] = { 0x304, 3 },
	[NSS_CC_DSP_ARES] = { 0x304, 4 },
	[NSS_CC_GEPHY_FULL_ARES] = { .reg = 0x304, .bitmask = GENMASK(4, 0) },
	[NSS_CC_GLOBAL_ARES] = { 0x308, 0 },
	[NSS_CC_XPCS_ARES] = { 0x30c, 0 },
};

/* For each read/write operation of clock register, there are three MDIO frames
 * sent to the device.
 *
 * 1. The high address part[23:8] of register is packaged into the first MDIO frame
 *    for selecting page.
 * 2. The low address part[7:0] of register is packaged into the second MDIO frame
 *    with the low 16bit data to read/write.
 * 3. The low address part[7:0] of register is packaged into the last MDIO frame
 *    with the high 16bit data to read/write.
 *
 * The clause22 MDIO frame format used by device is as below.
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | ST| OP|   ADDR  |   REG   | TA|             DATA              |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
static inline void convert_reg_to_mii_addr(u32 regaddr, u16 *reg, u16 *phy_addr, u16 *page)
{
	*reg = FIELD_GET(QCA8K_CLK_REG_MASK, regaddr);
	*phy_addr = FIELD_GET(QCA8K_CLK_PHY_ADDR_MASK, regaddr) | QCA8K_LOW_ADDR_PREFIX;
	*page = FIELD_GET(QCA8K_CLK_PAGE_MASK, regaddr);
}

static int qca8k_mii_read(struct mii_bus *bus, u16 switch_phy_id, u32 reg, u32 *val)
{
	int ret, data;

	ret = __mdiobus_read(bus, switch_phy_id, reg);
	if (ret >= 0) {
		data = ret;

		ret = __mdiobus_read(bus, switch_phy_id, (reg | QCA8K_REG_DATA_UPPER_16_BITS));
		if (ret >= 0)
			*val = data | ret << 16;
	}

	if (ret < 0)
		dev_err_ratelimited(&bus->dev, "fail to read qca8k mii register\n");

	return ret < 0 ? ret : 0;
}

static void qca8k_mii_write(struct mii_bus *bus, u16 switch_phy_id, u32 reg, u32 val)
{
	int ret;

	ret = __mdiobus_write(bus, switch_phy_id, reg, lower_16_bits(val));
	if (ret >= 0)
		ret = __mdiobus_write(bus, switch_phy_id, (reg | QCA8K_REG_DATA_UPPER_16_BITS),
				      upper_16_bits(val));

	if (ret < 0)
		dev_err_ratelimited(&bus->dev, "fail to write qca8k mii register\n");
}

static int qca8k_mii_page_set(struct mii_bus *bus, u16 switch_phy_id, u32 reg, u16 page)
{
	int ret;

	ret = __mdiobus_write(bus, switch_phy_id, reg, page);
	if (ret < 0)
		dev_err_ratelimited(&bus->dev, "fail to set page\n");

	return ret;
}

static int qca8k_regmap_read(void *context, unsigned int regaddr, unsigned int *val)
{
	struct mii_bus *bus = context;
	u16 reg, phy_addr, page;
	int ret;

	regaddr += QCA8K_CLK_REG_BASE;
	convert_reg_to_mii_addr(regaddr, &reg, &phy_addr, &page);

	mutex_lock(&bus->mdio_lock);
	ret = qca8k_mii_page_set(bus, QCA8K_HIGH_ADDR_PREFIX, QCA8K_CFG_PAGE_REG, page);
	if (ret < 0)
		goto qca8k_read_exit;

	ret = qca8k_mii_read(bus, phy_addr, reg, val);

qca8k_read_exit:
	mutex_unlock(&bus->mdio_lock);
	return ret;
};

static int qca8k_regmap_write(void *context, unsigned int regaddr, unsigned int val)
{
	struct mii_bus *bus = context;
	u16 reg, phy_addr, page;
	int ret;

	regaddr += QCA8K_CLK_REG_BASE;
	convert_reg_to_mii_addr(regaddr, &reg, &phy_addr, &page);

	mutex_lock(&bus->mdio_lock);
	ret = qca8k_mii_page_set(bus, QCA8K_HIGH_ADDR_PREFIX, QCA8K_CFG_PAGE_REG, page);
	if (ret < 0)
		goto qca8k_write_exit;

	qca8k_mii_write(bus, phy_addr, reg, val);

qca8k_write_exit:
	mutex_unlock(&bus->mdio_lock);
	return ret;
};

static int qca8k_regmap_update_bits(void *context, unsigned int regaddr,
				    unsigned int mask, unsigned int value)
{
	struct mii_bus *bus = context;
	u16 reg, phy_addr, page;
	int ret;
	u32 val;

	regaddr += QCA8K_CLK_REG_BASE;
	convert_reg_to_mii_addr(regaddr, &reg, &phy_addr, &page);

	mutex_lock(&bus->mdio_lock);
	ret = qca8k_mii_page_set(bus, QCA8K_HIGH_ADDR_PREFIX, QCA8K_CFG_PAGE_REG, page);
	if (ret < 0)
		goto qca8k_update_exit;

	ret = qca8k_mii_read(bus, phy_addr, reg, &val);
	if (ret < 0)
		goto qca8k_update_exit;

	val &= ~mask;
	val |= value;
	qca8k_mii_write(bus, phy_addr, reg, val);

qca8k_update_exit:
	mutex_unlock(&bus->mdio_lock);
	return ret;
}

static const struct regmap_config nss_cc_qca8k_regmap_config = {
	.reg_bits = 12,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x30c,
	.reg_read = qca8k_regmap_read,
	.reg_write = qca8k_regmap_write,
	.reg_update_bits = qca8k_regmap_update_bits,
	.disable_locking = true,
};

static const struct qcom_cc_desc nss_cc_qca8k_desc = {
	.config = &nss_cc_qca8k_regmap_config,
	.clks = nss_cc_qca8k_clocks,
	.num_clks = ARRAY_SIZE(nss_cc_qca8k_clocks),
	.resets = nss_cc_qca8k_resets,
	.num_resets = ARRAY_SIZE(nss_cc_qca8k_resets),
};

/*
 * The reference clock of QCA8k NSSCC needs to be enabled to make sure
 * the GPIO reset taking effect.
 */
static int nss_cc_qca8k_clock_enable_and_reset(struct device *dev)
{
	struct gpio_desc *gpiod;
	struct clk *clk;

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	gpiod = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(gpiod)) {
		return PTR_ERR(gpiod);
	} else if (gpiod) {
		msleep(100);
		gpiod_set_value_cansleep(gpiod, 0);
	}

	return 0;
}

static int nss_cc_qca8k_probe(struct mdio_device *mdiodev)
{
	struct regmap *regmap;
	int ret;

	ret = nss_cc_qca8k_clock_enable_and_reset(&mdiodev->dev);
	if (ret)
		return dev_err_probe(&mdiodev->dev, ret, "Fail to reset NSSCC\n");

	regmap = devm_regmap_init(&mdiodev->dev, NULL, mdiodev->bus, nss_cc_qca8k_desc.config);
	if (IS_ERR(regmap))
		return dev_err_probe(&mdiodev->dev, PTR_ERR(regmap), "Failed to init regmap\n");

	return qcom_cc_really_probe(&mdiodev->dev, &nss_cc_qca8k_desc, regmap);
}

static const struct of_device_id nss_cc_qca8k_match_table[] = {
	{ .compatible = "qcom,qca8084-nsscc" },
	{ }
};
MODULE_DEVICE_TABLE(of, nss_cc_qca8k_match_table);

static struct mdio_driver nss_cc_qca8k_driver = {
	.mdiodrv.driver = {
		.name = "qcom,qca8k-nsscc",
		.of_match_table	= nss_cc_qca8k_match_table,
	},
	.probe = nss_cc_qca8k_probe,
};

mdio_module_driver(nss_cc_qca8k_driver);

MODULE_DESCRIPTION("QCOM NSS_CC QCA8K Driver");
MODULE_LICENSE("GPL");
