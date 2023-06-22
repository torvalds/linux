// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/pm_runtime.h>

#include <dt-bindings/clock/qcom,camcc-lemans.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "common.h"
#include "reset.h"
#include "vdd-level-sm8150.h"

static DEFINE_VDD_REGULATORS(vdd_mm, VDD_NOMINAL + 1, 1, vdd_corner);
static DEFINE_VDD_REGULATORS(vdd_mxa, VDD_LOW_L1 + 1, 1, vdd_corner);
static DEFINE_VDD_REGULATORS(vdd_mxc, VDD_HIGH + 1, 1, vdd_corner);

static struct clk_vdd_class *cam_cc_lemans_regulators[] = {
	&vdd_mm,
	&vdd_mxa,
	&vdd_mxc,
};

static struct clk_vdd_class *cam_cc_lemans_regulators_1[] = {
	&vdd_mm,
	&vdd_mxc,
};

enum {
	P_BI_TCXO,
	P_CAM_CC_PLL0_OUT_EVEN,
	P_CAM_CC_PLL0_OUT_MAIN,
	P_CAM_CC_PLL0_OUT_ODD,
	P_CAM_CC_PLL2_OUT_EVEN,
	P_CAM_CC_PLL2_OUT_MAIN,
	P_CAM_CC_PLL3_OUT_EVEN,
	P_CAM_CC_PLL4_OUT_EVEN,
	P_CAM_CC_PLL5_OUT_EVEN,
	P_SLEEP_CLK,
};

static const struct pll_vco lucid_evo_vco[] = {
	{ 249600000, 2020000000, 0 },
};

static const struct pll_vco rivian_evo_vco[] = {
	{ 864000000, 1056000000, 0 },
};

/* 1200MHz configuration */
static struct alpha_pll_config cam_cc_pll0_config = {
	.l = 0x3E,
	.cal_l = 0x44,
	.alpha = 0x8000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x32AA299C,
	.user_ctl_val = 0x00008407,
	.user_ctl_hi_val = 0x00400805,
};

static struct clk_alpha_pll cam_cc_pll0 = {
	.offset = 0x0,
	.vco_table = lucid_evo_vco,
	.num_vco = ARRAY_SIZE(lucid_evo_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.config = &cam_cc_pll0_config,
	.clkr = {
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_pll0",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_evo_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_mxc,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER_D1] = 500000000,
				[VDD_LOWER] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1500000000,
				[VDD_NOMINAL] = 1800000000,
				[VDD_HIGH] = 2020000000},
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
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_pll0_out_even",
		.parent_hws = (const struct clk_hw*[]){
			&cam_cc_pll0.clkr.hw,
		},
		.num_parents = 1,
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
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_pll0_out_odd",
		.parent_hws = (const struct clk_hw*[]){
			&cam_cc_pll0.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_lucid_evo_ops,
	},
};

/* 960MHz configuration */
static struct alpha_pll_config cam_cc_pll2_config = {
	.l = 0x32,
	.alpha = 0x0,
	.config_ctl_val = 0x90008820,
	.config_ctl_hi_val = 0x00890263,
	.config_ctl_hi1_val = 0x00000247,
	.user_ctl_val = 0x00000001,
	.user_ctl_hi_val = 0x00400000,
};

static struct clk_alpha_pll cam_cc_pll2 = {
	.offset = 0x1000,
	.vco_table = rivian_evo_vco,
	.num_vco = ARRAY_SIZE(rivian_evo_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_RIVIAN_EVO],
	.config = &cam_cc_pll2_config,
	.clkr = {
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_pll2",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_rivian_evo_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_mxa,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOW] = 1056000000},
		},
	},
};

/* 960MHz configuration */
static struct alpha_pll_config cam_cc_pll3_config = {
	.l = 0x32,
	.cal_l = 0x44,
	.alpha = 0x0,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x32AA299C,
	.user_ctl_val = 0x00000403,
	.user_ctl_hi_val = 0x00400805,
};

static struct clk_alpha_pll cam_cc_pll3 = {
	.offset = 0x2000,
	.vco_table = lucid_evo_vco,
	.num_vco = ARRAY_SIZE(lucid_evo_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.config = &cam_cc_pll3_config,
	.clkr = {
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_pll3",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_evo_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_mxc,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER_D1] = 500000000,
				[VDD_LOWER] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1500000000,
				[VDD_NOMINAL] = 1800000000,
				[VDD_HIGH] = 2020000000},
		},
	},
};

static const struct clk_div_table post_div_table_cam_cc_pll3_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv cam_cc_pll3_out_even = {
	.offset = 0x2000,
	.post_div_shift = 10,
	.post_div_table = post_div_table_cam_cc_pll3_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_cam_cc_pll3_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_pll3_out_even",
		.parent_hws = (const struct clk_hw*[]){
			&cam_cc_pll3.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_lucid_evo_ops,
	},
};

/* 960MHz configuration */
static struct alpha_pll_config cam_cc_pll4_config = {
	.l = 0x32,
	.cal_l = 0x44,
	.alpha = 0x0,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x32AA299C,
	.user_ctl_val = 0x00000403,
	.user_ctl_hi_val = 0x00400805,
};

static struct clk_alpha_pll cam_cc_pll4 = {
	.offset = 0x3000,
	.vco_table = lucid_evo_vco,
	.num_vco = ARRAY_SIZE(lucid_evo_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.config = &cam_cc_pll4_config,
	.clkr = {
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_pll4",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_evo_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_mxc,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER_D1] = 500000000,
				[VDD_LOWER] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1500000000,
				[VDD_NOMINAL] = 1800000000,
				[VDD_HIGH] = 2020000000},
		},
	},
};

static const struct clk_div_table post_div_table_cam_cc_pll4_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv cam_cc_pll4_out_even = {
	.offset = 0x3000,
	.post_div_shift = 10,
	.post_div_table = post_div_table_cam_cc_pll4_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_cam_cc_pll4_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_pll4_out_even",
		.parent_hws = (const struct clk_hw*[]){
			&cam_cc_pll4.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_lucid_evo_ops,
	},
};

/* 960MHz configuration */
static struct alpha_pll_config cam_cc_pll5_config = {
	.l = 0x32,
	.cal_l = 0x44,
	.alpha = 0x0,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x32AA299C,
	.user_ctl_val = 0x00000403,
	.user_ctl_hi_val = 0x00400805,
};

static struct clk_alpha_pll cam_cc_pll5 = {
	.offset = 0x4000,
	.vco_table = lucid_evo_vco,
	.num_vco = ARRAY_SIZE(lucid_evo_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.config = &cam_cc_pll5_config,
	.clkr = {
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_pll5",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_evo_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_mxc,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER_D1] = 500000000,
				[VDD_LOWER] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1500000000,
				[VDD_NOMINAL] = 1800000000,
				[VDD_HIGH] = 2020000000},
		},
	},
};

static const struct clk_div_table post_div_table_cam_cc_pll5_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv cam_cc_pll5_out_even = {
	.offset = 0x4000,
	.post_div_shift = 10,
	.post_div_table = post_div_table_cam_cc_pll5_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_cam_cc_pll5_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_pll5_out_even",
		.parent_hws = (const struct clk_hw*[]){
			&cam_cc_pll5.clkr.hw,
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
	{ .fw_name = "bi_tcxo" },
	{ .hw = &cam_cc_pll0.clkr.hw },
	{ .hw = &cam_cc_pll0_out_even.clkr.hw },
	{ .hw = &cam_cc_pll0_out_odd.clkr.hw },
};

static const struct parent_map cam_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL2_OUT_EVEN, 3 },
	{ P_CAM_CC_PLL2_OUT_MAIN, 5 },
};

static const struct clk_parent_data cam_cc_parent_data_1[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &cam_cc_pll2.clkr.hw },
	{ .hw = &cam_cc_pll2.clkr.hw },
};

static const struct parent_map cam_cc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL4_OUT_EVEN, 6 },
};

static const struct clk_parent_data cam_cc_parent_data_2[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &cam_cc_pll4_out_even.clkr.hw },
};

static const struct parent_map cam_cc_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL5_OUT_EVEN, 6 },
};

static const struct clk_parent_data cam_cc_parent_data_3[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &cam_cc_pll5_out_even.clkr.hw },
};

static const struct parent_map cam_cc_parent_map_4[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL3_OUT_EVEN, 6 },
};

static const struct clk_parent_data cam_cc_parent_data_4[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &cam_cc_pll3_out_even.clkr.hw },
};

static const struct parent_map cam_cc_parent_map_5[] = {
	{ P_SLEEP_CLK, 0 },
};

static const struct clk_parent_data cam_cc_parent_data_5[] = {
	{ .fw_name = "sleep_clk" },
};

static const struct parent_map cam_cc_parent_map_6[] = {
	{ P_BI_TCXO, 0 },
};

static const struct clk_parent_data cam_cc_parent_data_6_ao[] = {
	{ .fw_name = "bi_tcxo_ao" },
};

static const struct freq_tbl ftbl_cam_cc_camnoc_axi_clk_src[] = {
	F(400000000, P_CAM_CC_PLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_camnoc_axi_clk_src = {
	.cmd_rcgr = 0x13170,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_camnoc_axi_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_camnoc_axi_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = cam_cc_lemans_regulators_1,
		.num_vdd_classes = ARRAY_SIZE(cam_cc_lemans_regulators_1),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 400000000},
	},
};

static const struct freq_tbl ftbl_cam_cc_cci_0_clk_src[] = {
	F(37500000, P_CAM_CC_PLL0_OUT_MAIN, 16, 1, 2),
	{ }
};

static struct clk_rcg2 cam_cc_cci_0_clk_src = {
	.cmd_rcgr = 0x130a0,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_cci_0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_cci_0_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 37500000},
	},
};

static struct clk_rcg2 cam_cc_cci_1_clk_src = {
	.cmd_rcgr = 0x130bc,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_cci_0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_cci_1_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 37500000},
	},
};

static struct clk_rcg2 cam_cc_cci_2_clk_src = {
	.cmd_rcgr = 0x130d8,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_cci_0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_cci_2_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 37500000},
	},
};

static struct clk_rcg2 cam_cc_cci_3_clk_src = {
	.cmd_rcgr = 0x130f4,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_cci_0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_cci_3_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 37500000},
	},
};

static const struct freq_tbl ftbl_cam_cc_cphy_rx_clk_src[] = {
	F(400000000, P_CAM_CC_PLL0_OUT_ODD, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_cphy_rx_clk_src = {
	.cmd_rcgr = 0x11034,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_cphy_rx_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_cphy_rx_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = cam_cc_lemans_regulators_1,
		.num_vdd_classes = ARRAY_SIZE(cam_cc_lemans_regulators_1),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 400000000},
	},
};

static struct clk_rcg2 cam_cc_csi0phytimer_clk_src = {
	.cmd_rcgr = 0x15074,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_cphy_rx_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_csi0phytimer_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mxc,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 400000000},
	},
};

static struct clk_rcg2 cam_cc_csi1phytimer_clk_src = {
	.cmd_rcgr = 0x15098,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_cphy_rx_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_csi1phytimer_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mxc,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 400000000},
	},
};

static struct clk_rcg2 cam_cc_csi2phytimer_clk_src = {
	.cmd_rcgr = 0x150b8,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_cphy_rx_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_csi2phytimer_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mxc,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 400000000},
	},
};

static struct clk_rcg2 cam_cc_csi3phytimer_clk_src = {
	.cmd_rcgr = 0x150d8,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_cphy_rx_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_csi3phytimer_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mxc,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 400000000},
	},
};

static struct clk_rcg2 cam_cc_csid_clk_src = {
	.cmd_rcgr = 0x13150,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_cphy_rx_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_csid_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = cam_cc_lemans_regulators_1,
		.num_vdd_classes = ARRAY_SIZE(cam_cc_lemans_regulators_1),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 400000000},
	},
};

static const struct freq_tbl ftbl_cam_cc_fast_ahb_clk_src[] = {
	F(300000000, P_CAM_CC_PLL0_OUT_MAIN, 4, 0, 0),
	F(400000000, P_CAM_CC_PLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_fast_ahb_clk_src = {
	.cmd_rcgr = 0x13120,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_fast_ahb_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_fast_ahb_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = cam_cc_lemans_regulators_1,
		.num_vdd_classes = ARRAY_SIZE(cam_cc_lemans_regulators_1),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 300000000,
			[VDD_NOMINAL] = 400000000},
	},
};

static const struct freq_tbl ftbl_cam_cc_icp_clk_src[] = {
	F(480000000, P_CAM_CC_PLL0_OUT_MAIN, 2.5, 0, 0),
	F(600000000, P_CAM_CC_PLL0_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_icp_clk_src = {
	.cmd_rcgr = 0x1307c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_icp_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_icp_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = cam_cc_lemans_regulators_1,
		.num_vdd_classes = ARRAY_SIZE(cam_cc_lemans_regulators_1),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 480000000,
			[VDD_NOMINAL] = 600000000},
	},
};

static const struct freq_tbl ftbl_cam_cc_ife_0_clk_src[] = {
	F(480000000, P_CAM_CC_PLL4_OUT_EVEN, 1, 0, 0),
	F(600000000, P_CAM_CC_PLL4_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_ife_0_clk_src = {
	.cmd_rcgr = 0x11004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_2,
	.freq_tbl = ftbl_cam_cc_ife_0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_ife_0_clk_src",
		.parent_data = cam_cc_parent_data_2,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = cam_cc_lemans_regulators_1,
		.num_vdd_classes = ARRAY_SIZE(cam_cc_lemans_regulators_1),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 480000000,
			[VDD_NOMINAL] = 600000000},
	},
};

static const struct freq_tbl ftbl_cam_cc_ife_1_clk_src[] = {
	F(480000000, P_CAM_CC_PLL5_OUT_EVEN, 1, 0, 0),
	F(600000000, P_CAM_CC_PLL5_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_ife_1_clk_src = {
	.cmd_rcgr = 0x12004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_3,
	.freq_tbl = ftbl_cam_cc_ife_1_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_ife_1_clk_src",
		.parent_data = cam_cc_parent_data_3,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = cam_cc_lemans_regulators_1,
		.num_vdd_classes = ARRAY_SIZE(cam_cc_lemans_regulators_1),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 480000000,
			[VDD_NOMINAL] = 600000000},
	},
};

static const struct freq_tbl ftbl_cam_cc_ife_lite_clk_src[] = {
	F(400000000, P_CAM_CC_PLL0_OUT_MAIN, 3, 0, 0),
	F(480000000, P_CAM_CC_PLL0_OUT_MAIN, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_ife_lite_clk_src = {
	.cmd_rcgr = 0x13000,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_ife_lite_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_ife_lite_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = cam_cc_lemans_regulators_1,
		.num_vdd_classes = ARRAY_SIZE(cam_cc_lemans_regulators_1),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 400000000,
			[VDD_NOMINAL] = 480000000},
	},
};

static struct clk_rcg2 cam_cc_ife_lite_csid_clk_src = {
	.cmd_rcgr = 0x13020,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_cphy_rx_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_ife_lite_csid_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = cam_cc_lemans_regulators_1,
		.num_vdd_classes = ARRAY_SIZE(cam_cc_lemans_regulators_1),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 400000000},
	},
};

static const struct freq_tbl ftbl_cam_cc_ipe_clk_src[] = {
	F(480000000, P_CAM_CC_PLL3_OUT_EVEN, 1, 0, 0),
	F(600000000, P_CAM_CC_PLL3_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_ipe_clk_src = {
	.cmd_rcgr = 0x10004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_4,
	.freq_tbl = ftbl_cam_cc_ipe_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_ipe_clk_src",
		.parent_data = cam_cc_parent_data_4,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_4),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = cam_cc_lemans_regulators_1,
		.num_vdd_classes = ARRAY_SIZE(cam_cc_lemans_regulators_1),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 480000000,
			[VDD_NOMINAL] = 600000000},
	},
};

static const struct freq_tbl ftbl_cam_cc_mclk0_clk_src[] = {
	F(19200000, P_CAM_CC_PLL2_OUT_MAIN, 1, 1, 50),
	F(24000000, P_CAM_CC_PLL2_OUT_MAIN, 10, 1, 4),
	F(64000000, P_CAM_CC_PLL2_OUT_MAIN, 15, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_mclk0_clk_src = {
	.cmd_rcgr = 0x15004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_1,
	.freq_tbl = ftbl_cam_cc_mclk0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_mclk0_clk_src",
		.parent_data = cam_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mxa,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 64000000},
	},
};

static struct clk_rcg2 cam_cc_mclk1_clk_src = {
	.cmd_rcgr = 0x15020,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_1,
	.freq_tbl = ftbl_cam_cc_mclk0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_mclk1_clk_src",
		.parent_data = cam_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mxa,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 64000000},
	},
};

static struct clk_rcg2 cam_cc_mclk2_clk_src = {
	.cmd_rcgr = 0x1503c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_1,
	.freq_tbl = ftbl_cam_cc_mclk0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_mclk2_clk_src",
		.parent_data = cam_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mxa,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 64000000},
	},
};

static struct clk_rcg2 cam_cc_mclk3_clk_src = {
	.cmd_rcgr = 0x15058,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_1,
	.freq_tbl = ftbl_cam_cc_mclk0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_mclk3_clk_src",
		.parent_data = cam_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mxa,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 64000000},
	},
};

static const struct freq_tbl ftbl_cam_cc_sleep_clk_src[] = {
	F(32000, P_SLEEP_CLK, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_sleep_clk_src = {
	.cmd_rcgr = 0x131f0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_5,
	.freq_tbl = ftbl_cam_cc_sleep_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_sleep_clk_src",
		.parent_data = cam_cc_parent_data_5,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_5),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 32000},
	},
};

static const struct freq_tbl ftbl_cam_cc_slow_ahb_clk_src[] = {
	F(80000000, P_CAM_CC_PLL0_OUT_EVEN, 7.5, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_slow_ahb_clk_src = {
	.cmd_rcgr = 0x13138,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_slow_ahb_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_slow_ahb_clk_src",
		.parent_data = cam_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = cam_cc_lemans_regulators_1,
		.num_vdd_classes = ARRAY_SIZE(cam_cc_lemans_regulators_1),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 80000000},
	},
};

static const struct freq_tbl ftbl_cam_cc_xo_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_xo_clk_src = {
	.cmd_rcgr = 0x131d4,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_6,
	.freq_tbl = ftbl_cam_cc_xo_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_xo_clk_src",
		.parent_data = cam_cc_parent_data_6_ao,
		.num_parents = ARRAY_SIZE(cam_cc_parent_data_6_ao),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch cam_cc_camnoc_axi_clk = {
	.halt_reg = 0x13188,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13188,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_camnoc_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_camnoc_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_camnoc_dcd_xo_clk = {
	.halt_reg = 0x13190,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13190,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_camnoc_dcd_xo_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_xo_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_camnoc_xo_clk = {
	.halt_reg = 0x13194,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13194,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_camnoc_xo_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_xo_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_cci_0_clk = {
	.halt_reg = 0x130b8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x130b8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_cci_0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_cci_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_cci_1_clk = {
	.halt_reg = 0x130d4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x130d4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_cci_1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_cci_1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_cci_2_clk = {
	.halt_reg = 0x130f0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x130f0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_cci_2_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_cci_2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_cci_3_clk = {
	.halt_reg = 0x1310c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1310c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_cci_3_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_cci_3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_core_ahb_clk = {
	.halt_reg = 0x131d0,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x131d0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_core_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_slow_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_cpas_ahb_clk = {
	.halt_reg = 0x13110,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13110,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_cpas_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_slow_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_cpas_fast_ahb_clk = {
	.halt_reg = 0x13118,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13118,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_cpas_fast_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_cpas_ife_0_clk = {
	.halt_reg = 0x11024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x11024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_cpas_ife_0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_ife_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_cpas_ife_1_clk = {
	.halt_reg = 0x12024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x12024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_cpas_ife_1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_ife_1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_cpas_ife_lite_clk = {
	.halt_reg = 0x1301c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1301c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_cpas_ife_lite_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_ife_lite_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_cpas_ipe_clk = {
	.halt_reg = 0x10024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_cpas_ipe_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_ipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_cpas_sfe_lite_0_clk = {
	.halt_reg = 0x13050,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13050,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_cpas_sfe_lite_0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_ife_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_cpas_sfe_lite_1_clk = {
	.halt_reg = 0x13068,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13068,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_cpas_sfe_lite_1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_ife_1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csi0phytimer_clk = {
	.halt_reg = 0x1508c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1508c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_csi0phytimer_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_csi0phytimer_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csi1phytimer_clk = {
	.halt_reg = 0x150b0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x150b0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_csi1phytimer_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_csi1phytimer_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csi2phytimer_clk = {
	.halt_reg = 0x150d0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x150d0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_csi2phytimer_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_csi2phytimer_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csi3phytimer_clk = {
	.halt_reg = 0x150f0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x150f0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_csi3phytimer_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_csi3phytimer_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csid_clk = {
	.halt_reg = 0x13168,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13168,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_csid_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_csid_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csid_csiphy_rx_clk = {
	.halt_reg = 0x15094,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x15094,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_csid_csiphy_rx_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csiphy0_clk = {
	.halt_reg = 0x15090,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x15090,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_csiphy0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csiphy1_clk = {
	.halt_reg = 0x150b4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x150b4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_csiphy1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csiphy2_clk = {
	.halt_reg = 0x150d4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x150d4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_csiphy2_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csiphy3_clk = {
	.halt_reg = 0x150f4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x150f4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_csiphy3_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_icp_ahb_clk = {
	.halt_reg = 0x1309c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1309c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_icp_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_slow_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_icp_clk = {
	.halt_reg = 0x13094,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13094,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_icp_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_icp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_0_clk = {
	.halt_reg = 0x1101c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1101c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_ife_0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_ife_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_0_fast_ahb_clk = {
	.halt_reg = 0x11030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x11030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_ife_0_fast_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_1_clk = {
	.halt_reg = 0x1201c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1201c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_ife_1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_ife_1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_1_fast_ahb_clk = {
	.halt_reg = 0x12030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x12030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_ife_1_fast_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_lite_ahb_clk = {
	.halt_reg = 0x13044,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13044,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_ife_lite_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_slow_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_lite_clk = {
	.halt_reg = 0x13018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_ife_lite_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_ife_lite_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_lite_cphy_rx_clk = {
	.halt_reg = 0x13040,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13040,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_ife_lite_cphy_rx_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_lite_csid_clk = {
	.halt_reg = 0x13038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13038,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_ife_lite_csid_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_ife_lite_csid_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ipe_ahb_clk = {
	.halt_reg = 0x10030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_ipe_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_slow_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ipe_clk = {
	.halt_reg = 0x1001c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1001c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_ipe_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_ipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ipe_fast_ahb_clk = {
	.halt_reg = 0x10034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_ipe_fast_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_mclk0_clk = {
	.halt_reg = 0x1501c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1501c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_mclk0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_mclk0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_mclk1_clk = {
	.halt_reg = 0x15038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x15038,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_mclk1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_mclk1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_mclk2_clk = {
	.halt_reg = 0x15054,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x15054,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_mclk2_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_mclk2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_mclk3_clk = {
	.halt_reg = 0x15070,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x15070,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_mclk3_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_mclk3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_sfe_lite_0_clk = {
	.halt_reg = 0x1304c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1304c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_sfe_lite_0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_ife_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_sfe_lite_0_fast_ahb_clk = {
	.halt_reg = 0x1305c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1305c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_sfe_lite_0_fast_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_sfe_lite_1_clk = {
	.halt_reg = 0x13064,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13064,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_sfe_lite_1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_ife_1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_sfe_lite_1_fast_ahb_clk = {
	.halt_reg = 0x13074,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13074,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_sfe_lite_1_fast_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_fast_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_sleep_clk = {
	.halt_reg = 0x13208,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x13208,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_sleep_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_sleep_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_titan_top_accu_shift_clk = {
	.halt_reg = 0x131f0,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x131f0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "cam_cc_titan_top_accu_shift_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cam_cc_xo_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap *cam_cc_lemans_clocks[] = {
	[CAM_CC_CAMNOC_AXI_CLK] = &cam_cc_camnoc_axi_clk.clkr,
	[CAM_CC_CAMNOC_AXI_CLK_SRC] = &cam_cc_camnoc_axi_clk_src.clkr,
	[CAM_CC_CAMNOC_DCD_XO_CLK] = &cam_cc_camnoc_dcd_xo_clk.clkr,
	[CAM_CC_CAMNOC_XO_CLK] = &cam_cc_camnoc_xo_clk.clkr,
	[CAM_CC_CCI_0_CLK] = &cam_cc_cci_0_clk.clkr,
	[CAM_CC_CCI_0_CLK_SRC] = &cam_cc_cci_0_clk_src.clkr,
	[CAM_CC_CCI_1_CLK] = &cam_cc_cci_1_clk.clkr,
	[CAM_CC_CCI_1_CLK_SRC] = &cam_cc_cci_1_clk_src.clkr,
	[CAM_CC_CCI_2_CLK] = &cam_cc_cci_2_clk.clkr,
	[CAM_CC_CCI_2_CLK_SRC] = &cam_cc_cci_2_clk_src.clkr,
	[CAM_CC_CCI_3_CLK] = &cam_cc_cci_3_clk.clkr,
	[CAM_CC_CCI_3_CLK_SRC] = &cam_cc_cci_3_clk_src.clkr,
	[CAM_CC_CORE_AHB_CLK] = &cam_cc_core_ahb_clk.clkr,
	[CAM_CC_CPAS_AHB_CLK] = &cam_cc_cpas_ahb_clk.clkr,
	[CAM_CC_CPAS_FAST_AHB_CLK] = &cam_cc_cpas_fast_ahb_clk.clkr,
	[CAM_CC_CPAS_IFE_0_CLK] = &cam_cc_cpas_ife_0_clk.clkr,
	[CAM_CC_CPAS_IFE_1_CLK] = &cam_cc_cpas_ife_1_clk.clkr,
	[CAM_CC_CPAS_IFE_LITE_CLK] = &cam_cc_cpas_ife_lite_clk.clkr,
	[CAM_CC_CPAS_IPE_CLK] = &cam_cc_cpas_ipe_clk.clkr,
	[CAM_CC_CPAS_SFE_LITE_0_CLK] = &cam_cc_cpas_sfe_lite_0_clk.clkr,
	[CAM_CC_CPAS_SFE_LITE_1_CLK] = &cam_cc_cpas_sfe_lite_1_clk.clkr,
	[CAM_CC_CPHY_RX_CLK_SRC] = &cam_cc_cphy_rx_clk_src.clkr,
	[CAM_CC_CSI0PHYTIMER_CLK] = &cam_cc_csi0phytimer_clk.clkr,
	[CAM_CC_CSI0PHYTIMER_CLK_SRC] = &cam_cc_csi0phytimer_clk_src.clkr,
	[CAM_CC_CSI1PHYTIMER_CLK] = &cam_cc_csi1phytimer_clk.clkr,
	[CAM_CC_CSI1PHYTIMER_CLK_SRC] = &cam_cc_csi1phytimer_clk_src.clkr,
	[CAM_CC_CSI2PHYTIMER_CLK] = &cam_cc_csi2phytimer_clk.clkr,
	[CAM_CC_CSI2PHYTIMER_CLK_SRC] = &cam_cc_csi2phytimer_clk_src.clkr,
	[CAM_CC_CSI3PHYTIMER_CLK] = &cam_cc_csi3phytimer_clk.clkr,
	[CAM_CC_CSI3PHYTIMER_CLK_SRC] = &cam_cc_csi3phytimer_clk_src.clkr,
	[CAM_CC_CSID_CLK] = &cam_cc_csid_clk.clkr,
	[CAM_CC_CSID_CLK_SRC] = &cam_cc_csid_clk_src.clkr,
	[CAM_CC_CSID_CSIPHY_RX_CLK] = &cam_cc_csid_csiphy_rx_clk.clkr,
	[CAM_CC_CSIPHY0_CLK] = &cam_cc_csiphy0_clk.clkr,
	[CAM_CC_CSIPHY1_CLK] = &cam_cc_csiphy1_clk.clkr,
	[CAM_CC_CSIPHY2_CLK] = &cam_cc_csiphy2_clk.clkr,
	[CAM_CC_CSIPHY3_CLK] = &cam_cc_csiphy3_clk.clkr,
	[CAM_CC_FAST_AHB_CLK_SRC] = &cam_cc_fast_ahb_clk_src.clkr,
	[CAM_CC_ICP_AHB_CLK] = &cam_cc_icp_ahb_clk.clkr,
	[CAM_CC_ICP_CLK] = &cam_cc_icp_clk.clkr,
	[CAM_CC_ICP_CLK_SRC] = &cam_cc_icp_clk_src.clkr,
	[CAM_CC_IFE_0_CLK] = &cam_cc_ife_0_clk.clkr,
	[CAM_CC_IFE_0_CLK_SRC] = &cam_cc_ife_0_clk_src.clkr,
	[CAM_CC_IFE_0_FAST_AHB_CLK] = &cam_cc_ife_0_fast_ahb_clk.clkr,
	[CAM_CC_IFE_1_CLK] = &cam_cc_ife_1_clk.clkr,
	[CAM_CC_IFE_1_CLK_SRC] = &cam_cc_ife_1_clk_src.clkr,
	[CAM_CC_IFE_1_FAST_AHB_CLK] = &cam_cc_ife_1_fast_ahb_clk.clkr,
	[CAM_CC_IFE_LITE_AHB_CLK] = &cam_cc_ife_lite_ahb_clk.clkr,
	[CAM_CC_IFE_LITE_CLK] = &cam_cc_ife_lite_clk.clkr,
	[CAM_CC_IFE_LITE_CLK_SRC] = &cam_cc_ife_lite_clk_src.clkr,
	[CAM_CC_IFE_LITE_CPHY_RX_CLK] = &cam_cc_ife_lite_cphy_rx_clk.clkr,
	[CAM_CC_IFE_LITE_CSID_CLK] = &cam_cc_ife_lite_csid_clk.clkr,
	[CAM_CC_IFE_LITE_CSID_CLK_SRC] = &cam_cc_ife_lite_csid_clk_src.clkr,
	[CAM_CC_IPE_AHB_CLK] = &cam_cc_ipe_ahb_clk.clkr,
	[CAM_CC_IPE_CLK] = &cam_cc_ipe_clk.clkr,
	[CAM_CC_IPE_CLK_SRC] = &cam_cc_ipe_clk_src.clkr,
	[CAM_CC_IPE_FAST_AHB_CLK] = &cam_cc_ipe_fast_ahb_clk.clkr,
	[CAM_CC_MCLK0_CLK] = &cam_cc_mclk0_clk.clkr,
	[CAM_CC_MCLK0_CLK_SRC] = &cam_cc_mclk0_clk_src.clkr,
	[CAM_CC_MCLK1_CLK] = &cam_cc_mclk1_clk.clkr,
	[CAM_CC_MCLK1_CLK_SRC] = &cam_cc_mclk1_clk_src.clkr,
	[CAM_CC_MCLK2_CLK] = &cam_cc_mclk2_clk.clkr,
	[CAM_CC_MCLK2_CLK_SRC] = &cam_cc_mclk2_clk_src.clkr,
	[CAM_CC_MCLK3_CLK] = &cam_cc_mclk3_clk.clkr,
	[CAM_CC_MCLK3_CLK_SRC] = &cam_cc_mclk3_clk_src.clkr,
	[CAM_CC_PLL0] = &cam_cc_pll0.clkr,
	[CAM_CC_PLL0_OUT_EVEN] = &cam_cc_pll0_out_even.clkr,
	[CAM_CC_PLL0_OUT_ODD] = &cam_cc_pll0_out_odd.clkr,
	[CAM_CC_PLL2] = &cam_cc_pll2.clkr,
	[CAM_CC_PLL3] = &cam_cc_pll3.clkr,
	[CAM_CC_PLL3_OUT_EVEN] = &cam_cc_pll3_out_even.clkr,
	[CAM_CC_PLL4] = &cam_cc_pll4.clkr,
	[CAM_CC_PLL4_OUT_EVEN] = &cam_cc_pll4_out_even.clkr,
	[CAM_CC_PLL5] = &cam_cc_pll5.clkr,
	[CAM_CC_PLL5_OUT_EVEN] = &cam_cc_pll5_out_even.clkr,
	[CAM_CC_SFE_LITE_0_CLK] = &cam_cc_sfe_lite_0_clk.clkr,
	[CAM_CC_SFE_LITE_0_FAST_AHB_CLK] = &cam_cc_sfe_lite_0_fast_ahb_clk.clkr,
	[CAM_CC_SFE_LITE_1_CLK] = &cam_cc_sfe_lite_1_clk.clkr,
	[CAM_CC_SFE_LITE_1_FAST_AHB_CLK] = &cam_cc_sfe_lite_1_fast_ahb_clk.clkr,
	[CAM_CC_SLEEP_CLK] = &cam_cc_sleep_clk.clkr,
	[CAM_CC_SLEEP_CLK_SRC] = &cam_cc_sleep_clk_src.clkr,
	[CAM_CC_SLOW_AHB_CLK_SRC] = &cam_cc_slow_ahb_clk_src.clkr,
	[CAM_CC_XO_CLK_SRC] = &cam_cc_xo_clk_src.clkr,
	[CAM_CC_TITAN_TOP_ACCU_SHIFT_CLK] = NULL,
};

static const struct qcom_reset_map cam_cc_lemans_resets[] = {
	[CAM_CC_ICP_BCR] = { 0x13078 },
	[CAM_CC_IFE_0_BCR] = { 0x11000 },
	[CAM_CC_IFE_1_BCR] = { 0x12000 },
	[CAM_CC_IPE_0_BCR] = { 0x10000 },
	[CAM_CC_SFE_LITE_0_BCR] = { 0x13048 },
	[CAM_CC_SFE_LITE_1_BCR] = { 0x13060 },
};

static const struct regmap_config cam_cc_lemans_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x16218,
	.fast_io = true,
};

static struct qcom_cc_desc cam_cc_lemans_desc = {
	.config = &cam_cc_lemans_regmap_config,
	.clks = cam_cc_lemans_clocks,
	.num_clks = ARRAY_SIZE(cam_cc_lemans_clocks),
	.resets = cam_cc_lemans_resets,
	.num_resets = ARRAY_SIZE(cam_cc_lemans_resets),
	.clk_regulators = cam_cc_lemans_regulators,
	.num_clk_regulators = ARRAY_SIZE(cam_cc_lemans_regulators),
};

static const struct of_device_id cam_cc_lemans_match_table[] = {
	{ .compatible = "qcom,lemans-camcc" },
	{ .compatible = "qcom,monaco_auto-camcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, cam_cc_lemans_match_table);

static int cam_cc_lemans_fixup(struct platform_device *pdev, struct regmap *regmap)
{
	u32 offset = 0x131ec;

	if (of_device_is_compatible(pdev->dev.of_node, "qcom,monaco_auto-camcc")) {
		cam_cc_camnoc_axi_clk_src.cmd_rcgr = 0x13154;
		cam_cc_camnoc_axi_clk.halt_reg = 0x1316c;
		cam_cc_camnoc_axi_clk.clkr.enable_reg = 0x1316c;
		cam_cc_camnoc_dcd_xo_clk.halt_reg = 0x13174;
		cam_cc_camnoc_dcd_xo_clk.clkr.enable_reg = 0x13174;
		cam_cc_camnoc_xo_clk.halt_reg = 0x13178;
		cam_cc_camnoc_xo_clk.clkr.enable_reg = 0x13178;

		cam_cc_csi0phytimer_clk_src.cmd_rcgr = 0x15054;
		cam_cc_csi1phytimer_clk_src.cmd_rcgr = 0x15078;
		cam_cc_csi2phytimer_clk_src.cmd_rcgr = 0x15098;
		cam_cc_csid_clk_src.cmd_rcgr = 0x13134;

		cam_cc_mclk0_clk_src.cmd_rcgr = 0x15000;
		cam_cc_mclk1_clk_src.cmd_rcgr = 0x1501c;
		cam_cc_mclk2_clk_src.cmd_rcgr = 0x15038;

		cam_cc_fast_ahb_clk_src.cmd_rcgr = 0x13104;
		cam_cc_slow_ahb_clk_src.cmd_rcgr = 0x1311c;
		cam_cc_xo_clk_src.cmd_rcgr = 0x131b8;
		cam_cc_sleep_clk_src.cmd_rcgr = 0x131d4;

		cam_cc_core_ahb_clk.halt_reg = 0x131b4;
		cam_cc_core_ahb_clk.clkr.enable_reg = 0x131b4;

		cam_cc_cpas_ahb_clk.halt_reg = 0x130f4;
		cam_cc_cpas_ahb_clk.clkr.enable_reg = 0x130f4;
		cam_cc_cpas_fast_ahb_clk.halt_reg = 0x130fc;
		cam_cc_cpas_fast_ahb_clk.clkr.enable_reg = 0x130fc;

		cam_cc_csi0phytimer_clk.halt_reg = 0x1506c;
		cam_cc_csi0phytimer_clk.clkr.enable_reg = 0x1506c;
		cam_cc_csi1phytimer_clk.halt_reg = 0x15090;
		cam_cc_csi1phytimer_clk.clkr.enable_reg = 0x15090;
		cam_cc_csi2phytimer_clk.halt_reg = 0x150b0;
		cam_cc_csi2phytimer_clk.clkr.enable_reg = 0x150b0;
		cam_cc_csid_clk.halt_reg = 0x1314c;
		cam_cc_csid_clk.clkr.enable_reg = 0x1314c;
		cam_cc_csid_csiphy_rx_clk.halt_reg = 0x15074;
		cam_cc_csid_csiphy_rx_clk.clkr.enable_reg = 0x15074;
		cam_cc_csiphy0_clk.halt_reg = 0x15070;
		cam_cc_csiphy0_clk.clkr.enable_reg = 0x15070;
		cam_cc_csiphy1_clk.halt_reg = 0x15094;
		cam_cc_csiphy1_clk.clkr.enable_reg = 0x15094;
		cam_cc_csiphy2_clk.halt_reg = 0x150b4;
		cam_cc_csiphy2_clk.clkr.enable_reg = 0x150b4;

		cam_cc_mclk0_clk.halt_reg = 0x15018;
		cam_cc_mclk0_clk.clkr.enable_reg = 0x15018;
		cam_cc_mclk1_clk.halt_reg = 0x15034;
		cam_cc_mclk1_clk.clkr.enable_reg = 0x15034;
		cam_cc_mclk2_clk.halt_reg = 0x15050;
		cam_cc_mclk2_clk.clkr.enable_reg = 0x15050;

		cam_cc_lemans_clocks[CAM_CC_CCI_3_CLK] = NULL;
		cam_cc_lemans_clocks[CAM_CC_CCI_3_CLK_SRC] = NULL;
		cam_cc_lemans_clocks[CAM_CC_CSI3PHYTIMER_CLK] = NULL;
		cam_cc_lemans_clocks[CAM_CC_CSI3PHYTIMER_CLK_SRC] = NULL;
		cam_cc_lemans_clocks[CAM_CC_CSIPHY3_CLK] = NULL;
		cam_cc_lemans_clocks[CAM_CC_MCLK3_CLK] = NULL;
		cam_cc_lemans_clocks[CAM_CC_MCLK3_CLK_SRC] = NULL;
		cam_cc_lemans_clocks[CAM_CC_TITAN_TOP_ACCU_SHIFT_CLK] =
				&cam_cc_titan_top_accu_shift_clk.clkr;

		offset = 0x131d0;
	}

	/*
	 * Keep clocks always enabled:
	 * cam_cc_gdsc_clk
	 */
	regmap_update_bits(regmap, offset, BIT(0), BIT(0));

	return 0;
}

static int cam_cc_lemans_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret;

	regmap = qcom_cc_map(pdev, &cam_cc_lemans_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ret = qcom_cc_runtime_init(pdev, &cam_cc_lemans_desc);
	if (ret)
		return ret;

	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret)
		return ret;

	clk_lucid_evo_pll_configure(&cam_cc_pll0, regmap, cam_cc_pll0.config);
	clk_rivian_evo_pll_configure(&cam_cc_pll2, regmap, cam_cc_pll2.config);
	clk_lucid_evo_pll_configure(&cam_cc_pll3, regmap, cam_cc_pll3.config);
	clk_lucid_evo_pll_configure(&cam_cc_pll4, regmap, cam_cc_pll4.config);
	clk_lucid_evo_pll_configure(&cam_cc_pll5, regmap, cam_cc_pll5.config);

	ret = cam_cc_lemans_fixup(pdev, regmap);
	if (ret)
		return ret;

	ret = qcom_cc_really_probe(pdev, &cam_cc_lemans_desc, regmap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register CAM CC clocks\n");
		return ret;
	}

	pm_runtime_put_sync(&pdev->dev);
	dev_info(&pdev->dev, "Registered CAM CC clocks\n");

	return ret;
}

static void cam_cc_lemans_sync_state(struct device *dev)
{
	qcom_cc_sync_state(dev, &cam_cc_lemans_desc);
}

static const struct dev_pm_ops cam_cc_lemans_pm_ops = {
	SET_RUNTIME_PM_OPS(qcom_cc_runtime_suspend, qcom_cc_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static struct platform_driver cam_cc_lemans_driver = {
	.probe = cam_cc_lemans_probe,
	.driver = {
		.name = "cam_cc-lemans",
		.of_match_table = cam_cc_lemans_match_table,
		.sync_state = cam_cc_lemans_sync_state,
		.pm = &cam_cc_lemans_pm_ops,
	},
};

static int __init cam_cc_lemans_init(void)
{
	return platform_driver_register(&cam_cc_lemans_driver);
}
subsys_initcall(cam_cc_lemans_init);

static void __exit cam_cc_lemans_exit(void)
{
	platform_driver_unregister(&cam_cc_lemans_driver);
}
module_exit(cam_cc_lemans_exit);

MODULE_DESCRIPTION("QTI CAM_CC LEMANS Driver");
MODULE_LICENSE("GPL");
