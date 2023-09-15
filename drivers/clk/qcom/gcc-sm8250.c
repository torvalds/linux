// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,gcc-sm8250.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "common.h"
#include "gdsc.h"
#include "reset.h"

enum {
	P_BI_TCXO,
	P_AUD_REF_CLK,
	P_GPLL0_OUT_EVEN,
	P_GPLL0_OUT_MAIN,
	P_GPLL4_OUT_MAIN,
	P_GPLL9_OUT_MAIN,
	P_SLEEP_CLK,
};

static struct clk_alpha_pll gpll0 = {
	.offset = 0x0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID],
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpll0",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ops,
		},
	},
};

static const struct clk_div_table post_div_table_gpll0_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll0_out_even = {
	.offset = 0x0,
	.post_div_shift = 8,
	.post_div_table = post_div_table_gpll0_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_gpll0_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll0_out_even",
		.parent_hws = (const struct clk_hw*[]){
			&gpll0.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_lucid_ops,
	},
};

static struct clk_alpha_pll gpll4 = {
	.offset = 0x76000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID],
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gpll4",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ops,
		},
	},
};

static struct clk_alpha_pll gpll9 = {
	.offset = 0x1c000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID],
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gpll9",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ops,
		},
	},
};

static const struct parent_map gcc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_0[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_even.clkr.hw },
};

static const struct clk_parent_data gcc_parent_data_0_ao[] = {
	{ .fw_name = "bi_tcxo_ao" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_SLEEP_CLK, 5 },
	{ P_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_1[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .fw_name = "sleep_clk" },
	{ .hw = &gpll0_out_even.clkr.hw },
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
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL9_OUT_MAIN, 2 },
	{ P_GPLL4_OUT_MAIN, 5 },
	{ P_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_4[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll9.clkr.hw },
	{ .hw = &gpll4.clkr.hw },
	{ .hw = &gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_5[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_AUD_REF_CLK, 2 },
	{ P_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_5[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .fw_name = "aud_ref_clk" },
	{ .hw = &gpll0_out_even.clkr.hw },
};

static const struct freq_tbl ftbl_gcc_cpuss_ahb_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_cpuss_ahb_clk_src = {
	.cmd_rcgr = 0x48010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_cpuss_ahb_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_cpuss_ahb_clk_src",
		.parent_data = gcc_parent_data_0_ao,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0_ao),
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
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
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
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
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
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcie_0_aux_clk_src[] = {
	F(9600000, P_BI_TCXO, 2, 0, 0),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie_0_aux_clk_src = {
	.cmd_rcgr = 0x6b038,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_pcie_0_aux_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_pcie_1_aux_clk_src = {
	.cmd_rcgr = 0x8d038,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_pcie_1_aux_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_pcie_2_aux_clk_src = {
	.cmd_rcgr = 0x6038,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_pcie_2_aux_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
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
		.parent_data = gcc_parent_data_0_ao,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0_ao),
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
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
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
	F(50000000, P_GPLL0_OUT_EVEN, 6, 0, 0),
	F(64000000, P_GPLL0_OUT_EVEN, 1, 16, 75),
	F(75000000, P_GPLL0_OUT_EVEN, 4, 0, 0),
	F(80000000, P_GPLL0_OUT_EVEN, 1, 4, 15),
	F(96000000, P_GPLL0_OUT_EVEN, 1, 8, 25),
	F(100000000, P_GPLL0_OUT_MAIN, 6, 0, 0),
	F(102400000, P_GPLL0_OUT_EVEN, 1, 128, 375),
	F(112000000, P_GPLL0_OUT_EVEN, 1, 28, 75),
	F(117964800, P_GPLL0_OUT_EVEN, 1, 6144, 15625),
	F(120000000, P_GPLL0_OUT_EVEN, 2.5, 0, 0),
	{ }
};

static struct clk_init_data gcc_qupv3_wrap0_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s0_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s0_clk_src = {
	.cmd_rcgr = 0x17010,
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
	.cmd_rcgr = 0x17140,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s1_clk_src_init,
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap0_s2_clk_src[] = {
	F(7372800, P_GPLL0_OUT_EVEN, 1, 384, 15625),
	F(14745600, P_GPLL0_OUT_EVEN, 1, 768, 15625),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(29491200, P_GPLL0_OUT_EVEN, 1, 1536, 15625),
	F(32000000, P_GPLL0_OUT_EVEN, 1, 8, 75),
	F(48000000, P_GPLL0_OUT_EVEN, 1, 4, 25),
	F(50000000, P_GPLL0_OUT_EVEN, 6, 0, 0),
	F(64000000, P_GPLL0_OUT_EVEN, 1, 16, 75),
	F(75000000, P_GPLL0_OUT_EVEN, 4, 0, 0),
	F(80000000, P_GPLL0_OUT_EVEN, 1, 4, 15),
	F(96000000, P_GPLL0_OUT_EVEN, 1, 8, 25),
	F(100000000, P_GPLL0_OUT_MAIN, 6, 0, 0),
	{ }
};

static struct clk_init_data gcc_qupv3_wrap0_s2_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s2_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s2_clk_src = {
	.cmd_rcgr = 0x17270,
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
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s3_clk_src = {
	.cmd_rcgr = 0x173a0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s3_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s4_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s4_clk_src = {
	.cmd_rcgr = 0x174d0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s4_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s5_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s5_clk_src = {
	.cmd_rcgr = 0x17600,
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
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s6_clk_src = {
	.cmd_rcgr = 0x17730,
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
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s7_clk_src = {
	.cmd_rcgr = 0x17860,
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
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s0_clk_src = {
	.cmd_rcgr = 0x18010,
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
	.cmd_rcgr = 0x18140,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s1_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s2_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s2_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s2_clk_src = {
	.cmd_rcgr = 0x18270,
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
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s3_clk_src = {
	.cmd_rcgr = 0x183a0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s3_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s4_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s4_clk_src = {
	.cmd_rcgr = 0x184d0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s4_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s5_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s5_clk_src = {
	.cmd_rcgr = 0x18600,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s5_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s0_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s0_clk_src = {
	.cmd_rcgr = 0x1e010,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_s0_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_s1_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s1_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s1_clk_src = {
	.cmd_rcgr = 0x1e140,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_s1_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_s2_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s2_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s2_clk_src = {
	.cmd_rcgr = 0x1e270,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_s2_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_s3_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s3_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s3_clk_src = {
	.cmd_rcgr = 0x1e3a0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_s3_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s4_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s4_clk_src = {
	.cmd_rcgr = 0x1e4d0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_s4_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s5_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s5_clk_src = {
	.cmd_rcgr = 0x1e600,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_s5_clk_src_init,
};

static const struct freq_tbl ftbl_gcc_sdcc2_apps_clk_src[] = {
	F(400000, P_BI_TCXO, 12, 1, 4),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(25000000, P_GPLL0_OUT_EVEN, 12, 0, 0),
	F(50000000, P_GPLL0_OUT_EVEN, 6, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 6, 0, 0),
	F(202000000, P_GPLL9_OUT_MAIN, 4, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc2_apps_clk_src = {
	.cmd_rcgr = 0x1400c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_sdcc2_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_sdcc2_apps_clk_src",
		.parent_data = gcc_parent_data_4,
		.num_parents = ARRAY_SIZE(gcc_parent_data_4),
		.flags = CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_floor_ops,
	},
};

static const struct freq_tbl ftbl_gcc_sdcc4_apps_clk_src[] = {
	F(400000, P_BI_TCXO, 12, 1, 4),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(25000000, P_GPLL0_OUT_EVEN, 12, 0, 0),
	F(50000000, P_GPLL0_OUT_EVEN, 6, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 6, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc4_apps_clk_src = {
	.cmd_rcgr = 0x1600c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_sdcc4_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_sdcc4_apps_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
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
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_tsif_ref_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_tsif_ref_clk_src",
		.parent_data = gcc_parent_data_5,
		.num_parents = ARRAY_SIZE(gcc_parent_data_5),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_ufs_card_axi_clk_src[] = {
	F(25000000, P_GPLL0_OUT_EVEN, 12, 0, 0),
	F(50000000, P_GPLL0_OUT_EVEN, 6, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 6, 0, 0),
	F(200000000, P_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_card_axi_clk_src = {
	.cmd_rcgr = 0x75024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_card_axi_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_card_axi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
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
	.cmd_rcgr = 0x7506c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_card_ice_core_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_card_ice_core_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_ufs_card_phy_aux_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_card_phy_aux_clk_src = {
	.cmd_rcgr = 0x750a0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_ufs_card_phy_aux_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_card_phy_aux_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
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
	.cmd_rcgr = 0x75084,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_card_unipro_core_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_card_unipro_core_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
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
	.cmd_rcgr = 0x77024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_phy_axi_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_phy_axi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_ufs_phy_ice_core_clk_src = {
	.cmd_rcgr = 0x7706c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_card_ice_core_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_phy_ice_core_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_ufs_phy_phy_aux_clk_src = {
	.cmd_rcgr = 0x770a0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_phy_phy_aux_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_ufs_phy_unipro_core_clk_src = {
	.cmd_rcgr = 0x77084,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_card_ice_core_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_phy_unipro_core_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
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
	.cmd_rcgr = 0xf020,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb30_prim_master_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_usb30_prim_master_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_usb30_prim_mock_utmi_clk_src = {
	.cmd_rcgr = 0xf038,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_card_phy_aux_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_usb30_prim_mock_utmi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_usb30_sec_master_clk_src = {
	.cmd_rcgr = 0x10020,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb30_prim_master_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_usb30_sec_master_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_usb30_sec_mock_utmi_clk_src = {
	.cmd_rcgr = 0x10038,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_card_phy_aux_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_usb30_sec_mock_utmi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_usb3_prim_phy_aux_clk_src = {
	.cmd_rcgr = 0xf064,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_ufs_card_phy_aux_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_usb3_prim_phy_aux_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_usb3_sec_phy_aux_clk_src = {
	.cmd_rcgr = 0x10064,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_ufs_card_phy_aux_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_usb3_sec_phy_aux_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div gcc_cpuss_ahb_postdiv_clk_src = {
	.reg = 0x48028,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "gcc_cpuss_ahb_postdiv_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_cpuss_ahb_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_usb30_prim_mock_utmi_postdiv_clk_src = {
	.reg = 0xf050,
	.shift = 0,
	.width = 2,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "gcc_usb30_prim_mock_utmi_postdiv_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_usb30_prim_mock_utmi_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_usb30_sec_mock_utmi_postdiv_clk_src = {
	.reg = 0x10050,
	.shift = 0,
	.width = 2,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "gcc_usb30_sec_mock_utmi_postdiv_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_usb30_sec_mock_utmi_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch gcc_aggre_noc_pcie_tbu_clk = {
	.halt_reg = 0x9000c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9000c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_aggre_noc_pcie_tbu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_ufs_card_axi_clk = {
	.halt_reg = 0x750cc,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x750cc,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x750cc,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_aggre_ufs_card_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_card_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_ufs_phy_axi_clk = {
	.halt_reg = 0x770cc,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x770cc,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x770cc,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
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

static struct clk_branch gcc_aggre_usb3_prim_axi_clk = {
	.halt_reg = 0xf080,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0xf080,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
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

static struct clk_branch gcc_aggre_usb3_sec_axi_clk = {
	.halt_reg = 0x10080,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x10080,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_aggre_usb3_sec_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb30_sec_master_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_boot_rom_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_hf_axi_clk = {
	.halt_reg = 0xb02c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0xb02c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camera_hf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_sf_axi_clk = {
	.halt_reg = 0xb030,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0xb030,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camera_sf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_xo_clk = {
	.halt_reg = 0xb040,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb040,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camera_xo_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_usb3_prim_axi_clk = {
	.halt_reg = 0xf07c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0xf07c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
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

static struct clk_branch gcc_cfg_noc_usb3_sec_axi_clk = {
	.halt_reg = 0x1007c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x1007c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_cfg_noc_usb3_sec_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb30_sec_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cpuss_ahb_clk = {
	.halt_reg = 0x48000,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(21),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_cpuss_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_cpuss_ahb_postdiv_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_IS_CRITICAL | CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cpuss_rbcpr_clk = {
	.halt_reg = 0x48004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x48004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_cpuss_rbcpr_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ddrss_gpu_axi_clk = {
	.halt_reg = 0x71154,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x71154,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ddrss_gpu_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ddrss_pcie_sf_tbu_clk = {
	.halt_reg = 0x8d058,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x8d058,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ddrss_pcie_sf_tbu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_hf_axi_clk = {
	.halt_reg = 0xb034,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0xb034,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_disp_hf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_sf_axi_clk = {
	.halt_reg = 0xb038,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0xb038,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_disp_sf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_xo_clk = {
	.halt_reg = 0xb044,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb044,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_disp_xo_clk",
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
	.halt_reg = 0x65000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x65000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
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
	.halt_reg = 0x66000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x66000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
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

static struct clk_branch gcc_gpu_gpll0_clk_src = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(15),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gpu_gpll0_clk_src",
			.parent_hws = (const struct clk_hw*[]){
				&gpll0.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gpu_gpll0_div_clk_src",
			.parent_hws = (const struct clk_hw*[]){
				&gpll0_out_even.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_iref_en = {
	.halt_reg = 0x8c014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gpu_iref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_memnoc_gfx_clk = {
	.halt_reg = 0x7100c,
	.halt_check = BRANCH_HALT_VOTED,
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

static struct clk_branch gcc_npu_axi_clk = {
	.halt_reg = 0x4d008,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4d008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_npu_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_npu_bwmon_axi_clk = {
	.halt_reg = 0x73008,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x73008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_npu_bwmon_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_npu_bwmon_cfg_ahb_clk = {
	.halt_reg = 0x73004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x73004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_npu_bwmon_cfg_ahb_clk",
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
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_npu_dma_clk = {
	.halt_reg = 0x4d00c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4d00c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_npu_dma_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_npu_gpll0_clk_src = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(18),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_npu_gpll0_clk_src",
			.parent_hws = (const struct clk_hw*[]){
				&gpll0.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_npu_gpll0_div_clk_src = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(19),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_npu_gpll0_div_clk_src",
			.parent_hws = (const struct clk_hw*[]){
				&gpll0_out_even.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
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
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_phy_refgen_clk_src.clkr.hw,
			},
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
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_phy_refgen_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie2_phy_refgen_clk = {
	.halt_reg = 0x6f034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6f034,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie2_phy_refgen_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_phy_refgen_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_aux_clk = {
	.halt_reg = 0x6b028,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(3),
		.hw.init = &(struct clk_init_data){
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
	.halt_reg = 0x6b024,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x6b024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_0_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_mstr_axi_clk = {
	.halt_reg = 0x6b01c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_0_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_pipe_clk = {
	.halt_reg = 0x6b02c,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x52008,
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
		.enable_reg = 0x52008,
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
		.enable_reg = 0x52008,
		.enable_mask = BIT(5),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_0_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_aux_clk = {
	.halt_reg = 0x8d028,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(29),
		.hw.init = &(struct clk_init_data){
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
	.halt_reg = 0x8d024,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x8d024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(28),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_1_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_mstr_axi_clk = {
	.halt_reg = 0x8d01c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(27),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_1_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_pipe_clk = {
	.halt_reg = 0x8d02c,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x52000,
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
		.enable_reg = 0x52000,
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
		.enable_reg = 0x52000,
		.enable_mask = BIT(25),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_1_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_aux_clk = {
	.halt_reg = 0x6028,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(14),
		.hw.init = &(struct clk_init_data){
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
	.halt_reg = 0x6024,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x6024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(13),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_2_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_mstr_axi_clk = {
	.halt_reg = 0x601c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(12),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_2_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_pipe_clk = {
	.halt_reg = 0x602c,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(15),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_2_pipe_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_slv_axi_clk = {
	.halt_reg = 0x6014,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x6014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_2_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_slv_q2a_axi_clk = {
	.halt_reg = 0x6010,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(10),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_2_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_mdm_clkref_en = {
	.halt_reg = 0x8c00c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c00c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_mdm_clkref_en",
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
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_0_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_wifi_clkref_en = {
	.halt_reg = 0x8c004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_wifi_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_wigig_clkref_en = {
	.halt_reg = 0x8c008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_wigig_clkref_en",
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
		.enable_reg = 0x52000,
		.enable_mask = BIT(13),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_prng_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_camera_nrt_ahb_clk = {
	.halt_reg = 0xb018,
	.halt_check = BRANCH_HALT_VOTED,
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
	.halt_check = BRANCH_HALT_VOTED,
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
	.halt_check = BRANCH_HALT_VOTED,
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
	.halt_check = BRANCH_HALT_VOTED,
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
	.halt_check = BRANCH_HALT_VOTED,
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

static struct clk_branch gcc_qupv3_wrap0_core_2x_clk = {
	.halt_reg = 0x23008,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap0_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_core_clk = {
	.halt_reg = 0x23000,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(8),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap0_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s0_clk = {
	.halt_reg = 0x1700c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(10),
		.hw.init = &(struct clk_init_data){
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
	.halt_reg = 0x1713c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
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
	.halt_reg = 0x1726c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(12),
		.hw.init = &(struct clk_init_data){
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
	.halt_reg = 0x1739c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(13),
		.hw.init = &(struct clk_init_data){
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
	.halt_reg = 0x174cc,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(14),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s4_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap0_s4_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s5_clk = {
	.halt_reg = 0x175fc,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(15),
		.hw.init = &(struct clk_init_data){
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
	.halt_reg = 0x1772c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(16),
		.hw.init = &(struct clk_init_data){
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

static struct clk_branch gcc_qupv3_wrap0_s7_clk = {
	.halt_reg = 0x1785c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(17),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s7_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap0_s7_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_core_2x_clk = {
	.halt_reg = 0x23140,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(18),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap1_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_core_clk = {
	.halt_reg = 0x23138,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(19),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap1_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s0_clk = {
	.halt_reg = 0x1800c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(22),
		.hw.init = &(struct clk_init_data){
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
	.halt_reg = 0x1813c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(23),
		.hw.init = &(struct clk_init_data){
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
	.halt_reg = 0x1826c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(24),
		.hw.init = &(struct clk_init_data){
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
	.halt_reg = 0x1839c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(25),
		.hw.init = &(struct clk_init_data){
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
	.halt_reg = 0x184cc,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(26),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap1_s4_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap1_s4_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s5_clk = {
	.halt_reg = 0x185fc,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(27),
		.hw.init = &(struct clk_init_data){
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

static struct clk_branch gcc_qupv3_wrap2_core_2x_clk = {
	.halt_reg = 0x23278,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(3),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap2_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_core_clk = {
	.halt_reg = 0x23270,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap2_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s0_clk = {
	.halt_reg = 0x1e00c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap2_s0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap2_s0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s1_clk = {
	.halt_reg = 0x1e13c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(5),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap2_s1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap2_s1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s2_clk = {
	.halt_reg = 0x1e26c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(6),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap2_s2_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap2_s2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s3_clk = {
	.halt_reg = 0x1e39c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(7),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap2_s3_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap2_s3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s4_clk = {
	.halt_reg = 0x1e4cc,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(8),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap2_s4_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap2_s4_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s5_clk = {
	.halt_reg = 0x1e5fc,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap2_s5_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap2_s5_clk_src.clkr.hw,
			},
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
		.enable_reg = 0x52008,
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
		.enable_reg = 0x52008,
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
		.enable_reg = 0x52008,
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
		.enable_reg = 0x52008,
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
		.enable_reg = 0x52010,
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
		.enable_reg = 0x52010,
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
			.parent_hws = (const struct clk_hw*[]){
				&gcc_sdcc2_apps_clk_src.clkr.hw,
			},
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
			.parent_hws = (const struct clk_hw*[]){
				&gcc_sdcc4_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_tsif_ahb_clk = {
	.halt_reg = 0x36004,
	.halt_check = BRANCH_HALT_VOTED,
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
			.parent_hws = (const struct clk_hw*[]){
				&gcc_tsif_ref_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_1x_clkref_en = {
	.halt_reg = 0x8c000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_1x_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_ahb_clk = {
	.halt_reg = 0x75018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x75018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x75018,
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
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_card_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_ice_core_clk = {
	.halt_reg = 0x75064,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x75064,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x75064,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_card_ice_core_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_card_ice_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_phy_aux_clk = {
	.halt_reg = 0x7509c,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x7509c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7509c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_card_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_card_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_rx_symbol_0_clk = {
	.halt_reg = 0x75020,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x75020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_card_rx_symbol_0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_rx_symbol_1_clk = {
	.halt_reg = 0x750b8,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x750b8,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_card_rx_symbol_1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_tx_symbol_0_clk = {
	.halt_reg = 0x7501c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x7501c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_card_tx_symbol_0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_unipro_core_clk = {
	.halt_reg = 0x7505c,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x7505c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7505c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_card_unipro_core_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_card_unipro_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_ahb_clk = {
	.halt_reg = 0x77018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x77018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x77018,
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
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_ice_core_clk = {
	.halt_reg = 0x77064,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x77064,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x77064,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
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

static struct clk_branch gcc_ufs_phy_phy_aux_clk = {
	.halt_reg = 0x7709c,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x7709c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7709c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
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

static struct clk_branch gcc_ufs_phy_rx_symbol_0_clk = {
	.halt_reg = 0x77020,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x77020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_rx_symbol_0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_rx_symbol_1_clk = {
	.halt_reg = 0x770b8,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x770b8,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_rx_symbol_1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_tx_symbol_0_clk = {
	.halt_reg = 0x7701c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x7701c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_tx_symbol_0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_unipro_core_clk = {
	.halt_reg = 0x7705c,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x7705c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7705c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
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

static struct clk_branch gcc_usb30_prim_master_clk = {
	.halt_reg = 0xf010,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0xf010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
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
	.halt_reg = 0xf01c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf01c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
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
	.halt_reg = 0xf018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb30_prim_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_sec_master_clk = {
	.halt_reg = 0x10010,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x10010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb30_sec_master_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb30_sec_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_sec_mock_utmi_clk = {
	.halt_reg = 0x1001c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1001c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb30_sec_mock_utmi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb30_sec_mock_utmi_postdiv_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_sec_sleep_clk = {
	.halt_reg = 0x10018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb30_sec_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_phy_aux_clk = {
	.halt_reg = 0xf054,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf054,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
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
	.halt_reg = 0xf058,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf058,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
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
	.halt_reg = 0xf05c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0xf05c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_prim_phy_pipe_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_sec_clkref_en = {
	.halt_reg = 0x8c010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_sec_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_sec_phy_aux_clk = {
	.halt_reg = 0x10054,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10054,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_sec_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb3_sec_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_sec_phy_com_aux_clk = {
	.halt_reg = 0x10058,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10058,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_sec_phy_com_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb3_sec_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_sec_phy_pipe_clk = {
	.halt_reg = 0x1005c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x1005c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_sec_phy_pipe_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_axi0_clk = {
	.halt_reg = 0xb024,
	.halt_check = BRANCH_HALT_VOTED,
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
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0xb028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_video_axi1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_xo_clk = {
	.halt_reg = 0xb03c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb03c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_video_xo_clk",
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
};

static struct gdsc pcie_1_gdsc = {
	.gdscr = 0x8d004,
	.pd = {
		.name = "pcie_1_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc pcie_2_gdsc = {
	.gdscr = 0x6004,
	.pd = {
		.name = "pcie_2_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc ufs_card_gdsc = {
	.gdscr = 0x75004,
	.pd = {
		.name = "ufs_card_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc ufs_phy_gdsc = {
	.gdscr = 0x77004,
	.pd = {
		.name = "ufs_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc usb30_prim_gdsc = {
	.gdscr = 0xf004,
	.pd = {
		.name = "usb30_prim_gdsc",
	},
	.pwrsts = PWRSTS_RET_ON,
};

static struct gdsc usb30_sec_gdsc = {
	.gdscr = 0x10004,
	.pd = {
		.name = "usb30_sec_gdsc",
	},
	.pwrsts = PWRSTS_RET_ON,
};

static struct gdsc hlos1_vote_mmnoc_mmu_tbu_hf0_gdsc = {
	.gdscr = 0x7d050,
	.pd = {
		.name = "hlos1_vote_mmnoc_mmu_tbu_hf0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc hlos1_vote_mmnoc_mmu_tbu_hf1_gdsc = {
	.gdscr = 0x7d058,
	.pd = {
		.name = "hlos1_vote_mmnoc_mmu_tbu_hf1_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc hlos1_vote_mmnoc_mmu_tbu_sf0_gdsc = {
	.gdscr = 0x7d054,
	.pd = {
		.name = "hlos1_vote_mmnoc_mmu_tbu_sf0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc hlos1_vote_mmnoc_mmu_tbu_sf1_gdsc = {
	.gdscr = 0x7d06c,
	.pd = {
		.name = "hlos1_vote_mmnoc_mmu_tbu_sf1_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct clk_regmap *gcc_sm8250_clocks[] = {
	[GCC_AGGRE_NOC_PCIE_TBU_CLK] = &gcc_aggre_noc_pcie_tbu_clk.clkr,
	[GCC_AGGRE_UFS_CARD_AXI_CLK] = &gcc_aggre_ufs_card_axi_clk.clkr,
	[GCC_AGGRE_UFS_PHY_AXI_CLK] = &gcc_aggre_ufs_phy_axi_clk.clkr,
	[GCC_AGGRE_USB3_PRIM_AXI_CLK] = &gcc_aggre_usb3_prim_axi_clk.clkr,
	[GCC_AGGRE_USB3_SEC_AXI_CLK] = &gcc_aggre_usb3_sec_axi_clk.clkr,
	[GCC_BOOT_ROM_AHB_CLK] = &gcc_boot_rom_ahb_clk.clkr,
	[GCC_CAMERA_HF_AXI_CLK] = &gcc_camera_hf_axi_clk.clkr,
	[GCC_CAMERA_SF_AXI_CLK] = &gcc_camera_sf_axi_clk.clkr,
	[GCC_CAMERA_XO_CLK] = &gcc_camera_xo_clk.clkr,
	[GCC_CFG_NOC_USB3_PRIM_AXI_CLK] = &gcc_cfg_noc_usb3_prim_axi_clk.clkr,
	[GCC_CFG_NOC_USB3_SEC_AXI_CLK] = &gcc_cfg_noc_usb3_sec_axi_clk.clkr,
	[GCC_CPUSS_AHB_CLK] = &gcc_cpuss_ahb_clk.clkr,
	[GCC_CPUSS_AHB_CLK_SRC] = &gcc_cpuss_ahb_clk_src.clkr,
	[GCC_CPUSS_AHB_POSTDIV_CLK_SRC] = &gcc_cpuss_ahb_postdiv_clk_src.clkr,
	[GCC_CPUSS_RBCPR_CLK] = &gcc_cpuss_rbcpr_clk.clkr,
	[GCC_DDRSS_GPU_AXI_CLK] = &gcc_ddrss_gpu_axi_clk.clkr,
	[GCC_DDRSS_PCIE_SF_TBU_CLK] = &gcc_ddrss_pcie_sf_tbu_clk.clkr,
	[GCC_DISP_HF_AXI_CLK] = &gcc_disp_hf_axi_clk.clkr,
	[GCC_DISP_SF_AXI_CLK] = &gcc_disp_sf_axi_clk.clkr,
	[GCC_DISP_XO_CLK] = &gcc_disp_xo_clk.clkr,
	[GCC_GP1_CLK] = &gcc_gp1_clk.clkr,
	[GCC_GP1_CLK_SRC] = &gcc_gp1_clk_src.clkr,
	[GCC_GP2_CLK] = &gcc_gp2_clk.clkr,
	[GCC_GP2_CLK_SRC] = &gcc_gp2_clk_src.clkr,
	[GCC_GP3_CLK] = &gcc_gp3_clk.clkr,
	[GCC_GP3_CLK_SRC] = &gcc_gp3_clk_src.clkr,
	[GCC_GPU_GPLL0_CLK_SRC] = &gcc_gpu_gpll0_clk_src.clkr,
	[GCC_GPU_GPLL0_DIV_CLK_SRC] = &gcc_gpu_gpll0_div_clk_src.clkr,
	[GCC_GPU_IREF_EN] = &gcc_gpu_iref_en.clkr,
	[GCC_GPU_MEMNOC_GFX_CLK] = &gcc_gpu_memnoc_gfx_clk.clkr,
	[GCC_GPU_SNOC_DVM_GFX_CLK] = &gcc_gpu_snoc_dvm_gfx_clk.clkr,
	[GCC_NPU_AXI_CLK] = &gcc_npu_axi_clk.clkr,
	[GCC_NPU_BWMON_AXI_CLK] = &gcc_npu_bwmon_axi_clk.clkr,
	[GCC_NPU_BWMON_CFG_AHB_CLK] = &gcc_npu_bwmon_cfg_ahb_clk.clkr,
	[GCC_NPU_CFG_AHB_CLK] = &gcc_npu_cfg_ahb_clk.clkr,
	[GCC_NPU_DMA_CLK] = &gcc_npu_dma_clk.clkr,
	[GCC_NPU_GPLL0_CLK_SRC] = &gcc_npu_gpll0_clk_src.clkr,
	[GCC_NPU_GPLL0_DIV_CLK_SRC] = &gcc_npu_gpll0_div_clk_src.clkr,
	[GCC_PCIE0_PHY_REFGEN_CLK] = &gcc_pcie0_phy_refgen_clk.clkr,
	[GCC_PCIE1_PHY_REFGEN_CLK] = &gcc_pcie1_phy_refgen_clk.clkr,
	[GCC_PCIE2_PHY_REFGEN_CLK] = &gcc_pcie2_phy_refgen_clk.clkr,
	[GCC_PCIE_0_AUX_CLK] = &gcc_pcie_0_aux_clk.clkr,
	[GCC_PCIE_0_AUX_CLK_SRC] = &gcc_pcie_0_aux_clk_src.clkr,
	[GCC_PCIE_0_CFG_AHB_CLK] = &gcc_pcie_0_cfg_ahb_clk.clkr,
	[GCC_PCIE_0_MSTR_AXI_CLK] = &gcc_pcie_0_mstr_axi_clk.clkr,
	[GCC_PCIE_0_PIPE_CLK] = &gcc_pcie_0_pipe_clk.clkr,
	[GCC_PCIE_0_SLV_AXI_CLK] = &gcc_pcie_0_slv_axi_clk.clkr,
	[GCC_PCIE_0_SLV_Q2A_AXI_CLK] = &gcc_pcie_0_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_1_AUX_CLK] = &gcc_pcie_1_aux_clk.clkr,
	[GCC_PCIE_1_AUX_CLK_SRC] = &gcc_pcie_1_aux_clk_src.clkr,
	[GCC_PCIE_1_CFG_AHB_CLK] = &gcc_pcie_1_cfg_ahb_clk.clkr,
	[GCC_PCIE_1_MSTR_AXI_CLK] = &gcc_pcie_1_mstr_axi_clk.clkr,
	[GCC_PCIE_1_PIPE_CLK] = &gcc_pcie_1_pipe_clk.clkr,
	[GCC_PCIE_1_SLV_AXI_CLK] = &gcc_pcie_1_slv_axi_clk.clkr,
	[GCC_PCIE_1_SLV_Q2A_AXI_CLK] = &gcc_pcie_1_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_2_AUX_CLK] = &gcc_pcie_2_aux_clk.clkr,
	[GCC_PCIE_2_AUX_CLK_SRC] = &gcc_pcie_2_aux_clk_src.clkr,
	[GCC_PCIE_2_CFG_AHB_CLK] = &gcc_pcie_2_cfg_ahb_clk.clkr,
	[GCC_PCIE_2_MSTR_AXI_CLK] = &gcc_pcie_2_mstr_axi_clk.clkr,
	[GCC_PCIE_2_PIPE_CLK] = &gcc_pcie_2_pipe_clk.clkr,
	[GCC_PCIE_2_SLV_AXI_CLK] = &gcc_pcie_2_slv_axi_clk.clkr,
	[GCC_PCIE_2_SLV_Q2A_AXI_CLK] = &gcc_pcie_2_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_MDM_CLKREF_EN] = &gcc_pcie_mdm_clkref_en.clkr,
	[GCC_PCIE_PHY_AUX_CLK] = &gcc_pcie_phy_aux_clk.clkr,
	[GCC_PCIE_PHY_REFGEN_CLK_SRC] = &gcc_pcie_phy_refgen_clk_src.clkr,
	[GCC_PCIE_WIFI_CLKREF_EN] = &gcc_pcie_wifi_clkref_en.clkr,
	[GCC_PCIE_WIGIG_CLKREF_EN] = &gcc_pcie_wigig_clkref_en.clkr,
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
	[GCC_QUPV3_WRAP2_CORE_2X_CLK] = &gcc_qupv3_wrap2_core_2x_clk.clkr,
	[GCC_QUPV3_WRAP2_CORE_CLK] = &gcc_qupv3_wrap2_core_clk.clkr,
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
	[GCC_UFS_1X_CLKREF_EN] = &gcc_ufs_1x_clkref_en.clkr,
	[GCC_UFS_CARD_AHB_CLK] = &gcc_ufs_card_ahb_clk.clkr,
	[GCC_UFS_CARD_AXI_CLK] = &gcc_ufs_card_axi_clk.clkr,
	[GCC_UFS_CARD_AXI_CLK_SRC] = &gcc_ufs_card_axi_clk_src.clkr,
	[GCC_UFS_CARD_ICE_CORE_CLK] = &gcc_ufs_card_ice_core_clk.clkr,
	[GCC_UFS_CARD_ICE_CORE_CLK_SRC] = &gcc_ufs_card_ice_core_clk_src.clkr,
	[GCC_UFS_CARD_PHY_AUX_CLK] = &gcc_ufs_card_phy_aux_clk.clkr,
	[GCC_UFS_CARD_PHY_AUX_CLK_SRC] = &gcc_ufs_card_phy_aux_clk_src.clkr,
	[GCC_UFS_CARD_RX_SYMBOL_0_CLK] = &gcc_ufs_card_rx_symbol_0_clk.clkr,
	[GCC_UFS_CARD_RX_SYMBOL_1_CLK] = &gcc_ufs_card_rx_symbol_1_clk.clkr,
	[GCC_UFS_CARD_TX_SYMBOL_0_CLK] = &gcc_ufs_card_tx_symbol_0_clk.clkr,
	[GCC_UFS_CARD_UNIPRO_CORE_CLK] = &gcc_ufs_card_unipro_core_clk.clkr,
	[GCC_UFS_CARD_UNIPRO_CORE_CLK_SRC] =
		&gcc_ufs_card_unipro_core_clk_src.clkr,
	[GCC_UFS_PHY_AHB_CLK] = &gcc_ufs_phy_ahb_clk.clkr,
	[GCC_UFS_PHY_AXI_CLK] = &gcc_ufs_phy_axi_clk.clkr,
	[GCC_UFS_PHY_AXI_CLK_SRC] = &gcc_ufs_phy_axi_clk_src.clkr,
	[GCC_UFS_PHY_ICE_CORE_CLK] = &gcc_ufs_phy_ice_core_clk.clkr,
	[GCC_UFS_PHY_ICE_CORE_CLK_SRC] = &gcc_ufs_phy_ice_core_clk_src.clkr,
	[GCC_UFS_PHY_PHY_AUX_CLK] = &gcc_ufs_phy_phy_aux_clk.clkr,
	[GCC_UFS_PHY_PHY_AUX_CLK_SRC] = &gcc_ufs_phy_phy_aux_clk_src.clkr,
	[GCC_UFS_PHY_RX_SYMBOL_0_CLK] = &gcc_ufs_phy_rx_symbol_0_clk.clkr,
	[GCC_UFS_PHY_RX_SYMBOL_1_CLK] = &gcc_ufs_phy_rx_symbol_1_clk.clkr,
	[GCC_UFS_PHY_TX_SYMBOL_0_CLK] = &gcc_ufs_phy_tx_symbol_0_clk.clkr,
	[GCC_UFS_PHY_UNIPRO_CORE_CLK] = &gcc_ufs_phy_unipro_core_clk.clkr,
	[GCC_UFS_PHY_UNIPRO_CORE_CLK_SRC] =
		&gcc_ufs_phy_unipro_core_clk_src.clkr,
	[GCC_USB30_PRIM_MASTER_CLK] = &gcc_usb30_prim_master_clk.clkr,
	[GCC_USB30_PRIM_MASTER_CLK_SRC] = &gcc_usb30_prim_master_clk_src.clkr,
	[GCC_USB30_PRIM_MOCK_UTMI_CLK] = &gcc_usb30_prim_mock_utmi_clk.clkr,
	[GCC_USB30_PRIM_MOCK_UTMI_CLK_SRC] =
		&gcc_usb30_prim_mock_utmi_clk_src.clkr,
	[GCC_USB30_PRIM_MOCK_UTMI_POSTDIV_CLK_SRC] =
		&gcc_usb30_prim_mock_utmi_postdiv_clk_src.clkr,
	[GCC_USB30_PRIM_SLEEP_CLK] = &gcc_usb30_prim_sleep_clk.clkr,
	[GCC_USB30_SEC_MASTER_CLK] = &gcc_usb30_sec_master_clk.clkr,
	[GCC_USB30_SEC_MASTER_CLK_SRC] = &gcc_usb30_sec_master_clk_src.clkr,
	[GCC_USB30_SEC_MOCK_UTMI_CLK] = &gcc_usb30_sec_mock_utmi_clk.clkr,
	[GCC_USB30_SEC_MOCK_UTMI_CLK_SRC] =
		&gcc_usb30_sec_mock_utmi_clk_src.clkr,
	[GCC_USB30_SEC_MOCK_UTMI_POSTDIV_CLK_SRC] =
		&gcc_usb30_sec_mock_utmi_postdiv_clk_src.clkr,
	[GCC_USB30_SEC_SLEEP_CLK] = &gcc_usb30_sec_sleep_clk.clkr,
	[GCC_USB3_PRIM_PHY_AUX_CLK] = &gcc_usb3_prim_phy_aux_clk.clkr,
	[GCC_USB3_PRIM_PHY_AUX_CLK_SRC] = &gcc_usb3_prim_phy_aux_clk_src.clkr,
	[GCC_USB3_PRIM_PHY_COM_AUX_CLK] = &gcc_usb3_prim_phy_com_aux_clk.clkr,
	[GCC_USB3_PRIM_PHY_PIPE_CLK] = &gcc_usb3_prim_phy_pipe_clk.clkr,
	[GCC_USB3_SEC_CLKREF_EN] = &gcc_usb3_sec_clkref_en.clkr,
	[GCC_USB3_SEC_PHY_AUX_CLK] = &gcc_usb3_sec_phy_aux_clk.clkr,
	[GCC_USB3_SEC_PHY_AUX_CLK_SRC] = &gcc_usb3_sec_phy_aux_clk_src.clkr,
	[GCC_USB3_SEC_PHY_COM_AUX_CLK] = &gcc_usb3_sec_phy_com_aux_clk.clkr,
	[GCC_USB3_SEC_PHY_PIPE_CLK] = &gcc_usb3_sec_phy_pipe_clk.clkr,
	[GCC_VIDEO_AXI0_CLK] = &gcc_video_axi0_clk.clkr,
	[GCC_VIDEO_AXI1_CLK] = &gcc_video_axi1_clk.clkr,
	[GCC_VIDEO_XO_CLK] = &gcc_video_xo_clk.clkr,
	[GPLL0] = &gpll0.clkr,
	[GPLL0_OUT_EVEN] = &gpll0_out_even.clkr,
	[GPLL4] = &gpll4.clkr,
	[GPLL9] = &gpll9.clkr,
};

static struct gdsc *gcc_sm8250_gdscs[] = {
	[PCIE_0_GDSC] = &pcie_0_gdsc,
	[PCIE_1_GDSC] = &pcie_1_gdsc,
	[PCIE_2_GDSC] = &pcie_2_gdsc,
	[UFS_CARD_GDSC] = &ufs_card_gdsc,
	[UFS_PHY_GDSC] = &ufs_phy_gdsc,
	[USB30_PRIM_GDSC] = &usb30_prim_gdsc,
	[USB30_SEC_GDSC] = &usb30_sec_gdsc,
	[HLOS1_VOTE_MMNOC_MMU_TBU_HF0_GDSC] =
					&hlos1_vote_mmnoc_mmu_tbu_hf0_gdsc,
	[HLOS1_VOTE_MMNOC_MMU_TBU_HF1_GDSC] =
					&hlos1_vote_mmnoc_mmu_tbu_hf1_gdsc,
	[HLOS1_VOTE_MMNOC_MMU_TBU_SF0_GDSC] =
					&hlos1_vote_mmnoc_mmu_tbu_sf0_gdsc,
	[HLOS1_VOTE_MMNOC_MMU_TBU_SF1_GDSC] =
					&hlos1_vote_mmnoc_mmu_tbu_sf1_gdsc,
};

static const struct qcom_reset_map gcc_sm8250_resets[] = {
	[GCC_GPU_BCR] = { 0x71000 },
	[GCC_MMSS_BCR] = { 0xb000 },
	[GCC_NPU_BWMON_BCR] = { 0x73000 },
	[GCC_NPU_BCR] = { 0x4d000 },
	[GCC_PCIE_0_BCR] = { 0x6b000 },
	[GCC_PCIE_0_LINK_DOWN_BCR] = { 0x6c014 },
	[GCC_PCIE_0_NOCSR_COM_PHY_BCR] = { 0x6c020 },
	[GCC_PCIE_0_PHY_BCR] = { 0x6c01c },
	[GCC_PCIE_0_PHY_NOCSR_COM_PHY_BCR] = { 0x6c028 },
	[GCC_PCIE_1_BCR] = { 0x8d000 },
	[GCC_PCIE_1_LINK_DOWN_BCR] = { 0x8e014 },
	[GCC_PCIE_1_NOCSR_COM_PHY_BCR] = { 0x8e020 },
	[GCC_PCIE_1_PHY_BCR] = { 0x8e01c },
	[GCC_PCIE_1_PHY_NOCSR_COM_PHY_BCR] = { 0x8e000 },
	[GCC_PCIE_2_BCR] = { 0x6000 },
	[GCC_PCIE_2_LINK_DOWN_BCR] = { 0x1f014 },
	[GCC_PCIE_2_NOCSR_COM_PHY_BCR] = { 0x1f020 },
	[GCC_PCIE_2_PHY_BCR] = { 0x1f01c },
	[GCC_PCIE_2_PHY_NOCSR_COM_PHY_BCR] = { 0x1f028 },
	[GCC_PCIE_PHY_BCR] = { 0x6f000 },
	[GCC_PCIE_PHY_CFG_AHB_BCR] = { 0x6f00c },
	[GCC_PCIE_PHY_COM_BCR] = { 0x6f010 },
	[GCC_PDM_BCR] = { 0x33000 },
	[GCC_PRNG_BCR] = { 0x34000 },
	[GCC_QUPV3_WRAPPER_0_BCR] = { 0x17000 },
	[GCC_QUPV3_WRAPPER_1_BCR] = { 0x18000 },
	[GCC_QUPV3_WRAPPER_2_BCR] = { 0x1e000 },
	[GCC_QUSB2PHY_PRIM_BCR] = { 0x12000 },
	[GCC_QUSB2PHY_SEC_BCR] = { 0x12004 },
	[GCC_SDCC2_BCR] = { 0x14000 },
	[GCC_SDCC4_BCR] = { 0x16000 },
	[GCC_TSIF_BCR] = { 0x36000 },
	[GCC_UFS_CARD_BCR] = { 0x75000 },
	[GCC_UFS_PHY_BCR] = { 0x77000 },
	[GCC_USB30_PRIM_BCR] = { 0xf000 },
	[GCC_USB30_SEC_BCR] = { 0x10000 },
	[GCC_USB3_DP_PHY_PRIM_BCR] = { 0x50008 },
	[GCC_USB3_DP_PHY_SEC_BCR] = { 0x50014 },
	[GCC_USB3_PHY_PRIM_BCR] = { 0x50000 },
	[GCC_USB3_PHY_SEC_BCR] = { 0x5000c },
	[GCC_USB3PHY_PHY_PRIM_BCR] = { 0x50004 },
	[GCC_USB3PHY_PHY_SEC_BCR] = { 0x50010 },
	[GCC_USB_PHY_CFG_AHB2PHY_BCR] = { 0x6a000 },
	[GCC_VIDEO_AXI0_CLK_ARES] = { 0xb024, 2 },
	[GCC_VIDEO_AXI1_CLK_ARES] = { 0xb028, 2 },
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

static const struct regmap_config gcc_sm8250_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x9c100,
	.fast_io = true,
};

static const struct qcom_cc_desc gcc_sm8250_desc = {
	.config = &gcc_sm8250_regmap_config,
	.clks = gcc_sm8250_clocks,
	.num_clks = ARRAY_SIZE(gcc_sm8250_clocks),
	.resets = gcc_sm8250_resets,
	.num_resets = ARRAY_SIZE(gcc_sm8250_resets),
	.gdscs = gcc_sm8250_gdscs,
	.num_gdscs = ARRAY_SIZE(gcc_sm8250_gdscs),
};

static const struct of_device_id gcc_sm8250_match_table[] = {
	{ .compatible = "qcom,gcc-sm8250" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_sm8250_match_table);

static int gcc_sm8250_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret;

	regmap = qcom_cc_map(pdev, &gcc_sm8250_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/*
	 * Disable the GPLL0 active input to NPU and GPU
	 * via MISC registers.
	 */
	regmap_update_bits(regmap, 0x4d110, 0x3, 0x3);
	regmap_update_bits(regmap, 0x71028, 0x3, 0x3);

	/*
	 * Keep the clocks always-ON
	 * GCC_VIDEO_AHB_CLK, GCC_CAMERA_AHB_CLK, GCC_DISP_AHB_CLK,
	 * GCC_CPUSS_DVM_BUS_CLK, GCC_GPU_CFG_AHB_CLK,
	 * GCC_SYS_NOC_CPUSS_AHB_CLK
	 */
	regmap_update_bits(regmap, 0x0b004, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x0b008, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x0b00c, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x4818c, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x71004, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x52000, BIT(0), BIT(0));

	ret = qcom_cc_register_rcg_dfs(regmap, gcc_dfs_clocks,
				       ARRAY_SIZE(gcc_dfs_clocks));
	if (ret)
		return ret;

	return qcom_cc_really_probe(pdev, &gcc_sm8250_desc, regmap);
}

static struct platform_driver gcc_sm8250_driver = {
	.probe = gcc_sm8250_probe,
	.driver = {
		.name = "gcc-sm8250",
		.of_match_table = gcc_sm8250_match_table,
	},
};

static int __init gcc_sm8250_init(void)
{
	return platform_driver_register(&gcc_sm8250_driver);
}
subsys_initcall(gcc_sm8250_init);

static void __exit gcc_sm8250_exit(void)
{
	platform_driver_unregister(&gcc_sm8250_driver);
}
module_exit(gcc_sm8250_exit);

MODULE_DESCRIPTION("QTI GCC SM8250 Driver");
MODULE_LICENSE("GPL v2");
