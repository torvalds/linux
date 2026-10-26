// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,nord-camcc.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "common.h"
#include "gdsc.h"
#include "reset.h"

enum {
	DT_IFACE,
	DT_BI_TCXO,
	DT_BI_TCXO_AO,
	DT_SLEEP_CLK
};

enum {
	P_BI_TCXO,
	P_CAM_CC_PLL0_OUT_EVEN,
	P_CAM_CC_PLL0_OUT_MAIN,
	P_CAM_CC_PLL0_OUT_ODD,
	P_CAM_CC_PLL2_OUT_EVEN,
	P_CAM_CC_PLL3_OUT_EVEN,
	P_CAM_CC_PLL4_OUT_EVEN,
	P_CAM_CC_PLL5_OUT_EVEN,
	P_CAM_CC_PLL6_OUT_EVEN,
	P_SLEEP_CLK,
};

static const struct pll_vco lucid_ole_vco[] = {
	{ 249600000, 2300000000, 0 },
};

/* 1200.0 MHz Configuration */
static const struct alpha_pll_config cam_cc_pll0_config = {
	.l = 0x3e,
	.alpha = 0x8000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x82aa299c,
	.test_ctl_val = 0x00000000,
	.test_ctl_hi_val = 0x00000003,
	.test_ctl_hi1_val = 0x00009000,
	.test_ctl_hi2_val = 0x00000034,
	.user_ctl_val = 0x00008400,
	.user_ctl_hi_val = 0x00400005,
};

static struct clk_alpha_pll cam_cc_pll0 = {
	.offset = 0x0,
	.config = &cam_cc_pll0_config,
	.vco_table = lucid_ole_vco,
	.num_vco = ARRAY_SIZE(lucid_ole_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_pll0",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO_AO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_evo_ops,
		},
	},
};

static const struct clk_div_table post_div_table_cam_cc_pll0_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv cam_cc_pll0_out_even = {
	.offset = 0x0,
	.post_div_shift = 10,
	.post_div_table = post_div_table_cam_cc_pll0_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_cam_cc_pll0_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_pll0_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&cam_cc_pll0.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_lucid_evo_ops,
	},
};

static const struct clk_div_table post_div_table_cam_cc_pll0_out_odd[] = {
	{ 0x2, 3 },
	{ }
};

static struct clk_alpha_pll_postdiv cam_cc_pll0_out_odd = {
	.offset = 0x0,
	.post_div_shift = 14,
	.post_div_table = post_div_table_cam_cc_pll0_out_odd,
	.num_post_div = ARRAY_SIZE(post_div_table_cam_cc_pll0_out_odd),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_pll0_out_odd",
		.parent_hws = (const struct clk_hw*[]) {
			&cam_cc_pll0.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_lucid_evo_ops,
	},
};

/* 720.0 MHz Configuration */
static const struct alpha_pll_config cam_cc_pll2_config = {
	.l = 0x25,
	.alpha = 0x8000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x82aa299c,
	.test_ctl_val = 0x00000000,
	.test_ctl_hi_val = 0x00000003,
	.test_ctl_hi1_val = 0x00009000,
	.test_ctl_hi2_val = 0x00000034,
	.user_ctl_val = 0x00000400,
	.user_ctl_hi_val = 0x00400005,
};

static struct clk_alpha_pll cam_cc_pll2 = {
	.offset = 0x2000,
	.config = &cam_cc_pll2_config,
	.vco_table = lucid_ole_vco,
	.num_vco = ARRAY_SIZE(lucid_ole_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_pll2",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_evo_ops,
		},
	},
};

static const struct clk_div_table post_div_table_cam_cc_pll2_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv cam_cc_pll2_out_even = {
	.offset = 0x2000,
	.post_div_shift = 10,
	.post_div_table = post_div_table_cam_cc_pll2_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_cam_cc_pll2_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_pll2_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&cam_cc_pll2.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_lucid_evo_ops,
	},
};

/* 720.0 MHz Configuration */
static const struct alpha_pll_config cam_cc_pll3_config = {
	.l = 0x25,
	.alpha = 0x8000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x82aa299c,
	.test_ctl_val = 0x00000000,
	.test_ctl_hi_val = 0x00000003,
	.test_ctl_hi1_val = 0x00009000,
	.test_ctl_hi2_val = 0x00000034,
	.user_ctl_val = 0x00000400,
	.user_ctl_hi_val = 0x00400005,
};

static struct clk_alpha_pll cam_cc_pll3 = {
	.offset = 0x3000,
	.config = &cam_cc_pll3_config,
	.vco_table = lucid_ole_vco,
	.num_vco = ARRAY_SIZE(lucid_ole_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_pll3",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_evo_ops,
		},
	},
};

static const struct clk_div_table post_div_table_cam_cc_pll3_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv cam_cc_pll3_out_even = {
	.offset = 0x3000,
	.post_div_shift = 10,
	.post_div_table = post_div_table_cam_cc_pll3_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_cam_cc_pll3_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_pll3_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&cam_cc_pll3.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_lucid_evo_ops,
	},
};

/* 720.0 MHz Configuration */
static const struct alpha_pll_config cam_cc_pll4_config = {
	.l = 0x25,
	.alpha = 0x8000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x82aa299c,
	.test_ctl_val = 0x00000000,
	.test_ctl_hi_val = 0x00000003,
	.test_ctl_hi1_val = 0x00009000,
	.test_ctl_hi2_val = 0x00000034,
	.user_ctl_val = 0x00000400,
	.user_ctl_hi_val = 0x00400005,
};

static struct clk_alpha_pll cam_cc_pll4 = {
	.offset = 0x4000,
	.config = &cam_cc_pll4_config,
	.vco_table = lucid_ole_vco,
	.num_vco = ARRAY_SIZE(lucid_ole_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_pll4",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_evo_ops,
		},
	},
};

static const struct clk_div_table post_div_table_cam_cc_pll4_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv cam_cc_pll4_out_even = {
	.offset = 0x4000,
	.post_div_shift = 10,
	.post_div_table = post_div_table_cam_cc_pll4_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_cam_cc_pll4_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_pll4_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&cam_cc_pll4.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_lucid_evo_ops,
	},
};

/* 720.0 MHz Configuration */
static const struct alpha_pll_config cam_cc_pll5_config = {
	.l = 0x25,
	.alpha = 0x8000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x82aa299c,
	.test_ctl_val = 0x00000000,
	.test_ctl_hi_val = 0x00000003,
	.test_ctl_hi1_val = 0x00009000,
	.test_ctl_hi2_val = 0x00000034,
	.user_ctl_val = 0x00000400,
	.user_ctl_hi_val = 0x00400005,
};

static struct clk_alpha_pll cam_cc_pll5 = {
	.offset = 0x5000,
	.config = &cam_cc_pll5_config,
	.vco_table = lucid_ole_vco,
	.num_vco = ARRAY_SIZE(lucid_ole_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_pll5",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_evo_ops,
		},
	},
};

static const struct clk_div_table post_div_table_cam_cc_pll5_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv cam_cc_pll5_out_even = {
	.offset = 0x5000,
	.post_div_shift = 10,
	.post_div_table = post_div_table_cam_cc_pll5_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_cam_cc_pll5_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_pll5_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&cam_cc_pll5.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_lucid_evo_ops,
	},
};

/* 720.0 MHz Configuration */
static const struct alpha_pll_config cam_cc_pll6_config = {
	.l = 0x25,
	.alpha = 0x8000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x82aa299c,
	.test_ctl_val = 0x00000000,
	.test_ctl_hi_val = 0x00000003,
	.test_ctl_hi1_val = 0x00009000,
	.test_ctl_hi2_val = 0x00000034,
	.user_ctl_val = 0x00000400,
	.user_ctl_hi_val = 0x00400005,
};

static struct clk_alpha_pll cam_cc_pll6 = {
	.offset = 0x6000,
	.config = &cam_cc_pll6_config,
	.vco_table = lucid_ole_vco,
	.num_vco = ARRAY_SIZE(lucid_ole_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_pll6",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_evo_ops,
		},
	},
};

static const struct clk_div_table post_div_table_cam_cc_pll6_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv cam_cc_pll6_out_even = {
	.offset = 0x6000,
	.post_div_shift = 10,
	.post_div_table = post_div_table_cam_cc_pll6_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_cam_cc_pll6_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_pll6_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&cam_cc_pll6.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_lucid_evo_ops,
	},
};

static const struct parent_map cam_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL0_OUT_MAIN, 1 },
	{ P_CAM_CC_PLL0_OUT_EVEN, 2 },
	{ P_CAM_CC_PLL0_OUT_ODD, 3 },
};

static const struct clk_parent_data cam_cc_parent_data_0[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &cam_cc_pll0.clkr.hw },
	{ .hw = &cam_cc_pll0_out_even.clkr.hw },
	{ .hw = &cam_cc_pll0_out_odd.clkr.hw },
};

static const struct parent_map cam_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL0_OUT_MAIN, 1 },
	{ P_CAM_CC_PLL0_OUT_EVEN, 2 },
	{ P_CAM_CC_PLL0_OUT_ODD, 3 },
};

static const struct clk_parent_data cam_cc_parent_data_1[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &cam_cc_pll0.clkr.hw },
	{ .hw = &cam_cc_pll0_out_even.clkr.hw },
	{ .hw = &cam_cc_pll0_out_odd.clkr.hw },
};

static const struct parent_map cam_cc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL0_OUT_MAIN, 1 },
	{ P_CAM_CC_PLL0_OUT_EVEN, 2 },
	{ P_CAM_CC_PLL3_OUT_EVEN, 3 },
};

static const struct clk_parent_data cam_cc_parent_data_2[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &cam_cc_pll0.clkr.hw },
	{ .hw = &cam_cc_pll0_out_even.clkr.hw },
	{ .hw = &cam_cc_pll3_out_even.clkr.hw },
};

static const struct parent_map cam_cc_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL2_OUT_EVEN, 2 },
};

static const struct clk_parent_data cam_cc_parent_data_3[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &cam_cc_pll2_out_even.clkr.hw },
};

static const struct parent_map cam_cc_parent_map_4[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL4_OUT_EVEN, 6 },
};

static const struct clk_parent_data cam_cc_parent_data_4[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &cam_cc_pll4_out_even.clkr.hw },
};

static const struct parent_map cam_cc_parent_map_5[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL5_OUT_EVEN, 6 },
};

static const struct clk_parent_data cam_cc_parent_data_5[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &cam_cc_pll5_out_even.clkr.hw },
};

static const struct parent_map cam_cc_parent_map_6[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL6_OUT_EVEN, 6 },
};

static const struct clk_parent_data cam_cc_parent_data_6[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &cam_cc_pll6_out_even.clkr.hw },
};

static const struct parent_map cam_cc_parent_map_7[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL0_OUT_MAIN, 1 },
	{ P_CAM_CC_PLL0_OUT_EVEN, 2 },
};

static const struct clk_parent_data cam_cc_parent_data_7[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &cam_cc_pll0.clkr.hw },
	{ .hw = &cam_cc_pll0_out_even.clkr.hw },
};

static const struct parent_map cam_cc_parent_map_8[] = {
	{ P_SLEEP_CLK, 0 },
};

static const struct clk_parent_data cam_cc_parent_data_8[] = {
	{ .index = DT_SLEEP_CLK },
};

static const struct parent_map cam_cc_parent_map_9[] = {
	{ P_BI_TCXO, 0 },
};

static const struct clk_parent_data cam_cc_parent_data_9[] = {
	{ .index = DT_BI_TCXO },
};

static const struct freq_tbl ftbl_cam_cc_camnoc_rt_axi_clk_src[] = {
	F(300000000, P_CAM_CC_PLL0_OUT_EVEN, 2, 0, 0),
	F(400000000, P_CAM_CC_PLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_camnoc_rt_axi_clk_src = {
	.cmd_rcgr = 0x13244,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_camnoc_rt_axi_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_camnoc_rt_axi_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_cci_0_clk_src[] = {
	F(37500000, P_CAM_CC_PLL0_OUT_EVEN, 16, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_cci_0_clk_src = {
	.cmd_rcgr = 0x13110,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_cci_0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_cci_0_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_cci_1_clk_src = {
	.cmd_rcgr = 0x13130,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_cci_0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_cci_1_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_cci_2_clk_src = {
	.cmd_rcgr = 0x13150,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_cci_0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_cci_2_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_cci_3_clk_src = {
	.cmd_rcgr = 0x13170,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_cci_0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_cci_3_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_cci_4_clk_src = {
	.cmd_rcgr = 0x13190,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_cci_0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_cci_4_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_cphy_rx_clk_src[] = {
	F(300000000, P_CAM_CC_PLL0_OUT_EVEN, 2, 0, 0),
	F(400000000, P_CAM_CC_PLL0_OUT_ODD, 1, 0, 0),
	F(480000000, P_CAM_CC_PLL0_OUT_MAIN, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_cphy_rx_clk_src = {
	.cmd_rcgr = 0x120ac,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_1,
	.freq_tbl = ftbl_cam_cc_cphy_rx_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_cphy_rx_clk_src",
		.parent_data = cam_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_csi0phytimer_clk_src[] = {
	F(300000000, P_CAM_CC_PLL0_OUT_EVEN, 2, 0, 0),
	F(400000000, P_CAM_CC_PLL0_OUT_ODD, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_csi0phytimer_clk_src = {
	.cmd_rcgr = 0x10000,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_1,
	.freq_tbl = ftbl_cam_cc_csi0phytimer_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_csi0phytimer_clk_src",
		.parent_data = cam_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_csi1phytimer_clk_src = {
	.cmd_rcgr = 0x10028,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_1,
	.freq_tbl = ftbl_cam_cc_csi0phytimer_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_csi1phytimer_clk_src",
		.parent_data = cam_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_csi2phytimer_clk_src = {
	.cmd_rcgr = 0x1004c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_1,
	.freq_tbl = ftbl_cam_cc_csi0phytimer_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_csi2phytimer_clk_src",
		.parent_data = cam_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_csi3phytimer_clk_src = {
	.cmd_rcgr = 0x10070,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_1,
	.freq_tbl = ftbl_cam_cc_csi0phytimer_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_csi3phytimer_clk_src",
		.parent_data = cam_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_csi4phytimer_clk_src = {
	.cmd_rcgr = 0x10094,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_1,
	.freq_tbl = ftbl_cam_cc_csi0phytimer_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_csi4phytimer_clk_src",
		.parent_data = cam_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_csid_clk_src[] = {
	F(300000000, P_CAM_CC_PLL0_OUT_MAIN, 4, 0, 0),
	F(400000000, P_CAM_CC_PLL0_OUT_MAIN, 3, 0, 0),
	F(480000000, P_CAM_CC_PLL0_OUT_MAIN, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_csid_clk_src = {
	.cmd_rcgr = 0x13214,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_1,
	.freq_tbl = ftbl_cam_cc_csid_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_csid_clk_src",
		.parent_data = cam_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_fast_ahb_clk_src[] = {
	F(200000000, P_CAM_CC_PLL0_OUT_EVEN, 3, 0, 0),
	F(300000000, P_CAM_CC_PLL0_OUT_MAIN, 4, 0, 0),
	F(400000000, P_CAM_CC_PLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_fast_ahb_clk_src = {
	.cmd_rcgr = 0x131dc,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_fast_ahb_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_fast_ahb_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_icp_0_clk_src[] = {
	F(360000000, P_CAM_CC_PLL3_OUT_EVEN, 1, 0, 0),
	F(480000000, P_CAM_CC_PLL3_OUT_EVEN, 1, 0, 0),
	F(600000000, P_CAM_CC_PLL3_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_icp_0_clk_src = {
	.cmd_rcgr = 0x130a4,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_2,
	.freq_tbl = ftbl_cam_cc_icp_0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_icp_0_clk_src",
		.parent_data = cam_cc_parent_data_2,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_icp_1_clk_src = {
	.cmd_rcgr = 0x130dc,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_2,
	.freq_tbl = ftbl_cam_cc_icp_0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_icp_1_clk_src",
		.parent_data = cam_cc_parent_data_2,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_ife_0_main_clk_src[] = {
	F(360000000, P_CAM_CC_PLL4_OUT_EVEN, 1, 0, 0),
	F(480000000, P_CAM_CC_PLL4_OUT_EVEN, 1, 0, 0),
	F(650000000, P_CAM_CC_PLL4_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_ife_0_main_clk_src = {
	.cmd_rcgr = 0x12018,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_4,
	.freq_tbl = ftbl_cam_cc_ife_0_main_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_ife_0_main_clk_src",
		.parent_data = cam_cc_parent_data_4,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_4),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_ife_1_main_clk_src[] = {
	F(360000000, P_CAM_CC_PLL5_OUT_EVEN, 1, 0, 0),
	F(480000000, P_CAM_CC_PLL5_OUT_EVEN, 1, 0, 0),
	F(650000000, P_CAM_CC_PLL5_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_ife_1_main_clk_src = {
	.cmd_rcgr = 0x120dc,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_5,
	.freq_tbl = ftbl_cam_cc_ife_1_main_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_ife_1_main_clk_src",
		.parent_data = cam_cc_parent_data_5,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_5),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_ife_2_main_clk_src[] = {
	F(360000000, P_CAM_CC_PLL6_OUT_EVEN, 1, 0, 0),
	F(480000000, P_CAM_CC_PLL6_OUT_EVEN, 1, 0, 0),
	F(650000000, P_CAM_CC_PLL6_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_ife_2_main_clk_src = {
	.cmd_rcgr = 0x12188,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_6,
	.freq_tbl = ftbl_cam_cc_ife_2_main_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_ife_2_main_clk_src",
		.parent_data = cam_cc_parent_data_6,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_6),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_ife_lite_clk_src = {
	.cmd_rcgr = 0x13000,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_1,
	.freq_tbl = ftbl_cam_cc_csid_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_ife_lite_clk_src",
		.parent_data = cam_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_ife_lite_csid_clk_src = {
	.cmd_rcgr = 0x13024,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_1,
	.freq_tbl = ftbl_cam_cc_csid_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_ife_lite_csid_clk_src",
		.parent_data = cam_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_ipe_0_clk_src[] = {
	F(360000000, P_CAM_CC_PLL2_OUT_EVEN, 1, 0, 0),
	F(480000000, P_CAM_CC_PLL2_OUT_EVEN, 1, 0, 0),
	F(650000000, P_CAM_CC_PLL2_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_ipe_0_clk_src = {
	.cmd_rcgr = 0x11018,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_3,
	.freq_tbl = ftbl_cam_cc_ipe_0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_ipe_0_clk_src",
		.parent_data = cam_cc_parent_data_3,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_ipe_1_clk_src = {
	.cmd_rcgr = 0x11074,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_3,
	.freq_tbl = ftbl_cam_cc_ipe_0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_ipe_1_clk_src",
		.parent_data = cam_cc_parent_data_3,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_qdss_debug_clk_src[] = {
	F(200000000, P_CAM_CC_PLL0_OUT_EVEN, 3, 0, 0),
	F(300000000, P_CAM_CC_PLL0_OUT_EVEN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_qdss_debug_clk_src = {
	.cmd_rcgr = 0x13330,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_qdss_debug_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_qdss_debug_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_qup_ahbm_clk_src[] = {
	F(150000000, P_CAM_CC_PLL0_OUT_EVEN, 4, 0, 0),
	F(240000000, P_CAM_CC_PLL0_OUT_MAIN, 5, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_qup_ahbm_clk_src = {
	.cmd_rcgr = 0x132e8,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_qup_ahbm_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_qup_ahbm_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_qup_core_2x_clk_src[] = {
	F(200000000, P_CAM_CC_PLL0_OUT_ODD, 2, 0, 0),
	F(300000000, P_CAM_CC_PLL0_OUT_EVEN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_qup_core_2x_clk_src = {
	.cmd_rcgr = 0x132a0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_qup_core_2x_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_qup_core_2x_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_qup_se_clk_src = {
	.cmd_rcgr = 0x13308,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_7,
	.freq_tbl = ftbl_cam_cc_cci_0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_qup_se_clk_src",
		.parent_data = cam_cc_parent_data_7,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_7),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_sleep_clk_src[] = {
	F(32000, P_SLEEP_CLK, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_sleep_clk_src = {
	.cmd_rcgr = 0x13384,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_8,
	.freq_tbl = ftbl_cam_cc_sleep_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_sleep_clk_src",
		.parent_data = cam_cc_parent_data_8,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_8),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_slow_ahb_clk_src[] = {
	F(80000000, P_CAM_CC_PLL0_OUT_EVEN, 7.5, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_slow_ahb_clk_src = {
	.cmd_rcgr = 0x131f8,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_slow_ahb_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_slow_ahb_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_xo_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_xo_clk_src = {
	.cmd_rcgr = 0x13368,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_9,
	.freq_tbl = ftbl_cam_cc_xo_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_xo_clk_src",
		.parent_data = cam_cc_parent_data_9,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_9),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_regmap_div cam_cc_qup_core_2x_div_clk_src = {
	.reg = 0x132cc,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_qup_core_2x_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&cam_cc_qup_core_2x_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch cam_cc_camnoc_dcd_xo_clk = {
	.halt_reg = 0x13290,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13290,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_camnoc_dcd_xo_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_xo_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_camnoc_nrt_axi_clk = {
	.halt_reg = 0x13278,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13278,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_camnoc_nrt_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_camnoc_rt_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_camnoc_rt_axi_clk = {
	.halt_reg = 0x13260,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13260,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_camnoc_rt_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_camnoc_rt_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_cci_0_clk = {
	.halt_reg = 0x1312c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1312c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_cci_0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_cci_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_cci_1_clk = {
	.halt_reg = 0x1314c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1314c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_cci_1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_cci_1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_cci_2_clk = {
	.halt_reg = 0x1316c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1316c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_cci_2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_cci_2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_cci_3_clk = {
	.halt_reg = 0x1318c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1318c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_cci_3_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_cci_3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_cci_4_clk = {
	.halt_reg = 0x131ac,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x131ac,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_cci_4_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_cci_4_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ccu_fast_ahb_clk = {
	.halt_reg = 0x1329c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1329c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ccu_fast_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csi0phytimer_clk = {
	.halt_reg = 0x1001c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1001c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_csi0phytimer_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_csi0phytimer_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csi1phytimer_clk = {
	.halt_reg = 0x10044,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10044,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_csi1phytimer_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_csi1phytimer_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csi2phytimer_clk = {
	.halt_reg = 0x10068,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10068,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_csi2phytimer_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_csi2phytimer_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csi3phytimer_clk = {
	.halt_reg = 0x1008c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1008c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_csi3phytimer_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_csi3phytimer_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csi4phytimer_clk = {
	.halt_reg = 0x100b0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x100b0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_csi4phytimer_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_csi4phytimer_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csid_clk = {
	.halt_reg = 0x13230,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13230,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_csid_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_csid_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csid_csiphy_rx_clk = {
	.halt_reg = 0x10024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_csid_csiphy_rx_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csiphy0_clk = {
	.halt_reg = 0x10020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_csiphy0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csiphy1_clk = {
	.halt_reg = 0x10048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10048,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_csiphy1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csiphy2_clk = {
	.halt_reg = 0x1006c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1006c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_csiphy2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csiphy3_clk = {
	.halt_reg = 0x10090,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10090,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_csiphy3_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csiphy4_clk = {
	.halt_reg = 0x100b4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x100b4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_csiphy4_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_icp_0_ahb_clk = {
	.halt_reg = 0x130d4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x130d4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_icp_0_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_slow_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_icp_0_clk = {
	.halt_reg = 0x130c0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x130c0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_icp_0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_icp_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_icp_1_ahb_clk = {
	.halt_reg = 0x1310c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1310c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_icp_1_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_slow_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_icp_1_clk = {
	.halt_reg = 0x130f8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x130f8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_icp_1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_icp_1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_0_main_clk = {
	.halt_reg = 0x12034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x12034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_0_main_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_0_main_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_0_main_fast_ahb_clk = {
	.halt_reg = 0x12054,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x12054,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_0_main_fast_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_0_pcp_clk = {
	.halt_reg = 0x12058,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x12058,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_0_pcp_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_0_main_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_0_pcp_fast_ahb_clk = {
	.halt_reg = 0x12070,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x12070,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_0_pcp_fast_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_0_scalar_clk = {
	.halt_reg = 0x12074,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x12074,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_0_scalar_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_0_main_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_0_scalar_fast_ahb_clk = {
	.halt_reg = 0x1208c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1208c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_0_scalar_fast_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_0_tmc_clk = {
	.halt_reg = 0x12090,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x12090,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_0_tmc_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_0_main_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_0_tmc_fast_ahb_clk = {
	.halt_reg = 0x120a8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x120a8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_0_tmc_fast_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_1_main_clk = {
	.halt_reg = 0x120f8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x120f8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_1_main_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_1_main_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_1_main_fast_ahb_clk = {
	.halt_reg = 0x12118,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x12118,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_1_main_fast_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_1_pcp_clk = {
	.halt_reg = 0x1211c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1211c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_1_pcp_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_1_main_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_1_pcp_fast_ahb_clk = {
	.halt_reg = 0x12134,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x12134,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_1_pcp_fast_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_1_scalar_clk = {
	.halt_reg = 0x12138,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x12138,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_1_scalar_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_1_main_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_1_scalar_fast_ahb_clk = {
	.halt_reg = 0x12150,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x12150,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_1_scalar_fast_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_1_tmc_clk = {
	.halt_reg = 0x12154,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x12154,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_1_tmc_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_1_main_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_1_tmc_fast_ahb_clk = {
	.halt_reg = 0x1216c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1216c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_1_tmc_fast_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_2_main_clk = {
	.halt_reg = 0x121a4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x121a4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_2_main_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_2_main_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_2_main_fast_ahb_clk = {
	.halt_reg = 0x121c4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x121c4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_2_main_fast_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_2_pcp_clk = {
	.halt_reg = 0x121c8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x121c8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_2_pcp_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_2_main_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_2_pcp_fast_ahb_clk = {
	.halt_reg = 0x121e0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x121e0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_2_pcp_fast_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_2_scalar_clk = {
	.halt_reg = 0x121e4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x121e4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_2_scalar_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_2_main_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_2_scalar_fast_ahb_clk = {
	.halt_reg = 0x121fc,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x121fc,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_2_scalar_fast_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_2_tmc_clk = {
	.halt_reg = 0x12200,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x12200,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_2_tmc_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_2_main_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_2_tmc_fast_ahb_clk = {
	.halt_reg = 0x12218,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x12218,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_2_tmc_fast_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_lite_ahb_clk = {
	.halt_reg = 0x13054,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13054,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_lite_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_slow_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_lite_clk = {
	.halt_reg = 0x1301c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1301c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_lite_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_lite_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_lite_cphy_rx_clk = {
	.halt_reg = 0x13050,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13050,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_lite_cphy_rx_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_lite_csid_clk = {
	.halt_reg = 0x1303c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1303c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_lite_csid_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_lite_csid_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ipe_0_ahb_clk = {
	.halt_reg = 0x11054,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x11054,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ipe_0_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_slow_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ipe_0_clk = {
	.halt_reg = 0x11034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x11034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ipe_0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ipe_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ipe_0_fast_ahb_clk = {
	.halt_reg = 0x11058,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x11058,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ipe_0_fast_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ipe_1_ahb_clk = {
	.halt_reg = 0x110b0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x110b0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ipe_1_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_slow_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ipe_1_clk = {
	.halt_reg = 0x11090,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x11090,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ipe_1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ipe_1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ipe_1_fast_ahb_clk = {
	.halt_reg = 0x110b4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x110b4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ipe_1_fast_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_qdss_debug_clk = {
	.halt_reg = 0x13348,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13348,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_qdss_debug_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_qdss_debug_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_qdss_debug_xo_clk = {
	.halt_reg = 0x1334c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1334c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_qdss_debug_xo_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_xo_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_qup_ahbm_clk = {
	.halt_reg = 0x13300,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13300,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_qup_ahbm_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_qup_ahbm_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_qup_core_2x_clk = {
	.halt_reg = 0x132b8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x132b8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_qup_core_2x_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_qup_core_2x_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_qup_core_clk = {
	.halt_reg = 0x132d0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x132d0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_qup_core_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_qup_core_2x_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_qup_se_clk = {
	.halt_reg = 0x13320,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13320,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_qup_se_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_qup_se_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_sfe_lite_0_clk = {
	.halt_reg = 0x1305c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1305c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_sfe_lite_0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_0_main_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_sfe_lite_0_fast_ahb_clk = {
	.halt_reg = 0x1306c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1306c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_sfe_lite_0_fast_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_sfe_lite_1_clk = {
	.halt_reg = 0x13074,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13074,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_sfe_lite_1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_1_main_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_sfe_lite_1_fast_ahb_clk = {
	.halt_reg = 0x13084,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13084,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_sfe_lite_1_fast_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_sfe_lite_2_clk = {
	.halt_reg = 0x1308c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1308c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_sfe_lite_2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_2_main_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_sfe_lite_2_fast_ahb_clk = {
	.halt_reg = 0x1309c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1309c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_sfe_lite_2_fast_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_sm_obs_clk = {
	.halt_reg = 0x1401c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1401c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_sm_obs_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_top_ahb_clk = {
	.halt_reg = 0x131b0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x131b0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_top_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_slow_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_top_fast_ahb_clk = {
	.halt_reg = 0x131c4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x131c4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_top_fast_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_top_ife_0_clk = {
	.halt_reg = 0x12048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x12048,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_top_ife_0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_0_main_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_top_ife_1_clk = {
	.halt_reg = 0x1210c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1210c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_top_ife_1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_1_main_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_top_ife_2_clk = {
	.halt_reg = 0x121b8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x121b8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_top_ife_2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_2_main_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_top_ife_lite_clk = {
	.halt_reg = 0x13020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_top_ife_lite_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_lite_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_top_ipe_0_clk = {
	.halt_reg = 0x11048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x11048,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_top_ipe_0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ipe_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_top_ipe_1_clk = {
	.halt_reg = 0x110a4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x110a4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_top_ipe_1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ipe_1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_top_qup_ahbm_clk = {
	.halt_reg = 0x13304,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13304,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_top_qup_ahbm_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_qup_ahbm_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_top_sfe_lite_0_clk = {
	.halt_reg = 0x13060,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13060,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_top_sfe_lite_0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_0_main_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_top_sfe_lite_1_clk = {
	.halt_reg = 0x13078,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13078,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_top_sfe_lite_1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_1_main_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_top_sfe_lite_2_clk = {
	.halt_reg = 0x13090,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13090,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_top_sfe_lite_2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_2_main_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_tpg_csiphy_rx_clk = {
	.halt_reg = 0x100b8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x100b8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_tpg_csiphy_rx_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc cam_cc_titan_top_gdsc = {
	.gdscr = 0x13350,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "cam_cc_titan_top_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc cam_cc_ife_0_gdsc = {
	.gdscr = 0x12004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "cam_cc_ife_0_gdsc",
	},
	.parent = &cam_cc_titan_top_gdsc.pd,
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc cam_cc_ife_1_gdsc = {
	.gdscr = 0x120c8,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "cam_cc_ife_1_gdsc",
	},
	.parent = &cam_cc_titan_top_gdsc.pd,
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc cam_cc_ife_2_gdsc = {
	.gdscr = 0x12174,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "cam_cc_ife_2_gdsc",
	},
	.parent = &cam_cc_titan_top_gdsc.pd,
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc cam_cc_ipe_0_gdsc = {
	.gdscr = 0x11004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "cam_cc_ipe_0_gdsc",
	},
	.parent = &cam_cc_titan_top_gdsc.pd,
	.pwrsts = PWRSTS_OFF_ON,
	.flags = HW_CTRL_TRIGGER | POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc cam_cc_ipe_1_gdsc = {
	.gdscr = 0x11060,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "cam_cc_ipe_1_gdsc",
	},
	.parent = &cam_cc_titan_top_gdsc.pd,
	.pwrsts = PWRSTS_OFF_ON,
	.flags = HW_CTRL_TRIGGER | POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct clk_regmap *cam_cc_nord_clocks[] = {
	[CAM_CC_CAMNOC_DCD_XO_CLK] = &cam_cc_camnoc_dcd_xo_clk.clkr,
	[CAM_CC_CAMNOC_NRT_AXI_CLK] = &cam_cc_camnoc_nrt_axi_clk.clkr,
	[CAM_CC_CAMNOC_RT_AXI_CLK] = &cam_cc_camnoc_rt_axi_clk.clkr,
	[CAM_CC_CAMNOC_RT_AXI_CLK_SRC] = &cam_cc_camnoc_rt_axi_clk_src.clkr,
	[CAM_CC_CCI_0_CLK] = &cam_cc_cci_0_clk.clkr,
	[CAM_CC_CCI_0_CLK_SRC] = &cam_cc_cci_0_clk_src.clkr,
	[CAM_CC_CCI_1_CLK] = &cam_cc_cci_1_clk.clkr,
	[CAM_CC_CCI_1_CLK_SRC] = &cam_cc_cci_1_clk_src.clkr,
	[CAM_CC_CCI_2_CLK] = &cam_cc_cci_2_clk.clkr,
	[CAM_CC_CCI_2_CLK_SRC] = &cam_cc_cci_2_clk_src.clkr,
	[CAM_CC_CCI_3_CLK] = &cam_cc_cci_3_clk.clkr,
	[CAM_CC_CCI_3_CLK_SRC] = &cam_cc_cci_3_clk_src.clkr,
	[CAM_CC_CCI_4_CLK] = &cam_cc_cci_4_clk.clkr,
	[CAM_CC_CCI_4_CLK_SRC] = &cam_cc_cci_4_clk_src.clkr,
	[CAM_CC_CCU_FAST_AHB_CLK] = &cam_cc_ccu_fast_ahb_clk.clkr,
	[CAM_CC_CPHY_RX_CLK_SRC] = &cam_cc_cphy_rx_clk_src.clkr,
	[CAM_CC_CSI0PHYTIMER_CLK] = &cam_cc_csi0phytimer_clk.clkr,
	[CAM_CC_CSI0PHYTIMER_CLK_SRC] = &cam_cc_csi0phytimer_clk_src.clkr,
	[CAM_CC_CSI1PHYTIMER_CLK] = &cam_cc_csi1phytimer_clk.clkr,
	[CAM_CC_CSI1PHYTIMER_CLK_SRC] = &cam_cc_csi1phytimer_clk_src.clkr,
	[CAM_CC_CSI2PHYTIMER_CLK] = &cam_cc_csi2phytimer_clk.clkr,
	[CAM_CC_CSI2PHYTIMER_CLK_SRC] = &cam_cc_csi2phytimer_clk_src.clkr,
	[CAM_CC_CSI3PHYTIMER_CLK] = &cam_cc_csi3phytimer_clk.clkr,
	[CAM_CC_CSI3PHYTIMER_CLK_SRC] = &cam_cc_csi3phytimer_clk_src.clkr,
	[CAM_CC_CSI4PHYTIMER_CLK] = &cam_cc_csi4phytimer_clk.clkr,
	[CAM_CC_CSI4PHYTIMER_CLK_SRC] = &cam_cc_csi4phytimer_clk_src.clkr,
	[CAM_CC_CSID_CLK] = &cam_cc_csid_clk.clkr,
	[CAM_CC_CSID_CLK_SRC] = &cam_cc_csid_clk_src.clkr,
	[CAM_CC_CSID_CSIPHY_RX_CLK] = &cam_cc_csid_csiphy_rx_clk.clkr,
	[CAM_CC_CSIPHY0_CLK] = &cam_cc_csiphy0_clk.clkr,
	[CAM_CC_CSIPHY1_CLK] = &cam_cc_csiphy1_clk.clkr,
	[CAM_CC_CSIPHY2_CLK] = &cam_cc_csiphy2_clk.clkr,
	[CAM_CC_CSIPHY3_CLK] = &cam_cc_csiphy3_clk.clkr,
	[CAM_CC_CSIPHY4_CLK] = &cam_cc_csiphy4_clk.clkr,
	[CAM_CC_FAST_AHB_CLK_SRC] = &cam_cc_fast_ahb_clk_src.clkr,
	[CAM_CC_ICP_0_AHB_CLK] = &cam_cc_icp_0_ahb_clk.clkr,
	[CAM_CC_ICP_0_CLK] = &cam_cc_icp_0_clk.clkr,
	[CAM_CC_ICP_0_CLK_SRC] = &cam_cc_icp_0_clk_src.clkr,
	[CAM_CC_ICP_1_AHB_CLK] = &cam_cc_icp_1_ahb_clk.clkr,
	[CAM_CC_ICP_1_CLK] = &cam_cc_icp_1_clk.clkr,
	[CAM_CC_ICP_1_CLK_SRC] = &cam_cc_icp_1_clk_src.clkr,
	[CAM_CC_IFE_0_MAIN_CLK] = &cam_cc_ife_0_main_clk.clkr,
	[CAM_CC_IFE_0_MAIN_CLK_SRC] = &cam_cc_ife_0_main_clk_src.clkr,
	[CAM_CC_IFE_0_MAIN_FAST_AHB_CLK] = &cam_cc_ife_0_main_fast_ahb_clk.clkr,
	[CAM_CC_IFE_0_PCP_CLK] = &cam_cc_ife_0_pcp_clk.clkr,
	[CAM_CC_IFE_0_PCP_FAST_AHB_CLK] = &cam_cc_ife_0_pcp_fast_ahb_clk.clkr,
	[CAM_CC_IFE_0_SCALAR_CLK] = &cam_cc_ife_0_scalar_clk.clkr,
	[CAM_CC_IFE_0_SCALAR_FAST_AHB_CLK] = &cam_cc_ife_0_scalar_fast_ahb_clk.clkr,
	[CAM_CC_IFE_0_TMC_CLK] = &cam_cc_ife_0_tmc_clk.clkr,
	[CAM_CC_IFE_0_TMC_FAST_AHB_CLK] = &cam_cc_ife_0_tmc_fast_ahb_clk.clkr,
	[CAM_CC_IFE_1_MAIN_CLK] = &cam_cc_ife_1_main_clk.clkr,
	[CAM_CC_IFE_1_MAIN_CLK_SRC] = &cam_cc_ife_1_main_clk_src.clkr,
	[CAM_CC_IFE_1_MAIN_FAST_AHB_CLK] = &cam_cc_ife_1_main_fast_ahb_clk.clkr,
	[CAM_CC_IFE_1_PCP_CLK] = &cam_cc_ife_1_pcp_clk.clkr,
	[CAM_CC_IFE_1_PCP_FAST_AHB_CLK] = &cam_cc_ife_1_pcp_fast_ahb_clk.clkr,
	[CAM_CC_IFE_1_SCALAR_CLK] = &cam_cc_ife_1_scalar_clk.clkr,
	[CAM_CC_IFE_1_SCALAR_FAST_AHB_CLK] = &cam_cc_ife_1_scalar_fast_ahb_clk.clkr,
	[CAM_CC_IFE_1_TMC_CLK] = &cam_cc_ife_1_tmc_clk.clkr,
	[CAM_CC_IFE_1_TMC_FAST_AHB_CLK] = &cam_cc_ife_1_tmc_fast_ahb_clk.clkr,
	[CAM_CC_IFE_2_MAIN_CLK] = &cam_cc_ife_2_main_clk.clkr,
	[CAM_CC_IFE_2_MAIN_CLK_SRC] = &cam_cc_ife_2_main_clk_src.clkr,
	[CAM_CC_IFE_2_MAIN_FAST_AHB_CLK] = &cam_cc_ife_2_main_fast_ahb_clk.clkr,
	[CAM_CC_IFE_2_PCP_CLK] = &cam_cc_ife_2_pcp_clk.clkr,
	[CAM_CC_IFE_2_PCP_FAST_AHB_CLK] = &cam_cc_ife_2_pcp_fast_ahb_clk.clkr,
	[CAM_CC_IFE_2_SCALAR_CLK] = &cam_cc_ife_2_scalar_clk.clkr,
	[CAM_CC_IFE_2_SCALAR_FAST_AHB_CLK] = &cam_cc_ife_2_scalar_fast_ahb_clk.clkr,
	[CAM_CC_IFE_2_TMC_CLK] = &cam_cc_ife_2_tmc_clk.clkr,
	[CAM_CC_IFE_2_TMC_FAST_AHB_CLK] = &cam_cc_ife_2_tmc_fast_ahb_clk.clkr,
	[CAM_CC_IFE_LITE_AHB_CLK] = &cam_cc_ife_lite_ahb_clk.clkr,
	[CAM_CC_IFE_LITE_CLK] = &cam_cc_ife_lite_clk.clkr,
	[CAM_CC_IFE_LITE_CLK_SRC] = &cam_cc_ife_lite_clk_src.clkr,
	[CAM_CC_IFE_LITE_CPHY_RX_CLK] = &cam_cc_ife_lite_cphy_rx_clk.clkr,
	[CAM_CC_IFE_LITE_CSID_CLK] = &cam_cc_ife_lite_csid_clk.clkr,
	[CAM_CC_IFE_LITE_CSID_CLK_SRC] = &cam_cc_ife_lite_csid_clk_src.clkr,
	[CAM_CC_IPE_0_AHB_CLK] = &cam_cc_ipe_0_ahb_clk.clkr,
	[CAM_CC_IPE_0_CLK] = &cam_cc_ipe_0_clk.clkr,
	[CAM_CC_IPE_0_CLK_SRC] = &cam_cc_ipe_0_clk_src.clkr,
	[CAM_CC_IPE_0_FAST_AHB_CLK] = &cam_cc_ipe_0_fast_ahb_clk.clkr,
	[CAM_CC_IPE_1_AHB_CLK] = &cam_cc_ipe_1_ahb_clk.clkr,
	[CAM_CC_IPE_1_CLK] = &cam_cc_ipe_1_clk.clkr,
	[CAM_CC_IPE_1_CLK_SRC] = &cam_cc_ipe_1_clk_src.clkr,
	[CAM_CC_IPE_1_FAST_AHB_CLK] = &cam_cc_ipe_1_fast_ahb_clk.clkr,
	[CAM_CC_PLL0] = &cam_cc_pll0.clkr,
	[CAM_CC_PLL0_OUT_EVEN] = &cam_cc_pll0_out_even.clkr,
	[CAM_CC_PLL0_OUT_ODD] = &cam_cc_pll0_out_odd.clkr,
	[CAM_CC_PLL2] = &cam_cc_pll2.clkr,
	[CAM_CC_PLL2_OUT_EVEN] = &cam_cc_pll2_out_even.clkr,
	[CAM_CC_PLL3] = &cam_cc_pll3.clkr,
	[CAM_CC_PLL3_OUT_EVEN] = &cam_cc_pll3_out_even.clkr,
	[CAM_CC_PLL4] = &cam_cc_pll4.clkr,
	[CAM_CC_PLL4_OUT_EVEN] = &cam_cc_pll4_out_even.clkr,
	[CAM_CC_PLL5] = &cam_cc_pll5.clkr,
	[CAM_CC_PLL5_OUT_EVEN] = &cam_cc_pll5_out_even.clkr,
	[CAM_CC_PLL6] = &cam_cc_pll6.clkr,
	[CAM_CC_PLL6_OUT_EVEN] = &cam_cc_pll6_out_even.clkr,
	[CAM_CC_QDSS_DEBUG_CLK] = &cam_cc_qdss_debug_clk.clkr,
	[CAM_CC_QDSS_DEBUG_CLK_SRC] = &cam_cc_qdss_debug_clk_src.clkr,
	[CAM_CC_QDSS_DEBUG_XO_CLK] = &cam_cc_qdss_debug_xo_clk.clkr,
	[CAM_CC_QUP_AHBM_CLK] = &cam_cc_qup_ahbm_clk.clkr,
	[CAM_CC_QUP_AHBM_CLK_SRC] = &cam_cc_qup_ahbm_clk_src.clkr,
	[CAM_CC_QUP_CORE_2X_CLK] = &cam_cc_qup_core_2x_clk.clkr,
	[CAM_CC_QUP_CORE_2X_CLK_SRC] = &cam_cc_qup_core_2x_clk_src.clkr,
	[CAM_CC_QUP_CORE_2X_DIV_CLK_SRC] = &cam_cc_qup_core_2x_div_clk_src.clkr,
	[CAM_CC_QUP_CORE_CLK] = &cam_cc_qup_core_clk.clkr,
	[CAM_CC_QUP_SE_CLK] = &cam_cc_qup_se_clk.clkr,
	[CAM_CC_QUP_SE_CLK_SRC] = &cam_cc_qup_se_clk_src.clkr,
	[CAM_CC_SFE_LITE_0_CLK] = &cam_cc_sfe_lite_0_clk.clkr,
	[CAM_CC_SFE_LITE_0_FAST_AHB_CLK] = &cam_cc_sfe_lite_0_fast_ahb_clk.clkr,
	[CAM_CC_SFE_LITE_1_CLK] = &cam_cc_sfe_lite_1_clk.clkr,
	[CAM_CC_SFE_LITE_1_FAST_AHB_CLK] = &cam_cc_sfe_lite_1_fast_ahb_clk.clkr,
	[CAM_CC_SFE_LITE_2_CLK] = &cam_cc_sfe_lite_2_clk.clkr,
	[CAM_CC_SFE_LITE_2_FAST_AHB_CLK] = &cam_cc_sfe_lite_2_fast_ahb_clk.clkr,
	[CAM_CC_SLEEP_CLK_SRC] = &cam_cc_sleep_clk_src.clkr,
	[CAM_CC_SLOW_AHB_CLK_SRC] = &cam_cc_slow_ahb_clk_src.clkr,
	[CAM_CC_SM_OBS_CLK] = &cam_cc_sm_obs_clk.clkr,
	[CAM_CC_TOP_AHB_CLK] = &cam_cc_top_ahb_clk.clkr,
	[CAM_CC_TOP_FAST_AHB_CLK] = &cam_cc_top_fast_ahb_clk.clkr,
	[CAM_CC_TOP_IFE_0_CLK] = &cam_cc_top_ife_0_clk.clkr,
	[CAM_CC_TOP_IFE_1_CLK] = &cam_cc_top_ife_1_clk.clkr,
	[CAM_CC_TOP_IFE_2_CLK] = &cam_cc_top_ife_2_clk.clkr,
	[CAM_CC_TOP_IFE_LITE_CLK] = &cam_cc_top_ife_lite_clk.clkr,
	[CAM_CC_TOP_IPE_0_CLK] = &cam_cc_top_ipe_0_clk.clkr,
	[CAM_CC_TOP_IPE_1_CLK] = &cam_cc_top_ipe_1_clk.clkr,
	[CAM_CC_TOP_QUP_AHBM_CLK] = &cam_cc_top_qup_ahbm_clk.clkr,
	[CAM_CC_TOP_SFE_LITE_0_CLK] = &cam_cc_top_sfe_lite_0_clk.clkr,
	[CAM_CC_TOP_SFE_LITE_1_CLK] = &cam_cc_top_sfe_lite_1_clk.clkr,
	[CAM_CC_TOP_SFE_LITE_2_CLK] = &cam_cc_top_sfe_lite_2_clk.clkr,
	[CAM_CC_TPG_CSIPHY_RX_CLK] = &cam_cc_tpg_csiphy_rx_clk.clkr,
	[CAM_CC_XO_CLK_SRC] = &cam_cc_xo_clk_src.clkr,
};

static struct gdsc *cam_cc_nord_gdscs[] = {
	[CAM_CC_TITAN_TOP_GDSC] = &cam_cc_titan_top_gdsc,
	[CAM_CC_IFE_0_GDSC] = &cam_cc_ife_0_gdsc,
	[CAM_CC_IFE_1_GDSC] = &cam_cc_ife_1_gdsc,
	[CAM_CC_IFE_2_GDSC] = &cam_cc_ife_2_gdsc,
	[CAM_CC_IPE_0_GDSC] = &cam_cc_ipe_0_gdsc,
	[CAM_CC_IPE_1_GDSC] = &cam_cc_ipe_1_gdsc,
};

static const struct qcom_reset_map cam_cc_nord_resets[] = {
	[CAM_CC_CCU_BCR] = { 0x13298 },
	[CAM_CC_ICP_0_BCR] = { 0x130a0 },
	[CAM_CC_ICP_1_BCR] = { 0x130d8 },
	[CAM_CC_IFE_0_BCR] = { 0x12000 },
	[CAM_CC_IFE_1_BCR] = { 0x120c4 },
	[CAM_CC_IFE_2_BCR] = { 0x12170 },
	[CAM_CC_IPE_0_BCR] = { 0x11000 },
	[CAM_CC_IPE_1_BCR] = { 0x1105c },
	[CAM_CC_QDSS_DEBUG_BCR] = { 0x1332c },
	[CAM_CC_SFE_LITE_0_BCR] = { 0x13058 },
	[CAM_CC_SFE_LITE_1_BCR] = { 0x13070 },
	[CAM_CC_SFE_LITE_2_BCR] = { 0x13088 },
};

static struct clk_alpha_pll *cam_cc_nord_plls[] = {
	&cam_cc_pll0,
	&cam_cc_pll2,
	&cam_cc_pll3,
	&cam_cc_pll4,
	&cam_cc_pll5,
	&cam_cc_pll6,
};

static const u32 cam_cc_nord_critical_cbcrs[] = {
	0x13294, /* CAM_CC_CAMNOC_XO_CLK */
	0x13364, /* CAM_CC_CORE_AHB_CLK */
	0x13380, /* CAM_CC_GDSC_CLK */
	0x132e4, /* CAM_CC_QUP_SLEEP_CLK */
	0x1339c, /* CAM_CC_SLEEP_CLK */
};

static const struct regmap_config cam_cc_nord_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x17000,
	.fast_io = true,
};

static const struct qcom_cc_driver_data cam_cc_nord_driver_data = {
	.alpha_plls = cam_cc_nord_plls,
	.num_alpha_plls = ARRAY_SIZE(cam_cc_nord_plls),
	.clk_cbcrs = cam_cc_nord_critical_cbcrs,
	.num_clk_cbcrs = ARRAY_SIZE(cam_cc_nord_critical_cbcrs),
};

static const struct qcom_cc_desc cam_cc_nord_desc = {
	.config = &cam_cc_nord_regmap_config,
	.clks = cam_cc_nord_clocks,
	.num_clks = ARRAY_SIZE(cam_cc_nord_clocks),
	.resets = cam_cc_nord_resets,
	.num_resets = ARRAY_SIZE(cam_cc_nord_resets),
	.gdscs = cam_cc_nord_gdscs,
	.num_gdscs = ARRAY_SIZE(cam_cc_nord_gdscs),
	.use_rpm = true,
	.driver_data = &cam_cc_nord_driver_data,
};

static const struct of_device_id cam_cc_nord_match_table[] = {
	{ .compatible = "qcom,nord-camcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, cam_cc_nord_match_table);

static int cam_cc_nord_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &cam_cc_nord_desc);
}

static struct platform_driver cam_cc_nord_driver = {
	.probe = cam_cc_nord_probe,
	.driver = {
		.name = "camcc-nord",
		.of_match_table = cam_cc_nord_match_table,
	},
};

module_platform_driver(cam_cc_nord_driver);

MODULE_DESCRIPTION("QTI CAMCC Nord Driver");
MODULE_LICENSE("GPL");
