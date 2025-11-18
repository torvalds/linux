// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_clock.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,lpass-sc7280.h>
#include <dt-bindings/clock/qcom,lpassaudiocc-sc7280.h>

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
	P_LPASS_AON_CC_PLL_OUT_EVEN,
	P_LPASS_AON_CC_PLL_OUT_MAIN,
	P_LPASS_AON_CC_PLL_OUT_MAIN_CDIV_DIV_CLK_SRC,
	P_LPASS_AON_CC_PLL_OUT_ODD,
	P_LPASS_AUDIO_CC_PLL_OUT_AUX,
	P_LPASS_AUDIO_CC_PLL_OUT_AUX2_DIV_CLK_SRC,
	P_LPASS_AUDIO_CC_PLL_MAIN_DIV_CLK,
};

static const struct pll_vco zonda_vco[] = {
	{ 595200000UL, 3600000000UL, 0 },
};

static struct clk_branch lpass_q6ss_ahbm_clk = {
	.halt_reg = 0x901c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x901c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
				.name = "lpass_q6ss_ahbm_clk",
				.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_q6ss_ahbs_clk = {
	.halt_reg = 0x9020,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "lpass_q6ss_ahbs_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

/* 1128.96MHz configuration */
static const struct alpha_pll_config lpass_audio_cc_pll_config = {
	.l = 0x3a,
	.alpha = 0xcccc,
	.config_ctl_val = 0x08200920,
	.config_ctl_hi_val = 0x05002001,
	.config_ctl_hi1_val = 0x00000000,
	.user_ctl_val = 0x03000101,
};

static struct clk_alpha_pll lpass_audio_cc_pll = {
	.offset = 0x0,
	.vco_table = zonda_vco,
	.num_vco = ARRAY_SIZE(zonda_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_ZONDA],
	.clkr = {
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_audio_cc_pll",
			.parent_data = &(const struct clk_parent_data){
				.index = 0,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_zonda_ops,
		},
	},
};

static const struct clk_div_table post_div_table_lpass_audio_cc_pll_out_aux2[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv lpass_audio_cc_pll_out_aux2 = {
	.offset = 0x0,
	.post_div_shift = 8,
	.post_div_table = post_div_table_lpass_audio_cc_pll_out_aux2,
	.num_post_div = ARRAY_SIZE(post_div_table_lpass_audio_cc_pll_out_aux2),
	.width = 2,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_ZONDA],
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "lpass_audio_cc_pll_out_aux2",
		.parent_hws = (const struct clk_hw*[]){
			&lpass_audio_cc_pll.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_zonda_ops,
	},
};

static const struct pll_vco lucid_vco[] = {
	{ 249600000, 2000000000, 0 },
};

/* 614.4 MHz configuration */
static const struct alpha_pll_config lpass_aon_cc_pll_config = {
	.l = 0x20,
	.alpha = 0x0,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002261,
	.config_ctl_hi1_val = 0x329A299C,
	.user_ctl_val = 0x00005100,
	.user_ctl_hi_val = 0x00000805,
	.user_ctl_hi1_val = 0x00000000,
};

static struct clk_alpha_pll lpass_aon_cc_pll = {
	.offset = 0x0,
	.vco_table = lucid_vco,
	.num_vco = ARRAY_SIZE(lucid_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID],
	.clkr = {
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_aon_cc_pll",
			.parent_data = &(const struct clk_parent_data){
				.index = 0,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_ops,
		},
	},
};

static const struct clk_div_table post_div_table_lpass_aon_cc_pll_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv lpass_aon_cc_pll_out_even = {
	.offset = 0x0,
	.post_div_shift = 8,
	.post_div_table = post_div_table_lpass_aon_cc_pll_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_lpass_aon_cc_pll_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID],
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "lpass_aon_cc_pll_out_even",
		.parent_hws = (const struct clk_hw*[]){
			&lpass_aon_cc_pll.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_lucid_ops,
	},
};

static const struct clk_div_table post_div_table_lpass_aon_cc_pll_out_odd[] = {
	{ 0x5, 5 },
	{ }
};

static struct clk_alpha_pll_postdiv lpass_aon_cc_pll_out_odd = {
	.offset = 0x0,
	.post_div_shift = 12,
	.post_div_table = post_div_table_lpass_aon_cc_pll_out_odd,
	.num_post_div = ARRAY_SIZE(post_div_table_lpass_aon_cc_pll_out_odd),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID],
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "lpass_aon_cc_pll_out_odd",
		.parent_hws = (const struct clk_hw*[]){
			&lpass_aon_cc_pll.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_lucid_ops,
	},
};

static const struct parent_map lpass_audio_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_LPASS_AUDIO_CC_PLL_OUT_AUX, 3 },
	{ P_LPASS_AON_CC_PLL_OUT_ODD, 5 },
	{ P_LPASS_AUDIO_CC_PLL_OUT_AUX2_DIV_CLK_SRC, 6 },
};

static struct clk_regmap_div lpass_audio_cc_pll_out_aux2_div_clk_src;
static struct clk_regmap_div lpass_audio_cc_pll_out_main_div_clk_src;

static const struct clk_parent_data lpass_audio_cc_parent_data_0[] = {
	{ .index = 0 },
	{ .hw = &lpass_audio_cc_pll.clkr.hw },
	{ .hw = &lpass_aon_cc_pll_out_odd.clkr.hw },
	{ .hw = &lpass_audio_cc_pll_out_aux2_div_clk_src.clkr.hw },
};

static const struct parent_map lpass_aon_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_LPASS_AON_CC_PLL_OUT_EVEN, 4 },
};

static const struct clk_parent_data lpass_aon_cc_parent_data_0[] = {
	{ .index = 0 },
	{ .hw = &lpass_aon_cc_pll_out_even.clkr.hw },
};

static const struct parent_map lpass_aon_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_LPASS_AON_CC_PLL_OUT_ODD, 1 },
	{ P_LPASS_AUDIO_CC_PLL_MAIN_DIV_CLK, 6 },
};

static const struct clk_parent_data lpass_aon_cc_parent_data_1[] = {
	{ .index = 0 },
	{ .hw = &lpass_aon_cc_pll_out_odd.clkr.hw },
	{ .hw = &lpass_audio_cc_pll_out_main_div_clk_src.clkr.hw },
};

static const struct freq_tbl ftbl_lpass_aon_cc_main_rcg_clk_src[] = {
	F(38400000, P_LPASS_AON_CC_PLL_OUT_EVEN, 8, 0, 0),
	F(76800000, P_LPASS_AON_CC_PLL_OUT_EVEN, 4, 0, 0),
	F(153600000, P_LPASS_AON_CC_PLL_OUT_EVEN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 lpass_aon_cc_main_rcg_clk_src = {
	.cmd_rcgr = 0x1000,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = lpass_aon_cc_parent_map_0,
	.freq_tbl = ftbl_lpass_aon_cc_main_rcg_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "lpass_aon_cc_main_rcg_clk_src",
		.parent_data = lpass_aon_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(lpass_aon_cc_parent_data_0),
		.flags = CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_lpass_aon_cc_tx_mclk_rcg_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(24576000, P_LPASS_AON_CC_PLL_OUT_ODD, 5, 0, 0),
	{ }
};

static struct clk_rcg2 lpass_aon_cc_tx_mclk_rcg_clk_src = {
	.cmd_rcgr = 0x13004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = lpass_aon_cc_parent_map_1,
	.freq_tbl = ftbl_lpass_aon_cc_tx_mclk_rcg_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "lpass_aon_cc_tx_mclk_rcg_clk_src",
		.parent_data = lpass_aon_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(lpass_aon_cc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div lpass_audio_cc_pll_out_aux2_div_clk_src = {
	.reg = 0x48,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "lpass_audio_cc_pll_out_aux2_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&lpass_audio_cc_pll_out_aux2.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div lpass_audio_cc_pll_out_main_div_clk_src = {
	.reg = 0x3c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "lpass_audio_cc_pll_out_main_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&lpass_audio_cc_pll.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div lpass_aon_cc_cdiv_tx_mclk_div_clk_src = {
	.reg = 0x13010,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "lpass_aon_cc_cdiv_tx_mclk_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&lpass_aon_cc_tx_mclk_rcg_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div lpass_aon_cc_pll_out_main_cdiv_div_clk_src = {
	.reg = 0x80,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "lpass_aon_cc_pll_out_main_cdiv_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&lpass_aon_cc_pll.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static const struct freq_tbl ftbl_lpass_audio_cc_ext_mclk0_clk_src[] = {
	F(256000, P_LPASS_AON_CC_PLL_OUT_ODD, 15, 1, 32),
	F(352800, P_LPASS_AUDIO_CC_PLL_OUT_AUX2_DIV_CLK_SRC, 10, 1, 32),
	F(512000, P_LPASS_AON_CC_PLL_OUT_ODD, 15, 1, 16),
	F(705600, P_LPASS_AUDIO_CC_PLL_OUT_AUX2_DIV_CLK_SRC, 10, 1, 16),
	F(768000, P_LPASS_AON_CC_PLL_OUT_ODD, 10, 1, 16),
	F(1024000, P_LPASS_AON_CC_PLL_OUT_ODD, 15, 1, 8),
	F(1411200, P_LPASS_AUDIO_CC_PLL_OUT_AUX2_DIV_CLK_SRC, 10, 1, 8),
	F(1536000, P_LPASS_AON_CC_PLL_OUT_ODD, 10, 1, 8),
	F(2048000, P_LPASS_AON_CC_PLL_OUT_ODD, 15, 1, 4),
	F(2822400, P_LPASS_AUDIO_CC_PLL_OUT_AUX2_DIV_CLK_SRC, 10, 1, 4),
	F(3072000, P_LPASS_AON_CC_PLL_OUT_ODD, 10, 1, 4),
	F(4096000, P_LPASS_AON_CC_PLL_OUT_ODD, 15, 1, 2),
	F(5644800, P_LPASS_AUDIO_CC_PLL_OUT_AUX2_DIV_CLK_SRC, 10, 1, 2),
	F(6144000, P_LPASS_AON_CC_PLL_OUT_ODD, 10, 1, 2),
	F(8192000, P_LPASS_AON_CC_PLL_OUT_ODD, 15, 0, 0),
	F(9600000, P_BI_TCXO, 2, 0, 0),
	F(11289600, P_LPASS_AUDIO_CC_PLL_OUT_AUX2_DIV_CLK_SRC, 10, 0, 0),
	F(12288000, P_LPASS_AON_CC_PLL_OUT_ODD, 10, 0, 0),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(22579200, P_LPASS_AUDIO_CC_PLL_OUT_AUX2_DIV_CLK_SRC, 5, 0, 0),
	F(24576000, P_LPASS_AON_CC_PLL_OUT_ODD, 5, 0, 0),
	{ }
};

static struct clk_rcg2 lpass_audio_cc_ext_mclk0_clk_src = {
	.cmd_rcgr = 0x20004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = lpass_audio_cc_parent_map_0,
	.freq_tbl = ftbl_lpass_audio_cc_ext_mclk0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "lpass_audio_cc_ext_mclk0_clk_src",
		.parent_data = lpass_audio_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(lpass_audio_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 lpass_audio_cc_ext_mclk1_clk_src = {
	.cmd_rcgr = 0x21004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = lpass_audio_cc_parent_map_0,
	.freq_tbl = ftbl_lpass_audio_cc_ext_mclk0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "lpass_audio_cc_ext_mclk1_clk_src",
		.parent_data = lpass_audio_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(lpass_audio_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 lpass_audio_cc_rx_mclk_clk_src = {
	.cmd_rcgr = 0x24004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = lpass_audio_cc_parent_map_0,
	.freq_tbl = ftbl_lpass_audio_cc_ext_mclk0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "lpass_audio_cc_rx_mclk_clk_src",
		.parent_data = lpass_audio_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(lpass_audio_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div lpass_audio_cc_cdiv_rx_mclk_div_clk_src = {
	.reg = 0x240d0,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "lpass_audio_cc_cdiv_rx_mclk_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&lpass_audio_cc_rx_mclk_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch lpass_aon_cc_audio_hm_h_clk;

static struct clk_branch lpass_audio_cc_codec_mem0_clk = {
	.halt_reg = 0x1e004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1e004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_audio_cc_codec_mem0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_aon_cc_audio_hm_h_clk.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_audio_cc_codec_mem1_clk = {
	.halt_reg = 0x1e008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1e008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_audio_cc_codec_mem1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_aon_cc_audio_hm_h_clk.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_audio_cc_codec_mem2_clk = {
	.halt_reg = 0x1e00c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1e00c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_audio_cc_codec_mem2_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_aon_cc_audio_hm_h_clk.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_audio_cc_codec_mem_clk = {
	.halt_reg = 0x1e000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1e000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_audio_cc_codec_mem_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_aon_cc_audio_hm_h_clk.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_audio_cc_ext_mclk0_clk = {
	.halt_reg = 0x20018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x20018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_audio_cc_ext_mclk0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_audio_cc_ext_mclk0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_audio_cc_ext_mclk1_clk = {
	.halt_reg = 0x21018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x21018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_audio_cc_ext_mclk1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_audio_cc_ext_mclk1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_audio_cc_rx_mclk_2x_clk = {
	.halt_reg = 0x240cc,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x240cc,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_audio_cc_rx_mclk_2x_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_audio_cc_rx_mclk_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_audio_cc_rx_mclk_clk = {
	.halt_reg = 0x240d4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x240d4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_audio_cc_rx_mclk_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_audio_cc_cdiv_rx_mclk_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_aon_cc_audio_hm_h_clk = {
	.halt_reg = 0x9014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_aon_cc_audio_hm_h_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_aon_cc_main_rcg_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch lpass_aon_cc_va_mem0_clk = {
	.halt_reg = 0x9028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_aon_cc_va_mem0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_aon_cc_main_rcg_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_aon_cc_tx_mclk_2x_clk = {
	.halt_reg = 0x1300c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1300c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_aon_cc_tx_mclk_2x_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_aon_cc_tx_mclk_rcg_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_aon_cc_tx_mclk_clk = {
	.halt_reg = 0x13014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_aon_cc_tx_mclk_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_aon_cc_cdiv_tx_mclk_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc lpass_aon_cc_lpass_audio_hm_gdsc = {
	.gdscr = 0x9090,
	.pd = {
		.name = "lpass_aon_cc_lpass_audio_hm_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct clk_regmap *lpass_cc_sc7280_clocks[] = {
	[LPASS_Q6SS_AHBM_CLK] = &lpass_q6ss_ahbm_clk.clkr,
	[LPASS_Q6SS_AHBS_CLK] = &lpass_q6ss_ahbs_clk.clkr,
};

static struct clk_regmap *lpass_aon_cc_sc7280_clocks[] = {
	[LPASS_AON_CC_AUDIO_HM_H_CLK] = &lpass_aon_cc_audio_hm_h_clk.clkr,
	[LPASS_AON_CC_VA_MEM0_CLK] = &lpass_aon_cc_va_mem0_clk.clkr,
	[LPASS_AON_CC_CDIV_TX_MCLK_DIV_CLK_SRC] = &lpass_aon_cc_cdiv_tx_mclk_div_clk_src.clkr,
	[LPASS_AON_CC_MAIN_RCG_CLK_SRC] = &lpass_aon_cc_main_rcg_clk_src.clkr,
	[LPASS_AON_CC_PLL] = &lpass_aon_cc_pll.clkr,
	[LPASS_AON_CC_PLL_OUT_EVEN] = &lpass_aon_cc_pll_out_even.clkr,
	[LPASS_AON_CC_PLL_OUT_MAIN_CDIV_DIV_CLK_SRC] =
		&lpass_aon_cc_pll_out_main_cdiv_div_clk_src.clkr,
	[LPASS_AON_CC_PLL_OUT_ODD] = &lpass_aon_cc_pll_out_odd.clkr,
	[LPASS_AON_CC_TX_MCLK_2X_CLK] = &lpass_aon_cc_tx_mclk_2x_clk.clkr,
	[LPASS_AON_CC_TX_MCLK_CLK] = &lpass_aon_cc_tx_mclk_clk.clkr,
	[LPASS_AON_CC_TX_MCLK_RCG_CLK_SRC] = &lpass_aon_cc_tx_mclk_rcg_clk_src.clkr,
};

static struct gdsc *lpass_aon_cc_sc7280_gdscs[] = {
	[LPASS_AON_CC_LPASS_AUDIO_HM_GDSC] = &lpass_aon_cc_lpass_audio_hm_gdsc,
};

static struct clk_regmap *lpass_audio_cc_sc7280_clocks[] = {
	[LPASS_AUDIO_CC_CDIV_RX_MCLK_DIV_CLK_SRC] = &lpass_audio_cc_cdiv_rx_mclk_div_clk_src.clkr,
	[LPASS_AUDIO_CC_CODEC_MEM0_CLK] = &lpass_audio_cc_codec_mem0_clk.clkr,
	[LPASS_AUDIO_CC_CODEC_MEM1_CLK] = &lpass_audio_cc_codec_mem1_clk.clkr,
	[LPASS_AUDIO_CC_CODEC_MEM2_CLK] = &lpass_audio_cc_codec_mem2_clk.clkr,
	[LPASS_AUDIO_CC_CODEC_MEM_CLK] = &lpass_audio_cc_codec_mem_clk.clkr,
	[LPASS_AUDIO_CC_EXT_MCLK0_CLK] = &lpass_audio_cc_ext_mclk0_clk.clkr,
	[LPASS_AUDIO_CC_EXT_MCLK0_CLK_SRC] = &lpass_audio_cc_ext_mclk0_clk_src.clkr,
	[LPASS_AUDIO_CC_EXT_MCLK1_CLK] = &lpass_audio_cc_ext_mclk1_clk.clkr,
	[LPASS_AUDIO_CC_EXT_MCLK1_CLK_SRC] = &lpass_audio_cc_ext_mclk1_clk_src.clkr,
	[LPASS_AUDIO_CC_PLL] = &lpass_audio_cc_pll.clkr,
	[LPASS_AUDIO_CC_PLL_OUT_AUX2] = &lpass_audio_cc_pll_out_aux2.clkr,
	[LPASS_AUDIO_CC_PLL_OUT_AUX2_DIV_CLK_SRC] = &lpass_audio_cc_pll_out_aux2_div_clk_src.clkr,
	[LPASS_AUDIO_CC_PLL_OUT_MAIN_DIV_CLK_SRC] = &lpass_audio_cc_pll_out_main_div_clk_src.clkr,
	[LPASS_AUDIO_CC_RX_MCLK_2X_CLK] = &lpass_audio_cc_rx_mclk_2x_clk.clkr,
	[LPASS_AUDIO_CC_RX_MCLK_CLK] = &lpass_audio_cc_rx_mclk_clk.clkr,
	[LPASS_AUDIO_CC_RX_MCLK_CLK_SRC] = &lpass_audio_cc_rx_mclk_clk_src.clkr,
};

static struct regmap_config lpass_audio_cc_sc7280_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
};

static const struct qcom_cc_desc lpass_cc_sc7280_desc = {
	.config = &lpass_audio_cc_sc7280_regmap_config,
	.clks = lpass_cc_sc7280_clocks,
	.num_clks = ARRAY_SIZE(lpass_cc_sc7280_clocks),
	.gdscs = lpass_aon_cc_sc7280_gdscs,
	.num_gdscs = ARRAY_SIZE(lpass_aon_cc_sc7280_gdscs),
};

static const struct qcom_cc_desc lpass_audio_cc_sc7280_desc = {
	.config = &lpass_audio_cc_sc7280_regmap_config,
	.clks = lpass_audio_cc_sc7280_clocks,
	.num_clks = ARRAY_SIZE(lpass_audio_cc_sc7280_clocks),
};

static const struct qcom_reset_map lpass_audio_cc_sc7280_resets[] = {
	[LPASS_AUDIO_SWR_RX_CGCR] = { 0xa0, 1 },
	[LPASS_AUDIO_SWR_TX_CGCR] = { 0xa8, 1 },
	[LPASS_AUDIO_SWR_WSA_CGCR] = { 0xb0, 1 },
};

static const struct regmap_config lpass_audio_cc_sc7280_reset_regmap_config = {
	.name = "lpassaudio_cc_reset",
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.max_register = 0xc8,
};

static const struct qcom_cc_desc lpass_audio_cc_reset_sc7280_desc = {
	.config = &lpass_audio_cc_sc7280_reset_regmap_config,
	.resets = lpass_audio_cc_sc7280_resets,
	.num_resets = ARRAY_SIZE(lpass_audio_cc_sc7280_resets),
};

static const struct of_device_id lpass_audio_cc_sc7280_match_table[] = {
	{ .compatible = "qcom,qcm6490-lpassaudiocc", .data = &lpass_audio_cc_reset_sc7280_desc },
	{ .compatible = "qcom,sc7280-lpassaudiocc", .data = &lpass_audio_cc_sc7280_desc },
	{ }
};
MODULE_DEVICE_TABLE(of, lpass_audio_cc_sc7280_match_table);

static int lpass_audio_setup_runtime_pm(struct platform_device *pdev)
{
	int ret;

	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, 50);
	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret)
		return ret;

	ret = devm_pm_clk_create(&pdev->dev);
	if (ret)
		return ret;

	ret = pm_clk_add(&pdev->dev, "iface");
	if (ret < 0)
		dev_err(&pdev->dev, "failed to acquire iface clock\n");

	return pm_runtime_resume_and_get(&pdev->dev);
}

static int lpass_audio_cc_sc7280_probe(struct platform_device *pdev)
{
	const struct qcom_cc_desc *desc;
	struct regmap *regmap;
	int ret;

	desc = device_get_match_data(&pdev->dev);

	if (of_device_is_compatible(pdev->dev.of_node, "qcom,qcm6490-lpassaudiocc"))
		return qcom_cc_probe_by_index(pdev, 1, desc);

	ret = lpass_audio_setup_runtime_pm(pdev);
	if (ret)
		return ret;

	lpass_audio_cc_sc7280_regmap_config.name = "lpassaudio_cc";
	lpass_audio_cc_sc7280_regmap_config.max_register = 0x2f000;

	regmap = qcom_cc_map(pdev, desc);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		goto exit;
	}

	clk_zonda_pll_configure(&lpass_audio_cc_pll, regmap, &lpass_audio_cc_pll_config);

	/* PLL settings */
	regmap_write(regmap, 0x4, 0x3b);
	regmap_write(regmap, 0x8, 0xff05);

	ret = qcom_cc_really_probe(&pdev->dev, desc, regmap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register LPASS AUDIO CC clocks\n");
		goto exit;
	}

	ret = qcom_cc_probe_by_index(pdev, 1, &lpass_audio_cc_reset_sc7280_desc);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register LPASS AUDIO CC Resets\n");
		goto exit;
	}

exit:
	pm_runtime_put_autosuspend(&pdev->dev);

	return ret;
}

static const struct dev_pm_ops lpass_audio_cc_pm_ops = {
	SET_RUNTIME_PM_OPS(pm_clk_suspend, pm_clk_resume, NULL)
};

static struct platform_driver lpass_audio_cc_sc7280_driver = {
	.probe = lpass_audio_cc_sc7280_probe,
	.driver = {
		.name = "lpass_audio_cc-sc7280",
		.of_match_table = lpass_audio_cc_sc7280_match_table,
		.pm = &lpass_audio_cc_pm_ops,
	},
};

static const struct qcom_cc_desc lpass_aon_cc_sc7280_desc = {
	.config = &lpass_audio_cc_sc7280_regmap_config,
	.clks = lpass_aon_cc_sc7280_clocks,
	.num_clks = ARRAY_SIZE(lpass_aon_cc_sc7280_clocks),
	.gdscs = lpass_aon_cc_sc7280_gdscs,
	.num_gdscs = ARRAY_SIZE(lpass_aon_cc_sc7280_gdscs),
};

static const struct of_device_id lpass_aon_cc_sc7280_match_table[] = {
	{ .compatible = "qcom,sc7280-lpassaoncc" },
	{ }
};
MODULE_DEVICE_TABLE(of, lpass_aon_cc_sc7280_match_table);

static int lpass_aon_cc_sc7280_probe(struct platform_device *pdev)
{
	const struct qcom_cc_desc *desc;
	struct regmap *regmap;
	int ret;

	ret = lpass_audio_setup_runtime_pm(pdev);
	if (ret)
		return ret;

	if (of_property_read_bool(pdev->dev.of_node, "qcom,adsp-pil-mode")) {
		lpass_audio_cc_sc7280_regmap_config.name = "cc";
		desc = &lpass_cc_sc7280_desc;
		ret = qcom_cc_probe(pdev, desc);
		goto exit;
	}

	lpass_audio_cc_sc7280_regmap_config.name = "lpasscc_aon";
	lpass_audio_cc_sc7280_regmap_config.max_register = 0xa0008;
	desc = &lpass_aon_cc_sc7280_desc;

	regmap = qcom_cc_map(pdev, desc);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		goto exit;
	}

	clk_lucid_pll_configure(&lpass_aon_cc_pll, regmap, &lpass_aon_cc_pll_config);

	ret = qcom_cc_really_probe(&pdev->dev, &lpass_aon_cc_sc7280_desc, regmap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register LPASS AON CC clocks\n");
		goto exit;
	}

exit:
	pm_runtime_put_autosuspend(&pdev->dev);

	return ret;
}

static struct platform_driver lpass_aon_cc_sc7280_driver = {
	.probe = lpass_aon_cc_sc7280_probe,
	.driver = {
		.name = "lpass_aon_cc-sc7280",
		.of_match_table = lpass_aon_cc_sc7280_match_table,
		.pm = &lpass_audio_cc_pm_ops,
	},
};

static int __init lpass_audio_cc_sc7280_init(void)
{
	int ret;

	ret = platform_driver_register(&lpass_aon_cc_sc7280_driver);
	if (ret)
		return ret;

	return platform_driver_register(&lpass_audio_cc_sc7280_driver);
}
subsys_initcall(lpass_audio_cc_sc7280_init);

static void __exit lpass_audio_cc_sc7280_exit(void)
{
	platform_driver_unregister(&lpass_audio_cc_sc7280_driver);
	platform_driver_unregister(&lpass_aon_cc_sc7280_driver);
}
module_exit(lpass_audio_cc_sc7280_exit);

MODULE_DESCRIPTION("QTI LPASS_AUDIO_CC SC7280 Driver");
MODULE_LICENSE("GPL v2");
