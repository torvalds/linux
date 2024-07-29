// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

#include <dt-bindings/clock/qcom,gcc-sm8150.h>

#include "common.h"
#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "reset.h"
#include "gdsc.h"

enum {
	P_BI_TCXO,
	P_AUD_REF_CLK,
	P_GPLL0_OUT_EVEN,
	P_GPLL0_OUT_MAIN,
	P_GPLL7_OUT_MAIN,
	P_GPLL9_OUT_MAIN,
	P_SLEEP_CLK,
};

static struct clk_alpha_pll gpll0 = {
	.offset = 0x0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TRION],
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpll0",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
				.name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_trion_ops,
		},
	},
};

static const struct clk_div_table post_div_table_trion_even[] = {
	{ 0x0, 1 },
	{ 0x1, 2 },
	{ 0x3, 4 },
	{ 0x7, 8 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll0_out_even = {
	.offset = 0x0,
	.post_div_shift = 8,
	.post_div_table = post_div_table_trion_even,
	.num_post_div = ARRAY_SIZE(post_div_table_trion_even),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TRION],
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll0_out_even",
		.parent_hws = (const struct clk_hw*[]){
			&gpll0.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_trion_ops,
	},
};

static struct clk_alpha_pll gpll7 = {
	.offset = 0x1a000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TRION],
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(7),
		.hw.init = &(struct clk_init_data){
			.name = "gpll7",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
				.name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_trion_ops,
		},
	},
};

static struct clk_alpha_pll gpll9 = {
	.offset = 0x1c000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TRION],
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gpll9",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
				.name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_trion_ops,
		},
	},
};

static const struct parent_map gcc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parents_0[] = {
	{ .fw_name = "bi_tcxo", .name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_SLEEP_CLK, 5 },
	{ P_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parents_1[] = {
	{ .fw_name = "bi_tcxo", .name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .fw_name = "sleep_clk", .name = "sleep_clk" },
	{ .hw = &gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_SLEEP_CLK, 5 },
};

static const struct clk_parent_data gcc_parents_2[] = {
	{ .fw_name = "bi_tcxo", .name = "bi_tcxo" },
	{ .fw_name = "sleep_clk", .name = "sleep_clk" },
};

static const struct parent_map gcc_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
};

static const struct clk_parent_data gcc_parents_3[] = {
	{ .fw_name = "bi_tcxo", .name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
};

static const struct parent_map gcc_parent_map_4[] = {
	{ P_BI_TCXO, 0 },
};

static const struct clk_parent_data gcc_parents_4[] = {
	{ .fw_name = "bi_tcxo", .name = "bi_tcxo" },
};

static const struct parent_map gcc_parent_map_5[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL7_OUT_MAIN, 3 },
	{ P_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parents_5[] = {
	{ .fw_name = "bi_tcxo", .name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll7.clkr.hw },
	{ .hw = &gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_6[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL9_OUT_MAIN, 2 },
	{ P_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parents_6[] = {
	{ .fw_name = "bi_tcxo", .name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll9.clkr.hw },
	{ .hw = &gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_7[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_AUD_REF_CLK, 2 },
	{ P_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parents_7[] = {
	{ .fw_name = "bi_tcxo", .name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .fw_name = "aud_ref_clk", .name = "aud_ref_clk" },
	{ .hw = &gpll0_out_even.clkr.hw },
};

static const struct freq_tbl ftbl_gcc_emac_ptp_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(50000000, P_GPLL0_OUT_EVEN, 6, 0, 0),
	F(125000000, P_GPLL7_OUT_MAIN, 4, 0, 0),
	F(250000000, P_GPLL7_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_emac_ptp_clk_src = {
	.cmd_rcgr = 0x6038,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_emac_ptp_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_emac_ptp_clk_src",
		.parent_data = gcc_parents_5,
		.num_parents = ARRAY_SIZE(gcc_parents_5),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_emac_rgmii_clk_src[] = {
	F(2500000, P_BI_TCXO, 1, 25, 192),
	F(5000000, P_BI_TCXO, 1, 25, 96),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(25000000, P_GPLL0_OUT_EVEN, 12, 0, 0),
	F(50000000, P_GPLL0_OUT_EVEN, 6, 0, 0),
	F(125000000, P_GPLL7_OUT_MAIN, 4, 0, 0),
	F(250000000, P_GPLL7_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_emac_rgmii_clk_src = {
	.cmd_rcgr = 0x601c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_emac_rgmii_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_emac_rgmii_clk_src",
		.parent_data = gcc_parents_5,
		.num_parents = ARRAY_SIZE(gcc_parents_5),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_gp1_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(25000000, P_GPLL0_OUT_EVEN, 12, 0, 0),
	F(50000000, P_GPLL0_OUT_EVEN, 6, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 6, 0, 0),
	F(200000000, P_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_gp1_clk_src = {
	.cmd_rcgr = 0x64004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_gp1_clk_src",
		.parent_data = gcc_parents_1,
		.num_parents = ARRAY_SIZE(gcc_parents_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_gp2_clk_src = {
	.cmd_rcgr = 0x65004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_gp2_clk_src",
		.parent_data = gcc_parents_1,
		.num_parents = ARRAY_SIZE(gcc_parents_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_gp3_clk_src = {
	.cmd_rcgr = 0x66004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_gp3_clk_src",
		.parent_data = gcc_parents_1,
		.num_parents = ARRAY_SIZE(gcc_parents_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcie_0_aux_clk_src[] = {
	F(9600000, P_BI_TCXO, 2, 0, 0),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie_0_aux_clk_src = {
	.cmd_rcgr = 0x6b02c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_pcie_0_aux_clk_src",
		.parent_data = gcc_parents_2,
		.num_parents = ARRAY_SIZE(gcc_parents_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_pcie_1_aux_clk_src = {
	.cmd_rcgr = 0x8d02c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_pcie_1_aux_clk_src",
		.parent_data = gcc_parents_2,
		.num_parents = ARRAY_SIZE(gcc_parents_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcie_phy_refgen_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 6, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie_phy_refgen_clk_src = {
	.cmd_rcgr = 0x6f014,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_phy_refgen_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_pcie_phy_refgen_clk_src",
		.parent_data = gcc_parents_0,
		.num_parents = ARRAY_SIZE(gcc_parents_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pdm2_clk_src[] = {
	F(9600000, P_BI_TCXO, 2, 0, 0),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(60000000, P_GPLL0_OUT_MAIN, 10, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pdm2_clk_src = {
	.cmd_rcgr = 0x33010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pdm2_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_pdm2_clk_src",
		.parent_data = gcc_parents_0,
		.num_parents = ARRAY_SIZE(gcc_parents_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_qspi_core_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(75000000, P_GPLL0_OUT_EVEN, 4, 0, 0),
	F(150000000, P_GPLL0_OUT_MAIN, 4, 0, 0),
	F(300000000, P_GPLL0_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_qspi_core_clk_src = {
	.cmd_rcgr = 0x4b008,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qspi_core_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_qspi_core_clk_src",
		.parent_data = gcc_parents_0,
		.num_parents = ARRAY_SIZE(gcc_parents_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
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
	F(80000000, P_GPLL0_OUT_EVEN, 1, 4, 15),
	F(96000000, P_GPLL0_OUT_EVEN, 1, 8, 25),
	F(100000000, P_GPLL0_OUT_EVEN, 3, 0, 0),
	F(102400000, P_GPLL0_OUT_EVEN, 1, 128, 375),
	F(112000000, P_GPLL0_OUT_EVEN, 1, 28, 75),
	F(117964800, P_GPLL0_OUT_EVEN, 1, 6144, 15625),
	F(120000000, P_GPLL0_OUT_EVEN, 2.5, 0, 0),
	F(128000000, P_GPLL0_OUT_MAIN, 1, 16, 75),
	{ }
};

static struct clk_init_data gcc_qupv3_wrap0_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s0_clk_src",
	.parent_data = gcc_parents_0,
	.num_parents = ARRAY_SIZE(gcc_parents_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s0_clk_src = {
	.cmd_rcgr = 0x17148,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s0_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s1_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s1_clk_src",
	.parent_data = gcc_parents_0,
	.num_parents = ARRAY_SIZE(gcc_parents_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s1_clk_src = {
	.cmd_rcgr = 0x17278,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s1_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s2_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s2_clk_src",
	.parent_data = gcc_parents_0,
	.num_parents = ARRAY_SIZE(gcc_parents_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s2_clk_src = {
	.cmd_rcgr = 0x173a8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s2_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s3_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s3_clk_src",
	.parent_data = gcc_parents_0,
	.num_parents = ARRAY_SIZE(gcc_parents_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s3_clk_src = {
	.cmd_rcgr = 0x174d8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s3_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s4_clk_src",
	.parent_data = gcc_parents_0,
	.num_parents = ARRAY_SIZE(gcc_parents_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s4_clk_src = {
	.cmd_rcgr = 0x17608,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s4_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s5_clk_src",
	.parent_data = gcc_parents_0,
	.num_parents = ARRAY_SIZE(gcc_parents_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s5_clk_src = {
	.cmd_rcgr = 0x17738,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s5_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s6_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s6_clk_src",
	.parent_data = gcc_parents_0,
	.num_parents = ARRAY_SIZE(gcc_parents_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s6_clk_src = {
	.cmd_rcgr = 0x17868,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s6_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s7_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s7_clk_src",
	.parent_data = gcc_parents_0,
	.num_parents = ARRAY_SIZE(gcc_parents_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s7_clk_src = {
	.cmd_rcgr = 0x17998,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s7_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s0_clk_src",
	.parent_data = gcc_parents_0,
	.num_parents = ARRAY_SIZE(gcc_parents_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s0_clk_src = {
	.cmd_rcgr = 0x18148,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s0_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s1_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s1_clk_src",
	.parent_data = gcc_parents_0,
	.num_parents = ARRAY_SIZE(gcc_parents_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s1_clk_src = {
	.cmd_rcgr = 0x18278,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s1_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s2_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s2_clk_src",
	.parent_data = gcc_parents_0,
	.num_parents = ARRAY_SIZE(gcc_parents_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s2_clk_src = {
	.cmd_rcgr = 0x183a8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s2_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s3_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s3_clk_src",
	.parent_data = gcc_parents_0,
	.num_parents = ARRAY_SIZE(gcc_parents_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s3_clk_src = {
	.cmd_rcgr = 0x184d8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s3_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s4_clk_src",
	.parent_data = gcc_parents_0,
	.num_parents = ARRAY_SIZE(gcc_parents_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s4_clk_src = {
	.cmd_rcgr = 0x18608,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s4_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s5_clk_src",
	.parent_data = gcc_parents_0,
	.num_parents = ARRAY_SIZE(gcc_parents_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s5_clk_src = {
	.cmd_rcgr = 0x18738,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s5_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s0_clk_src",
	.parent_data = gcc_parents_0,
	.num_parents = ARRAY_SIZE(gcc_parents_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s0_clk_src = {
	.cmd_rcgr = 0x1e148,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_s0_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_s1_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s1_clk_src",
	.parent_data = gcc_parents_0,
	.num_parents = ARRAY_SIZE(gcc_parents_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s1_clk_src = {
	.cmd_rcgr = 0x1e278,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_s1_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_s2_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s2_clk_src",
	.parent_data = gcc_parents_0,
	.num_parents = ARRAY_SIZE(gcc_parents_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s2_clk_src = {
	.cmd_rcgr = 0x1e3a8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_s2_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_s3_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s3_clk_src",
	.parent_data = gcc_parents_0,
	.num_parents = ARRAY_SIZE(gcc_parents_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s3_clk_src = {
	.cmd_rcgr = 0x1e4d8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_s3_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s4_clk_src",
	.parent_data = gcc_parents_0,
	.num_parents = ARRAY_SIZE(gcc_parents_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s4_clk_src = {
	.cmd_rcgr = 0x1e608,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_s4_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s5_clk_src",
	.parent_data = gcc_parents_0,
	.num_parents = ARRAY_SIZE(gcc_parents_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s5_clk_src = {
	.cmd_rcgr = 0x1e738,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_s5_clk_src_init,
};

static const struct freq_tbl ftbl_gcc_sdcc2_apps_clk_src[] = {
	F(400000, P_BI_TCXO, 12, 1, 4),
	F(9600000, P_BI_TCXO, 2, 0, 0),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(25000000, P_GPLL0_OUT_MAIN, 12, 1, 2),
	F(50000000, P_GPLL0_OUT_MAIN, 12, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 6, 0, 0),
	F(202000000, P_GPLL9_OUT_MAIN, 4, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc2_apps_clk_src = {
	.cmd_rcgr = 0x1400c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_6,
	.freq_tbl = ftbl_gcc_sdcc2_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_sdcc2_apps_clk_src",
		.parent_data = gcc_parents_6,
		.num_parents = ARRAY_SIZE(gcc_parents_6),
		.flags = CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_floor_ops,
	},
};

static const struct freq_tbl ftbl_gcc_sdcc4_apps_clk_src[] = {
	F(400000, P_BI_TCXO, 12, 1, 4),
	F(9600000, P_BI_TCXO, 2, 0, 0),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(25000000, P_GPLL0_OUT_MAIN, 12, 1, 2),
	F(50000000, P_GPLL0_OUT_MAIN, 12, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 6, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc4_apps_clk_src = {
	.cmd_rcgr = 0x1600c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_sdcc4_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_sdcc4_apps_clk_src",
		.parent_data = gcc_parents_3,
		.num_parents = ARRAY_SIZE(gcc_parents_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_floor_ops,
	},
};

static const struct freq_tbl ftbl_gcc_tsif_ref_clk_src[] = {
	F(105495, P_BI_TCXO, 2, 1, 91),
	{ }
};

static struct clk_rcg2 gcc_tsif_ref_clk_src = {
	.cmd_rcgr = 0x36010,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_7,
	.freq_tbl = ftbl_gcc_tsif_ref_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_tsif_ref_clk_src",
		.parent_data = gcc_parents_7,
		.num_parents = ARRAY_SIZE(gcc_parents_7),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_ufs_card_axi_clk_src[] = {
	F(25000000, P_GPLL0_OUT_EVEN, 12, 0, 0),
	F(50000000, P_GPLL0_OUT_EVEN, 6, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 6, 0, 0),
	F(200000000, P_GPLL0_OUT_MAIN, 3, 0, 0),
	F(240000000, P_GPLL0_OUT_MAIN, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_card_axi_clk_src = {
	.cmd_rcgr = 0x75020,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_card_axi_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_card_axi_clk_src",
		.parent_data = gcc_parents_0,
		.num_parents = ARRAY_SIZE(gcc_parents_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_ufs_card_ice_core_clk_src[] = {
	F(37500000, P_GPLL0_OUT_EVEN, 8, 0, 0),
	F(75000000, P_GPLL0_OUT_EVEN, 4, 0, 0),
	F(150000000, P_GPLL0_OUT_MAIN, 4, 0, 0),
	F(300000000, P_GPLL0_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_card_ice_core_clk_src = {
	.cmd_rcgr = 0x75060,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_card_ice_core_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_card_ice_core_clk_src",
		.parent_data = gcc_parents_0,
		.num_parents = ARRAY_SIZE(gcc_parents_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_ufs_card_phy_aux_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_card_phy_aux_clk_src = {
	.cmd_rcgr = 0x75094,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_ufs_card_phy_aux_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_card_phy_aux_clk_src",
		.parent_data = gcc_parents_4,
		.num_parents = ARRAY_SIZE(gcc_parents_4),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_ufs_card_unipro_core_clk_src[] = {
	F(37500000, P_GPLL0_OUT_EVEN, 8, 0, 0),
	F(75000000, P_GPLL0_OUT_MAIN, 8, 0, 0),
	F(150000000, P_GPLL0_OUT_MAIN, 4, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_card_unipro_core_clk_src = {
	.cmd_rcgr = 0x75078,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_card_unipro_core_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_card_unipro_core_clk_src",
		.parent_data = gcc_parents_0,
		.num_parents = ARRAY_SIZE(gcc_parents_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_ufs_phy_axi_clk_src[] = {
	F(25000000, P_GPLL0_OUT_EVEN, 12, 0, 0),
	F(37500000, P_GPLL0_OUT_EVEN, 8, 0, 0),
	F(75000000, P_GPLL0_OUT_EVEN, 4, 0, 0),
	F(150000000, P_GPLL0_OUT_MAIN, 4, 0, 0),
	F(300000000, P_GPLL0_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_phy_axi_clk_src = {
	.cmd_rcgr = 0x77020,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_phy_axi_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_phy_axi_clk_src",
		.parent_data = gcc_parents_0,
		.num_parents = ARRAY_SIZE(gcc_parents_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_ufs_phy_ice_core_clk_src = {
	.cmd_rcgr = 0x77060,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_card_ice_core_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_phy_ice_core_clk_src",
		.parent_data = gcc_parents_0,
		.num_parents = ARRAY_SIZE(gcc_parents_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_ufs_phy_phy_aux_clk_src = {
	.cmd_rcgr = 0x77094,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_phy_phy_aux_clk_src",
		.parent_data = gcc_parents_4,
		.num_parents = ARRAY_SIZE(gcc_parents_4),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_ufs_phy_unipro_core_clk_src = {
	.cmd_rcgr = 0x77078,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_card_ice_core_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_phy_unipro_core_clk_src",
		.parent_data = gcc_parents_0,
		.num_parents = ARRAY_SIZE(gcc_parents_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb30_prim_master_clk_src[] = {
	F(33333333, P_GPLL0_OUT_EVEN, 9, 0, 0),
	F(66666667, P_GPLL0_OUT_EVEN, 4.5, 0, 0),
	F(133333333, P_GPLL0_OUT_MAIN, 4.5, 0, 0),
	F(200000000, P_GPLL0_OUT_MAIN, 3, 0, 0),
	F(240000000, P_GPLL0_OUT_MAIN, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb30_prim_master_clk_src = {
	.cmd_rcgr = 0xf01c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb30_prim_master_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_usb30_prim_master_clk_src",
		.parent_data = gcc_parents_0,
		.num_parents = ARRAY_SIZE(gcc_parents_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb30_prim_mock_utmi_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(20000000, P_GPLL0_OUT_EVEN, 15, 0, 0),
	F(60000000, P_GPLL0_OUT_EVEN, 5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb30_prim_mock_utmi_clk_src = {
	.cmd_rcgr = 0xf034,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb30_prim_mock_utmi_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_usb30_prim_mock_utmi_clk_src",
		.parent_data = gcc_parents_0,
		.num_parents = ARRAY_SIZE(gcc_parents_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_usb30_sec_master_clk_src = {
	.cmd_rcgr = 0x1001c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb30_prim_master_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_usb30_sec_master_clk_src",
		.parent_data = gcc_parents_0,
		.num_parents = ARRAY_SIZE(gcc_parents_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_usb30_sec_mock_utmi_clk_src = {
	.cmd_rcgr = 0x10034,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb30_prim_mock_utmi_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_usb30_sec_mock_utmi_clk_src",
		.parent_data = gcc_parents_0,
		.num_parents = ARRAY_SIZE(gcc_parents_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_usb3_prim_phy_aux_clk_src = {
	.cmd_rcgr = 0xf060,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_ufs_card_phy_aux_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_usb3_prim_phy_aux_clk_src",
		.parent_data = gcc_parents_2,
		.num_parents = ARRAY_SIZE(gcc_parents_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_usb3_sec_phy_aux_clk_src = {
	.cmd_rcgr = 0x10060,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_ufs_card_phy_aux_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_usb3_sec_phy_aux_clk_src",
		.parent_data = gcc_parents_2,
		.num_parents = ARRAY_SIZE(gcc_parents_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_aggre_noc_pcie_tbu_clk = {
	.halt_reg = 0x90018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x90018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_aggre_noc_pcie_tbu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_ufs_card_axi_clk = {
	.halt_reg = 0x750c0,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x750c0,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x750c0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_aggre_ufs_card_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_ufs_card_axi_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_ufs_card_axi_hw_ctl_clk = {
	.halt_reg = 0x750c0,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x750c0,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x750c0,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_aggre_ufs_card_axi_hw_ctl_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_aggre_ufs_card_axi_clk.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch_simple_ops,
		},
	},
};

static struct clk_branch gcc_aggre_ufs_phy_axi_clk = {
	.halt_reg = 0x770c0,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x770c0,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x770c0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_aggre_ufs_phy_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_ufs_phy_axi_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_ufs_phy_axi_hw_ctl_clk = {
	.halt_reg = 0x770c0,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x770c0,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x770c0,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_aggre_ufs_phy_axi_hw_ctl_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_aggre_ufs_phy_axi_clk.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch_simple_ops,
		},
	},
};

static struct clk_branch gcc_aggre_usb3_prim_axi_clk = {
	.halt_reg = 0xf07c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf07c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_aggre_usb3_prim_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_usb30_prim_master_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_usb3_sec_axi_clk = {
	.halt_reg = 0x1007c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1007c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_aggre_usb3_sec_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_usb30_sec_master_clk_src.clkr.hw },
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
		.enable_reg = 0x52004,
		.enable_mask = BIT(10),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_boot_rom_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

/*
 * Clock ON depends on external parent 'config noc', so cant poll
 * delay and also mark as crtitical for camss boot
 */
static struct clk_branch gcc_camera_ahb_clk = {
	.halt_reg = 0xb008,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0xb008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xb008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camera_ahb_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_hf_axi_clk = {
	.halt_reg = 0xb030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb030,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camera_hf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_sf_axi_clk = {
	.halt_reg = 0xb034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb034,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camera_sf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

/* XO critical input to camss, so no need to poll */
static struct clk_branch gcc_camera_xo_clk = {
	.halt_reg = 0xb044,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0xb044,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camera_xo_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_usb3_prim_axi_clk = {
	.halt_reg = 0xf078,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf078,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_cfg_noc_usb3_prim_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_usb30_prim_master_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_usb3_sec_axi_clk = {
	.halt_reg = 0x10078,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10078,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_cfg_noc_usb3_sec_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_usb30_sec_master_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cpuss_dvm_bus_clk = {
	.halt_reg = 0x48190,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x48190,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_cpuss_dvm_bus_clk",
			 /* required for cpuss */
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cpuss_gnoc_clk = {
	.halt_reg = 0x48004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x48004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52004,
		.enable_mask = BIT(22),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_cpuss_gnoc_clk",
			 /* required for cpuss */
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cpuss_rbcpr_clk = {
	.halt_reg = 0x48008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x48008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_cpuss_rbcpr_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ddrss_gpu_axi_clk = {
	.halt_reg = 0x71154,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x71154,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ddrss_gpu_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

/*
 * Clock ON depends on external parent 'config noc', so cant poll
 * delay and also mark as crtitical for disp boot
 */
static struct clk_branch gcc_disp_ahb_clk = {
	.halt_reg = 0xb00c,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0xb00c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xb00c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_disp_ahb_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_hf_axi_clk = {
	.halt_reg = 0xb038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb038,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_disp_hf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_sf_axi_clk = {
	.halt_reg = 0xb03c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb03c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_disp_sf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

/* XO critical input to disp, so no need to poll */
static struct clk_branch gcc_disp_xo_clk = {
	.halt_reg = 0xb048,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0xb048,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_disp_xo_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac_axi_clk = {
	.halt_reg = 0x6010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_emac_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac_ptp_clk = {
	.halt_reg = 0x6034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6034,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_emac_ptp_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_emac_ptp_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac_rgmii_clk = {
	.halt_reg = 0x6018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_emac_rgmii_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_emac_rgmii_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac_slv_ahb_clk = {
	.halt_reg = 0x6014,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x6014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x6014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_emac_slv_ahb_clk",
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gp1_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_gp1_clk_src.clkr.hw },
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gp2_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_gp2_clk_src.clkr.hw },
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gp3_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_gp3_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_cfg_ahb_clk = {
	.halt_reg = 0x71004,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x71004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x71004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gpu_cfg_ahb_clk",
			 /* required for gpu */
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_gpll0_clk_src = {
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x52004,
		.enable_mask = BIT(15),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gpu_gpll0_clk_src",
			.parent_hws = (const struct clk_hw *[]){
				&gpll0.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_gpll0_div_clk_src = {
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x52004,
		.enable_mask = BIT(16),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gpu_gpll0_div_clk_src",
			.parent_hws = (const struct clk_hw *[]){
				&gpll0_out_even.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_iref_clk = {
	.halt_reg = 0x8c010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gpu_iref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_memnoc_gfx_clk = {
	.halt_reg = 0x7100c,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x7100c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gpu_memnoc_gfx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_snoc_dvm_gfx_clk = {
	.halt_reg = 0x71018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x71018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gpu_snoc_dvm_gfx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_npu_at_clk = {
	.halt_reg = 0x4d010,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x4d010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_npu_at_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_npu_axi_clk = {
	.halt_reg = 0x4d008,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x4d008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_npu_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_npu_cfg_ahb_clk = {
	.halt_reg = 0x4d004,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x4d004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x4d004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_npu_cfg_ahb_clk",
			 /* required for npu */
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_npu_gpll0_clk_src = {
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x52004,
		.enable_mask = BIT(18),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_npu_gpll0_clk_src",
			.parent_hws = (const struct clk_hw *[]){
				&gpll0.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_npu_gpll0_div_clk_src = {
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x52004,
		.enable_mask = BIT(19),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_npu_gpll0_div_clk_src",
			.parent_hws = (const struct clk_hw *[]){
				&gpll0_out_even.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_npu_trig_clk = {
	.halt_reg = 0x4d00c,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x4d00c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_npu_trig_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie0_phy_refgen_clk = {
	.halt_reg = 0x6f02c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6f02c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie0_phy_refgen_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_pcie_phy_refgen_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie1_phy_refgen_clk = {
	.halt_reg = 0x6f030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6f030,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie1_phy_refgen_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_pcie_phy_refgen_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_aux_clk = {
	.halt_reg = 0x6b020,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x5200c,
		.enable_mask = BIT(3),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_0_aux_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_pcie_0_aux_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_cfg_ahb_clk = {
	.halt_reg = 0x6b01c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x6b01c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x5200c,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_0_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_clkref_clk = {
	.halt_reg = 0x8c00c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c00c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_0_clkref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_mstr_axi_clk = {
	.halt_reg = 0x6b018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x5200c,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_0_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

/* Clock ON depends on external parent 'PIPE' clock, so dont poll */
static struct clk_branch gcc_pcie_0_pipe_clk = {
	.halt_reg = 0x6b024,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x5200c,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_0_pipe_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_slv_axi_clk = {
	.halt_reg = 0x6b014,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x6b014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x5200c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_0_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_slv_q2a_axi_clk = {
	.halt_reg = 0x6b010,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x5200c,
		.enable_mask = BIT(5),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_0_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_aux_clk = {
	.halt_reg = 0x8d020,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52004,
		.enable_mask = BIT(29),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_1_aux_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_pcie_1_aux_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_cfg_ahb_clk = {
	.halt_reg = 0x8d01c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x8d01c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52004,
		.enable_mask = BIT(28),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_1_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_clkref_clk = {
	.halt_reg = 0x8c02c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c02c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_1_clkref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_mstr_axi_clk = {
	.halt_reg = 0x8d018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52004,
		.enable_mask = BIT(27),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_1_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

/* Clock ON depends on external parent 'PIPE' clock, so dont poll */
static struct clk_branch gcc_pcie_1_pipe_clk = {
	.halt_reg = 0x8d024,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x52004,
		.enable_mask = BIT(30),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_1_pipe_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_slv_axi_clk = {
	.halt_reg = 0x8d014,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x8d014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52004,
		.enable_mask = BIT(26),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_1_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_slv_q2a_axi_clk = {
	.halt_reg = 0x8d010,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52004,
		.enable_mask = BIT(25),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_1_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_phy_aux_clk = {
	.halt_reg = 0x6f004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6f004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_phy_aux_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_pcie_0_aux_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pdm2_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_pdm2_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm_ahb_clk = {
	.halt_reg = 0x33004,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x33004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x33004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pdm_xo4_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_prng_ahb_clk = {
	.halt_reg = 0x34004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52004,
		.enable_mask = BIT(13),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_prng_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_camera_nrt_ahb_clk = {
	.halt_reg = 0xb018,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0xb018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xb018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qmip_camera_nrt_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_camera_rt_ahb_clk = {
	.halt_reg = 0xb01c,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0xb01c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xb01c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qmip_camera_rt_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_disp_ahb_clk = {
	.halt_reg = 0xb020,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0xb020,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xb020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qmip_disp_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_cvp_ahb_clk = {
	.halt_reg = 0xb010,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0xb010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xb010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qmip_video_cvp_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_vcodec_ahb_clk = {
	.halt_reg = 0xb014,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0xb014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xb014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qmip_video_vcodec_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qspi_cnoc_periph_ahb_clk = {
	.halt_reg = 0x4b000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4b000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qspi_cnoc_periph_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qspi_core_clk = {
	.halt_reg = 0x4b004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4b004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qspi_core_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_qspi_core_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s0_clk = {
	.halt_reg = 0x17144,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x5200c,
		.enable_mask = BIT(10),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s0_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_qupv3_wrap0_s0_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s1_clk = {
	.halt_reg = 0x17274,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x5200c,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s1_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_qupv3_wrap0_s1_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s2_clk = {
	.halt_reg = 0x173a4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x5200c,
		.enable_mask = BIT(12),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s2_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_qupv3_wrap0_s2_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s3_clk = {
	.halt_reg = 0x174d4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x5200c,
		.enable_mask = BIT(13),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s3_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_qupv3_wrap0_s3_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s4_clk = {
	.halt_reg = 0x17604,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x5200c,
		.enable_mask = BIT(14),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s4_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_qupv3_wrap0_s4_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s5_clk = {
	.halt_reg = 0x17734,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x5200c,
		.enable_mask = BIT(15),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s5_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_qupv3_wrap0_s5_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s6_clk = {
	.halt_reg = 0x17864,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x5200c,
		.enable_mask = BIT(16),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s6_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_qupv3_wrap0_s6_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s7_clk = {
	.halt_reg = 0x17994,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x5200c,
		.enable_mask = BIT(17),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s7_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_qupv3_wrap0_s7_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s0_clk = {
	.halt_reg = 0x18144,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x5200c,
		.enable_mask = BIT(22),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap1_s0_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_qupv3_wrap1_s0_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s1_clk = {
	.halt_reg = 0x18274,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x5200c,
		.enable_mask = BIT(23),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap1_s1_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_qupv3_wrap1_s1_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s2_clk = {
	.halt_reg = 0x183a4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x5200c,
		.enable_mask = BIT(24),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap1_s2_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_qupv3_wrap1_s2_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s3_clk = {
	.halt_reg = 0x184d4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x5200c,
		.enable_mask = BIT(25),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap1_s3_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_qupv3_wrap1_s3_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s4_clk = {
	.halt_reg = 0x18604,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x5200c,
		.enable_mask = BIT(26),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap1_s4_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_qupv3_wrap1_s4_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s5_clk = {
	.halt_reg = 0x18734,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x5200c,
		.enable_mask = BIT(27),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap1_s5_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_qupv3_wrap1_s5_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s0_clk = {
	.halt_reg = 0x1e144,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52014,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap2_s0_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_qupv3_wrap2_s0_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s1_clk = {
	.halt_reg = 0x1e274,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52014,
		.enable_mask = BIT(5),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap2_s1_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_qupv3_wrap2_s1_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s2_clk = {
	.halt_reg = 0x1e3a4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52014,
		.enable_mask = BIT(6),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap2_s2_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_qupv3_wrap2_s2_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s3_clk = {
	.halt_reg = 0x1e4d4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52014,
		.enable_mask = BIT(7),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap2_s3_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_qupv3_wrap2_s3_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s4_clk = {
	.halt_reg = 0x1e604,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52014,
		.enable_mask = BIT(8),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap2_s4_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_qupv3_wrap2_s4_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s5_clk = {
	.halt_reg = 0x1e734,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52014,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap2_s5_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_qupv3_wrap2_s5_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_0_m_ahb_clk = {
	.halt_reg = 0x17004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x5200c,
		.enable_mask = BIT(6),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap_0_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_0_s_ahb_clk = {
	.halt_reg = 0x17008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x17008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x5200c,
		.enable_mask = BIT(7),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap_0_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_1_m_ahb_clk = {
	.halt_reg = 0x18004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x5200c,
		.enable_mask = BIT(20),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap_1_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_1_s_ahb_clk = {
	.halt_reg = 0x18008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x18008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x5200c,
		.enable_mask = BIT(21),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap_1_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_2_m_ahb_clk = {
	.halt_reg = 0x1e004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52014,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap_2_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_2_s_ahb_clk = {
	.halt_reg = 0x1e008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x1e008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52014,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap_2_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc2_ahb_clk = {
	.halt_reg = 0x14008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x14008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc2_apps_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_sdcc2_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc4_ahb_clk = {
	.halt_reg = 0x16008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x16008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc4_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc4_apps_clk = {
	.halt_reg = 0x16004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x16004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc4_apps_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_sdcc4_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_tsif_ahb_clk = {
	.halt_reg = 0x36004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x36004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_tsif_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_tsif_inactivity_timers_clk = {
	.halt_reg = 0x3600c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3600c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_tsif_inactivity_timers_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_tsif_ref_clk = {
	.halt_reg = 0x36008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x36008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_tsif_ref_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_tsif_ref_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_ahb_clk = {
	.halt_reg = 0x75014,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x75014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x75014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_card_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_axi_clk = {
	.halt_reg = 0x75010,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x75010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x75010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_card_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_ufs_card_axi_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_axi_hw_ctl_clk = {
	.halt_reg = 0x75010,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x75010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x75010,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_card_axi_hw_ctl_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_ufs_card_axi_clk.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch_simple_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_clkref_clk = {
	.halt_reg = 0x8c004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_card_clkref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_ice_core_clk = {
	.halt_reg = 0x7505c,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x7505c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7505c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_card_ice_core_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_ufs_card_ice_core_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_ice_core_hw_ctl_clk = {
	.halt_reg = 0x7505c,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x7505c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7505c,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_card_ice_core_hw_ctl_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_ufs_card_ice_core_clk.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch_simple_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_phy_aux_clk = {
	.halt_reg = 0x75090,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x75090,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x75090,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_card_phy_aux_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_ufs_card_phy_aux_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_phy_aux_hw_ctl_clk = {
	.halt_reg = 0x75090,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x75090,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x75090,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_card_phy_aux_hw_ctl_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_ufs_card_phy_aux_clk.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch_simple_ops,
		},
	},
};

/* external clocks so add BRANCH_HALT_SKIP */
static struct clk_branch gcc_ufs_card_rx_symbol_0_clk = {
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x7501c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_card_rx_symbol_0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

/* external clocks so add BRANCH_HALT_SKIP */
static struct clk_branch gcc_ufs_card_rx_symbol_1_clk = {
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x750ac,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_card_rx_symbol_1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

/* external clocks so add BRANCH_HALT_SKIP */
static struct clk_branch gcc_ufs_card_tx_symbol_0_clk = {
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x75018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_card_tx_symbol_0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_unipro_core_clk = {
	.halt_reg = 0x75058,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x75058,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x75058,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_card_unipro_core_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_ufs_card_unipro_core_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_unipro_core_hw_ctl_clk = {
	.halt_reg = 0x75058,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x75058,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x75058,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_card_unipro_core_hw_ctl_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_ufs_card_unipro_core_clk.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch_simple_ops,
		},
	},
};

static struct clk_branch gcc_ufs_mem_clkref_clk = {
	.halt_reg = 0x8c000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_mem_clkref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_ahb_clk = {
	.halt_reg = 0x77014,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x77014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x77014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_axi_clk = {
	.halt_reg = 0x77010,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x77010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x77010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_ufs_phy_axi_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_axi_hw_ctl_clk = {
	.halt_reg = 0x77010,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x77010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x77010,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_axi_hw_ctl_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_ufs_phy_axi_clk.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch_simple_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_ice_core_clk = {
	.halt_reg = 0x7705c,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x7705c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7705c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_ice_core_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_ufs_phy_ice_core_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_ice_core_hw_ctl_clk = {
	.halt_reg = 0x7705c,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x7705c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7705c,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_ice_core_hw_ctl_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_ufs_phy_ice_core_clk.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch_simple_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_phy_aux_clk = {
	.halt_reg = 0x77090,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x77090,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x77090,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_phy_aux_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_ufs_phy_phy_aux_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_phy_aux_hw_ctl_clk = {
	.halt_reg = 0x77090,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x77090,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x77090,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_phy_aux_hw_ctl_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_ufs_phy_phy_aux_clk.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch_simple_ops,
		},
	},
};

/* external clocks so add BRANCH_HALT_SKIP */
static struct clk_branch gcc_ufs_phy_rx_symbol_0_clk = {
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x7701c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_rx_symbol_0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

/* external clocks so add BRANCH_HALT_SKIP */
static struct clk_branch gcc_ufs_phy_rx_symbol_1_clk = {
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x770ac,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_rx_symbol_1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

/* external clocks so add BRANCH_HALT_SKIP */
static struct clk_branch gcc_ufs_phy_tx_symbol_0_clk = {
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x77018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_tx_symbol_0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_unipro_core_clk = {
	.halt_reg = 0x77058,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x77058,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x77058,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_unipro_core_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_ufs_phy_unipro_core_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_unipro_core_hw_ctl_clk = {
	.halt_reg = 0x77058,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x77058,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x77058,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_unipro_core_hw_ctl_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_ufs_phy_unipro_core_clk.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch_simple_ops,
		},
	},
};

static struct clk_branch gcc_usb30_prim_master_clk = {
	.halt_reg = 0xf010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb30_prim_master_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_usb30_prim_master_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_prim_mock_utmi_clk = {
	.halt_reg = 0xf018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb30_prim_mock_utmi_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_usb30_prim_mock_utmi_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_prim_sleep_clk = {
	.halt_reg = 0xf014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb30_prim_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_sec_master_clk = {
	.halt_reg = 0x10010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb30_sec_master_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_usb30_sec_master_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_sec_mock_utmi_clk = {
	.halt_reg = 0x10018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb30_sec_mock_utmi_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_usb30_sec_mock_utmi_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_sec_sleep_clk = {
	.halt_reg = 0x10014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb30_sec_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_clkref_clk = {
	.halt_reg = 0x8c008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_prim_clkref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_phy_aux_clk = {
	.halt_reg = 0xf050,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf050,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_prim_phy_aux_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_usb3_prim_phy_aux_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_phy_com_aux_clk = {
	.halt_reg = 0xf054,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf054,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_prim_phy_com_aux_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_usb3_prim_phy_aux_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_phy_pipe_clk = {
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0xf058,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_prim_phy_pipe_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_sec_clkref_clk = {
	.halt_reg = 0x8c028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_sec_clkref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_sec_phy_aux_clk = {
	.halt_reg = 0x10050,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10050,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_sec_phy_aux_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_usb3_sec_phy_aux_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_sec_phy_com_aux_clk = {
	.halt_reg = 0x10054,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10054,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_sec_phy_com_aux_clk",
			.parent_hws = (const struct clk_hw *[]){
				      &gcc_usb3_sec_phy_aux_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_sec_phy_pipe_clk = {
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x10058,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_sec_phy_pipe_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

/*
 * Clock ON depends on external parent 'config noc', so cant poll
 * delay and also mark as crtitical for video boot
 */
static struct clk_branch gcc_video_ahb_clk = {
	.halt_reg = 0xb004,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0xb004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xb004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_video_ahb_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_axi0_clk = {
	.halt_reg = 0xb024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb024,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_video_axi0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_axi1_clk = {
	.halt_reg = 0xb028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_video_axi1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_axic_clk = {
	.halt_reg = 0xb02c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb02c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_video_axic_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

/* XO critical input to video, so no need to poll */
static struct clk_branch gcc_video_xo_clk = {
	.halt_reg = 0xb040,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0xb040,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_video_xo_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc pcie_0_gdsc = {
	.gdscr = 0x6b004,
	.pd = {
		.name = "pcie_0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR,
};

static struct gdsc pcie_1_gdsc = {
	.gdscr = 0x8d004,
	.pd = {
		.name = "pcie_1_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR,
};

static struct gdsc ufs_card_gdsc = {
	.gdscr = 0x75004,
	.pd = {
		.name = "ufs_card_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR,
};

static struct gdsc ufs_phy_gdsc = {
	.gdscr = 0x77004,
	.pd = {
		.name = "ufs_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR,
};

static struct gdsc emac_gdsc = {
	.gdscr = 0x6004,
	.pd = {
		.name = "emac_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR,
};

static struct gdsc usb30_prim_gdsc = {
	.gdscr = 0xf004,
	.pd = {
		.name = "usb30_prim_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR,
};

static struct gdsc usb30_sec_gdsc = {
	.gdscr = 0x10004,
	.pd = {
		.name = "usb30_sec_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR,
};

static struct clk_regmap *gcc_sm8150_clocks[] = {
	[GCC_AGGRE_NOC_PCIE_TBU_CLK] = &gcc_aggre_noc_pcie_tbu_clk.clkr,
	[GCC_AGGRE_UFS_CARD_AXI_CLK] = &gcc_aggre_ufs_card_axi_clk.clkr,
	[GCC_AGGRE_UFS_CARD_AXI_HW_CTL_CLK] =
		&gcc_aggre_ufs_card_axi_hw_ctl_clk.clkr,
	[GCC_AGGRE_UFS_PHY_AXI_CLK] = &gcc_aggre_ufs_phy_axi_clk.clkr,
	[GCC_AGGRE_UFS_PHY_AXI_HW_CTL_CLK] =
		&gcc_aggre_ufs_phy_axi_hw_ctl_clk.clkr,
	[GCC_AGGRE_USB3_PRIM_AXI_CLK] = &gcc_aggre_usb3_prim_axi_clk.clkr,
	[GCC_AGGRE_USB3_SEC_AXI_CLK] = &gcc_aggre_usb3_sec_axi_clk.clkr,
	[GCC_BOOT_ROM_AHB_CLK] = &gcc_boot_rom_ahb_clk.clkr,
	[GCC_CAMERA_AHB_CLK] = &gcc_camera_ahb_clk.clkr,
	[GCC_CAMERA_HF_AXI_CLK] = &gcc_camera_hf_axi_clk.clkr,
	[GCC_CAMERA_SF_AXI_CLK] = &gcc_camera_sf_axi_clk.clkr,
	[GCC_CAMERA_XO_CLK] = &gcc_camera_xo_clk.clkr,
	[GCC_CFG_NOC_USB3_PRIM_AXI_CLK] = &gcc_cfg_noc_usb3_prim_axi_clk.clkr,
	[GCC_CFG_NOC_USB3_SEC_AXI_CLK] = &gcc_cfg_noc_usb3_sec_axi_clk.clkr,
	[GCC_CPUSS_DVM_BUS_CLK] = &gcc_cpuss_dvm_bus_clk.clkr,
	[GCC_CPUSS_GNOC_CLK] = &gcc_cpuss_gnoc_clk.clkr,
	[GCC_CPUSS_RBCPR_CLK] = &gcc_cpuss_rbcpr_clk.clkr,
	[GCC_DDRSS_GPU_AXI_CLK] = &gcc_ddrss_gpu_axi_clk.clkr,
	[GCC_DISP_AHB_CLK] = &gcc_disp_ahb_clk.clkr,
	[GCC_DISP_HF_AXI_CLK] = &gcc_disp_hf_axi_clk.clkr,
	[GCC_DISP_SF_AXI_CLK] = &gcc_disp_sf_axi_clk.clkr,
	[GCC_DISP_XO_CLK] = &gcc_disp_xo_clk.clkr,
	[GCC_EMAC_AXI_CLK] = &gcc_emac_axi_clk.clkr,
	[GCC_EMAC_PTP_CLK] = &gcc_emac_ptp_clk.clkr,
	[GCC_EMAC_PTP_CLK_SRC] = &gcc_emac_ptp_clk_src.clkr,
	[GCC_EMAC_RGMII_CLK] = &gcc_emac_rgmii_clk.clkr,
	[GCC_EMAC_RGMII_CLK_SRC] = &gcc_emac_rgmii_clk_src.clkr,
	[GCC_EMAC_SLV_AHB_CLK] = &gcc_emac_slv_ahb_clk.clkr,
	[GCC_GP1_CLK] = &gcc_gp1_clk.clkr,
	[GCC_GP1_CLK_SRC] = &gcc_gp1_clk_src.clkr,
	[GCC_GP2_CLK] = &gcc_gp2_clk.clkr,
	[GCC_GP2_CLK_SRC] = &gcc_gp2_clk_src.clkr,
	[GCC_GP3_CLK] = &gcc_gp3_clk.clkr,
	[GCC_GP3_CLK_SRC] = &gcc_gp3_clk_src.clkr,
	[GCC_GPU_CFG_AHB_CLK] = &gcc_gpu_cfg_ahb_clk.clkr,
	[GCC_GPU_GPLL0_CLK_SRC] = &gcc_gpu_gpll0_clk_src.clkr,
	[GCC_GPU_GPLL0_DIV_CLK_SRC] = &gcc_gpu_gpll0_div_clk_src.clkr,
	[GCC_GPU_IREF_CLK] = &gcc_gpu_iref_clk.clkr,
	[GCC_GPU_MEMNOC_GFX_CLK] = &gcc_gpu_memnoc_gfx_clk.clkr,
	[GCC_GPU_SNOC_DVM_GFX_CLK] = &gcc_gpu_snoc_dvm_gfx_clk.clkr,
	[GCC_NPU_AT_CLK] = &gcc_npu_at_clk.clkr,
	[GCC_NPU_AXI_CLK] = &gcc_npu_axi_clk.clkr,
	[GCC_NPU_CFG_AHB_CLK] = &gcc_npu_cfg_ahb_clk.clkr,
	[GCC_NPU_GPLL0_CLK_SRC] = &gcc_npu_gpll0_clk_src.clkr,
	[GCC_NPU_GPLL0_DIV_CLK_SRC] = &gcc_npu_gpll0_div_clk_src.clkr,
	[GCC_NPU_TRIG_CLK] = &gcc_npu_trig_clk.clkr,
	[GCC_PCIE0_PHY_REFGEN_CLK] = &gcc_pcie0_phy_refgen_clk.clkr,
	[GCC_PCIE1_PHY_REFGEN_CLK] = &gcc_pcie1_phy_refgen_clk.clkr,
	[GCC_PCIE_0_AUX_CLK] = &gcc_pcie_0_aux_clk.clkr,
	[GCC_PCIE_0_AUX_CLK_SRC] = &gcc_pcie_0_aux_clk_src.clkr,
	[GCC_PCIE_0_CFG_AHB_CLK] = &gcc_pcie_0_cfg_ahb_clk.clkr,
	[GCC_PCIE_0_CLKREF_CLK] = &gcc_pcie_0_clkref_clk.clkr,
	[GCC_PCIE_0_MSTR_AXI_CLK] = &gcc_pcie_0_mstr_axi_clk.clkr,
	[GCC_PCIE_0_PIPE_CLK] = &gcc_pcie_0_pipe_clk.clkr,
	[GCC_PCIE_0_SLV_AXI_CLK] = &gcc_pcie_0_slv_axi_clk.clkr,
	[GCC_PCIE_0_SLV_Q2A_AXI_CLK] = &gcc_pcie_0_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_1_AUX_CLK] = &gcc_pcie_1_aux_clk.clkr,
	[GCC_PCIE_1_AUX_CLK_SRC] = &gcc_pcie_1_aux_clk_src.clkr,
	[GCC_PCIE_1_CFG_AHB_CLK] = &gcc_pcie_1_cfg_ahb_clk.clkr,
	[GCC_PCIE_1_CLKREF_CLK] = &gcc_pcie_1_clkref_clk.clkr,
	[GCC_PCIE_1_MSTR_AXI_CLK] = &gcc_pcie_1_mstr_axi_clk.clkr,
	[GCC_PCIE_1_PIPE_CLK] = &gcc_pcie_1_pipe_clk.clkr,
	[GCC_PCIE_1_SLV_AXI_CLK] = &gcc_pcie_1_slv_axi_clk.clkr,
	[GCC_PCIE_1_SLV_Q2A_AXI_CLK] = &gcc_pcie_1_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_PHY_AUX_CLK] = &gcc_pcie_phy_aux_clk.clkr,
	[GCC_PCIE_PHY_REFGEN_CLK_SRC] = &gcc_pcie_phy_refgen_clk_src.clkr,
	[GCC_PDM2_CLK] = &gcc_pdm2_clk.clkr,
	[GCC_PDM2_CLK_SRC] = &gcc_pdm2_clk_src.clkr,
	[GCC_PDM_AHB_CLK] = &gcc_pdm_ahb_clk.clkr,
	[GCC_PDM_XO4_CLK] = &gcc_pdm_xo4_clk.clkr,
	[GCC_PRNG_AHB_CLK] = &gcc_prng_ahb_clk.clkr,
	[GCC_QMIP_CAMERA_NRT_AHB_CLK] = &gcc_qmip_camera_nrt_ahb_clk.clkr,
	[GCC_QMIP_CAMERA_RT_AHB_CLK] = &gcc_qmip_camera_rt_ahb_clk.clkr,
	[GCC_QMIP_DISP_AHB_CLK] = &gcc_qmip_disp_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_CVP_AHB_CLK] = &gcc_qmip_video_cvp_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_VCODEC_AHB_CLK] = &gcc_qmip_video_vcodec_ahb_clk.clkr,
	[GCC_QSPI_CNOC_PERIPH_AHB_CLK] = &gcc_qspi_cnoc_periph_ahb_clk.clkr,
	[GCC_QSPI_CORE_CLK] = &gcc_qspi_core_clk.clkr,
	[GCC_QSPI_CORE_CLK_SRC] = &gcc_qspi_core_clk_src.clkr,
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
	[GCC_QUPV3_WRAP2_S0_CLK] = &gcc_qupv3_wrap2_s0_clk.clkr,
	[GCC_QUPV3_WRAP2_S0_CLK_SRC] = &gcc_qupv3_wrap2_s0_clk_src.clkr,
	[GCC_QUPV3_WRAP2_S1_CLK] = &gcc_qupv3_wrap2_s1_clk.clkr,
	[GCC_QUPV3_WRAP2_S1_CLK_SRC] = &gcc_qupv3_wrap2_s1_clk_src.clkr,
	[GCC_QUPV3_WRAP2_S2_CLK] = &gcc_qupv3_wrap2_s2_clk.clkr,
	[GCC_QUPV3_WRAP2_S2_CLK_SRC] = &gcc_qupv3_wrap2_s2_clk_src.clkr,
	[GCC_QUPV3_WRAP2_S3_CLK] = &gcc_qupv3_wrap2_s3_clk.clkr,
	[GCC_QUPV3_WRAP2_S3_CLK_SRC] = &gcc_qupv3_wrap2_s3_clk_src.clkr,
	[GCC_QUPV3_WRAP2_S4_CLK] = &gcc_qupv3_wrap2_s4_clk.clkr,
	[GCC_QUPV3_WRAP2_S4_CLK_SRC] = &gcc_qupv3_wrap2_s4_clk_src.clkr,
	[GCC_QUPV3_WRAP2_S5_CLK] = &gcc_qupv3_wrap2_s5_clk.clkr,
	[GCC_QUPV3_WRAP2_S5_CLK_SRC] = &gcc_qupv3_wrap2_s5_clk_src.clkr,
	[GCC_QUPV3_WRAP_0_M_AHB_CLK] = &gcc_qupv3_wrap_0_m_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_0_S_AHB_CLK] = &gcc_qupv3_wrap_0_s_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_1_M_AHB_CLK] = &gcc_qupv3_wrap_1_m_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_1_S_AHB_CLK] = &gcc_qupv3_wrap_1_s_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_2_M_AHB_CLK] = &gcc_qupv3_wrap_2_m_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_2_S_AHB_CLK] = &gcc_qupv3_wrap_2_s_ahb_clk.clkr,
	[GCC_SDCC2_AHB_CLK] = &gcc_sdcc2_ahb_clk.clkr,
	[GCC_SDCC2_APPS_CLK] = &gcc_sdcc2_apps_clk.clkr,
	[GCC_SDCC2_APPS_CLK_SRC] = &gcc_sdcc2_apps_clk_src.clkr,
	[GCC_SDCC4_AHB_CLK] = &gcc_sdcc4_ahb_clk.clkr,
	[GCC_SDCC4_APPS_CLK] = &gcc_sdcc4_apps_clk.clkr,
	[GCC_SDCC4_APPS_CLK_SRC] = &gcc_sdcc4_apps_clk_src.clkr,
	[GCC_TSIF_AHB_CLK] = &gcc_tsif_ahb_clk.clkr,
	[GCC_TSIF_INACTIVITY_TIMERS_CLK] = &gcc_tsif_inactivity_timers_clk.clkr,
	[GCC_TSIF_REF_CLK] = &gcc_tsif_ref_clk.clkr,
	[GCC_TSIF_REF_CLK_SRC] = &gcc_tsif_ref_clk_src.clkr,
	[GCC_UFS_CARD_AHB_CLK] = &gcc_ufs_card_ahb_clk.clkr,
	[GCC_UFS_CARD_AXI_CLK] = &gcc_ufs_card_axi_clk.clkr,
	[GCC_UFS_CARD_AXI_CLK_SRC] = &gcc_ufs_card_axi_clk_src.clkr,
	[GCC_UFS_CARD_AXI_HW_CTL_CLK] = &gcc_ufs_card_axi_hw_ctl_clk.clkr,
	[GCC_UFS_CARD_CLKREF_CLK] = &gcc_ufs_card_clkref_clk.clkr,
	[GCC_UFS_CARD_ICE_CORE_CLK] = &gcc_ufs_card_ice_core_clk.clkr,
	[GCC_UFS_CARD_ICE_CORE_CLK_SRC] = &gcc_ufs_card_ice_core_clk_src.clkr,
	[GCC_UFS_CARD_ICE_CORE_HW_CTL_CLK] =
		&gcc_ufs_card_ice_core_hw_ctl_clk.clkr,
	[GCC_UFS_CARD_PHY_AUX_CLK] = &gcc_ufs_card_phy_aux_clk.clkr,
	[GCC_UFS_CARD_PHY_AUX_CLK_SRC] = &gcc_ufs_card_phy_aux_clk_src.clkr,
	[GCC_UFS_CARD_PHY_AUX_HW_CTL_CLK] =
		&gcc_ufs_card_phy_aux_hw_ctl_clk.clkr,
	[GCC_UFS_CARD_RX_SYMBOL_0_CLK] = &gcc_ufs_card_rx_symbol_0_clk.clkr,
	[GCC_UFS_CARD_RX_SYMBOL_1_CLK] = &gcc_ufs_card_rx_symbol_1_clk.clkr,
	[GCC_UFS_CARD_TX_SYMBOL_0_CLK] = &gcc_ufs_card_tx_symbol_0_clk.clkr,
	[GCC_UFS_CARD_UNIPRO_CORE_CLK] = &gcc_ufs_card_unipro_core_clk.clkr,
	[GCC_UFS_CARD_UNIPRO_CORE_CLK_SRC] =
		&gcc_ufs_card_unipro_core_clk_src.clkr,
	[GCC_UFS_CARD_UNIPRO_CORE_HW_CTL_CLK] =
		&gcc_ufs_card_unipro_core_hw_ctl_clk.clkr,
	[GCC_UFS_MEM_CLKREF_CLK] = &gcc_ufs_mem_clkref_clk.clkr,
	[GCC_UFS_PHY_AHB_CLK] = &gcc_ufs_phy_ahb_clk.clkr,
	[GCC_UFS_PHY_AXI_CLK] = &gcc_ufs_phy_axi_clk.clkr,
	[GCC_UFS_PHY_AXI_CLK_SRC] = &gcc_ufs_phy_axi_clk_src.clkr,
	[GCC_UFS_PHY_AXI_HW_CTL_CLK] = &gcc_ufs_phy_axi_hw_ctl_clk.clkr,
	[GCC_UFS_PHY_ICE_CORE_CLK] = &gcc_ufs_phy_ice_core_clk.clkr,
	[GCC_UFS_PHY_ICE_CORE_CLK_SRC] = &gcc_ufs_phy_ice_core_clk_src.clkr,
	[GCC_UFS_PHY_ICE_CORE_HW_CTL_CLK] =
		&gcc_ufs_phy_ice_core_hw_ctl_clk.clkr,
	[GCC_UFS_PHY_PHY_AUX_CLK] = &gcc_ufs_phy_phy_aux_clk.clkr,
	[GCC_UFS_PHY_PHY_AUX_CLK_SRC] = &gcc_ufs_phy_phy_aux_clk_src.clkr,
	[GCC_UFS_PHY_PHY_AUX_HW_CTL_CLK] = &gcc_ufs_phy_phy_aux_hw_ctl_clk.clkr,
	[GCC_UFS_PHY_RX_SYMBOL_0_CLK] = &gcc_ufs_phy_rx_symbol_0_clk.clkr,
	[GCC_UFS_PHY_RX_SYMBOL_1_CLK] = &gcc_ufs_phy_rx_symbol_1_clk.clkr,
	[GCC_UFS_PHY_TX_SYMBOL_0_CLK] = &gcc_ufs_phy_tx_symbol_0_clk.clkr,
	[GCC_UFS_PHY_UNIPRO_CORE_CLK] = &gcc_ufs_phy_unipro_core_clk.clkr,
	[GCC_UFS_PHY_UNIPRO_CORE_CLK_SRC] =
		&gcc_ufs_phy_unipro_core_clk_src.clkr,
	[GCC_UFS_PHY_UNIPRO_CORE_HW_CTL_CLK] =
		&gcc_ufs_phy_unipro_core_hw_ctl_clk.clkr,
	[GCC_USB30_PRIM_MASTER_CLK] = &gcc_usb30_prim_master_clk.clkr,
	[GCC_USB30_PRIM_MASTER_CLK_SRC] = &gcc_usb30_prim_master_clk_src.clkr,
	[GCC_USB30_PRIM_MOCK_UTMI_CLK] = &gcc_usb30_prim_mock_utmi_clk.clkr,
	[GCC_USB30_PRIM_MOCK_UTMI_CLK_SRC] =
		&gcc_usb30_prim_mock_utmi_clk_src.clkr,
	[GCC_USB30_PRIM_SLEEP_CLK] = &gcc_usb30_prim_sleep_clk.clkr,
	[GCC_USB30_SEC_MASTER_CLK] = &gcc_usb30_sec_master_clk.clkr,
	[GCC_USB30_SEC_MASTER_CLK_SRC] = &gcc_usb30_sec_master_clk_src.clkr,
	[GCC_USB30_SEC_MOCK_UTMI_CLK] = &gcc_usb30_sec_mock_utmi_clk.clkr,
	[GCC_USB30_SEC_MOCK_UTMI_CLK_SRC] =
		&gcc_usb30_sec_mock_utmi_clk_src.clkr,
	[GCC_USB30_SEC_SLEEP_CLK] = &gcc_usb30_sec_sleep_clk.clkr,
	[GCC_USB3_PRIM_CLKREF_CLK] = &gcc_usb3_prim_clkref_clk.clkr,
	[GCC_USB3_PRIM_PHY_AUX_CLK] = &gcc_usb3_prim_phy_aux_clk.clkr,
	[GCC_USB3_PRIM_PHY_AUX_CLK_SRC] = &gcc_usb3_prim_phy_aux_clk_src.clkr,
	[GCC_USB3_PRIM_PHY_COM_AUX_CLK] = &gcc_usb3_prim_phy_com_aux_clk.clkr,
	[GCC_USB3_PRIM_PHY_PIPE_CLK] = &gcc_usb3_prim_phy_pipe_clk.clkr,
	[GCC_USB3_SEC_CLKREF_CLK] = &gcc_usb3_sec_clkref_clk.clkr,
	[GCC_USB3_SEC_PHY_AUX_CLK] = &gcc_usb3_sec_phy_aux_clk.clkr,
	[GCC_USB3_SEC_PHY_AUX_CLK_SRC] = &gcc_usb3_sec_phy_aux_clk_src.clkr,
	[GCC_USB3_SEC_PHY_COM_AUX_CLK] = &gcc_usb3_sec_phy_com_aux_clk.clkr,
	[GCC_USB3_SEC_PHY_PIPE_CLK] = &gcc_usb3_sec_phy_pipe_clk.clkr,
	[GCC_VIDEO_AHB_CLK] = &gcc_video_ahb_clk.clkr,
	[GCC_VIDEO_AXI0_CLK] = &gcc_video_axi0_clk.clkr,
	[GCC_VIDEO_AXI1_CLK] = &gcc_video_axi1_clk.clkr,
	[GCC_VIDEO_AXIC_CLK] = &gcc_video_axic_clk.clkr,
	[GCC_VIDEO_XO_CLK] = &gcc_video_xo_clk.clkr,
	[GPLL0] = &gpll0.clkr,
	[GPLL0_OUT_EVEN] = &gpll0_out_even.clkr,
	[GPLL7] = &gpll7.clkr,
	[GPLL9] = &gpll9.clkr,
};

static const struct qcom_reset_map gcc_sm8150_resets[] = {
	[GCC_EMAC_BCR] = { 0x6000 },
	[GCC_GPU_BCR] = { 0x71000 },
	[GCC_MMSS_BCR] = { 0xb000 },
	[GCC_NPU_BCR] = { 0x4d000 },
	[GCC_PCIE_0_BCR] = { 0x6b000 },
	[GCC_PCIE_0_PHY_BCR] = { 0x6c01c },
	[GCC_PCIE_1_BCR] = { 0x8d000 },
	[GCC_PCIE_1_PHY_BCR] = { 0x8e01c },
	[GCC_PCIE_PHY_BCR] = { 0x6f000 },
	[GCC_PDM_BCR] = { 0x33000 },
	[GCC_PRNG_BCR] = { 0x34000 },
	[GCC_QSPI_BCR] = { 0x24008 },
	[GCC_QUPV3_WRAPPER_0_BCR] = { 0x17000 },
	[GCC_QUPV3_WRAPPER_1_BCR] = { 0x18000 },
	[GCC_QUPV3_WRAPPER_2_BCR] = { 0x1e000 },
	[GCC_QUSB2PHY_PRIM_BCR] = { 0x12000 },
	[GCC_QUSB2PHY_SEC_BCR] = { 0x12004 },
	[GCC_USB3_PHY_PRIM_BCR] = { 0x50000 },
	[GCC_USB3_DP_PHY_PRIM_BCR] = { 0x50008 },
	[GCC_USB3_PHY_SEC_BCR] = { 0x5000c },
	[GCC_USB3PHY_PHY_SEC_BCR] = { 0x50010 },
	[GCC_SDCC2_BCR] = { 0x14000 },
	[GCC_SDCC4_BCR] = { 0x16000 },
	[GCC_TSIF_BCR] = { 0x36000 },
	[GCC_UFS_CARD_BCR] = { 0x75000 },
	[GCC_UFS_PHY_BCR] = { 0x77000 },
	[GCC_USB30_PRIM_BCR] = { 0xf000 },
	[GCC_USB30_SEC_BCR] = { 0x10000 },
	[GCC_USB_PHY_CFG_AHB2PHY_BCR] = { 0x6a000 },
	[GCC_VIDEO_AXIC_CLK_BCR] = { 0xb02c, 2 },
	[GCC_VIDEO_AXI0_CLK_BCR] = { 0xb024, 2 },
	[GCC_VIDEO_AXI1_CLK_BCR] = { 0xb028, 2 },
};

static struct gdsc *gcc_sm8150_gdscs[] = {
	[EMAC_GDSC] = &emac_gdsc,
	[PCIE_0_GDSC] = &pcie_0_gdsc,
	[PCIE_1_GDSC] = &pcie_1_gdsc,
	[UFS_CARD_GDSC] = &ufs_card_gdsc,
	[UFS_PHY_GDSC] = &ufs_phy_gdsc,
	[USB30_PRIM_GDSC] = &usb30_prim_gdsc,
	[USB30_SEC_GDSC] = &usb30_sec_gdsc,
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
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_s0_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_s1_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_s2_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_s3_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_s4_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_s5_clk_src),
};

static const struct regmap_config gcc_sm8150_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x9c040,
	.fast_io	= true,
};

static const struct qcom_cc_desc gcc_sm8150_desc = {
	.config = &gcc_sm8150_regmap_config,
	.clks = gcc_sm8150_clocks,
	.num_clks = ARRAY_SIZE(gcc_sm8150_clocks),
	.resets = gcc_sm8150_resets,
	.num_resets = ARRAY_SIZE(gcc_sm8150_resets),
	.gdscs = gcc_sm8150_gdscs,
	.num_gdscs = ARRAY_SIZE(gcc_sm8150_gdscs),
};

static const struct of_device_id gcc_sm8150_match_table[] = {
	{ .compatible = "qcom,gcc-sm8150" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_sm8150_match_table);

static int gcc_sm8150_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret;

	regmap = qcom_cc_map(pdev, &gcc_sm8150_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/* Disable the GPLL0 active input to NPU and GPU via MISC registers */
	regmap_update_bits(regmap, 0x4d110, 0x3, 0x3);
	regmap_update_bits(regmap, 0x71028, 0x3, 0x3);

	ret = qcom_cc_register_rcg_dfs(regmap, gcc_dfs_clocks,
				       ARRAY_SIZE(gcc_dfs_clocks));
	if (ret)
		dev_err_probe(&pdev->dev, ret, "Failed to register with DFS!\n");

	return qcom_cc_really_probe(&pdev->dev, &gcc_sm8150_desc, regmap);
}

static struct platform_driver gcc_sm8150_driver = {
	.probe		= gcc_sm8150_probe,
	.driver		= {
		.name	= "gcc-sm8150",
		.of_match_table = gcc_sm8150_match_table,
	},
};

static int __init gcc_sm8150_init(void)
{
	return platform_driver_register(&gcc_sm8150_driver);
}
subsys_initcall(gcc_sm8150_init);

static void __exit gcc_sm8150_exit(void)
{
	platform_driver_unregister(&gcc_sm8150_driver);
}
module_exit(gcc_sm8150_exit);

MODULE_DESCRIPTION("QTI GCC SM8150 Driver");
MODULE_LICENSE("GPL v2");
