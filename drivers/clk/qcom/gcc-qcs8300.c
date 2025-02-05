// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,qcs8300-gcc.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "clk-regmap-phy-mux.h"
#include "common.h"
#include "gdsc.h"
#include "reset.h"

enum {
	DT_BI_TCXO,
	DT_SLEEP_CLK,
	DT_PCIE_0_PIPE_CLK,
	DT_PCIE_1_PIPE_CLK,
	DT_PCIE_PHY_AUX_CLK,
	DT_RXC0_REF_CLK,
	DT_UFS_PHY_RX_SYMBOL_0_CLK,
	DT_UFS_PHY_RX_SYMBOL_1_CLK,
	DT_UFS_PHY_TX_SYMBOL_0_CLK,
	DT_USB3_PHY_WRAPPER_GCC_USB30_PRIM_PIPE_CLK,
};

enum {
	P_BI_TCXO,
	P_GCC_GPLL0_OUT_EVEN,
	P_GCC_GPLL0_OUT_MAIN,
	P_GCC_GPLL1_OUT_MAIN,
	P_GCC_GPLL4_OUT_MAIN,
	P_GCC_GPLL7_OUT_MAIN,
	P_GCC_GPLL9_OUT_MAIN,
	P_PCIE_0_PIPE_CLK,
	P_PCIE_1_PIPE_CLK,
	P_PCIE_PHY_AUX_CLK,
	P_RXC0_REF_CLK,
	P_SLEEP_CLK,
	P_UFS_PHY_RX_SYMBOL_0_CLK,
	P_UFS_PHY_RX_SYMBOL_1_CLK,
	P_UFS_PHY_TX_SYMBOL_0_CLK,
	P_USB3_PHY_WRAPPER_GCC_USB30_PRIM_PIPE_CLK,
};

static struct clk_alpha_pll gcc_gpll0 = {
	.offset = 0x0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr = {
		.enable_reg = 0x4b028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll0",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_evo_ops,
		},
	},
};

static const struct clk_div_table post_div_table_gcc_gpll0_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv gcc_gpll0_out_even = {
	.offset = 0x0,
	.post_div_shift = 10,
	.post_div_table = post_div_table_gcc_gpll0_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_gcc_gpll0_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gpll0_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_gpll0.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_lucid_evo_ops,
	},
};

static struct clk_alpha_pll gcc_gpll1 = {
	.offset = 0x1000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr = {
		.enable_reg = 0x4b028,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll1",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_evo_ops,
		},
	},
};

static struct clk_alpha_pll gcc_gpll4 = {
	.offset = 0x4000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr = {
		.enable_reg = 0x4b028,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll4",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_evo_ops,
		},
	},
};

static struct clk_alpha_pll gcc_gpll7 = {
	.offset = 0x7000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr = {
		.enable_reg = 0x4b028,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll7",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_evo_ops,
		},
	},
};

static struct clk_alpha_pll gcc_gpll9 = {
	.offset = 0x9000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr = {
		.enable_reg = 0x4b028,
		.enable_mask = BIT(9),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll9",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_evo_ops,
		},
	},
};

static const struct parent_map gcc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_0[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_SLEEP_CLK, 5 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_1[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .index = DT_SLEEP_CLK },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_SLEEP_CLK, 5 },
};

static const struct clk_parent_data gcc_parent_data_2[] = {
	{ .index = DT_BI_TCXO },
	{ .index = DT_SLEEP_CLK },
};

static const struct parent_map gcc_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL1_OUT_MAIN, 4 },
	{ P_GCC_GPLL4_OUT_MAIN, 5 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_3[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll1.clkr.hw },
	{ .hw = &gcc_gpll4.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_4[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL4_OUT_MAIN, 5 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_4[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll4.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_5[] = {
	{ P_BI_TCXO, 0 },
};

static const struct clk_parent_data gcc_parent_data_5[] = {
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_6[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL7_OUT_MAIN, 2 },
	{ P_GCC_GPLL4_OUT_MAIN, 5 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_6[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll7.clkr.hw },
	{ .hw = &gcc_gpll4.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_7[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL7_OUT_MAIN, 2 },
	{ P_RXC0_REF_CLK, 3 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_7[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll7.clkr.hw },
	{ .index = DT_RXC0_REF_CLK },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_8[] = {
	{ P_PCIE_PHY_AUX_CLK, 1 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_8[] = {
	{ .index = DT_PCIE_PHY_AUX_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_10[] = {
	{ P_PCIE_PHY_AUX_CLK, 1 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_10[] = {
	{ .index = DT_PCIE_PHY_AUX_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_12[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL9_OUT_MAIN, 2 },
	{ P_GCC_GPLL4_OUT_MAIN, 5 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_12[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll9.clkr.hw },
	{ .hw = &gcc_gpll4.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_13[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
};

static const struct clk_parent_data gcc_parent_data_13[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
};

static const struct parent_map gcc_parent_map_14[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL4_OUT_MAIN, 3 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_14[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll4.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_15[] = {
	{ P_UFS_PHY_RX_SYMBOL_0_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_15[] = {
	{ .index = DT_UFS_PHY_RX_SYMBOL_0_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_16[] = {
	{ P_UFS_PHY_RX_SYMBOL_1_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_16[] = {
	{ .index = DT_UFS_PHY_RX_SYMBOL_1_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_17[] = {
	{ P_UFS_PHY_TX_SYMBOL_0_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_17[] = {
	{ .index = DT_UFS_PHY_TX_SYMBOL_0_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_18[] = {
	{ P_USB3_PHY_WRAPPER_GCC_USB30_PRIM_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_18[] = {
	{ .index = DT_USB3_PHY_WRAPPER_GCC_USB30_PRIM_PIPE_CLK },
	{ .index = DT_BI_TCXO },
};

static struct clk_regmap_mux gcc_pcie_0_phy_aux_clk_src = {
	.reg = 0xa9074,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_8,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_phy_aux_clk_src",
			.parent_data = gcc_parent_data_8,
			.num_parents = ARRAY_SIZE(gcc_parent_data_8),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_pcie_0_pipe_clk_src = {
	.reg = 0xa906c,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_PCIE_0_PIPE_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_mux gcc_pcie_1_phy_aux_clk_src = {
	.reg = 0x77074,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_10,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_phy_aux_clk_src",
			.parent_data = gcc_parent_data_10,
			.num_parents = ARRAY_SIZE(gcc_parent_data_10),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_pcie_1_pipe_clk_src = {
	.reg = 0x7706c,
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

static struct clk_regmap_mux gcc_ufs_phy_rx_symbol_0_clk_src = {
	.reg = 0x83060,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_15,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_rx_symbol_0_clk_src",
			.parent_data = gcc_parent_data_15,
			.num_parents = ARRAY_SIZE(gcc_parent_data_15),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_ufs_phy_rx_symbol_1_clk_src = {
	.reg = 0x830d0,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_16,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_rx_symbol_1_clk_src",
			.parent_data = gcc_parent_data_16,
			.num_parents = ARRAY_SIZE(gcc_parent_data_16),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_ufs_phy_tx_symbol_0_clk_src = {
	.reg = 0x83050,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_17,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_tx_symbol_0_clk_src",
			.parent_data = gcc_parent_data_17,
			.num_parents = ARRAY_SIZE(gcc_parent_data_17),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb3_prim_phy_pipe_clk_src = {
	.reg = 0x1b068,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_18,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_prim_phy_pipe_clk_src",
			.parent_data = gcc_parent_data_18,
			.num_parents = ARRAY_SIZE(gcc_parent_data_18),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static const struct freq_tbl ftbl_gcc_emac0_phy_aux_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_emac0_phy_aux_clk_src = {
	.cmd_rcgr = 0xb6028,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_emac0_phy_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_emac0_phy_aux_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_emac0_ptp_clk_src[] = {
	F(125000000, P_GCC_GPLL7_OUT_MAIN, 8, 0, 0),
	F(230400000, P_GCC_GPLL4_OUT_MAIN, 3.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_emac0_ptp_clk_src = {
	.cmd_rcgr = 0xb6060,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_6,
	.freq_tbl = ftbl_gcc_emac0_ptp_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_emac0_ptp_clk_src",
		.parent_data = gcc_parent_data_6,
		.num_parents = ARRAY_SIZE(gcc_parent_data_6),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_emac0_rgmii_clk_src[] = {
	F(5000000, P_GCC_GPLL0_OUT_EVEN, 10, 1, 6),
	F(50000000, P_GCC_GPLL0_OUT_EVEN, 6, 0, 0),
	F(125000000, P_GCC_GPLL7_OUT_MAIN, 8, 0, 0),
	F(250000000, P_GCC_GPLL7_OUT_MAIN, 4, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_emac0_rgmii_clk_src = {
	.cmd_rcgr = 0xb6048,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_7,
	.freq_tbl = ftbl_gcc_emac0_rgmii_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_emac0_rgmii_clk_src",
		.parent_data = gcc_parent_data_7,
		.num_parents = ARRAY_SIZE(gcc_parent_data_7),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_gp1_clk_src[] = {
	F(100000000, P_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	F(200000000, P_GCC_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_gp1_clk_src = {
	.cmd_rcgr = 0x70004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp1_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_gp2_clk_src = {
	.cmd_rcgr = 0x71004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp2_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_gp3_clk_src = {
	.cmd_rcgr = 0x62004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp3_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_gp4_clk_src = {
	.cmd_rcgr = 0x1e004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp4_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_gp5_clk_src = {
	.cmd_rcgr = 0x1f004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp5_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_0_aux_clk_src = {
	.cmd_rcgr = 0xa9078,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_emac0_phy_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_0_aux_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcie_0_phy_rchng_clk_src[] = {
	F(100000000, P_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie_0_phy_rchng_clk_src = {
	.cmd_rcgr = 0xa9054,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_phy_rchng_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_0_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_1_aux_clk_src = {
	.cmd_rcgr = 0x77078,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_emac0_phy_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_1_aux_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_1_phy_rchng_clk_src = {
	.cmd_rcgr = 0x77054,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_phy_rchng_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_1_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pdm2_clk_src[] = {
	F(60000000, P_GCC_GPLL0_OUT_MAIN, 10, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pdm2_clk_src = {
	.cmd_rcgr = 0x3f010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pdm2_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pdm2_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap0_s0_clk_src[] = {
	F(7372800, P_GCC_GPLL0_OUT_EVEN, 1, 384, 15625),
	F(14745600, P_GCC_GPLL0_OUT_EVEN, 1, 768, 15625),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(29491200, P_GCC_GPLL0_OUT_EVEN, 1, 1536, 15625),
	F(32000000, P_GCC_GPLL0_OUT_EVEN, 1, 8, 75),
	F(48000000, P_GCC_GPLL0_OUT_EVEN, 1, 4, 25),
	F(64000000, P_GCC_GPLL0_OUT_EVEN, 1, 16, 75),
	F(80000000, P_GCC_GPLL0_OUT_EVEN, 1, 4, 15),
	F(96000000, P_GCC_GPLL0_OUT_EVEN, 1, 8, 25),
	F(120000000, P_GCC_GPLL0_OUT_MAIN, 5, 0, 0),
	{ }
};

static struct clk_init_data gcc_qupv3_wrap0_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s0_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s0_clk_src = {
	.cmd_rcgr = 0x23154,
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
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s1_clk_src = {
	.cmd_rcgr = 0x23288,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s1_clk_src_init,
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap0_s2_clk_src[] = {
	F(7372800, P_GCC_GPLL0_OUT_EVEN, 1, 384, 15625),
	F(14745600, P_GCC_GPLL0_OUT_EVEN, 1, 768, 15625),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(29491200, P_GCC_GPLL0_OUT_EVEN, 1, 1536, 15625),
	F(32000000, P_GCC_GPLL0_OUT_EVEN, 1, 8, 75),
	F(48000000, P_GCC_GPLL0_OUT_EVEN, 1, 4, 25),
	F(64000000, P_GCC_GPLL0_OUT_EVEN, 1, 16, 75),
	F(80000000, P_GCC_GPLL0_OUT_EVEN, 1, 4, 15),
	F(96000000, P_GCC_GPLL0_OUT_EVEN, 1, 8, 25),
	F(100000000, P_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	{ }
};

static struct clk_init_data gcc_qupv3_wrap0_s2_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s2_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s2_clk_src = {
	.cmd_rcgr = 0x233bc,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s2_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s3_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s3_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s3_clk_src = {
	.cmd_rcgr = 0x234f0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s3_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s4_clk_src",
	.parent_data = gcc_parent_data_4,
	.num_parents = ARRAY_SIZE(gcc_parent_data_4),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s4_clk_src = {
	.cmd_rcgr = 0x23624,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s4_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s5_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s5_clk_src = {
	.cmd_rcgr = 0x23758,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s5_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s6_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s6_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s6_clk_src = {
	.cmd_rcgr = 0x2388c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s6_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s7_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s7_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s7_clk_src = {
	.cmd_rcgr = 0x239c0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s7_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s0_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s0_clk_src = {
	.cmd_rcgr = 0x24154,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s0_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s1_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s1_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s1_clk_src = {
	.cmd_rcgr = 0x24288,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s1_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s2_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s2_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s2_clk_src = {
	.cmd_rcgr = 0x243bc,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s2_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s3_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s3_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s3_clk_src = {
	.cmd_rcgr = 0x244f0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s3_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s4_clk_src",
	.parent_data = gcc_parent_data_4,
	.num_parents = ARRAY_SIZE(gcc_parent_data_4),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s4_clk_src = {
	.cmd_rcgr = 0x24624,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s4_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s5_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s5_clk_src = {
	.cmd_rcgr = 0x24758,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s5_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s6_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s6_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s6_clk_src = {
	.cmd_rcgr = 0x2488c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s6_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s7_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s7_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s7_clk_src = {
	.cmd_rcgr = 0x249c0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s7_clk_src_init,
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap3_s0_clk_src[] = {
	F(7372800, P_GCC_GPLL0_OUT_EVEN, 1, 384, 15625),
	F(14745600, P_GCC_GPLL0_OUT_EVEN, 1, 768, 15625),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(29491200, P_GCC_GPLL0_OUT_EVEN, 1, 1536, 15625),
	F(32000000, P_GCC_GPLL0_OUT_EVEN, 1, 8, 75),
	F(48000000, P_GCC_GPLL0_OUT_EVEN, 1, 4, 25),
	F(64000000, P_GCC_GPLL0_OUT_EVEN, 1, 16, 75),
	F(75000000, P_GCC_GPLL0_OUT_EVEN, 4, 0, 0),
	F(80000000, P_GCC_GPLL0_OUT_EVEN, 1, 4, 15),
	F(96000000, P_GCC_GPLL0_OUT_EVEN, 1, 8, 25),
	F(100000000, P_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	F(403200000, P_GCC_GPLL4_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_init_data gcc_qupv3_wrap3_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap3_s0_clk_src",
	.parent_data = gcc_parent_data_3,
	.num_parents = ARRAY_SIZE(gcc_parent_data_3),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap3_s0_clk_src = {
	.cmd_rcgr = 0xc4158,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_qupv3_wrap3_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap3_s0_clk_src_init,
};

static const struct freq_tbl ftbl_gcc_sdcc1_apps_clk_src[] = {
	F(144000, P_BI_TCXO, 16, 3, 25),
	F(400000, P_BI_TCXO, 12, 1, 4),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(20000000, P_GCC_GPLL0_OUT_EVEN, 5, 1, 3),
	F(25000000, P_GCC_GPLL0_OUT_EVEN, 12, 0, 0),
	F(50000000, P_GCC_GPLL0_OUT_EVEN, 6, 0, 0),
	F(100000000, P_GCC_GPLL0_OUT_EVEN, 3, 0, 0),
	F(192000000, P_GCC_GPLL9_OUT_MAIN, 4, 0, 0),
	F(384000000, P_GCC_GPLL9_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc1_apps_clk_src = {
	.cmd_rcgr = 0x20014,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_12,
	.freq_tbl = ftbl_gcc_sdcc1_apps_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_sdcc1_apps_clk_src",
		.parent_data = gcc_parent_data_12,
		.num_parents = ARRAY_SIZE(gcc_parent_data_12),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_floor_ops,
	},
};

static const struct freq_tbl ftbl_gcc_sdcc1_ice_core_clk_src[] = {
	F(150000000, P_GCC_GPLL0_OUT_MAIN, 4, 0, 0),
	F(300000000, P_GCC_GPLL0_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc1_ice_core_clk_src = {
	.cmd_rcgr = 0x2002c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_13,
	.freq_tbl = ftbl_gcc_sdcc1_ice_core_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_sdcc1_ice_core_clk_src",
		.parent_data = gcc_parent_data_13,
		.num_parents = ARRAY_SIZE(gcc_parent_data_13),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_floor_ops,
	},
};

static const struct freq_tbl ftbl_gcc_ufs_phy_axi_clk_src[] = {
	F(25000000, P_GCC_GPLL0_OUT_EVEN, 12, 0, 0),
	F(75000000, P_GCC_GPLL0_OUT_EVEN, 4, 0, 0),
	F(150000000, P_GCC_GPLL0_OUT_MAIN, 4, 0, 0),
	F(300000000, P_GCC_GPLL0_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_phy_axi_clk_src = {
	.cmd_rcgr = 0x8302c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_phy_axi_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_ufs_phy_axi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_ufs_phy_ice_core_clk_src[] = {
	F(75000000, P_GCC_GPLL0_OUT_EVEN, 4, 0, 0),
	F(201600000, P_GCC_GPLL4_OUT_MAIN, 4, 0, 0),
	F(403200000, P_GCC_GPLL4_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_phy_ice_core_clk_src = {
	.cmd_rcgr = 0x83074,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_14,
	.freq_tbl = ftbl_gcc_ufs_phy_ice_core_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_ufs_phy_ice_core_clk_src",
		.parent_data = gcc_parent_data_14,
		.num_parents = ARRAY_SIZE(gcc_parent_data_14),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_ufs_phy_phy_aux_clk_src = {
	.cmd_rcgr = 0x830a8,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_emac0_phy_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_ufs_phy_phy_aux_clk_src",
		.parent_data = gcc_parent_data_5,
		.num_parents = ARRAY_SIZE(gcc_parent_data_5),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_ufs_phy_unipro_core_clk_src[] = {
	F(75000000, P_GCC_GPLL0_OUT_EVEN, 4, 0, 0),
	F(150000000, P_GCC_GPLL0_OUT_MAIN, 4, 0, 0),
	F(300000000, P_GCC_GPLL0_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_phy_unipro_core_clk_src = {
	.cmd_rcgr = 0x8308c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_phy_unipro_core_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_ufs_phy_unipro_core_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb20_master_clk_src[] = {
	F(120000000, P_GCC_GPLL0_OUT_MAIN, 5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb20_master_clk_src = {
	.cmd_rcgr = 0x1c028,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb20_master_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb20_master_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_usb20_mock_utmi_clk_src = {
	.cmd_rcgr = 0x1c040,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_emac0_phy_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb20_mock_utmi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb30_prim_master_clk_src[] = {
	F(133333333, P_GCC_GPLL0_OUT_MAIN, 4.5, 0, 0),
	F(200000000, P_GCC_GPLL0_OUT_MAIN, 3, 0, 0),
	F(240000000, P_GCC_GPLL0_OUT_MAIN, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb30_prim_master_clk_src = {
	.cmd_rcgr = 0x1b028,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb30_prim_master_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_prim_master_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_usb30_prim_mock_utmi_clk_src = {
	.cmd_rcgr = 0x1b040,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_emac0_phy_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_prim_mock_utmi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_usb3_prim_phy_aux_clk_src = {
	.cmd_rcgr = 0x1b06c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_emac0_phy_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb3_prim_phy_aux_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_regmap_div gcc_pcie_0_pipe_div_clk_src = {
	.reg = 0xa9070,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_0_pipe_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_pcie_0_pipe_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_pcie_1_pipe_div_clk_src = {
	.reg = 0x77070,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_1_pipe_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_pcie_1_pipe_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_qupv3_wrap3_s0_div_clk_src = {
	.reg = 0xc4288,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_wrap3_s0_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_qupv3_wrap3_s0_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_usb20_mock_utmi_postdiv_clk_src = {
	.reg = 0x1c058,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb20_mock_utmi_postdiv_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_usb20_mock_utmi_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_usb30_prim_mock_utmi_postdiv_clk_src = {
	.reg = 0x1b058,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_prim_mock_utmi_postdiv_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_usb30_prim_mock_utmi_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch gcc_aggre_noc_qupv3_axi_clk = {
	.halt_reg = 0x8e200,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x8e200,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x4b000,
		.enable_mask = BIT(28),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_noc_qupv3_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_ufs_phy_axi_clk = {
	.halt_reg = 0x830d4,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x830d4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x830d4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_ufs_phy_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_ufs_phy_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_usb2_prim_axi_clk = {
	.halt_reg = 0x1c05c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x1c05c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1c05c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_usb2_prim_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb20_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_usb3_prim_axi_clk = {
	.halt_reg = 0x1b084,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x1b084,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1b084,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_usb3_prim_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb30_prim_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ahb2phy0_clk = {
	.halt_reg = 0x76004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x76004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x76004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ahb2phy0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ahb2phy2_clk = {
	.halt_reg = 0x76008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x76008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x76008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ahb2phy2_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ahb2phy3_clk = {
	.halt_reg = 0x7600c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x7600c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7600c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ahb2phy3_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_boot_rom_ahb_clk = {
	.halt_reg = 0x44004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x44004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x4b000,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_boot_rom_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_hf_axi_clk = {
	.halt_reg = 0x32010,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x32010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x32010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camera_hf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_sf_axi_clk = {
	.halt_reg = 0x32018,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x32018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x32018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camera_sf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_throttle_xo_clk = {
	.halt_reg = 0x32024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x32024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camera_throttle_xo_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_usb2_prim_axi_clk = {
	.halt_reg = 0x1c060,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x1c060,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1c060,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cfg_noc_usb2_prim_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb20_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_usb3_prim_axi_clk = {
	.halt_reg = 0x1b088,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x1b088,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1b088,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cfg_noc_usb3_prim_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb30_prim_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ddrss_gpu_axi_clk = {
	.halt_reg = 0x7d164,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x7d164,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d164,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ddrss_gpu_axi_clk",
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch gcc_disp_hf_axi_clk = {
	.halt_reg = 0x33010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x33010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x33010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_disp_hf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_edp_ref_clkref_en = {
	.halt_reg = 0x97448,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x97448,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_edp_ref_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_axi_clk = {
	.halt_reg = 0xb6018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xb6018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xb6018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_phy_aux_clk = {
	.halt_reg = 0xb6024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb6024,
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
	.halt_reg = 0xb6040,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb6040,
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
	.halt_reg = 0xb6044,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb6044,
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

static struct clk_branch gcc_emac0_slv_ahb_clk = {
	.halt_reg = 0xb6020,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xb6020,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xb6020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_slv_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp1_clk = {
	.halt_reg = 0x70000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x70000,
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
	.halt_reg = 0x71000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x71000,
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
	.halt_reg = 0x62000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x62000,
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

static struct clk_branch gcc_gp4_clk = {
	.halt_reg = 0x1e000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1e000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gp4_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_gp4_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp5_clk = {
	.halt_reg = 0x1f000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1f000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gp5_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_gp5_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_gpll0_clk_src = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x4b000,
		.enable_mask = BIT(15),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpu_gpll0_clk_src",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_gpll0.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_gpll0_div_clk_src = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x4b000,
		.enable_mask = BIT(16),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpu_gpll0_div_clk_src",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_gpll0_out_even.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_memnoc_gfx_center_pipeline_clk = {
	.halt_reg = 0x7d160,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x7d160,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d160,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpu_memnoc_gfx_center_pipeline_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_memnoc_gfx_clk = {
	.halt_reg = 0x7d010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x7d010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpu_memnoc_gfx_clk",
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch gcc_gpu_snoc_dvm_gfx_clk = {
	.halt_reg = 0x7d01c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x7d01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpu_snoc_dvm_gfx_clk",
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch gcc_gpu_tcu_throttle_ahb_clk = {
	.halt_reg = 0x7d008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x7d008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpu_tcu_throttle_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_tcu_throttle_clk = {
	.halt_reg = 0x7d014,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x7d014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpu_tcu_throttle_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_aux_clk = {
	.halt_reg = 0xa9038,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b010,
		.enable_mask = BIT(16),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_0_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_cfg_ahb_clk = {
	.halt_reg = 0xa902c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xa902c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x4b010,
		.enable_mask = BIT(12),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_mstr_axi_clk = {
	.halt_reg = 0xa9024,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b010,
		.enable_mask = BIT(11),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_phy_aux_clk = {
	.halt_reg = 0xa9030,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b010,
		.enable_mask = BIT(13),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_0_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_phy_rchng_clk = {
	.halt_reg = 0xa9050,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b010,
		.enable_mask = BIT(15),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_phy_rchng_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_0_phy_rchng_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_pipe_clk = {
	.halt_reg = 0xa9040,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x4b010,
		.enable_mask = BIT(14),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_0_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_pipediv2_clk = {
	.halt_reg = 0xa9048,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x4b018,
		.enable_mask = BIT(22),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_pipediv2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_0_pipe_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_slv_axi_clk = {
	.halt_reg = 0xa901c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b010,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_slv_q2a_axi_clk = {
	.halt_reg = 0xa9018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b018,
		.enable_mask = BIT(12),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_aux_clk = {
	.halt_reg = 0x77038,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b000,
		.enable_mask = BIT(31),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_1_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_cfg_ahb_clk = {
	.halt_reg = 0x7702c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x7702c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_mstr_axi_clk = {
	.halt_reg = 0x77024,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_phy_aux_clk = {
	.halt_reg = 0x77030,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(3),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_1_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_phy_rchng_clk = {
	.halt_reg = 0x77050,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b000,
		.enable_mask = BIT(22),
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
	.halt_reg = 0x77040,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(4),
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

static struct clk_branch gcc_pcie_1_pipediv2_clk = {
	.halt_reg = 0x77048,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x4b018,
		.enable_mask = BIT(16),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_pipediv2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_1_pipe_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_slv_axi_clk = {
	.halt_reg = 0x7701c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_slv_q2a_axi_clk = {
	.halt_reg = 0x77018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_clkref_en = {
	.halt_reg = 0x9746c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x9746c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_throttle_cfg_clk = {
	.halt_reg = 0xb2034,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b020,
		.enable_mask = BIT(15),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_throttle_cfg_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm2_clk = {
	.halt_reg = 0x3f00c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3f00c,
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
	.halt_reg = 0x3f004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x3f004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3f004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pdm_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm_xo4_clk = {
	.halt_reg = 0x3f008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3f008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pdm_xo4_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_camera_nrt_ahb_clk = {
	.halt_reg = 0x32008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x32008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x32008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_camera_nrt_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_camera_rt_ahb_clk = {
	.halt_reg = 0x3200c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x3200c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3200c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_camera_rt_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_disp_ahb_clk = {
	.halt_reg = 0x33008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x33008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x33008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_disp_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_disp_rot_ahb_clk = {
	.halt_reg = 0x3300c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x3300c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_disp_rot_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_cvp_ahb_clk = {
	.halt_reg = 0x34008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x34008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x34008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_video_cvp_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_vcodec_ahb_clk = {
	.halt_reg = 0x3400c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x3400c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3400c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_video_vcodec_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_vcpu_ahb_clk = {
	.halt_reg = 0x34010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x34010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x34010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_video_vcpu_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_core_2x_clk = {
	.halt_reg = 0x23018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(9),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_core_clk = {
	.halt_reg = 0x2300c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s0_clk = {
	.halt_reg = 0x2314c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(10),
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
	.halt_reg = 0x23280,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(11),
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
	.halt_reg = 0x233b4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(12),
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
	.halt_reg = 0x234e8,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(13),
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
	.halt_reg = 0x2361c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(14),
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
	.halt_reg = 0x23750,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(15),
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
	.halt_reg = 0x23884,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(16),
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
	.halt_reg = 0x239b8,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(17),
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

static struct clk_branch gcc_qupv3_wrap1_core_2x_clk = {
	.halt_reg = 0x24018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(18),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_core_clk = {
	.halt_reg = 0x2400c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(19),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s0_clk = {
	.halt_reg = 0x2414c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(22),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_s0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap1_s0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s1_clk = {
	.halt_reg = 0x24280,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(23),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_s1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap1_s1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s2_clk = {
	.halt_reg = 0x243b4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(24),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_s2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap1_s2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s3_clk = {
	.halt_reg = 0x244e8,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(25),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_s3_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap1_s3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s4_clk = {
	.halt_reg = 0x2461c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(26),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_s4_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap1_s4_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s5_clk = {
	.halt_reg = 0x24750,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(27),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_s5_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap1_s5_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s6_clk = {
	.halt_reg = 0x24884,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b018,
		.enable_mask = BIT(27),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_s6_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap1_s6_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s7_clk = {
	.halt_reg = 0x249b8,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b018,
		.enable_mask = BIT(28),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_s7_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap1_s7_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap3_core_2x_clk = {
	.halt_reg = 0xc4018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b000,
		.enable_mask = BIT(24),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap3_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap3_core_clk = {
	.halt_reg = 0xc400c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b000,
		.enable_mask = BIT(23),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap3_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap3_qspi_clk = {
	.halt_reg = 0xc4284,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b000,
		.enable_mask = BIT(26),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap3_qspi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap3_s0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap3_s0_clk = {
	.halt_reg = 0xc4150,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4b000,
		.enable_mask = BIT(25),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap3_s0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap3_s0_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_0_m_ahb_clk = {
	.halt_reg = 0x23004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x23004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(6),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_0_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_0_s_ahb_clk = {
	.halt_reg = 0x23008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x23008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_0_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_1_m_ahb_clk = {
	.halt_reg = 0x24004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x24004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(20),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_1_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_1_s_ahb_clk = {
	.halt_reg = 0x24008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x24008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(21),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_1_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_3_m_ahb_clk = {
	.halt_reg = 0xc4004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xc4004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x4b000,
		.enable_mask = BIT(27),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_3_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_3_s_ahb_clk = {
	.halt_reg = 0xc4008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xc4008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x4b000,
		.enable_mask = BIT(20),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_3_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_ahb_clk = {
	.halt_reg = 0x2000c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2000c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sdcc1_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_apps_clk = {
	.halt_reg = 0x20004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x20004,
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

static struct clk_branch gcc_sdcc1_ice_core_clk = {
	.halt_reg = 0x20044,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x20044,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x20044,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sdcc1_ice_core_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_sdcc1_ice_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sgmi_clkref_en = {
	.halt_reg = 0x97034,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x97034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sgmi_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_ahb_clk = {
	.halt_reg = 0x83020,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x83020,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x83020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_axi_clk = {
	.halt_reg = 0x83018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x83018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x83018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_ufs_phy_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_ice_core_clk = {
	.halt_reg = 0x8306c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x8306c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x8306c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_ice_core_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_ufs_phy_ice_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_phy_aux_clk = {
	.halt_reg = 0x830a4,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x830a4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x830a4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_ufs_phy_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_rx_symbol_0_clk = {
	.halt_reg = 0x83028,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x83028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_rx_symbol_0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_ufs_phy_rx_symbol_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_rx_symbol_1_clk = {
	.halt_reg = 0x830c0,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x830c0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_rx_symbol_1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_ufs_phy_rx_symbol_1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_tx_symbol_0_clk = {
	.halt_reg = 0x83024,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x83024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_tx_symbol_0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_ufs_phy_tx_symbol_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_unipro_core_clk = {
	.halt_reg = 0x83064,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x83064,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x83064,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_unipro_core_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_ufs_phy_unipro_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb20_master_clk = {
	.halt_reg = 0x1c018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1c018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb20_master_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb20_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb20_mock_utmi_clk = {
	.halt_reg = 0x1c024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1c024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb20_mock_utmi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb20_mock_utmi_postdiv_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb20_sleep_clk = {
	.halt_reg = 0x1c020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1c020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb20_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_prim_master_clk = {
	.halt_reg = 0x1b018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1b018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_prim_master_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb30_prim_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_prim_mock_utmi_clk = {
	.halt_reg = 0x1b024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1b024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_prim_mock_utmi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb30_prim_mock_utmi_postdiv_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_prim_sleep_clk = {
	.halt_reg = 0x1b020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1b020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_prim_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_phy_aux_clk = {
	.halt_reg = 0x1b05c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1b05c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_prim_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb3_prim_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_phy_com_aux_clk = {
	.halt_reg = 0x1b060,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1b060,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_prim_phy_com_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb3_prim_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_phy_pipe_clk = {
	.halt_reg = 0x1b064,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x1b064,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1b064,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_prim_phy_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb3_prim_phy_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb_clkref_en = {
	.halt_reg = 0x97468,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x97468,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_axi0_clk = {
	.halt_reg = 0x34014,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x34014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x34014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_video_axi0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_axi1_clk = {
	.halt_reg = 0x3401c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x3401c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3401c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_video_axi1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc gcc_emac0_gdsc = {
	.gdscr = 0xb6004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_emac0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc gcc_pcie_0_gdsc = {
	.gdscr = 0xa9004,
	.collapse_ctrl = 0x4b104,
	.collapse_mask = BIT(0),
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_pcie_0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE | RETAIN_FF_ENABLE | POLL_CFG_GDSCR,
};

static struct gdsc gcc_pcie_1_gdsc = {
	.gdscr = 0x77004,
	.collapse_ctrl = 0x4b104,
	.collapse_mask = BIT(1),
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_pcie_1_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE | RETAIN_FF_ENABLE | POLL_CFG_GDSCR,
};

static struct gdsc gcc_ufs_phy_gdsc = {
	.gdscr = 0x83004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_ufs_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE | POLL_CFG_GDSCR,
};

static struct gdsc gcc_usb20_prim_gdsc = {
	.gdscr = 0x1c004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_usb20_prim_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE | POLL_CFG_GDSCR,
};

static struct gdsc gcc_usb30_prim_gdsc = {
	.gdscr = 0x1b004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_usb30_prim_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE | POLL_CFG_GDSCR,
};

static struct clk_regmap *gcc_qcs8300_clocks[] = {
	[GCC_AGGRE_NOC_QUPV3_AXI_CLK] = &gcc_aggre_noc_qupv3_axi_clk.clkr,
	[GCC_AGGRE_UFS_PHY_AXI_CLK] = &gcc_aggre_ufs_phy_axi_clk.clkr,
	[GCC_AGGRE_USB2_PRIM_AXI_CLK] = &gcc_aggre_usb2_prim_axi_clk.clkr,
	[GCC_AGGRE_USB3_PRIM_AXI_CLK] = &gcc_aggre_usb3_prim_axi_clk.clkr,
	[GCC_AHB2PHY0_CLK] = &gcc_ahb2phy0_clk.clkr,
	[GCC_AHB2PHY2_CLK] = &gcc_ahb2phy2_clk.clkr,
	[GCC_AHB2PHY3_CLK] = &gcc_ahb2phy3_clk.clkr,
	[GCC_BOOT_ROM_AHB_CLK] = &gcc_boot_rom_ahb_clk.clkr,
	[GCC_CAMERA_HF_AXI_CLK] = &gcc_camera_hf_axi_clk.clkr,
	[GCC_CAMERA_SF_AXI_CLK] = &gcc_camera_sf_axi_clk.clkr,
	[GCC_CAMERA_THROTTLE_XO_CLK] = &gcc_camera_throttle_xo_clk.clkr,
	[GCC_CFG_NOC_USB2_PRIM_AXI_CLK] = &gcc_cfg_noc_usb2_prim_axi_clk.clkr,
	[GCC_CFG_NOC_USB3_PRIM_AXI_CLK] = &gcc_cfg_noc_usb3_prim_axi_clk.clkr,
	[GCC_DDRSS_GPU_AXI_CLK] = &gcc_ddrss_gpu_axi_clk.clkr,
	[GCC_DISP_HF_AXI_CLK] = &gcc_disp_hf_axi_clk.clkr,
	[GCC_EDP_REF_CLKREF_EN] = &gcc_edp_ref_clkref_en.clkr,
	[GCC_EMAC0_AXI_CLK] = &gcc_emac0_axi_clk.clkr,
	[GCC_EMAC0_PHY_AUX_CLK] = &gcc_emac0_phy_aux_clk.clkr,
	[GCC_EMAC0_PHY_AUX_CLK_SRC] = &gcc_emac0_phy_aux_clk_src.clkr,
	[GCC_EMAC0_PTP_CLK] = &gcc_emac0_ptp_clk.clkr,
	[GCC_EMAC0_PTP_CLK_SRC] = &gcc_emac0_ptp_clk_src.clkr,
	[GCC_EMAC0_RGMII_CLK] = &gcc_emac0_rgmii_clk.clkr,
	[GCC_EMAC0_RGMII_CLK_SRC] = &gcc_emac0_rgmii_clk_src.clkr,
	[GCC_EMAC0_SLV_AHB_CLK] = &gcc_emac0_slv_ahb_clk.clkr,
	[GCC_GP1_CLK] = &gcc_gp1_clk.clkr,
	[GCC_GP1_CLK_SRC] = &gcc_gp1_clk_src.clkr,
	[GCC_GP2_CLK] = &gcc_gp2_clk.clkr,
	[GCC_GP2_CLK_SRC] = &gcc_gp2_clk_src.clkr,
	[GCC_GP3_CLK] = &gcc_gp3_clk.clkr,
	[GCC_GP3_CLK_SRC] = &gcc_gp3_clk_src.clkr,
	[GCC_GP4_CLK] = &gcc_gp4_clk.clkr,
	[GCC_GP4_CLK_SRC] = &gcc_gp4_clk_src.clkr,
	[GCC_GP5_CLK] = &gcc_gp5_clk.clkr,
	[GCC_GP5_CLK_SRC] = &gcc_gp5_clk_src.clkr,
	[GCC_GPLL0] = &gcc_gpll0.clkr,
	[GCC_GPLL0_OUT_EVEN] = &gcc_gpll0_out_even.clkr,
	[GCC_GPLL1] = &gcc_gpll1.clkr,
	[GCC_GPLL4] = &gcc_gpll4.clkr,
	[GCC_GPLL7] = &gcc_gpll7.clkr,
	[GCC_GPLL9] = &gcc_gpll9.clkr,
	[GCC_GPU_GPLL0_CLK_SRC] = &gcc_gpu_gpll0_clk_src.clkr,
	[GCC_GPU_GPLL0_DIV_CLK_SRC] = &gcc_gpu_gpll0_div_clk_src.clkr,
	[GCC_GPU_MEMNOC_GFX_CENTER_PIPELINE_CLK] = &gcc_gpu_memnoc_gfx_center_pipeline_clk.clkr,
	[GCC_GPU_MEMNOC_GFX_CLK] = &gcc_gpu_memnoc_gfx_clk.clkr,
	[GCC_GPU_SNOC_DVM_GFX_CLK] = &gcc_gpu_snoc_dvm_gfx_clk.clkr,
	[GCC_GPU_TCU_THROTTLE_AHB_CLK] = &gcc_gpu_tcu_throttle_ahb_clk.clkr,
	[GCC_GPU_TCU_THROTTLE_CLK] = &gcc_gpu_tcu_throttle_clk.clkr,
	[GCC_PCIE_0_AUX_CLK] = &gcc_pcie_0_aux_clk.clkr,
	[GCC_PCIE_0_AUX_CLK_SRC] = &gcc_pcie_0_aux_clk_src.clkr,
	[GCC_PCIE_0_CFG_AHB_CLK] = &gcc_pcie_0_cfg_ahb_clk.clkr,
	[GCC_PCIE_0_MSTR_AXI_CLK] = &gcc_pcie_0_mstr_axi_clk.clkr,
	[GCC_PCIE_0_PHY_AUX_CLK] = &gcc_pcie_0_phy_aux_clk.clkr,
	[GCC_PCIE_0_PHY_AUX_CLK_SRC] = &gcc_pcie_0_phy_aux_clk_src.clkr,
	[GCC_PCIE_0_PHY_RCHNG_CLK] = &gcc_pcie_0_phy_rchng_clk.clkr,
	[GCC_PCIE_0_PHY_RCHNG_CLK_SRC] = &gcc_pcie_0_phy_rchng_clk_src.clkr,
	[GCC_PCIE_0_PIPE_CLK] = &gcc_pcie_0_pipe_clk.clkr,
	[GCC_PCIE_0_PIPE_CLK_SRC] = &gcc_pcie_0_pipe_clk_src.clkr,
	[GCC_PCIE_0_PIPE_DIV_CLK_SRC] = &gcc_pcie_0_pipe_div_clk_src.clkr,
	[GCC_PCIE_0_PIPEDIV2_CLK] = &gcc_pcie_0_pipediv2_clk.clkr,
	[GCC_PCIE_0_SLV_AXI_CLK] = &gcc_pcie_0_slv_axi_clk.clkr,
	[GCC_PCIE_0_SLV_Q2A_AXI_CLK] = &gcc_pcie_0_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_1_AUX_CLK] = &gcc_pcie_1_aux_clk.clkr,
	[GCC_PCIE_1_AUX_CLK_SRC] = &gcc_pcie_1_aux_clk_src.clkr,
	[GCC_PCIE_1_CFG_AHB_CLK] = &gcc_pcie_1_cfg_ahb_clk.clkr,
	[GCC_PCIE_1_MSTR_AXI_CLK] = &gcc_pcie_1_mstr_axi_clk.clkr,
	[GCC_PCIE_1_PHY_AUX_CLK] = &gcc_pcie_1_phy_aux_clk.clkr,
	[GCC_PCIE_1_PHY_AUX_CLK_SRC] = &gcc_pcie_1_phy_aux_clk_src.clkr,
	[GCC_PCIE_1_PHY_RCHNG_CLK] = &gcc_pcie_1_phy_rchng_clk.clkr,
	[GCC_PCIE_1_PHY_RCHNG_CLK_SRC] = &gcc_pcie_1_phy_rchng_clk_src.clkr,
	[GCC_PCIE_1_PIPE_CLK] = &gcc_pcie_1_pipe_clk.clkr,
	[GCC_PCIE_1_PIPE_CLK_SRC] = &gcc_pcie_1_pipe_clk_src.clkr,
	[GCC_PCIE_1_PIPE_DIV_CLK_SRC] = &gcc_pcie_1_pipe_div_clk_src.clkr,
	[GCC_PCIE_1_PIPEDIV2_CLK] = &gcc_pcie_1_pipediv2_clk.clkr,
	[GCC_PCIE_1_SLV_AXI_CLK] = &gcc_pcie_1_slv_axi_clk.clkr,
	[GCC_PCIE_1_SLV_Q2A_AXI_CLK] = &gcc_pcie_1_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_CLKREF_EN] = &gcc_pcie_clkref_en.clkr,
	[GCC_PCIE_THROTTLE_CFG_CLK] = &gcc_pcie_throttle_cfg_clk.clkr,
	[GCC_PDM2_CLK] = &gcc_pdm2_clk.clkr,
	[GCC_PDM2_CLK_SRC] = &gcc_pdm2_clk_src.clkr,
	[GCC_PDM_AHB_CLK] = &gcc_pdm_ahb_clk.clkr,
	[GCC_PDM_XO4_CLK] = &gcc_pdm_xo4_clk.clkr,
	[GCC_QMIP_CAMERA_NRT_AHB_CLK] = &gcc_qmip_camera_nrt_ahb_clk.clkr,
	[GCC_QMIP_CAMERA_RT_AHB_CLK] = &gcc_qmip_camera_rt_ahb_clk.clkr,
	[GCC_QMIP_DISP_AHB_CLK] = &gcc_qmip_disp_ahb_clk.clkr,
	[GCC_QMIP_DISP_ROT_AHB_CLK] = &gcc_qmip_disp_rot_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_CVP_AHB_CLK] = &gcc_qmip_video_cvp_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_VCODEC_AHB_CLK] = &gcc_qmip_video_vcodec_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_VCPU_AHB_CLK] = &gcc_qmip_video_vcpu_ahb_clk.clkr,
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
	[GCC_QUPV3_WRAP1_CORE_2X_CLK] = &gcc_qupv3_wrap1_core_2x_clk.clkr,
	[GCC_QUPV3_WRAP1_CORE_CLK] = &gcc_qupv3_wrap1_core_clk.clkr,
	[GCC_QUPV3_WRAP1_S0_CLK] = &gcc_qupv3_wrap1_s0_clk.clkr,
	[GCC_QUPV3_WRAP1_S0_CLK_SRC] = &gcc_qupv3_wrap1_s0_clk_src.clkr,
	[GCC_QUPV3_WRAP1_S1_CLK] = &gcc_qupv3_wrap1_s1_clk.clkr,
	[GCC_QUPV3_WRAP1_S1_CLK_SRC] = &gcc_qupv3_wrap1_s1_clk_src.clkr,
	[GCC_QUPV3_WRAP1_S2_CLK] = &gcc_qupv3_wrap1_s2_clk.clkr,
	[GCC_QUPV3_WRAP1_S2_CLK_SRC] = &gcc_qupv3_wrap1_s2_clk_src.clkr,
	[GCC_QUPV3_WRAP1_S3_CLK] = &gcc_qupv3_wrap1_s3_clk.clkr,
	[GCC_QUPV3_WRAP1_S3_CLK_SRC] = &gcc_qupv3_wrap1_s3_clk_src.clkr,
	[GCC_QUPV3_WRAP1_S4_CLK] = &gcc_qupv3_wrap1_s4_clk.clkr,
	[GCC_QUPV3_WRAP1_S4_CLK_SRC] = &gcc_qupv3_wrap1_s4_clk_src.clkr,
	[GCC_QUPV3_WRAP1_S5_CLK] = &gcc_qupv3_wrap1_s5_clk.clkr,
	[GCC_QUPV3_WRAP1_S5_CLK_SRC] = &gcc_qupv3_wrap1_s5_clk_src.clkr,
	[GCC_QUPV3_WRAP1_S6_CLK] = &gcc_qupv3_wrap1_s6_clk.clkr,
	[GCC_QUPV3_WRAP1_S6_CLK_SRC] = &gcc_qupv3_wrap1_s6_clk_src.clkr,
	[GCC_QUPV3_WRAP1_S7_CLK] = &gcc_qupv3_wrap1_s7_clk.clkr,
	[GCC_QUPV3_WRAP1_S7_CLK_SRC] = &gcc_qupv3_wrap1_s7_clk_src.clkr,
	[GCC_QUPV3_WRAP3_CORE_2X_CLK] = &gcc_qupv3_wrap3_core_2x_clk.clkr,
	[GCC_QUPV3_WRAP3_CORE_CLK] = &gcc_qupv3_wrap3_core_clk.clkr,
	[GCC_QUPV3_WRAP3_QSPI_CLK] = &gcc_qupv3_wrap3_qspi_clk.clkr,
	[GCC_QUPV3_WRAP3_S0_CLK] = &gcc_qupv3_wrap3_s0_clk.clkr,
	[GCC_QUPV3_WRAP3_S0_CLK_SRC] = &gcc_qupv3_wrap3_s0_clk_src.clkr,
	[GCC_QUPV3_WRAP3_S0_DIV_CLK_SRC] = &gcc_qupv3_wrap3_s0_div_clk_src.clkr,
	[GCC_QUPV3_WRAP_0_M_AHB_CLK] = &gcc_qupv3_wrap_0_m_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_0_S_AHB_CLK] = &gcc_qupv3_wrap_0_s_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_1_M_AHB_CLK] = &gcc_qupv3_wrap_1_m_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_1_S_AHB_CLK] = &gcc_qupv3_wrap_1_s_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_3_M_AHB_CLK] = &gcc_qupv3_wrap_3_m_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_3_S_AHB_CLK] = &gcc_qupv3_wrap_3_s_ahb_clk.clkr,
	[GCC_SDCC1_AHB_CLK] = &gcc_sdcc1_ahb_clk.clkr,
	[GCC_SDCC1_APPS_CLK] = &gcc_sdcc1_apps_clk.clkr,
	[GCC_SDCC1_APPS_CLK_SRC] = &gcc_sdcc1_apps_clk_src.clkr,
	[GCC_SDCC1_ICE_CORE_CLK] = &gcc_sdcc1_ice_core_clk.clkr,
	[GCC_SDCC1_ICE_CORE_CLK_SRC] = &gcc_sdcc1_ice_core_clk_src.clkr,
	[GCC_SGMI_CLKREF_EN] = &gcc_sgmi_clkref_en.clkr,
	[GCC_UFS_PHY_AHB_CLK] = &gcc_ufs_phy_ahb_clk.clkr,
	[GCC_UFS_PHY_AXI_CLK] = &gcc_ufs_phy_axi_clk.clkr,
	[GCC_UFS_PHY_AXI_CLK_SRC] = &gcc_ufs_phy_axi_clk_src.clkr,
	[GCC_UFS_PHY_ICE_CORE_CLK] = &gcc_ufs_phy_ice_core_clk.clkr,
	[GCC_UFS_PHY_ICE_CORE_CLK_SRC] = &gcc_ufs_phy_ice_core_clk_src.clkr,
	[GCC_UFS_PHY_PHY_AUX_CLK] = &gcc_ufs_phy_phy_aux_clk.clkr,
	[GCC_UFS_PHY_PHY_AUX_CLK_SRC] = &gcc_ufs_phy_phy_aux_clk_src.clkr,
	[GCC_UFS_PHY_RX_SYMBOL_0_CLK] = &gcc_ufs_phy_rx_symbol_0_clk.clkr,
	[GCC_UFS_PHY_RX_SYMBOL_0_CLK_SRC] = &gcc_ufs_phy_rx_symbol_0_clk_src.clkr,
	[GCC_UFS_PHY_RX_SYMBOL_1_CLK] = &gcc_ufs_phy_rx_symbol_1_clk.clkr,
	[GCC_UFS_PHY_RX_SYMBOL_1_CLK_SRC] = &gcc_ufs_phy_rx_symbol_1_clk_src.clkr,
	[GCC_UFS_PHY_TX_SYMBOL_0_CLK] = &gcc_ufs_phy_tx_symbol_0_clk.clkr,
	[GCC_UFS_PHY_TX_SYMBOL_0_CLK_SRC] = &gcc_ufs_phy_tx_symbol_0_clk_src.clkr,
	[GCC_UFS_PHY_UNIPRO_CORE_CLK] = &gcc_ufs_phy_unipro_core_clk.clkr,
	[GCC_UFS_PHY_UNIPRO_CORE_CLK_SRC] = &gcc_ufs_phy_unipro_core_clk_src.clkr,
	[GCC_USB20_MASTER_CLK] = &gcc_usb20_master_clk.clkr,
	[GCC_USB20_MASTER_CLK_SRC] = &gcc_usb20_master_clk_src.clkr,
	[GCC_USB20_MOCK_UTMI_CLK] = &gcc_usb20_mock_utmi_clk.clkr,
	[GCC_USB20_MOCK_UTMI_CLK_SRC] = &gcc_usb20_mock_utmi_clk_src.clkr,
	[GCC_USB20_MOCK_UTMI_POSTDIV_CLK_SRC] = &gcc_usb20_mock_utmi_postdiv_clk_src.clkr,
	[GCC_USB20_SLEEP_CLK] = &gcc_usb20_sleep_clk.clkr,
	[GCC_USB30_PRIM_MASTER_CLK] = &gcc_usb30_prim_master_clk.clkr,
	[GCC_USB30_PRIM_MASTER_CLK_SRC] = &gcc_usb30_prim_master_clk_src.clkr,
	[GCC_USB30_PRIM_MOCK_UTMI_CLK] = &gcc_usb30_prim_mock_utmi_clk.clkr,
	[GCC_USB30_PRIM_MOCK_UTMI_CLK_SRC] = &gcc_usb30_prim_mock_utmi_clk_src.clkr,
	[GCC_USB30_PRIM_MOCK_UTMI_POSTDIV_CLK_SRC] = &gcc_usb30_prim_mock_utmi_postdiv_clk_src.clkr,
	[GCC_USB30_PRIM_SLEEP_CLK] = &gcc_usb30_prim_sleep_clk.clkr,
	[GCC_USB3_PRIM_PHY_AUX_CLK] = &gcc_usb3_prim_phy_aux_clk.clkr,
	[GCC_USB3_PRIM_PHY_AUX_CLK_SRC] = &gcc_usb3_prim_phy_aux_clk_src.clkr,
	[GCC_USB3_PRIM_PHY_COM_AUX_CLK] = &gcc_usb3_prim_phy_com_aux_clk.clkr,
	[GCC_USB3_PRIM_PHY_PIPE_CLK] = &gcc_usb3_prim_phy_pipe_clk.clkr,
	[GCC_USB3_PRIM_PHY_PIPE_CLK_SRC] = &gcc_usb3_prim_phy_pipe_clk_src.clkr,
	[GCC_USB_CLKREF_EN] = &gcc_usb_clkref_en.clkr,
	[GCC_VIDEO_AXI0_CLK] = &gcc_video_axi0_clk.clkr,
	[GCC_VIDEO_AXI1_CLK] = &gcc_video_axi1_clk.clkr,
};

static struct gdsc *gcc_qcs8300_gdscs[] = {
	[GCC_EMAC0_GDSC] = &gcc_emac0_gdsc,
	[GCC_PCIE_0_GDSC] = &gcc_pcie_0_gdsc,
	[GCC_PCIE_1_GDSC] = &gcc_pcie_1_gdsc,
	[GCC_UFS_PHY_GDSC] = &gcc_ufs_phy_gdsc,
	[GCC_USB20_PRIM_GDSC] = &gcc_usb20_prim_gdsc,
	[GCC_USB30_PRIM_GDSC] = &gcc_usb30_prim_gdsc,
};

static const struct qcom_reset_map gcc_qcs8300_resets[] = {
	[GCC_EMAC0_BCR] = { 0xb6000 },
	[GCC_PCIE_0_BCR] = { 0xa9000 },
	[GCC_PCIE_0_LINK_DOWN_BCR] = { 0xbf000 },
	[GCC_PCIE_0_NOCSR_COM_PHY_BCR] = { 0xbf008 },
	[GCC_PCIE_0_PHY_BCR] = { 0xa9144 },
	[GCC_PCIE_0_PHY_NOCSR_COM_PHY_BCR] = { 0xbf00c },
	[GCC_PCIE_1_BCR] = { 0x77000 },
	[GCC_PCIE_1_LINK_DOWN_BCR] = { 0xae084 },
	[GCC_PCIE_1_NOCSR_COM_PHY_BCR] = { 0xae090 },
	[GCC_PCIE_1_PHY_BCR] = { 0xae08c },
	[GCC_PCIE_1_PHY_NOCSR_COM_PHY_BCR] = { 0xae094 },
	[GCC_SDCC1_BCR] = { 0x20000 },
	[GCC_UFS_PHY_BCR] = { 0x83000 },
	[GCC_USB20_PRIM_BCR] = { 0x1c000 },
	[GCC_USB2_PHY_PRIM_BCR] = { 0x5c01c },
	[GCC_USB2_PHY_SEC_BCR] = { 0x5c020 },
	[GCC_USB30_PRIM_BCR] = { 0x1b000 },
	[GCC_USB3_DP_PHY_PRIM_BCR] = { 0x5c008 },
	[GCC_USB3_PHY_PRIM_BCR] = { 0x5c000 },
	[GCC_USB3_PHY_TERT_BCR] = { 0x5c024 },
	[GCC_USB3_UNIPHY_MP0_BCR] = { 0x5c00c },
	[GCC_USB3_UNIPHY_MP1_BCR] = { 0x5c010 },
	[GCC_USB3PHY_PHY_PRIM_BCR] = { 0x5c004 },
	[GCC_USB3UNIPHY_PHY_MP0_BCR] = { 0x5c014 },
	[GCC_USB3UNIPHY_PHY_MP1_BCR] = { 0x5c018 },
	[GCC_USB_PHY_CFG_AHB2PHY_BCR] = { 0x76000 },
	[GCC_VIDEO_AXI0_CLK_ARES] = { 0x34014, 2 },
	[GCC_VIDEO_AXI1_CLK_ARES] = { 0x3401c, 2 },
	[GCC_VIDEO_BCR] = { 0x34000 },
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
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s0_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s1_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s2_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s3_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s4_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s5_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s6_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s7_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap3_s0_clk_src),
};

static const struct regmap_config gcc_qcs8300_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x472cffc,
	.fast_io = true,
};

static const struct qcom_cc_desc gcc_qcs8300_desc = {
	.config = &gcc_qcs8300_regmap_config,
	.clks = gcc_qcs8300_clocks,
	.num_clks = ARRAY_SIZE(gcc_qcs8300_clocks),
	.resets = gcc_qcs8300_resets,
	.num_resets = ARRAY_SIZE(gcc_qcs8300_resets),
	.gdscs = gcc_qcs8300_gdscs,
	.num_gdscs = ARRAY_SIZE(gcc_qcs8300_gdscs),
};

static const struct of_device_id gcc_qcs8300_match_table[] = {
	{ .compatible = "qcom,qcs8300-gcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_qcs8300_match_table);

static int gcc_qcs8300_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret;

	regmap = qcom_cc_map(pdev, &gcc_qcs8300_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ret = qcom_cc_register_rcg_dfs(regmap, gcc_dfs_clocks,
				       ARRAY_SIZE(gcc_dfs_clocks));
	if (ret)
		return ret;

	/* Keep some clocks always enabled */
	qcom_branch_set_clk_en(regmap, 0x32004); /* GCC_CAMERA_AHB_CLK */
	qcom_branch_set_clk_en(regmap, 0x32020); /* GCC_CAMERA_XO_CLK */
	qcom_branch_set_clk_en(regmap, 0x33004); /* GCC_DISP_AHB_CLK */
	qcom_branch_set_clk_en(regmap, 0x33018); /* GCC_DISP_XO_CLK */
	qcom_branch_set_clk_en(regmap, 0x7d004); /* GCC_GPU_CFG_AHB_CLK */
	qcom_branch_set_clk_en(regmap, 0x34004); /* GCC_VIDEO_AHB_CLK */
	qcom_branch_set_clk_en(regmap, 0x34024); /* GCC_VIDEO_XO_CLK */

	/* FORCE_MEM_CORE_ON for ufs phy ice core clocks */
	qcom_branch_set_force_mem_core(regmap, gcc_ufs_phy_ice_core_clk, true);

	return qcom_cc_really_probe(&pdev->dev, &gcc_qcs8300_desc, regmap);
}

static struct platform_driver gcc_qcs8300_driver = {
	.probe = gcc_qcs8300_probe,
	.driver = {
		.name = "gcc-qcs8300",
		.of_match_table = gcc_qcs8300_match_table,
	},
};

static int __init gcc_qcs8300_init(void)
{
	return platform_driver_register(&gcc_qcs8300_driver);
}
subsys_initcall(gcc_qcs8300_init);

static void __exit gcc_qcs8300_exit(void)
{
	platform_driver_unregister(&gcc_qcs8300_driver);
}
module_exit(gcc_qcs8300_exit);

MODULE_DESCRIPTION("QTI GCC QCS8300 Driver");
MODULE_LICENSE("GPL");
