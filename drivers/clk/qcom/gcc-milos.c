// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2025, Luca Weiss <luca.weiss@fairphone.com>
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,milos-gcc.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "gdsc.h"
#include "reset.h"

/* Need to match the order of clocks in DT binding */
enum {
	DT_BI_TCXO,
	DT_SLEEP_CLK,
	DT_PCIE_0_PIPE,
	DT_PCIE_1_PIPE,
	DT_UFS_PHY_RX_SYMBOL_0,
	DT_UFS_PHY_RX_SYMBOL_1,
	DT_UFS_PHY_TX_SYMBOL_0,
	DT_USB3_PHY_WRAPPER_GCC_USB30_PIPE,
};

enum {
	P_BI_TCXO,
	P_GCC_GPLL0_OUT_EVEN,
	P_GCC_GPLL0_OUT_MAIN,
	P_GCC_GPLL0_OUT_ODD,
	P_GCC_GPLL2_OUT_MAIN,
	P_GCC_GPLL4_OUT_MAIN,
	P_GCC_GPLL6_OUT_MAIN,
	P_GCC_GPLL7_OUT_MAIN,
	P_GCC_GPLL9_OUT_MAIN,
	P_PCIE_0_PIPE_CLK,
	P_PCIE_1_PIPE_CLK,
	P_SLEEP_CLK,
	P_UFS_PHY_RX_SYMBOL_0_CLK,
	P_UFS_PHY_RX_SYMBOL_1_CLK,
	P_UFS_PHY_TX_SYMBOL_0_CLK,
	P_USB3_PHY_WRAPPER_GCC_USB30_PIPE_CLK,
};

static struct clk_alpha_pll gcc_gpll0 = {
	.offset = 0x0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x52020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll0",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ole_ops,
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
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gpll0_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_gpll0.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_lucid_ole_ops,
	},
};

static struct clk_alpha_pll gcc_gpll2 = {
	.offset = 0x2000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x52020,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll2",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ole_ops,
		},
	},
};

static struct clk_alpha_pll gcc_gpll4 = {
	.offset = 0x4000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x52020,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll4",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ole_ops,
		},
	},
};

static struct clk_alpha_pll gcc_gpll6 = {
	.offset = 0x6000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x52020,
		.enable_mask = BIT(6),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll6",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ole_ops,
		},
	},
};

static struct clk_alpha_pll gcc_gpll7 = {
	.offset = 0x7000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x52020,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll7",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ole_ops,
		},
	},
};

static struct clk_alpha_pll gcc_gpll9 = {
	.offset = 0x9000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x52020,
		.enable_mask = BIT(9),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll9",
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
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL4_OUT_MAIN, 5 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_2[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll4.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
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
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL6_OUT_MAIN, 2 },
	{ P_GCC_GPLL7_OUT_MAIN, 3 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_4[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll6.clkr.hw },
	{ .hw = &gcc_gpll7.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_5[] = {
	{ P_BI_TCXO, 0 },
};

static const struct clk_parent_data gcc_parent_data_5[] = {
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_6[] = {
	{ P_PCIE_0_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_6[] = {
	{ .index = DT_PCIE_0_PIPE },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_7[] = {
	{ P_PCIE_1_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_7[] = {
	{ .index = DT_PCIE_1_PIPE },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_8[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL7_OUT_MAIN, 2 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_8[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll7.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_9[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL6_OUT_MAIN, 2 },
	{ P_GCC_GPLL0_OUT_ODD, 3 },
	{ P_GCC_GPLL2_OUT_MAIN, 4 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_9[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll6.clkr.hw },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll2.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_10[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL6_OUT_MAIN, 2 },
	{ P_GCC_GPLL0_OUT_ODD, 3 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_10[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll6.clkr.hw },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_11[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL9_OUT_MAIN, 2 },
	{ P_GCC_GPLL4_OUT_MAIN, 5 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_11[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll9.clkr.hw },
	{ .hw = &gcc_gpll4.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_12[] = {
	{ P_UFS_PHY_RX_SYMBOL_0_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_12[] = {
	{ .index = DT_UFS_PHY_RX_SYMBOL_0 },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_13[] = {
	{ P_UFS_PHY_RX_SYMBOL_1_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_13[] = {
	{ .index = DT_UFS_PHY_RX_SYMBOL_1 },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_14[] = {
	{ P_UFS_PHY_TX_SYMBOL_0_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_14[] = {
	{ .index = DT_UFS_PHY_TX_SYMBOL_0 },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_15[] = {
	{ P_USB3_PHY_WRAPPER_GCC_USB30_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_15[] = {
	{ .index = DT_USB3_PHY_WRAPPER_GCC_USB30_PIPE },
	{ .index = DT_BI_TCXO },
};

static struct clk_regmap_mux gcc_pcie_0_pipe_clk_src = {
	.reg = 0x6b070,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_6,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_pipe_clk_src",
			.parent_data = gcc_parent_data_6,
			.num_parents = ARRAY_SIZE(gcc_parent_data_6),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_pcie_1_pipe_clk_src = {
	.reg = 0x9006c,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_7,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_pipe_clk_src",
			.parent_data = gcc_parent_data_7,
			.num_parents = ARRAY_SIZE(gcc_parent_data_7),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_ufs_phy_rx_symbol_0_clk_src = {
	.reg = 0x77064,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_12,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_rx_symbol_0_clk_src",
			.parent_data = gcc_parent_data_12,
			.num_parents = ARRAY_SIZE(gcc_parent_data_12),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_ufs_phy_rx_symbol_1_clk_src = {
	.reg = 0x770e0,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_13,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_rx_symbol_1_clk_src",
			.parent_data = gcc_parent_data_13,
			.num_parents = ARRAY_SIZE(gcc_parent_data_13),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_ufs_phy_tx_symbol_0_clk_src = {
	.reg = 0x77054,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_14,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_tx_symbol_0_clk_src",
			.parent_data = gcc_parent_data_14,
			.num_parents = ARRAY_SIZE(gcc_parent_data_14),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb3_prim_phy_pipe_clk_src = {
	.reg = 0x3906c,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_15,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_prim_phy_pipe_clk_src",
			.parent_data = gcc_parent_data_15,
			.num_parents = ARRAY_SIZE(gcc_parent_data_15),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static const struct freq_tbl ftbl_gcc_gp1_clk_src[] = {
	F(50000000, P_GCC_GPLL0_OUT_EVEN, 6, 0, 0),
	F(100000000, P_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	F(200000000, P_GCC_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_gp1_clk_src = {
	.cmd_rcgr = 0x64004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp1_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_gp2_clk_src = {
	.cmd_rcgr = 0x65004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp2_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_gp3_clk_src = {
	.cmd_rcgr = 0x66004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp3_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcie_0_aux_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie_0_aux_clk_src = {
	.cmd_rcgr = 0x6b074,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_0_aux_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcie_0_phy_rchng_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(100000000, P_GCC_GPLL0_OUT_EVEN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie_0_phy_rchng_clk_src = {
	.cmd_rcgr = 0x6b058,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_phy_rchng_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_0_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_1_aux_clk_src = {
	.cmd_rcgr = 0x90070,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_1_aux_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_1_phy_rchng_clk_src = {
	.cmd_rcgr = 0x90054,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_phy_rchng_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_1_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pdm2_clk_src[] = {
	F(60000000, P_GCC_GPLL0_OUT_MAIN, 10, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pdm2_clk_src = {
	.cmd_rcgr = 0x33010,
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

static const struct freq_tbl ftbl_gcc_qupv3_wrap0_qspi_ref_clk_src[] = {
	F(7372800, P_GCC_GPLL0_OUT_EVEN, 1, 384, 15625),
	F(14745600, P_GCC_GPLL0_OUT_EVEN, 1, 768, 15625),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(29491200, P_GCC_GPLL0_OUT_EVEN, 1, 1536, 15625),
	F(32000000, P_GCC_GPLL0_OUT_EVEN, 1, 8, 75),
	F(48000000, P_GCC_GPLL0_OUT_EVEN, 1, 4, 25),
	F(51200000, P_GCC_GPLL0_OUT_EVEN, 1, 64, 375),
	F(64000000, P_GCC_GPLL0_OUT_EVEN, 1, 16, 75),
	F(75000000, P_GCC_GPLL0_OUT_EVEN, 4, 0, 0),
	F(80000000, P_GCC_GPLL0_OUT_EVEN, 1, 4, 15),
	F(96000000, P_GCC_GPLL0_OUT_EVEN, 1, 8, 25),
	F(100000000, P_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	F(102400000, P_GCC_GPLL0_OUT_EVEN, 1, 128, 375),
	F(112000000, P_GCC_GPLL0_OUT_EVEN, 1, 28, 75),
	F(117964800, P_GCC_GPLL0_OUT_EVEN, 1, 6144, 15625),
	F(120000000, P_GCC_GPLL0_OUT_MAIN, 5, 0, 0),
	F(150000000, P_GCC_GPLL0_OUT_EVEN, 2, 0, 0),
	F(200000000, P_GCC_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_init_data gcc_qupv3_wrap0_qspi_ref_clk_src_init = {
	.name = "gcc_qupv3_wrap0_qspi_ref_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_qspi_ref_clk_src = {
	.cmd_rcgr = 0x18768,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_qspi_ref_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_qspi_ref_clk_src_init,
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap0_s0_clk_src[] = {
	F(7372800, P_GCC_GPLL0_OUT_EVEN, 1, 384, 15625),
	F(14745600, P_GCC_GPLL0_OUT_EVEN, 1, 768, 15625),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(29491200, P_GCC_GPLL0_OUT_EVEN, 1, 1536, 15625),
	F(32000000, P_GCC_GPLL0_OUT_EVEN, 1, 8, 75),
	F(48000000, P_GCC_GPLL0_OUT_EVEN, 1, 4, 25),
	F(51200000, P_GCC_GPLL0_OUT_EVEN, 1, 64, 375),
	F(64000000, P_GCC_GPLL0_OUT_EVEN, 1, 16, 75),
	F(75000000, P_GCC_GPLL0_OUT_EVEN, 4, 0, 0),
	F(80000000, P_GCC_GPLL0_OUT_EVEN, 1, 4, 15),
	F(96000000, P_GCC_GPLL0_OUT_EVEN, 1, 8, 25),
	F(100000000, P_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	F(102400000, P_GCC_GPLL0_OUT_EVEN, 1, 128, 375),
	F(112000000, P_GCC_GPLL0_OUT_EVEN, 1, 28, 75),
	F(117964800, P_GCC_GPLL0_OUT_EVEN, 1, 6144, 15625),
	F(120000000, P_GCC_GPLL0_OUT_MAIN, 5, 0, 0),
	{ }
};

static struct clk_init_data gcc_qupv3_wrap0_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s0_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s0_clk_src = {
	.cmd_rcgr = 0x18010,
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
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s1_clk_src = {
	.cmd_rcgr = 0x18148,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s1_clk_src_init,
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap0_s3_clk_src[] = {
	F(7372800, P_GCC_GPLL0_OUT_EVEN, 1, 384, 15625),
	F(14745600, P_GCC_GPLL0_OUT_EVEN, 1, 768, 15625),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(29491200, P_GCC_GPLL0_OUT_EVEN, 1, 1536, 15625),
	F(32000000, P_GCC_GPLL0_OUT_EVEN, 1, 8, 75),
	F(48000000, P_GCC_GPLL0_OUT_EVEN, 1, 4, 25),
	F(51200000, P_GCC_GPLL0_OUT_EVEN, 1, 64, 375),
	F(64000000, P_GCC_GPLL0_OUT_EVEN, 1, 16, 75),
	F(75000000, P_GCC_GPLL0_OUT_EVEN, 4, 0, 0),
	F(80000000, P_GCC_GPLL0_OUT_EVEN, 1, 4, 15),
	F(96000000, P_GCC_GPLL0_OUT_EVEN, 1, 8, 25),
	F(100000000, P_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	{ }
};

static struct clk_init_data gcc_qupv3_wrap0_s3_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s3_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s3_clk_src = {
	.cmd_rcgr = 0x18290,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s3_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s3_clk_src_init,
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap0_s4_clk_src[] = {
	F(7372800, P_GCC_GPLL0_OUT_EVEN, 1, 384, 15625),
	F(14745600, P_GCC_GPLL0_OUT_EVEN, 1, 768, 15625),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(29491200, P_GCC_GPLL0_OUT_EVEN, 1, 1536, 15625),
	F(32000000, P_GCC_GPLL0_OUT_EVEN, 1, 8, 75),
	F(48000000, P_GCC_GPLL0_OUT_EVEN, 1, 4, 25),
	F(51200000, P_GCC_GPLL0_OUT_EVEN, 1, 64, 375),
	F(64000000, P_GCC_GPLL0_OUT_EVEN, 1, 16, 75),
	F(80000000, P_GCC_GPLL0_OUT_EVEN, 1, 4, 15),
	F(96000000, P_GCC_GPLL0_OUT_EVEN, 1, 8, 25),
	F(128000000, P_GCC_GPLL6_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_init_data gcc_qupv3_wrap0_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s4_clk_src",
	.parent_data = gcc_parent_data_4,
	.num_parents = ARRAY_SIZE(gcc_parent_data_4),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s4_clk_src = {
	.cmd_rcgr = 0x183c8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s4_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s4_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s5_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s5_clk_src = {
	.cmd_rcgr = 0x18500,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s3_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s5_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s6_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s6_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s6_clk_src = {
	.cmd_rcgr = 0x18638,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s3_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s6_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_qspi_ref_clk_src_init = {
	.name = "gcc_qupv3_wrap1_qspi_ref_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_qspi_ref_clk_src = {
	.cmd_rcgr = 0x1e768,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_qspi_ref_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_qspi_ref_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s0_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s0_clk_src = {
	.cmd_rcgr = 0x1e010,
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
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s1_clk_src = {
	.cmd_rcgr = 0x1e148,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s1_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s3_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s3_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s3_clk_src = {
	.cmd_rcgr = 0x1e290,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s3_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s3_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s4_clk_src",
	.parent_data = gcc_parent_data_4,
	.num_parents = ARRAY_SIZE(gcc_parent_data_4),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s4_clk_src = {
	.cmd_rcgr = 0x1e3c8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s4_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s4_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s5_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s5_clk_src = {
	.cmd_rcgr = 0x1e500,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s3_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s5_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s6_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s6_clk_src",
	.parent_data = gcc_parent_data_8,
	.num_parents = ARRAY_SIZE(gcc_parent_data_8),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s6_clk_src = {
	.cmd_rcgr = 0x1e638,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_8,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s3_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s6_clk_src_init,
};

static const struct freq_tbl ftbl_gcc_sdcc1_apps_clk_src[] = {
	F(144000, P_BI_TCXO, 16, 3, 25),
	F(400000, P_BI_TCXO, 12, 1, 4),
	F(20000000, P_GCC_GPLL0_OUT_EVEN, 5, 1, 3),
	F(25000000, P_GCC_GPLL0_OUT_EVEN, 12, 0, 0),
	F(50000000, P_GCC_GPLL0_OUT_EVEN, 6, 0, 0),
	F(100000000, P_GCC_GPLL0_OUT_EVEN, 3, 0, 0),
	F(192000000, P_GCC_GPLL6_OUT_MAIN, 2, 0, 0),
	F(384000000, P_GCC_GPLL6_OUT_MAIN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc1_apps_clk_src = {
	.cmd_rcgr = 0xa3014,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_9,
	.freq_tbl = ftbl_gcc_sdcc1_apps_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_sdcc1_apps_clk_src",
		.parent_data = gcc_parent_data_9,
		.num_parents = ARRAY_SIZE(gcc_parent_data_9),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_sdcc1_ice_core_clk_src[] = {
	F(100000000, P_GCC_GPLL0_OUT_EVEN, 3, 0, 0),
	F(150000000, P_GCC_GPLL0_OUT_EVEN, 2, 0, 0),
	F(300000000, P_GCC_GPLL0_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc1_ice_core_clk_src = {
	.cmd_rcgr = 0xa3038,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_10,
	.freq_tbl = ftbl_gcc_sdcc1_ice_core_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_sdcc1_ice_core_clk_src",
		.parent_data = gcc_parent_data_10,
		.num_parents = ARRAY_SIZE(gcc_parent_data_10),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_sdcc2_apps_clk_src[] = {
	F(400000, P_BI_TCXO, 12, 1, 4),
	F(25000000, P_GCC_GPLL0_OUT_EVEN, 12, 0, 0),
	F(37500000, P_GCC_GPLL0_OUT_EVEN, 8, 0, 0),
	F(50000000, P_GCC_GPLL0_OUT_EVEN, 6, 0, 0),
	F(100000000, P_GCC_GPLL0_OUT_EVEN, 3, 0, 0),
	F(202000000, P_GCC_GPLL9_OUT_MAIN, 4, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc2_apps_clk_src = {
	.cmd_rcgr = 0x14018,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_11,
	.freq_tbl = ftbl_gcc_sdcc2_apps_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_sdcc2_apps_clk_src",
		.parent_data = gcc_parent_data_11,
		.num_parents = ARRAY_SIZE(gcc_parent_data_11),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_ufs_phy_axi_clk_src[] = {
	F(25000000, P_GCC_GPLL0_OUT_EVEN, 12, 0, 0),
	F(75000000, P_GCC_GPLL0_OUT_EVEN, 4, 0, 0),
	F(100000000, P_GCC_GPLL0_OUT_EVEN, 3, 0, 0),
	F(150000000, P_GCC_GPLL0_OUT_MAIN, 4, 0, 0),
	F(300000000, P_GCC_GPLL0_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_phy_axi_clk_src = {
	.cmd_rcgr = 0x77030,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_ufs_phy_axi_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_ufs_phy_axi_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_ufs_phy_ice_core_clk_src[] = {
	F(100000000, P_GCC_GPLL0_OUT_EVEN, 3, 0, 0),
	F(201500000, P_GCC_GPLL4_OUT_MAIN, 4, 0, 0),
	F(403000000, P_GCC_GPLL4_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_phy_ice_core_clk_src = {
	.cmd_rcgr = 0x77080,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_ufs_phy_ice_core_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_ufs_phy_ice_core_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_ufs_phy_phy_aux_clk_src[] = {
	F(9600000, P_BI_TCXO, 2, 0, 0),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_phy_phy_aux_clk_src = {
	.cmd_rcgr = 0x770b4,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_ufs_phy_phy_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_ufs_phy_phy_aux_clk_src",
		.parent_data = gcc_parent_data_5,
		.num_parents = ARRAY_SIZE(gcc_parent_data_5),
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
	.cmd_rcgr = 0x77098,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_ufs_phy_unipro_core_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_ufs_phy_unipro_core_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb30_prim_master_clk_src[] = {
	F(66666667, P_GCC_GPLL0_OUT_EVEN, 4.5, 0, 0),
	F(133333333, P_GCC_GPLL0_OUT_MAIN, 4.5, 0, 0),
	F(200000000, P_GCC_GPLL0_OUT_MAIN, 3, 0, 0),
	F(240000000, P_GCC_GPLL0_OUT_MAIN, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb30_prim_master_clk_src = {
	.cmd_rcgr = 0x3902c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb30_prim_master_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_prim_master_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_usb30_prim_mock_utmi_clk_src = {
	.cmd_rcgr = 0x39044,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_prim_mock_utmi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_usb3_prim_phy_aux_clk_src = {
	.cmd_rcgr = 0x39070,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb3_prim_phy_aux_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_regmap_div gcc_pcie_0_pipe_div2_clk_src = {
	.reg = 0x6b094,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_0_pipe_div2_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_pcie_0_pipe_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_pcie_1_pipe_div2_clk_src = {
	.reg = 0x90090,
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

static struct clk_regmap_div gcc_qupv3_wrap0_s2_clk_src = {
	.reg = 0x18280,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_wrap0_s2_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_qupv3_wrap0_qspi_ref_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_qupv3_wrap1_s2_clk_src = {
	.reg = 0x1e280,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_wrap1_s2_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_qupv3_wrap1_qspi_ref_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_usb30_prim_mock_utmi_postdiv_clk_src = {
	.reg = 0x3905c,
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

static struct clk_branch gcc_aggre_noc_pcie_axi_clk = {
	.halt_reg = 0x1005c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x1005c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(12),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_noc_pcie_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_ufs_phy_axi_clk = {
	.halt_reg = 0x770e4,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x770e4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x770e4,
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

static struct clk_branch gcc_aggre_ufs_phy_axi_hw_ctl_clk = {
	.halt_reg = 0x770e4,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x770e4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x770e4,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_ufs_phy_axi_hw_ctl_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_ufs_phy_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_usb3_prim_axi_clk = {
	.halt_reg = 0x39090,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x39090,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x39090,
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

static struct clk_branch gcc_boot_rom_ahb_clk = {
	.halt_reg = 0x38004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x38004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_boot_rom_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_hf_axi_clk = {
	.halt_reg = 0x26010,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x26010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x26010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camera_hf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_sf_axi_clk = {
	.halt_reg = 0x26014,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x26014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x26014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camera_sf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_pcie_anoc_ahb_clk = {
	.halt_reg = 0x10050,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x10050,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(20),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cfg_noc_pcie_anoc_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_usb3_prim_axi_clk = {
	.halt_reg = 0x3908c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x3908c,
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

static struct clk_branch gcc_cnoc_pcie_sf_axi_clk = {
	.halt_reg = 0x10058,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x10058,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(6),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cnoc_pcie_sf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ddrss_gpu_axi_clk = {
	.halt_reg = 0x7115c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x7115c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7115c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ddrss_gpu_axi_clk",
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch gcc_ddrss_pcie_sf_qtb_clk = {
	.halt_reg = 0x1006c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x1006c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(19),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ddrss_pcie_sf_qtb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_gpll0_div_clk_src = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(23),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_disp_gpll0_div_clk_src",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_gpll0_out_even.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_hf_axi_clk = {
	.halt_reg = 0x2700c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x2700c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2700c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_disp_hf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp1_clk = {
	.halt_reg = 0x64000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x64000,
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
	.halt_reg = 0x65000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x65000,
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
	.halt_reg = 0x66000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x66000,
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

static struct clk_branch gcc_gpu_gpll0_clk_src = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x52000,
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
		.enable_reg = 0x52000,
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

static struct clk_branch gcc_gpu_memnoc_gfx_clk = {
	.halt_reg = 0x71010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x71010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x71010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpu_memnoc_gfx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_snoc_dvm_gfx_clk = {
	.halt_reg = 0x71018,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x71018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpu_snoc_dvm_gfx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_aux_clk = {
	.halt_reg = 0x6b03c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(3),
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
	.halt_reg = 0x6b038,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x6b038,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_mstr_axi_clk = {
	.halt_reg = 0x6b02c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x6b02c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_phy_rchng_clk = {
	.halt_reg = 0x6b054,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(22),
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
	.halt_reg = 0x6b048,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(4),
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

static struct clk_branch gcc_pcie_0_pipe_div2_clk = {
	.halt_reg = 0x6b098,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(13),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_pipe_div2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_0_pipe_div2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_slv_axi_clk = {
	.halt_reg = 0x6b020,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x6b020,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_slv_q2a_axi_clk = {
	.halt_reg = 0x6b01c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_aux_clk = {
	.halt_reg = 0x90038,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(29),
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
	.halt_reg = 0x90034,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x90034,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(28),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_mstr_axi_clk = {
	.halt_reg = 0x90028,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x90028,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(27),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_phy_rchng_clk = {
	.halt_reg = 0x90050,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(8),
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
	.halt_reg = 0x90044,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(7),
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
	.halt_reg = 0x90094,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(15),
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
	.halt_reg = 0x9001c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x9001c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(26),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_slv_q2a_axi_clk = {
	.halt_reg = 0x90018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(25),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_rscc_cfg_ahb_clk = {
	.halt_reg = 0x11004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x11004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(20),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_rscc_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_rscc_xo_clk = {
	.halt_reg = 0x11008,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(21),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_rscc_xo_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm2_clk = {
	.halt_reg = 0x3300c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3300c,
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
	.halt_reg = 0x33004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x33004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x33004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pdm_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm_xo4_clk = {
	.halt_reg = 0x33008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x33008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pdm_xo4_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_camera_nrt_ahb_clk = {
	.halt_reg = 0x26008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x26008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x26008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_camera_nrt_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_camera_rt_ahb_clk = {
	.halt_reg = 0x2600c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2600c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2600c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_camera_rt_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_disp_ahb_clk = {
	.halt_reg = 0x27008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x27008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x27008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_disp_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_gpu_ahb_clk = {
	.halt_reg = 0x71008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x71008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x71008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_gpu_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_pcie_ahb_clk = {
	.halt_reg = 0x6b018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x6b018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(11),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_pcie_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_cv_cpu_ahb_clk = {
	.halt_reg = 0x32014,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x32014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x32014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_video_cv_cpu_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_cvp_ahb_clk = {
	.halt_reg = 0x32008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x32008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x32008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_video_cvp_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_v_cpu_ahb_clk = {
	.halt_reg = 0x32010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x32010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x32010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_video_v_cpu_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_vcodec_ahb_clk = {
	.halt_reg = 0x3200c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x3200c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3200c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_video_vcodec_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_core_2x_clk = {
	.halt_reg = 0x23018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(18),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_core_clk = {
	.halt_reg = 0x23008,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(19),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_qspi_ref_clk = {
	.halt_reg = 0x18764,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(29),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_qspi_ref_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap0_qspi_ref_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s0_clk = {
	.halt_reg = 0x18004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(22),
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
	.halt_reg = 0x1813c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(23),
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
	.halt_reg = 0x18274,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(24),
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
	.halt_reg = 0x18284,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(25),
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
	.halt_reg = 0x183bc,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(26),
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
	.halt_reg = 0x184f4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(27),
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
	.halt_reg = 0x1862c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(28),
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

static struct clk_branch gcc_qupv3_wrap1_core_2x_clk = {
	.halt_reg = 0x23168,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(3),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_core_clk = {
	.halt_reg = 0x23158,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_qspi_ref_clk = {
	.halt_reg = 0x1e764,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(30),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_qspi_ref_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap1_qspi_ref_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s0_clk = {
	.halt_reg = 0x1e004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(4),
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
	.halt_reg = 0x1e13c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(5),
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
	.halt_reg = 0x1e274,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(6),
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
	.halt_reg = 0x1e284,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(7),
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
	.halt_reg = 0x1e3bc,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(8),
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
	.halt_reg = 0x1e4f4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(9),
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
	.halt_reg = 0x1e62c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(10),
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

static struct clk_branch gcc_qupv3_wrap_0_m_ahb_clk = {
	.halt_reg = 0x23000,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(20),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_0_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_0_s_ahb_clk = {
	.halt_reg = 0x23004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x23004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(21),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_0_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_1_m_ahb_clk = {
	.halt_reg = 0x23150,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_1_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_1_s_ahb_clk = {
	.halt_reg = 0x23154,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x23154,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_1_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_ahb_clk = {
	.halt_reg = 0xa3004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa3004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sdcc1_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_apps_clk = {
	.halt_reg = 0xa3008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa3008,
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
	.halt_reg = 0xa302c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xa302c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xa302c,
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

static struct clk_branch gcc_sdcc2_ahb_clk = {
	.halt_reg = 0x14010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x14010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sdcc2_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc2_apps_clk = {
	.halt_reg = 0x14004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x14004,
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

static struct clk_branch gcc_ufs_phy_ahb_clk = {
	.halt_reg = 0x77024,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x77024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x77024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_axi_clk = {
	.halt_reg = 0x77018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x77018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x77018,
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

static struct clk_branch gcc_ufs_phy_axi_hw_ctl_clk = {
	.halt_reg = 0x77018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x77018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x77018,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_axi_hw_ctl_clk",
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
	.halt_reg = 0x77074,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x77074,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x77074,
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

static struct clk_branch gcc_ufs_phy_ice_core_hw_ctl_clk = {
	.halt_reg = 0x77074,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x77074,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x77074,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_ice_core_hw_ctl_clk",
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
	.halt_reg = 0x770b0,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x770b0,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x770b0,
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

static struct clk_branch gcc_ufs_phy_phy_aux_hw_ctl_clk = {
	.halt_reg = 0x770b0,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x770b0,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x770b0,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_phy_aux_hw_ctl_clk",
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
	.halt_reg = 0x7702c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x7702c,
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
	.halt_reg = 0x770cc,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x770cc,
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
	.halt_reg = 0x77028,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x77028,
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
	.halt_reg = 0x77068,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x77068,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x77068,
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

static struct clk_branch gcc_ufs_phy_unipro_core_hw_ctl_clk = {
	.halt_reg = 0x77068,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x77068,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x77068,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_unipro_core_hw_ctl_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_ufs_phy_unipro_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_prim_atb_clk = {
	.halt_reg = 0x39088,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x39088,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_prim_atb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb30_prim_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_prim_master_clk = {
	.halt_reg = 0x39018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x39018,
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
	.halt_reg = 0x39028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x39028,
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
	.halt_reg = 0x39024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x39024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_prim_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_phy_aux_clk = {
	.halt_reg = 0x39060,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x39060,
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
	.halt_reg = 0x39064,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x39064,
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
	.halt_reg = 0x39068,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x39068,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x39068,
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

static struct clk_branch gcc_video_axi0_clk = {
	.halt_reg = 0x32018,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x32018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x32018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_video_axi0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc pcie_0_gdsc = {
	.gdscr = 0x6b004,
	.collapse_ctrl = 0x5214c,
	.collapse_mask = BIT(0),
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "pcie_0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE | POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc pcie_0_phy_gdsc = {
	.gdscr = 0x6c000,
	.collapse_ctrl = 0x5214c,
	.collapse_mask = BIT(1),
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "pcie_0_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE | POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc pcie_1_gdsc = {
	.gdscr = 0x90004,
	.collapse_ctrl = 0x5214c,
	.collapse_mask = BIT(3),
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "pcie_1_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE | POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc pcie_1_phy_gdsc = {
	.gdscr = 0xa2000,
	.collapse_ctrl = 0x5214c,
	.collapse_mask = BIT(4),
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "pcie_1_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE | POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc ufs_phy_gdsc = {
	.gdscr = 0x77004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "ufs_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc ufs_mem_phy_gdsc = {
	.gdscr = 0x9e000,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "ufs_mem_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc usb30_prim_gdsc = {
	.gdscr = 0x39004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "usb30_prim_gdsc",
	},
	.pwrsts = PWRSTS_RET_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc usb3_phy_gdsc = {
	.gdscr = 0x5000c,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "usb3_phy_gdsc",
	},
	.pwrsts = PWRSTS_RET_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct clk_regmap *gcc_milos_clocks[] = {
	[GCC_AGGRE_NOC_PCIE_AXI_CLK] = &gcc_aggre_noc_pcie_axi_clk.clkr,
	[GCC_AGGRE_UFS_PHY_AXI_CLK] = &gcc_aggre_ufs_phy_axi_clk.clkr,
	[GCC_AGGRE_UFS_PHY_AXI_HW_CTL_CLK] = &gcc_aggre_ufs_phy_axi_hw_ctl_clk.clkr,
	[GCC_AGGRE_USB3_PRIM_AXI_CLK] = &gcc_aggre_usb3_prim_axi_clk.clkr,
	[GCC_BOOT_ROM_AHB_CLK] = &gcc_boot_rom_ahb_clk.clkr,
	[GCC_CAMERA_HF_AXI_CLK] = &gcc_camera_hf_axi_clk.clkr,
	[GCC_CAMERA_SF_AXI_CLK] = &gcc_camera_sf_axi_clk.clkr,
	[GCC_CFG_NOC_PCIE_ANOC_AHB_CLK] = &gcc_cfg_noc_pcie_anoc_ahb_clk.clkr,
	[GCC_CFG_NOC_USB3_PRIM_AXI_CLK] = &gcc_cfg_noc_usb3_prim_axi_clk.clkr,
	[GCC_CNOC_PCIE_SF_AXI_CLK] = &gcc_cnoc_pcie_sf_axi_clk.clkr,
	[GCC_DDRSS_GPU_AXI_CLK] = &gcc_ddrss_gpu_axi_clk.clkr,
	[GCC_DDRSS_PCIE_SF_QTB_CLK] = &gcc_ddrss_pcie_sf_qtb_clk.clkr,
	[GCC_DISP_GPLL0_DIV_CLK_SRC] = &gcc_disp_gpll0_div_clk_src.clkr,
	[GCC_DISP_HF_AXI_CLK] = &gcc_disp_hf_axi_clk.clkr,
	[GCC_GP1_CLK] = &gcc_gp1_clk.clkr,
	[GCC_GP1_CLK_SRC] = &gcc_gp1_clk_src.clkr,
	[GCC_GP2_CLK] = &gcc_gp2_clk.clkr,
	[GCC_GP2_CLK_SRC] = &gcc_gp2_clk_src.clkr,
	[GCC_GP3_CLK] = &gcc_gp3_clk.clkr,
	[GCC_GP3_CLK_SRC] = &gcc_gp3_clk_src.clkr,
	[GCC_GPLL0] = &gcc_gpll0.clkr,
	[GCC_GPLL0_OUT_EVEN] = &gcc_gpll0_out_even.clkr,
	[GCC_GPLL2] = &gcc_gpll2.clkr,
	[GCC_GPLL4] = &gcc_gpll4.clkr,
	[GCC_GPLL6] = &gcc_gpll6.clkr,
	[GCC_GPLL7] = &gcc_gpll7.clkr,
	[GCC_GPLL9] = &gcc_gpll9.clkr,
	[GCC_GPU_GPLL0_CLK_SRC] = &gcc_gpu_gpll0_clk_src.clkr,
	[GCC_GPU_GPLL0_DIV_CLK_SRC] = &gcc_gpu_gpll0_div_clk_src.clkr,
	[GCC_GPU_MEMNOC_GFX_CLK] = &gcc_gpu_memnoc_gfx_clk.clkr,
	[GCC_GPU_SNOC_DVM_GFX_CLK] = &gcc_gpu_snoc_dvm_gfx_clk.clkr,
	[GCC_PCIE_0_AUX_CLK] = &gcc_pcie_0_aux_clk.clkr,
	[GCC_PCIE_0_AUX_CLK_SRC] = &gcc_pcie_0_aux_clk_src.clkr,
	[GCC_PCIE_0_CFG_AHB_CLK] = &gcc_pcie_0_cfg_ahb_clk.clkr,
	[GCC_PCIE_0_MSTR_AXI_CLK] = &gcc_pcie_0_mstr_axi_clk.clkr,
	[GCC_PCIE_0_PHY_RCHNG_CLK] = &gcc_pcie_0_phy_rchng_clk.clkr,
	[GCC_PCIE_0_PHY_RCHNG_CLK_SRC] = &gcc_pcie_0_phy_rchng_clk_src.clkr,
	[GCC_PCIE_0_PIPE_CLK] = &gcc_pcie_0_pipe_clk.clkr,
	[GCC_PCIE_0_PIPE_CLK_SRC] = &gcc_pcie_0_pipe_clk_src.clkr,
	[GCC_PCIE_0_PIPE_DIV2_CLK] = &gcc_pcie_0_pipe_div2_clk.clkr,
	[GCC_PCIE_0_PIPE_DIV2_CLK_SRC] = &gcc_pcie_0_pipe_div2_clk_src.clkr,
	[GCC_PCIE_0_SLV_AXI_CLK] = &gcc_pcie_0_slv_axi_clk.clkr,
	[GCC_PCIE_0_SLV_Q2A_AXI_CLK] = &gcc_pcie_0_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_1_AUX_CLK] = &gcc_pcie_1_aux_clk.clkr,
	[GCC_PCIE_1_AUX_CLK_SRC] = &gcc_pcie_1_aux_clk_src.clkr,
	[GCC_PCIE_1_CFG_AHB_CLK] = &gcc_pcie_1_cfg_ahb_clk.clkr,
	[GCC_PCIE_1_MSTR_AXI_CLK] = &gcc_pcie_1_mstr_axi_clk.clkr,
	[GCC_PCIE_1_PHY_RCHNG_CLK] = &gcc_pcie_1_phy_rchng_clk.clkr,
	[GCC_PCIE_1_PHY_RCHNG_CLK_SRC] = &gcc_pcie_1_phy_rchng_clk_src.clkr,
	[GCC_PCIE_1_PIPE_CLK] = &gcc_pcie_1_pipe_clk.clkr,
	[GCC_PCIE_1_PIPE_CLK_SRC] = &gcc_pcie_1_pipe_clk_src.clkr,
	[GCC_PCIE_1_PIPE_DIV2_CLK] = &gcc_pcie_1_pipe_div2_clk.clkr,
	[GCC_PCIE_1_PIPE_DIV2_CLK_SRC] = &gcc_pcie_1_pipe_div2_clk_src.clkr,
	[GCC_PCIE_1_SLV_AXI_CLK] = &gcc_pcie_1_slv_axi_clk.clkr,
	[GCC_PCIE_1_SLV_Q2A_AXI_CLK] = &gcc_pcie_1_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_RSCC_CFG_AHB_CLK] = &gcc_pcie_rscc_cfg_ahb_clk.clkr,
	[GCC_PCIE_RSCC_XO_CLK] = &gcc_pcie_rscc_xo_clk.clkr,
	[GCC_PDM2_CLK] = &gcc_pdm2_clk.clkr,
	[GCC_PDM2_CLK_SRC] = &gcc_pdm2_clk_src.clkr,
	[GCC_PDM_AHB_CLK] = &gcc_pdm_ahb_clk.clkr,
	[GCC_PDM_XO4_CLK] = &gcc_pdm_xo4_clk.clkr,
	[GCC_QMIP_CAMERA_NRT_AHB_CLK] = &gcc_qmip_camera_nrt_ahb_clk.clkr,
	[GCC_QMIP_CAMERA_RT_AHB_CLK] = &gcc_qmip_camera_rt_ahb_clk.clkr,
	[GCC_QMIP_DISP_AHB_CLK] = &gcc_qmip_disp_ahb_clk.clkr,
	[GCC_QMIP_GPU_AHB_CLK] = &gcc_qmip_gpu_ahb_clk.clkr,
	[GCC_QMIP_PCIE_AHB_CLK] = &gcc_qmip_pcie_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_CV_CPU_AHB_CLK] = &gcc_qmip_video_cv_cpu_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_CVP_AHB_CLK] = &gcc_qmip_video_cvp_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_V_CPU_AHB_CLK] = &gcc_qmip_video_v_cpu_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_VCODEC_AHB_CLK] = &gcc_qmip_video_vcodec_ahb_clk.clkr,
	[GCC_QUPV3_WRAP0_CORE_2X_CLK] = &gcc_qupv3_wrap0_core_2x_clk.clkr,
	[GCC_QUPV3_WRAP0_CORE_CLK] = &gcc_qupv3_wrap0_core_clk.clkr,
	[GCC_QUPV3_WRAP0_QSPI_REF_CLK] = &gcc_qupv3_wrap0_qspi_ref_clk.clkr,
	[GCC_QUPV3_WRAP0_QSPI_REF_CLK_SRC] = &gcc_qupv3_wrap0_qspi_ref_clk_src.clkr,
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
	[GCC_QUPV3_WRAP1_CORE_2X_CLK] = &gcc_qupv3_wrap1_core_2x_clk.clkr,
	[GCC_QUPV3_WRAP1_CORE_CLK] = &gcc_qupv3_wrap1_core_clk.clkr,
	[GCC_QUPV3_WRAP1_QSPI_REF_CLK] = &gcc_qupv3_wrap1_qspi_ref_clk.clkr,
	[GCC_QUPV3_WRAP1_QSPI_REF_CLK_SRC] = &gcc_qupv3_wrap1_qspi_ref_clk_src.clkr,
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
	[GCC_QUPV3_WRAP_0_M_AHB_CLK] = &gcc_qupv3_wrap_0_m_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_0_S_AHB_CLK] = &gcc_qupv3_wrap_0_s_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_1_M_AHB_CLK] = &gcc_qupv3_wrap_1_m_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_1_S_AHB_CLK] = &gcc_qupv3_wrap_1_s_ahb_clk.clkr,
	[GCC_SDCC1_AHB_CLK] = &gcc_sdcc1_ahb_clk.clkr,
	[GCC_SDCC1_APPS_CLK] = &gcc_sdcc1_apps_clk.clkr,
	[GCC_SDCC1_APPS_CLK_SRC] = &gcc_sdcc1_apps_clk_src.clkr,
	[GCC_SDCC1_ICE_CORE_CLK] = &gcc_sdcc1_ice_core_clk.clkr,
	[GCC_SDCC1_ICE_CORE_CLK_SRC] = &gcc_sdcc1_ice_core_clk_src.clkr,
	[GCC_SDCC2_AHB_CLK] = &gcc_sdcc2_ahb_clk.clkr,
	[GCC_SDCC2_APPS_CLK] = &gcc_sdcc2_apps_clk.clkr,
	[GCC_SDCC2_APPS_CLK_SRC] = &gcc_sdcc2_apps_clk_src.clkr,
	[GCC_UFS_PHY_AHB_CLK] = &gcc_ufs_phy_ahb_clk.clkr,
	[GCC_UFS_PHY_AXI_CLK] = &gcc_ufs_phy_axi_clk.clkr,
	[GCC_UFS_PHY_AXI_CLK_SRC] = &gcc_ufs_phy_axi_clk_src.clkr,
	[GCC_UFS_PHY_AXI_HW_CTL_CLK] = &gcc_ufs_phy_axi_hw_ctl_clk.clkr,
	[GCC_UFS_PHY_ICE_CORE_CLK] = &gcc_ufs_phy_ice_core_clk.clkr,
	[GCC_UFS_PHY_ICE_CORE_CLK_SRC] = &gcc_ufs_phy_ice_core_clk_src.clkr,
	[GCC_UFS_PHY_ICE_CORE_HW_CTL_CLK] = &gcc_ufs_phy_ice_core_hw_ctl_clk.clkr,
	[GCC_UFS_PHY_PHY_AUX_CLK] = &gcc_ufs_phy_phy_aux_clk.clkr,
	[GCC_UFS_PHY_PHY_AUX_CLK_SRC] = &gcc_ufs_phy_phy_aux_clk_src.clkr,
	[GCC_UFS_PHY_PHY_AUX_HW_CTL_CLK] = &gcc_ufs_phy_phy_aux_hw_ctl_clk.clkr,
	[GCC_UFS_PHY_RX_SYMBOL_0_CLK] = &gcc_ufs_phy_rx_symbol_0_clk.clkr,
	[GCC_UFS_PHY_RX_SYMBOL_0_CLK_SRC] = &gcc_ufs_phy_rx_symbol_0_clk_src.clkr,
	[GCC_UFS_PHY_RX_SYMBOL_1_CLK] = &gcc_ufs_phy_rx_symbol_1_clk.clkr,
	[GCC_UFS_PHY_RX_SYMBOL_1_CLK_SRC] = &gcc_ufs_phy_rx_symbol_1_clk_src.clkr,
	[GCC_UFS_PHY_TX_SYMBOL_0_CLK] = &gcc_ufs_phy_tx_symbol_0_clk.clkr,
	[GCC_UFS_PHY_TX_SYMBOL_0_CLK_SRC] = &gcc_ufs_phy_tx_symbol_0_clk_src.clkr,
	[GCC_UFS_PHY_UNIPRO_CORE_CLK] = &gcc_ufs_phy_unipro_core_clk.clkr,
	[GCC_UFS_PHY_UNIPRO_CORE_CLK_SRC] = &gcc_ufs_phy_unipro_core_clk_src.clkr,
	[GCC_UFS_PHY_UNIPRO_CORE_HW_CTL_CLK] = &gcc_ufs_phy_unipro_core_hw_ctl_clk.clkr,
	[GCC_USB30_PRIM_ATB_CLK] = &gcc_usb30_prim_atb_clk.clkr,
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
	[GCC_VIDEO_AXI0_CLK] = &gcc_video_axi0_clk.clkr,
};

static const struct qcom_reset_map gcc_milos_resets[] = {
	[GCC_CAMERA_BCR] = { 0x26000 },
	[GCC_DISPLAY_BCR] = { 0x27000 },
	[GCC_GPU_BCR] = { 0x71000 },
	[GCC_PCIE_0_BCR] = { 0x6b000 },
	[GCC_PCIE_0_LINK_DOWN_BCR] = { 0x6c014 },
	[GCC_PCIE_0_NOCSR_COM_PHY_BCR] = { 0x6c020 },
	[GCC_PCIE_0_PHY_BCR] = { 0x6c01c },
	[GCC_PCIE_0_PHY_NOCSR_COM_PHY_BCR] = { 0x6c028 },
	[GCC_PCIE_1_BCR] = { 0x90000 },
	[GCC_PCIE_1_LINK_DOWN_BCR] = { 0x8e014 },
	[GCC_PCIE_1_NOCSR_COM_PHY_BCR] = { 0x8e020 },
	[GCC_PCIE_1_PHY_BCR] = { 0x8e01c },
	[GCC_PCIE_1_PHY_NOCSR_COM_PHY_BCR] = { 0x8e024 },
	[GCC_PCIE_RSCC_BCR] = { 0x11000 },
	[GCC_PDM_BCR] = { 0x33000 },
	[GCC_QUPV3_WRAPPER_0_BCR] = { 0x18000 },
	[GCC_QUPV3_WRAPPER_1_BCR] = { 0x1e000 },
	[GCC_QUSB2PHY_PRIM_BCR] = { 0x12000 },
	[GCC_QUSB2PHY_SEC_BCR] = { 0x12004 },
	[GCC_SDCC1_BCR] = { 0xa3000 },
	[GCC_SDCC2_BCR] = { 0x14000 },
	[GCC_UFS_PHY_BCR] = { 0x77000 },
	[GCC_USB30_PRIM_BCR] = { 0x39000 },
	[GCC_USB3_DP_PHY_PRIM_BCR] = { 0x50008 },
	[GCC_USB3_PHY_PRIM_BCR] = { 0x50000 },
	[GCC_USB3PHY_PHY_PRIM_BCR] = { 0x50004 },
	[GCC_VIDEO_AXI0_CLK_ARES] = { 0x32018, 2 },
	[GCC_VIDEO_BCR] = { 0x32000 },
};

static const struct clk_rcg_dfs_data gcc_milos_dfs_clocks[] = {
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_qspi_ref_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s0_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s1_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s3_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s4_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s5_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s6_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_qspi_ref_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s0_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s1_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s3_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s4_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s5_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s6_clk_src),
};

static struct gdsc *gcc_milos_gdscs[] = {
	[PCIE_0_GDSC] = &pcie_0_gdsc,
	[PCIE_0_PHY_GDSC] = &pcie_0_phy_gdsc,
	[PCIE_1_GDSC] = &pcie_1_gdsc,
	[PCIE_1_PHY_GDSC] = &pcie_1_phy_gdsc,
	[UFS_PHY_GDSC] = &ufs_phy_gdsc,
	[UFS_MEM_PHY_GDSC] = &ufs_mem_phy_gdsc,
	[USB30_PRIM_GDSC] = &usb30_prim_gdsc,
	[USB3_PHY_GDSC] = &usb3_phy_gdsc,
};

static u32 gcc_milos_critical_cbcrs[] = {
	0x26004, /* GCC_CAMERA_AHB_CLK */
	0x26018, /* GCC_CAMERA_HF_XO_CLK */
	0x2601c, /* GCC_CAMERA_SF_XO_CLK */
	0x27004, /* GCC_DISP_AHB_CLK */
	0x27018, /* GCC_DISP_XO_CLK */
	0x71004, /* GCC_GPU_CFG_AHB_CLK */
	0x32004, /* GCC_VIDEO_AHB_CLK */
	0x32024, /* GCC_VIDEO_XO_CLK */
};

static const struct regmap_config gcc_milos_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x1f41f0,
	.fast_io = true,
};

static struct qcom_cc_driver_data gcc_milos_driver_data = {
	.clk_cbcrs = gcc_milos_critical_cbcrs,
	.num_clk_cbcrs = ARRAY_SIZE(gcc_milos_critical_cbcrs),
	.dfs_rcgs = gcc_milos_dfs_clocks,
	.num_dfs_rcgs = ARRAY_SIZE(gcc_milos_dfs_clocks),
};

static const struct qcom_cc_desc gcc_milos_desc = {
	.config = &gcc_milos_regmap_config,
	.clks = gcc_milos_clocks,
	.num_clks = ARRAY_SIZE(gcc_milos_clocks),
	.resets = gcc_milos_resets,
	.num_resets = ARRAY_SIZE(gcc_milos_resets),
	.gdscs = gcc_milos_gdscs,
	.num_gdscs = ARRAY_SIZE(gcc_milos_gdscs),
	.use_rpm = true,
	.driver_data = &gcc_milos_driver_data,
};

static const struct of_device_id gcc_milos_match_table[] = {
	{ .compatible = "qcom,milos-gcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_milos_match_table);

static int gcc_milos_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &gcc_milos_desc);
}

static struct platform_driver gcc_milos_driver = {
	.probe = gcc_milos_probe,
	.driver = {
		.name = "gcc-milos",
		.of_match_table = gcc_milos_match_table,
	},
};

static int __init gcc_milos_init(void)
{
	return platform_driver_register(&gcc_milos_driver);
}
subsys_initcall(gcc_milos_init);

static void __exit gcc_milos_exit(void)
{
	platform_driver_unregister(&gcc_milos_driver);
}
module_exit(gcc_milos_exit);

MODULE_DESCRIPTION("QTI GCC Milos Driver");
MODULE_LICENSE("GPL");
