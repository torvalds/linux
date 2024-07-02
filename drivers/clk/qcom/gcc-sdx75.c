// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,sdx75-gcc.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "clk-regmap-phy-mux.h"
#include "gdsc.h"
#include "reset.h"

enum {
	DT_BI_TCXO,
	DT_SLEEP_CLK,
	DT_EMAC0_SGMIIPHY_MAC_RCLK,
	DT_EMAC0_SGMIIPHY_MAC_TCLK,
	DT_EMAC0_SGMIIPHY_RCLK,
	DT_EMAC0_SGMIIPHY_TCLK,
	DT_EMAC1_SGMIIPHY_MAC_RCLK,
	DT_EMAC1_SGMIIPHY_MAC_TCLK,
	DT_EMAC1_SGMIIPHY_RCLK,
	DT_EMAC1_SGMIIPHY_TCLK,
	DT_PCIE20_PHY_AUX_CLK,
	DT_PCIE_1_PIPE_CLK,
	DT_PCIE_2_PIPE_CLK,
	DT_PCIE_PIPE_CLK,
	DT_USB3_PHY_WRAPPER_GCC_USB30_PIPE_CLK,
};

enum {
	P_BI_TCXO,
	P_EMAC0_SGMIIPHY_MAC_RCLK,
	P_EMAC0_SGMIIPHY_MAC_TCLK,
	P_EMAC0_SGMIIPHY_RCLK,
	P_EMAC0_SGMIIPHY_TCLK,
	P_EMAC1_SGMIIPHY_MAC_RCLK,
	P_EMAC1_SGMIIPHY_MAC_TCLK,
	P_EMAC1_SGMIIPHY_RCLK,
	P_EMAC1_SGMIIPHY_TCLK,
	P_GPLL0_OUT_EVEN,
	P_GPLL0_OUT_MAIN,
	P_GPLL4_OUT_MAIN,
	P_GPLL5_OUT_MAIN,
	P_GPLL6_OUT_MAIN,
	P_GPLL8_OUT_MAIN,
	P_PCIE20_PHY_AUX_CLK,
	P_PCIE_1_PIPE_CLK,
	P_PCIE_2_PIPE_CLK,
	P_PCIE_PIPE_CLK,
	P_SLEEP_CLK,
	P_USB3_PHY_WRAPPER_GCC_USB30_PIPE_CLK,
};

static struct clk_alpha_pll gpll0 = {
	.offset = 0x0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x7d000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpll0",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ole_ops,
		},
	},
};

static const struct clk_div_table post_div_table_gpll0_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll0_out_even = {
	.offset = 0x0,
	.post_div_shift = 10,
	.post_div_table = post_div_table_gpll0_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_gpll0_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpll0_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&gpll0.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_lucid_ole_ops,
	},
};

static struct clk_alpha_pll gpll4 = {
	.offset = 0x4000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x7d000,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpll4",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ole_ops,
		},
	},
};

static struct clk_alpha_pll gpll5 = {
	.offset = 0x5000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x7d000,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpll5",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ole_ops,
		},
	},
};

static struct clk_alpha_pll gpll6 = {
	.offset = 0x6000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x7d000,
		.enable_mask = BIT(6),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpll6",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ole_ops,
		},
	},
};

static struct clk_alpha_pll gpll8 = {
	.offset = 0x8000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x7d000,
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpll8",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ole_ops,
		},
	},
};

static const struct parent_map gcc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_0[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL4_OUT_MAIN, 2 },
	{ P_GPLL5_OUT_MAIN, 5 },
	{ P_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_1[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll4.clkr.hw },
	{ .hw = &gpll5.clkr.hw },
	{ .hw = &gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_SLEEP_CLK, 5 },
	{ P_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_2[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll0.clkr.hw },
	{ .index = DT_SLEEP_CLK },
	{ .hw = &gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
	{ P_SLEEP_CLK, 5 },
};

static const struct clk_parent_data gcc_parent_data_3[] = {
	{ .index = DT_BI_TCXO },
	{ .index = DT_SLEEP_CLK },
};

static const struct parent_map gcc_parent_map_4[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_SLEEP_CLK, 5 },
};

static const struct clk_parent_data gcc_parent_data_4[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll0.clkr.hw },
	{ .index = DT_SLEEP_CLK },
};

static const struct parent_map gcc_parent_map_5[] = {
	{ P_EMAC0_SGMIIPHY_RCLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_5[] = {
	{ .index = DT_EMAC0_SGMIIPHY_RCLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_6[] = {
	{ P_EMAC0_SGMIIPHY_TCLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_6[] = {
	{ .index = DT_EMAC0_SGMIIPHY_TCLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_7[] = {
	{ P_EMAC0_SGMIIPHY_MAC_RCLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_7[] = {
	{ .index = DT_EMAC0_SGMIIPHY_MAC_RCLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_8[] = {
	{ P_EMAC0_SGMIIPHY_MAC_TCLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_8[] = {
	{ .index = DT_EMAC0_SGMIIPHY_MAC_TCLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_9[] = {
	{ P_EMAC1_SGMIIPHY_RCLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_9[] = {
	{ .index = DT_EMAC1_SGMIIPHY_RCLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_10[] = {
	{ P_EMAC1_SGMIIPHY_TCLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_10[] = {
	{ .index = DT_EMAC1_SGMIIPHY_TCLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_11[] = {
	{ P_EMAC1_SGMIIPHY_MAC_RCLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_11[] = {
	{ .index = DT_EMAC1_SGMIIPHY_MAC_RCLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_12[] = {
	{ P_EMAC1_SGMIIPHY_MAC_TCLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_12[] = {
	{ .index = DT_EMAC1_SGMIIPHY_MAC_TCLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_15[] = {
	{ P_PCIE20_PHY_AUX_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_15[] = {
	{ .index = DT_PCIE20_PHY_AUX_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_17[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL6_OUT_MAIN, 2 },
	{ P_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_17[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll6.clkr.hw },
	{ .hw = &gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_18[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL8_OUT_MAIN, 2 },
	{ P_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_18[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll8.clkr.hw },
	{ .hw = &gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_19[] = {
	{ P_USB3_PHY_WRAPPER_GCC_USB30_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_19[] = {
	{ .index = DT_USB3_PHY_WRAPPER_GCC_USB30_PIPE_CLK },
	{ .index = DT_BI_TCXO },
};

static struct clk_regmap_mux gcc_emac0_cc_sgmiiphy_rx_clk_src = {
	.reg = 0x71060,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_5,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_cc_sgmiiphy_rx_clk_src",
			.parent_data = gcc_parent_data_5,
			.num_parents = ARRAY_SIZE(gcc_parent_data_5),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_emac0_cc_sgmiiphy_tx_clk_src = {
	.reg = 0x71058,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_6,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_cc_sgmiiphy_tx_clk_src",
			.parent_data = gcc_parent_data_6,
			.num_parents = ARRAY_SIZE(gcc_parent_data_6),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_emac0_sgmiiphy_mac_rclk_src = {
	.reg = 0x71098,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_7,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_sgmiiphy_mac_rclk_src",
			.parent_data = gcc_parent_data_7,
			.num_parents = ARRAY_SIZE(gcc_parent_data_7),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_emac0_sgmiiphy_mac_tclk_src = {
	.reg = 0x71094,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_8,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_sgmiiphy_mac_tclk_src",
			.parent_data = gcc_parent_data_8,
			.num_parents = ARRAY_SIZE(gcc_parent_data_8),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_emac1_cc_sgmiiphy_rx_clk_src = {
	.reg = 0x72060,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_9,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_cc_sgmiiphy_rx_clk_src",
			.parent_data = gcc_parent_data_9,
			.num_parents = ARRAY_SIZE(gcc_parent_data_9),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_emac1_cc_sgmiiphy_tx_clk_src = {
	.reg = 0x72058,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_10,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_cc_sgmiiphy_tx_clk_src",
			.parent_data = gcc_parent_data_10,
			.num_parents = ARRAY_SIZE(gcc_parent_data_10),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_emac1_sgmiiphy_mac_rclk_src = {
	.reg = 0x72098,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_11,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_sgmiiphy_mac_rclk_src",
			.parent_data = gcc_parent_data_11,
			.num_parents = ARRAY_SIZE(gcc_parent_data_11),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_emac1_sgmiiphy_mac_tclk_src = {
	.reg = 0x72094,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_12,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_sgmiiphy_mac_tclk_src",
			.parent_data = gcc_parent_data_12,
			.num_parents = ARRAY_SIZE(gcc_parent_data_12),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_pcie_1_pipe_clk_src = {
	.reg = 0x67084,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_PCIE_1_PIPE_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_pcie_2_pipe_clk_src = {
	.reg = 0x68050,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_PCIE_2_PIPE_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_mux gcc_pcie_aux_clk_src = {
	.reg = 0x53074,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_15,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_aux_clk_src",
			.parent_data = gcc_parent_data_15,
			.num_parents = ARRAY_SIZE(gcc_parent_data_15),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_pcie_pipe_clk_src = {
	.reg = 0x53058,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_PCIE_PIPE_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb3_phy_pipe_clk_src = {
	.reg = 0x27070,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_19,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_phy_pipe_clk_src",
			.parent_data = gcc_parent_data_19,
			.num_parents = ARRAY_SIZE(gcc_parent_data_19),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static const struct freq_tbl ftbl_gcc_eee_emac0_clk_src[] = {
	F(100000000, P_GPLL0_OUT_EVEN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_eee_emac0_clk_src = {
	.cmd_rcgr = 0x710b0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_eee_emac0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_eee_emac0_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_eee_emac1_clk_src = {
	.cmd_rcgr = 0x720b0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_eee_emac0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_eee_emac1_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_emac0_phy_aux_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_emac0_phy_aux_clk_src = {
	.cmd_rcgr = 0x7102c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_emac0_phy_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_emac0_phy_aux_clk_src",
		.parent_data = gcc_parent_data_4,
		.num_parents = ARRAY_SIZE(gcc_parent_data_4),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_emac0_ptp_clk_src[] = {
	F(75000000, P_GPLL0_OUT_EVEN, 4, 0, 0),
	F(125000000, P_GPLL4_OUT_MAIN, 4, 0, 0),
	F(230400000, P_GPLL5_OUT_MAIN, 3.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_emac0_ptp_clk_src = {
	.cmd_rcgr = 0x7107c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_emac0_ptp_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_emac0_ptp_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_emac0_rgmii_clk_src[] = {
	F(5000000, P_GPLL0_OUT_EVEN, 10, 1, 6),
	F(50000000, P_GPLL0_OUT_EVEN, 6, 0, 0),
	F(125000000, P_GPLL4_OUT_MAIN, 4, 0, 0),
	F(250000000, P_GPLL4_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_emac0_rgmii_clk_src = {
	.cmd_rcgr = 0x71064,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_emac0_rgmii_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_emac0_rgmii_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_emac1_phy_aux_clk_src = {
	.cmd_rcgr = 0x7202c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_emac0_phy_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_emac1_phy_aux_clk_src",
		.parent_data = gcc_parent_data_4,
		.num_parents = ARRAY_SIZE(gcc_parent_data_4),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_emac1_ptp_clk_src = {
	.cmd_rcgr = 0x7207c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_emac0_ptp_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_emac1_ptp_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_emac1_rgmii_clk_src = {
	.cmd_rcgr = 0x72064,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_emac0_rgmii_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_emac1_rgmii_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_gp1_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(50000000, P_GPLL0_OUT_EVEN, 6, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 6, 0, 0),
	F(200000000, P_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_gp1_clk_src = {
	.cmd_rcgr = 0x47004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp1_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_gp2_clk_src = {
	.cmd_rcgr = 0x48004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp2_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_gp3_clk_src = {
	.cmd_rcgr = 0x49004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp3_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_1_aux_phy_clk_src = {
	.cmd_rcgr = 0x67044,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_emac0_phy_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_1_aux_phy_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcie_1_phy_rchng_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(100000000, P_GPLL0_OUT_EVEN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie_1_phy_rchng_clk_src = {
	.cmd_rcgr = 0x6706c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_1_phy_rchng_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_1_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_2_aux_phy_clk_src = {
	.cmd_rcgr = 0x68064,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_emac0_phy_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_2_aux_phy_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_2_phy_rchng_clk_src = {
	.cmd_rcgr = 0x68038,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_1_phy_rchng_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_2_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_aux_phy_clk_src = {
	.cmd_rcgr = 0x5305c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_emac0_phy_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_aux_phy_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_rchng_phy_clk_src = {
	.cmd_rcgr = 0x53078,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_1_phy_rchng_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_rchng_phy_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pdm2_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(60000000, P_GPLL0_OUT_MAIN, 10, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pdm2_clk_src = {
	.cmd_rcgr = 0x34010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pdm2_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pdm2_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap0_s0_clk_src[] = {
	F(7372800, P_GPLL0_OUT_EVEN, 1, 384, 15625),
	F(14745600, P_GPLL0_OUT_EVEN, 1, 768, 15625),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(29491200, P_GPLL0_OUT_EVEN, 1, 1536, 15625),
	F(32000000, P_GPLL0_OUT_EVEN, 1, 8, 75),
	F(48000000, P_GPLL0_OUT_EVEN, 1, 4, 25),
	F(64000000, P_GPLL0_OUT_EVEN, 1, 16, 75),
	F(75000000, P_GPLL0_OUT_EVEN, 4, 0, 0),
	F(80000000, P_GPLL0_OUT_EVEN, 1, 4, 15),
	F(96000000, P_GPLL0_OUT_EVEN, 1, 8, 25),
	F(100000000, P_GPLL0_OUT_MAIN, 6, 0, 0),
	{ }
};

static struct clk_init_data gcc_qupv3_wrap0_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s0_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s0_clk_src = {
	.cmd_rcgr = 0x6c010,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s0_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s1_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s1_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s1_clk_src = {
	.cmd_rcgr = 0x6c148,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s1_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s2_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s2_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s2_clk_src = {
	.cmd_rcgr = 0x6c280,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s2_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s3_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s3_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s3_clk_src = {
	.cmd_rcgr = 0x6c3b8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s3_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s4_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s4_clk_src = {
	.cmd_rcgr = 0x6c4f0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s4_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s5_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s5_clk_src = {
	.cmd_rcgr = 0x6c628,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s5_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s6_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s6_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s6_clk_src = {
	.cmd_rcgr = 0x6c760,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s6_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s7_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s7_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s7_clk_src = {
	.cmd_rcgr = 0x6c898,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s7_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s8_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s8_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s8_clk_src = {
	.cmd_rcgr = 0x6c9d0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s8_clk_src_init,
};

static const struct freq_tbl ftbl_gcc_sdcc1_apps_clk_src[] = {
	F(144000, P_BI_TCXO, 16, 3, 25),
	F(400000, P_BI_TCXO, 12, 1, 4),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(20000000, P_GPLL0_OUT_EVEN, 5, 1, 3),
	F(25000000, P_GPLL0_OUT_EVEN, 12, 0, 0),
	F(50000000, P_GPLL0_OUT_EVEN, 6, 0, 0),
	F(100000000, P_GPLL0_OUT_EVEN, 3, 0, 0),
	F(192000000, P_GPLL6_OUT_MAIN, 2, 0, 0),
	F(384000000, P_GPLL6_OUT_MAIN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc1_apps_clk_src = {
	.cmd_rcgr = 0x6b014,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_17,
	.freq_tbl = ftbl_gcc_sdcc1_apps_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_sdcc1_apps_clk_src",
		.parent_data = gcc_parent_data_17,
		.num_parents = ARRAY_SIZE(gcc_parent_data_17),
		.ops = &clk_rcg2_floor_ops,
	},
};

static const struct freq_tbl ftbl_gcc_sdcc2_apps_clk_src[] = {
	F(400000, P_BI_TCXO, 12, 1, 4),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(25000000, P_GPLL0_OUT_EVEN, 12, 0, 0),
	F(50000000, P_GPLL0_OUT_EVEN, 6, 0, 0),
	F(100000000, P_GPLL0_OUT_EVEN, 3, 0, 0),
	F(202000000, P_GPLL8_OUT_MAIN, 4, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc2_apps_clk_src = {
	.cmd_rcgr = 0x6a018,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_18,
	.freq_tbl = ftbl_gcc_sdcc2_apps_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_sdcc2_apps_clk_src",
		.parent_data = gcc_parent_data_18,
		.num_parents = ARRAY_SIZE(gcc_parent_data_18),
		.ops = &clk_rcg2_floor_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb30_master_clk_src[] = {
	F(200000000, P_GPLL0_OUT_EVEN, 1.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb30_master_clk_src = {
	.cmd_rcgr = 0x27034,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb30_master_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_master_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_usb30_mock_utmi_clk_src = {
	.cmd_rcgr = 0x2704c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_emac0_phy_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_mock_utmi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb3_phy_aux_clk_src[] = {
	F(1000000, P_BI_TCXO, 1, 5, 96),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb3_phy_aux_clk_src = {
	.cmd_rcgr = 0x27074,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_usb3_phy_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb3_phy_aux_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_regmap_div gcc_pcie_1_pipe_div2_clk_src = {
	.reg = 0x67088,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_1_pipe_div2_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_pcie_1_pipe_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_pcie_2_pipe_div2_clk_src = {
	.reg = 0x68088,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_2_pipe_div2_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_pcie_2_pipe_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_usb30_mock_utmi_postdiv_clk_src = {
	.reg = 0x27064,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_mock_utmi_postdiv_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_usb30_mock_utmi_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch gcc_boot_rom_ahb_clk = {
	.halt_reg = 0x37004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x37004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d008,
		.enable_mask = BIT(26),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_boot_rom_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_eee_emac0_clk = {
	.halt_reg = 0x710ac,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x710ac,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_eee_emac0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_eee_emac0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_eee_emac1_clk = {
	.halt_reg = 0x720ac,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x720ac,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_eee_emac1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_eee_emac1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_axi_clk = {
	.halt_reg = 0x71018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x71018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x71018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_cc_sgmiiphy_rx_clk = {
	.halt_reg = 0x7105c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x7105c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_cc_sgmiiphy_rx_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac0_cc_sgmiiphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_cc_sgmiiphy_tx_clk = {
	.halt_reg = 0x71054,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x71054,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_cc_sgmiiphy_tx_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac0_cc_sgmiiphy_tx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_phy_aux_clk = {
	.halt_reg = 0x71028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x71028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac0_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_ptp_clk = {
	.halt_reg = 0x71044,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x71044,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_ptp_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac0_ptp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_rgmii_clk = {
	.halt_reg = 0x71050,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x71050,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_rgmii_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac0_rgmii_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_rpcs_rx_clk = {
	.halt_reg = 0x710a0,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x710a0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_rpcs_rx_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac0_sgmiiphy_mac_rclk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_rpcs_tx_clk = {
	.halt_reg = 0x7109c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x7109c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_rpcs_tx_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac0_sgmiiphy_mac_tclk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_slv_ahb_clk = {
	.halt_reg = 0x71024,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x71024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x71024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_slv_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_xgxs_rx_clk = {
	.halt_reg = 0x710a8,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x710a8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_xgxs_rx_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac0_sgmiiphy_mac_rclk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_xgxs_tx_clk = {
	.halt_reg = 0x710a4,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x710a4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_xgxs_tx_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac0_sgmiiphy_mac_tclk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac1_axi_clk = {
	.halt_reg = 0x72018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x72018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x72018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac1_cc_sgmiiphy_rx_clk = {
	.halt_reg = 0x7205c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x7205c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_cc_sgmiiphy_rx_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac1_cc_sgmiiphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac1_cc_sgmiiphy_tx_clk = {
	.halt_reg = 0x72054,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x72054,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_cc_sgmiiphy_tx_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac1_cc_sgmiiphy_tx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac1_phy_aux_clk = {
	.halt_reg = 0x72028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x72028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac1_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac1_ptp_clk = {
	.halt_reg = 0x72044,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x72044,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_ptp_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac1_ptp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac1_rgmii_clk = {
	.halt_reg = 0x72050,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x72050,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_rgmii_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac1_rgmii_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac1_rpcs_rx_clk = {
	.halt_reg = 0x720a0,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x720a0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_rpcs_rx_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac1_sgmiiphy_mac_rclk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac1_rpcs_tx_clk = {
	.halt_reg = 0x7209c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x7209c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_rpcs_tx_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac1_sgmiiphy_mac_tclk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac1_slv_ahb_clk = {
	.halt_reg = 0x72024,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x72024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x72024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_slv_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac1_xgxs_rx_clk = {
	.halt_reg = 0x720a8,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x720a8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_xgxs_rx_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac1_sgmiiphy_mac_rclk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac1_xgxs_tx_clk = {
	.halt_reg = 0x720a4,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x720a4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_xgxs_tx_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac1_sgmiiphy_mac_tclk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac_0_clkref_en = {
	.halt_reg = 0x98108,
	.halt_check = BRANCH_HALT_ENABLE,
	.clkr = {
		.enable_reg = 0x98108,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac_0_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac_1_clkref_en = {
	.halt_reg = 0x9810c,
	.halt_check = BRANCH_HALT_ENABLE,
	.clkr = {
		.enable_reg = 0x9810c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac_1_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp1_clk = {
	.halt_reg = 0x47000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x47000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gp1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_gp1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp2_clk = {
	.halt_reg = 0x48000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x48000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gp2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_gp2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp3_clk = {
	.halt_reg = 0x49000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x49000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gp3_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_gp3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_clkref_en = {
	.halt_reg = 0x98004,
	.halt_check = BRANCH_HALT_ENABLE,
	.clkr = {
		.enable_reg = 0x98004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_aux_clk = {
	.halt_reg = 0x67038,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(22),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_1_aux_phy_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_cfg_ahb_clk = {
	.halt_reg = 0x67034,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x67034,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(21),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_clkref_en = {
	.halt_reg = 0x98114,
	.halt_check = BRANCH_HALT_ENABLE,
	.clkr = {
		.enable_reg = 0x98114,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_mstr_axi_clk = {
	.halt_reg = 0x67028,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(20),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_phy_rchng_clk = {
	.halt_reg = 0x67068,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(24),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_phy_rchng_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_1_phy_rchng_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_pipe_clk = {
	.halt_reg = 0x6705c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(23),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_1_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_pipe_div2_clk = {
	.halt_reg = 0x6708c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x7d020,
		.enable_mask = BIT(3),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_pipe_div2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_1_pipe_div2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_slv_axi_clk = {
	.halt_reg = 0x6701c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(19),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_slv_q2a_axi_clk = {
	.halt_reg = 0x67018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(18),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_aux_clk = {
	.halt_reg = 0x68058,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(29),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_2_aux_phy_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_cfg_ahb_clk = {
	.halt_reg = 0x68034,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x68034,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(28),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_clkref_en = {
	.halt_reg = 0x98110,
	.halt_check = BRANCH_HALT_ENABLE,
	.clkr = {
		.enable_reg = 0x98110,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_mstr_axi_clk = {
	.halt_reg = 0x68028,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d008,
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_phy_rchng_clk = {
	.halt_reg = 0x68098,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(31),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2_phy_rchng_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_2_phy_rchng_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_pipe_clk = {
	.halt_reg = 0x6807c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(30),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_2_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_pipe_div2_clk = {
	.halt_reg = 0x6808c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x7d020,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2_pipe_div2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_2_pipe_div2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_slv_axi_clk = {
	.halt_reg = 0x6801c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(26),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_slv_q2a_axi_clk = {
	.halt_reg = 0x68018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(25),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_aux_clk = {
	.halt_reg = 0x5303c,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x5303c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(15),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_cfg_ahb_clk = {
	.halt_reg = 0x53034,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x53034,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(13),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_mstr_axi_clk = {
	.halt_reg = 0x53028,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x53028,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(12),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_pipe_clk = {
	.halt_reg = 0x5304c,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x5304c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(17),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_rchng_phy_clk = {
	.halt_reg = 0x53038,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x53038,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(14),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_rchng_phy_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_rchng_phy_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_sleep_clk = {
	.halt_reg = 0x53048,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x53048,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(16),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_sleep_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_aux_phy_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_slv_axi_clk = {
	.halt_reg = 0x5301c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(11),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_slv_q2a_axi_clk = {
	.halt_reg = 0x53018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x53018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm2_clk = {
	.halt_reg = 0x3400c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3400c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pdm2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pdm2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm_ahb_clk = {
	.halt_reg = 0x34004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x34004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pdm_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm_xo4_clk = {
	.halt_reg = 0x34008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x34008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pdm_xo4_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_core_2x_clk = {
	.halt_reg = 0x2d018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d008,
		.enable_mask = BIT(15),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_core_clk = {
	.halt_reg = 0x2d008,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d008,
		.enable_mask = BIT(14),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s0_clk = {
	.halt_reg = 0x6c004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d008,
		.enable_mask = BIT(16),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap0_s0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s1_clk = {
	.halt_reg = 0x6c13c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d008,
		.enable_mask = BIT(17),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap0_s1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s2_clk = {
	.halt_reg = 0x6c274,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d008,
		.enable_mask = BIT(18),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap0_s2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s3_clk = {
	.halt_reg = 0x6c3ac,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d008,
		.enable_mask = BIT(19),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s3_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap0_s3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s4_clk = {
	.halt_reg = 0x6c4e4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d008,
		.enable_mask = BIT(20),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s4_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap0_s4_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s5_clk = {
	.halt_reg = 0x6c61c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d008,
		.enable_mask = BIT(21),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s5_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap0_s5_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s6_clk = {
	.halt_reg = 0x6c754,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d008,
		.enable_mask = BIT(22),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s6_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap0_s6_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s7_clk = {
	.halt_reg = 0x6c88c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d008,
		.enable_mask = BIT(23),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s7_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap0_s7_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s8_clk = {
	.halt_reg = 0x6c9c4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d020,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s8_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap0_s8_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_0_m_ahb_clk = {
	.halt_reg = 0x2d000,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2d000,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d008,
		.enable_mask = BIT(12),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_0_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_0_s_ahb_clk = {
	.halt_reg = 0x2d004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2d004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d008,
		.enable_mask = BIT(13),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_0_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_ahb_clk = {
	.halt_reg = 0x6b004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6b004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sdcc1_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_apps_clk = {
	.halt_reg = 0x6b008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6b008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sdcc1_apps_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_sdcc1_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc2_ahb_clk = {
	.halt_reg = 0x6a010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6a010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sdcc2_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc2_apps_clk = {
	.halt_reg = 0x6a004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6a004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sdcc2_apps_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_sdcc2_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb2_clkref_en = {
	.halt_reg = 0x98008,
	.halt_check = BRANCH_HALT_ENABLE,
	.clkr = {
		.enable_reg = 0x98008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb2_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_master_clk = {
	.halt_reg = 0x27018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x27018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_master_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb30_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_mock_utmi_clk = {
	.halt_reg = 0x27030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x27030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_mock_utmi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb30_mock_utmi_postdiv_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_mstr_axi_clk = {
	.halt_reg = 0x27024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x27024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_sleep_clk = {
	.halt_reg = 0x2702c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2702c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_slv_ahb_clk = {
	.halt_reg = 0x27028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x27028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_slv_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_phy_aux_clk = {
	.halt_reg = 0x27068,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x27068,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb3_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_phy_pipe_clk = {
	.halt_reg = 0x2706c,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x2706c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2706c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_phy_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb3_phy_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_clkref_en = {
	.halt_reg = 0x98000,
	.halt_check = BRANCH_HALT_ENABLE,
	.clkr = {
		.enable_reg = 0x98000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_prim_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb_phy_cfg_ahb2phy_clk = {
	.halt_reg = 0x29004,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x29004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x29004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb_phy_cfg_ahb2phy_clk",
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct gdsc gcc_emac0_gdsc = {
	.gdscr = 0x71004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_emac0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct gdsc gcc_emac1_gdsc = {
	.gdscr = 0x72004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_emac1_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct gdsc gcc_pcie_1_gdsc = {
	.gdscr = 0x67004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_pcie_1_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct gdsc gcc_pcie_1_phy_gdsc = {
	.gdscr = 0x56004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "gcc_pcie_1_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct gdsc gcc_pcie_2_gdsc = {
	.gdscr = 0x68004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_pcie_2_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct gdsc gcc_pcie_2_phy_gdsc = {
	.gdscr = 0x6e004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "gcc_pcie_2_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct gdsc gcc_pcie_gdsc = {
	.gdscr = 0x53004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_pcie_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct gdsc gcc_pcie_phy_gdsc = {
	.gdscr = 0x54004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "gcc_pcie_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct gdsc gcc_usb30_gdsc = {
	.gdscr = 0x27004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_usb30_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct gdsc gcc_usb3_phy_gdsc = {
	.gdscr = 0x28008,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "gcc_usb3_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct clk_regmap *gcc_sdx75_clocks[] = {
	[GCC_BOOT_ROM_AHB_CLK] = &gcc_boot_rom_ahb_clk.clkr,
	[GCC_EEE_EMAC0_CLK] = &gcc_eee_emac0_clk.clkr,
	[GCC_EEE_EMAC0_CLK_SRC] = &gcc_eee_emac0_clk_src.clkr,
	[GCC_EEE_EMAC1_CLK] = &gcc_eee_emac1_clk.clkr,
	[GCC_EEE_EMAC1_CLK_SRC] = &gcc_eee_emac1_clk_src.clkr,
	[GCC_EMAC0_AXI_CLK] = &gcc_emac0_axi_clk.clkr,
	[GCC_EMAC0_CC_SGMIIPHY_RX_CLK] = &gcc_emac0_cc_sgmiiphy_rx_clk.clkr,
	[GCC_EMAC0_CC_SGMIIPHY_RX_CLK_SRC] = &gcc_emac0_cc_sgmiiphy_rx_clk_src.clkr,
	[GCC_EMAC0_CC_SGMIIPHY_TX_CLK] = &gcc_emac0_cc_sgmiiphy_tx_clk.clkr,
	[GCC_EMAC0_CC_SGMIIPHY_TX_CLK_SRC] = &gcc_emac0_cc_sgmiiphy_tx_clk_src.clkr,
	[GCC_EMAC0_PHY_AUX_CLK] = &gcc_emac0_phy_aux_clk.clkr,
	[GCC_EMAC0_PHY_AUX_CLK_SRC] = &gcc_emac0_phy_aux_clk_src.clkr,
	[GCC_EMAC0_PTP_CLK] = &gcc_emac0_ptp_clk.clkr,
	[GCC_EMAC0_PTP_CLK_SRC] = &gcc_emac0_ptp_clk_src.clkr,
	[GCC_EMAC0_RGMII_CLK] = &gcc_emac0_rgmii_clk.clkr,
	[GCC_EMAC0_RGMII_CLK_SRC] = &gcc_emac0_rgmii_clk_src.clkr,
	[GCC_EMAC0_RPCS_RX_CLK] = &gcc_emac0_rpcs_rx_clk.clkr,
	[GCC_EMAC0_RPCS_TX_CLK] = &gcc_emac0_rpcs_tx_clk.clkr,
	[GCC_EMAC0_SGMIIPHY_MAC_RCLK_SRC] = &gcc_emac0_sgmiiphy_mac_rclk_src.clkr,
	[GCC_EMAC0_SGMIIPHY_MAC_TCLK_SRC] = &gcc_emac0_sgmiiphy_mac_tclk_src.clkr,
	[GCC_EMAC0_SLV_AHB_CLK] = &gcc_emac0_slv_ahb_clk.clkr,
	[GCC_EMAC0_XGXS_RX_CLK] = &gcc_emac0_xgxs_rx_clk.clkr,
	[GCC_EMAC0_XGXS_TX_CLK] = &gcc_emac0_xgxs_tx_clk.clkr,
	[GCC_EMAC1_AXI_CLK] = &gcc_emac1_axi_clk.clkr,
	[GCC_EMAC1_CC_SGMIIPHY_RX_CLK] = &gcc_emac1_cc_sgmiiphy_rx_clk.clkr,
	[GCC_EMAC1_CC_SGMIIPHY_RX_CLK_SRC] = &gcc_emac1_cc_sgmiiphy_rx_clk_src.clkr,
	[GCC_EMAC1_CC_SGMIIPHY_TX_CLK] = &gcc_emac1_cc_sgmiiphy_tx_clk.clkr,
	[GCC_EMAC1_CC_SGMIIPHY_TX_CLK_SRC] = &gcc_emac1_cc_sgmiiphy_tx_clk_src.clkr,
	[GCC_EMAC1_PHY_AUX_CLK] = &gcc_emac1_phy_aux_clk.clkr,
	[GCC_EMAC1_PHY_AUX_CLK_SRC] = &gcc_emac1_phy_aux_clk_src.clkr,
	[GCC_EMAC1_PTP_CLK] = &gcc_emac1_ptp_clk.clkr,
	[GCC_EMAC1_PTP_CLK_SRC] = &gcc_emac1_ptp_clk_src.clkr,
	[GCC_EMAC1_RGMII_CLK] = &gcc_emac1_rgmii_clk.clkr,
	[GCC_EMAC1_RGMII_CLK_SRC] = &gcc_emac1_rgmii_clk_src.clkr,
	[GCC_EMAC1_RPCS_RX_CLK] = &gcc_emac1_rpcs_rx_clk.clkr,
	[GCC_EMAC1_RPCS_TX_CLK] = &gcc_emac1_rpcs_tx_clk.clkr,
	[GCC_EMAC1_SGMIIPHY_MAC_RCLK_SRC] = &gcc_emac1_sgmiiphy_mac_rclk_src.clkr,
	[GCC_EMAC1_SGMIIPHY_MAC_TCLK_SRC] = &gcc_emac1_sgmiiphy_mac_tclk_src.clkr,
	[GCC_EMAC1_SLV_AHB_CLK] = &gcc_emac1_slv_ahb_clk.clkr,
	[GCC_EMAC1_XGXS_RX_CLK] = &gcc_emac1_xgxs_rx_clk.clkr,
	[GCC_EMAC1_XGXS_TX_CLK] = &gcc_emac1_xgxs_tx_clk.clkr,
	[GCC_EMAC_0_CLKREF_EN] = &gcc_emac_0_clkref_en.clkr,
	[GCC_EMAC_1_CLKREF_EN] = &gcc_emac_1_clkref_en.clkr,
	[GCC_GP1_CLK] = &gcc_gp1_clk.clkr,
	[GCC_GP1_CLK_SRC] = &gcc_gp1_clk_src.clkr,
	[GCC_GP2_CLK] = &gcc_gp2_clk.clkr,
	[GCC_GP2_CLK_SRC] = &gcc_gp2_clk_src.clkr,
	[GCC_GP3_CLK] = &gcc_gp3_clk.clkr,
	[GCC_GP3_CLK_SRC] = &gcc_gp3_clk_src.clkr,
	[GCC_PCIE_0_CLKREF_EN] = &gcc_pcie_0_clkref_en.clkr,
	[GCC_PCIE_1_AUX_CLK] = &gcc_pcie_1_aux_clk.clkr,
	[GCC_PCIE_1_AUX_PHY_CLK_SRC] = &gcc_pcie_1_aux_phy_clk_src.clkr,
	[GCC_PCIE_1_CFG_AHB_CLK] = &gcc_pcie_1_cfg_ahb_clk.clkr,
	[GCC_PCIE_1_CLKREF_EN] = &gcc_pcie_1_clkref_en.clkr,
	[GCC_PCIE_1_MSTR_AXI_CLK] = &gcc_pcie_1_mstr_axi_clk.clkr,
	[GCC_PCIE_1_PHY_RCHNG_CLK] = &gcc_pcie_1_phy_rchng_clk.clkr,
	[GCC_PCIE_1_PHY_RCHNG_CLK_SRC] = &gcc_pcie_1_phy_rchng_clk_src.clkr,
	[GCC_PCIE_1_PIPE_CLK] = &gcc_pcie_1_pipe_clk.clkr,
	[GCC_PCIE_1_PIPE_CLK_SRC] = &gcc_pcie_1_pipe_clk_src.clkr,
	[GCC_PCIE_1_PIPE_DIV2_CLK] = &gcc_pcie_1_pipe_div2_clk.clkr,
	[GCC_PCIE_1_PIPE_DIV2_CLK_SRC] = &gcc_pcie_1_pipe_div2_clk_src.clkr,
	[GCC_PCIE_1_SLV_AXI_CLK] = &gcc_pcie_1_slv_axi_clk.clkr,
	[GCC_PCIE_1_SLV_Q2A_AXI_CLK] = &gcc_pcie_1_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_2_AUX_CLK] = &gcc_pcie_2_aux_clk.clkr,
	[GCC_PCIE_2_AUX_PHY_CLK_SRC] = &gcc_pcie_2_aux_phy_clk_src.clkr,
	[GCC_PCIE_2_CFG_AHB_CLK] = &gcc_pcie_2_cfg_ahb_clk.clkr,
	[GCC_PCIE_2_CLKREF_EN] = &gcc_pcie_2_clkref_en.clkr,
	[GCC_PCIE_2_MSTR_AXI_CLK] = &gcc_pcie_2_mstr_axi_clk.clkr,
	[GCC_PCIE_2_PHY_RCHNG_CLK] = &gcc_pcie_2_phy_rchng_clk.clkr,
	[GCC_PCIE_2_PHY_RCHNG_CLK_SRC] = &gcc_pcie_2_phy_rchng_clk_src.clkr,
	[GCC_PCIE_2_PIPE_CLK] = &gcc_pcie_2_pipe_clk.clkr,
	[GCC_PCIE_2_PIPE_CLK_SRC] = &gcc_pcie_2_pipe_clk_src.clkr,
	[GCC_PCIE_2_PIPE_DIV2_CLK] = &gcc_pcie_2_pipe_div2_clk.clkr,
	[GCC_PCIE_2_PIPE_DIV2_CLK_SRC] = &gcc_pcie_2_pipe_div2_clk_src.clkr,
	[GCC_PCIE_2_SLV_AXI_CLK] = &gcc_pcie_2_slv_axi_clk.clkr,
	[GCC_PCIE_2_SLV_Q2A_AXI_CLK] = &gcc_pcie_2_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_AUX_CLK] = &gcc_pcie_aux_clk.clkr,
	[GCC_PCIE_AUX_CLK_SRC] = &gcc_pcie_aux_clk_src.clkr,
	[GCC_PCIE_AUX_PHY_CLK_SRC] = &gcc_pcie_aux_phy_clk_src.clkr,
	[GCC_PCIE_CFG_AHB_CLK] = &gcc_pcie_cfg_ahb_clk.clkr,
	[GCC_PCIE_MSTR_AXI_CLK] = &gcc_pcie_mstr_axi_clk.clkr,
	[GCC_PCIE_PIPE_CLK] = &gcc_pcie_pipe_clk.clkr,
	[GCC_PCIE_PIPE_CLK_SRC] = &gcc_pcie_pipe_clk_src.clkr,
	[GCC_PCIE_RCHNG_PHY_CLK] = &gcc_pcie_rchng_phy_clk.clkr,
	[GCC_PCIE_RCHNG_PHY_CLK_SRC] = &gcc_pcie_rchng_phy_clk_src.clkr,
	[GCC_PCIE_SLEEP_CLK] = &gcc_pcie_sleep_clk.clkr,
	[GCC_PCIE_SLV_AXI_CLK] = &gcc_pcie_slv_axi_clk.clkr,
	[GCC_PCIE_SLV_Q2A_AXI_CLK] = &gcc_pcie_slv_q2a_axi_clk.clkr,
	[GCC_PDM2_CLK] = &gcc_pdm2_clk.clkr,
	[GCC_PDM2_CLK_SRC] = &gcc_pdm2_clk_src.clkr,
	[GCC_PDM_AHB_CLK] = &gcc_pdm_ahb_clk.clkr,
	[GCC_PDM_XO4_CLK] = &gcc_pdm_xo4_clk.clkr,
	[GCC_QUPV3_WRAP0_CORE_2X_CLK] = &gcc_qupv3_wrap0_core_2x_clk.clkr,
	[GCC_QUPV3_WRAP0_CORE_CLK] = &gcc_qupv3_wrap0_core_clk.clkr,
	[GCC_QUPV3_WRAP0_S0_CLK] = &gcc_qupv3_wrap0_s0_clk.clkr,
	[GCC_QUPV3_WRAP0_S0_CLK_SRC] = &gcc_qupv3_wrap0_s0_clk_src.clkr,
	[GCC_QUPV3_WRAP0_S1_CLK] = &gcc_qupv3_wrap0_s1_clk.clkr,
	[GCC_QUPV3_WRAP0_S1_CLK_SRC] = &gcc_qupv3_wrap0_s1_clk_src.clkr,
	[GCC_QUPV3_WRAP0_S2_CLK] = &gcc_qupv3_wrap0_s2_clk.clkr,
	[GCC_QUPV3_WRAP0_S2_CLK_SRC] = &gcc_qupv3_wrap0_s2_clk_src.clkr,
	[GCC_QUPV3_WRAP0_S3_CLK] = &gcc_qupv3_wrap0_s3_clk.clkr,
	[GCC_QUPV3_WRAP0_S3_CLK_SRC] = &gcc_qupv3_wrap0_s3_clk_src.clkr,
	[GCC_QUPV3_WRAP0_S4_CLK] = &gcc_qupv3_wrap0_s4_clk.clkr,
	[GCC_QUPV3_WRAP0_S4_CLK_SRC] = &gcc_qupv3_wrap0_s4_clk_src.clkr,
	[GCC_QUPV3_WRAP0_S5_CLK] = &gcc_qupv3_wrap0_s5_clk.clkr,
	[GCC_QUPV3_WRAP0_S5_CLK_SRC] = &gcc_qupv3_wrap0_s5_clk_src.clkr,
	[GCC_QUPV3_WRAP0_S6_CLK] = &gcc_qupv3_wrap0_s6_clk.clkr,
	[GCC_QUPV3_WRAP0_S6_CLK_SRC] = &gcc_qupv3_wrap0_s6_clk_src.clkr,
	[GCC_QUPV3_WRAP0_S7_CLK] = &gcc_qupv3_wrap0_s7_clk.clkr,
	[GCC_QUPV3_WRAP0_S7_CLK_SRC] = &gcc_qupv3_wrap0_s7_clk_src.clkr,
	[GCC_QUPV3_WRAP0_S8_CLK] = &gcc_qupv3_wrap0_s8_clk.clkr,
	[GCC_QUPV3_WRAP0_S8_CLK_SRC] = &gcc_qupv3_wrap0_s8_clk_src.clkr,
	[GCC_QUPV3_WRAP_0_M_AHB_CLK] = &gcc_qupv3_wrap_0_m_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_0_S_AHB_CLK] = &gcc_qupv3_wrap_0_s_ahb_clk.clkr,
	[GCC_SDCC1_AHB_CLK] = &gcc_sdcc1_ahb_clk.clkr,
	[GCC_SDCC1_APPS_CLK] = &gcc_sdcc1_apps_clk.clkr,
	[GCC_SDCC1_APPS_CLK_SRC] = &gcc_sdcc1_apps_clk_src.clkr,
	[GCC_SDCC2_AHB_CLK] = &gcc_sdcc2_ahb_clk.clkr,
	[GCC_SDCC2_APPS_CLK] = &gcc_sdcc2_apps_clk.clkr,
	[GCC_SDCC2_APPS_CLK_SRC] = &gcc_sdcc2_apps_clk_src.clkr,
	[GCC_USB2_CLKREF_EN] = &gcc_usb2_clkref_en.clkr,
	[GCC_USB30_MASTER_CLK] = &gcc_usb30_master_clk.clkr,
	[GCC_USB30_MASTER_CLK_SRC] = &gcc_usb30_master_clk_src.clkr,
	[GCC_USB30_MOCK_UTMI_CLK] = &gcc_usb30_mock_utmi_clk.clkr,
	[GCC_USB30_MOCK_UTMI_CLK_SRC] = &gcc_usb30_mock_utmi_clk_src.clkr,
	[GCC_USB30_MOCK_UTMI_POSTDIV_CLK_SRC] = &gcc_usb30_mock_utmi_postdiv_clk_src.clkr,
	[GCC_USB30_MSTR_AXI_CLK] = &gcc_usb30_mstr_axi_clk.clkr,
	[GCC_USB30_SLEEP_CLK] = &gcc_usb30_sleep_clk.clkr,
	[GCC_USB30_SLV_AHB_CLK] = &gcc_usb30_slv_ahb_clk.clkr,
	[GCC_USB3_PHY_AUX_CLK] = &gcc_usb3_phy_aux_clk.clkr,
	[GCC_USB3_PHY_AUX_CLK_SRC] = &gcc_usb3_phy_aux_clk_src.clkr,
	[GCC_USB3_PHY_PIPE_CLK] = &gcc_usb3_phy_pipe_clk.clkr,
	[GCC_USB3_PHY_PIPE_CLK_SRC] = &gcc_usb3_phy_pipe_clk_src.clkr,
	[GCC_USB3_PRIM_CLKREF_EN] = &gcc_usb3_prim_clkref_en.clkr,
	[GCC_USB_PHY_CFG_AHB2PHY_CLK] = &gcc_usb_phy_cfg_ahb2phy_clk.clkr,
	[GPLL0] = &gpll0.clkr,
	[GPLL0_OUT_EVEN] = &gpll0_out_even.clkr,
	[GPLL4] = &gpll4.clkr,
	[GPLL5] = &gpll5.clkr,
	[GPLL6] = &gpll6.clkr,
	[GPLL8] = &gpll8.clkr,
};

static struct gdsc *gcc_sdx75_gdscs[] = {
	[GCC_EMAC0_GDSC] = &gcc_emac0_gdsc,
	[GCC_EMAC1_GDSC] = &gcc_emac1_gdsc,
	[GCC_PCIE_1_GDSC] = &gcc_pcie_1_gdsc,
	[GCC_PCIE_1_PHY_GDSC] = &gcc_pcie_1_phy_gdsc,
	[GCC_PCIE_2_GDSC] = &gcc_pcie_2_gdsc,
	[GCC_PCIE_2_PHY_GDSC] = &gcc_pcie_2_phy_gdsc,
	[GCC_PCIE_GDSC] = &gcc_pcie_gdsc,
	[GCC_PCIE_PHY_GDSC] = &gcc_pcie_phy_gdsc,
	[GCC_USB30_GDSC] = &gcc_usb30_gdsc,
	[GCC_USB3_PHY_GDSC] = &gcc_usb3_phy_gdsc,
};

static const struct qcom_reset_map gcc_sdx75_resets[] = {
	[GCC_EMAC0_BCR] = { 0x71000 },
	[GCC_EMAC0_RGMII_CLK_ARES] = { 0x71050, 2 },
	[GCC_EMAC1_BCR] = { 0x72000 },
	[GCC_EMMC_BCR] = { 0x6b000 },
	[GCC_PCIE_1_BCR] = { 0x67000 },
	[GCC_PCIE_1_LINK_DOWN_BCR] = { 0x9e700 },
	[GCC_PCIE_1_NOCSR_COM_PHY_BCR] = { 0x56120 },
	[GCC_PCIE_1_PHY_BCR] = { 0x56000 },
	[GCC_PCIE_2_BCR] = { 0x68000 },
	[GCC_PCIE_2_LINK_DOWN_BCR] = { 0x9f700 },
	[GCC_PCIE_2_NOCSR_COM_PHY_BCR] = { 0x6e130 },
	[GCC_PCIE_2_PHY_BCR] = { 0x6e000 },
	[GCC_PCIE_BCR] = { 0x53000 },
	[GCC_PCIE_LINK_DOWN_BCR] = { 0x87000 },
	[GCC_PCIE_NOCSR_COM_PHY_BCR] = { 0x88008 },
	[GCC_PCIE_PHY_BCR] = { 0x54000 },
	[GCC_PCIE_PHY_CFG_AHB_BCR] = { 0x88000 },
	[GCC_PCIE_PHY_COM_BCR] = { 0x88004 },
	[GCC_PCIE_PHY_NOCSR_COM_PHY_BCR] = { 0x8800c },
	[GCC_QUSB2PHY_BCR] = { 0x2a000 },
	[GCC_TCSR_PCIE_BCR] = { 0x84000 },
	[GCC_USB30_BCR] = { 0x27000 },
	[GCC_USB3_PHY_BCR] = { 0x28000 },
	[GCC_USB3PHY_PHY_BCR] = { 0x28004 },
	[GCC_USB_PHY_CFG_AHB2PHY_BCR] = { 0x29000 },
};

static const struct clk_rcg_dfs_data gcc_dfs_clocks[] = {
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s0_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s1_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s2_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s3_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s4_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s5_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s6_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s7_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s8_clk_src),
};

static const struct regmap_config gcc_sdx75_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x1f41f0,
	.fast_io = true,
};

static const struct qcom_cc_desc gcc_sdx75_desc = {
	.config = &gcc_sdx75_regmap_config,
	.clks = gcc_sdx75_clocks,
	.num_clks = ARRAY_SIZE(gcc_sdx75_clocks),
	.resets = gcc_sdx75_resets,
	.num_resets = ARRAY_SIZE(gcc_sdx75_resets),
	.gdscs = gcc_sdx75_gdscs,
	.num_gdscs = ARRAY_SIZE(gcc_sdx75_gdscs),
};

static const struct of_device_id gcc_sdx75_match_table[] = {
	{ .compatible = "qcom,sdx75-gcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_sdx75_match_table);

static int gcc_sdx75_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret;

	regmap = qcom_cc_map(pdev, &gcc_sdx75_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ret = qcom_cc_register_rcg_dfs(regmap, gcc_dfs_clocks,
				       ARRAY_SIZE(gcc_dfs_clocks));
	if (ret)
		return ret;

	/* Keep some clocks always-on */
	qcom_branch_set_clk_en(regmap, 0x3e004); /* GCC_AHB_PCIE_LINK_CLK */
	qcom_branch_set_clk_en(regmap, 0x3e008); /* GCC_XO_PCIE_LINK_CLK */

	return qcom_cc_really_probe(pdev, &gcc_sdx75_desc, regmap);
}

static struct platform_driver gcc_sdx75_driver = {
	.probe = gcc_sdx75_probe,
	.driver = {
		.name = "gcc-sdx75",
		.of_match_table = gcc_sdx75_match_table,
	},
};

static int __init gcc_sdx75_init(void)
{
	return platform_driver_register(&gcc_sdx75_driver);
}
subsys_initcall(gcc_sdx75_init);

static void __exit gcc_sdx75_exit(void)
{
	platform_driver_unregister(&gcc_sdx75_driver);
}
module_exit(gcc_sdx75_exit);

MODULE_DESCRIPTION("QTI GCC SDX75 Driver");
MODULE_LICENSE("GPL");
