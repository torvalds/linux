// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,gcc-anorak.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "common.h"
#include "reset.h"
#include "vdd-level.h"

static DEFINE_VDD_REGULATORS(vdd_cx, VDD_HIGH_L1 + 1, 1, vdd_corner);
static DEFINE_VDD_REGULATORS(vdd_mxa, VDD_HIGH + 1, 1, vdd_corner);

static struct clk_vdd_class *gcc_anorak_regulators[] = {
	&vdd_cx,
	&vdd_mxa,
};

enum {
	P_BI_TCXO,
	P_GCC_GPLL0_OUT_EVEN,
	P_GCC_GPLL0_OUT_MAIN,
	P_GCC_GPLL4_OUT_MAIN,
	P_GCC_GPLL9_OUT_MAIN,
	P_PCIE_0_PIPE_CLK,
	P_PCIE_1_PIPE_CLK,
	P_PCIE_2_PHY_AUX_CLK,
	P_PCIE_2_PIPE_CLK,
	P_SLEEP_CLK,
	P_UFS_PHY_RX_SYMBOL_0_CLK,
	P_UFS_PHY_RX_SYMBOL_1_CLK,
	P_UFS_PHY_TX_SYMBOL_0_CLK,
	P_USB3_PHY_WRAPPER_GCC_USB30_PIPE_CLK,
};

static struct clk_alpha_pll gcc_gpll0 = {
	.offset = 0x0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_gpll0",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_evo_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER_D1] = 500000000,
				[VDD_LOWER] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1500000000,
				[VDD_NOMINAL] = 1800000000,
				[VDD_HIGH] = 2000000000},
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
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_gpll0_out_even",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_gpll0.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_lucid_evo_ops,
	},
};

static struct clk_alpha_pll gcc_gpll4 = {
	.offset = 0x4000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_gpll4",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_evo_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER_D1] = 500000000,
				[VDD_LOWER] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1500000000,
				[VDD_NOMINAL] = 1800000000,
				[VDD_HIGH] = 2000000000},
		},
	},
};

static struct clk_alpha_pll gcc_gpll9 = {
	.offset = 0x9000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(9),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_gpll9",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_evo_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER_D1] = 500000000,
				[VDD_LOWER] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1500000000,
				[VDD_NOMINAL] = 1800000000,
				[VDD_HIGH] = 2000000000},
		},
	},
};

static const struct parent_map gcc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_0[] = {
	{ .fw_name = "bi_tcxo" },
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
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .fw_name = "sleep_clk" },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_SLEEP_CLK, 5 },
};

static const struct clk_parent_data gcc_parent_data_2[] = {
	{ .fw_name = "bi_tcxo" },
	{ .fw_name = "sleep_clk" },
};

static const struct parent_map gcc_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
};

static const struct clk_parent_data gcc_parent_data_3[] = {
	{ .fw_name = "bi_tcxo" },
};

static const struct parent_map gcc_parent_map_4[] = {
	{ P_PCIE_0_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_4[] = {
	{ .fw_name = "pcie_0_pipe_clk" },
	{ .fw_name = "bi_tcxo" },
};

static const struct parent_map gcc_parent_map_5[] = {
	{ P_PCIE_1_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_5[] = {
	{ .fw_name = "pcie_1_pipe_clk" },
	{ .fw_name = "bi_tcxo" },
};

static const struct parent_map gcc_parent_map_6[] = {
	{ P_PCIE_2_PHY_AUX_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_6[] = {
	{ .fw_name = "pcie_2_phy_aux_clk" },
	{ .fw_name = "bi_tcxo" },
};

static const struct parent_map gcc_parent_map_7[] = {
	{ P_PCIE_2_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_7[] = {
	{ .fw_name = "pcie_2_pipe_clk" },
	{ .fw_name = "bi_tcxo" },
};

static const struct parent_map gcc_parent_map_8[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL9_OUT_MAIN, 2 },
	{ P_GCC_GPLL4_OUT_MAIN, 5 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_8[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll9.clkr.hw },
	{ .hw = &gcc_gpll4.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_9[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL4_OUT_MAIN, 5 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_9[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll4.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_10[] = {
	{ P_UFS_PHY_RX_SYMBOL_0_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_10[] = {
	{ .fw_name = "ufs_phy_rx_symbol_0_clk" },
	{ .fw_name = "bi_tcxo" },
};

static const struct parent_map gcc_parent_map_11[] = {
	{ P_UFS_PHY_RX_SYMBOL_1_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_11[] = {
	{ .fw_name = "ufs_phy_rx_symbol_1_clk" },
	{ .fw_name = "bi_tcxo" },
};

static const struct parent_map gcc_parent_map_12[] = {
	{ P_UFS_PHY_TX_SYMBOL_0_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_12[] = {
	{ .fw_name = "ufs_phy_tx_symbol_0_clk" },
	{ .fw_name = "bi_tcxo" },
};

static const struct parent_map gcc_parent_map_13[] = {
	{ P_USB3_PHY_WRAPPER_GCC_USB30_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_13[] = {
	{ .fw_name = "usb3_phy_wrapper_gcc_usb30_pipe_clk" },
	{ .fw_name = "bi_tcxo" },
};

static struct clk_regmap_mux gcc_pcie_0_pipe_clk_src = {
	.reg = 0x7b068,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_4,
	.clkr = {
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_0_pipe_clk_src",
			.parent_data = gcc_parent_data_4,
			.num_parents = ARRAY_SIZE(gcc_parent_data_4),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_pcie_1_pipe_clk_src = {
	.reg = 0xad064,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_5,
	.clkr = {
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_1_pipe_clk_src",
			.parent_data = gcc_parent_data_5,
			.num_parents = ARRAY_SIZE(gcc_parent_data_5),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_pcie_2_phy_aux_clk_src = {
	.reg = 0x9d08c,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_6,
	.clkr = {
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_2_phy_aux_clk_src",
			.parent_data = gcc_parent_data_6,
			.num_parents = ARRAY_SIZE(gcc_parent_data_6),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_pcie_2_pipe_clk_src = {
	.reg = 0x9d06c,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_7,
	.clkr = {
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_2_pipe_clk_src",
			.parent_data = gcc_parent_data_7,
			.num_parents = ARRAY_SIZE(gcc_parent_data_7),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_ufs_phy_rx_symbol_0_clk_src = {
	.reg = 0x87060,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_10,
	.clkr = {
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ufs_phy_rx_symbol_0_clk_src",
			.parent_data = gcc_parent_data_10,
			.num_parents = ARRAY_SIZE(gcc_parent_data_10),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_ufs_phy_rx_symbol_1_clk_src = {
	.reg = 0x870d0,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_11,
	.clkr = {
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ufs_phy_rx_symbol_1_clk_src",
			.parent_data = gcc_parent_data_11,
			.num_parents = ARRAY_SIZE(gcc_parent_data_11),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_ufs_phy_tx_symbol_0_clk_src = {
	.reg = 0x87050,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_12,
	.clkr = {
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ufs_phy_tx_symbol_0_clk_src",
			.parent_data = gcc_parent_data_12,
			.num_parents = ARRAY_SIZE(gcc_parent_data_12),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb3_prim_phy_pipe_clk_src = {
	.reg = 0x49068,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_13,
	.clkr = {
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_usb3_prim_phy_pipe_clk_src",
			.parent_data = gcc_parent_data_13,
			.num_parents = ARRAY_SIZE(gcc_parent_data_13),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static const struct freq_tbl ftbl_gcc_gp10_clk_src[] = {
	F(30000000, P_GCC_GPLL0_OUT_EVEN, 10, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_gp10_clk_src = {
	.cmd_rcgr = 0xb5004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_gp10_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_gp10_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 30000000},
	},
};

static struct clk_rcg2 gcc_gp11_clk_src = {
	.cmd_rcgr = 0xb6004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_gp10_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_gp11_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 30000000},
	},
};

static const struct freq_tbl ftbl_gcc_gp1_clk_src[] = {
	F(50000000, P_GCC_GPLL0_OUT_EVEN, 6, 0, 0),
	F(100000000, P_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	F(200000000, P_GCC_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_gp1_clk_src = {
	.cmd_rcgr = 0x74004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_gp1_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 50000000,
			[VDD_LOW] = 100000000,
			[VDD_NOMINAL] = 200000000},
	},
};

static struct clk_rcg2 gcc_gp2_clk_src = {
	.cmd_rcgr = 0x75004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_gp2_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 50000000,
			[VDD_LOW] = 100000000,
			[VDD_NOMINAL] = 200000000},
	},
};

static struct clk_rcg2 gcc_gp3_clk_src = {
	.cmd_rcgr = 0x76004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_gp3_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 50000000,
			[VDD_LOW] = 100000000,
			[VDD_NOMINAL] = 200000000},
	},
};

static struct clk_rcg2 gcc_gp4_clk_src = {
	.cmd_rcgr = 0xaf004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_gp10_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_gp4_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 30000000},
	},
};

static struct clk_rcg2 gcc_gp5_clk_src = {
	.cmd_rcgr = 0xb0004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_gp10_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_gp5_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 30000000},
	},
};

static struct clk_rcg2 gcc_gp6_clk_src = {
	.cmd_rcgr = 0xb1004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_gp10_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_gp6_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 30000000},
	},
};

static struct clk_rcg2 gcc_gp7_clk_src = {
	.cmd_rcgr = 0xb2004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_gp10_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_gp7_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 30000000},
	},
};

static struct clk_rcg2 gcc_gp8_clk_src = {
	.cmd_rcgr = 0xb3004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_gp10_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_gp8_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 30000000},
	},
};

static struct clk_rcg2 gcc_gp9_clk_src = {
	.cmd_rcgr = 0xb4004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_gp10_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_gp9_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 30000000},
	},
};

static const struct freq_tbl ftbl_gcc_pcie_0_aux_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie_0_aux_clk_src = {
	.cmd_rcgr = 0x7b070,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_pcie_0_aux_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = gcc_anorak_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_anorak_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000},
	},
};

static const struct freq_tbl ftbl_gcc_pcie_0_phy_rchng_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(100000000, P_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie_0_phy_rchng_clk_src = {
	.cmd_rcgr = 0x7b050,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_phy_rchng_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_pcie_0_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 100000000},
	},
};

static struct clk_rcg2 gcc_pcie_1_aux_clk_src = {
	.cmd_rcgr = 0xad06c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_pcie_1_aux_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = gcc_anorak_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_anorak_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000},
	},
};

static struct clk_rcg2 gcc_pcie_1_phy_rchng_clk_src = {
	.cmd_rcgr = 0xad04c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_phy_rchng_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_pcie_1_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 100000000},
	},
};

static struct clk_rcg2 gcc_pcie_2_aux_clk_src = {
	.cmd_rcgr = 0x9d074,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_pcie_2_aux_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = gcc_anorak_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_anorak_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000},
	},
};

static struct clk_rcg2 gcc_pcie_2_phy_rchng_clk_src = {
	.cmd_rcgr = 0x9d054,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_phy_rchng_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_pcie_2_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 100000000},
	},
};

static const struct freq_tbl ftbl_gcc_pdm2_clk_src[] = {
	F(60000000, P_GCC_GPLL0_OUT_MAIN, 10, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pdm2_clk_src = {
	.cmd_rcgr = 0x43010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pdm2_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_pdm2_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 60000000},
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
	.cmd_rcgr = 0x27014,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &gcc_qupv3_wrap0_s0_clk_src_init,
	.clkr.vdd_data = {
		.vdd_classes = gcc_anorak_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_anorak_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 120000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap0_s1_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s1_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s1_clk_src = {
	.cmd_rcgr = 0x27148,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &gcc_qupv3_wrap0_s1_clk_src_init,
	.clkr.vdd_data = {
		.vdd_classes = gcc_anorak_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_anorak_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 120000000},
	},
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap0_s2_clk_src[] = {
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
	{ }
};

static struct clk_init_data gcc_qupv3_wrap0_s2_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s2_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s2_clk_src = {
	.cmd_rcgr = 0x2727c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &gcc_qupv3_wrap0_s2_clk_src_init,
	.clkr.vdd_data = {
		.vdd_classes = gcc_anorak_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_anorak_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 100000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap0_s3_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s3_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s3_clk_src = {
	.cmd_rcgr = 0x273b0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &gcc_qupv3_wrap0_s3_clk_src_init,
	.clkr.vdd_data = {
		.vdd_classes = gcc_anorak_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_anorak_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 100000000},
	},
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap0_s4_clk_src[] = {
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
	F(102400000, P_GCC_GPLL0_OUT_EVEN, 1, 128, 375),
	F(112000000, P_GCC_GPLL0_OUT_EVEN, 1, 28, 75),
	F(117964800, P_GCC_GPLL0_OUT_EVEN, 1, 6144, 15625),
	F(120000000, P_GCC_GPLL0_OUT_MAIN, 5, 0, 0),
	F(150000000, P_GCC_GPLL0_OUT_EVEN, 2, 0, 0),
	F(200000000, P_GCC_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_init_data gcc_qupv3_wrap0_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s4_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s4_clk_src = {
	.cmd_rcgr = 0x274e4,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s4_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &gcc_qupv3_wrap0_s4_clk_src_init,
	.clkr.vdd_data = {
		.vdd_classes = gcc_anorak_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_anorak_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 150000000,
			[VDD_LOW] = 200000000},
	},
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap0_s5_clk_src[] = {
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
	F(102400000, P_GCC_GPLL0_OUT_EVEN, 1, 128, 375),
	F(112000000, P_GCC_GPLL0_OUT_EVEN, 1, 28, 75),
	F(117964800, P_GCC_GPLL0_OUT_EVEN, 1, 6144, 15625),
	F(120000000, P_GCC_GPLL0_OUT_MAIN, 5, 0, 0),
	F(128000000, P_GCC_GPLL0_OUT_MAIN, 1, 16, 75),
	{ }
};

static struct clk_init_data gcc_qupv3_wrap0_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s5_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s5_clk_src = {
	.cmd_rcgr = 0x27620,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s5_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &gcc_qupv3_wrap0_s5_clk_src_init,
	.clkr.vdd_data = {
		.vdd_classes = gcc_anorak_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_anorak_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 128000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap0_s6_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s6_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s6_clk_src = {
	.cmd_rcgr = 0x27754,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &gcc_qupv3_wrap0_s6_clk_src_init,
	.clkr.vdd_data = {
		.vdd_classes = gcc_anorak_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_anorak_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 100000000},
	},
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap1_s0_clk_src[] = {
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
	F(102400000, P_GCC_GPLL0_OUT_EVEN, 1, 128, 375),
	F(112000000, P_GCC_GPLL0_OUT_EVEN, 1, 28, 75),
	F(117964800, P_GCC_GPLL0_OUT_EVEN, 1, 6144, 15625),
	F(120000000, P_GCC_GPLL0_OUT_MAIN, 5, 0, 0),
	{ }
};

static struct clk_init_data gcc_qupv3_wrap1_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s0_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s0_clk_src = {
	.cmd_rcgr = 0x28014,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap1_s0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &gcc_qupv3_wrap1_s0_clk_src_init,
	.clkr.vdd_data = {
		.vdd_classes = gcc_anorak_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_anorak_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 120000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap1_s1_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s1_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s1_clk_src = {
	.cmd_rcgr = 0x28148,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap1_s0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &gcc_qupv3_wrap1_s1_clk_src_init,
	.clkr.vdd_data = {
		.vdd_classes = gcc_anorak_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_anorak_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 120000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap1_s2_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s2_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s2_clk_src = {
	.cmd_rcgr = 0x2827c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &gcc_qupv3_wrap1_s2_clk_src_init,
	.clkr.vdd_data = {
		.vdd_classes = gcc_anorak_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_anorak_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 100000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap1_s3_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s3_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s3_clk_src = {
	.cmd_rcgr = 0x283b0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &gcc_qupv3_wrap1_s3_clk_src_init,
	.clkr.vdd_data = {
		.vdd_classes = gcc_anorak_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_anorak_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 100000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap1_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s4_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s4_clk_src = {
	.cmd_rcgr = 0x284e4,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s4_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &gcc_qupv3_wrap1_s4_clk_src_init,
	.clkr.vdd_data = {
		.vdd_classes = gcc_anorak_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_anorak_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 150000000,
			[VDD_LOW] = 200000000},
	},
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap1_s5_clk_src[] = {
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
	F(128000000, P_GCC_GPLL0_OUT_MAIN, 1, 16, 75),
	{ }
};

static struct clk_init_data gcc_qupv3_wrap1_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s5_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s5_clk_src = {
	.cmd_rcgr = 0x28620,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap1_s5_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &gcc_qupv3_wrap1_s5_clk_src_init,
	.clkr.vdd_data = {
		.vdd_classes = gcc_anorak_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_anorak_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 128000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap1_s6_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s6_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s6_clk_src = {
	.cmd_rcgr = 0x28754,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &gcc_qupv3_wrap1_s6_clk_src_init,
	.clkr.vdd_data = {
		.vdd_classes = gcc_anorak_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_anorak_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 100000000},
	},
};

static const struct freq_tbl ftbl_gcc_sdcc2_apps_clk_src[] = {
	F(400000, P_BI_TCXO, 12, 1, 4),
	F(25000000, P_GCC_GPLL0_OUT_EVEN, 12, 0, 0),
	F(50000000, P_GCC_GPLL0_OUT_EVEN, 6, 0, 0),
	F(100000000, P_GCC_GPLL0_OUT_EVEN, 3, 0, 0),
	F(202000000, P_GCC_GPLL9_OUT_MAIN, 4, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc2_apps_clk_src = {
	.cmd_rcgr = 0x24014,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_8,
	.freq_tbl = ftbl_gcc_sdcc2_apps_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_sdcc2_apps_clk_src",
		.parent_data = gcc_parent_data_8,
		.num_parents = ARRAY_SIZE(gcc_parent_data_8),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = gcc_anorak_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_anorak_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 100000000,
			[VDD_LOW_L1] = 202000000},
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
	.cmd_rcgr = 0x8702c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_phy_axi_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_ufs_phy_axi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = gcc_anorak_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_anorak_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 150000000,
			[VDD_NOMINAL] = 300000000},
	},
};

static const struct freq_tbl ftbl_gcc_ufs_phy_ice_core_clk_src[] = {
	F(100000000, P_GCC_GPLL0_OUT_EVEN, 3, 0, 0),
	F(201500000, P_GCC_GPLL4_OUT_MAIN, 4, 0, 0),
	F(403000000, P_GCC_GPLL4_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_phy_ice_core_clk_src = {
	.cmd_rcgr = 0x87074,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_9,
	.freq_tbl = ftbl_gcc_ufs_phy_ice_core_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_ufs_phy_ice_core_clk_src",
		.parent_data = gcc_parent_data_9,
		.num_parents = ARRAY_SIZE(gcc_parent_data_9),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = gcc_anorak_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_anorak_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 100000000,
			[VDD_LOW] = 201500000,
			[VDD_NOMINAL] = 403000000},
	},
};

static const struct freq_tbl ftbl_gcc_ufs_phy_phy_aux_clk_src[] = {
	F(9600000, P_BI_TCXO, 2, 0, 0),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_phy_phy_aux_clk_src = {
	.cmd_rcgr = 0x870a8,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_ufs_phy_phy_aux_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_ufs_phy_phy_aux_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000},
	},
};

static const struct freq_tbl ftbl_gcc_ufs_phy_unipro_core_clk_src[] = {
	F(75000000, P_GCC_GPLL0_OUT_EVEN, 4, 0, 0),
	F(150000000, P_GCC_GPLL0_OUT_MAIN, 4, 0, 0),
	F(300000000, P_GCC_GPLL0_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_phy_unipro_core_clk_src = {
	.cmd_rcgr = 0x8708c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_phy_unipro_core_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_ufs_phy_unipro_core_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = gcc_anorak_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_anorak_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 150000000,
			[VDD_NOMINAL] = 300000000},
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
	.cmd_rcgr = 0x49028,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb30_prim_master_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_usb30_prim_master_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = gcc_anorak_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_anorak_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 66666667,
			[VDD_LOW] = 133333333,
			[VDD_NOMINAL] = 200000000,
			[VDD_HIGH] = 240000000},
	},
};

static struct clk_rcg2 gcc_usb30_prim_mock_utmi_clk_src = {
	.cmd_rcgr = 0x49040,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_usb30_prim_mock_utmi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000},
	},
};

static struct clk_rcg2 gcc_usb3_prim_phy_aux_clk_src = {
	.cmd_rcgr = 0x4906c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_usb3_prim_phy_aux_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000},
	},
};

static struct clk_regmap_div gcc_gp10_div_clk_src = {
	.reg = 0xb501c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp10_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_gp10_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_gp11_div_clk_src = {
	.reg = 0xb601c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp11_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_gp11_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_gp4_div_clk_src = {
	.reg = 0xaf01c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp4_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_gp4_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_gp5_div_clk_src = {
	.reg = 0xb001c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp5_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_gp5_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_gp6_div_clk_src = {
	.reg = 0xb101c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp6_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_gp6_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_gp7_div_clk_src = {
	.reg = 0xb201c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp7_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_gp7_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_gp8_div_clk_src = {
	.reg = 0xb301c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp8_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_gp8_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_gp9_div_clk_src = {
	.reg = 0xb401c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp9_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_gp9_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_pcie_0_pipe_div_clk_src = {
	.reg = 0x7b06c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_0_pipe_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_pcie_0_pipe_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_pcie_1_pipe_div_clk_src = {
	.reg = 0xad068,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_1_pipe_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_pcie_1_pipe_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_pcie_2_pipe_div_clk_src = {
	.reg = 0x9d070,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_2_pipe_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_pcie_2_pipe_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_pwm0_xo512_div_clk_src = {
	.reg = 0x43030,
	.shift = 0,
	.width = 9,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pwm0_xo512_div_clk_src",
		.parent_data = &(const struct clk_parent_data){
			.fw_name = "bi_tcxo",
		},
		.num_parents = 1,
		.ops = &clk_regmap_div_ops,
	},
};

static struct clk_regmap_div gcc_qupv3_wrap0_s4_div_clk_src = {
	.reg = 0x27614,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_wrap0_s4_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_qupv3_wrap0_s4_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_qupv3_wrap1_s4_div_clk_src = {
	.reg = 0x28614,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_wrap1_s4_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_qupv3_wrap1_s4_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_usb30_prim_mock_utmi_postdiv_clk_src = {
	.reg = 0x49058,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_prim_mock_utmi_postdiv_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_usb30_prim_mock_utmi_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch gcc_aggre_noc_pcie_axi_clk = {
	.halt_reg = 0x7b098,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x7b098,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(12),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_aggre_noc_pcie_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_noc_pcie_sf_axi_clk = {
	.halt_reg = 0xad094,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0xad094,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(17),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_aggre_noc_pcie_sf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_ufs_phy_axi_clk = {
	.halt_reg = 0x870d4,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x870d4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x870d4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_aggre_ufs_phy_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_ufs_phy_axi_hw_ctl_clk = {
	.halt_reg = 0x870d4,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x870d4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x870d4,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_aggre_ufs_phy_axi_hw_ctl_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_hw_ctl_ops,
		},
	},
};

static struct clk_branch gcc_aggre_usb3_prim_axi_clk = {
	.halt_reg = 0x49088,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x49088,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x49088,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_aggre_usb3_prim_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb30_prim_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_boot_rom_ahb_clk = {
	.halt_reg = 0x48004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x48004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_boot_rom_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_hf_axi_clk = {
	.halt_reg = 0x36010,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x36010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x36010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camera_hf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_sf_axi_clk = {
	.halt_reg = 0x36014,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x36014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x36014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camera_sf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_pcie_anoc_ahb_clk = {
	.halt_reg = 0x20030,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x20030,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(20),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_cfg_noc_pcie_anoc_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_usb3_prim_axi_clk = {
	.halt_reg = 0x49084,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x49084,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x49084,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_cfg_noc_usb3_prim_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb30_prim_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ddrss_gpu_axi_clk = {
	.halt_reg = 0x81154,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x81154,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x81154,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ddrss_gpu_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ddrss_pcie_sf_tbu_clk = {
	.halt_reg = 0x9d0a0,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x9d0a0,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(19),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ddrss_pcie_sf_tbu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp1_hf_axi_clk = {
	.halt_reg = 0x2e008,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x2e008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2e008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_disp1_hf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_hf_axi_clk = {
	.halt_reg = 0x37008,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x37008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x37008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_disp_hf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_edp_0_clkref_en = {
	.halt_reg = 0x9c028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9c028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_edp_0_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_edp_1_clkref_en = {
	.halt_reg = 0x9c02c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9c02c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_edp_1_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp10_clk = {
	.halt_reg = 0xb5000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb5000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_gp10_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_gp10_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp11_clk = {
	.halt_reg = 0xb6000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb6000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_gp11_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_gp11_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp1_clk = {
	.halt_reg = 0x74000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x74000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_gp1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_gp1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp2_clk = {
	.halt_reg = 0x75000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x75000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_gp2_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_gp2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp3_clk = {
	.halt_reg = 0x76000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x76000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_gp3_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_gp3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp4_clk = {
	.halt_reg = 0xaf000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xaf000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_gp4_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_gp4_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp5_clk = {
	.halt_reg = 0xb0000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb0000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_gp5_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_gp5_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp6_clk = {
	.halt_reg = 0xb1000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb1000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_gp6_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_gp6_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp7_clk = {
	.halt_reg = 0xb2000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb2000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_gp7_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_gp7_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp8_clk = {
	.halt_reg = 0xb3000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb3000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_gp8_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_gp8_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp9_clk = {
	.halt_reg = 0xb4000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb4000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_gp9_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_gp9_div_clk_src.clkr.hw,
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
		.enable_reg = 0x62000,
		.enable_mask = BIT(15),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_gpu_gpll0_clk_src",
			.parent_hws = (const struct clk_hw*[]){
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
		.enable_reg = 0x62000,
		.enable_mask = BIT(16),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_gpu_gpll0_div_clk_src",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_gpll0_out_even.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_memnoc_gfx_clk = {
	.halt_reg = 0x81010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x81010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x81010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_gpu_memnoc_gfx_clk",
			.flags = CLK_DONT_HOLD_STATE,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_snoc_dvm_gfx_clk = {
	.halt_reg = 0x81018,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x81018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_gpu_snoc_dvm_gfx_clk",
			.flags = CLK_DONT_HOLD_STATE,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_hlos1_vote_aggre_noc_mmu_audio_tbu_clk = {
	.halt_reg = 0x8d004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x8d004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_hlos1_vote_aggre_noc_mmu_audio_tbu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_hlos1_vote_aggre_noc_mmu_pcie_tbu_clk = {
	.halt_reg = 0x8d010,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x8d010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_hlos1_vote_aggre_noc_mmu_pcie_tbu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_hlos1_vote_aggre_noc_mmu_tbu1_clk = {
	.halt_reg = 0x8d008,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x8d008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_hlos1_vote_aggre_noc_mmu_tbu1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_hlos1_vote_aggre_noc_mmu_tbu2_clk = {
	.halt_reg = 0x8d00c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x8d00c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_hlos1_vote_aggre_noc_mmu_tbu2_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_hlos1_vote_mmnoc_mmu_tbu_hf0_clk = {
	.halt_reg = 0x8d018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x8d018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_hlos1_vote_mmnoc_mmu_tbu_hf0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_hlos1_vote_mmnoc_mmu_tbu_hf1_clk = {
	.halt_reg = 0x8d01c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x8d01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_hlos1_vote_mmnoc_mmu_tbu_hf1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_hlos1_vote_mmnoc_mmu_tbu_hf2_clk = {
	.halt_reg = 0x8d070,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x8d070,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_hlos1_vote_mmnoc_mmu_tbu_hf2_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_hlos1_vote_mmnoc_mmu_tbu_hf3_clk = {
	.halt_reg = 0x8d074,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x8d074,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_hlos1_vote_mmnoc_mmu_tbu_hf3_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_hlos1_vote_mmnoc_mmu_tbu_hf4_clk = {
	.halt_reg = 0x8d080,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x8d080,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_hlos1_vote_mmnoc_mmu_tbu_hf4_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_hlos1_vote_mmnoc_mmu_tbu_hf5_clk = {
	.halt_reg = 0x8d084,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x8d084,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_hlos1_vote_mmnoc_mmu_tbu_hf5_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_hlos1_vote_mmnoc_mmu_tbu_sf0_clk = {
	.halt_reg = 0x8d014,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x8d014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_hlos1_vote_mmnoc_mmu_tbu_sf0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_hlos1_vote_mmnoc_mmu_tbu_sf1_clk = {
	.halt_reg = 0x8d030,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x8d030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_hlos1_vote_mmnoc_mmu_tbu_sf1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_hlos1_vote_mmu_tcu_clk = {
	.halt_reg = 0x8d02c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x8d02c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_hlos1_vote_mmu_tcu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_hlos1_vote_turing_mmu_tbu0_clk = {
	.halt_reg = 0x8d020,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x8d020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_hlos1_vote_turing_mmu_tbu0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_hlos1_vote_turing_mmu_tbu1_clk = {
	.halt_reg = 0x8d024,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x8d024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_hlos1_vote_turing_mmu_tbu1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_aux_clk = {
	.halt_reg = 0x7b034,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(3),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_0_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_0_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_cfg_ahb_clk = {
	.halt_reg = 0x7b030,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x7b030,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_0_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_clkref_en = {
	.halt_reg = 0x9c004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9c004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_0_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_mstr_axi_clk = {
	.halt_reg = 0x7b028,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_0_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_phy_rchng_clk = {
	.halt_reg = 0x7b04c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(22),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_0_phy_rchng_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_0_phy_rchng_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_pipe_clk = {
	.halt_reg = 0x7b03c,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_0_pipe_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_0_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_pipe_div2_clk = {
	.halt_reg = 0x7b044,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(15),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_0_pipe_div2_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_0_pipe_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_slv_axi_clk = {
	.halt_reg = 0x7b020,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x7b020,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_0_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_slv_q2a_axi_clk = {
	.halt_reg = 0x7b01c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_0_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_aux_clk = {
	.halt_reg = 0xad030,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(9),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_1_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_1_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_cfg_ahb_clk = {
	.halt_reg = 0xad02c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xad02c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_1_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_clkref_en = {
	.halt_reg = 0x9c008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9c008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_1_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_mstr_axi_clk = {
	.halt_reg = 0xad024,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_1_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_phy_rchng_clk = {
	.halt_reg = 0xad048,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_1_phy_rchng_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_1_phy_rchng_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_pipe_clk = {
	.halt_reg = 0xad038,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_1_pipe_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_1_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_pipe_div2_clk = {
	.halt_reg = 0xad040,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(16),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_1_pipe_div2_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_1_pipe_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_slv_axi_clk = {
	.halt_reg = 0xad01c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xad01c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(6),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_1_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_slv_q2a_axi_clk = {
	.halt_reg = 0xad018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_1_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_aux_clk = {
	.halt_reg = 0x9d030,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(29),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_2_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_2_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_cfg_ahb_clk = {
	.halt_reg = 0x9d02c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x9d02c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(28),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_2_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_clkref_en = {
	.halt_reg = 0x9c00c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9c00c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_2_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_mstr_axi_clk = {
	.halt_reg = 0x9d024,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(27),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_2_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_phy_aux_clk = {
	.halt_reg = 0x9d038,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(24),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_2_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_2_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_phy_rchng_clk = {
	.halt_reg = 0x9d050,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(23),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_2_phy_rchng_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_2_phy_rchng_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_pipe_clk = {
	.halt_reg = 0x9d040,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(30),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_2_pipe_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_2_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_pipe_div2_clk = {
	.halt_reg = 0x9d048,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(17),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_2_pipe_div2_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_2_pipe_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_slv_axi_clk = {
	.halt_reg = 0x9d01c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x9d01c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(26),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_2_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_slv_q2a_axi_clk = {
	.halt_reg = 0x9d018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(25),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pcie_2_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm2_clk = {
	.halt_reg = 0x4300c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4300c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pdm2_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pdm2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm_ahb_clk = {
	.halt_reg = 0x43004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x43004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x43004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pdm_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm_xo4_clk = {
	.halt_reg = 0x43008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x43008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pdm_xo4_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pwm0_xo512_clk = {
	.halt_reg = 0x4302c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4302c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pwm0_xo512_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pwm0_xo512_div_clk_src.clkr.hw
			},
			.flags = CLK_SET_RATE_PARENT,
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_camera_nrt_ahb_clk = {
	.halt_reg = 0x36008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x36008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x36008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qmip_camera_nrt_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_camera_rt_ahb_clk = {
	.halt_reg = 0x3600c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x3600c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3600c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qmip_camera_rt_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_gpu_ahb_clk = {
	.halt_reg = 0x81008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x81008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x81008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qmip_gpu_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_pcie_ahb_clk = {
	.halt_reg = 0x7b018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x7b018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7b018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qmip_pcie_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_cv_cpu_ahb_clk = {
	.halt_reg = 0x42014,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x42014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x42014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qmip_video_cv_cpu_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_cvp_ahb_clk = {
	.halt_reg = 0x42008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x42008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x42008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qmip_video_cvp_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_v_cpu_ahb_clk = {
	.halt_reg = 0x42010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x42010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x42010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qmip_video_v_cpu_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_vcodec_ahb_clk = {
	.halt_reg = 0x4200c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x4200c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x4200c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qmip_video_vcodec_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_core_2x_clk = {
	.halt_reg = 0x3300c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(9),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap0_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_core_clk = {
	.halt_reg = 0x33000,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap0_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_qspi0_clk = {
	.halt_reg = 0x27610,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(3),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap0_qspi0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap0_s4_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s0_clk = {
	.halt_reg = 0x2700c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap0_s0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s1_clk = {
	.halt_reg = 0x27140,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(11),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap0_s1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s2_clk = {
	.halt_reg = 0x27274,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(12),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s2_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap0_s2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s3_clk = {
	.halt_reg = 0x273a8,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(13),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s3_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap0_s3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s4_clk = {
	.halt_reg = 0x274dc,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(14),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s4_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap0_s4_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s5_clk = {
	.halt_reg = 0x27618,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(15),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s5_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap0_s5_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s6_clk = {
	.halt_reg = 0x2774c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(16),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s6_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap0_s6_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_core_2x_clk = {
	.halt_reg = 0x3314c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(18),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap1_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_core_clk = {
	.halt_reg = 0x33140,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(19),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap1_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_qspi0_clk = {
	.halt_reg = 0x28610,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap1_qspi0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap1_s4_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s0_clk = {
	.halt_reg = 0x2800c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(22),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap1_s0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap1_s0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s1_clk = {
	.halt_reg = 0x28140,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(23),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap1_s1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap1_s1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s2_clk = {
	.halt_reg = 0x28274,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(24),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap1_s2_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap1_s2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s3_clk = {
	.halt_reg = 0x283a8,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(25),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap1_s3_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap1_s3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s4_clk = {
	.halt_reg = 0x284dc,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(26),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap1_s4_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap1_s4_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s5_clk = {
	.halt_reg = 0x28618,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(27),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap1_s5_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap1_s5_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s6_clk = {
	.halt_reg = 0x2874c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(28),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap1_s6_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap1_s6_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_0_m_ahb_clk = {
	.halt_reg = 0x27004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x27004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(6),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap_0_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_0_s_ahb_clk = {
	.halt_reg = 0x27008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x27008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap_0_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_1_m_ahb_clk = {
	.halt_reg = 0x28004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x28004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(20),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap_1_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_1_s_ahb_clk = {
	.halt_reg = 0x28008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x28008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(21),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap_1_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc2_ahb_clk = {
	.halt_reg = 0x2400c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2400c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_sdcc2_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc2_apps_clk = {
	.halt_reg = 0x24004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x24004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_sdcc2_apps_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_sdcc2_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_0_clkref_en = {
	.halt_reg = 0x9c000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9c000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_0_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_ahb_clk = {
	.halt_reg = 0x87020,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x87020,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x87020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ufs_phy_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_axi_clk = {
	.halt_reg = 0x87018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x87018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x87018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ufs_phy_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_axi_hw_ctl_clk = {
	.halt_reg = 0x87018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x87018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x87018,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ufs_phy_axi_hw_ctl_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_hw_ctl_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_ice_core_clk = {
	.halt_reg = 0x8706c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x8706c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x8706c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ufs_phy_ice_core_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_ice_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_ice_core_hw_ctl_clk = {
	.halt_reg = 0x8706c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x8706c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x8706c,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ufs_phy_ice_core_hw_ctl_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_ice_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_hw_ctl_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_phy_aux_clk = {
	.halt_reg = 0x870a4,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x870a4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x870a4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ufs_phy_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_phy_aux_hw_ctl_clk = {
	.halt_reg = 0x870a4,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x870a4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x870a4,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ufs_phy_phy_aux_hw_ctl_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_hw_ctl_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_rx_symbol_0_clk = {
	.halt_reg = 0x87028,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x87028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ufs_phy_rx_symbol_0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_rx_symbol_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_rx_symbol_1_clk = {
	.halt_reg = 0x870c0,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x870c0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ufs_phy_rx_symbol_1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_rx_symbol_1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_tx_symbol_0_clk = {
	.halt_reg = 0x87024,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x87024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ufs_phy_tx_symbol_0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_tx_symbol_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_unipro_core_clk = {
	.halt_reg = 0x87064,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x87064,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x87064,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ufs_phy_unipro_core_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_unipro_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_unipro_core_hw_ctl_clk = {
	.halt_reg = 0x87064,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x87064,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x87064,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ufs_phy_unipro_core_hw_ctl_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_unipro_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_hw_ctl_ops,
		},
	},
};

static struct clk_branch gcc_usb2_0_clkref_en = {
	.halt_reg = 0x9c010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9c010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb2_0_clkref_en",
			.flags = CLK_DONT_HOLD_STATE,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_prim_master_clk = {
	.halt_reg = 0x49018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x49018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_usb30_prim_master_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb30_prim_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_prim_mock_utmi_clk = {
	.halt_reg = 0x49024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x49024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_usb30_prim_mock_utmi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb30_prim_mock_utmi_postdiv_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_prim_sleep_clk = {
	.halt_reg = 0x49020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x49020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_usb30_prim_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_0_clkref_en = {
	.halt_reg = 0x9c014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9c014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_0_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_phy_aux_clk = {
	.halt_reg = 0x4905c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4905c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_usb3_prim_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb3_prim_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_phy_com_aux_clk = {
	.halt_reg = 0x49060,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x49060,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_usb3_prim_phy_com_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb3_prim_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_phy_pipe_clk = {
	.halt_reg = 0x49064,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x49064,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x49064,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_usb3_prim_phy_pipe_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb3_prim_phy_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_axi0_clk = {
	.halt_reg = 0x42018,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x42018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x42018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_video_axi0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_axi1_clk = {
	.halt_reg = 0x42020,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x42020,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x42020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_video_axi1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap *gcc_anorak_clocks[] = {
	[GCC_AGGRE_NOC_PCIE_AXI_CLK] = &gcc_aggre_noc_pcie_axi_clk.clkr,
	[GCC_AGGRE_NOC_PCIE_SF_AXI_CLK] = &gcc_aggre_noc_pcie_sf_axi_clk.clkr,
	[GCC_AGGRE_UFS_PHY_AXI_CLK] = &gcc_aggre_ufs_phy_axi_clk.clkr,
	[GCC_AGGRE_UFS_PHY_AXI_HW_CTL_CLK] = &gcc_aggre_ufs_phy_axi_hw_ctl_clk.clkr,
	[GCC_AGGRE_USB3_PRIM_AXI_CLK] = &gcc_aggre_usb3_prim_axi_clk.clkr,
	[GCC_BOOT_ROM_AHB_CLK] = &gcc_boot_rom_ahb_clk.clkr,
	[GCC_CAMERA_HF_AXI_CLK] = &gcc_camera_hf_axi_clk.clkr,
	[GCC_CAMERA_SF_AXI_CLK] = &gcc_camera_sf_axi_clk.clkr,
	[GCC_CFG_NOC_PCIE_ANOC_AHB_CLK] = &gcc_cfg_noc_pcie_anoc_ahb_clk.clkr,
	[GCC_CFG_NOC_USB3_PRIM_AXI_CLK] = &gcc_cfg_noc_usb3_prim_axi_clk.clkr,
	[GCC_DDRSS_GPU_AXI_CLK] = &gcc_ddrss_gpu_axi_clk.clkr,
	[GCC_DDRSS_PCIE_SF_TBU_CLK] = &gcc_ddrss_pcie_sf_tbu_clk.clkr,
	[GCC_DISP1_HF_AXI_CLK] = &gcc_disp1_hf_axi_clk.clkr,
	[GCC_DISP_HF_AXI_CLK] = &gcc_disp_hf_axi_clk.clkr,
	[GCC_EDP_0_CLKREF_EN] = &gcc_edp_0_clkref_en.clkr,
	[GCC_EDP_1_CLKREF_EN] = &gcc_edp_1_clkref_en.clkr,
	[GCC_GP10_CLK] = &gcc_gp10_clk.clkr,
	[GCC_GP10_CLK_SRC] = &gcc_gp10_clk_src.clkr,
	[GCC_GP10_DIV_CLK_SRC] = &gcc_gp10_div_clk_src.clkr,
	[GCC_GP11_CLK] = &gcc_gp11_clk.clkr,
	[GCC_GP11_CLK_SRC] = &gcc_gp11_clk_src.clkr,
	[GCC_GP11_DIV_CLK_SRC] = &gcc_gp11_div_clk_src.clkr,
	[GCC_GP1_CLK] = &gcc_gp1_clk.clkr,
	[GCC_GP1_CLK_SRC] = &gcc_gp1_clk_src.clkr,
	[GCC_GP2_CLK] = &gcc_gp2_clk.clkr,
	[GCC_GP2_CLK_SRC] = &gcc_gp2_clk_src.clkr,
	[GCC_GP3_CLK] = &gcc_gp3_clk.clkr,
	[GCC_GP3_CLK_SRC] = &gcc_gp3_clk_src.clkr,
	[GCC_GP4_CLK] = &gcc_gp4_clk.clkr,
	[GCC_GP4_CLK_SRC] = &gcc_gp4_clk_src.clkr,
	[GCC_GP4_DIV_CLK_SRC] = &gcc_gp4_div_clk_src.clkr,
	[GCC_GP5_CLK] = &gcc_gp5_clk.clkr,
	[GCC_GP5_CLK_SRC] = &gcc_gp5_clk_src.clkr,
	[GCC_GP5_DIV_CLK_SRC] = &gcc_gp5_div_clk_src.clkr,
	[GCC_GP6_CLK] = &gcc_gp6_clk.clkr,
	[GCC_GP6_CLK_SRC] = &gcc_gp6_clk_src.clkr,
	[GCC_GP6_DIV_CLK_SRC] = &gcc_gp6_div_clk_src.clkr,
	[GCC_GP7_CLK] = &gcc_gp7_clk.clkr,
	[GCC_GP7_CLK_SRC] = &gcc_gp7_clk_src.clkr,
	[GCC_GP7_DIV_CLK_SRC] = &gcc_gp7_div_clk_src.clkr,
	[GCC_GP8_CLK] = &gcc_gp8_clk.clkr,
	[GCC_GP8_CLK_SRC] = &gcc_gp8_clk_src.clkr,
	[GCC_GP8_DIV_CLK_SRC] = &gcc_gp8_div_clk_src.clkr,
	[GCC_GP9_CLK] = &gcc_gp9_clk.clkr,
	[GCC_GP9_CLK_SRC] = &gcc_gp9_clk_src.clkr,
	[GCC_GP9_DIV_CLK_SRC] = &gcc_gp9_div_clk_src.clkr,
	[GCC_GPLL0] = &gcc_gpll0.clkr,
	[GCC_GPLL0_OUT_EVEN] = &gcc_gpll0_out_even.clkr,
	[GCC_GPLL4] = &gcc_gpll4.clkr,
	[GCC_GPLL9] = &gcc_gpll9.clkr,
	[GCC_GPU_GPLL0_CLK_SRC] = &gcc_gpu_gpll0_clk_src.clkr,
	[GCC_GPU_GPLL0_DIV_CLK_SRC] = &gcc_gpu_gpll0_div_clk_src.clkr,
	[GCC_GPU_MEMNOC_GFX_CLK] = &gcc_gpu_memnoc_gfx_clk.clkr,
	[GCC_GPU_SNOC_DVM_GFX_CLK] = &gcc_gpu_snoc_dvm_gfx_clk.clkr,
	[GCC_HLOS1_VOTE_AGGRE_NOC_MMU_AUDIO_TBU_CLK] =
		&gcc_hlos1_vote_aggre_noc_mmu_audio_tbu_clk.clkr,
	[GCC_HLOS1_VOTE_AGGRE_NOC_MMU_PCIE_TBU_CLK] =
		&gcc_hlos1_vote_aggre_noc_mmu_pcie_tbu_clk.clkr,
	[GCC_HLOS1_VOTE_AGGRE_NOC_MMU_TBU1_CLK] = &gcc_hlos1_vote_aggre_noc_mmu_tbu1_clk.clkr,
	[GCC_HLOS1_VOTE_AGGRE_NOC_MMU_TBU2_CLK] = &gcc_hlos1_vote_aggre_noc_mmu_tbu2_clk.clkr,
	[GCC_HLOS1_VOTE_MMNOC_MMU_TBU_HF0_CLK] = &gcc_hlos1_vote_mmnoc_mmu_tbu_hf0_clk.clkr,
	[GCC_HLOS1_VOTE_MMNOC_MMU_TBU_HF1_CLK] = &gcc_hlos1_vote_mmnoc_mmu_tbu_hf1_clk.clkr,
	[GCC_HLOS1_VOTE_MMNOC_MMU_TBU_HF2_CLK] = &gcc_hlos1_vote_mmnoc_mmu_tbu_hf2_clk.clkr,
	[GCC_HLOS1_VOTE_MMNOC_MMU_TBU_HF3_CLK] = &gcc_hlos1_vote_mmnoc_mmu_tbu_hf3_clk.clkr,
	[GCC_HLOS1_VOTE_MMNOC_MMU_TBU_HF4_CLK] = &gcc_hlos1_vote_mmnoc_mmu_tbu_hf4_clk.clkr,
	[GCC_HLOS1_VOTE_MMNOC_MMU_TBU_HF5_CLK] = &gcc_hlos1_vote_mmnoc_mmu_tbu_hf5_clk.clkr,
	[GCC_HLOS1_VOTE_MMNOC_MMU_TBU_SF0_CLK] = &gcc_hlos1_vote_mmnoc_mmu_tbu_sf0_clk.clkr,
	[GCC_HLOS1_VOTE_MMNOC_MMU_TBU_SF1_CLK] = &gcc_hlos1_vote_mmnoc_mmu_tbu_sf1_clk.clkr,
	[GCC_HLOS1_VOTE_MMU_TCU_CLK] = &gcc_hlos1_vote_mmu_tcu_clk.clkr,
	[GCC_HLOS1_VOTE_TURING_MMU_TBU0_CLK] = &gcc_hlos1_vote_turing_mmu_tbu0_clk.clkr,
	[GCC_HLOS1_VOTE_TURING_MMU_TBU1_CLK] = &gcc_hlos1_vote_turing_mmu_tbu1_clk.clkr,
	[GCC_PCIE_0_AUX_CLK] = &gcc_pcie_0_aux_clk.clkr,
	[GCC_PCIE_0_AUX_CLK_SRC] = &gcc_pcie_0_aux_clk_src.clkr,
	[GCC_PCIE_0_CFG_AHB_CLK] = &gcc_pcie_0_cfg_ahb_clk.clkr,
	[GCC_PCIE_0_CLKREF_EN] = &gcc_pcie_0_clkref_en.clkr,
	[GCC_PCIE_0_MSTR_AXI_CLK] = &gcc_pcie_0_mstr_axi_clk.clkr,
	[GCC_PCIE_0_PHY_RCHNG_CLK] = &gcc_pcie_0_phy_rchng_clk.clkr,
	[GCC_PCIE_0_PHY_RCHNG_CLK_SRC] = &gcc_pcie_0_phy_rchng_clk_src.clkr,
	[GCC_PCIE_0_PIPE_CLK] = &gcc_pcie_0_pipe_clk.clkr,
	[GCC_PCIE_0_PIPE_CLK_SRC] = &gcc_pcie_0_pipe_clk_src.clkr,
	[GCC_PCIE_0_PIPE_DIV2_CLK] = &gcc_pcie_0_pipe_div2_clk.clkr,
	[GCC_PCIE_0_PIPE_DIV_CLK_SRC] = &gcc_pcie_0_pipe_div_clk_src.clkr,
	[GCC_PCIE_0_SLV_AXI_CLK] = &gcc_pcie_0_slv_axi_clk.clkr,
	[GCC_PCIE_0_SLV_Q2A_AXI_CLK] = &gcc_pcie_0_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_1_AUX_CLK] = &gcc_pcie_1_aux_clk.clkr,
	[GCC_PCIE_1_AUX_CLK_SRC] = &gcc_pcie_1_aux_clk_src.clkr,
	[GCC_PCIE_1_CFG_AHB_CLK] = &gcc_pcie_1_cfg_ahb_clk.clkr,
	[GCC_PCIE_1_CLKREF_EN] = &gcc_pcie_1_clkref_en.clkr,
	[GCC_PCIE_1_MSTR_AXI_CLK] = &gcc_pcie_1_mstr_axi_clk.clkr,
	[GCC_PCIE_1_PHY_RCHNG_CLK] = &gcc_pcie_1_phy_rchng_clk.clkr,
	[GCC_PCIE_1_PHY_RCHNG_CLK_SRC] = &gcc_pcie_1_phy_rchng_clk_src.clkr,
	[GCC_PCIE_1_PIPE_CLK] = &gcc_pcie_1_pipe_clk.clkr,
	[GCC_PCIE_1_PIPE_CLK_SRC] = &gcc_pcie_1_pipe_clk_src.clkr,
	[GCC_PCIE_1_PIPE_DIV2_CLK] = &gcc_pcie_1_pipe_div2_clk.clkr,
	[GCC_PCIE_1_PIPE_DIV_CLK_SRC] = &gcc_pcie_1_pipe_div_clk_src.clkr,
	[GCC_PCIE_1_SLV_AXI_CLK] = &gcc_pcie_1_slv_axi_clk.clkr,
	[GCC_PCIE_1_SLV_Q2A_AXI_CLK] = &gcc_pcie_1_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_2_AUX_CLK] = &gcc_pcie_2_aux_clk.clkr,
	[GCC_PCIE_2_AUX_CLK_SRC] = &gcc_pcie_2_aux_clk_src.clkr,
	[GCC_PCIE_2_CFG_AHB_CLK] = &gcc_pcie_2_cfg_ahb_clk.clkr,
	[GCC_PCIE_2_CLKREF_EN] = &gcc_pcie_2_clkref_en.clkr,
	[GCC_PCIE_2_MSTR_AXI_CLK] = &gcc_pcie_2_mstr_axi_clk.clkr,
	[GCC_PCIE_2_PHY_AUX_CLK] = &gcc_pcie_2_phy_aux_clk.clkr,
	[GCC_PCIE_2_PHY_AUX_CLK_SRC] = &gcc_pcie_2_phy_aux_clk_src.clkr,
	[GCC_PCIE_2_PHY_RCHNG_CLK] = &gcc_pcie_2_phy_rchng_clk.clkr,
	[GCC_PCIE_2_PHY_RCHNG_CLK_SRC] = &gcc_pcie_2_phy_rchng_clk_src.clkr,
	[GCC_PCIE_2_PIPE_CLK] = &gcc_pcie_2_pipe_clk.clkr,
	[GCC_PCIE_2_PIPE_CLK_SRC] = &gcc_pcie_2_pipe_clk_src.clkr,
	[GCC_PCIE_2_PIPE_DIV2_CLK] = &gcc_pcie_2_pipe_div2_clk.clkr,
	[GCC_PCIE_2_PIPE_DIV_CLK_SRC] = &gcc_pcie_2_pipe_div_clk_src.clkr,
	[GCC_PCIE_2_SLV_AXI_CLK] = &gcc_pcie_2_slv_axi_clk.clkr,
	[GCC_PCIE_2_SLV_Q2A_AXI_CLK] = &gcc_pcie_2_slv_q2a_axi_clk.clkr,
	[GCC_PDM2_CLK] = &gcc_pdm2_clk.clkr,
	[GCC_PDM2_CLK_SRC] = &gcc_pdm2_clk_src.clkr,
	[GCC_PDM_AHB_CLK] = &gcc_pdm_ahb_clk.clkr,
	[GCC_PDM_XO4_CLK] = &gcc_pdm_xo4_clk.clkr,
	[GCC_PWM0_XO512_CLK] = &gcc_pwm0_xo512_clk.clkr,
	[GCC_PWM0_XO512_DIV_CLK_SRC] = &gcc_pwm0_xo512_div_clk_src.clkr,
	[GCC_QMIP_CAMERA_NRT_AHB_CLK] = &gcc_qmip_camera_nrt_ahb_clk.clkr,
	[GCC_QMIP_CAMERA_RT_AHB_CLK] = &gcc_qmip_camera_rt_ahb_clk.clkr,
	[GCC_QMIP_GPU_AHB_CLK] = &gcc_qmip_gpu_ahb_clk.clkr,
	[GCC_QMIP_PCIE_AHB_CLK] = &gcc_qmip_pcie_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_CV_CPU_AHB_CLK] = &gcc_qmip_video_cv_cpu_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_CVP_AHB_CLK] = &gcc_qmip_video_cvp_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_V_CPU_AHB_CLK] = &gcc_qmip_video_v_cpu_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_VCODEC_AHB_CLK] = &gcc_qmip_video_vcodec_ahb_clk.clkr,
	[GCC_QUPV3_WRAP0_CORE_2X_CLK] = &gcc_qupv3_wrap0_core_2x_clk.clkr,
	[GCC_QUPV3_WRAP0_CORE_CLK] = &gcc_qupv3_wrap0_core_clk.clkr,
	[GCC_QUPV3_WRAP0_QSPI0_CLK] = &gcc_qupv3_wrap0_qspi0_clk.clkr,
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
	[GCC_QUPV3_WRAP0_S4_DIV_CLK_SRC] = &gcc_qupv3_wrap0_s4_div_clk_src.clkr,
	[GCC_QUPV3_WRAP0_S5_CLK] = &gcc_qupv3_wrap0_s5_clk.clkr,
	[GCC_QUPV3_WRAP0_S5_CLK_SRC] = &gcc_qupv3_wrap0_s5_clk_src.clkr,
	[GCC_QUPV3_WRAP0_S6_CLK] = &gcc_qupv3_wrap0_s6_clk.clkr,
	[GCC_QUPV3_WRAP0_S6_CLK_SRC] = &gcc_qupv3_wrap0_s6_clk_src.clkr,
	[GCC_QUPV3_WRAP1_CORE_2X_CLK] = &gcc_qupv3_wrap1_core_2x_clk.clkr,
	[GCC_QUPV3_WRAP1_CORE_CLK] = &gcc_qupv3_wrap1_core_clk.clkr,
	[GCC_QUPV3_WRAP1_QSPI0_CLK] = &gcc_qupv3_wrap1_qspi0_clk.clkr,
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
	[GCC_QUPV3_WRAP1_S4_DIV_CLK_SRC] = &gcc_qupv3_wrap1_s4_div_clk_src.clkr,
	[GCC_QUPV3_WRAP1_S5_CLK] = &gcc_qupv3_wrap1_s5_clk.clkr,
	[GCC_QUPV3_WRAP1_S5_CLK_SRC] = &gcc_qupv3_wrap1_s5_clk_src.clkr,
	[GCC_QUPV3_WRAP1_S6_CLK] = &gcc_qupv3_wrap1_s6_clk.clkr,
	[GCC_QUPV3_WRAP1_S6_CLK_SRC] = &gcc_qupv3_wrap1_s6_clk_src.clkr,
	[GCC_QUPV3_WRAP_0_M_AHB_CLK] = &gcc_qupv3_wrap_0_m_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_0_S_AHB_CLK] = &gcc_qupv3_wrap_0_s_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_1_M_AHB_CLK] = &gcc_qupv3_wrap_1_m_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_1_S_AHB_CLK] = &gcc_qupv3_wrap_1_s_ahb_clk.clkr,
	[GCC_SDCC2_AHB_CLK] = &gcc_sdcc2_ahb_clk.clkr,
	[GCC_SDCC2_APPS_CLK] = &gcc_sdcc2_apps_clk.clkr,
	[GCC_SDCC2_APPS_CLK_SRC] = &gcc_sdcc2_apps_clk_src.clkr,
	[GCC_UFS_0_CLKREF_EN] = &gcc_ufs_0_clkref_en.clkr,
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
	[GCC_USB2_0_CLKREF_EN] = &gcc_usb2_0_clkref_en.clkr,
	[GCC_USB30_PRIM_MASTER_CLK] = &gcc_usb30_prim_master_clk.clkr,
	[GCC_USB30_PRIM_MASTER_CLK_SRC] = &gcc_usb30_prim_master_clk_src.clkr,
	[GCC_USB30_PRIM_MOCK_UTMI_CLK] = &gcc_usb30_prim_mock_utmi_clk.clkr,
	[GCC_USB30_PRIM_MOCK_UTMI_CLK_SRC] = &gcc_usb30_prim_mock_utmi_clk_src.clkr,
	[GCC_USB30_PRIM_MOCK_UTMI_POSTDIV_CLK_SRC] = &gcc_usb30_prim_mock_utmi_postdiv_clk_src.clkr,
	[GCC_USB30_PRIM_SLEEP_CLK] = &gcc_usb30_prim_sleep_clk.clkr,
	[GCC_USB3_0_CLKREF_EN] = &gcc_usb3_0_clkref_en.clkr,
	[GCC_USB3_PRIM_PHY_AUX_CLK] = &gcc_usb3_prim_phy_aux_clk.clkr,
	[GCC_USB3_PRIM_PHY_AUX_CLK_SRC] = &gcc_usb3_prim_phy_aux_clk_src.clkr,
	[GCC_USB3_PRIM_PHY_COM_AUX_CLK] = &gcc_usb3_prim_phy_com_aux_clk.clkr,
	[GCC_USB3_PRIM_PHY_PIPE_CLK] = &gcc_usb3_prim_phy_pipe_clk.clkr,
	[GCC_USB3_PRIM_PHY_PIPE_CLK_SRC] = &gcc_usb3_prim_phy_pipe_clk_src.clkr,
	[GCC_VIDEO_AXI0_CLK] = &gcc_video_axi0_clk.clkr,
	[GCC_VIDEO_AXI1_CLK] = &gcc_video_axi1_clk.clkr,
};

static const struct qcom_reset_map gcc_anorak_resets[] = {
	[GCC_CAMERA_BCR] = { 0x36000 },
	[GCC_DISPLAY1_BCR] = { 0x2e000 },
	[GCC_DISPLAY_BCR] = { 0x37000 },
	[GCC_GPU_BCR] = { 0x81000 },
	[GCC_PCIE_0_BCR] = { 0x7b000 },
	[GCC_PCIE_0_LINK_DOWN_BCR] = { 0x7c014 },
	[GCC_PCIE_0_NOCSR_COM_PHY_BCR] = { 0x7c020 },
	[GCC_PCIE_0_PHY_BCR] = { 0x7c01c },
	[GCC_PCIE_0_PHY_NOCSR_COM_PHY_BCR] = { 0x7c028 },
	[GCC_PCIE_1_BCR] = { 0xad000 },
	[GCC_PCIE_1_LINK_DOWN_BCR] = { 0xae014 },
	[GCC_PCIE_1_NOCSR_COM_PHY_BCR] = { 0xae020 },
	[GCC_PCIE_1_PHY_BCR] = { 0xae01c },
	[GCC_PCIE_1_PHY_NOCSR_COM_PHY_BCR] = { 0xae028 },
	[GCC_PCIE_2_BCR] = { 0x9d000 },
	[GCC_PCIE_2_LINK_DOWN_BCR] = { 0x9e014 },
	[GCC_PCIE_2_NOCSR_COM_PHY_BCR] = { 0x9e020 },
	[GCC_PCIE_2_PHY_BCR] = { 0x9e01c },
	[GCC_PCIE_2_PHY_NOCSR_COM_PHY_BCR] = { 0x9e000 },
	[GCC_PCIE_PHY_BCR] = { 0x7f000 },
	[GCC_PCIE_PHY_CFG_AHB_BCR] = { 0x7f00c },
	[GCC_PCIE_PHY_COM_BCR] = { 0x7f010 },
	[GCC_PDM_BCR] = { 0x43000 },
	[GCC_QUPV3_WRAPPER_0_BCR] = { 0x27000 },
	[GCC_QUPV3_WRAPPER_1_BCR] = { 0x28000 },
	[GCC_QUSB2PHY_PRIM_BCR] = { 0x22000 },
	[GCC_QUSB2PHY_SEC_BCR] = { 0x22004 },
	[GCC_SDCC2_BCR] = { 0x24000 },
	[GCC_UFS_PHY_BCR] = { 0x87000 },
	[GCC_USB30_PRIM_BCR] = { 0x49000 },
	[GCC_USB3_DP_PHY_PRIM_BCR] = { 0x60008 },
	[GCC_USB3_DP_PHY_SEC_BCR] = { 0x60014 },
	[GCC_USB3_PHY_PRIM_BCR] = { 0x60000 },
	[GCC_USB3_PHY_SEC_BCR] = { 0x6000c },
	[GCC_USB3PHY_PHY_PRIM_BCR] = { 0x60004 },
	[GCC_USB3PHY_PHY_SEC_BCR] = { 0x60010 },
	[GCC_USB_PHY_CFG_AHB2PHY_BCR] = { 0x7a000 },
	[GCC_VIDEO_AXI0_CLK_ARES] = { 0x42018, 2 },
	[GCC_VIDEO_AXI1_CLK_ARES] = { 0x42020, 2 },
	[GCC_VIDEO_BCR] = { 0x42000 },
};


static const struct clk_rcg_dfs_data gcc_dfs_clocks[] = {
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s0_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s1_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s2_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s3_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s4_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s5_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s6_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s0_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s1_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s2_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s3_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s4_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s5_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s6_clk_src),
};

static const struct regmap_config gcc_anorak_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x1f41f0,
	.fast_io = true,
};

static const struct qcom_cc_desc gcc_anorak_desc = {
	.config = &gcc_anorak_regmap_config,
	.clks = gcc_anorak_clocks,
	.num_clks = ARRAY_SIZE(gcc_anorak_clocks),
	.resets = gcc_anorak_resets,
	.num_resets = ARRAY_SIZE(gcc_anorak_resets),
	.clk_regulators = gcc_anorak_regulators,
	.num_clk_regulators = ARRAY_SIZE(gcc_anorak_regulators),
};

static const struct of_device_id gcc_anorak_match_table[] = {
	{ .compatible = "qcom,anorak-gcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_anorak_match_table);

static int gcc_anorak_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret;

	regmap = qcom_cc_map(pdev, &gcc_anorak_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/*
	 * Keep the clocks always-ON
	 * GCC_CAMERA_AHB_CLK, GCC_CAMERA_XO_CLK, GCC_DISP_AHB_CLK
	 * GCC_VIDEO_AHB_CLK, GCC_VIDEO_XO_CLK, GCC_GPU_CFG_AHB_CLK
	 * GCC_DISP1_AHB_CLK
	 */
	regmap_update_bits(regmap, 0x36004, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x3601C, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x37004, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x42004, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x42028, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x81004, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x2e004, BIT(0), BIT(0));

	ret = qcom_cc_register_rcg_dfs(regmap, gcc_dfs_clocks,
				       ARRAY_SIZE(gcc_dfs_clocks));
	if (ret)
		return ret;

	ret = qcom_cc_really_probe(pdev, &gcc_anorak_desc, regmap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register GCC clocks\n");
		return ret;
	}

	dev_info(&pdev->dev, "Registered GCC clocks\n");

	return ret;
}

static void gcc_anorak_sync_state(struct device *dev)
{
	qcom_cc_sync_state(dev, &gcc_anorak_desc);
}

static struct platform_driver gcc_anorak_driver = {
	.probe = gcc_anorak_probe,
	.driver = {
		.name = "gcc-anorak",
		.of_match_table = gcc_anorak_match_table,
		.sync_state = gcc_anorak_sync_state,
	},
};

static int __init gcc_anorak_init(void)
{
	return platform_driver_register(&gcc_anorak_driver);
}
subsys_initcall(gcc_anorak_init);

static void __exit gcc_anorak_exit(void)
{
	platform_driver_unregister(&gcc_anorak_driver);
}
module_exit(gcc_anorak_exit);

MODULE_DESCRIPTION("QTI GCC ANORAK Driver");
MODULE_LICENSE("GPL");
