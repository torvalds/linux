// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,hawi-videocc.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "common.h"
#include "gdsc.h"
#include "reset.h"

enum {
	DT_BI_TCXO,
	DT_AHB_CLK,
};

enum {
	P_BI_TCXO,
	P_VIDEO_CC_PLL0_OUT_EVEN,
	P_VIDEO_CC_PLL0_OUT_MAIN,
	P_VIDEO_CC_PLL1_OUT_MAIN,
	P_VIDEO_CC_PLL2_OUT_MAIN,
	P_VIDEO_CC_PLL3_OUT_MAIN,
};

static const struct pll_vco taycan_eha_t_vco[] = {
	{ 249600000, 2500000000, 0 },
};

/* 360.0 MHz Configuration */
static const struct alpha_pll_config video_cc_pll0_config = {
	.l = 0x12,
	.cal_l = 0x48,
	.alpha = 0xc000,
	.config_ctl_val = 0xa5c400e7,
	.config_ctl_hi_val = 0x0a8060e0,
	.config_ctl_hi1_val = 0xf51dea20,
	.user_ctl_val = 0x00000400,
	.user_ctl_hi_val = 0x00000002,
};

static struct clk_alpha_pll video_cc_pll0 = {
	.offset = 0x0,
	.config = &video_cc_pll0_config,
	.vco_table = taycan_eha_t_vco,
	.num_vco = ARRAY_SIZE(taycan_eha_t_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EHA_T],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_pll0",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_taycan_eha_t_ops,
		},
	},
};

static const struct clk_div_table post_div_table_video_cc_pll0_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv video_cc_pll0_out_even = {
	.offset = 0x0,
	.post_div_shift = 10,
	.post_div_table = post_div_table_video_cc_pll0_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_video_cc_pll0_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EHA_T],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_pll0_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&video_cc_pll0.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_taycan_eha_t_ops,
	},
};

/* 300.0 MHz Configuration */
static const struct alpha_pll_config video_cc_pll1_config = {
	.l = 0xf,
	.cal_l = 0x48,
	.alpha = 0xa000,
	.config_ctl_val = 0xa5c400e7,
	.config_ctl_hi_val = 0x0a8060e0,
	.config_ctl_hi1_val = 0xf51dea20,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00000002,
};

static struct clk_alpha_pll video_cc_pll1 = {
	.offset = 0x1000,
	.config = &video_cc_pll1_config,
	.vco_table = taycan_eha_t_vco,
	.num_vco = ARRAY_SIZE(taycan_eha_t_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EHA_T],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_pll1",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_taycan_eha_t_ops,
		},
	},
};

/* 300.0 MHz Configuration */
static const struct alpha_pll_config video_cc_pll2_config = {
	.l = 0xf,
	.cal_l = 0x48,
	.alpha = 0xa000,
	.config_ctl_val = 0xa5c400e7,
	.config_ctl_hi_val = 0x0a8060e0,
	.config_ctl_hi1_val = 0xf51dea20,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00000002,
};

static struct clk_alpha_pll video_cc_pll2 = {
	.offset = 0x2000,
	.config = &video_cc_pll2_config,
	.vco_table = taycan_eha_t_vco,
	.num_vco = ARRAY_SIZE(taycan_eha_t_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EHA_T],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_pll2",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_taycan_eha_t_ops,
		},
	},
};

/* 300.0 MHz Configuration */
static const struct alpha_pll_config video_cc_pll3_config = {
	.l = 0xf,
	.cal_l = 0x48,
	.alpha = 0xa000,
	.config_ctl_val = 0xa5c400e7,
	.config_ctl_hi_val = 0x0a8060e0,
	.config_ctl_hi1_val = 0xf51dea20,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00000002,
};

static struct clk_alpha_pll video_cc_pll3 = {
	.offset = 0x3000,
	.config = &video_cc_pll3_config,
	.vco_table = taycan_eha_t_vco,
	.num_vco = ARRAY_SIZE(taycan_eha_t_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EHA_T],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_pll3",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_taycan_eha_t_ops,
		},
	},
};

static const struct parent_map video_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
};

static const struct clk_parent_data video_cc_parent_data_0[] = {
	{ .index = DT_BI_TCXO },
};

static const struct parent_map video_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_VIDEO_CC_PLL1_OUT_MAIN, 1 },
};

static const struct clk_parent_data video_cc_parent_data_1[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &video_cc_pll1.clkr.hw },
};

static const struct parent_map video_cc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_VIDEO_CC_PLL3_OUT_MAIN, 1 },
};

static const struct clk_parent_data video_cc_parent_data_2[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &video_cc_pll3.clkr.hw },
};

static const struct parent_map video_cc_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
	{ P_VIDEO_CC_PLL2_OUT_MAIN, 1 },
};

static const struct clk_parent_data video_cc_parent_data_3[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &video_cc_pll2.clkr.hw },
};

static const struct parent_map video_cc_parent_map_4[] = {
	{ P_BI_TCXO, 0 },
	{ P_VIDEO_CC_PLL0_OUT_MAIN, 1 },
	{ P_VIDEO_CC_PLL0_OUT_EVEN, 2 },
};

static const struct clk_parent_data video_cc_parent_data_4[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &video_cc_pll0.clkr.hw },
	{ .hw = &video_cc_pll0_out_even.clkr.hw },
};

static const struct freq_tbl ftbl_video_cc_ahb_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_ahb_clk_src = {
	.cmd_rcgr = 0x8060,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_0,
	.freq_tbl = ftbl_video_cc_ahb_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_ahb_clk_src",
		.parent_data = video_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_video_cc_mvs0_clk_src[] = {
	F(150000000, P_VIDEO_CC_PLL1_OUT_MAIN, 2, 0, 0),
	F(240000000, P_VIDEO_CC_PLL1_OUT_MAIN, 2, 0, 0),
	F(285000000, P_VIDEO_CC_PLL1_OUT_MAIN, 2, 0, 0),
	F(311000000, P_VIDEO_CC_PLL1_OUT_MAIN, 2, 0, 0),
	F(420000000, P_VIDEO_CC_PLL1_OUT_MAIN, 2, 0, 0),
	F(444000000, P_VIDEO_CC_PLL1_OUT_MAIN, 2, 0, 0),
	F(533000000, P_VIDEO_CC_PLL1_OUT_MAIN, 2, 0, 0),
	F(630000000, P_VIDEO_CC_PLL1_OUT_MAIN, 2, 0, 0),
	F(714000000, P_VIDEO_CC_PLL1_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_mvs0_clk_src = {
	.cmd_rcgr = 0x8030,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_1,
	.freq_tbl = ftbl_video_cc_mvs0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_mvs0_clk_src",
		.parent_data = video_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_video_cc_mvs0a_clk_src[] = {
	F(150000000, P_VIDEO_CC_PLL3_OUT_MAIN, 2, 0, 0),
	F(240000000, P_VIDEO_CC_PLL3_OUT_MAIN, 2, 0, 0),
	F(338000000, P_VIDEO_CC_PLL3_OUT_MAIN, 2, 0, 0),
	F(420000000, P_VIDEO_CC_PLL3_OUT_MAIN, 2, 0, 0),
	F(444000000, P_VIDEO_CC_PLL3_OUT_MAIN, 2, 0, 0),
	F(533000000, P_VIDEO_CC_PLL3_OUT_MAIN, 2, 0, 0),
	F(630000000, P_VIDEO_CC_PLL3_OUT_MAIN, 2, 0, 0),
	F(710000000, P_VIDEO_CC_PLL3_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_mvs0a_clk_src = {
	.cmd_rcgr = 0x8000,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_2,
	.freq_tbl = ftbl_video_cc_mvs0a_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_mvs0a_clk_src",
		.parent_data = video_cc_parent_data_2,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_video_cc_mvs0b_clk_src[] = {
	F(150000000, P_VIDEO_CC_PLL2_OUT_MAIN, 2, 0, 0),
	F(240000000, P_VIDEO_CC_PLL2_OUT_MAIN, 2, 0, 0),
	F(311000000, P_VIDEO_CC_PLL2_OUT_MAIN, 2, 0, 0),
	F(420000000, P_VIDEO_CC_PLL2_OUT_MAIN, 2, 0, 0),
	F(444000000, P_VIDEO_CC_PLL2_OUT_MAIN, 2, 0, 0),
	F(533000000, P_VIDEO_CC_PLL2_OUT_MAIN, 2, 0, 0),
	F(630000000, P_VIDEO_CC_PLL2_OUT_MAIN, 2, 0, 0),
	F(667000000, P_VIDEO_CC_PLL2_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_mvs0b_clk_src = {
	.cmd_rcgr = 0x8018,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_3,
	.freq_tbl = ftbl_video_cc_mvs0b_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_mvs0b_clk_src",
		.parent_data = video_cc_parent_data_3,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_video_cc_mvs0c_clk_src[] = {
	F(225000000, P_VIDEO_CC_PLL0_OUT_EVEN, 1, 0, 0),
	F(360000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(430000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(557000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(634000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(782000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(928000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(1060000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_mvs0c_clk_src = {
	.cmd_rcgr = 0x8048,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_4,
	.freq_tbl = ftbl_video_cc_mvs0c_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_mvs0c_clk_src",
		.parent_data = video_cc_parent_data_4,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_4),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 video_cc_xo_clk_src = {
	.cmd_rcgr = 0x8180,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_0,
	.freq_tbl = ftbl_video_cc_ahb_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_xo_clk_src",
		.parent_data = video_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch video_cc_cx_axi0_clk = {
	.halt_reg = 0x81e8,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x81e8,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x81e8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_cx_axi0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvs0_clk = {
	.halt_reg = 0x80cc,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x80cc,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x80cc,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&video_cc_mvs0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch video_cc_mvs0_shift_clk = {
	.halt_reg = 0x81a4,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x81a4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x81a4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs0_shift_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&video_cc_xo_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvs0_vpp0_clk = {
	.halt_reg = 0x811c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x811c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x811c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs0_vpp0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&video_cc_mvs0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch video_cc_mvs0_vpp0_vpp1_gating_clk = {
	.halt_reg = 0x80c8,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x80c8,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x80c8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs0_vpp0_vpp1_gating_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&video_cc_mvs0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvs0_vpp1_clk = {
	.halt_reg = 0x80f4,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x80f4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x80f4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs0_vpp1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&video_cc_mvs0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch video_cc_mvs0a_clk = {
	.halt_reg = 0x8090,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x8090,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x8090,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs0a_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&video_cc_mvs0a_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvs0b_clk = {
	.halt_reg = 0x80b8,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x80b8,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x80b8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs0b_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&video_cc_mvs0b_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvs0c_clk = {
	.halt_reg = 0x814c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x814c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x814c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs0c_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&video_cc_mvs0c_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvs0c_ctl_freerun_clk = {
	.halt_reg = 0x8160,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8160,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs0c_ctl_freerun_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&video_cc_mvs0c_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvs0c_debug_clk = {
	.halt_reg = 0x8148,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8148,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs0c_debug_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&video_cc_mvs0c_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvs0c_freerun_clk = {
	.halt_reg = 0x815c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x815c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs0c_freerun_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&video_cc_mvs0c_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvs0c_shift_clk = {
	.halt_reg = 0x81a8,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x81a8,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x81a8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs0c_shift_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&video_cc_xo_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc video_cc_mvs0c_gdsc = {
	.gdscr = 0x8130,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x6,
	.pd = {
		.name = "video_cc_mvs0c_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc video_cc_axi0_cx_int_gdsc = {
	.gdscr = 0x81cc,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "video_cc_axi0_cx_int_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.parent = &video_cc_mvs0c_gdsc.pd,
	.flags = HW_CTRL_TRIGGER | POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc video_cc_mm_int_gdsc = {
	.gdscr = 0x81b4,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "video_cc_mm_int_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.parent = &video_cc_axi0_cx_int_gdsc.pd,
	.flags = HW_CTRL_TRIGGER | POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc video_cc_mvs0_gdsc = {
	.gdscr = 0x80a4,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x6,
	.pd = {
		.name = "video_cc_mvs0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.parent = &video_cc_mm_int_gdsc.pd,
	.flags = HW_CTRL_TRIGGER | POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc video_cc_mvs0_vpp0_gdsc = {
	.gdscr = 0x8108,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "video_cc_mvs0_vpp0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.parent = &video_cc_mvs0_gdsc.pd,
	.flags = HW_CTRL_TRIGGER | POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc video_cc_mvs0_vpp1_gdsc = {
	.gdscr = 0x80e0,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "video_cc_mvs0_vpp1_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.parent = &video_cc_mvs0_gdsc.pd,
	.flags = HW_CTRL_TRIGGER | POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc video_cc_mvs0a_gdsc = {
	.gdscr = 0x807c,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "video_cc_mvs0a_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.parent = &video_cc_mm_int_gdsc.pd,
	.flags = HW_CTRL_TRIGGER | POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct clk_regmap *video_cc_hawi_clocks[] = {
	[VIDEO_CC_AHB_CLK_SRC] = &video_cc_ahb_clk_src.clkr,
	[VIDEO_CC_CX_AXI0_CLK] = &video_cc_cx_axi0_clk.clkr,
	[VIDEO_CC_MVS0_CLK] = &video_cc_mvs0_clk.clkr,
	[VIDEO_CC_MVS0_CLK_SRC] = &video_cc_mvs0_clk_src.clkr,
	[VIDEO_CC_MVS0_SHIFT_CLK] = &video_cc_mvs0_shift_clk.clkr,
	[VIDEO_CC_MVS0_VPP0_CLK] = &video_cc_mvs0_vpp0_clk.clkr,
	[VIDEO_CC_MVS0_VPP0_VPP1_GATING_CLK] = &video_cc_mvs0_vpp0_vpp1_gating_clk.clkr,
	[VIDEO_CC_MVS0_VPP1_CLK] = &video_cc_mvs0_vpp1_clk.clkr,
	[VIDEO_CC_MVS0A_CLK] = &video_cc_mvs0a_clk.clkr,
	[VIDEO_CC_MVS0A_CLK_SRC] = &video_cc_mvs0a_clk_src.clkr,
	[VIDEO_CC_MVS0B_CLK] = &video_cc_mvs0b_clk.clkr,
	[VIDEO_CC_MVS0B_CLK_SRC] = &video_cc_mvs0b_clk_src.clkr,
	[VIDEO_CC_MVS0C_CLK] = &video_cc_mvs0c_clk.clkr,
	[VIDEO_CC_MVS0C_CLK_SRC] = &video_cc_mvs0c_clk_src.clkr,
	[VIDEO_CC_MVS0C_CTL_FREERUN_CLK] = &video_cc_mvs0c_ctl_freerun_clk.clkr,
	[VIDEO_CC_MVS0C_DEBUG_CLK] = &video_cc_mvs0c_debug_clk.clkr,
	[VIDEO_CC_MVS0C_FREERUN_CLK] = &video_cc_mvs0c_freerun_clk.clkr,
	[VIDEO_CC_MVS0C_SHIFT_CLK] = &video_cc_mvs0c_shift_clk.clkr,
	[VIDEO_CC_PLL0] = &video_cc_pll0.clkr,
	[VIDEO_CC_PLL0_OUT_EVEN] = &video_cc_pll0_out_even.clkr,
	[VIDEO_CC_PLL1] = &video_cc_pll1.clkr,
	[VIDEO_CC_PLL2] = &video_cc_pll2.clkr,
	[VIDEO_CC_PLL3] = &video_cc_pll3.clkr,
	[VIDEO_CC_XO_CLK_SRC] = &video_cc_xo_clk_src.clkr,
};

static struct gdsc *video_cc_hawi_gdscs[] = {
	[VIDEO_CC_AXI0_CX_INT_GDSC] = &video_cc_axi0_cx_int_gdsc,
	[VIDEO_CC_MM_INT_GDSC] = &video_cc_mm_int_gdsc,
	[VIDEO_CC_MVS0_GDSC] = &video_cc_mvs0_gdsc,
	[VIDEO_CC_MVS0_VPP0_GDSC] = &video_cc_mvs0_vpp0_gdsc,
	[VIDEO_CC_MVS0_VPP1_GDSC] = &video_cc_mvs0_vpp1_gdsc,
	[VIDEO_CC_MVS0A_GDSC] = &video_cc_mvs0a_gdsc,
	[VIDEO_CC_MVS0C_GDSC] = &video_cc_mvs0c_gdsc,
};

static const struct qcom_reset_map video_cc_hawi_resets[] = {
	[VIDEO_CC_AXI0_CX_INT_BCR] = { 0x81c8 },
	[VIDEO_CC_INTERFACE_BCR] = { 0x8164 },
	[VIDEO_CC_MM_INT_BCR] = { 0x81b0 },
	[VIDEO_CC_MVS0_BCR] = { 0x80a0 },
	[VIDEO_CC_MVS0_VPP0_BCR] = { 0x8104 },
	[VIDEO_CC_MVS0_VPP1_BCR] = { 0x80dc },
	[VIDEO_CC_MVS0A_BCR] = { 0x8078 },
	[VIDEO_CC_MVS0C_CLK_ARES] = { 0x814c, 2 },
	[VIDEO_CC_MVS0C_BCR] = { 0x812c },
	[VIDEO_CC_MVS0C_CTL_FREERUN_CLK_ARES] = { 0x8160, 2 },
	[VIDEO_CC_MVS0C_FREERUN_CLK_ARES] = { 0x815c, 2 },
	[VIDEO_CC_XO_CLK_ARES] = { 0x8198, 2 },
};

static struct clk_alpha_pll *video_cc_hawi_plls[] = {
	&video_cc_pll0,
	&video_cc_pll1,
	&video_cc_pll2,
	&video_cc_pll3,
};

static const u32 video_cc_hawi_critical_cbcrs[] = {
	0x8168, /* VIDEO_CC_AHB_CLK */
	0x81e4, /* VIDEO_CC_CX_DBGCH_XO_CLK */
	0x81e0, /* VIDEO_CC_CX_XO_CLK */
	0x81a0, /* VIDEO_CC_DBGCH_XO_CLK */
	0x81ac, /* VIDEO_CC_SLEEP_CLK */
	0x819c, /* VIDEO_CC_TS_XO_CLK */
	0x8198, /* VIDEO_CC_XO_CLK */
};

static const struct regmap_config video_cc_hawi_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xa018,
	.fast_io = true,
};

static struct qcom_cc_driver_data video_cc_hawi_driver_data = {
	.alpha_plls = video_cc_hawi_plls,
	.num_alpha_plls = ARRAY_SIZE(video_cc_hawi_plls),
	.clk_cbcrs = video_cc_hawi_critical_cbcrs,
	.num_clk_cbcrs = ARRAY_SIZE(video_cc_hawi_critical_cbcrs),
};

static const struct qcom_cc_desc video_cc_hawi_desc = {
	.config = &video_cc_hawi_regmap_config,
	.clks = video_cc_hawi_clocks,
	.num_clks = ARRAY_SIZE(video_cc_hawi_clocks),
	.resets = video_cc_hawi_resets,
	.num_resets = ARRAY_SIZE(video_cc_hawi_resets),
	.gdscs = video_cc_hawi_gdscs,
	.num_gdscs = ARRAY_SIZE(video_cc_hawi_gdscs),
	.use_rpm = true,
	.driver_data = &video_cc_hawi_driver_data,
};

static const struct of_device_id video_cc_hawi_match_table[] = {
	{ .compatible = "qcom,hawi-videocc" },
	{ }
};
MODULE_DEVICE_TABLE(of, video_cc_hawi_match_table);

static int video_cc_hawi_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &video_cc_hawi_desc);
}

static struct platform_driver video_cc_hawi_driver = {
	.probe = video_cc_hawi_probe,
	.driver = {
		.name = "videocc-hawi",
		.of_match_table = video_cc_hawi_match_table,
	},
};

module_platform_driver(video_cc_hawi_driver);

MODULE_DESCRIPTION("QTI VIDEOCC Hawi Driver");
MODULE_LICENSE("GPL");
