// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,videocc-seraph.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "common.h"
#include "reset.h"
#include "vdd-level.h"

static DEFINE_VDD_REGULATORS(vdd_mm, VDD_HIGH_L1 + 1, 1, vdd_corner);
static DEFINE_VDD_REGULATORS(vdd_mx, VDD_HIGH_L1 + 1, 1, vdd_corner);

static struct clk_vdd_class *video_cc_seraph_regulators[] = {
	&vdd_mm,
	&vdd_mx,
};

enum {
	P_BI_TCXO,
	P_VIDEO_CC_PLL0_OUT_MAIN,
	P_VIDEO_CC_PLL1_OUT_MAIN,
	P_VIDEO_CC_PLL2_OUT_MAIN,
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
	.config_ctl_hi_val = 0x0a8060e0,
	.config_ctl_hi1_val = 0xf51dea20,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00000002,
};

static struct clk_alpha_pll video_cc_pll0 = {
	.offset = 0x0,
	.vco_table = taycan_eko_t_vco,
	.num_vco = ARRAY_SIZE(taycan_eko_t_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EKO_T],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_pll0",
			.parent_data = &(const struct clk_parent_data) {
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_taycan_eko_t_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_mx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER_D1] = 621000000,
				[VDD_LOW] = 1600000000,
				[VDD_NOMINAL] = 2000000000,
				[VDD_HIGH] = 2500000000},
		},
	},
};

/* 480.0 MHz Configuration */
static const struct alpha_pll_config video_cc_pll1_config = {
	.l = 0x19,
	.cal_l = 0x48,
	.alpha = 0x0,
	.config_ctl_val = 0x25c400e7,
	.config_ctl_hi_val = 0x0a8060e0,
	.config_ctl_hi1_val = 0xf51dea20,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00000002,
};

static struct clk_alpha_pll video_cc_pll1 = {
	.offset = 0x1000,
	.vco_table = taycan_eko_t_vco,
	.num_vco = ARRAY_SIZE(taycan_eko_t_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EKO_T],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_pll1",
			.parent_data = &(const struct clk_parent_data) {
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_taycan_eko_t_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_mx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER_D1] = 621000000,
				[VDD_LOW] = 1600000000,
				[VDD_NOMINAL] = 2000000000,
				[VDD_HIGH] = 2500000000},
		},
	},
};

/* 480.0 MHz Configuration */
static const struct alpha_pll_config video_cc_pll2_config = {
	.l = 0x19,
	.cal_l = 0x48,
	.alpha = 0x0,
	.config_ctl_val = 0x25c400e7,
	.config_ctl_hi_val = 0x0a8060e0,
	.config_ctl_hi1_val = 0xf51dea20,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00000002,
};

static struct clk_alpha_pll video_cc_pll2 = {
	.offset = 0x2000,
	.vco_table = taycan_eko_t_vco,
	.num_vco = ARRAY_SIZE(taycan_eko_t_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EKO_T],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_pll2",
			.parent_data = &(const struct clk_parent_data) {
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_taycan_eko_t_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_mx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER_D1] = 621000000,
				[VDD_LOW] = 1600000000,
				[VDD_NOMINAL] = 2000000000,
				[VDD_HIGH] = 2500000000},
		},
	},
};

static const struct parent_map video_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
};

static const struct clk_parent_data video_cc_parent_data_0[] = {
	{ .fw_name = "bi_tcxo" },
};

static const struct parent_map video_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_VIDEO_CC_PLL1_OUT_MAIN, 1 },
};

static const struct clk_parent_data video_cc_parent_data_1[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &video_cc_pll1.clkr.hw },
};

static const struct parent_map video_cc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_VIDEO_CC_PLL2_OUT_MAIN, 1 },
};

static const struct clk_parent_data video_cc_parent_data_2[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &video_cc_pll2.clkr.hw },
};

static const struct parent_map video_cc_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
	{ P_VIDEO_CC_PLL0_OUT_MAIN, 1 },
};

static const struct clk_parent_data video_cc_parent_data_3[] = {
	{ .fw_name = "bi_tcxo" },
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
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_ahb_clk_src",
		.parent_data = video_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 19200000},
	},
};

static const struct freq_tbl ftbl_video_cc_mvs0_clk_src[] = {
	F(240000000, P_VIDEO_CC_PLL1_OUT_MAIN, 2, 0, 0),
	F(338000000, P_VIDEO_CC_PLL1_OUT_MAIN, 2, 0, 0),
	F(420000000, P_VIDEO_CC_PLL1_OUT_MAIN, 2, 0, 0),
	F(444000000, P_VIDEO_CC_PLL1_OUT_MAIN, 2, 0, 0),
	F(533000000, P_VIDEO_CC_PLL1_OUT_MAIN, 2, 0, 0),
	F(630000000, P_VIDEO_CC_PLL1_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_mvs0_clk_src = {
	.cmd_rcgr = 0x8030,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_1,
	.freq_tbl = ftbl_video_cc_mvs0_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_mvs0_clk_src",
		.parent_data = video_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = video_cc_seraph_regulators,
		.num_vdd_classes = ARRAY_SIZE(video_cc_seraph_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 240000000,
			[VDD_LOWER] = 338000000,
			[VDD_LOW] = 420000000,
			[VDD_LOW_L1] = 444000000,
			[VDD_NOMINAL] = 533000000,
			[VDD_HIGH] = 630000000},
	},
};

static const struct freq_tbl ftbl_video_cc_mvs0b_clk_src[] = {
	F(240000000, P_VIDEO_CC_PLL2_OUT_MAIN, 2, 0, 0),
	F(338000000, P_VIDEO_CC_PLL2_OUT_MAIN, 2, 0, 0),
	F(420000000, P_VIDEO_CC_PLL2_OUT_MAIN, 2, 0, 0),
	F(444000000, P_VIDEO_CC_PLL2_OUT_MAIN, 2, 0, 0),
	F(533000000, P_VIDEO_CC_PLL2_OUT_MAIN, 2, 0, 0),
	F(630000000, P_VIDEO_CC_PLL2_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_mvs0b_clk_src = {
	.cmd_rcgr = 0x8018,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_2,
	.freq_tbl = ftbl_video_cc_mvs0b_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_mvs0b_clk_src",
		.parent_data = video_cc_parent_data_2,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = video_cc_seraph_regulators,
		.num_vdd_classes = ARRAY_SIZE(video_cc_seraph_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 240000000,
			[VDD_LOWER] = 338000000,
			[VDD_LOW] = 420000000,
			[VDD_LOW_L1] = 444000000,
			[VDD_NOMINAL] = 533000000,
			[VDD_HIGH] = 630000000},
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
	.parent_map = video_cc_parent_map_3,
	.freq_tbl = ftbl_video_cc_mvs0c_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_mvs0c_clk_src",
		.parent_data = video_cc_parent_data_3,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = video_cc_seraph_regulators,
		.num_vdd_classes = ARRAY_SIZE(video_cc_seraph_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 360000000,
			[VDD_LOWER] = 507000000,
			[VDD_LOW] = 630000000,
			[VDD_LOW_L1] = 666000000,
			[VDD_NOMINAL] = 800000000,
			[VDD_HIGH] = 1104000000,
			[VDD_HIGH_L1] = 1260000000},
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
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 19200000},
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
	.mem_enable_ack_mask = 0xc00,
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
				.ops = &clk_branch2_mem_ops,
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

static struct clk_regmap *video_cc_seraph_clocks[] = {
	[VIDEO_CC_AHB_CLK_SRC] = &video_cc_ahb_clk_src.clkr,
	[VIDEO_CC_MVS0_CLK] = &video_cc_mvs0_clk.clkr,
	[VIDEO_CC_MVS0_CLK_SRC] = &video_cc_mvs0_clk_src.clkr,
	[VIDEO_CC_MVS0_FREERUN_CLK] = &video_cc_mvs0_freerun_clk.branch.clkr,
	[VIDEO_CC_MVS0_SHIFT_CLK] = &video_cc_mvs0_shift_clk.clkr,
	[VIDEO_CC_MVS0_VPP0_CLK] = &video_cc_mvs0_vpp0_clk.clkr,
	[VIDEO_CC_MVS0_VPP0_FREERUN_CLK] = &video_cc_mvs0_vpp0_freerun_clk.clkr,
	[VIDEO_CC_MVS0_VPP1_CLK] = &video_cc_mvs0_vpp1_clk.clkr,
	[VIDEO_CC_MVS0_VPP1_FREERUN_CLK] = &video_cc_mvs0_vpp1_freerun_clk.clkr,
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
	[VIDEO_CC_XO_CLK_SRC] = &video_cc_xo_clk_src.clkr,
};

static const struct qcom_reset_map video_cc_seraph_resets[] = {
	[VIDEO_CC_INTERFACE_BCR] = { 0x8178 },
	[VIDEO_CC_MVS0_BCR] = { 0x80a4 },
	[VIDEO_CC_MVS0_FREERUN_CLK_ARES] = { 0x80e0, 2 },
	[VIDEO_CC_MVS0_VPP0_BCR] = { 0x811c },
	[VIDEO_CC_MVS0_VPP1_BCR] = { 0x80f0 },
	[VIDEO_CC_MVS0C_CLK_ARES] = { 0x8164, 2 },
	[VIDEO_CC_MVS0C_BCR] = { 0x8148 },
	[VIDEO_CC_MVS0C_FREERUN_CLK_ARES] = { 0x8174, 2 },
	[VIDEO_CC_XO_CLK_ARES] = { 0x81ac, 2 },
};

static const struct regmap_config video_cc_seraph_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xa00c,
	.fast_io = true,
};

static struct qcom_cc_desc video_cc_seraph_desc = {
	.config = &video_cc_seraph_regmap_config,
	.clks = video_cc_seraph_clocks,
	.num_clks = ARRAY_SIZE(video_cc_seraph_clocks),
	.resets = video_cc_seraph_resets,
	.num_resets = ARRAY_SIZE(video_cc_seraph_resets),
	.clk_regulators = video_cc_seraph_regulators,
	.num_clk_regulators = ARRAY_SIZE(video_cc_seraph_regulators),
};

static const struct of_device_id video_cc_seraph_match_table[] = {
	{ .compatible = "qcom,seraph-videocc" },
	{ }
};
MODULE_DEVICE_TABLE(of, video_cc_seraph_match_table);

static int video_cc_seraph_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	unsigned int accu_cfg_mask = 0x1f << 21;
	int ret;

	regmap = qcom_cc_map(pdev, &video_cc_seraph_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ret = qcom_cc_runtime_init(pdev, &video_cc_seraph_desc);
	if (ret)
		return ret;

	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret)
		return ret;

	clk_taycan_eko_t_pll_configure(&video_cc_pll0, regmap, &video_cc_pll0_config);
	clk_taycan_eko_t_pll_configure(&video_cc_pll1, regmap, &video_cc_pll1_config);
	clk_taycan_eko_t_pll_configure(&video_cc_pll2, regmap, &video_cc_pll2_config);

	/*
	 *	Maximize ctl data download delay and enable memory redundancy
	 *	MVS0 CFG3
	 *	MVS0 VPP1 CFG3
	 *	MVS0 VPP0 CFG3
	 *	MVS0C CFG3
	 */
	regmap_update_bits(regmap, 0x80b4, accu_cfg_mask, accu_cfg_mask);
	regmap_update_bits(regmap, 0x8100, accu_cfg_mask, accu_cfg_mask);
	regmap_update_bits(regmap, 0x812c, accu_cfg_mask, accu_cfg_mask);
	regmap_update_bits(regmap, 0x8158, accu_cfg_mask, accu_cfg_mask);

	/*
	 * Keep clocks always enabled:
	 *	video_cc_ahb_clk
	 *	video_cc_sleep_clk
	 *	video_cc_ts_xo_clk
	 *	video_cc_xo_clk
	 */
	regmap_update_bits(regmap, 0x817c, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x81bc, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x81b0, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x81ac, BIT(0), BIT(0));

	ret = qcom_cc_really_probe(pdev, &video_cc_seraph_desc, regmap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register VIDEO CC clocks ret=%d\n", ret);
		return ret;
	}

	pm_runtime_put_sync(&pdev->dev);
	dev_info(&pdev->dev, "Registered VIDEO CC clocks\n");

	return ret;
}

static void video_cc_seraph_sync_state(struct device *dev)
{
	qcom_cc_sync_state(dev, &video_cc_seraph_desc);
}

static const struct dev_pm_ops video_cc_seraph_pm_ops = {
	SET_RUNTIME_PM_OPS(qcom_cc_runtime_suspend, qcom_cc_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static struct platform_driver video_cc_seraph_driver = {
	.probe = video_cc_seraph_probe,
	.driver = {
		.name = "videocc-seraph",
		.of_match_table = video_cc_seraph_match_table,
		.sync_state = video_cc_seraph_sync_state,
		.pm = &video_cc_seraph_pm_ops,
	},
};

module_platform_driver(video_cc_seraph_driver);

MODULE_DESCRIPTION("QTI VIDEOCC SERAPH Driver");
MODULE_LICENSE("GPL");
