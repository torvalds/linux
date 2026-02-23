// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,kaanapali-videocc.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "common.h"
#include "gdsc.h"
#include "reset.h"

#define ACCU_CFG_MASK GENMASK(25, 21)

enum {
	DT_BI_TCXO,
	DT_AHB_CLK,
};

enum {
	P_BI_TCXO,
	P_VIDEO_CC_PLL0_OUT_MAIN,
	P_VIDEO_CC_PLL1_OUT_MAIN,
	P_VIDEO_CC_PLL2_OUT_MAIN,
	P_VIDEO_CC_PLL3_OUT_MAIN,
};

static const struct pll_vco taycan_eko_t_vco[] = {
	{ 249600000, 2500000000, 0 },
};

/* 360.0 MHz Configuration */
static const struct alpha_pll_config video_cc_pll0_config = {
	.l = 0x12,
	.cal_l = 0x48,
	.alpha = 0xc000,
	.config_ctl_val = 0x25c400e7,
	.config_ctl_hi_val = 0x0a8062e0,
	.config_ctl_hi1_val = 0xf51dea20,
	.user_ctl_val = 0x00000008,
	.user_ctl_hi_val = 0x00000002,
};

static struct clk_alpha_pll video_cc_pll0 = {
	.offset = 0x0,
	.config = &video_cc_pll0_config,
	.vco_table = taycan_eko_t_vco,
	.num_vco = ARRAY_SIZE(taycan_eko_t_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EKO_T],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_pll0",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_taycan_eko_t_ops,
		},
	},
};

/* 480.0 MHz Configuration */
static const struct alpha_pll_config video_cc_pll1_config = {
	.l = 0x19,
	.cal_l = 0x48,
	.alpha = 0x0,
	.config_ctl_val = 0x25c400e7,
	.config_ctl_hi_val = 0x0a8062e0,
	.config_ctl_hi1_val = 0xf51dea20,
	.user_ctl_val = 0x00000008,
	.user_ctl_hi_val = 0x00000002,
};

static struct clk_alpha_pll video_cc_pll1 = {
	.offset = 0x1000,
	.config = &video_cc_pll1_config,
	.vco_table = taycan_eko_t_vco,
	.num_vco = ARRAY_SIZE(taycan_eko_t_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EKO_T],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_pll1",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_taycan_eko_t_ops,
		},
	},
};

/* 480.0 MHz Configuration */
static const struct alpha_pll_config video_cc_pll2_config = {
	.l = 0x19,
	.cal_l = 0x48,
	.alpha = 0x0,
	.config_ctl_val = 0x25c400e7,
	.config_ctl_hi_val = 0x0a8062e0,
	.config_ctl_hi1_val = 0xf51dea20,
	.user_ctl_val = 0x00000008,
	.user_ctl_hi_val = 0x00000002,
};

static struct clk_alpha_pll video_cc_pll2 = {
	.offset = 0x2000,
	.config = &video_cc_pll2_config,
	.vco_table = taycan_eko_t_vco,
	.num_vco = ARRAY_SIZE(taycan_eko_t_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EKO_T],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_pll2",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_taycan_eko_t_ops,
		},
	},
};

/* 480.0 MHz Configuration */
static const struct alpha_pll_config video_cc_pll3_config = {
	.l = 0x19,
	.cal_l = 0x48,
	.alpha = 0x0,
	.config_ctl_val = 0x25c400e7,
	.config_ctl_hi_val = 0x0a8062e0,
	.config_ctl_hi1_val = 0xf51dea20,
	.user_ctl_val = 0x00000008,
	.user_ctl_hi_val = 0x00000002,
};

static struct clk_alpha_pll video_cc_pll3 = {
	.offset = 0x3000,
	.config = &video_cc_pll3_config,
	.vco_table = taycan_eko_t_vco,
	.num_vco = ARRAY_SIZE(taycan_eko_t_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EKO_T],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_pll3",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_taycan_eko_t_ops,
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
};

static const struct clk_parent_data video_cc_parent_data_4[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &video_cc_pll0.clkr.hw },
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
	F(240000000, P_VIDEO_CC_PLL1_OUT_MAIN, 2, 0, 0),
	F(338000000, P_VIDEO_CC_PLL1_OUT_MAIN, 2, 0, 0),
	F(420000000, P_VIDEO_CC_PLL1_OUT_MAIN, 2, 0, 0),
	F(444000000, P_VIDEO_CC_PLL1_OUT_MAIN, 2, 0, 0),
	F(533000000, P_VIDEO_CC_PLL1_OUT_MAIN, 2, 0, 0),
	F(630000000, P_VIDEO_CC_PLL1_OUT_MAIN, 2, 0, 0),
	F(800000000, P_VIDEO_CC_PLL1_OUT_MAIN, 2, 0, 0),
	F(1000000000, P_VIDEO_CC_PLL1_OUT_MAIN, 2, 0, 0),
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
	F(240000000, P_VIDEO_CC_PLL3_OUT_MAIN, 2, 0, 0),
	F(338000000, P_VIDEO_CC_PLL3_OUT_MAIN, 2, 0, 0),
	F(420000000, P_VIDEO_CC_PLL3_OUT_MAIN, 2, 0, 0),
	F(444000000, P_VIDEO_CC_PLL3_OUT_MAIN, 2, 0, 0),
	F(533000000, P_VIDEO_CC_PLL3_OUT_MAIN, 2, 0, 0),
	F(630000000, P_VIDEO_CC_PLL3_OUT_MAIN, 2, 0, 0),
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
	F(240000000, P_VIDEO_CC_PLL2_OUT_MAIN, 2, 0, 0),
	F(338000000, P_VIDEO_CC_PLL2_OUT_MAIN, 2, 0, 0),
	F(420000000, P_VIDEO_CC_PLL2_OUT_MAIN, 2, 0, 0),
	F(444000000, P_VIDEO_CC_PLL2_OUT_MAIN, 2, 0, 0),
	F(533000000, P_VIDEO_CC_PLL2_OUT_MAIN, 2, 0, 0),
	F(630000000, P_VIDEO_CC_PLL2_OUT_MAIN, 2, 0, 0),
	F(850000000, P_VIDEO_CC_PLL2_OUT_MAIN, 2, 0, 0),
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
	F(360000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(507000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(630000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(666000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(800000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(1104000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(1260000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
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
	.cmd_rcgr = 0x8194,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_0,
	.freq_tbl = ftbl_video_cc_ahb_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_xo_clk_src",
		.parent_data = video_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch video_cc_mvs0_clk = {
	.halt_reg = 0x80d0,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x80d0,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x80d0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&video_cc_mvs0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_mem_branch video_cc_mvs0_freerun_clk = {
	.mem_enable_reg = 0x80e4,
	.mem_ack_reg =  0x80e4,
	.mem_enable_mask = BIT(3),
	.mem_enable_ack_mask = GENMASK(11, 10),
	.mem_enable_invert = true,
	.branch = {
		.halt_reg = 0x80e0,
		.halt_check = BRANCH_HALT,
		.clkr = {
			.enable_reg = 0x80e0,
			.enable_mask = BIT(0),
			.hw.init = &(const struct clk_init_data) {
				.name = "video_cc_mvs0_freerun_clk",
				.parent_hws = (const struct clk_hw*[]) {
					&video_cc_mvs0_clk_src.clkr.hw,
				},
				.num_parents = 1,
				.flags = CLK_SET_RATE_PARENT,
				.ops = &clk_branch2_ops,
			},
		},
	},
};

static struct clk_branch video_cc_mvs0_shift_clk = {
	.halt_reg = 0x81b4,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x81b4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x81b4,
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
	.halt_reg = 0x8134,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x8134,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x8134,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs0_vpp0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&video_cc_mvs0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvs0_vpp0_freerun_clk = {
	.halt_reg = 0x8144,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8144,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs0_vpp0_freerun_clk",
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
	.halt_reg = 0x8108,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x8108,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x8108,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs0_vpp1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&video_cc_mvs0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvs0_vpp1_freerun_clk = {
	.halt_reg = 0x8118,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8118,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs0_vpp1_freerun_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&video_cc_mvs0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
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

static struct clk_branch video_cc_mvs0a_freerun_clk = {
	.halt_reg = 0x80a0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x80a0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs0a_freerun_clk",
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
	.halt_reg = 0x80bc,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x80bc,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x80bc,
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

static struct clk_branch video_cc_mvs0b_freerun_clk = {
	.halt_reg = 0x80cc,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x80cc,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs0b_freerun_clk",
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
	.halt_reg = 0x8164,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x8164,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x8164,
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

static struct clk_branch video_cc_mvs0c_freerun_clk = {
	.halt_reg = 0x8174,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8174,
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
	.halt_reg = 0x81b8,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x81b8,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x81b8,
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

static struct gdsc video_cc_mvs0_vpp0_gdsc = {
	.gdscr = 0x8120,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "video_cc_mvs0_vpp0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = HW_CTRL_TRIGGER | POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc video_cc_mvs0_vpp1_gdsc = {
	.gdscr = 0x80f4,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "video_cc_mvs0_vpp1_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
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
	.flags = HW_CTRL_TRIGGER | POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc video_cc_mvs0c_gdsc = {
	.gdscr = 0x814c,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x6,
	.pd = {
		.name = "video_cc_mvs0c_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc video_cc_mvs0_gdsc = {
	.gdscr = 0x80a8,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x6,
	.pd = {
		.name = "video_cc_mvs0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.parent = &video_cc_mvs0c_gdsc.pd,
	.flags = HW_CTRL_TRIGGER | POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct clk_regmap *video_cc_kaanapali_clocks[] = {
	[VIDEO_CC_AHB_CLK_SRC] = &video_cc_ahb_clk_src.clkr,
	[VIDEO_CC_MVS0_CLK] = &video_cc_mvs0_clk.clkr,
	[VIDEO_CC_MVS0_CLK_SRC] = &video_cc_mvs0_clk_src.clkr,
	[VIDEO_CC_MVS0_FREERUN_CLK] = &video_cc_mvs0_freerun_clk.branch.clkr,
	[VIDEO_CC_MVS0_SHIFT_CLK] = &video_cc_mvs0_shift_clk.clkr,
	[VIDEO_CC_MVS0_VPP0_CLK] = &video_cc_mvs0_vpp0_clk.clkr,
	[VIDEO_CC_MVS0_VPP0_FREERUN_CLK] = &video_cc_mvs0_vpp0_freerun_clk.clkr,
	[VIDEO_CC_MVS0_VPP1_CLK] = &video_cc_mvs0_vpp1_clk.clkr,
	[VIDEO_CC_MVS0_VPP1_FREERUN_CLK] = &video_cc_mvs0_vpp1_freerun_clk.clkr,
	[VIDEO_CC_MVS0A_CLK] = &video_cc_mvs0a_clk.clkr,
	[VIDEO_CC_MVS0A_CLK_SRC] = &video_cc_mvs0a_clk_src.clkr,
	[VIDEO_CC_MVS0A_FREERUN_CLK] = &video_cc_mvs0a_freerun_clk.clkr,
	[VIDEO_CC_MVS0B_CLK] = &video_cc_mvs0b_clk.clkr,
	[VIDEO_CC_MVS0B_CLK_SRC] = &video_cc_mvs0b_clk_src.clkr,
	[VIDEO_CC_MVS0B_FREERUN_CLK] = &video_cc_mvs0b_freerun_clk.clkr,
	[VIDEO_CC_MVS0C_CLK] = &video_cc_mvs0c_clk.clkr,
	[VIDEO_CC_MVS0C_CLK_SRC] = &video_cc_mvs0c_clk_src.clkr,
	[VIDEO_CC_MVS0C_FREERUN_CLK] = &video_cc_mvs0c_freerun_clk.clkr,
	[VIDEO_CC_MVS0C_SHIFT_CLK] = &video_cc_mvs0c_shift_clk.clkr,
	[VIDEO_CC_PLL0] = &video_cc_pll0.clkr,
	[VIDEO_CC_PLL1] = &video_cc_pll1.clkr,
	[VIDEO_CC_PLL2] = &video_cc_pll2.clkr,
	[VIDEO_CC_PLL3] = &video_cc_pll3.clkr,
	[VIDEO_CC_XO_CLK_SRC] = &video_cc_xo_clk_src.clkr,
};

static struct gdsc *video_cc_kaanapali_gdscs[] = {
	[VIDEO_CC_MVS0A_GDSC] = &video_cc_mvs0a_gdsc,
	[VIDEO_CC_MVS0_GDSC] = &video_cc_mvs0_gdsc,
	[VIDEO_CC_MVS0_VPP1_GDSC] = &video_cc_mvs0_vpp1_gdsc,
	[VIDEO_CC_MVS0_VPP0_GDSC] = &video_cc_mvs0_vpp0_gdsc,
	[VIDEO_CC_MVS0C_GDSC] = &video_cc_mvs0c_gdsc,
};

static const struct qcom_reset_map video_cc_kaanapali_resets[] = {
	[VIDEO_CC_INTERFACE_BCR] = { 0x8178 },
	[VIDEO_CC_MVS0_BCR] = { 0x80a4 },
	[VIDEO_CC_MVS0_VPP0_BCR] = { 0x811c },
	[VIDEO_CC_MVS0_VPP1_BCR] = { 0x80f0 },
	[VIDEO_CC_MVS0A_BCR] = { 0x8078 },
	[VIDEO_CC_MVS0C_CLK_ARES] = { 0x8164, 2 },
	[VIDEO_CC_MVS0C_BCR] = { 0x8148 },
	[VIDEO_CC_MVS0_FREERUN_CLK_ARES] = { 0x80e0, 2 },
	[VIDEO_CC_MVS0C_FREERUN_CLK_ARES] = { 0x8174, 2 },
	[VIDEO_CC_XO_CLK_ARES] = { 0x81ac, 2 },
};

static struct clk_alpha_pll *video_cc_kaanapali_plls[] = {
	&video_cc_pll0,
	&video_cc_pll1,
	&video_cc_pll2,
	&video_cc_pll3,
};

static u32 video_cc_kaanapali_critical_cbcrs[] = {
	0x817c, /* VIDEO_CC_AHB_CLK */
	0x81bc, /* VIDEO_CC_SLEEP_CLK */
	0x81b0, /* VIDEO_CC_TS_XO_CLK */
	0x81ac, /* VIDEO_CC_XO_CLK */
};

static const struct regmap_config video_cc_kaanapali_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xa010,
	.fast_io = true,
};

static void clk_kaanapali_regs_configure(struct device *dev, struct regmap *regmap)
{
	/*
	 * Enable clk_on sync for MVS0 and VPP clocks via VIDEO_CC_SPARE1
	 * during core reset by default.
	 */
	regmap_set_bits(regmap, 0x9f24, BIT(0));

	/*
	 *	As per HW design recommendation
	 *	Update DLY_ACCU_RED_SHIFTER_DONE to 0xF for the below GDSCs
	 *	MVS0A CFG3, MVS0 CFG3, MVS0 VPP1 CFG3, MVS0 VPP0 CFG3, MVS0C CFG3
	 */
	regmap_set_bits(regmap, 0x8088, ACCU_CFG_MASK);
	regmap_set_bits(regmap, 0x80b4, ACCU_CFG_MASK);
	regmap_set_bits(regmap, 0x8100, ACCU_CFG_MASK);
	regmap_set_bits(regmap, 0x812c, ACCU_CFG_MASK);
	regmap_set_bits(regmap, 0x8158, ACCU_CFG_MASK);
}

static struct qcom_cc_driver_data video_cc_kaanapali_driver_data = {
	.alpha_plls = video_cc_kaanapali_plls,
	.num_alpha_plls = ARRAY_SIZE(video_cc_kaanapali_plls),
	.clk_cbcrs = video_cc_kaanapali_critical_cbcrs,
	.num_clk_cbcrs = ARRAY_SIZE(video_cc_kaanapali_critical_cbcrs),
	.clk_regs_configure = clk_kaanapali_regs_configure,
};

static const struct qcom_cc_desc video_cc_kaanapali_desc = {
	.config = &video_cc_kaanapali_regmap_config,
	.clks = video_cc_kaanapali_clocks,
	.num_clks = ARRAY_SIZE(video_cc_kaanapali_clocks),
	.resets = video_cc_kaanapali_resets,
	.num_resets = ARRAY_SIZE(video_cc_kaanapali_resets),
	.gdscs = video_cc_kaanapali_gdscs,
	.num_gdscs = ARRAY_SIZE(video_cc_kaanapali_gdscs),
	.use_rpm = true,
	.driver_data = &video_cc_kaanapali_driver_data,
};

static const struct of_device_id video_cc_kaanapali_match_table[] = {
	{ .compatible = "qcom,kaanapali-videocc" },
	{ }
};
MODULE_DEVICE_TABLE(of, video_cc_kaanapali_match_table);

static int video_cc_kaanapali_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &video_cc_kaanapali_desc);
}

static struct platform_driver video_cc_kaanapali_driver = {
	.probe = video_cc_kaanapali_probe,
	.driver = {
		.name = "videocc-kaanapali",
		.of_match_table = video_cc_kaanapali_match_table,
	},
};

module_platform_driver(video_cc_kaanapali_driver);

MODULE_DESCRIPTION("QTI VIDEOCC Kaanapali Driver");
MODULE_LICENSE("GPL");
