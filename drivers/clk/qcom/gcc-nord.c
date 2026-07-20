// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,nord-gcc.h>

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
	DT_PCIE_A_PIPE_CLK,
	DT_PCIE_B_PIPE_CLK,
	DT_PCIE_C_PIPE_CLK,
	DT_PCIE_D_PIPE_CLK,
};

enum {
	P_BI_TCXO,
	P_GCC_GPLL0_OUT_EVEN,
	P_GCC_GPLL0_OUT_MAIN,
	P_PCIE_A_PIPE_CLK,
	P_PCIE_B_PIPE_CLK,
	P_PCIE_C_PIPE_CLK,
	P_PCIE_D_PIPE_CLK,
	P_SLEEP_CLK,
};

static struct clk_alpha_pll gcc_gpll0 = {
	.offset = 0x0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x9d020,
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
	{ P_SLEEP_CLK, 5 },
};

static const struct clk_parent_data gcc_parent_data_1[] = {
	{ .index = DT_BI_TCXO },
	{ .index = DT_SLEEP_CLK },
};

static const struct parent_map gcc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_SLEEP_CLK, 5 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_2[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .index = DT_SLEEP_CLK },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
};

static const struct clk_parent_data gcc_parent_data_3[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
};

static struct clk_regmap_phy_mux gcc_pcie_a_pipe_clk_src = {
	.reg = 0x49094,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_a_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_PCIE_A_PIPE_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_pcie_b_pipe_clk_src = {
	.reg = 0x4a094,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_b_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_PCIE_B_PIPE_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_pcie_c_pipe_clk_src = {
	.reg = 0x4b094,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_c_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_PCIE_C_PIPE_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_pcie_d_pipe_clk_src = {
	.reg = 0x4c094,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_d_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_PCIE_D_PIPE_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static const struct freq_tbl ftbl_gcc_gp1_clk_src[] = {
	F(66666667, P_GCC_GPLL0_OUT_MAIN, 9, 0, 0),
	F(100000000, P_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	F(200000000, P_GCC_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_gp1_clk_src = {
	.cmd_rcgr = 0x30004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp1_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_gp2_clk_src = {
	.cmd_rcgr = 0x31004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp2_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcie_a_aux_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie_a_aux_clk_src = {
	.cmd_rcgr = 0x49098,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_pcie_a_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_a_aux_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_a_phy_aux_clk_src = {
	.cmd_rcgr = 0x4d020,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_pcie_a_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_a_phy_aux_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcie_a_phy_rchng_clk_src[] = {
	F(66666667, P_GCC_GPLL0_OUT_MAIN, 9, 0, 0),
	F(100000000, P_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie_a_phy_rchng_clk_src = {
	.cmd_rcgr = 0x4907c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_a_phy_rchng_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_a_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_b_aux_clk_src = {
	.cmd_rcgr = 0x4a098,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_pcie_a_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_b_aux_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_b_phy_aux_clk_src = {
	.cmd_rcgr = 0x4e020,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_pcie_a_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_b_phy_aux_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_b_phy_rchng_clk_src = {
	.cmd_rcgr = 0x4a07c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_a_phy_rchng_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_b_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_c_aux_clk_src = {
	.cmd_rcgr = 0x4b098,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_pcie_a_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_c_aux_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_c_phy_aux_clk_src = {
	.cmd_rcgr = 0x4f020,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_pcie_a_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_c_phy_aux_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_c_phy_rchng_clk_src = {
	.cmd_rcgr = 0x4b07c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_pcie_a_phy_rchng_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_c_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_d_aux_clk_src = {
	.cmd_rcgr = 0x4c098,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_pcie_a_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_d_aux_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_d_phy_aux_clk_src = {
	.cmd_rcgr = 0x50020,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_pcie_a_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_d_phy_aux_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_d_phy_rchng_clk_src = {
	.cmd_rcgr = 0x4c07c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_pcie_a_phy_rchng_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_d_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_noc_refgen_clk_src = {
	.cmd_rcgr = 0x52094,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_a_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_noc_refgen_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_noc_safety_clk_src = {
	.cmd_rcgr = 0x520ac,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_a_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_noc_safety_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pdm2_clk_src[] = {
	F(40000000, P_GCC_GPLL0_OUT_MAIN, 15, 0, 0),
	F(60000000, P_GCC_GPLL0_OUT_MAIN, 10, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pdm2_clk_src = {
	.cmd_rcgr = 0x1a010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pdm2_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pdm2_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap3_qspi_ref_clk_src[] = {
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
	F(240000000, P_GCC_GPLL0_OUT_MAIN, 2.5, 0, 0),
	{ }
};

static struct clk_init_data gcc_qupv3_wrap3_qspi_ref_clk_src_init = {
	.name = "gcc_qupv3_wrap3_qspi_ref_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap3_qspi_ref_clk_src = {
	.cmd_rcgr = 0x23174,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap3_qspi_ref_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &gcc_qupv3_wrap3_qspi_ref_clk_src_init,
};

static struct clk_regmap_div gcc_qupv3_wrap3_s0_clk_src = {
	.reg = 0x2316c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_wrap3_s0_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_qupv3_wrap3_qspi_ref_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch gcc_boot_rom_ahb_clk = {
	.halt_reg = 0x1f004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1f004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_boot_rom_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp1_clk = {
	.halt_reg = 0x30000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x30000,
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
	.halt_reg = 0x31000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x31000,
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

static struct clk_branch gcc_mmu_0_tcu_vote_clk = {
	.halt_reg = 0x7d094,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d094,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_mmu_0_tcu_vote_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_a_aux_clk = {
	.halt_reg = 0x49058,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x49058,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d008,
		.enable_mask = BIT(14),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_a_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_a_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_a_cfg_ahb_clk = {
	.halt_reg = 0x49054,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x49054,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d008,
		.enable_mask = BIT(13),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_a_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_a_dti_qtc_clk = {
	.halt_reg = 0x49018,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x49018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d008,
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_a_dti_qtc_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_a_mstr_axi_clk = {
	.halt_reg = 0x49040,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x49040,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d008,
		.enable_mask = BIT(12),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_a_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_a_phy_aux_clk = {
	.halt_reg = 0x4d01c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9d010,
		.enable_mask = BIT(12),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_a_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_a_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_a_phy_rchng_clk = {
	.halt_reg = 0x49078,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x49078,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d008,
		.enable_mask = BIT(16),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_a_phy_rchng_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_a_phy_rchng_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_a_pipe_clk = {
	.halt_reg = 0x49068,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x49068,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d008,
		.enable_mask = BIT(15),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_a_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_a_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_a_slv_axi_clk = {
	.halt_reg = 0x4902c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x4902c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d008,
		.enable_mask = BIT(11),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_a_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_a_slv_q2a_axi_clk = {
	.halt_reg = 0x49024,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x49024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d008,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_a_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_b_aux_clk = {
	.halt_reg = 0x4a058,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9d008,
		.enable_mask = BIT(23),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_b_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_b_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_b_cfg_ahb_clk = {
	.halt_reg = 0x4a054,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x4a054,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d008,
		.enable_mask = BIT(22),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_b_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_b_dti_qtc_clk = {
	.halt_reg = 0x4a018,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x4a018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d008,
		.enable_mask = BIT(17),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_b_dti_qtc_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_b_mstr_axi_clk = {
	.halt_reg = 0x4a040,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x4a040,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d008,
		.enable_mask = BIT(21),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_b_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_b_phy_aux_clk = {
	.halt_reg = 0x4e01c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9d010,
		.enable_mask = BIT(13),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_b_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_b_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_b_phy_rchng_clk = {
	.halt_reg = 0x4a078,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9d008,
		.enable_mask = BIT(25),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_b_phy_rchng_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_b_phy_rchng_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_b_pipe_clk = {
	.halt_reg = 0x4a068,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9d008,
		.enable_mask = BIT(24),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_b_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_b_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_b_slv_axi_clk = {
	.halt_reg = 0x4a02c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x4a02c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d008,
		.enable_mask = BIT(20),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_b_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_b_slv_q2a_axi_clk = {
	.halt_reg = 0x4a024,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9d008,
		.enable_mask = BIT(19),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_b_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_c_aux_clk = {
	.halt_reg = 0x4b058,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9d010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_c_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_c_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_c_cfg_ahb_clk = {
	.halt_reg = 0x4b054,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x4b054,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d008,
		.enable_mask = BIT(31),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_c_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_c_dti_qtc_clk = {
	.halt_reg = 0x4b018,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x4b018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d008,
		.enable_mask = BIT(26),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_c_dti_qtc_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_c_mstr_axi_clk = {
	.halt_reg = 0x4b040,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x4b040,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d008,
		.enable_mask = BIT(30),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_c_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_c_phy_aux_clk = {
	.halt_reg = 0x4f01c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9d010,
		.enable_mask = BIT(14),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_c_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_c_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_c_phy_rchng_clk = {
	.halt_reg = 0x4b078,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9d010,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_c_phy_rchng_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_c_phy_rchng_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_c_pipe_clk = {
	.halt_reg = 0x4b068,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9d010,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_c_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_c_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_c_slv_axi_clk = {
	.halt_reg = 0x4b02c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x4b02c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d008,
		.enable_mask = BIT(29),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_c_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_c_slv_q2a_axi_clk = {
	.halt_reg = 0x4b024,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9d008,
		.enable_mask = BIT(28),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_c_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_d_aux_clk = {
	.halt_reg = 0x4c058,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9d010,
		.enable_mask = BIT(9),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_d_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_d_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_d_cfg_ahb_clk = {
	.halt_reg = 0x4c054,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x4c054,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d010,
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_d_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_d_dti_qtc_clk = {
	.halt_reg = 0x4c018,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x4c018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d010,
		.enable_mask = BIT(3),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_d_dti_qtc_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_d_mstr_axi_clk = {
	.halt_reg = 0x4c040,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x4c040,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d010,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_d_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_d_phy_aux_clk = {
	.halt_reg = 0x5001c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9d010,
		.enable_mask = BIT(16),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_d_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_d_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_d_phy_rchng_clk = {
	.halt_reg = 0x4c078,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9d010,
		.enable_mask = BIT(11),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_d_phy_rchng_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_d_phy_rchng_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_d_pipe_clk = {
	.halt_reg = 0x4c068,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9d010,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_d_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_d_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_d_slv_axi_clk = {
	.halt_reg = 0x4c02c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x4c02c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d010,
		.enable_mask = BIT(6),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_d_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_d_slv_q2a_axi_clk = {
	.halt_reg = 0x4c024,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9d010,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_d_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_link_ahb_clk = {
	.halt_reg = 0x52464,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x52464,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_link_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_link_xo_clk = {
	.halt_reg = 0x52468,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x52468,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52468,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_link_xo_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_noc_async_bridge_clk = {
	.halt_reg = 0x52048,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x52048,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d018,
		.enable_mask = BIT(18),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_noc_async_bridge_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_noc_cnoc_sf_qx_clk = {
	.halt_reg = 0x52040,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x52040,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d010,
		.enable_mask = BIT(24),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_noc_cnoc_sf_qx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_noc_m_cfg_clk = {
	.halt_reg = 0x52060,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x52060,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d018,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_noc_m_cfg_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_noc_m_pdb_clk = {
	.halt_reg = 0x52084,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x52084,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d018,
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_noc_m_pdb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_noc_mstr_axi_clk = {
	.halt_reg = 0x52050,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x52050,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d010,
		.enable_mask = BIT(25),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_noc_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_noc_pwrctl_clk = {
	.halt_reg = 0x52080,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9d018,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_noc_pwrctl_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_noc_qosgen_extref_clk = {
	.halt_reg = 0x52074,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9d010,
		.enable_mask = BIT(19),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_noc_qosgen_extref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_noc_refgen_clk = {
	.halt_reg = 0x52078,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x52078,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_noc_refgen_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_noc_refgen_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_noc_s_cfg_clk = {
	.halt_reg = 0x52064,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9d018,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_noc_s_cfg_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_noc_s_pdb_clk = {
	.halt_reg = 0x5208c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x5208c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d018,
		.enable_mask = BIT(9),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_noc_s_pdb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_noc_safety_clk = {
	.halt_reg = 0x5207c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5207c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_noc_safety_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_noc_safety_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_noc_slave_axi_clk = {
	.halt_reg = 0x52058,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x52058,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d010,
		.enable_mask = BIT(26),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_noc_slave_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_noc_tsctr_clk = {
	.halt_reg = 0x52070,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9d010,
		.enable_mask = BIT(18),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_noc_tsctr_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_noc_xo_clk = {
	.halt_reg = 0x52068,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9d018,
		.enable_mask = BIT(6),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_noc_xo_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm2_clk = {
	.halt_reg = 0x1a00c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1a00c,
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
	.halt_reg = 0x1a004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x1a004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1a004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pdm_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm_xo4_clk = {
	.halt_reg = 0x1a008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1a008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pdm_xo4_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap3_core_2x_clk = {
	.halt_reg = 0x23020,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9d000,
		.enable_mask = BIT(24),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap3_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap3_core_clk = {
	.halt_reg = 0x2300c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9d000,
		.enable_mask = BIT(23),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap3_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap3_m_clk = {
	.halt_reg = 0x23004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x23004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d000,
		.enable_mask = BIT(22),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap3_m_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap3_qspi_ref_clk = {
	.halt_reg = 0x23170,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9d000,
		.enable_mask = BIT(26),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap3_qspi_ref_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap3_qspi_ref_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap3_s0_clk = {
	.halt_reg = 0x2315c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9d000,
		.enable_mask = BIT(25),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap3_s0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap3_s0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap3_s_ahb_clk = {
	.halt_reg = 0x23008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x23008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9d010,
		.enable_mask = BIT(15),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap3_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_smmu_pcie_qtc_vote_clk = {
	.halt_reg = 0x7d0b8,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d0b8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_smmu_pcie_qtc_vote_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc gcc_pcie_a_gdsc = {
	.gdscr = 0x49004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.collapse_ctrl = 0x8d02c,
	.collapse_mask = BIT(1),
	.pd = {
		.name = "gcc_pcie_a_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE | VOTABLE,
};

static struct gdsc gcc_pcie_a_phy_gdsc = {
	.gdscr = 0x4d004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.collapse_ctrl = 0x8d02c,
	.collapse_mask = BIT(5),
	.pd = {
		.name = "gcc_pcie_a_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE | VOTABLE,
};

static struct gdsc gcc_pcie_b_gdsc = {
	.gdscr = 0x4a004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.collapse_ctrl = 0x8d02c,
	.collapse_mask = BIT(2),
	.pd = {
		.name = "gcc_pcie_b_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE | VOTABLE,
};

static struct gdsc gcc_pcie_b_phy_gdsc = {
	.gdscr = 0x4e004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.collapse_ctrl = 0x8d02c,
	.collapse_mask = BIT(6),
	.pd = {
		.name = "gcc_pcie_b_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE | VOTABLE,
};

static struct gdsc gcc_pcie_c_gdsc = {
	.gdscr = 0x4b004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.collapse_ctrl = 0x8d02c,
	.collapse_mask = BIT(3),
	.pd = {
		.name = "gcc_pcie_c_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE | VOTABLE,
};

static struct gdsc gcc_pcie_c_phy_gdsc = {
	.gdscr = 0x4f004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.collapse_ctrl = 0x8d02c,
	.collapse_mask = BIT(7),
	.pd = {
		.name = "gcc_pcie_c_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE | VOTABLE,
};

static struct gdsc gcc_pcie_d_gdsc = {
	.gdscr = 0x4c004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.collapse_ctrl = 0x8d02c,
	.collapse_mask = BIT(4),
	.pd = {
		.name = "gcc_pcie_d_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE | VOTABLE,
};

static struct gdsc gcc_pcie_d_phy_gdsc = {
	.gdscr = 0x50004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.collapse_ctrl = 0x8d02c,
	.collapse_mask = BIT(8),
	.pd = {
		.name = "gcc_pcie_d_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE | VOTABLE,
};

static struct gdsc gcc_pcie_noc_gdsc = {
	.gdscr = 0x52004,
	.gds_hw_ctrl = 0x52018,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.collapse_ctrl = 0x8d02c,
	.collapse_mask = BIT(0),
	.pd = {
		.name = "gcc_pcie_noc_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE | VOTABLE,
};

static struct clk_regmap *gcc_nord_clocks[] = {
	[GCC_BOOT_ROM_AHB_CLK] = &gcc_boot_rom_ahb_clk.clkr,
	[GCC_GP1_CLK] = &gcc_gp1_clk.clkr,
	[GCC_GP1_CLK_SRC] = &gcc_gp1_clk_src.clkr,
	[GCC_GP2_CLK] = &gcc_gp2_clk.clkr,
	[GCC_GP2_CLK_SRC] = &gcc_gp2_clk_src.clkr,
	[GCC_GPLL0] = &gcc_gpll0.clkr,
	[GCC_GPLL0_OUT_EVEN] = &gcc_gpll0_out_even.clkr,
	[GCC_MMU_0_TCU_VOTE_CLK] = &gcc_mmu_0_tcu_vote_clk.clkr,
	[GCC_PCIE_A_AUX_CLK] = &gcc_pcie_a_aux_clk.clkr,
	[GCC_PCIE_A_AUX_CLK_SRC] = &gcc_pcie_a_aux_clk_src.clkr,
	[GCC_PCIE_A_CFG_AHB_CLK] = &gcc_pcie_a_cfg_ahb_clk.clkr,
	[GCC_PCIE_A_DTI_QTC_CLK] = &gcc_pcie_a_dti_qtc_clk.clkr,
	[GCC_PCIE_A_MSTR_AXI_CLK] = &gcc_pcie_a_mstr_axi_clk.clkr,
	[GCC_PCIE_A_PHY_AUX_CLK] = &gcc_pcie_a_phy_aux_clk.clkr,
	[GCC_PCIE_A_PHY_AUX_CLK_SRC] = &gcc_pcie_a_phy_aux_clk_src.clkr,
	[GCC_PCIE_A_PHY_RCHNG_CLK] = &gcc_pcie_a_phy_rchng_clk.clkr,
	[GCC_PCIE_A_PHY_RCHNG_CLK_SRC] = &gcc_pcie_a_phy_rchng_clk_src.clkr,
	[GCC_PCIE_A_PIPE_CLK] = &gcc_pcie_a_pipe_clk.clkr,
	[GCC_PCIE_A_PIPE_CLK_SRC] = &gcc_pcie_a_pipe_clk_src.clkr,
	[GCC_PCIE_A_SLV_AXI_CLK] = &gcc_pcie_a_slv_axi_clk.clkr,
	[GCC_PCIE_A_SLV_Q2A_AXI_CLK] = &gcc_pcie_a_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_B_AUX_CLK] = &gcc_pcie_b_aux_clk.clkr,
	[GCC_PCIE_B_AUX_CLK_SRC] = &gcc_pcie_b_aux_clk_src.clkr,
	[GCC_PCIE_B_CFG_AHB_CLK] = &gcc_pcie_b_cfg_ahb_clk.clkr,
	[GCC_PCIE_B_DTI_QTC_CLK] = &gcc_pcie_b_dti_qtc_clk.clkr,
	[GCC_PCIE_B_MSTR_AXI_CLK] = &gcc_pcie_b_mstr_axi_clk.clkr,
	[GCC_PCIE_B_PHY_AUX_CLK] = &gcc_pcie_b_phy_aux_clk.clkr,
	[GCC_PCIE_B_PHY_AUX_CLK_SRC] = &gcc_pcie_b_phy_aux_clk_src.clkr,
	[GCC_PCIE_B_PHY_RCHNG_CLK] = &gcc_pcie_b_phy_rchng_clk.clkr,
	[GCC_PCIE_B_PHY_RCHNG_CLK_SRC] = &gcc_pcie_b_phy_rchng_clk_src.clkr,
	[GCC_PCIE_B_PIPE_CLK] = &gcc_pcie_b_pipe_clk.clkr,
	[GCC_PCIE_B_PIPE_CLK_SRC] = &gcc_pcie_b_pipe_clk_src.clkr,
	[GCC_PCIE_B_SLV_AXI_CLK] = &gcc_pcie_b_slv_axi_clk.clkr,
	[GCC_PCIE_B_SLV_Q2A_AXI_CLK] = &gcc_pcie_b_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_C_AUX_CLK] = &gcc_pcie_c_aux_clk.clkr,
	[GCC_PCIE_C_AUX_CLK_SRC] = &gcc_pcie_c_aux_clk_src.clkr,
	[GCC_PCIE_C_CFG_AHB_CLK] = &gcc_pcie_c_cfg_ahb_clk.clkr,
	[GCC_PCIE_C_DTI_QTC_CLK] = &gcc_pcie_c_dti_qtc_clk.clkr,
	[GCC_PCIE_C_MSTR_AXI_CLK] = &gcc_pcie_c_mstr_axi_clk.clkr,
	[GCC_PCIE_C_PHY_AUX_CLK] = &gcc_pcie_c_phy_aux_clk.clkr,
	[GCC_PCIE_C_PHY_AUX_CLK_SRC] = &gcc_pcie_c_phy_aux_clk_src.clkr,
	[GCC_PCIE_C_PHY_RCHNG_CLK] = &gcc_pcie_c_phy_rchng_clk.clkr,
	[GCC_PCIE_C_PHY_RCHNG_CLK_SRC] = &gcc_pcie_c_phy_rchng_clk_src.clkr,
	[GCC_PCIE_C_PIPE_CLK] = &gcc_pcie_c_pipe_clk.clkr,
	[GCC_PCIE_C_PIPE_CLK_SRC] = &gcc_pcie_c_pipe_clk_src.clkr,
	[GCC_PCIE_C_SLV_AXI_CLK] = &gcc_pcie_c_slv_axi_clk.clkr,
	[GCC_PCIE_C_SLV_Q2A_AXI_CLK] = &gcc_pcie_c_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_D_AUX_CLK] = &gcc_pcie_d_aux_clk.clkr,
	[GCC_PCIE_D_AUX_CLK_SRC] = &gcc_pcie_d_aux_clk_src.clkr,
	[GCC_PCIE_D_CFG_AHB_CLK] = &gcc_pcie_d_cfg_ahb_clk.clkr,
	[GCC_PCIE_D_DTI_QTC_CLK] = &gcc_pcie_d_dti_qtc_clk.clkr,
	[GCC_PCIE_D_MSTR_AXI_CLK] = &gcc_pcie_d_mstr_axi_clk.clkr,
	[GCC_PCIE_D_PHY_AUX_CLK] = &gcc_pcie_d_phy_aux_clk.clkr,
	[GCC_PCIE_D_PHY_AUX_CLK_SRC] = &gcc_pcie_d_phy_aux_clk_src.clkr,
	[GCC_PCIE_D_PHY_RCHNG_CLK] = &gcc_pcie_d_phy_rchng_clk.clkr,
	[GCC_PCIE_D_PHY_RCHNG_CLK_SRC] = &gcc_pcie_d_phy_rchng_clk_src.clkr,
	[GCC_PCIE_D_PIPE_CLK] = &gcc_pcie_d_pipe_clk.clkr,
	[GCC_PCIE_D_PIPE_CLK_SRC] = &gcc_pcie_d_pipe_clk_src.clkr,
	[GCC_PCIE_D_SLV_AXI_CLK] = &gcc_pcie_d_slv_axi_clk.clkr,
	[GCC_PCIE_D_SLV_Q2A_AXI_CLK] = &gcc_pcie_d_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_LINK_AHB_CLK] = &gcc_pcie_link_ahb_clk.clkr,
	[GCC_PCIE_LINK_XO_CLK] = &gcc_pcie_link_xo_clk.clkr,
	[GCC_PCIE_NOC_ASYNC_BRIDGE_CLK] = &gcc_pcie_noc_async_bridge_clk.clkr,
	[GCC_PCIE_NOC_CNOC_SF_QX_CLK] = &gcc_pcie_noc_cnoc_sf_qx_clk.clkr,
	[GCC_PCIE_NOC_M_CFG_CLK] = &gcc_pcie_noc_m_cfg_clk.clkr,
	[GCC_PCIE_NOC_M_PDB_CLK] = &gcc_pcie_noc_m_pdb_clk.clkr,
	[GCC_PCIE_NOC_MSTR_AXI_CLK] = &gcc_pcie_noc_mstr_axi_clk.clkr,
	[GCC_PCIE_NOC_PWRCTL_CLK] = &gcc_pcie_noc_pwrctl_clk.clkr,
	[GCC_PCIE_NOC_QOSGEN_EXTREF_CLK] = &gcc_pcie_noc_qosgen_extref_clk.clkr,
	[GCC_PCIE_NOC_REFGEN_CLK] = &gcc_pcie_noc_refgen_clk.clkr,
	[GCC_PCIE_NOC_REFGEN_CLK_SRC] = &gcc_pcie_noc_refgen_clk_src.clkr,
	[GCC_PCIE_NOC_S_CFG_CLK] = &gcc_pcie_noc_s_cfg_clk.clkr,
	[GCC_PCIE_NOC_S_PDB_CLK] = &gcc_pcie_noc_s_pdb_clk.clkr,
	[GCC_PCIE_NOC_SAFETY_CLK] = &gcc_pcie_noc_safety_clk.clkr,
	[GCC_PCIE_NOC_SAFETY_CLK_SRC] = &gcc_pcie_noc_safety_clk_src.clkr,
	[GCC_PCIE_NOC_SLAVE_AXI_CLK] = &gcc_pcie_noc_slave_axi_clk.clkr,
	[GCC_PCIE_NOC_TSCTR_CLK] = &gcc_pcie_noc_tsctr_clk.clkr,
	[GCC_PCIE_NOC_XO_CLK] = &gcc_pcie_noc_xo_clk.clkr,
	[GCC_PDM2_CLK] = &gcc_pdm2_clk.clkr,
	[GCC_PDM2_CLK_SRC] = &gcc_pdm2_clk_src.clkr,
	[GCC_PDM_AHB_CLK] = &gcc_pdm_ahb_clk.clkr,
	[GCC_PDM_XO4_CLK] = &gcc_pdm_xo4_clk.clkr,
	[GCC_QUPV3_WRAP3_CORE_2X_CLK] = &gcc_qupv3_wrap3_core_2x_clk.clkr,
	[GCC_QUPV3_WRAP3_CORE_CLK] = &gcc_qupv3_wrap3_core_clk.clkr,
	[GCC_QUPV3_WRAP3_M_CLK] = &gcc_qupv3_wrap3_m_clk.clkr,
	[GCC_QUPV3_WRAP3_QSPI_REF_CLK] = &gcc_qupv3_wrap3_qspi_ref_clk.clkr,
	[GCC_QUPV3_WRAP3_QSPI_REF_CLK_SRC] = &gcc_qupv3_wrap3_qspi_ref_clk_src.clkr,
	[GCC_QUPV3_WRAP3_S0_CLK] = &gcc_qupv3_wrap3_s0_clk.clkr,
	[GCC_QUPV3_WRAP3_S0_CLK_SRC] = &gcc_qupv3_wrap3_s0_clk_src.clkr,
	[GCC_QUPV3_WRAP3_S_AHB_CLK] = &gcc_qupv3_wrap3_s_ahb_clk.clkr,
	[GCC_SMMU_PCIE_QTC_VOTE_CLK] = &gcc_smmu_pcie_qtc_vote_clk.clkr,
};

static struct gdsc *gcc_nord_gdscs[] = {
	[GCC_PCIE_A_GDSC] = &gcc_pcie_a_gdsc,
	[GCC_PCIE_A_PHY_GDSC] = &gcc_pcie_a_phy_gdsc,
	[GCC_PCIE_B_GDSC] = &gcc_pcie_b_gdsc,
	[GCC_PCIE_B_PHY_GDSC] = &gcc_pcie_b_phy_gdsc,
	[GCC_PCIE_C_GDSC] = &gcc_pcie_c_gdsc,
	[GCC_PCIE_C_PHY_GDSC] = &gcc_pcie_c_phy_gdsc,
	[GCC_PCIE_D_GDSC] = &gcc_pcie_d_gdsc,
	[GCC_PCIE_D_PHY_GDSC] = &gcc_pcie_d_phy_gdsc,
	[GCC_PCIE_NOC_GDSC] = &gcc_pcie_noc_gdsc,
};

static const struct qcom_reset_map gcc_nord_resets[] = {
	[GCC_PCIE_A_BCR] = { 0x49000 },
	[GCC_PCIE_A_LINK_DOWN_BCR] = { 0xb9000 },
	[GCC_PCIE_A_NOCSR_COM_PHY_BCR] = { 0xb900c },
	[GCC_PCIE_A_PHY_BCR] = { 0x4d000 },
	[GCC_PCIE_A_PHY_CFG_AHB_BCR] = { 0xb9014 },
	[GCC_PCIE_A_PHY_COM_BCR] = { 0xb9018 },
	[GCC_PCIE_A_PHY_NOCSR_COM_PHY_BCR] = { 0xb9010 },
	[GCC_PCIE_B_BCR] = { 0x4a000 },
	[GCC_PCIE_B_LINK_DOWN_BCR] = { 0xba000 },
	[GCC_PCIE_B_NOCSR_COM_PHY_BCR] = { 0xba008 },
	[GCC_PCIE_B_PHY_BCR] = { 0x4e000 },
	[GCC_PCIE_B_PHY_CFG_AHB_BCR] = { 0xba010 },
	[GCC_PCIE_B_PHY_COM_BCR] = { 0xba014 },
	[GCC_PCIE_B_PHY_NOCSR_COM_PHY_BCR] = { 0xba00c },
	[GCC_PCIE_C_BCR] = { 0x4b000 },
	[GCC_PCIE_C_LINK_DOWN_BCR] = { 0xbb07c },
	[GCC_PCIE_C_NOCSR_COM_PHY_BCR] = { 0xbb084 },
	[GCC_PCIE_C_PHY_BCR] = { 0x4f000 },
	[GCC_PCIE_C_PHY_CFG_AHB_BCR] = { 0xbb08c },
	[GCC_PCIE_C_PHY_COM_BCR] = { 0xbb090 },
	[GCC_PCIE_C_PHY_NOCSR_COM_PHY_BCR] = { 0xbb088 },
	[GCC_PCIE_D_BCR] = { 0x4c000 },
	[GCC_PCIE_D_LINK_DOWN_BCR] = { 0xbc000 },
	[GCC_PCIE_D_NOCSR_COM_PHY_BCR] = { 0xbc008 },
	[GCC_PCIE_D_PHY_BCR] = { 0x50000 },
	[GCC_PCIE_D_PHY_CFG_AHB_BCR] = { 0xbc010 },
	[GCC_PCIE_D_PHY_COM_BCR] = { 0xbc014 },
	[GCC_PCIE_D_PHY_NOCSR_COM_PHY_BCR] = { 0xbc00c },
	[GCC_PCIE_NOC_BCR] = { 0x52000 },
	[GCC_PDM_BCR] = { 0x1a000 },
	[GCC_QUPV3_WRAPPER_3_BCR] = { 0x23000 },
	[GCC_TCSR_PCIE_BCR] = { 0xb901c },
};

static const struct clk_rcg_dfs_data gcc_nord_dfs_clocks[] = {
	DEFINE_RCG_DFS(gcc_qupv3_wrap3_qspi_ref_clk_src),
};

static const struct regmap_config gcc_nord_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x1f41f0,
	.fast_io = true,
};

static const struct qcom_cc_driver_data gcc_nord_driver_data = {
	.dfs_rcgs = gcc_nord_dfs_clocks,
	.num_dfs_rcgs = ARRAY_SIZE(gcc_nord_dfs_clocks),
};

static const struct qcom_cc_desc gcc_nord_desc = {
	.config = &gcc_nord_regmap_config,
	.clks = gcc_nord_clocks,
	.num_clks = ARRAY_SIZE(gcc_nord_clocks),
	.resets = gcc_nord_resets,
	.num_resets = ARRAY_SIZE(gcc_nord_resets),
	.gdscs = gcc_nord_gdscs,
	.num_gdscs = ARRAY_SIZE(gcc_nord_gdscs),
	.use_rpm = true,
	.driver_data = &gcc_nord_driver_data,
};

static const struct of_device_id gcc_nord_match_table[] = {
	{ .compatible = "qcom,nord-gcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_nord_match_table);

static int gcc_nord_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &gcc_nord_desc);
}

static struct platform_driver gcc_nord_driver = {
	.probe = gcc_nord_probe,
	.driver = {
		.name = "gcc-nord",
		.of_match_table = gcc_nord_match_table,
	},
};

static int __init gcc_nord_init(void)
{
	return platform_driver_register(&gcc_nord_driver);
}
subsys_initcall(gcc_nord_init);

static void __exit gcc_nord_exit(void)
{
	platform_driver_unregister(&gcc_nord_driver);
}
module_exit(gcc_nord_exit);

MODULE_DESCRIPTION("QTI GCC NORD Driver");
MODULE_LICENSE("GPL");
