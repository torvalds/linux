// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021, Konrad Dybcio <konrad.dybcio@somainline.org>
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,gcc-sm6350.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "common.h"
#include "gdsc.h"
#include "reset.h"

enum {
	P_BI_TCXO,
	P_GPLL0_OUT_EVEN,
	P_GPLL0_OUT_MAIN,
	P_GPLL0_OUT_ODD,
	P_GPLL6_OUT_EVEN,
	P_GPLL7_OUT_MAIN,
	P_SLEEP_CLK,
};

static struct clk_alpha_pll gpll0 = {
	.offset = 0x0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpll0",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_fabia_ops,
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
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll0_out_even",
		.parent_hws = (const struct clk_hw*[]){
			&gpll0.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_fabia_ops,
	},
};

static const struct clk_div_table post_div_table_gpll0_out_odd[] = {
	{ 0x3, 3 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll0_out_odd = {
	.offset = 0x0,
	.post_div_shift = 12,
	.post_div_table = post_div_table_gpll0_out_odd,
	.num_post_div = ARRAY_SIZE(post_div_table_gpll0_out_odd),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll0_out_odd",
		.parent_hws = (const struct clk_hw*[]){
			&gpll0.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_fabia_ops,
	},
};

static struct clk_alpha_pll gpll6 = {
	.offset = 0x6000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(6),
		.hw.init = &(struct clk_init_data){
			.name = "gpll6",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_fabia_ops,
		},
	},
};

static const struct clk_div_table post_div_table_gpll6_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll6_out_even = {
	.offset = 0x6000,
	.post_div_shift = 8,
	.post_div_table = post_div_table_gpll6_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_gpll6_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll6_out_even",
		.parent_hws = (const struct clk_hw*[]){
			&gpll6.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_fabia_ops,
	},
};

static struct clk_alpha_pll gpll7 = {
	.offset = 0x7000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(7),
		.hw.init = &(struct clk_init_data){
			.name = "gpll7",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_fabia_ops,
		},
	},
};

static const struct parent_map gcc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL6_OUT_EVEN, 2 },
	{ P_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_0[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll6_out_even.clkr.hw },
	{ .hw = &gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_1[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_ODD, 2 },
};

static const struct clk_parent_data gcc_parent_data_2_ao[] = {
	{ .fw_name = "bi_tcxo_ao" },
	{ .hw = &gpll0_out_odd.clkr.hw },
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
	{ P_GPLL0_OUT_ODD, 2 },
};

static const struct clk_parent_data gcc_parent_data_4[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_odd.clkr.hw },
};

static const struct parent_map gcc_parent_map_5[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_ODD, 2 },
	{ P_SLEEP_CLK, 5 },
	{ P_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_5[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0_out_odd.clkr.hw },
	{ .fw_name = "sleep_clk" },
	{ .hw = &gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_6[] = {
	{ P_BI_TCXO, 0 },
	{ P_SLEEP_CLK, 5 },
};

static const struct clk_parent_data gcc_parent_data_6[] = {
	{ .fw_name = "bi_tcxo" },
	{ .fw_name = "sleep_clk" }
};

static const struct parent_map gcc_parent_map_7[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL6_OUT_EVEN, 2 },
	{ P_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_7[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll6_out_even.clkr.hw },
	{ .hw = &gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_8[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_ODD, 2 },
	{ P_GPLL7_OUT_MAIN, 3 },
};

static const struct clk_parent_data gcc_parent_data_8[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0_out_odd.clkr.hw },
	{ .hw = &gpll7.clkr.hw },
};

static struct clk_regmap_div gcc_gpu_gpll0_main_div_clk_src = {
	.reg = 0x4514C,
	.shift = 0,
	.width = 2,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "gcc_gpu_gpll0_main_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gpll0.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_npu_pll0_main_div_clk_src = {
	.reg = 0x4ce00,
	.shift = 0,
	.width = 2,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "gcc_npu_pll0_main_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gpll0.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static const struct freq_tbl ftbl_gcc_cpuss_ahb_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_cpuss_ahb_clk_src = {
	.cmd_rcgr = 0x30014,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_cpuss_ahb_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_cpuss_ahb_clk_src",
		.parent_data = gcc_parent_data_2_ao,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2_ao),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_gp1_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(25000000, P_GPLL0_OUT_EVEN, 12, 0, 0),
	F(50000000, P_GPLL0_OUT_ODD, 4, 0, 0),
	F(100000000, P_GPLL0_OUT_ODD, 2, 0, 0),
	F(200000000, P_GPLL0_OUT_ODD, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_gp1_clk_src = {
	.cmd_rcgr = 0x37004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_gp1_clk_src",
		.parent_data = gcc_parent_data_5,
		.num_parents = ARRAY_SIZE(gcc_parent_data_5),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_gp2_clk_src = {
	.cmd_rcgr = 0x38004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_gp2_clk_src",
		.parent_data = gcc_parent_data_5,
		.num_parents = ARRAY_SIZE(gcc_parent_data_5),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_gp3_clk_src = {
	.cmd_rcgr = 0x39004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_gp3_clk_src",
		.parent_data = gcc_parent_data_5,
		.num_parents = ARRAY_SIZE(gcc_parent_data_5),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pdm2_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(60000000, P_GPLL0_OUT_EVEN, 5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pdm2_clk_src = {
	.cmd_rcgr = 0x23010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_pdm2_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_pdm2_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
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
	F(75000000, P_GPLL0_OUT_EVEN, 4, 0, 0),
	F(80000000, P_GPLL0_OUT_EVEN, 1, 4, 15),
	F(96000000, P_GPLL0_OUT_EVEN, 1, 8, 25),
	F(100000000, P_GPLL0_OUT_EVEN, 3, 0, 0),
	F(102400000, P_GPLL0_OUT_EVEN, 1, 128, 375),
	F(112000000, P_GPLL0_OUT_EVEN, 1, 28, 75),
	F(117964800, P_GPLL0_OUT_EVEN, 1, 6144, 15625),
	F(120000000, P_GPLL0_OUT_EVEN, 2.5, 0, 0),
	F(128000000, P_GPLL6_OUT_EVEN, 3, 0, 0),
	{ }
};

static struct clk_init_data gcc_qupv3_wrap0_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s0_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s0_clk_src = {
	.cmd_rcgr = 0x21148,
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
	.cmd_rcgr = 0x21278,
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
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s2_clk_src = {
	.cmd_rcgr = 0x213a8,
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
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s3_clk_src = {
	.cmd_rcgr = 0x214d8,
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
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s4_clk_src = {
	.cmd_rcgr = 0x21608,
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
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s5_clk_src = {
	.cmd_rcgr = 0x21738,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s5_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s0_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s0_clk_src = {
	.cmd_rcgr = 0x22018,
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
	.cmd_rcgr = 0x22148,
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
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s2_clk_src = {
	.cmd_rcgr = 0x22278,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s2_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s3_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s3_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s3_clk_src = {
	.cmd_rcgr = 0x223a8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s3_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s4_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s4_clk_src = {
	.cmd_rcgr = 0x224d8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s4_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s5_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s5_clk_src = {
	.cmd_rcgr = 0x22608,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s5_clk_src_init,
};

static const struct freq_tbl ftbl_gcc_sdcc1_apps_clk_src[] = {
	F(144000, P_BI_TCXO, 16, 3, 25),
	F(400000, P_BI_TCXO, 12, 1, 4),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(20000000, P_GPLL0_OUT_EVEN, 5, 1, 3),
	F(25000000, P_GPLL0_OUT_EVEN, 12, 0, 0),
	F(50000000, P_GPLL0_OUT_EVEN, 6, 0, 0),
	F(100000000, P_GPLL0_OUT_EVEN, 3, 0, 0),
	F(192000000, P_GPLL6_OUT_EVEN, 2, 0, 0),
	F(384000000, P_GPLL6_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc1_apps_clk_src = {
	.cmd_rcgr = 0x4b024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_7,
	.freq_tbl = ftbl_gcc_sdcc1_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_sdcc1_apps_clk_src",
		.parent_data = gcc_parent_data_7,
		.num_parents = ARRAY_SIZE(gcc_parent_data_7),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_sdcc1_ice_core_clk_src[] = {
	F(100000000, P_GPLL0_OUT_EVEN, 3, 0, 0),
	F(150000000, P_GPLL0_OUT_EVEN, 2, 0, 0),
	F(300000000, P_GPLL0_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc1_ice_core_clk_src = {
	.cmd_rcgr = 0x4b00c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_sdcc1_ice_core_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_sdcc1_ice_core_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_sdcc2_apps_clk_src[] = {
	F(400000, P_BI_TCXO, 12, 1, 4),
	F(9600000, P_BI_TCXO, 2, 0, 0),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(25000000, P_GPLL0_OUT_ODD, 8, 0, 0),
	F(50000000, P_GPLL0_OUT_ODD, 4, 0, 0),
	F(100000000, P_GPLL0_OUT_ODD, 2, 0, 0),
	F(202000000, P_GPLL7_OUT_MAIN, 4, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc2_apps_clk_src = {
	.cmd_rcgr = 0x2000c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_8,
	.freq_tbl = ftbl_gcc_sdcc2_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_sdcc2_apps_clk_src",
		.parent_data = gcc_parent_data_8,
		.num_parents = ARRAY_SIZE(gcc_parent_data_8),
		.flags = CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_floor_ops,
	},
};

static const struct freq_tbl ftbl_gcc_ufs_phy_axi_clk_src[] = {
	F(25000000, P_GPLL0_OUT_ODD, 8, 0, 0),
	F(50000000, P_GPLL0_OUT_ODD, 4, 0, 0),
	F(100000000, P_GPLL0_OUT_ODD, 2, 0, 0),
	F(200000000, P_GPLL0_OUT_ODD, 1, 0, 0),
	F(240000000, P_GPLL0_OUT_MAIN, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_phy_axi_clk_src = {
	.cmd_rcgr = 0x3a01c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_ufs_phy_axi_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_phy_axi_clk_src",
		.parent_data = gcc_parent_data_4,
		.num_parents = ARRAY_SIZE(gcc_parent_data_4),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_ufs_phy_ice_core_clk_src[] = {
	F(37500000, P_GPLL0_OUT_EVEN, 8, 0, 0),
	F(75000000, P_GPLL0_OUT_EVEN, 4, 0, 0),
	F(150000000, P_GPLL0_OUT_EVEN, 2, 0, 0),
	F(300000000, P_GPLL0_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_phy_ice_core_clk_src = {
	.cmd_rcgr = 0x3a048,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_ufs_phy_ice_core_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_phy_ice_core_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_ufs_phy_phy_aux_clk_src[] = {
	F(9600000, P_BI_TCXO, 2, 0, 0),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_phy_phy_aux_clk_src = {
	.cmd_rcgr = 0x3a0b0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_ufs_phy_phy_aux_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_phy_phy_aux_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_ufs_phy_unipro_core_clk_src[] = {
	F(37500000, P_GPLL0_OUT_EVEN, 8, 0, 0),
	F(75000000, P_GPLL0_OUT_EVEN, 4, 0, 0),
	F(150000000, P_GPLL0_OUT_EVEN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_phy_unipro_core_clk_src = {
	.cmd_rcgr = 0x3a060,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_ufs_phy_unipro_core_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_phy_unipro_core_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb30_prim_master_clk_src[] = {
	F(66666667, P_GPLL0_OUT_ODD, 3, 0, 0),
	F(133333333, P_GPLL0_OUT_MAIN, 4.5, 0, 0),
	F(200000000, P_GPLL0_OUT_MAIN, 3, 0, 0),
	F(240000000, P_GPLL0_OUT_MAIN, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb30_prim_master_clk_src = {
	.cmd_rcgr = 0x1a01c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_usb30_prim_master_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_usb30_prim_master_clk_src",
		.parent_data = gcc_parent_data_4,
		.num_parents = ARRAY_SIZE(gcc_parent_data_4),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb30_prim_mock_utmi_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb30_prim_mock_utmi_clk_src = {
	.cmd_rcgr = 0x1a034,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_usb30_prim_mock_utmi_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_usb30_prim_mock_utmi_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_usb3_prim_phy_aux_clk_src = {
	.cmd_rcgr = 0x1a060,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_6,
	.freq_tbl = ftbl_gcc_usb30_prim_mock_utmi_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_usb3_prim_phy_aux_clk_src",
		.parent_data = gcc_parent_data_6,
		.num_parents = ARRAY_SIZE(gcc_parent_data_6),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_aggre_ufs_phy_axi_clk = {
	.halt_reg = 0x3e014,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x3e014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3e014,
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

static struct clk_branch gcc_aggre_ufs_phy_axi_hw_ctl_clk = {
	.halt_reg = 0x3e014,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x3e014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3e014,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_aggre_ufs_phy_axi_hw_ctl_clk",
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
	.halt_reg = 0x3e014,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x3e014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3e014,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_axi_hw_ctl_clk",
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
	.halt_reg = 0x3e010,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x3e010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3e010,
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

static struct clk_branch gcc_boot_rom_ahb_clk = {
	.halt_reg = 0x26004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x26004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(28),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_boot_rom_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_ahb_clk = {
	.halt_reg = 0x17008,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x17008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x17008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camera_ahb_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_axi_clk = {
	.halt_reg = 0x17018,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x17018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x17018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camera_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_throttle_nrt_axi_clk = {
	.halt_reg = 0x17078,
	.halt_check = BRANCH_VOTED,
	.hwcg_reg = 0x17078,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x17078,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camera_throttle_nrt_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_throttle_rt_axi_clk = {
	.halt_reg = 0x17024,
	.halt_check = BRANCH_VOTED,
	.hwcg_reg = 0x17024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x17024,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camera_throttle_rt_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_xo_clk = {
	.halt_reg = 0x17030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17030,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camera_xo_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ce1_ahb_clk = {
	.halt_reg = 0x2b00c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2b00c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(3),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ce1_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ce1_axi_clk = {
	.halt_reg = 0x2b008,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ce1_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ce1_clk = {
	.halt_reg = 0x2b004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ce1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_usb3_prim_axi_clk = {
	.halt_reg = 0x1101c,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x1101c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1101c,
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

static struct clk_branch gcc_cpuss_ahb_clk = {
	.halt_reg = 0x30000,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x30000,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_cpuss_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_cpuss_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_IS_CRITICAL | CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cpuss_gnoc_clk = {
	.halt_reg = 0x30004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x30004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(5),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_cpuss_gnoc_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cpuss_rbcpr_clk = {
	.halt_reg = 0x30008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x30008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_cpuss_rbcpr_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ddrss_gpu_axi_clk = {
	.halt_reg = 0x2d038,
	.halt_check = BRANCH_VOTED,
	.hwcg_reg = 0x2d038,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2d038,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ddrss_gpu_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_ahb_clk = {
	.halt_reg = 0x1700c,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x1700c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1700c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_disp_ahb_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_axi_clk = {
	.halt_reg = 0x1701c,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x1701c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1701c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_disp_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_cc_sleep_clk = {
	.halt_reg = 0x17074,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x17074,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x17074,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_disp_cc_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_cc_xo_clk = {
	.halt_reg = 0x17070,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x17070,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x17070,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_disp_cc_xo_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_gpll0_clk = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_disp_gpll0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gpll0.clkr.hw,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_throttle_axi_clk = {
	.halt_reg = 0x17028,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x17028,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x17028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_disp_throttle_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_xo_clk = {
	.halt_reg = 0x17034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17034,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_disp_xo_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp1_clk = {
	.halt_reg = 0x37000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x37000,
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
	.halt_reg = 0x38000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x38000,
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
	.halt_reg = 0x39000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x39000,
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

static struct clk_branch gcc_gpu_cfg_ahb_clk = {
	.halt_reg = 0x45004,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x45004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gpu_cfg_ahb_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_gpll0_clk = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(7),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gpu_gpll0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gpll0.clkr.hw,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_gpll0_div_clk = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(8),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gpu_gpll0_div_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_gpu_gpll0_main_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_memnoc_gfx_clk = {
	.halt_reg = 0x4500c,
	.halt_check = BRANCH_VOTED,
	.hwcg_reg = 0x4500c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x4500c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gpu_memnoc_gfx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_snoc_dvm_gfx_clk = {
	.halt_reg = 0x45014,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x45014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x45014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gpu_snoc_dvm_gfx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_npu_axi_clk = {
	.halt_reg = 0x4c008,
	.halt_check = BRANCH_VOTED,
	.hwcg_reg = 0x4c008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x4c008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_npu_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_npu_bwmon_axi_clk = {
	.halt_reg = 0x4d004,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x4d004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x4d004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_npu_bwmon_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_npu_bwmon_dma_cfg_ahb_clk = {
	.halt_reg = 0x4d008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4d008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_npu_bwmon_dma_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_npu_bwmon_dsp_cfg_ahb_clk = {
	.halt_reg = 0x4d00c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4d00c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_npu_bwmon_dsp_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_npu_cfg_ahb_clk = {
	.halt_reg = 0x4c004,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x4c004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x4c004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_npu_cfg_ahb_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_npu_dma_clk = {
	.halt_reg = 0x4c140,
	.halt_check = BRANCH_VOTED,
	.hwcg_reg = 0x4c140,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x4c140,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_npu_dma_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_npu_gpll0_clk = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_npu_gpll0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gpll0.clkr.hw,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_npu_gpll0_div_clk = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(10),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_npu_gpll0_div_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_npu_pll0_main_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm2_clk = {
	.halt_reg = 0x2300c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2300c,
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
	.halt_reg = 0x23004,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x23004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x23004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pdm_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm_xo4_clk = {
	.halt_reg = 0x23008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x23008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pdm_xo4_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_prng_ahb_clk = {
	.halt_reg = 0x24004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x24004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(26),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_prng_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_core_2x_clk = {
	.halt_reg = 0x21014,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap0_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_core_clk = {
	.halt_reg = 0x2100c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(8),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap0_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s0_clk = {
	.halt_reg = 0x21144,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
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
	.halt_reg = 0x21274,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
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
	.halt_reg = 0x213a4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
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
	.halt_reg = 0x214d4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
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
	.halt_reg = 0x21604,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
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
	.halt_reg = 0x21734,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
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

static struct clk_branch gcc_qupv3_wrap1_core_2x_clk = {
	.halt_reg = 0x22004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(16),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap1_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_core_clk = {
	.halt_reg = 0x22008,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(17),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap1_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s0_clk = {
	.halt_reg = 0x22014,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(20),
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
	.halt_reg = 0x22144,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(21),
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
	.halt_reg = 0x22274,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(22),
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
	.halt_reg = 0x223a4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(23),
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
	.halt_reg = 0x224d4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(24),
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
	.halt_reg = 0x22604,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(25),
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

static struct clk_branch gcc_qupv3_wrap_0_m_ahb_clk = {
	.halt_reg = 0x21004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x21004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(6),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap_0_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_0_s_ahb_clk = {
	.halt_reg = 0x21008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x21008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(7),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap_0_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_1_m_ahb_clk = {
	.halt_reg = 0x2200c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2200c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(18),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap_1_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_1_s_ahb_clk = {
	.halt_reg = 0x22010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x22010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(19),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap_1_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_ahb_clk = {
	.halt_reg = 0x4b004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4b004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc1_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_apps_clk = {
	.halt_reg = 0x4b008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4b008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc1_apps_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_sdcc1_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_ice_core_clk = {
	.halt_reg = 0x4b03c,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x4b03c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x4b03c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc1_ice_core_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_sdcc1_ice_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc2_ahb_clk = {
	.halt_reg = 0x20008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x20008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc2_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc2_apps_clk = {
	.halt_reg = 0x20004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x20004,
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

static struct clk_branch gcc_sys_noc_cpuss_ahb_clk = {
	.halt_reg = 0x10140,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x10140,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sys_noc_cpuss_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_cpuss_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_IS_CRITICAL | CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
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
	.halt_reg = 0x3a00c,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x3a00c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3a00c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_axi_clk = {
	.halt_reg = 0x3a034,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x3a034,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3a034,
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
	.halt_reg = 0x3a0a4,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x3a0a4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3a0a4,
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

static struct clk_branch gcc_ufs_phy_ice_core_hw_ctl_clk = {
	.halt_reg = 0x3a0a4,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x3a0a4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3a0a4,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_ice_core_hw_ctl_clk",
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
	.halt_reg = 0x3a0ac,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x3a0ac,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3a0ac,
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

static struct clk_branch gcc_ufs_phy_phy_aux_hw_ctl_clk = {
	.halt_reg = 0x3a0ac,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x3a0ac,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3a0ac,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_phy_aux_hw_ctl_clk",
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
	.halt_reg = 0x3a014,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x3a014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_rx_symbol_0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_rx_symbol_1_clk = {
	.halt_reg = 0x3a018,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x3a018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_rx_symbol_1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_tx_symbol_0_clk = {
	.halt_reg = 0x3a010,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x3a010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_tx_symbol_0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_unipro_core_clk = {
	.halt_reg = 0x3a09c,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x3a09c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3a09c,
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

static struct clk_branch gcc_ufs_phy_unipro_core_hw_ctl_clk = {
	.halt_reg = 0x3a09c,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x3a09c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3a09c,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_unipro_core_hw_ctl_clk",
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
	.halt_reg = 0x1a00c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1a00c,
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
	.halt_reg = 0x1a018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1a018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb30_prim_mock_utmi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb30_prim_mock_utmi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_prim_sleep_clk = {
	.halt_reg = 0x1a014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1a014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb30_prim_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_clkref_clk = {
	.halt_reg = 0x8c010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_prim_clkref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_phy_aux_clk = {
	.halt_reg = 0x1a050,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1a050,
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
	.halt_reg = 0x1a054,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1a054,
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
	.halt_reg = 0x1a058,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x1a058,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1a058,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_prim_phy_pipe_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_ahb_clk = {
	.halt_reg = 0x17004,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x17004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x17004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_video_ahb_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_axi_clk = {
	.halt_reg = 0x17014,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x17014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x17014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_video_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_throttle_axi_clk = {
	.halt_reg = 0x17020,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x17020,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x17020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_video_throttle_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_xo_clk = {
	.halt_reg = 0x1702c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1702c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_video_xo_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc usb30_prim_gdsc = {
	.gdscr = 0x1a004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "usb30_prim_gdsc",
	},
	.pwrsts = PWRSTS_RET_ON,
};

static struct gdsc ufs_phy_gdsc = {
	.gdscr = 0x3a004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "ufs_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc hlos1_vote_mmnoc_mmu_tbu_hf0_gdsc = {
	.gdscr = 0xb7040,
	.pd = {
		.name = "hlos1_vote_mmnoc_mmu_tbu_hf0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc hlos1_vote_mmnoc_mmu_tbu_hf1_gdsc = {
	.gdscr = 0xb7044,
	.pd = {
		.name = "hlos1_vote_mmnoc_mmu_tbu_hf1_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct clk_regmap *gcc_sm6350_clocks[] = {
	[GCC_AGGRE_UFS_PHY_AXI_CLK] = &gcc_aggre_ufs_phy_axi_clk.clkr,
	[GCC_AGGRE_USB3_PRIM_AXI_CLK] = &gcc_aggre_usb3_prim_axi_clk.clkr,
	[GCC_BOOT_ROM_AHB_CLK] = &gcc_boot_rom_ahb_clk.clkr,
	[GCC_CAMERA_AHB_CLK] = &gcc_camera_ahb_clk.clkr,
	[GCC_CAMERA_AXI_CLK] = &gcc_camera_axi_clk.clkr,
	[GCC_CAMERA_THROTTLE_NRT_AXI_CLK] =
		&gcc_camera_throttle_nrt_axi_clk.clkr,
	[GCC_CAMERA_THROTTLE_RT_AXI_CLK] = &gcc_camera_throttle_rt_axi_clk.clkr,
	[GCC_CAMERA_XO_CLK] = &gcc_camera_xo_clk.clkr,
	[GCC_CE1_AHB_CLK] = &gcc_ce1_ahb_clk.clkr,
	[GCC_CE1_AXI_CLK] = &gcc_ce1_axi_clk.clkr,
	[GCC_CE1_CLK] = &gcc_ce1_clk.clkr,
	[GCC_CFG_NOC_USB3_PRIM_AXI_CLK] = &gcc_cfg_noc_usb3_prim_axi_clk.clkr,
	[GCC_CPUSS_AHB_CLK] = &gcc_cpuss_ahb_clk.clkr,
	[GCC_CPUSS_AHB_CLK_SRC] = &gcc_cpuss_ahb_clk_src.clkr,
	[GCC_CPUSS_GNOC_CLK] = &gcc_cpuss_gnoc_clk.clkr,
	[GCC_CPUSS_RBCPR_CLK] = &gcc_cpuss_rbcpr_clk.clkr,
	[GCC_DDRSS_GPU_AXI_CLK] = &gcc_ddrss_gpu_axi_clk.clkr,
	[GCC_DISP_AHB_CLK] = &gcc_disp_ahb_clk.clkr,
	[GCC_DISP_AXI_CLK] = &gcc_disp_axi_clk.clkr,
	[GCC_DISP_CC_SLEEP_CLK] = &gcc_disp_cc_sleep_clk.clkr,
	[GCC_DISP_CC_XO_CLK] = &gcc_disp_cc_xo_clk.clkr,
	[GCC_DISP_GPLL0_CLK] = &gcc_disp_gpll0_clk.clkr,
	[GCC_DISP_THROTTLE_AXI_CLK] = &gcc_disp_throttle_axi_clk.clkr,
	[GCC_DISP_XO_CLK] = &gcc_disp_xo_clk.clkr,
	[GCC_GP1_CLK] = &gcc_gp1_clk.clkr,
	[GCC_GP1_CLK_SRC] = &gcc_gp1_clk_src.clkr,
	[GCC_GP2_CLK] = &gcc_gp2_clk.clkr,
	[GCC_GP2_CLK_SRC] = &gcc_gp2_clk_src.clkr,
	[GCC_GP3_CLK] = &gcc_gp3_clk.clkr,
	[GCC_GP3_CLK_SRC] = &gcc_gp3_clk_src.clkr,
	[GCC_GPU_CFG_AHB_CLK] = &gcc_gpu_cfg_ahb_clk.clkr,
	[GCC_GPU_GPLL0_CLK] = &gcc_gpu_gpll0_clk.clkr,
	[GCC_GPU_GPLL0_DIV_CLK] = &gcc_gpu_gpll0_div_clk.clkr,
	[GCC_GPU_MEMNOC_GFX_CLK] = &gcc_gpu_memnoc_gfx_clk.clkr,
	[GCC_GPU_SNOC_DVM_GFX_CLK] = &gcc_gpu_snoc_dvm_gfx_clk.clkr,
	[GCC_NPU_AXI_CLK] = &gcc_npu_axi_clk.clkr,
	[GCC_NPU_BWMON_AXI_CLK] = &gcc_npu_bwmon_axi_clk.clkr,
	[GCC_NPU_BWMON_DMA_CFG_AHB_CLK] = &gcc_npu_bwmon_dma_cfg_ahb_clk.clkr,
	[GCC_NPU_BWMON_DSP_CFG_AHB_CLK] = &gcc_npu_bwmon_dsp_cfg_ahb_clk.clkr,
	[GCC_NPU_CFG_AHB_CLK] = &gcc_npu_cfg_ahb_clk.clkr,
	[GCC_NPU_DMA_CLK] = &gcc_npu_dma_clk.clkr,
	[GCC_NPU_GPLL0_CLK] = &gcc_npu_gpll0_clk.clkr,
	[GCC_NPU_GPLL0_DIV_CLK] = &gcc_npu_gpll0_div_clk.clkr,
	[GCC_PDM2_CLK] = &gcc_pdm2_clk.clkr,
	[GCC_PDM2_CLK_SRC] = &gcc_pdm2_clk_src.clkr,
	[GCC_PDM_AHB_CLK] = &gcc_pdm_ahb_clk.clkr,
	[GCC_PDM_XO4_CLK] = &gcc_pdm_xo4_clk.clkr,
	[GCC_PRNG_AHB_CLK] = &gcc_prng_ahb_clk.clkr,
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
	[GCC_SYS_NOC_CPUSS_AHB_CLK] = &gcc_sys_noc_cpuss_ahb_clk.clkr,
	[GCC_UFS_MEM_CLKREF_CLK] = &gcc_ufs_mem_clkref_clk.clkr,
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
	[GCC_USB30_PRIM_SLEEP_CLK] = &gcc_usb30_prim_sleep_clk.clkr,
	[GCC_USB3_PRIM_CLKREF_CLK] = &gcc_usb3_prim_clkref_clk.clkr,
	[GCC_USB3_PRIM_PHY_AUX_CLK] = &gcc_usb3_prim_phy_aux_clk.clkr,
	[GCC_USB3_PRIM_PHY_AUX_CLK_SRC] = &gcc_usb3_prim_phy_aux_clk_src.clkr,
	[GCC_USB3_PRIM_PHY_COM_AUX_CLK] = &gcc_usb3_prim_phy_com_aux_clk.clkr,
	[GCC_USB3_PRIM_PHY_PIPE_CLK] = &gcc_usb3_prim_phy_pipe_clk.clkr,
	[GCC_VIDEO_AHB_CLK] = &gcc_video_ahb_clk.clkr,
	[GCC_VIDEO_AXI_CLK] = &gcc_video_axi_clk.clkr,
	[GCC_VIDEO_THROTTLE_AXI_CLK] = &gcc_video_throttle_axi_clk.clkr,
	[GCC_VIDEO_XO_CLK] = &gcc_video_xo_clk.clkr,
	[GPLL0] = &gpll0.clkr,
	[GPLL0_OUT_EVEN] = &gpll0_out_even.clkr,
	[GPLL0_OUT_ODD] = &gpll0_out_odd.clkr,
	[GPLL6] = &gpll6.clkr,
	[GPLL6_OUT_EVEN] = &gpll6_out_even.clkr,
	[GPLL7] = &gpll7.clkr,
	[GCC_UFS_PHY_PHY_AUX_HW_CTL_CLK] = &gcc_ufs_phy_phy_aux_hw_ctl_clk.clkr,
	[GCC_UFS_PHY_AXI_HW_CTL_CLK] = &gcc_ufs_phy_axi_hw_ctl_clk.clkr,
	[GCC_AGGRE_UFS_PHY_AXI_HW_CTL_CLK] =
				&gcc_aggre_ufs_phy_axi_hw_ctl_clk.clkr,
	[GCC_UFS_PHY_UNIPRO_CORE_HW_CTL_CLK] =
				&gcc_ufs_phy_unipro_core_hw_ctl_clk.clkr,
	[GCC_UFS_PHY_ICE_CORE_HW_CTL_CLK] =
				&gcc_ufs_phy_ice_core_hw_ctl_clk.clkr,
	[GCC_GPU_GPLL0_MAIN_DIV_CLK_SRC] = &gcc_gpu_gpll0_main_div_clk_src.clkr,
	[GCC_NPU_PLL0_MAIN_DIV_CLK_SRC] = &gcc_npu_pll0_main_div_clk_src.clkr,
};

static struct gdsc *gcc_sm6350_gdscs[] = {
	[USB30_PRIM_GDSC] = &usb30_prim_gdsc,
	[UFS_PHY_GDSC] = &ufs_phy_gdsc,
	[HLOS1_VOTE_MMNOC_MMU_TBU_HF0_GDSC] = &hlos1_vote_mmnoc_mmu_tbu_hf0_gdsc,
	[HLOS1_VOTE_MMNOC_MMU_TBU_HF1_GDSC] = &hlos1_vote_mmnoc_mmu_tbu_hf1_gdsc,
};

static const struct qcom_reset_map gcc_sm6350_resets[] = {
	[GCC_QUSB2PHY_PRIM_BCR] = { 0x1d000 },
	[GCC_QUSB2PHY_SEC_BCR] = { 0x1e000 },
	[GCC_SDCC1_BCR] = { 0x4b000 },
	[GCC_SDCC2_BCR] = { 0x20000 },
	[GCC_UFS_PHY_BCR] = { 0x3a000 },
	[GCC_USB30_PRIM_BCR] = { 0x1a000 },
	[GCC_USB3_PHY_PRIM_BCR] = { 0x1c000 },
	[GCC_USB3_DP_PHY_PRIM_BCR] = { 0x1c008 },
};

static const struct clk_rcg_dfs_data gcc_dfs_clocks[] = {
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s0_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s1_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s2_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s3_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s4_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s5_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s0_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s1_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s2_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s3_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s4_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s5_clk_src),
};

static const struct regmap_config gcc_sm6350_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xbf030,
	.fast_io = true,
};

static const struct qcom_cc_desc gcc_sm6350_desc = {
	.config = &gcc_sm6350_regmap_config,
	.clks = gcc_sm6350_clocks,
	.num_clks = ARRAY_SIZE(gcc_sm6350_clocks),
	.resets = gcc_sm6350_resets,
	.num_resets = ARRAY_SIZE(gcc_sm6350_resets),
	.gdscs = gcc_sm6350_gdscs,
	.num_gdscs = ARRAY_SIZE(gcc_sm6350_gdscs),
};

static const struct of_device_id gcc_sm6350_match_table[] = {
	{ .compatible = "qcom,gcc-sm6350" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_sm6350_match_table);

static int gcc_sm6350_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret;

	regmap = qcom_cc_map(pdev, &gcc_sm6350_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/* Disable the GPLL0 active input to NPU and GPU via MISC registers */
	regmap_update_bits(regmap, 0x4cf00, 0x3, 0x3);
	regmap_update_bits(regmap, 0x45f00, 0x3, 0x3);

	ret = qcom_cc_register_rcg_dfs(regmap, gcc_dfs_clocks,
			ARRAY_SIZE(gcc_dfs_clocks));
	if (ret)
		return ret;

	return qcom_cc_really_probe(&pdev->dev, &gcc_sm6350_desc, regmap);
}

static struct platform_driver gcc_sm6350_driver = {
	.probe = gcc_sm6350_probe,
	.driver = {
		.name = "gcc-sm6350",
		.of_match_table = gcc_sm6350_match_table,
	},
};

static int __init gcc_sm6350_init(void)
{
	return platform_driver_register(&gcc_sm6350_driver);
}
core_initcall(gcc_sm6350_init);

static void __exit gcc_sm6350_exit(void)
{
	platform_driver_unregister(&gcc_sm6350_driver);
}
module_exit(gcc_sm6350_exit);

MODULE_DESCRIPTION("QTI GCC SM6350 Driver");
MODULE_LICENSE("GPL v2");
