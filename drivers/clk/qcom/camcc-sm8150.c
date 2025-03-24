// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/pm_runtime.h>

#include <dt-bindings/clock/qcom,sm8150-camcc.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "common.h"
#include "gdsc.h"
#include "reset.h"

enum {
	DT_BI_TCXO,
	DT_IFACE,
};

enum {
	P_BI_TCXO,
	P_CAM_CC_PLL0_OUT_EVEN,
	P_CAM_CC_PLL0_OUT_MAIN,
	P_CAM_CC_PLL0_OUT_ODD,
	P_CAM_CC_PLL1_OUT_EVEN,
	P_CAM_CC_PLL2_OUT_EARLY,
	P_CAM_CC_PLL2_OUT_MAIN,
	P_CAM_CC_PLL3_OUT_EVEN,
	P_CAM_CC_PLL4_OUT_EVEN,
};

static const struct pll_vco regera_vco[] = {
	{ 600000000, 3300000000, 0 },
};

static const struct pll_vco trion_vco[] = {
	{ 249600000, 2000000000, 0 },
};

static const struct alpha_pll_config cam_cc_pll0_config = {
	.l = 0x3e,
	.alpha = 0x8000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002267,
	.config_ctl_hi1_val = 0x00000024,
	.test_ctl_val = 0x00000000,
	.test_ctl_hi_val = 0x00000000,
	.test_ctl_hi1_val = 0x00000020,
	.user_ctl_val = 0x00003100,
	.user_ctl_hi_val = 0x00000805,
	.user_ctl_hi1_val = 0x000000D0,
};

static struct clk_alpha_pll cam_cc_pll0 = {
	.offset = 0x0,
	.vco_table = trion_vco,
	.num_vco = ARRAY_SIZE(trion_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TRION],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_pll0",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_trion_ops,
		},
	},
};

static const struct clk_div_table post_div_table_cam_cc_pll0_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv cam_cc_pll0_out_even = {
	.offset = 0x0,
	.post_div_shift = 8,
	.post_div_table = post_div_table_cam_cc_pll0_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_cam_cc_pll0_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TRION],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_pll0_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&cam_cc_pll0.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_trion_ops,
	},
};

static const struct clk_div_table post_div_table_cam_cc_pll0_out_odd[] = {
	{ 0x3, 3 },
	{ }
};

static struct clk_alpha_pll_postdiv cam_cc_pll0_out_odd = {
	.offset = 0x0,
	.post_div_shift = 12,
	.post_div_table = post_div_table_cam_cc_pll0_out_odd,
	.num_post_div = ARRAY_SIZE(post_div_table_cam_cc_pll0_out_odd),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TRION],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_pll0_out_odd",
		.parent_hws = (const struct clk_hw*[]) {
			&cam_cc_pll0.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_trion_ops,
	},
};

static const struct alpha_pll_config cam_cc_pll1_config = {
	.l = 0x1f,
	.alpha = 0x4000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002267,
	.config_ctl_hi1_val = 0x00000024,
	.test_ctl_val = 0x00000000,
	.test_ctl_hi_val = 0x00000000,
	.test_ctl_hi1_val = 0x00000020,
	.user_ctl_val = 0x00000100,
	.user_ctl_hi_val = 0x00000805,
	.user_ctl_hi1_val = 0x000000D0,
};

static struct clk_alpha_pll cam_cc_pll1 = {
	.offset = 0x1000,
	.vco_table = trion_vco,
	.num_vco = ARRAY_SIZE(trion_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TRION],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_pll1",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_trion_ops,
		},
	},
};

static const struct clk_div_table post_div_table_cam_cc_pll1_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv cam_cc_pll1_out_even = {
	.offset = 0x1000,
	.post_div_shift = 8,
	.post_div_table = post_div_table_cam_cc_pll1_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_cam_cc_pll1_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TRION],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_pll1_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&cam_cc_pll1.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_trion_ops,
	},
};

static const struct alpha_pll_config cam_cc_pll2_config = {
	.l = 0x32,
	.alpha = 0x0,
	.config_ctl_val = 0x10000807,
	.config_ctl_hi_val = 0x00000011,
	.config_ctl_hi1_val = 0x04300142,
	.test_ctl_val = 0x04000400,
	.test_ctl_hi_val = 0x00004000,
	.test_ctl_hi1_val = 0x00000000,
	.user_ctl_val = 0x00000100,
	.user_ctl_hi_val = 0x00000000,
	.user_ctl_hi1_val = 0x00000000,
};

static struct clk_alpha_pll cam_cc_pll2 = {
	.offset = 0x2000,
	.vco_table = regera_vco,
	.num_vco = ARRAY_SIZE(regera_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_REGERA],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_pll2",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_regera_ops,
		},
	},
};

static const struct clk_div_table post_div_table_cam_cc_pll2_out_main[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv cam_cc_pll2_out_main = {
	.offset = 0x2000,
	.post_div_shift = 8,
	.post_div_table = post_div_table_cam_cc_pll2_out_main,
	.num_post_div = ARRAY_SIZE(post_div_table_cam_cc_pll2_out_main),
	.width = 2,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_REGERA],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_pll2_out_main",
		.parent_hws = (const struct clk_hw*[]) {
			&cam_cc_pll2.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_trion_ops,
	},
};

static const struct alpha_pll_config cam_cc_pll3_config = {
	.l = 0x29,
	.alpha = 0xaaaa,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002267,
	.config_ctl_hi1_val = 0x00000024,
	.test_ctl_val = 0x00000000,
	.test_ctl_hi_val = 0x00000000,
	.test_ctl_hi1_val = 0x00000020,
	.user_ctl_val = 0x00000100,
	.user_ctl_hi_val = 0x00000805,
	.user_ctl_hi1_val = 0x000000D0,
};

static struct clk_alpha_pll cam_cc_pll3 = {
	.offset = 0x3000,
	.vco_table = trion_vco,
	.num_vco = ARRAY_SIZE(trion_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TRION],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_pll3",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_trion_ops,
		},
	},
};

static const struct clk_div_table post_div_table_cam_cc_pll3_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv cam_cc_pll3_out_even = {
	.offset = 0x3000,
	.post_div_shift = 8,
	.post_div_table = post_div_table_cam_cc_pll3_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_cam_cc_pll3_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TRION],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_pll3_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&cam_cc_pll3.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_trion_ops,
	},
};

static const struct alpha_pll_config cam_cc_pll4_config = {
	.l = 0x29,
	.alpha = 0xaaaa,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002267,
	.config_ctl_hi1_val = 0x00000024,
	.test_ctl_val = 0x00000000,
	.test_ctl_hi_val = 0x00000000,
	.test_ctl_hi1_val = 0x00000020,
	.user_ctl_val = 0x00000100,
	.user_ctl_hi_val = 0x00000805,
	.user_ctl_hi1_val = 0x000000D0,
};

static struct clk_alpha_pll cam_cc_pll4 = {
	.offset = 0x4000,
	.vco_table = trion_vco,
	.num_vco = ARRAY_SIZE(trion_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TRION],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_pll4",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_trion_ops,
		},
	},
};

static const struct clk_div_table post_div_table_cam_cc_pll4_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv cam_cc_pll4_out_even = {
	.offset = 0x4000,
	.post_div_shift = 8,
	.post_div_table = post_div_table_cam_cc_pll4_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_cam_cc_pll4_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TRION],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_pll4_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&cam_cc_pll4.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_trion_ops,
	},
};

static const struct parent_map cam_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL0_OUT_MAIN, 1 },
	{ P_CAM_CC_PLL0_OUT_EVEN, 2 },
	{ P_CAM_CC_PLL0_OUT_ODD, 3 },
	{ P_CAM_CC_PLL2_OUT_MAIN, 5 },
};

static const struct clk_parent_data cam_cc_parent_data_0[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &cam_cc_pll0.clkr.hw },
	{ .hw = &cam_cc_pll0_out_even.clkr.hw },
	{ .hw = &cam_cc_pll0_out_odd.clkr.hw },
	{ .hw = &cam_cc_pll2_out_main.clkr.hw },
};

static const struct parent_map cam_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL2_OUT_EARLY, 5 },
};

static const struct clk_parent_data cam_cc_parent_data_1[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &cam_cc_pll2.clkr.hw },
};

static const struct parent_map cam_cc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL3_OUT_EVEN, 6 },
};

static const struct clk_parent_data cam_cc_parent_data_2[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &cam_cc_pll3_out_even.clkr.hw },
};

static const struct parent_map cam_cc_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL4_OUT_EVEN, 6 },
};

static const struct clk_parent_data cam_cc_parent_data_3[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &cam_cc_pll4_out_even.clkr.hw },
};

static const struct parent_map cam_cc_parent_map_4[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL1_OUT_EVEN, 4 },
};

static const struct clk_parent_data cam_cc_parent_data_4[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &cam_cc_pll1_out_even.clkr.hw },
};

static const struct freq_tbl ftbl_cam_cc_bps_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(100000000, P_CAM_CC_PLL0_OUT_EVEN, 6, 0, 0),
	F(200000000, P_CAM_CC_PLL0_OUT_ODD, 2, 0, 0),
	F(400000000, P_CAM_CC_PLL0_OUT_ODD, 1, 0, 0),
	F(480000000, P_CAM_CC_PLL2_OUT_MAIN, 1, 0, 0),
	F(600000000, P_CAM_CC_PLL0_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_bps_clk_src = {
	.cmd_rcgr = 0x7010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_bps_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_bps_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_camnoc_axi_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(150000000, P_CAM_CC_PLL0_OUT_EVEN, 4, 0, 0),
	F(266666667, P_CAM_CC_PLL0_OUT_ODD, 1.5, 0, 0),
	F(320000000, P_CAM_CC_PLL2_OUT_MAIN, 1.5, 0, 0),
	F(400000000, P_CAM_CC_PLL0_OUT_MAIN, 3, 0, 0),
	F(480000000, P_CAM_CC_PLL2_OUT_MAIN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_camnoc_axi_clk_src = {
	.cmd_rcgr = 0xc170,
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
	{ }
};

static struct clk_rcg2 cam_cc_cci_0_clk_src = {
	.cmd_rcgr = 0xc108,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_cci_0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_cci_0_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_cci_1_clk_src = {
	.cmd_rcgr = 0xc124,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_cci_0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_cci_1_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_cphy_rx_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(400000000, P_CAM_CC_PLL0_OUT_ODD, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_cphy_rx_clk_src = {
	.cmd_rcgr = 0xa064,
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

static const struct freq_tbl ftbl_cam_cc_csi0phytimer_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(300000000, P_CAM_CC_PLL0_OUT_EVEN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_csi0phytimer_clk_src = {
	.cmd_rcgr = 0x6004,
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
	.cmd_rcgr = 0x6028,
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
	.cmd_rcgr = 0x604c,
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

static struct clk_rcg2 cam_cc_csi3phytimer_clk_src = {
	.cmd_rcgr = 0x6070,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_csi0phytimer_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_csi3phytimer_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_fast_ahb_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(50000000, P_CAM_CC_PLL0_OUT_EVEN, 12, 0, 0),
	F(100000000, P_CAM_CC_PLL0_OUT_EVEN, 6, 0, 0),
	F(200000000, P_CAM_CC_PLL0_OUT_EVEN, 3, 0, 0),
	F(300000000, P_CAM_CC_PLL0_OUT_MAIN, 4, 0, 0),
	F(400000000, P_CAM_CC_PLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_fast_ahb_clk_src = {
	.cmd_rcgr = 0x703c,
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

static const struct freq_tbl ftbl_cam_cc_fd_core_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(400000000, P_CAM_CC_PLL0_OUT_ODD, 1, 0, 0),
	F(480000000, P_CAM_CC_PLL2_OUT_MAIN, 1, 0, 0),
	F(600000000, P_CAM_CC_PLL0_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_fd_core_clk_src = {
	.cmd_rcgr = 0xc0e0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_fd_core_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_fd_core_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_icp_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(400000000, P_CAM_CC_PLL0_OUT_ODD, 1, 0, 0),
	F(600000000, P_CAM_CC_PLL0_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_icp_clk_src = {
	.cmd_rcgr = 0xc0b8,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_icp_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_icp_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_ife_0_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(400000000, P_CAM_CC_PLL3_OUT_EVEN, 1, 0, 0),
	F(558000000, P_CAM_CC_PLL3_OUT_EVEN, 1, 0, 0),
	F(637000000, P_CAM_CC_PLL3_OUT_EVEN, 1, 0, 0),
	F(847000000, P_CAM_CC_PLL3_OUT_EVEN, 1, 0, 0),
	F(950000000, P_CAM_CC_PLL3_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_ife_0_clk_src = {
	.cmd_rcgr = 0xa010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_2,
	.freq_tbl = ftbl_cam_cc_ife_0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_ife_0_clk_src",
		.parent_data = cam_cc_parent_data_2,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_ife_0_csid_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(75000000, P_CAM_CC_PLL0_OUT_EVEN, 8, 0, 0),
	F(400000000, P_CAM_CC_PLL0_OUT_ODD, 1, 0, 0),
	F(480000000, P_CAM_CC_PLL2_OUT_MAIN, 1, 0, 0),
	F(600000000, P_CAM_CC_PLL0_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_ife_0_csid_clk_src = {
	.cmd_rcgr = 0xa03c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_ife_0_csid_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_ife_0_csid_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_ife_1_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(400000000, P_CAM_CC_PLL4_OUT_EVEN, 1, 0, 0),
	F(558000000, P_CAM_CC_PLL4_OUT_EVEN, 1, 0, 0),
	F(637000000, P_CAM_CC_PLL4_OUT_EVEN, 1, 0, 0),
	F(847000000, P_CAM_CC_PLL4_OUT_EVEN, 1, 0, 0),
	F(950000000, P_CAM_CC_PLL4_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_ife_1_clk_src = {
	.cmd_rcgr = 0xb010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_3,
	.freq_tbl = ftbl_cam_cc_ife_1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_ife_1_clk_src",
		.parent_data = cam_cc_parent_data_3,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_ife_1_csid_clk_src = {
	.cmd_rcgr = 0xb034,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_ife_0_csid_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_ife_1_csid_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_ife_lite_0_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(320000000, P_CAM_CC_PLL2_OUT_MAIN, 1.5, 0, 0),
	F(400000000, P_CAM_CC_PLL0_OUT_ODD, 1, 0, 0),
	F(480000000, P_CAM_CC_PLL2_OUT_MAIN, 1, 0, 0),
	F(600000000, P_CAM_CC_PLL0_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_ife_lite_0_clk_src = {
	.cmd_rcgr = 0xc004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_ife_lite_0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_ife_lite_0_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_ife_lite_0_csid_clk_src = {
	.cmd_rcgr = 0xc020,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_fd_core_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_ife_lite_0_csid_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_ife_lite_1_clk_src = {
	.cmd_rcgr = 0xc048,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_ife_lite_0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_ife_lite_1_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_ife_lite_1_csid_clk_src = {
	.cmd_rcgr = 0xc064,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_fd_core_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_ife_lite_1_csid_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_ipe_0_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(300000000, P_CAM_CC_PLL1_OUT_EVEN, 1, 0, 0),
	F(475000000, P_CAM_CC_PLL1_OUT_EVEN, 1, 0, 0),
	F(520000000, P_CAM_CC_PLL1_OUT_EVEN, 1, 0, 0),
	F(600000000, P_CAM_CC_PLL1_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_ipe_0_clk_src = {
	.cmd_rcgr = 0x8010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_4,
	.freq_tbl = ftbl_cam_cc_ipe_0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_ipe_0_clk_src",
		.parent_data = cam_cc_parent_data_4,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_4),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 cam_cc_jpeg_clk_src = {
	.cmd_rcgr = 0xc08c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_bps_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_jpeg_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_lrme_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(100000000, P_CAM_CC_PLL0_OUT_EVEN, 6, 0, 0),
	F(240000000, P_CAM_CC_PLL2_OUT_MAIN, 2, 0, 0),
	F(300000000, P_CAM_CC_PLL0_OUT_EVEN, 2, 0, 0),
	F(320000000, P_CAM_CC_PLL2_OUT_MAIN, 1.5, 0, 0),
	F(400000000, P_CAM_CC_PLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_lrme_clk_src = {
	.cmd_rcgr = 0xc144,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_lrme_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "cam_cc_lrme_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_cam_cc_mclk0_clk_src[] = {
	F(12000000, P_CAM_CC_PLL2_OUT_EARLY, 10, 1, 8),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(24000000, P_CAM_CC_PLL2_OUT_EARLY, 10, 1, 4),
	F(68571429, P_CAM_CC_PLL2_OUT_EARLY, 14, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_mclk0_clk_src = {
	.cmd_rcgr = 0x5004,
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
	.cmd_rcgr = 0x5024,
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
	.cmd_rcgr = 0x5044,
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
	.cmd_rcgr = 0x5064,
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

static const struct freq_tbl ftbl_cam_cc_slow_ahb_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(80000000, P_CAM_CC_PLL0_OUT_EVEN, 7.5, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_slow_ahb_clk_src = {
	.cmd_rcgr = 0x7058,
	.mnd_width = 8,
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

static struct clk_branch cam_cc_bps_ahb_clk = {
	.halt_reg = 0x7070,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x7070,
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
	.halt_reg = 0x7054,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x7054,
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

static struct clk_branch cam_cc_bps_axi_clk = {
	.halt_reg = 0x7038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x7038,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_bps_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_camnoc_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_bps_clk = {
	.halt_reg = 0x7028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x7028,
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

static struct clk_branch cam_cc_camnoc_axi_clk = {
	.halt_reg = 0xc18c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc18c,
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

static struct clk_branch cam_cc_camnoc_dcd_xo_clk = {
	.halt_reg = 0xc194,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc194,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_camnoc_dcd_xo_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_cci_0_clk = {
	.halt_reg = 0xc120,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc120,
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
	.halt_reg = 0xc13c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc13c,
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
	.halt_reg = 0xc1c8,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0xc1c8,
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
	.halt_reg = 0xc168,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc168,
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

static struct clk_branch cam_cc_csi0phytimer_clk = {
	.halt_reg = 0x601c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x601c,
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
	.halt_reg = 0x6040,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6040,
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
	.halt_reg = 0x6064,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6064,
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
	.halt_reg = 0x6088,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6088,
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

static struct clk_branch cam_cc_csiphy0_clk = {
	.halt_reg = 0x6020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6020,
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
	.halt_reg = 0x6044,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6044,
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
	.halt_reg = 0x6068,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6068,
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
	.halt_reg = 0x608c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x608c,
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

static struct clk_branch cam_cc_fd_core_clk = {
	.halt_reg = 0xc0f8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc0f8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_fd_core_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_fd_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_fd_core_uar_clk = {
	.halt_reg = 0xc100,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc100,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_fd_core_uar_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_fd_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_icp_ahb_clk = {
	.halt_reg = 0xc0d8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc0d8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_icp_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_slow_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_icp_clk = {
	.halt_reg = 0xc0d0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc0d0,
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

static struct clk_branch cam_cc_ife_0_axi_clk = {
	.halt_reg = 0xa080,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa080,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_0_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_camnoc_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_0_clk = {
	.halt_reg = 0xa028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_0_cphy_rx_clk = {
	.halt_reg = 0xa07c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa07c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_0_cphy_rx_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_0_csid_clk = {
	.halt_reg = 0xa054,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa054,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_0_csid_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_0_csid_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_0_dsp_clk = {
	.halt_reg = 0xa038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa038,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_0_dsp_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_1_axi_clk = {
	.halt_reg = 0xb058,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb058,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_1_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_camnoc_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_1_clk = {
	.halt_reg = 0xb028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_1_cphy_rx_clk = {
	.halt_reg = 0xb054,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb054,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_1_cphy_rx_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_1_csid_clk = {
	.halt_reg = 0xb04c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb04c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_1_csid_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_1_csid_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_1_dsp_clk = {
	.halt_reg = 0xb030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_1_dsp_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_lite_0_clk = {
	.halt_reg = 0xc01c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_lite_0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_lite_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_lite_0_cphy_rx_clk = {
	.halt_reg = 0xc040,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc040,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_lite_0_cphy_rx_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_lite_0_csid_clk = {
	.halt_reg = 0xc038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc038,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_lite_0_csid_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_lite_0_csid_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_lite_1_clk = {
	.halt_reg = 0xc060,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc060,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_lite_1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_lite_1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_lite_1_cphy_rx_clk = {
	.halt_reg = 0xc084,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc084,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_lite_1_cphy_rx_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_lite_1_csid_clk = {
	.halt_reg = 0xc07c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc07c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ife_lite_1_csid_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ife_lite_1_csid_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ipe_0_ahb_clk = {
	.halt_reg = 0x8040,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8040,
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

static struct clk_branch cam_cc_ipe_0_areg_clk = {
	.halt_reg = 0x803c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x803c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ipe_0_areg_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ipe_0_axi_clk = {
	.halt_reg = 0x8038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8038,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ipe_0_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_camnoc_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ipe_0_clk = {
	.halt_reg = 0x8028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8028,
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

static struct clk_branch cam_cc_ipe_1_ahb_clk = {
	.halt_reg = 0x9028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9028,
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

static struct clk_branch cam_cc_ipe_1_areg_clk = {
	.halt_reg = 0x9024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ipe_1_areg_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ipe_1_axi_clk = {
	.halt_reg = 0x9020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ipe_1_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_camnoc_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ipe_1_clk = {
	.halt_reg = 0x9010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_ipe_1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_ipe_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_jpeg_clk = {
	.halt_reg = 0xc0a4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc0a4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_jpeg_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_jpeg_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_lrme_clk = {
	.halt_reg = 0xc15c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc15c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "cam_cc_lrme_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&cam_cc_lrme_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_mclk0_clk = {
	.halt_reg = 0x501c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x501c,
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
	.halt_reg = 0x503c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x503c,
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
	.halt_reg = 0x505c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x505c,
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
	.halt_reg = 0x507c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x507c,
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

static struct gdsc titan_top_gdsc = {
	.gdscr = 0xc1bc,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "titan_top_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR,
};

static struct gdsc bps_gdsc = {
	.gdscr = 0x7004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "bps_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.parent = &titan_top_gdsc.pd,
	.flags = POLL_CFG_GDSCR,
};

static struct gdsc ife_0_gdsc = {
	.gdscr = 0xa004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "ife_0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.parent = &titan_top_gdsc.pd,
	.flags = POLL_CFG_GDSCR,
};

static struct gdsc ife_1_gdsc = {
	.gdscr = 0xb004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "ife_1_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.parent = &titan_top_gdsc.pd,
	.flags = POLL_CFG_GDSCR,
};

static struct gdsc ipe_0_gdsc = {
	.gdscr = 0x8004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "ipe_0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.parent = &titan_top_gdsc.pd,
	.flags = POLL_CFG_GDSCR,
};

static struct gdsc ipe_1_gdsc = {
	.gdscr = 0x9004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "ipe_1_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.parent = &titan_top_gdsc.pd,
	.flags = POLL_CFG_GDSCR,
};

static struct clk_regmap *cam_cc_sm8150_clocks[] = {
	[CAM_CC_PLL0] = &cam_cc_pll0.clkr,
	[CAM_CC_PLL0_OUT_EVEN] = &cam_cc_pll0_out_even.clkr,
	[CAM_CC_PLL0_OUT_ODD] = &cam_cc_pll0_out_odd.clkr,
	[CAM_CC_PLL1] = &cam_cc_pll1.clkr,
	[CAM_CC_PLL1_OUT_EVEN] = &cam_cc_pll1_out_even.clkr,
	[CAM_CC_PLL2] = &cam_cc_pll2.clkr,
	[CAM_CC_PLL2_OUT_MAIN] = &cam_cc_pll2_out_main.clkr,
	[CAM_CC_PLL3] = &cam_cc_pll3.clkr,
	[CAM_CC_PLL3_OUT_EVEN] = &cam_cc_pll3_out_even.clkr,
	[CAM_CC_PLL4] = &cam_cc_pll4.clkr,
	[CAM_CC_PLL4_OUT_EVEN] = &cam_cc_pll4_out_even.clkr,
	[CAM_CC_BPS_AHB_CLK] = &cam_cc_bps_ahb_clk.clkr,
	[CAM_CC_BPS_AREG_CLK] = &cam_cc_bps_areg_clk.clkr,
	[CAM_CC_BPS_AXI_CLK] = &cam_cc_bps_axi_clk.clkr,
	[CAM_CC_BPS_CLK] = &cam_cc_bps_clk.clkr,
	[CAM_CC_BPS_CLK_SRC] = &cam_cc_bps_clk_src.clkr,
	[CAM_CC_CAMNOC_AXI_CLK] = &cam_cc_camnoc_axi_clk.clkr,
	[CAM_CC_CAMNOC_AXI_CLK_SRC] = &cam_cc_camnoc_axi_clk_src.clkr,
	[CAM_CC_CAMNOC_DCD_XO_CLK] = &cam_cc_camnoc_dcd_xo_clk.clkr,
	[CAM_CC_CCI_0_CLK] = &cam_cc_cci_0_clk.clkr,
	[CAM_CC_CCI_0_CLK_SRC] = &cam_cc_cci_0_clk_src.clkr,
	[CAM_CC_CCI_1_CLK] = &cam_cc_cci_1_clk.clkr,
	[CAM_CC_CCI_1_CLK_SRC] = &cam_cc_cci_1_clk_src.clkr,
	[CAM_CC_CORE_AHB_CLK] = &cam_cc_core_ahb_clk.clkr,
	[CAM_CC_CPAS_AHB_CLK] = &cam_cc_cpas_ahb_clk.clkr,
	[CAM_CC_CPHY_RX_CLK_SRC] = &cam_cc_cphy_rx_clk_src.clkr,
	[CAM_CC_CSI0PHYTIMER_CLK] = &cam_cc_csi0phytimer_clk.clkr,
	[CAM_CC_CSI0PHYTIMER_CLK_SRC] = &cam_cc_csi0phytimer_clk_src.clkr,
	[CAM_CC_CSI1PHYTIMER_CLK] = &cam_cc_csi1phytimer_clk.clkr,
	[CAM_CC_CSI1PHYTIMER_CLK_SRC] = &cam_cc_csi1phytimer_clk_src.clkr,
	[CAM_CC_CSI2PHYTIMER_CLK] = &cam_cc_csi2phytimer_clk.clkr,
	[CAM_CC_CSI2PHYTIMER_CLK_SRC] = &cam_cc_csi2phytimer_clk_src.clkr,
	[CAM_CC_CSI3PHYTIMER_CLK] = &cam_cc_csi3phytimer_clk.clkr,
	[CAM_CC_CSI3PHYTIMER_CLK_SRC] = &cam_cc_csi3phytimer_clk_src.clkr,
	[CAM_CC_CSIPHY0_CLK] = &cam_cc_csiphy0_clk.clkr,
	[CAM_CC_CSIPHY1_CLK] = &cam_cc_csiphy1_clk.clkr,
	[CAM_CC_CSIPHY2_CLK] = &cam_cc_csiphy2_clk.clkr,
	[CAM_CC_CSIPHY3_CLK] = &cam_cc_csiphy3_clk.clkr,
	[CAM_CC_FAST_AHB_CLK_SRC] = &cam_cc_fast_ahb_clk_src.clkr,
	[CAM_CC_FD_CORE_CLK] = &cam_cc_fd_core_clk.clkr,
	[CAM_CC_FD_CORE_CLK_SRC] = &cam_cc_fd_core_clk_src.clkr,
	[CAM_CC_FD_CORE_UAR_CLK] = &cam_cc_fd_core_uar_clk.clkr,
	[CAM_CC_ICP_AHB_CLK] = &cam_cc_icp_ahb_clk.clkr,
	[CAM_CC_ICP_CLK] = &cam_cc_icp_clk.clkr,
	[CAM_CC_ICP_CLK_SRC] = &cam_cc_icp_clk_src.clkr,
	[CAM_CC_IFE_0_AXI_CLK] = &cam_cc_ife_0_axi_clk.clkr,
	[CAM_CC_IFE_0_CLK] = &cam_cc_ife_0_clk.clkr,
	[CAM_CC_IFE_0_CLK_SRC] = &cam_cc_ife_0_clk_src.clkr,
	[CAM_CC_IFE_0_CPHY_RX_CLK] = &cam_cc_ife_0_cphy_rx_clk.clkr,
	[CAM_CC_IFE_0_CSID_CLK] = &cam_cc_ife_0_csid_clk.clkr,
	[CAM_CC_IFE_0_CSID_CLK_SRC] = &cam_cc_ife_0_csid_clk_src.clkr,
	[CAM_CC_IFE_0_DSP_CLK] = &cam_cc_ife_0_dsp_clk.clkr,
	[CAM_CC_IFE_1_AXI_CLK] = &cam_cc_ife_1_axi_clk.clkr,
	[CAM_CC_IFE_1_CLK] = &cam_cc_ife_1_clk.clkr,
	[CAM_CC_IFE_1_CLK_SRC] = &cam_cc_ife_1_clk_src.clkr,
	[CAM_CC_IFE_1_CPHY_RX_CLK] = &cam_cc_ife_1_cphy_rx_clk.clkr,
	[CAM_CC_IFE_1_CSID_CLK] = &cam_cc_ife_1_csid_clk.clkr,
	[CAM_CC_IFE_1_CSID_CLK_SRC] = &cam_cc_ife_1_csid_clk_src.clkr,
	[CAM_CC_IFE_1_DSP_CLK] = &cam_cc_ife_1_dsp_clk.clkr,
	[CAM_CC_IFE_LITE_0_CLK] = &cam_cc_ife_lite_0_clk.clkr,
	[CAM_CC_IFE_LITE_0_CLK_SRC] = &cam_cc_ife_lite_0_clk_src.clkr,
	[CAM_CC_IFE_LITE_0_CPHY_RX_CLK] = &cam_cc_ife_lite_0_cphy_rx_clk.clkr,
	[CAM_CC_IFE_LITE_0_CSID_CLK] = &cam_cc_ife_lite_0_csid_clk.clkr,
	[CAM_CC_IFE_LITE_0_CSID_CLK_SRC] = &cam_cc_ife_lite_0_csid_clk_src.clkr,
	[CAM_CC_IFE_LITE_1_CLK] = &cam_cc_ife_lite_1_clk.clkr,
	[CAM_CC_IFE_LITE_1_CLK_SRC] = &cam_cc_ife_lite_1_clk_src.clkr,
	[CAM_CC_IFE_LITE_1_CPHY_RX_CLK] = &cam_cc_ife_lite_1_cphy_rx_clk.clkr,
	[CAM_CC_IFE_LITE_1_CSID_CLK] = &cam_cc_ife_lite_1_csid_clk.clkr,
	[CAM_CC_IFE_LITE_1_CSID_CLK_SRC] = &cam_cc_ife_lite_1_csid_clk_src.clkr,
	[CAM_CC_IPE_0_AHB_CLK] = &cam_cc_ipe_0_ahb_clk.clkr,
	[CAM_CC_IPE_0_AREG_CLK] = &cam_cc_ipe_0_areg_clk.clkr,
	[CAM_CC_IPE_0_AXI_CLK] = &cam_cc_ipe_0_axi_clk.clkr,
	[CAM_CC_IPE_0_CLK] = &cam_cc_ipe_0_clk.clkr,
	[CAM_CC_IPE_0_CLK_SRC] = &cam_cc_ipe_0_clk_src.clkr,
	[CAM_CC_IPE_1_AHB_CLK] = &cam_cc_ipe_1_ahb_clk.clkr,
	[CAM_CC_IPE_1_AREG_CLK] = &cam_cc_ipe_1_areg_clk.clkr,
	[CAM_CC_IPE_1_AXI_CLK] = &cam_cc_ipe_1_axi_clk.clkr,
	[CAM_CC_IPE_1_CLK] = &cam_cc_ipe_1_clk.clkr,
	[CAM_CC_JPEG_CLK] = &cam_cc_jpeg_clk.clkr,
	[CAM_CC_JPEG_CLK_SRC] = &cam_cc_jpeg_clk_src.clkr,
	[CAM_CC_LRME_CLK] = &cam_cc_lrme_clk.clkr,
	[CAM_CC_LRME_CLK_SRC] = &cam_cc_lrme_clk_src.clkr,
	[CAM_CC_MCLK0_CLK] = &cam_cc_mclk0_clk.clkr,
	[CAM_CC_MCLK0_CLK_SRC] = &cam_cc_mclk0_clk_src.clkr,
	[CAM_CC_MCLK1_CLK] = &cam_cc_mclk1_clk.clkr,
	[CAM_CC_MCLK1_CLK_SRC] = &cam_cc_mclk1_clk_src.clkr,
	[CAM_CC_MCLK2_CLK] = &cam_cc_mclk2_clk.clkr,
	[CAM_CC_MCLK2_CLK_SRC] = &cam_cc_mclk2_clk_src.clkr,
	[CAM_CC_MCLK3_CLK] = &cam_cc_mclk3_clk.clkr,
	[CAM_CC_MCLK3_CLK_SRC] = &cam_cc_mclk3_clk_src.clkr,
	[CAM_CC_SLOW_AHB_CLK_SRC] = &cam_cc_slow_ahb_clk_src.clkr,
};

static struct gdsc *cam_cc_sm8150_gdscs[] = {
	[TITAN_TOP_GDSC] = &titan_top_gdsc,
	[BPS_GDSC] = &bps_gdsc,
	[IFE_0_GDSC] = &ife_0_gdsc,
	[IFE_1_GDSC] = &ife_1_gdsc,
	[IPE_0_GDSC] = &ipe_0_gdsc,
	[IPE_1_GDSC] = &ipe_1_gdsc,
};

static const struct qcom_reset_map cam_cc_sm8150_resets[] = {
	[CAM_CC_BPS_BCR] = { 0x7000 },
	[CAM_CC_CAMNOC_BCR] = { 0xc16c },
	[CAM_CC_CCI_BCR] = { 0xc104 },
	[CAM_CC_CPAS_BCR] = { 0xc164 },
	[CAM_CC_CSI0PHY_BCR] = { 0x6000 },
	[CAM_CC_CSI1PHY_BCR] = { 0x6024 },
	[CAM_CC_CSI2PHY_BCR] = { 0x6048 },
	[CAM_CC_CSI3PHY_BCR] = { 0x606c },
	[CAM_CC_FD_BCR] = { 0xc0dc },
	[CAM_CC_ICP_BCR] = { 0xc0b4 },
	[CAM_CC_IFE_0_BCR] = { 0xa000 },
	[CAM_CC_IFE_1_BCR] = { 0xb000 },
	[CAM_CC_IFE_LITE_0_BCR] = { 0xc000 },
	[CAM_CC_IFE_LITE_1_BCR] = { 0xc044 },
	[CAM_CC_IPE_0_BCR] = { 0x8000 },
	[CAM_CC_IPE_1_BCR] = { 0x9000 },
	[CAM_CC_JPEG_BCR] = { 0xc088 },
	[CAM_CC_LRME_BCR] = { 0xc140 },
	[CAM_CC_MCLK0_BCR] = { 0x5000 },
	[CAM_CC_MCLK1_BCR] = { 0x5020 },
	[CAM_CC_MCLK2_BCR] = { 0x5040 },
	[CAM_CC_MCLK3_BCR] = { 0x5060 },
};

static const struct regmap_config cam_cc_sm8150_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xe004,
	.fast_io = true,
};

static const struct qcom_cc_desc cam_cc_sm8150_desc = {
	.config = &cam_cc_sm8150_regmap_config,
	.clks = cam_cc_sm8150_clocks,
	.num_clks = ARRAY_SIZE(cam_cc_sm8150_clocks),
	.resets = cam_cc_sm8150_resets,
	.num_resets = ARRAY_SIZE(cam_cc_sm8150_resets),
	.gdscs = cam_cc_sm8150_gdscs,
	.num_gdscs = ARRAY_SIZE(cam_cc_sm8150_gdscs),
};

static const struct of_device_id cam_cc_sm8150_match_table[] = {
	{ .compatible = "qcom,sm8150-camcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, cam_cc_sm8150_match_table);

static int cam_cc_sm8150_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret;

	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret)
		return ret;

	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret)
		return ret;

	regmap = qcom_cc_map(pdev, &cam_cc_sm8150_desc);
	if (IS_ERR(regmap)) {
		pm_runtime_put(&pdev->dev);
		return PTR_ERR(regmap);
	}

	clk_trion_pll_configure(&cam_cc_pll0, regmap, &cam_cc_pll0_config);
	clk_trion_pll_configure(&cam_cc_pll1, regmap, &cam_cc_pll1_config);
	clk_regera_pll_configure(&cam_cc_pll2, regmap, &cam_cc_pll2_config);
	clk_trion_pll_configure(&cam_cc_pll3, regmap, &cam_cc_pll3_config);
	clk_trion_pll_configure(&cam_cc_pll4, regmap, &cam_cc_pll4_config);

	/* Keep the critical clock always-on */
	qcom_branch_set_clk_en(regmap, 0xc1e4); /* cam_cc_gdsc_clk */

	ret = qcom_cc_really_probe(&pdev->dev, &cam_cc_sm8150_desc, regmap);

	pm_runtime_put(&pdev->dev);

	return ret;
}

static struct platform_driver cam_cc_sm8150_driver = {
	.probe = cam_cc_sm8150_probe,
	.driver = {
		.name = "camcc-sm8150",
		.of_match_table = cam_cc_sm8150_match_table,
	},
};

module_platform_driver(cam_cc_sm8150_driver);

MODULE_DESCRIPTION("QTI CAM_CC SM8150 Driver");
MODULE_LICENSE("GPL");
