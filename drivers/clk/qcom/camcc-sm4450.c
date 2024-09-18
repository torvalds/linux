// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,sm4450-camcc.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "common.h"
#include "gdsc.h"
#include "reset.h"

enum {
	DT_BI_TCXO,
};

enum {
	P_BI_TCXO,
	P_CAM_CC_PLL0_OUT_EVEN,
	P_CAM_CC_PLL0_OUT_MAIN,
	P_CAM_CC_PLL0_OUT_ODD,
	P_CAM_CC_PLL1_OUT_EVEN,
	P_CAM_CC_PLL1_OUT_MAIN,
	P_CAM_CC_PLL2_OUT_EVEN,
	P_CAM_CC_PLL2_OUT_MAIN,
	P_CAM_CC_PLL3_OUT_EVEN,
	P_CAM_CC_PLL4_OUT_EVEN,
	P_CAM_CC_PLL4_OUT_MAIN,
};

static const struct pll_vco lucid_evo_vco[] = {
	{ 249600000, 2020000000, 0 },
};

static const struct pll_vco rivian_evo_vco[] = {
	{ 864000000, 1056000000, 0 },
};

/* 1200.0 MHz Configuration */
static const struct alpha_pll_config cam_cc_pll0_config = {
	.l = 0x3e,
	.alpha = 0x8000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x32aa299c,
	.user_ctl_val = 0x00008400,
	.user_ctl_hi_val = 0x00000805,
};

static struct clk_alpha_pll cam_cc_pll0 = {
	.offset = 0x0,
	.vco_table = lucid_evo_vco,
	.num_vco = ARRAY_SIZE(lucid_evo_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_pll0",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
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
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
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
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
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

/* 600.0 MHz Configuration */
static const struct alpha_pll_config cam_cc_pll1_config = {
	.l = 0x1f,
	.alpha = 0x4000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x32aa299c,
	.user_ctl_val = 0x00000400,
	.user_ctl_hi_val = 0x00000805,
};

static struct clk_alpha_pll cam_cc_pll1 = {
	.offset = 0x1000,
	.vco_table = lucid_evo_vco,
	.num_vco = ARRAY_SIZE(lucid_evo_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_pll1",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_evo_ops,
		},
	},
};

static const struct clk_div_table post_div_table_cam_cc_pll1_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv cam_cc_pll1_out_even = {
	.offset = 0x1000,
	.post_div_shift = 10,
	.post_div_table = post_div_table_cam_cc_pll1_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_cam_cc_pll1_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_pll1_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&cam_cc_pll1.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_lucid_evo_ops,
	},
};

/* 960.0 MHz Configuration */
static const struct alpha_pll_config cam_cc_pll2_config = {
	.l = 0x32,
	.alpha = 0x0,
	.config_ctl_val = 0x90008820,
	.config_ctl_hi_val = 0x00890263,
	.config_ctl_hi1_val = 0x00000247,
	.user_ctl_val = 0x00000400,
	.user_ctl_hi_val = 0x00400000,
};

static struct clk_alpha_pll cam_cc_pll2 = {
	.offset = 0x2000,
	.vco_table = rivian_evo_vco,
	.num_vco = ARRAY_SIZE(rivian_evo_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_RIVIAN_EVO],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_pll2",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_rivian_evo_ops,
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
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_RIVIAN_EVO],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_pll2_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&cam_cc_pll2.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_rivian_evo_ops,
	},
};

/* 600.0 MHz Configuration */
static const struct alpha_pll_config cam_cc_pll3_config = {
	.l = 0x1f,
	.alpha = 0x4000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x32aa299c,
	.user_ctl_val = 0x00000400,
	.user_ctl_hi_val = 0x00000805,
};

static struct clk_alpha_pll cam_cc_pll3 = {
	.offset = 0x3000,
	.vco_table = lucid_evo_vco,
	.num_vco = ARRAY_SIZE(lucid_evo_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
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
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
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

/* 700.0 MHz Configuration */
static const struct alpha_pll_config cam_cc_pll4_config = {
	.l = 0x24,
	.alpha = 0x7555,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x32aa299c,
	.user_ctl_val = 0x00000400,
	.user_ctl_hi_val = 0x00000805,
};

static struct clk_alpha_pll cam_cc_pll4 = {
	.offset = 0x4000,
	.vco_table = lucid_evo_vco,
	.num_vco = ARRAY_SIZE(lucid_evo_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
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
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
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

static const struct parent_map cam_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL0_OUT_MAIN, 1 },
	{ P_CAM_CC_PLL0_OUT_ODD, 5 },
	{ P_CAM_CC_PLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data cam_cc_parent_data_0[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &cam_cc_pll0.clkr.hw },
	{ .hw = &cam_cc_pll0_out_odd.clkr.hw },
	{ .hw = &cam_cc_pll0_out_even.clkr.hw },
};

static const struct parent_map cam_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL2_OUT_EVEN, 3 },
	{ P_CAM_CC_PLL2_OUT_MAIN, 4 },
};

static const struct clk_parent_data cam_cc_parent_data_1[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &cam_cc_pll2_out_even.clkr.hw },
	{ .hw = &cam_cc_pll2.clkr.hw },
};

static const struct parent_map cam_cc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL0_OUT_ODD, 5 },
	{ P_CAM_CC_PLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data cam_cc_parent_data_2[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &cam_cc_pll0_out_odd.clkr.hw },
	{ .hw = &cam_cc_pll0_out_even.clkr.hw },
};

static const struct parent_map cam_cc_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL0_OUT_MAIN, 1 },
	{ P_CAM_CC_PLL4_OUT_EVEN, 2 },
	{ P_CAM_CC_PLL4_OUT_MAIN, 3 },
	{ P_CAM_CC_PLL0_OUT_ODD, 5 },
	{ P_CAM_CC_PLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data cam_cc_parent_data_3[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &cam_cc_pll0.clkr.hw },
	{ .hw = &cam_cc_pll4_out_even.clkr.hw },
	{ .hw = &cam_cc_pll4.clkr.hw },
	{ .hw = &cam_cc_pll0_out_odd.clkr.hw },
	{ .hw = &cam_cc_pll0_out_even.clkr.hw },
};

static const struct parent_map cam_cc_parent_map_4[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL0_OUT_MAIN, 1 },
	{ P_CAM_CC_PLL1_OUT_MAIN, 2 },
	{ P_CAM_CC_PLL1_OUT_EVEN, 3 },
	{ P_CAM_CC_PLL0_OUT_ODD, 5 },
	{ P_CAM_CC_PLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data cam_cc_parent_data_4[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &cam_cc_pll0.clkr.hw },
	{ .hw = &cam_cc_pll1.clkr.hw },
	{ .hw = &cam_cc_pll1_out_even.clkr.hw },
	{ .hw = &cam_cc_pll0_out_odd.clkr.hw },
	{ .hw = &cam_cc_pll0_out_even.clkr.hw },
};

static const struct parent_map cam_cc_parent_map_5[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL1_OUT_MAIN, 2 },
	{ P_CAM_CC_PLL1_OUT_EVEN, 3 },
};

static const struct clk_parent_data cam_cc_parent_data_5[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &cam_cc_pll1.clkr.hw },
	{ .hw = &cam_cc_pll1_out_even.clkr.hw },
};

static const struct parent_map cam_cc_parent_map_6[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL0_OUT_MAIN, 1 },
	{ P_CAM_CC_PLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data cam_cc_parent_data_6[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &cam_cc_pll0.clkr.hw },
	{ .hw = &cam_cc_pll0_out_even.clkr.hw },
};

static const struct parent_map cam_cc_parent_map_7[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL0_OUT_MAIN, 1 },
	{ P_CAM_CC_PLL3_OUT_EVEN, 5 },
	{ P_CAM_CC_PLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data cam_cc_parent_data_7[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &cam_cc_pll0.clkr.hw },
	{ .hw = &cam_cc_pll3_out_even.clkr.hw },
	{ .hw = &cam_cc_pll0_out_even.clkr.hw },
};

static const struct freq_tbl ftbl_cam_cc_bps_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(300000000, P_CAM_CC_PLL1_OUT_EVEN, 1, 0, 0),
	F(410000000, P_CAM_CC_PLL1_OUT_EVEN, 1, 0, 0),
	F(460000000, P_CAM_CC_PLL1_OUT_EVEN, 1, 0, 0),
	F(600000000, P_CAM_CC_PLL1_OUT_EVEN, 1, 0, 0),
	F(700000000, P_CAM_CC_PLL1_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_bps_clk_src = {
	.cmd_rcgr = 0xa004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_4,
	.freq_tbl = ftbl_cam_cc_bps_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_bps_clk_src",
		.parent_data = cam_cc_parent_data_4,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_4),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_camnoc_axi_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(150000000, P_CAM_CC_PLL0_OUT_EVEN, 4, 0, 0),
	F(240000000, P_CAM_CC_PLL0_OUT_EVEN, 2.5, 0, 0),
	F(300000000, P_CAM_CC_PLL0_OUT_EVEN, 2, 0, 0),
	F(400000000, P_CAM_CC_PLL0_OUT_ODD, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_camnoc_axi_clk_src = {
	.cmd_rcgr = 0x13014,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_camnoc_axi_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_camnoc_axi_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_cci_0_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(37500000, P_CAM_CC_PLL0_OUT_EVEN, 16, 0, 0),
	F(50000000, P_CAM_CC_PLL0_OUT_EVEN, 12, 0, 0),
	F(100000000, P_CAM_CC_PLL0_OUT_EVEN, 6, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_cci_0_clk_src = {
	.cmd_rcgr = 0x10004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_2,
	.freq_tbl = ftbl_cam_cc_cci_0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_cci_0_clk_src",
		.parent_data = cam_cc_parent_data_2,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_cci_1_clk_src = {
	.cmd_rcgr = 0x11004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_2,
	.freq_tbl = ftbl_cam_cc_cci_0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_cci_1_clk_src",
		.parent_data = cam_cc_parent_data_2,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_cphy_rx_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(300000000, P_CAM_CC_PLL0_OUT_EVEN, 2, 0, 0),
	F(400000000, P_CAM_CC_PLL0_OUT_EVEN, 1.5, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_cphy_rx_clk_src = {
	.cmd_rcgr = 0xc054,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_cphy_rx_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_cphy_rx_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_cre_clk_src = {
	.cmd_rcgr = 0x16004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_5,
	.freq_tbl = ftbl_cam_cc_bps_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_cre_clk_src",
		.parent_data = cam_cc_parent_data_5,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_5),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_csi0phytimer_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(300000000, P_CAM_CC_PLL0_OUT_EVEN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_csi0phytimer_clk_src = {
	.cmd_rcgr = 0x9004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_csi0phytimer_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_csi0phytimer_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_csi1phytimer_clk_src = {
	.cmd_rcgr = 0x9028,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_csi0phytimer_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_csi1phytimer_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_csi2phytimer_clk_src = {
	.cmd_rcgr = 0x904c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_csi0phytimer_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_csi2phytimer_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_fast_ahb_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(100000000, P_CAM_CC_PLL0_OUT_EVEN, 6, 0, 0),
	F(150000000, P_CAM_CC_PLL0_OUT_EVEN, 4, 0, 0),
	F(200000000, P_CAM_CC_PLL0_OUT_MAIN, 6, 0, 0),
	F(240000000, P_CAM_CC_PLL0_OUT_MAIN, 5, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_fast_ahb_clk_src = {
	.cmd_rcgr = 0xa02c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_fast_ahb_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_fast_ahb_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_icp_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(400000000, P_CAM_CC_PLL0_OUT_MAIN, 3, 0, 0),
	F(480000000, P_CAM_CC_PLL0_OUT_MAIN, 2.5, 0, 0),
	F(600000000, P_CAM_CC_PLL0_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_icp_clk_src = {
	.cmd_rcgr = 0xf014,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_6,
	.freq_tbl = ftbl_cam_cc_icp_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_icp_clk_src",
		.parent_data = cam_cc_parent_data_6,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_6),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_mclk0_clk_src[] = {
	F(19200000, P_CAM_CC_PLL2_OUT_MAIN, 1, 1, 50),
	F(24000000, P_CAM_CC_PLL2_OUT_MAIN, 10, 1, 4),
	F(64000000, P_CAM_CC_PLL2_OUT_MAIN, 15, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_mclk0_clk_src = {
	.cmd_rcgr = 0x8004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_1,
	.freq_tbl = ftbl_cam_cc_mclk0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_mclk0_clk_src",
		.parent_data = cam_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_mclk1_clk_src = {
	.cmd_rcgr = 0x8024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_1,
	.freq_tbl = ftbl_cam_cc_mclk0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_mclk1_clk_src",
		.parent_data = cam_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_mclk2_clk_src = {
	.cmd_rcgr = 0x8044,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_1,
	.freq_tbl = ftbl_cam_cc_mclk0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_mclk2_clk_src",
		.parent_data = cam_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_mclk3_clk_src = {
	.cmd_rcgr = 0x8064,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_1,
	.freq_tbl = ftbl_cam_cc_mclk0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_mclk3_clk_src",
		.parent_data = cam_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_ope_0_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(300000000, P_CAM_CC_PLL3_OUT_EVEN, 1, 0, 0),
	F(410000000, P_CAM_CC_PLL3_OUT_EVEN, 1, 0, 0),
	F(460000000, P_CAM_CC_PLL3_OUT_EVEN, 1, 0, 0),
	F(600000000, P_CAM_CC_PLL3_OUT_EVEN, 1, 0, 0),
	F(700000000, P_CAM_CC_PLL3_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_ope_0_clk_src = {
	.cmd_rcgr = 0xb004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_7,
	.freq_tbl = ftbl_cam_cc_ope_0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_ope_0_clk_src",
		.parent_data = cam_cc_parent_data_7,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_7),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_slow_ahb_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(80000000, P_CAM_CC_PLL0_OUT_EVEN, 7.5, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_slow_ahb_clk_src = {
	.cmd_rcgr = 0xa048,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_slow_ahb_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_slow_ahb_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_tfe_0_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(350000000, P_CAM_CC_PLL4_OUT_EVEN, 1, 0, 0),
	F(432000000, P_CAM_CC_PLL4_OUT_EVEN, 1, 0, 0),
	F(548000000, P_CAM_CC_PLL4_OUT_EVEN, 1, 0, 0),
	F(630000000, P_CAM_CC_PLL4_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_tfe_0_clk_src = {
	.cmd_rcgr = 0xc004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_3,
	.freq_tbl = ftbl_cam_cc_tfe_0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_tfe_0_clk_src",
		.parent_data = cam_cc_parent_data_3,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_tfe_0_csid_clk_src = {
	.cmd_rcgr = 0xc02c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_cphy_rx_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_tfe_0_csid_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_tfe_1_clk_src = {
	.cmd_rcgr = 0xd004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_3,
	.freq_tbl = ftbl_cam_cc_tfe_0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_tfe_1_clk_src",
		.parent_data = cam_cc_parent_data_3,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_tfe_1_csid_clk_src = {
	.cmd_rcgr = 0xd024,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_cphy_rx_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_tfe_1_csid_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_branch cam_cc_bps_ahb_clk = {
	.halt_reg = 0xa060,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa060,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_bps_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_slow_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_bps_areg_clk = {
	.halt_reg = 0xa044,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa044,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_bps_areg_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_bps_clk = {
	.halt_reg = 0xa01c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_bps_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_bps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_camnoc_atb_clk = {
	.halt_reg = 0x13034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_camnoc_atb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_camnoc_axi_clk = {
	.halt_reg = 0x1302c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1302c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_camnoc_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_camnoc_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_camnoc_axi_hf_clk = {
	.halt_reg = 0x1300c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1300c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_camnoc_axi_hf_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_camnoc_axi_sf_clk = {
	.halt_reg = 0x13004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_camnoc_axi_sf_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_cci_0_clk = {
	.halt_reg = 0x1001c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1001c,
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
	.halt_reg = 0x1101c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1101c,
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

static struct clk_branch cam_cc_core_ahb_clk = {
	.halt_reg = 0x1401c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x1401c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_core_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_slow_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_cpas_ahb_clk = {
	.halt_reg = 0x12004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x12004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_cpas_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_slow_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_cre_ahb_clk = {
	.halt_reg = 0x16020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x16020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_cre_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_slow_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_cre_clk = {
	.halt_reg = 0x1601c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1601c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_cre_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_cre_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csi0phytimer_clk = {
	.halt_reg = 0x901c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x901c,
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
	.halt_reg = 0x9040,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9040,
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
	.halt_reg = 0x9064,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9064,
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

static struct clk_branch cam_cc_csiphy0_clk = {
	.halt_reg = 0x9020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9020,
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
	.halt_reg = 0x9044,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9044,
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
	.halt_reg = 0x9068,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9068,
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

static struct clk_branch cam_cc_icp_atb_clk = {
	.halt_reg = 0xf004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_icp_atb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_icp_clk = {
	.halt_reg = 0xf02c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf02c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_icp_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_icp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_icp_cti_clk = {
	.halt_reg = 0xf008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_icp_cti_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_icp_ts_clk = {
	.halt_reg = 0xf00c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf00c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_icp_ts_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_mclk0_clk = {
	.halt_reg = 0x801c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x801c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_mclk0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_mclk0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_mclk1_clk = {
	.halt_reg = 0x803c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x803c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_mclk1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_mclk1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_mclk2_clk = {
	.halt_reg = 0x805c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x805c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_mclk2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_mclk2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_mclk3_clk = {
	.halt_reg = 0x807c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x807c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_mclk3_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_mclk3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ope_0_ahb_clk = {
	.halt_reg = 0xb030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ope_0_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_slow_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ope_0_areg_clk = {
	.halt_reg = 0xb02c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb02c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ope_0_areg_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ope_0_clk = {
	.halt_reg = 0xb01c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ope_0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ope_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_soc_ahb_clk = {
	.halt_reg = 0x14018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x14018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_soc_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_sys_tmr_clk = {
	.halt_reg = 0xf034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_sys_tmr_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_tfe_0_ahb_clk = {
	.halt_reg = 0xc070,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc070,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_tfe_0_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_slow_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_tfe_0_clk = {
	.halt_reg = 0xc01c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_tfe_0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_tfe_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_tfe_0_cphy_rx_clk = {
	.halt_reg = 0xc06c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc06c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_tfe_0_cphy_rx_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_tfe_0_csid_clk = {
	.halt_reg = 0xc044,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc044,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_tfe_0_csid_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_tfe_0_csid_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_tfe_1_ahb_clk = {
	.halt_reg = 0xd048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xd048,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_tfe_1_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_slow_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_tfe_1_clk = {
	.halt_reg = 0xd01c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xd01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_tfe_1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_tfe_1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_tfe_1_cphy_rx_clk = {
	.halt_reg = 0xd044,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xd044,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_tfe_1_cphy_rx_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_tfe_1_csid_clk = {
	.halt_reg = 0xd03c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xd03c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_tfe_1_csid_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_tfe_1_csid_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc cam_cc_camss_top_gdsc = {
	.gdscr = 0x14004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "cam_cc_camss_top_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct clk_regmap *cam_cc_sm4450_clocks[] = {
	[CAM_CC_BPS_AHB_CLK] = &cam_cc_bps_ahb_clk.clkr,
	[CAM_CC_BPS_AREG_CLK] = &cam_cc_bps_areg_clk.clkr,
	[CAM_CC_BPS_CLK] = &cam_cc_bps_clk.clkr,
	[CAM_CC_BPS_CLK_SRC] = &cam_cc_bps_clk_src.clkr,
	[CAM_CC_CAMNOC_ATB_CLK] = &cam_cc_camnoc_atb_clk.clkr,
	[CAM_CC_CAMNOC_AXI_CLK] = &cam_cc_camnoc_axi_clk.clkr,
	[CAM_CC_CAMNOC_AXI_CLK_SRC] = &cam_cc_camnoc_axi_clk_src.clkr,
	[CAM_CC_CAMNOC_AXI_HF_CLK] = &cam_cc_camnoc_axi_hf_clk.clkr,
	[CAM_CC_CAMNOC_AXI_SF_CLK] = &cam_cc_camnoc_axi_sf_clk.clkr,
	[CAM_CC_CCI_0_CLK] = &cam_cc_cci_0_clk.clkr,
	[CAM_CC_CCI_0_CLK_SRC] = &cam_cc_cci_0_clk_src.clkr,
	[CAM_CC_CCI_1_CLK] = &cam_cc_cci_1_clk.clkr,
	[CAM_CC_CCI_1_CLK_SRC] = &cam_cc_cci_1_clk_src.clkr,
	[CAM_CC_CORE_AHB_CLK] = &cam_cc_core_ahb_clk.clkr,
	[CAM_CC_CPAS_AHB_CLK] = &cam_cc_cpas_ahb_clk.clkr,
	[CAM_CC_CPHY_RX_CLK_SRC] = &cam_cc_cphy_rx_clk_src.clkr,
	[CAM_CC_CRE_AHB_CLK] = &cam_cc_cre_ahb_clk.clkr,
	[CAM_CC_CRE_CLK] = &cam_cc_cre_clk.clkr,
	[CAM_CC_CRE_CLK_SRC] = &cam_cc_cre_clk_src.clkr,
	[CAM_CC_CSI0PHYTIMER_CLK] = &cam_cc_csi0phytimer_clk.clkr,
	[CAM_CC_CSI0PHYTIMER_CLK_SRC] = &cam_cc_csi0phytimer_clk_src.clkr,
	[CAM_CC_CSI1PHYTIMER_CLK] = &cam_cc_csi1phytimer_clk.clkr,
	[CAM_CC_CSI1PHYTIMER_CLK_SRC] = &cam_cc_csi1phytimer_clk_src.clkr,
	[CAM_CC_CSI2PHYTIMER_CLK] = &cam_cc_csi2phytimer_clk.clkr,
	[CAM_CC_CSI2PHYTIMER_CLK_SRC] = &cam_cc_csi2phytimer_clk_src.clkr,
	[CAM_CC_CSIPHY0_CLK] = &cam_cc_csiphy0_clk.clkr,
	[CAM_CC_CSIPHY1_CLK] = &cam_cc_csiphy1_clk.clkr,
	[CAM_CC_CSIPHY2_CLK] = &cam_cc_csiphy2_clk.clkr,
	[CAM_CC_FAST_AHB_CLK_SRC] = &cam_cc_fast_ahb_clk_src.clkr,
	[CAM_CC_ICP_ATB_CLK] = &cam_cc_icp_atb_clk.clkr,
	[CAM_CC_ICP_CLK] = &cam_cc_icp_clk.clkr,
	[CAM_CC_ICP_CLK_SRC] = &cam_cc_icp_clk_src.clkr,
	[CAM_CC_ICP_CTI_CLK] = &cam_cc_icp_cti_clk.clkr,
	[CAM_CC_ICP_TS_CLK] = &cam_cc_icp_ts_clk.clkr,
	[CAM_CC_MCLK0_CLK] = &cam_cc_mclk0_clk.clkr,
	[CAM_CC_MCLK0_CLK_SRC] = &cam_cc_mclk0_clk_src.clkr,
	[CAM_CC_MCLK1_CLK] = &cam_cc_mclk1_clk.clkr,
	[CAM_CC_MCLK1_CLK_SRC] = &cam_cc_mclk1_clk_src.clkr,
	[CAM_CC_MCLK2_CLK] = &cam_cc_mclk2_clk.clkr,
	[CAM_CC_MCLK2_CLK_SRC] = &cam_cc_mclk2_clk_src.clkr,
	[CAM_CC_MCLK3_CLK] = &cam_cc_mclk3_clk.clkr,
	[CAM_CC_MCLK3_CLK_SRC] = &cam_cc_mclk3_clk_src.clkr,
	[CAM_CC_OPE_0_AHB_CLK] = &cam_cc_ope_0_ahb_clk.clkr,
	[CAM_CC_OPE_0_AREG_CLK] = &cam_cc_ope_0_areg_clk.clkr,
	[CAM_CC_OPE_0_CLK] = &cam_cc_ope_0_clk.clkr,
	[CAM_CC_OPE_0_CLK_SRC] = &cam_cc_ope_0_clk_src.clkr,
	[CAM_CC_PLL0] = &cam_cc_pll0.clkr,
	[CAM_CC_PLL0_OUT_EVEN] = &cam_cc_pll0_out_even.clkr,
	[CAM_CC_PLL0_OUT_ODD] = &cam_cc_pll0_out_odd.clkr,
	[CAM_CC_PLL1] = &cam_cc_pll1.clkr,
	[CAM_CC_PLL1_OUT_EVEN] = &cam_cc_pll1_out_even.clkr,
	[CAM_CC_PLL2] = &cam_cc_pll2.clkr,
	[CAM_CC_PLL2_OUT_EVEN] = &cam_cc_pll2_out_even.clkr,
	[CAM_CC_PLL3] = &cam_cc_pll3.clkr,
	[CAM_CC_PLL3_OUT_EVEN] = &cam_cc_pll3_out_even.clkr,
	[CAM_CC_PLL4] = &cam_cc_pll4.clkr,
	[CAM_CC_PLL4_OUT_EVEN] = &cam_cc_pll4_out_even.clkr,
	[CAM_CC_SLOW_AHB_CLK_SRC] = &cam_cc_slow_ahb_clk_src.clkr,
	[CAM_CC_SOC_AHB_CLK] = &cam_cc_soc_ahb_clk.clkr,
	[CAM_CC_SYS_TMR_CLK] = &cam_cc_sys_tmr_clk.clkr,
	[CAM_CC_TFE_0_AHB_CLK] = &cam_cc_tfe_0_ahb_clk.clkr,
	[CAM_CC_TFE_0_CLK] = &cam_cc_tfe_0_clk.clkr,
	[CAM_CC_TFE_0_CLK_SRC] = &cam_cc_tfe_0_clk_src.clkr,
	[CAM_CC_TFE_0_CPHY_RX_CLK] = &cam_cc_tfe_0_cphy_rx_clk.clkr,
	[CAM_CC_TFE_0_CSID_CLK] = &cam_cc_tfe_0_csid_clk.clkr,
	[CAM_CC_TFE_0_CSID_CLK_SRC] = &cam_cc_tfe_0_csid_clk_src.clkr,
	[CAM_CC_TFE_1_AHB_CLK] = &cam_cc_tfe_1_ahb_clk.clkr,
	[CAM_CC_TFE_1_CLK] = &cam_cc_tfe_1_clk.clkr,
	[CAM_CC_TFE_1_CLK_SRC] = &cam_cc_tfe_1_clk_src.clkr,
	[CAM_CC_TFE_1_CPHY_RX_CLK] = &cam_cc_tfe_1_cphy_rx_clk.clkr,
	[CAM_CC_TFE_1_CSID_CLK] = &cam_cc_tfe_1_csid_clk.clkr,
	[CAM_CC_TFE_1_CSID_CLK_SRC] = &cam_cc_tfe_1_csid_clk_src.clkr,
};

static struct gdsc *cam_cc_sm4450_gdscs[] = {
	[CAM_CC_CAMSS_TOP_GDSC] = &cam_cc_camss_top_gdsc,
};

static const struct qcom_reset_map cam_cc_sm4450_resets[] = {
	[CAM_CC_BPS_BCR] = { 0xa000 },
	[CAM_CC_CAMNOC_BCR] = { 0x13000 },
	[CAM_CC_CAMSS_TOP_BCR] = { 0x14000 },
	[CAM_CC_CCI_0_BCR] = { 0x10000 },
	[CAM_CC_CCI_1_BCR] = { 0x11000 },
	[CAM_CC_CPAS_BCR] = { 0x12000 },
	[CAM_CC_CRE_BCR] = { 0x16000 },
	[CAM_CC_CSI0PHY_BCR] = { 0x9000 },
	[CAM_CC_CSI1PHY_BCR] = { 0x9024 },
	[CAM_CC_CSI2PHY_BCR] = { 0x9048 },
	[CAM_CC_ICP_BCR] = { 0xf000 },
	[CAM_CC_MCLK0_BCR] = { 0x8000 },
	[CAM_CC_MCLK1_BCR] = { 0x8020 },
	[CAM_CC_MCLK2_BCR] = { 0x8040 },
	[CAM_CC_MCLK3_BCR] = { 0x8060 },
	[CAM_CC_OPE_0_BCR] = { 0xb000 },
	[CAM_CC_TFE_0_BCR] = { 0xc000 },
	[CAM_CC_TFE_1_BCR] = { 0xd000 },
};

static const struct regmap_config cam_cc_sm4450_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x16024,
	.fast_io = true,
};

static struct qcom_cc_desc cam_cc_sm4450_desc = {
	.config = &cam_cc_sm4450_regmap_config,
	.clks = cam_cc_sm4450_clocks,
	.num_clks = ARRAY_SIZE(cam_cc_sm4450_clocks),
	.resets = cam_cc_sm4450_resets,
	.num_resets = ARRAY_SIZE(cam_cc_sm4450_resets),
	.gdscs = cam_cc_sm4450_gdscs,
	.num_gdscs = ARRAY_SIZE(cam_cc_sm4450_gdscs),
};

static const struct of_device_id cam_cc_sm4450_match_table[] = {
	{ .compatible = "qcom,sm4450-camcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, cam_cc_sm4450_match_table);

static int cam_cc_sm4450_probe(struct platform_device *pdev)
{
	struct regmap *regmap;

	regmap = qcom_cc_map(pdev, &cam_cc_sm4450_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	clk_lucid_evo_pll_configure(&cam_cc_pll0, regmap, &cam_cc_pll0_config);
	clk_lucid_evo_pll_configure(&cam_cc_pll1, regmap, &cam_cc_pll1_config);
	clk_rivian_evo_pll_configure(&cam_cc_pll2, regmap, &cam_cc_pll2_config);
	clk_lucid_evo_pll_configure(&cam_cc_pll3, regmap, &cam_cc_pll3_config);
	clk_lucid_evo_pll_configure(&cam_cc_pll4, regmap, &cam_cc_pll4_config);

	return qcom_cc_really_probe(&pdev->dev, &cam_cc_sm4450_desc, regmap);
}

static struct platform_driver cam_cc_sm4450_driver = {
	.probe = cam_cc_sm4450_probe,
	.driver = {
		.name = "camcc-sm4450",
		.of_match_table = cam_cc_sm4450_match_table,
	},
};

module_platform_driver(cam_cc_sm4450_driver);

MODULE_DESCRIPTION("QTI CAMCC SM4450 Driver");
MODULE_LICENSE("GPL");
