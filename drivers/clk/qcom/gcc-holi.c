// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,gcc-holi.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "common.h"
#include "clk-regmap-mux.h"
#include "clk-regmap-divider.h"
#include "reset.h"
#include "vdd-level-holi.h"

static DEFINE_VDD_REGULATORS(vdd_cx, VDD_HIGH_L1 + 1, 1, vdd_corner);
static DEFINE_VDD_REGULATORS(vdd_mx, VDD_HIGH_L1 + 1, 1, vdd_corner);

static struct clk_vdd_class *gcc_holi_regulators[] = {
	&vdd_cx,
	&vdd_mx,
};

enum {
	P_BI_TCXO,
	P_GPLL0_OUT_EVEN,
	P_GPLL0_OUT_ODD,
	P_GPLL0_OUT_MAIN,
	P_GPLL10_OUT_EVEN,
	P_GPLL11_OUT_EVEN,
	P_GPLL11_OUT_ODD,
	P_GPLL3_OUT_EVEN,
	P_GPLL3_OUT_MAIN,
	P_GPLL4_OUT_EVEN,
	P_GPLL5_OUT_EVEN,
	P_GPLL6_OUT_EVEN,
	P_GPLL6_OUT_MAIN,
	P_GPLL7_OUT_EVEN,
	P_GPLL8_OUT_EVEN,
	P_GPLL8_OUT_MAIN,
	P_GPLL9_OUT_EARLY,
	P_GPLL9_OUT_MAIN,
	P_SLEEP_CLK,
};

static struct pll_vco agera_vco[] = {
	{ 600000000, 3300000000, 0 },
	{ 600000000, 2000000000, 1 },
};

static struct pll_vco fabia_vco[] = {
	{ 249600000, 2000000000, 0 },
	{ 125000000, 1000000000, 1 },
};

static struct clk_alpha_pll gpll0 = {
	.offset = 0x0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gpll0",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_fabia_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_MIN] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1600000000,
				[VDD_NOMINAL] = 2000000000},
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
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gpll0_out_even",
		.parent_data = &(const struct clk_parent_data){
			.hw = &gpll0.clkr.hw,
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
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gpll0_out_odd",
		.parent_data = &(const struct clk_parent_data){
			.hw = &gpll0.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_fabia_ops,
	},
};

/* 1152MHz Configuration */
static const struct alpha_pll_config gpll10_config = {
	.l = 0x3C,
	.cal_l = 0x36,
	.alpha = 0x0,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002067,
	.test_ctl_val = 0x40000000,
	.test_ctl_hi_val = 0x00000002,
	.user_ctl_val = 0x00000001,
	.user_ctl_hi_val = 0x00004805,
};

static struct clk_alpha_pll gpll10 = {
	.offset = 0xa000,
	.vco_table = fabia_vco,
	.num_vco = ARRAY_SIZE(fabia_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.flags = SUPPORTS_FSM_LEGACY_MODE,
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data){
			.name = "gpll10",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_fabia_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_mx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_MIN] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1600000000,
				[VDD_NOMINAL] = 2000000000},
		},
	},
};

/* 532MHz Configuration */
static const struct alpha_pll_config gpll11_config = {
	.l = 0x1B,
	.cal_l = 0x25,
	.alpha = 0xB555,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002067,
	.test_ctl_val = 0x40000000,
	.test_ctl_hi_val = 0x00000002,
	.user_ctl_val = 0x00000001,
	.user_ctl_hi_val = 0x00004805,
};

static struct clk_alpha_pll gpll11 = {
	.offset = 0xb000,
	.vco_table = fabia_vco,
	.num_vco = ARRAY_SIZE(fabia_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.flags = SUPPORTS_FSM_LEGACY_MODE,
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(11),
		.hw.init = &(const struct clk_init_data){
			.name = "gpll11",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fabia_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_MIN] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1600000000,
				[VDD_NOMINAL] = 2000000000},
		},
	},
};

static struct clk_alpha_pll gpll3 = {
	.offset = 0x3000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(3),
		.hw.init = &(const struct clk_init_data){
			.name = "gpll3",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_fabia_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_MIN] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1600000000,
				[VDD_NOMINAL] = 2000000000},
		},
	},
};

static const struct clk_div_table post_div_table_gpll3_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll3_out_even = {
	.offset = 0x3000,
	.post_div_shift = 8,
	.post_div_table = post_div_table_gpll3_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_gpll3_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gpll3_out_even",
		.parent_data = &(const struct clk_parent_data){
			.hw = &gpll3.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_fabia_ops,
	},
};

static struct clk_alpha_pll gpll4 = {
	.offset = 0x4000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data){
			.name = "gpll4",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_fabia_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_MIN] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1600000000,
				[VDD_NOMINAL] = 2000000000},
		},
	},
};

static struct clk_alpha_pll gpll5 = {
	.offset = 0x5000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data){
			.name = "gpll5",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_fabia_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_MIN] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1600000000,
				[VDD_NOMINAL] = 2000000000},
		},
	},
};

static struct clk_alpha_pll gpll6 = {
	.offset = 0x6000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(6),
		.hw.init = &(const struct clk_init_data){
			.name = "gpll6",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_fabia_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_MIN] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1600000000,
				[VDD_NOMINAL] = 2000000000},
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
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gpll6_out_even",
		.parent_data = &(const struct clk_parent_data){
			.hw = &gpll6.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_fabia_ops,
	},
};

static struct clk_alpha_pll gpll7 = {
	.offset = 0x7000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data){
			.name = "gpll7",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_fabia_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_MIN] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1600000000,
				[VDD_NOMINAL] = 2000000000},
		},
	},
};

/* 420MHz Configuration */
static const struct alpha_pll_config gpll8_config = {
	.l = 0x15,
	.cal_l = 0x2A,
	.alpha = 0xE000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002067,
	.test_ctl_val = 0x40000000,
	.test_ctl_hi_val = 0x00000002,
	.user_ctl_val = 0x00000103,
	.user_ctl_hi_val = 0x00004805,
};

static struct clk_alpha_pll gpll8 = {
	.offset = 0x8000,
	.vco_table = fabia_vco,
	.num_vco = ARRAY_SIZE(fabia_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.flags = SUPPORTS_FSM_LEGACY_MODE,
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data){
			.name = "gpll8",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fabia_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_MIN] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1600000000,
				[VDD_NOMINAL] = 2000000000},
		},
	},
};

static const struct clk_div_table post_div_table_gpll8_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll8_out_even = {
	.offset = 0x8000,
	.post_div_shift = 8,
	.post_div_table = post_div_table_gpll8_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_gpll8_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gpll8_out_even",
		.parent_data = &(const struct clk_parent_data){
			.hw = &gpll8.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_fabia_ops,
	},
};

/* 480MHz Configuration */
static const struct alpha_pll_config gpll9_config = {
	.l = 0x64,
	.alpha = 0x0,
	.config_ctl_hi_val = 0x400003D2,
	.config_ctl_val = 0x20000800,
	.test_ctl_val = 0x4000400,
	.test_ctl_hi_val = 0x4000,
	.user_ctl_val = 0x0000030B,
};

static struct clk_alpha_pll gpll9 = {
	.offset = 0x9000,
	.vco_table = agera_vco,
	.num_vco = ARRAY_SIZE(agera_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_AGERA],
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(9),
		.hw.init = &(const struct clk_init_data){
			.name = "gpll9",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_agera_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_mx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_MIN] = 1200000000,
				[VDD_LOWER] = 1800000000,
				[VDD_LOW] = 2400000000,
				[VDD_NOMINAL] = 3000000000,
				[VDD_HIGH] = 3300000000},
		},
	},
};

static struct clk_fixed_factor gpll9_out_early = {
	.mult = 1,
	.div = 2,
	.hw.init = &(const struct clk_init_data){
		.name = "gpll9_out_early",
		.parent_data = &(const struct clk_parent_data){
			.hw = &gpll9.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct clk_alpha_pll_postdiv gpll9_out_main = {
	.offset = 0x9000,
	.post_div_shift = 8,
	.width = 2,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_AGERA],
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gpll9_out_main",
		.parent_data = &(const struct clk_parent_data){
			.hw = &gpll9.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_ops,
	},
};

static const struct parent_map gcc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL0_OUT_EVEN, 2 },
};

static const struct clk_parent_data gcc_parent_data_0[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL0_OUT_EVEN, 2 },
	{ P_GPLL6_OUT_EVEN, 4 },
};

static const struct clk_parent_data gcc_parent_data_1[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_even.clkr.hw },
	{ .hw = &gpll6_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL0_OUT_EVEN, 2 },
	{ P_GPLL0_OUT_ODD, 4 },
};

static const struct clk_parent_data gcc_parent_data_2[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_even.clkr.hw },
	{ .hw = &gpll0_out_odd.clkr.hw },
};

static const struct clk_parent_data gcc_parent_data_2_ao[] = {
	{ .fw_name = "bi_tcxo_ao", },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_even.clkr.hw },
	{ .hw = &gpll0_out_odd.clkr.hw },
};

static const struct parent_map gcc_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL9_OUT_EARLY, 2 },
	{ P_GPLL10_OUT_EVEN, 3 },
	{ P_GPLL9_OUT_MAIN, 4 },
	{ P_GPLL3_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_3[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll9_out_early.hw },
	{ .hw = &gpll10.clkr.hw },
	{ .hw = &gpll9_out_main.clkr.hw },
	{ .hw = &gpll3_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_4[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL0_OUT_EVEN, 2 },
	{ P_GPLL0_OUT_ODD, 4 },
	{ P_GPLL4_OUT_EVEN, 5 },
	{ P_GPLL3_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_4[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_even.clkr.hw },
	{ .hw = &gpll0_out_odd.clkr.hw },
	{ .hw = &gpll4.clkr.hw },
	{ .hw = &gpll3_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_5[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL8_OUT_MAIN, 2 },
	{ P_GPLL10_OUT_EVEN, 3 },
	{ P_GPLL9_OUT_MAIN, 4 },
	{ P_GPLL8_OUT_EVEN, 5 },
	{ P_GPLL3_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_5[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll8.clkr.hw },
	{ .hw = &gpll10.clkr.hw },
	{ .hw = &gpll9_out_main.clkr.hw },
	{ .hw = &gpll8_out_even.clkr.hw },
	{ .hw = &gpll3_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_6[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL8_OUT_MAIN, 2 },
	{ P_GPLL5_OUT_EVEN, 3 },
	{ P_GPLL9_OUT_MAIN, 4 },
	{ P_GPLL8_OUT_EVEN, 5 },
	{ P_GPLL3_OUT_MAIN, 6 },
};

static const struct clk_parent_data gcc_parent_data_6[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll8.clkr.hw },
	{ .hw = &gpll5.clkr.hw },
	{ .hw = &gpll9_out_main.clkr.hw },
	{ .hw = &gpll8_out_even.clkr.hw },
	{ .hw = &gpll3.clkr.hw },
};

static const struct parent_map gcc_parent_map_7[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL0_OUT_EVEN, 2 },
	{ P_GPLL0_OUT_ODD, 4 },
	{ P_SLEEP_CLK, 5 },
};

static const struct clk_parent_data gcc_parent_data_7[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_even.clkr.hw },
	{ .hw = &gpll0_out_odd.clkr.hw },
	{ .fw_name = "sleep_clk" },
};

static const struct parent_map gcc_parent_map_8[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL0_OUT_EVEN, 2 },
	{ P_GPLL10_OUT_EVEN, 3 },
	{ P_GPLL4_OUT_EVEN, 5 },
	{ P_GPLL3_OUT_MAIN, 6 },
};

static const struct clk_parent_data gcc_parent_data_8[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_even.clkr.hw },
	{ .hw = &gpll10.clkr.hw },
	{ .hw = &gpll4.clkr.hw },
	{ .hw = &gpll3.clkr.hw },
};

static const struct parent_map gcc_parent_map_9[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL0_OUT_EVEN, 2 },
	{ P_GPLL10_OUT_EVEN, 3 },
	{ P_GPLL9_OUT_MAIN, 4 },
	{ P_GPLL8_OUT_EVEN, 5 },
	{ P_GPLL3_OUT_MAIN, 6 },
};

static const struct clk_parent_data gcc_parent_data_9[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_even.clkr.hw },
	{ .hw = &gpll10.clkr.hw },
	{ .hw = &gpll9_out_main.clkr.hw },
	{ .hw = &gpll8_out_even.clkr.hw },
	{ .hw = &gpll3.clkr.hw },
};

static const struct parent_map gcc_parent_map_10[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL8_OUT_MAIN, 2 },
	{ P_GPLL10_OUT_EVEN, 3 },
	{ P_GPLL9_OUT_MAIN, 4 },
	{ P_GPLL8_OUT_EVEN, 5 },
	{ P_GPLL3_OUT_MAIN, 6 },
};

static const struct clk_parent_data gcc_parent_data_10[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll8.clkr.hw },
	{ .hw = &gpll10.clkr.hw },
	{ .hw = &gpll9_out_main.clkr.hw },
	{ .hw = &gpll8_out_even.clkr.hw },
	{ .hw = &gpll3.clkr.hw },
};

static const struct parent_map gcc_parent_map_11[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL8_OUT_MAIN, 2 },
	{ P_GPLL10_OUT_EVEN, 3 },
	{ P_GPLL6_OUT_MAIN, 4 },
	{ P_GPLL3_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_11[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll8.clkr.hw },
	{ .hw = &gpll10.clkr.hw },
	{ .hw = &gpll6.clkr.hw },
	{ .hw = &gpll3_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_12[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL0_OUT_EVEN, 2 },
	{ P_GPLL7_OUT_EVEN, 3 },
	{ P_GPLL4_OUT_EVEN, 5 },
};

static const struct clk_parent_data gcc_parent_data_12[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_even.clkr.hw },
	{ .hw = &gpll7.clkr.hw },
	{ .hw = &gpll4.clkr.hw },
};

static const struct parent_map gcc_parent_map_13[] = {
	{ P_BI_TCXO, 0 },
	{ P_SLEEP_CLK, 5 },
};

static const struct clk_parent_data gcc_parent_data_13[] = {
	{ .fw_name = "bi_tcxo" },
	{ .fw_name = "sleep_clk" },
};

static const struct parent_map gcc_parent_map_14[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL11_OUT_ODD, 2 },
	{ P_GPLL11_OUT_EVEN, 3 },
};

static const struct clk_parent_data gcc_parent_data_14[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll11.clkr.hw },
	{ .hw = &gpll11.clkr.hw },
};

static const struct parent_map gcc_parent_map_15[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
};

static const struct clk_parent_data gcc_parent_data_15[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
};

static const struct parent_map gcc_parent_map_16[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL6_OUT_EVEN, 4 },
};

static const struct clk_parent_data gcc_parent_data_16[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll6_out_even.clkr.hw },
};

static const struct freq_tbl ftbl_gcc_camss_axi_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(150000000, P_GPLL0_OUT_EVEN, 2, 0, 0),
	F(240000000, P_GPLL0_OUT_MAIN, 2.5, 0, 0),
	F(300000000, P_GPLL0_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_axi_clk_src = {
	.cmd_rcgr = 0x5802c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_8,
	.freq_tbl = ftbl_gcc_camss_axi_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_camss_axi_clk_src",
		.parent_data = gcc_parent_data_8,
		.num_parents = ARRAY_SIZE(gcc_parent_data_8),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 150000000,
			[VDD_LOW_L1] = 240000000,
			[VDD_NOMINAL] = 300000000},
	},
};

static const struct freq_tbl ftbl_gcc_camss_cci_0_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(37500000, P_GPLL0_OUT_EVEN, 8, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_cci_0_clk_src = {
	.cmd_rcgr = 0x56000,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_9,
	.freq_tbl = ftbl_gcc_camss_cci_0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_camss_cci_0_clk_src",
		.parent_data = gcc_parent_data_9,
		.num_parents = ARRAY_SIZE(gcc_parent_data_9),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 37500000},
	},
};

static struct clk_rcg2 gcc_camss_cci_1_clk_src = {
	.cmd_rcgr = 0x5c000,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_9,
	.freq_tbl = ftbl_gcc_camss_cci_0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_camss_cci_1_clk_src",
		.parent_data = gcc_parent_data_9,
		.num_parents = ARRAY_SIZE(gcc_parent_data_9),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 37500000},
	},
};

static const struct freq_tbl ftbl_gcc_camss_csi0phytimer_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(100000000, P_GPLL0_OUT_ODD, 2, 0, 0),
	F(200000000, P_GPLL0_OUT_ODD, 1, 0, 0),
	F(268800000, P_GPLL4_OUT_EVEN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_csi0phytimer_clk_src = {
	.cmd_rcgr = 0x59000,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_camss_csi0phytimer_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_camss_csi0phytimer_clk_src",
		.parent_data = gcc_parent_data_4,
		.num_parents = ARRAY_SIZE(gcc_parent_data_4),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 200000000,
			[VDD_NOMINAL] = 268800000},
	},
};

static struct clk_rcg2 gcc_camss_csi1phytimer_clk_src = {
	.cmd_rcgr = 0x5901c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_camss_csi0phytimer_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_camss_csi1phytimer_clk_src",
		.parent_data = gcc_parent_data_4,
		.num_parents = ARRAY_SIZE(gcc_parent_data_4),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 200000000,
			[VDD_NOMINAL] = 268800000},
	},
};

static struct clk_rcg2 gcc_camss_csi2phytimer_clk_src = {
	.cmd_rcgr = 0x59038,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_camss_csi0phytimer_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_camss_csi2phytimer_clk_src",
		.parent_data = gcc_parent_data_4,
		.num_parents = ARRAY_SIZE(gcc_parent_data_4),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 200000000,
			[VDD_NOMINAL] = 268800000},
	},
};

static struct clk_rcg2 gcc_camss_csi3phytimer_clk_src = {
	.cmd_rcgr = 0x59054,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_camss_csi0phytimer_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_camss_csi3phytimer_clk_src",
		.parent_data = gcc_parent_data_4,
		.num_parents = ARRAY_SIZE(gcc_parent_data_4),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 200000000,
			[VDD_NOMINAL] = 268800000},
	},
};

static const struct freq_tbl ftbl_gcc_camss_mclk0_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(24000000, P_GPLL9_OUT_MAIN, 1, 1, 20),
	F(64000000, P_GPLL9_OUT_EARLY, 15, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_mclk0_clk_src = {
	.cmd_rcgr = 0x51000,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_camss_mclk0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_camss_mclk0_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 64000000},
	},
};

static struct clk_rcg2 gcc_camss_mclk1_clk_src = {
	.cmd_rcgr = 0x5101c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_camss_mclk0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_camss_mclk1_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 64000000},
	},
};

static struct clk_rcg2 gcc_camss_mclk2_clk_src = {
	.cmd_rcgr = 0x51038,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_camss_mclk0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_camss_mclk2_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 64000000},
	},
};

static struct clk_rcg2 gcc_camss_mclk3_clk_src = {
	.cmd_rcgr = 0x51054,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_camss_mclk0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_camss_mclk3_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 64000000},
	},
};

static struct clk_rcg2 gcc_camss_mclk4_clk_src = {
	.cmd_rcgr = 0x51070,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_camss_mclk0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_camss_mclk4_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 64000000},
	},
};

static const struct freq_tbl ftbl_gcc_camss_ope_ahb_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(171428571, P_GPLL0_OUT_MAIN, 3.5, 0, 0),
	F(240000000, P_GPLL0_OUT_MAIN, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_ope_ahb_clk_src = {
	.cmd_rcgr = 0x55024,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_10,
	.freq_tbl = ftbl_gcc_camss_ope_ahb_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_camss_ope_ahb_clk_src",
		.parent_data = gcc_parent_data_10,
		.num_parents = ARRAY_SIZE(gcc_parent_data_10),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 171428571,
			[VDD_NOMINAL] = 240000000},
	},
};

static const struct freq_tbl ftbl_gcc_camss_ope_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(200000000, P_GPLL8_OUT_EVEN, 2, 0, 0),
	F(266600000, P_GPLL8_OUT_EVEN, 1, 0, 0),
	F(480000000, P_GPLL8_OUT_EVEN, 1, 0, 0),
	F(580000000, P_GPLL8_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_ope_clk_src = {
	.cmd_rcgr = 0x55004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_10,
	.freq_tbl = ftbl_gcc_camss_ope_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_camss_ope_clk_src",
		.parent_data = gcc_parent_data_10,
		.num_parents = ARRAY_SIZE(gcc_parent_data_10),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 200000000,
			[VDD_LOW_L1] = 266600000,
			[VDD_NOMINAL] = 480000000,
			[VDD_HIGH] = 580000000},
	},
};

static const struct freq_tbl ftbl_gcc_camss_tfe_0_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(120000000, P_GPLL0_OUT_MAIN, 5, 0, 0),
	F(133333333, P_GPLL0_OUT_MAIN, 4.5, 0, 0),
	F(137142857, P_GPLL9_OUT_MAIN, 3.5, 0, 0),
	F(150000000, P_GPLL0_OUT_MAIN, 4, 0, 0),
	F(160000000, P_GPLL9_OUT_MAIN, 3, 0, 0),
	F(171428571, P_GPLL0_OUT_MAIN, 3.5, 0, 0),
	F(192000000, P_GPLL9_OUT_MAIN, 2.5, 0, 0),
	F(200000000, P_GPLL0_OUT_MAIN, 3, 0, 0),
	F(240000000, P_GPLL0_OUT_MAIN, 2.5, 0, 0),
	F(300000000, P_GPLL0_OUT_MAIN, 2, 0, 0),
	F(329142857, P_GPLL10_OUT_EVEN, 3.5, 0, 0),
	F(384000000, P_GPLL10_OUT_EVEN, 3, 0, 0),
	F(460800000, P_GPLL10_OUT_EVEN, 2.5, 0, 0),
	F(576000000, P_GPLL10_OUT_EVEN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_tfe_0_clk_src = {
	.cmd_rcgr = 0x52004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_camss_tfe_0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_camss_tfe_0_clk_src",
		.parent_data = gcc_parent_data_5,
		.num_parents = ARRAY_SIZE(gcc_parent_data_5),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 300000000,
			[VDD_LOW_L1] = 460800000,
			[VDD_NOMINAL] = 576000000},
	},
};

static const struct freq_tbl ftbl_gcc_camss_tfe_0_csid_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(120000000, P_GPLL0_OUT_MAIN, 5, 0, 0),
	F(300000000, P_GPLL0_OUT_MAIN, 2, 0, 0),
	F(426400000, P_GPLL3_OUT_MAIN, 2.5, 0, 0),
	F(466500000, P_GPLL5_OUT_EVEN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_tfe_0_csid_clk_src = {
	.cmd_rcgr = 0x52094,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_6,
	.freq_tbl = ftbl_gcc_camss_tfe_0_csid_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_camss_tfe_0_csid_clk_src",
		.parent_data = gcc_parent_data_6,
		.num_parents = ARRAY_SIZE(gcc_parent_data_6),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 300000000,
			[VDD_LOW_L1] = 426400000,
			[VDD_NOMINAL] = 466500000},
	},
};

static struct clk_rcg2 gcc_camss_tfe_1_clk_src = {
	.cmd_rcgr = 0x52024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_camss_tfe_0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_camss_tfe_1_clk_src",
		.parent_data = gcc_parent_data_5,
		.num_parents = ARRAY_SIZE(gcc_parent_data_5),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 300000000,
			[VDD_LOW_L1] = 460800000,
			[VDD_NOMINAL] = 576000000},
	},
};

static struct clk_rcg2 gcc_camss_tfe_1_csid_clk_src = {
	.cmd_rcgr = 0x520b4,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_6,
	.freq_tbl = ftbl_gcc_camss_tfe_0_csid_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_camss_tfe_1_csid_clk_src",
		.parent_data = gcc_parent_data_6,
		.num_parents = ARRAY_SIZE(gcc_parent_data_6),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 300000000,
			[VDD_LOW_L1] = 426400000,
			[VDD_NOMINAL] = 466500000},
	},
};

static struct clk_rcg2 gcc_camss_tfe_2_clk_src = {
	.cmd_rcgr = 0x52044,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_camss_tfe_0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_camss_tfe_2_clk_src",
		.parent_data = gcc_parent_data_5,
		.num_parents = ARRAY_SIZE(gcc_parent_data_5),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 300000000,
			[VDD_LOW_L1] = 460800000,
			[VDD_NOMINAL] = 576000000},
	},
};

static struct clk_rcg2 gcc_camss_tfe_2_csid_clk_src = {
	.cmd_rcgr = 0x520d4,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_6,
	.freq_tbl = ftbl_gcc_camss_tfe_0_csid_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_camss_tfe_2_csid_clk_src",
		.parent_data = gcc_parent_data_6,
		.num_parents = ARRAY_SIZE(gcc_parent_data_6),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 300000000,
			[VDD_LOW_L1] = 426400000,
			[VDD_NOMINAL] = 466500000},
	},
};

static const struct freq_tbl ftbl_gcc_camss_tfe_cphy_rx_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(240000000, P_GPLL0_OUT_MAIN, 2.5, 0, 0),
	F(341333333, P_GPLL6_OUT_MAIN, 1, 4, 9),
	F(384000000, P_GPLL6_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_tfe_cphy_rx_clk_src = {
	.cmd_rcgr = 0x52064,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_11,
	.freq_tbl = ftbl_gcc_camss_tfe_cphy_rx_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_camss_tfe_cphy_rx_clk_src",
		.parent_data = gcc_parent_data_11,
		.num_parents = ARRAY_SIZE(gcc_parent_data_11),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 240000000,
			[VDD_LOW_L1] = 341333333,
			[VDD_HIGH] = 384000000},
	},
};

static const struct freq_tbl ftbl_gcc_camss_top_ahb_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(40000000, P_GPLL0_OUT_EVEN, 7.5, 0, 0),
	F(80000000, P_GPLL0_OUT_MAIN, 7.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_top_ahb_clk_src = {
	.cmd_rcgr = 0x58010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_8,
	.freq_tbl = ftbl_gcc_camss_top_ahb_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_camss_top_ahb_clk_src",
		.parent_data = gcc_parent_data_8,
		.num_parents = ARRAY_SIZE(gcc_parent_data_8),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 80000000},
	},
};

static const struct freq_tbl ftbl_gcc_cpuss_ahb_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(50000000, P_GPLL0_OUT_ODD, 4, 0, 0),
	F(100000000, P_GPLL0_OUT_ODD, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_cpuss_ahb_clk_src = {
	.cmd_rcgr = 0x2b13c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_cpuss_ahb_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_cpuss_ahb_clk_src",
		.parent_data = gcc_parent_data_2_ao,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2_ao),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_gp1_clk_src[] = {
	F(25000000, P_GPLL0_OUT_EVEN, 12, 0, 0),
	F(50000000, P_GPLL0_OUT_ODD, 4, 0, 0),
	F(100000000, P_GPLL0_OUT_ODD, 2, 0, 0),
	F(200000000, P_GPLL0_OUT_ODD, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_gp1_clk_src = {
	.cmd_rcgr = 0x4d004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_7,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_gp1_clk_src",
		.parent_data = gcc_parent_data_7,
		.num_parents = ARRAY_SIZE(gcc_parent_data_7),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 50000000,
			[VDD_LOW] = 100000000,
			[VDD_NOMINAL] = 200000000},
	},
};

static struct clk_rcg2 gcc_gp2_clk_src = {
	.cmd_rcgr = 0x4e004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_7,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_gp2_clk_src",
		.parent_data = gcc_parent_data_7,
		.num_parents = ARRAY_SIZE(gcc_parent_data_7),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 50000000,
			[VDD_LOW] = 100000000,
			[VDD_NOMINAL] = 200000000},
	},
};

static struct clk_rcg2 gcc_gp3_clk_src = {
	.cmd_rcgr = 0x4f004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_7,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_gp3_clk_src",
		.parent_data = gcc_parent_data_7,
		.num_parents = ARRAY_SIZE(gcc_parent_data_7),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 50000000,
			[VDD_LOW] = 100000000,
			[VDD_NOMINAL] = 200000000},
	},
};

static const struct freq_tbl ftbl_gcc_pdm2_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(60000000, P_GPLL0_OUT_EVEN, 5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pdm2_clk_src = {
	.cmd_rcgr = 0x20010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pdm2_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_pdm2_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 60000000},
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
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s0_clk_src = {
	.cmd_rcgr = 0x1f148,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &gcc_qupv3_wrap0_s0_clk_src_init,
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 100000000,
			[VDD_NOMINAL] = 128000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap0_s1_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s1_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s1_clk_src = {
	.cmd_rcgr = 0x1f278,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &gcc_qupv3_wrap0_s1_clk_src_init,
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 100000000,
			[VDD_NOMINAL] = 128000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap0_s2_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s2_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s2_clk_src = {
	.cmd_rcgr = 0x1f3a8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &gcc_qupv3_wrap0_s2_clk_src_init,
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 100000000,
			[VDD_NOMINAL] = 128000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap0_s3_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s3_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s3_clk_src = {
	.cmd_rcgr = 0x1f4d8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &gcc_qupv3_wrap0_s3_clk_src_init,
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 100000000,
			[VDD_NOMINAL] = 128000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap0_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s4_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s4_clk_src = {
	.cmd_rcgr = 0x1f608,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &gcc_qupv3_wrap0_s4_clk_src_init,
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 100000000,
			[VDD_NOMINAL] = 128000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap0_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s5_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s5_clk_src = {
	.cmd_rcgr = 0x1f738,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &gcc_qupv3_wrap0_s5_clk_src_init,
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 100000000,
			[VDD_NOMINAL] = 128000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap1_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s0_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s0_clk_src = {
	.cmd_rcgr = 0x5301c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &gcc_qupv3_wrap1_s0_clk_src_init,
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 100000000,
			[VDD_NOMINAL] = 128000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap1_s1_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s1_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s1_clk_src = {
	.cmd_rcgr = 0x5314c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &gcc_qupv3_wrap1_s1_clk_src_init,
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 100000000,
			[VDD_NOMINAL] = 128000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap1_s2_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s2_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s2_clk_src = {
	.cmd_rcgr = 0x5327c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &gcc_qupv3_wrap1_s2_clk_src_init,
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 100000000,
			[VDD_NOMINAL] = 128000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap1_s3_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s3_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s3_clk_src = {
	.cmd_rcgr = 0x533ac,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &gcc_qupv3_wrap1_s3_clk_src_init,
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 100000000,
			[VDD_NOMINAL] = 128000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap1_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s4_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s4_clk_src = {
	.cmd_rcgr = 0x534dc,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &gcc_qupv3_wrap1_s4_clk_src_init,
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 100000000,
			[VDD_NOMINAL] = 128000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap1_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s5_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s5_clk_src = {
	.cmd_rcgr = 0x5360c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &gcc_qupv3_wrap1_s5_clk_src_init,
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 100000000,
			[VDD_NOMINAL] = 128000000},
	},
};

static const struct freq_tbl ftbl_gcc_sdcc1_apps_clk_src[] = {
	F(144000, P_BI_TCXO, 16, 3, 25),
	F(400000, P_BI_TCXO, 12, 1, 4),
	F(20000000, P_GPLL0_OUT_EVEN, 5, 1, 3),
	F(25000000, P_GPLL0_OUT_EVEN, 6, 1, 2),
	F(50000000, P_GPLL0_OUT_EVEN, 6, 0, 0),
	F(100000000, P_GPLL0_OUT_EVEN, 3, 0, 0),
	F(192000000, P_GPLL6_OUT_EVEN, 2, 0, 0),
	F(384000000, P_GPLL6_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc1_apps_clk_src = {
	.cmd_rcgr = 0x38028,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_sdcc1_apps_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_sdcc1_apps_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_floor_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 100000000,
			[VDD_LOW_L1] = 384000000},
	},
};

static const struct freq_tbl ftbl_gcc_sdcc1_ice_core_clk_src[] = {
	F(75000000, P_GPLL0_OUT_EVEN, 4, 0, 0),
	F(100000000, P_GPLL0_OUT_EVEN, 3, 0, 0),
	F(150000000, P_GPLL0_OUT_EVEN, 2, 0, 0),
	F(200000000, P_GPLL0_OUT_MAIN, 3, 0, 0),
	F(300000000, P_GPLL0_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc1_ice_core_clk_src = {
	.cmd_rcgr = 0x38010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_sdcc1_ice_core_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_sdcc1_ice_core_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_floor_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 100000000,
			[VDD_LOW] = 150000000,
			[VDD_LOW_L1] = 300000000},
	},
};

static const struct freq_tbl ftbl_gcc_sdcc2_apps_clk_src[] = {
	F(400000, P_BI_TCXO, 12, 1, 4),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(25000000, P_GPLL0_OUT_EVEN, 12, 0, 0),
	F(50000000, P_GPLL0_OUT_EVEN, 6, 0, 0),
	F(100000000, P_GPLL0_OUT_EVEN, 3, 0, 0),
	F(202000000, P_GPLL7_OUT_EVEN, 4, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc2_apps_clk_src = {
	.cmd_rcgr = 0x1e00c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_12,
	.freq_tbl = ftbl_gcc_sdcc2_apps_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_sdcc2_apps_clk_src",
		.parent_data = gcc_parent_data_12,
		.num_parents = ARRAY_SIZE(gcc_parent_data_12),
		.ops = &clk_rcg2_floor_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 100000000,
			[VDD_LOW_L1] = 202000000},
	},
};

static const struct freq_tbl ftbl_gcc_ufs_phy_axi_clk_src[] = {
	F(25000000, P_GPLL0_OUT_EVEN, 12, 0, 0),
	F(50000000, P_GPLL0_OUT_ODD, 4, 0, 0),
	F(100000000, P_GPLL0_OUT_ODD, 2, 0, 0),
	F(200000000, P_GPLL0_OUT_ODD, 1, 0, 0),
	F(240000000, P_GPLL0_OUT_MAIN, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_phy_axi_clk_src = {
	.cmd_rcgr = 0x45020,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_ufs_phy_axi_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_ufs_phy_axi_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 50000000,
			[VDD_LOW] = 100000000,
			[VDD_NOMINAL] = 200000000,
			[VDD_HIGH] = 240000000},
	},
};

static const struct freq_tbl ftbl_gcc_ufs_phy_ice_core_clk_src[] = {
	F(75000000, P_GPLL0_OUT_EVEN, 4, 0, 0),
	F(150000000, P_GPLL0_OUT_EVEN, 2, 0, 0),
	F(300000000, P_GPLL0_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_phy_ice_core_clk_src = {
	.cmd_rcgr = 0x45048,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_phy_ice_core_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_ufs_phy_ice_core_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 150000000,
			[VDD_NOMINAL] = 300000000},
	},
};

static const struct freq_tbl ftbl_gcc_ufs_phy_phy_aux_clk_src[] = {
	F(9600000, P_BI_TCXO, 2, 0, 0),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_phy_phy_aux_clk_src = {
	.cmd_rcgr = 0x4507c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_phy_phy_aux_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_ufs_phy_phy_aux_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000},
	},
};

static const struct freq_tbl ftbl_gcc_ufs_phy_unipro_core_clk_src[] = {
	F(37500000, P_GPLL0_OUT_EVEN, 8, 0, 0),
	F(75000000, P_GPLL0_OUT_EVEN, 4, 0, 0),
	F(150000000, P_GPLL0_OUT_EVEN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_phy_unipro_core_clk_src = {
	.cmd_rcgr = 0x45060,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_phy_unipro_core_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_ufs_phy_unipro_core_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 37500000,
			[VDD_LOW] = 75000000,
			[VDD_NOMINAL] = 150000000},
	},
};

static const struct freq_tbl ftbl_gcc_usb30_prim_master_clk_src[] = {
	F(66666667, P_GPLL0_OUT_EVEN, 4.5, 0, 0),
	F(133333333, P_GPLL0_OUT_MAIN, 4.5, 0, 0),
	F(200000000, P_GPLL0_OUT_ODD, 1, 0, 0),
	F(240000000, P_GPLL0_OUT_MAIN, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb30_prim_master_clk_src = {
	.cmd_rcgr = 0x1a01c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_usb30_prim_master_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_usb30_prim_master_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 66666667,
			[VDD_LOW] = 133333333,
			[VDD_NOMINAL] = 200000000,
			[VDD_HIGH] = 240000000},
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
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb30_prim_mock_utmi_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_usb30_prim_mock_utmi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000},
	},
};

static struct clk_rcg2 gcc_usb3_prim_phy_aux_clk_src = {
	.cmd_rcgr = 0x1a060,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_13,
	.freq_tbl = ftbl_gcc_usb30_prim_mock_utmi_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_usb3_prim_phy_aux_clk_src",
		.parent_data = gcc_parent_data_13,
		.num_parents = ARRAY_SIZE(gcc_parent_data_13),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000},
	},
};

static const struct freq_tbl ftbl_gcc_video_venus_clk_src[] = {
	F(133000000, P_GPLL11_OUT_EVEN, 4, 0, 0),
	F(240000000, P_GPLL11_OUT_EVEN, 2.5, 0, 0),
	F(300000000, P_GPLL11_OUT_EVEN, 2, 0, 0),
	F(384000000, P_GPLL11_OUT_EVEN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_video_venus_clk_src = {
	.cmd_rcgr = 0x58060,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_14,
	.freq_tbl = ftbl_gcc_video_venus_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gcc_video_venus_clk_src",
		.parent_data = gcc_parent_data_14,
		.num_parents = ARRAY_SIZE(gcc_parent_data_14),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 133000000,
			[VDD_LOW] = 240000000,
			[VDD_LOW_L1] = 300000000,
			[VDD_NOMINAL] = 384000000},
	},
};

static struct clk_regmap_div gcc_cpuss_ahb_postdiv_clk_src = {
	.reg = 0x2b154,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_cpuss_ahb_postdiv_clk_src",
		.parent_data = &(const struct clk_parent_data){
			.hw = &gcc_cpuss_ahb_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_usb30_prim_mock_utmi_postdiv_clk_src = {
	.reg = 0x1a04c,
	.shift = 0,
	.width = 2,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_prim_mock_utmi_postdiv_clk_src",
		.parent_data = &(const struct clk_parent_data){
			.hw = &gcc_usb30_prim_mock_utmi_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch gcc_ahb2phy_csi_clk = {
	.halt_reg = 0x1d004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x1d004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1d004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ahb2phy_csi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ahb2phy_usb_clk = {
	.halt_reg = 0x1d008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x1d008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1d008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ahb2phy_usb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_bimc_gpu_axi_clk = {
	.halt_reg = 0x71154,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x71154,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x71154,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_bimc_gpu_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_boot_rom_ahb_clk = {
	.halt_reg = 0x23004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x23004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x79004,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_boot_rom_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cam_throttle_nrt_clk = {
	.halt_reg = 0x17070,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x17070,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x79004,
		.enable_mask = BIT(27),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_cam_throttle_nrt_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cam_throttle_rt_clk = {
	.halt_reg = 0x1706c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x1706c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x79004,
		.enable_mask = BIT(26),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_cam_throttle_rt_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_axi_clk = {
	.halt_reg = 0x58044,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x58044,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_axi_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_camnoc_atb_clk = {
	.halt_reg = 0x5804c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x5804c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x5804c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_camnoc_atb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_camnoc_nts_xo_clk = {
	.halt_reg = 0x58050,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x58050,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x58050,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_camnoc_nts_xo_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_cci_0_clk = {
	.halt_reg = 0x56018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x56018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_cci_0_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_cci_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_cci_1_clk = {
	.halt_reg = 0x5c018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5c018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_cci_1_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_cci_1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_cphy_0_clk = {
	.halt_reg = 0x52088,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x52088,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_cphy_0_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_tfe_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_cphy_1_clk = {
	.halt_reg = 0x5208c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5208c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_cphy_1_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_tfe_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_cphy_2_clk = {
	.halt_reg = 0x52090,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x52090,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_cphy_2_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_tfe_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_cphy_3_clk = {
	.halt_reg = 0x520f8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x520f8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_cphy_3_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_tfe_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi0phytimer_clk = {
	.halt_reg = 0x59018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x59018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_csi0phytimer_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_csi0phytimer_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi1phytimer_clk = {
	.halt_reg = 0x59034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x59034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_csi1phytimer_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_csi1phytimer_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi2phytimer_clk = {
	.halt_reg = 0x59050,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x59050,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_csi2phytimer_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_csi2phytimer_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi3phytimer_clk = {
	.halt_reg = 0x5906c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5906c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_csi3phytimer_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_csi3phytimer_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_mclk0_clk = {
	.halt_reg = 0x51018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x51018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_mclk0_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_mclk0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_mclk1_clk = {
	.halt_reg = 0x51034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x51034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_mclk1_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_mclk1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_mclk2_clk = {
	.halt_reg = 0x51050,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x51050,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_mclk2_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_mclk2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_mclk3_clk = {
	.halt_reg = 0x5106c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5106c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_mclk3_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_mclk3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_mclk4_clk = {
	.halt_reg = 0x51088,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x51088,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_mclk4_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_mclk4_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_nrt_axi_clk = {
	.halt_reg = 0x58054,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x58054,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_nrt_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_ope_ahb_clk = {
	.halt_reg = 0x5503c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5503c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_ope_ahb_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_ope_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_ope_clk = {
	.halt_reg = 0x5501c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5501c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_ope_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_ope_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_rt_axi_clk = {
	.halt_reg = 0x5805c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5805c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_rt_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_tfe_0_clk = {
	.halt_reg = 0x5201c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5201c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_tfe_0_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_tfe_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_tfe_0_cphy_rx_clk = {
	.halt_reg = 0x5207c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5207c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_tfe_0_cphy_rx_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_tfe_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_tfe_0_csid_clk = {
	.halt_reg = 0x520ac,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x520ac,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_tfe_0_csid_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_tfe_0_csid_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_tfe_1_clk = {
	.halt_reg = 0x5203c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5203c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_tfe_1_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_tfe_1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_tfe_1_cphy_rx_clk = {
	.halt_reg = 0x52080,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x52080,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_tfe_1_cphy_rx_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_tfe_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_tfe_1_csid_clk = {
	.halt_reg = 0x520cc,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x520cc,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_tfe_1_csid_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_tfe_1_csid_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_tfe_2_clk = {
	.halt_reg = 0x5205c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5205c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_tfe_2_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_tfe_2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_tfe_2_cphy_rx_clk = {
	.halt_reg = 0x52084,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x52084,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_tfe_2_cphy_rx_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_tfe_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_tfe_2_csid_clk = {
	.halt_reg = 0x520ec,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x520ec,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_tfe_2_csid_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_tfe_2_csid_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_top_ahb_clk = {
	.halt_reg = 0x58028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x58028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_camss_top_ahb_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_camss_top_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_usb3_prim_axi_clk = {
	.halt_reg = 0x1a084,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x1a084,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1a084,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_cfg_noc_usb3_prim_axi_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_usb30_prim_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap_div gcc_disp_gpll0_clk_src = {
	.reg = 0x17058,
	.shift = 0,
	.width = 2,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_disp_gpll0_clk_src",
		.parent_names =
			(const char *[]){ "gpll0" },
		.num_parents = 1,
		.ops = &clk_regmap_div_ops,
	},
};

static struct clk_branch gcc_disp_gpll0_div_clk_src = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x79004,
		.enable_mask = BIT(20),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_disp_gpll0_div_clk_src",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_disp_gpll0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_hf_axi_clk = {
	.halt_reg = 0x17020,
	.halt_check = BRANCH_VOTED,
	.hwcg_reg = 0x17020,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x17020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_disp_hf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_sleep_clk = {
	.halt_reg = 0x17074,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x17074,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x17074,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_disp_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_throttle_core_clk = {
	.halt_reg = 0x17064,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x17064,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_disp_throttle_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp1_clk = {
	.halt_reg = 0x4d000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4d000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_gp1_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_gp1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp2_clk = {
	.halt_reg = 0x4e000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4e000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_gp2_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_gp2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp3_clk = {
	.halt_reg = 0x4f000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4f000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_gp3_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_gp3_clk_src.clkr.hw,
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
		.enable_reg = 0x79004,
		.enable_mask = BIT(15),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_gpu_gpll0_clk_src",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gpll0.clkr.hw,
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
		.enable_reg = 0x79004,
		.enable_mask = BIT(16),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_gpu_gpll0_div_clk_src",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gpll0_out_even.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_memnoc_gfx_clk = {
	.halt_reg = 0x3600c,
	.halt_check = BRANCH_VOTED,
	.hwcg_reg = 0x3600c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3600c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_gpu_memnoc_gfx_clk",
			.flags = CLK_DONT_HOLD_STATE,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_snoc_dvm_gfx_clk = {
	.halt_reg = 0x36018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x36018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_gpu_snoc_dvm_gfx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_throttle_core_clk = {
	.halt_reg = 0x36048,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x36048,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x79004,
		.enable_mask = BIT(31),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_gpu_throttle_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm2_clk = {
	.halt_reg = 0x2000c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2000c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pdm2_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_pdm2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm_ahb_clk = {
	.halt_reg = 0x20004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x20004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x20004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pdm_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm_xo4_clk = {
	.halt_reg = 0x20008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x20008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_pdm_xo4_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_prng_ahb_clk = {
	.halt_reg = 0x21004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x21004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x79004,
		.enable_mask = BIT(13),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_prng_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_camera_nrt_ahb_clk = {
	.halt_reg = 0x17014,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x17014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qmip_camera_nrt_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_camera_rt_ahb_clk = {
	.halt_reg = 0x17060,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x17060,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qmip_camera_rt_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_disp_ahb_clk = {
	.halt_reg = 0x17018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x17018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qmip_disp_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_gpu_cfg_ahb_clk = {
	.halt_reg = 0x36040,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x36040,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qmip_gpu_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_vcodec_ahb_clk = {
	.halt_reg = 0x17010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x17010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x79004,
		.enable_mask = BIT(25),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qmip_video_vcodec_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_core_2x_clk = {
	.halt_reg = 0x1f014,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(9),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap0_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_core_clk = {
	.halt_reg = 0x1f00c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap0_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s0_clk = {
	.halt_reg = 0x1f144,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s0_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_qupv3_wrap0_s0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s1_clk = {
	.halt_reg = 0x1f274,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(11),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s1_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_qupv3_wrap0_s1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s2_clk = {
	.halt_reg = 0x1f3a4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(12),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s2_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_qupv3_wrap0_s2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s3_clk = {
	.halt_reg = 0x1f4d4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(13),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s3_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_qupv3_wrap0_s3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s4_clk = {
	.halt_reg = 0x1f604,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(14),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s4_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_qupv3_wrap0_s4_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s5_clk = {
	.halt_reg = 0x1f734,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(15),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s5_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_qupv3_wrap0_s5_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_core_2x_clk = {
	.halt_reg = 0x53014,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(20),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap1_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_core_clk = {
	.halt_reg = 0x5300c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(19),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap1_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s0_clk = {
	.halt_reg = 0x53018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(21),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap1_s0_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_qupv3_wrap1_s0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s1_clk = {
	.halt_reg = 0x53148,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(22),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap1_s1_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_qupv3_wrap1_s1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s2_clk = {
	.halt_reg = 0x53278,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(23),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap1_s2_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_qupv3_wrap1_s2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s3_clk = {
	.halt_reg = 0x533a8,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(24),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap1_s3_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_qupv3_wrap1_s3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s4_clk = {
	.halt_reg = 0x534d8,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(25),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap1_s4_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_qupv3_wrap1_s4_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s5_clk = {
	.halt_reg = 0x53608,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(26),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap1_s5_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_qupv3_wrap1_s5_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_0_m_ahb_clk = {
	.halt_reg = 0x1f004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x1f004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(6),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap_0_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_0_s_ahb_clk = {
	.halt_reg = 0x1f008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x1f008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap_0_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_1_m_ahb_clk = {
	.halt_reg = 0x53004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x53004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(17),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap_1_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_1_s_ahb_clk = {
	.halt_reg = 0x53008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x53008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(18),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_qupv3_wrap_1_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_ahb_clk = {
	.halt_reg = 0x38008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x38008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_sdcc1_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_apps_clk = {
	.halt_reg = 0x38004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x38004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_sdcc1_apps_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_sdcc1_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_ice_core_clk = {
	.halt_reg = 0x3800c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x3800c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3800c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_sdcc1_ice_core_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_sdcc1_ice_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc2_ahb_clk = {
	.halt_reg = 0x1e008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1e008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_sdcc2_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc2_apps_clk = {
	.halt_reg = 0x1e004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1e004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_sdcc2_apps_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_sdcc2_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sys_noc_ufs_phy_axi_clk = {
	.halt_reg = 0x45098,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x45098,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_sys_noc_ufs_phy_axi_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_ufs_phy_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sys_noc_usb3_prim_axi_clk = {
	.halt_reg = 0x1a080,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x1a080,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1a080,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_sys_noc_usb3_prim_axi_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_usb30_prim_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_ahb_clk = {
	.halt_reg = 0x45014,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x45014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x45014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ufs_phy_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_axi_clk = {
	.halt_reg = 0x45010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x45010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x45010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ufs_phy_axi_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_ufs_phy_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_ice_core_clk = {
	.halt_reg = 0x45044,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x45044,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x45044,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ufs_phy_ice_core_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_ufs_phy_ice_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_phy_aux_clk = {
	.halt_reg = 0x45078,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x45078,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x45078,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ufs_phy_phy_aux_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_ufs_phy_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_rx_symbol_0_clk = {
	.halt_reg = 0x4501c,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x4501c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ufs_phy_rx_symbol_0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_tx_symbol_0_clk = {
	.halt_reg = 0x45018,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x45018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ufs_phy_tx_symbol_0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_unipro_core_clk = {
	.halt_reg = 0x45040,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x45040,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x45040,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ufs_phy_unipro_core_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_ufs_phy_unipro_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_prim_master_clk = {
	.halt_reg = 0x1a010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1a010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_usb30_prim_master_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_usb30_prim_master_clk_src.clkr.hw,
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
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_usb30_prim_mock_utmi_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw =
			&gcc_usb30_prim_mock_utmi_postdiv_clk_src.clkr.hw,
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
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_usb30_prim_sleep_clk",
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
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_ufs_mem_clkref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_rx5_pcie_clkref_en_clk = {
	.halt_reg = 0x8c00c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c00c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_rx5_pcie_clkref_en_clk",
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
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_usb3_prim_clkref_clk",
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
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_usb3_prim_phy_com_aux_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_usb3_prim_phy_aux_clk_src.clkr.hw,
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
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_usb3_prim_phy_pipe_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_vcodec0_axi_clk = {
	.halt_reg = 0x6e008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6e008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_vcodec0_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_venus_ahb_clk = {
	.halt_reg = 0x6e010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6e010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_venus_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_venus_ctl_axi_clk = {
	.halt_reg = 0x6e004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6e004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_venus_ctl_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_axi0_clk = {
	.halt_reg = 0x1701c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x1701c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1701c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_video_axi0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_throttle_core_clk = {
	.halt_reg = 0x17068,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x17068,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x79004,
		.enable_mask = BIT(28),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_video_throttle_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_vcodec0_sys_clk = {
	.halt_reg = 0x580a4,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x580a4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x580a4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_video_vcodec0_sys_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_video_venus_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_venus_ctl_clk = {
	.halt_reg = 0x5808c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5808c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_video_venus_ctl_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_video_venus_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_xo_clk = {
	.halt_reg = 0x17024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gcc_video_xo_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap *gcc_holi_clocks[] = {
	[GCC_AHB2PHY_CSI_CLK] = &gcc_ahb2phy_csi_clk.clkr,
	[GCC_AHB2PHY_USB_CLK] = &gcc_ahb2phy_usb_clk.clkr,
	[GCC_BIMC_GPU_AXI_CLK] = &gcc_bimc_gpu_axi_clk.clkr,
	[GCC_BOOT_ROM_AHB_CLK] = &gcc_boot_rom_ahb_clk.clkr,
	[GCC_CAM_THROTTLE_NRT_CLK] = &gcc_cam_throttle_nrt_clk.clkr,
	[GCC_CAM_THROTTLE_RT_CLK] = &gcc_cam_throttle_rt_clk.clkr,
	[GCC_CAMSS_AXI_CLK] = &gcc_camss_axi_clk.clkr,
	[GCC_CAMSS_AXI_CLK_SRC] = &gcc_camss_axi_clk_src.clkr,
	[GCC_CAMSS_CAMNOC_ATB_CLK] = &gcc_camss_camnoc_atb_clk.clkr,
	[GCC_CAMSS_CAMNOC_NTS_XO_CLK] = &gcc_camss_camnoc_nts_xo_clk.clkr,
	[GCC_CAMSS_CCI_0_CLK] = &gcc_camss_cci_0_clk.clkr,
	[GCC_CAMSS_CCI_0_CLK_SRC] = &gcc_camss_cci_0_clk_src.clkr,
	[GCC_CAMSS_CCI_1_CLK] = &gcc_camss_cci_1_clk.clkr,
	[GCC_CAMSS_CCI_1_CLK_SRC] = &gcc_camss_cci_1_clk_src.clkr,
	[GCC_CAMSS_CPHY_0_CLK] = &gcc_camss_cphy_0_clk.clkr,
	[GCC_CAMSS_CPHY_1_CLK] = &gcc_camss_cphy_1_clk.clkr,
	[GCC_CAMSS_CPHY_2_CLK] = &gcc_camss_cphy_2_clk.clkr,
	[GCC_CAMSS_CPHY_3_CLK] = &gcc_camss_cphy_3_clk.clkr,
	[GCC_CAMSS_CSI0PHYTIMER_CLK] = &gcc_camss_csi0phytimer_clk.clkr,
	[GCC_CAMSS_CSI0PHYTIMER_CLK_SRC] = &gcc_camss_csi0phytimer_clk_src.clkr,
	[GCC_CAMSS_CSI1PHYTIMER_CLK] = &gcc_camss_csi1phytimer_clk.clkr,
	[GCC_CAMSS_CSI1PHYTIMER_CLK_SRC] = &gcc_camss_csi1phytimer_clk_src.clkr,
	[GCC_CAMSS_CSI2PHYTIMER_CLK] = &gcc_camss_csi2phytimer_clk.clkr,
	[GCC_CAMSS_CSI2PHYTIMER_CLK_SRC] = &gcc_camss_csi2phytimer_clk_src.clkr,
	[GCC_CAMSS_CSI3PHYTIMER_CLK] = &gcc_camss_csi3phytimer_clk.clkr,
	[GCC_CAMSS_CSI3PHYTIMER_CLK_SRC] = &gcc_camss_csi3phytimer_clk_src.clkr,
	[GCC_CAMSS_MCLK0_CLK] = &gcc_camss_mclk0_clk.clkr,
	[GCC_CAMSS_MCLK0_CLK_SRC] = &gcc_camss_mclk0_clk_src.clkr,
	[GCC_CAMSS_MCLK1_CLK] = &gcc_camss_mclk1_clk.clkr,
	[GCC_CAMSS_MCLK1_CLK_SRC] = &gcc_camss_mclk1_clk_src.clkr,
	[GCC_CAMSS_MCLK2_CLK] = &gcc_camss_mclk2_clk.clkr,
	[GCC_CAMSS_MCLK2_CLK_SRC] = &gcc_camss_mclk2_clk_src.clkr,
	[GCC_CAMSS_MCLK3_CLK] = &gcc_camss_mclk3_clk.clkr,
	[GCC_CAMSS_MCLK3_CLK_SRC] = &gcc_camss_mclk3_clk_src.clkr,
	[GCC_CAMSS_MCLK4_CLK] = &gcc_camss_mclk4_clk.clkr,
	[GCC_CAMSS_MCLK4_CLK_SRC] = &gcc_camss_mclk4_clk_src.clkr,
	[GCC_CAMSS_NRT_AXI_CLK] = &gcc_camss_nrt_axi_clk.clkr,
	[GCC_CAMSS_OPE_AHB_CLK] = &gcc_camss_ope_ahb_clk.clkr,
	[GCC_CAMSS_OPE_AHB_CLK_SRC] = &gcc_camss_ope_ahb_clk_src.clkr,
	[GCC_CAMSS_OPE_CLK] = &gcc_camss_ope_clk.clkr,
	[GCC_CAMSS_OPE_CLK_SRC] = &gcc_camss_ope_clk_src.clkr,
	[GCC_CAMSS_RT_AXI_CLK] = &gcc_camss_rt_axi_clk.clkr,
	[GCC_CAMSS_TFE_0_CLK] = &gcc_camss_tfe_0_clk.clkr,
	[GCC_CAMSS_TFE_0_CLK_SRC] = &gcc_camss_tfe_0_clk_src.clkr,
	[GCC_CAMSS_TFE_0_CPHY_RX_CLK] = &gcc_camss_tfe_0_cphy_rx_clk.clkr,
	[GCC_CAMSS_TFE_0_CSID_CLK] = &gcc_camss_tfe_0_csid_clk.clkr,
	[GCC_CAMSS_TFE_0_CSID_CLK_SRC] = &gcc_camss_tfe_0_csid_clk_src.clkr,
	[GCC_CAMSS_TFE_1_CLK] = &gcc_camss_tfe_1_clk.clkr,
	[GCC_CAMSS_TFE_1_CLK_SRC] = &gcc_camss_tfe_1_clk_src.clkr,
	[GCC_CAMSS_TFE_1_CPHY_RX_CLK] = &gcc_camss_tfe_1_cphy_rx_clk.clkr,
	[GCC_CAMSS_TFE_1_CSID_CLK] = &gcc_camss_tfe_1_csid_clk.clkr,
	[GCC_CAMSS_TFE_1_CSID_CLK_SRC] = &gcc_camss_tfe_1_csid_clk_src.clkr,
	[GCC_CAMSS_TFE_2_CLK] = &gcc_camss_tfe_2_clk.clkr,
	[GCC_CAMSS_TFE_2_CLK_SRC] = &gcc_camss_tfe_2_clk_src.clkr,
	[GCC_CAMSS_TFE_2_CPHY_RX_CLK] = &gcc_camss_tfe_2_cphy_rx_clk.clkr,
	[GCC_CAMSS_TFE_2_CSID_CLK] = &gcc_camss_tfe_2_csid_clk.clkr,
	[GCC_CAMSS_TFE_2_CSID_CLK_SRC] = &gcc_camss_tfe_2_csid_clk_src.clkr,
	[GCC_CAMSS_TFE_CPHY_RX_CLK_SRC] = &gcc_camss_tfe_cphy_rx_clk_src.clkr,
	[GCC_CAMSS_TOP_AHB_CLK] = &gcc_camss_top_ahb_clk.clkr,
	[GCC_CAMSS_TOP_AHB_CLK_SRC] = &gcc_camss_top_ahb_clk_src.clkr,
	[GCC_CFG_NOC_USB3_PRIM_AXI_CLK] = &gcc_cfg_noc_usb3_prim_axi_clk.clkr,
	[GCC_CPUSS_AHB_CLK_SRC] = &gcc_cpuss_ahb_clk_src.clkr,
	[GCC_CPUSS_AHB_POSTDIV_CLK_SRC] = &gcc_cpuss_ahb_postdiv_clk_src.clkr,
	[GCC_DISP_GPLL0_CLK_SRC] = &gcc_disp_gpll0_clk_src.clkr,
	[GCC_DISP_GPLL0_DIV_CLK_SRC] = &gcc_disp_gpll0_div_clk_src.clkr,
	[GCC_DISP_HF_AXI_CLK] = &gcc_disp_hf_axi_clk.clkr,
	[GCC_DISP_SLEEP_CLK] = &gcc_disp_sleep_clk.clkr,
	[GCC_DISP_THROTTLE_CORE_CLK] = &gcc_disp_throttle_core_clk.clkr,
	[GCC_GP1_CLK] = &gcc_gp1_clk.clkr,
	[GCC_GP1_CLK_SRC] = &gcc_gp1_clk_src.clkr,
	[GCC_GP2_CLK] = &gcc_gp2_clk.clkr,
	[GCC_GP2_CLK_SRC] = &gcc_gp2_clk_src.clkr,
	[GCC_GP3_CLK] = &gcc_gp3_clk.clkr,
	[GCC_GP3_CLK_SRC] = &gcc_gp3_clk_src.clkr,
	[GCC_GPU_GPLL0_CLK_SRC] = &gcc_gpu_gpll0_clk_src.clkr,
	[GCC_GPU_GPLL0_DIV_CLK_SRC] = &gcc_gpu_gpll0_div_clk_src.clkr,
	[GCC_GPU_MEMNOC_GFX_CLK] = &gcc_gpu_memnoc_gfx_clk.clkr,
	[GCC_GPU_SNOC_DVM_GFX_CLK] = &gcc_gpu_snoc_dvm_gfx_clk.clkr,
	[GCC_GPU_THROTTLE_CORE_CLK] = &gcc_gpu_throttle_core_clk.clkr,
	[GCC_PDM2_CLK] = &gcc_pdm2_clk.clkr,
	[GCC_PDM2_CLK_SRC] = &gcc_pdm2_clk_src.clkr,
	[GCC_PDM_AHB_CLK] = &gcc_pdm_ahb_clk.clkr,
	[GCC_PDM_XO4_CLK] = &gcc_pdm_xo4_clk.clkr,
	[GCC_PRNG_AHB_CLK] = &gcc_prng_ahb_clk.clkr,
	[GCC_QMIP_CAMERA_NRT_AHB_CLK] = &gcc_qmip_camera_nrt_ahb_clk.clkr,
	[GCC_QMIP_CAMERA_RT_AHB_CLK] = &gcc_qmip_camera_rt_ahb_clk.clkr,
	[GCC_QMIP_DISP_AHB_CLK] = &gcc_qmip_disp_ahb_clk.clkr,
	[GCC_QMIP_GPU_CFG_AHB_CLK] = &gcc_qmip_gpu_cfg_ahb_clk.clkr,
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
	[GCC_SYS_NOC_UFS_PHY_AXI_CLK] = &gcc_sys_noc_ufs_phy_axi_clk.clkr,
	[GCC_SYS_NOC_USB3_PRIM_AXI_CLK] = &gcc_sys_noc_usb3_prim_axi_clk.clkr,
	[GCC_UFS_PHY_AHB_CLK] = &gcc_ufs_phy_ahb_clk.clkr,
	[GCC_UFS_PHY_AXI_CLK] = &gcc_ufs_phy_axi_clk.clkr,
	[GCC_UFS_PHY_AXI_CLK_SRC] = &gcc_ufs_phy_axi_clk_src.clkr,
	[GCC_UFS_PHY_ICE_CORE_CLK] = &gcc_ufs_phy_ice_core_clk.clkr,
	[GCC_UFS_PHY_ICE_CORE_CLK_SRC] = &gcc_ufs_phy_ice_core_clk_src.clkr,
	[GCC_UFS_PHY_PHY_AUX_CLK] = &gcc_ufs_phy_phy_aux_clk.clkr,
	[GCC_UFS_PHY_PHY_AUX_CLK_SRC] = &gcc_ufs_phy_phy_aux_clk_src.clkr,
	[GCC_UFS_PHY_RX_SYMBOL_0_CLK] = &gcc_ufs_phy_rx_symbol_0_clk.clkr,
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
	[GCC_USB3_PRIM_CLKREF_CLK] = &gcc_usb3_prim_clkref_clk.clkr,
	[GCC_USB3_PRIM_PHY_AUX_CLK_SRC] = &gcc_usb3_prim_phy_aux_clk_src.clkr,
	[GCC_USB3_PRIM_PHY_COM_AUX_CLK] = &gcc_usb3_prim_phy_com_aux_clk.clkr,
	[GCC_USB3_PRIM_PHY_PIPE_CLK] = &gcc_usb3_prim_phy_pipe_clk.clkr,
	[GCC_VCODEC0_AXI_CLK] = &gcc_vcodec0_axi_clk.clkr,
	[GCC_VENUS_AHB_CLK] = &gcc_venus_ahb_clk.clkr,
	[GCC_VENUS_CTL_AXI_CLK] = &gcc_venus_ctl_axi_clk.clkr,
	[GCC_VIDEO_AXI0_CLK] = &gcc_video_axi0_clk.clkr,
	[GCC_VIDEO_THROTTLE_CORE_CLK] = &gcc_video_throttle_core_clk.clkr,
	[GCC_VIDEO_VCODEC0_SYS_CLK] = &gcc_video_vcodec0_sys_clk.clkr,
	[GCC_VIDEO_VENUS_CLK_SRC] = &gcc_video_venus_clk_src.clkr,
	[GCC_VIDEO_VENUS_CTL_CLK] = &gcc_video_venus_ctl_clk.clkr,
	[GCC_VIDEO_XO_CLK] = &gcc_video_xo_clk.clkr,
	[GCC_UFS_MEM_CLKREF_CLK] = &gcc_ufs_mem_clkref_clk.clkr,
	[GCC_RX5_PCIE_CLKREF_EN_CLK] = &gcc_rx5_pcie_clkref_en_clk.clkr,
	[GPLL0] = &gpll0.clkr,
	[GPLL0_OUT_EVEN] = &gpll0_out_even.clkr,
	[GPLL0_OUT_ODD] = &gpll0_out_odd.clkr,
	[GPLL10] = &gpll10.clkr,
	[GPLL11] = &gpll11.clkr,
	[GPLL3] = &gpll3.clkr,
	[GPLL3_OUT_EVEN] = &gpll3_out_even.clkr,
	[GPLL4] = &gpll4.clkr,
	[GPLL5] = &gpll5.clkr,
	[GPLL6] = &gpll6.clkr,
	[GPLL6_OUT_EVEN] = &gpll6_out_even.clkr,
	[GPLL7] = &gpll7.clkr,
	[GPLL8] = &gpll8.clkr,
	[GPLL8_OUT_EVEN] = &gpll8_out_even.clkr,
	[GPLL9] = &gpll9.clkr,
	[GPLL9_OUT_MAIN] = &gpll9_out_main.clkr,
};

static const struct qcom_reset_map gcc_holi_resets[] = {
	[GCC_CAMSS_OPE_BCR] = { 0x55000 },
	[GCC_CAMSS_TFE_BCR] = { 0x52000 },
	[GCC_CAMSS_TOP_BCR] = { 0x58000 },
	[GCC_GPU_BCR] = { 0x36000 },
	[GCC_MMSS_BCR] = { 0x17000 },
	[GCC_PDM_BCR] = { 0x20000 },
	[GCC_PRNG_BCR] = { 0x21000 },
	[GCC_QUPV3_WRAPPER_0_BCR] = { 0x1f000 },
	[GCC_QUPV3_WRAPPER_1_BCR] = { 0x53000 },
	[GCC_QUSB2PHY_PRIM_BCR] = { 0x1c000 },
	[GCC_QUSB2PHY_SEC_BCR] = { 0x1c004 },
	[GCC_SDCC1_BCR] = { 0x38000 },
	[GCC_SDCC2_BCR] = { 0x1e000 },
	[GCC_UFS_PHY_BCR] = { 0x45000 },
	[GCC_USB30_PRIM_BCR] = { 0x1a000 },
	[GCC_USB3_DP_PHY_PRIM_BCR] = { 0x1b020 },
	[GCC_USB3_PHY_PRIM_SP0_BCR] = { 0x1b000 },
	[GCC_USB_PHY_CFG_AHB2PHY_BCR] = { 0x1d000 },
	[GCC_VCODEC0_BCR] = { 0x58094 },
	[GCC_VENUS_BCR] = { 0x58078 },
	[GCC_VIDEO_INTERFACE_BCR] = { 0x6e000 },
	[GCC_VENUS_CTL_AXI_CLK_ARES] = { 0x6e004, 2 },
	[GCC_VIDEO_VENUS_CTL_CLK_ARES] = { 0x5808c, 2 },
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

static const struct regmap_config gcc_holi_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xc7000,
	.fast_io = true,
};

struct clk_hw *gcc_holi_hws[] = {
	[GPLL9_OUT_EARLY] = &gpll9_out_early.hw,
};

static const struct qcom_cc_desc gcc_holi_desc = {
	.config = &gcc_holi_regmap_config,
	.clks = gcc_holi_clocks,
	.num_clks = ARRAY_SIZE(gcc_holi_clocks),
	.resets = gcc_holi_resets,
	.clk_hws = gcc_holi_hws,
	.num_clk_hws = ARRAY_SIZE(gcc_holi_hws),
	.num_resets = ARRAY_SIZE(gcc_holi_resets),
	.clk_regulators = gcc_holi_regulators,
	.num_clk_regulators = ARRAY_SIZE(gcc_holi_regulators),
};

static const struct of_device_id gcc_holi_match_table[] = {
	{ .compatible = "qcom,holi-gcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_holi_match_table);

static int gcc_holi_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret;

	regmap = qcom_cc_map(pdev, &gcc_holi_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/*
	 * Keep the clocks always-ON
	 * GCC_CAMERA_AHB_CLK, GCC_CAMERA_XO_CLK, GCC_CPUSS_GNOC_CLK,
	 * GCC_DISP_AHB_CLK, GCC_DISP_XO_CLK, GCC_GPU_CFG_AHB_CLK,
	 * GCC_VIDEO_AHB_CLK, GCC_SYS_NOC_CPUSS_AHB_CLK
	 */
	regmap_update_bits(regmap, 0x17008, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x17028, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x2b004, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x1700c, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x1702c, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x36004, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x17004, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x79004, BIT(0), BIT(0));

	ret = qcom_cc_register_rcg_dfs(regmap, gcc_dfs_clocks,
				ARRAY_SIZE(gcc_dfs_clocks));
	if (ret)
		return ret;

	/* GCC_DISP_GPLL0_CDIVR__CLK_DIV */
	regmap_update_bits(regmap, 0x17058, 0x1, 0x1);

	clk_fabia_pll_configure(&gpll10, regmap, &gpll10_config);
	clk_fabia_pll_configure(&gpll11, regmap, &gpll11_config);
	clk_fabia_pll_configure(&gpll8, regmap, &gpll8_config);
	clk_agera_pll_configure(&gpll9, regmap, &gpll9_config);

	ret = qcom_cc_really_probe(pdev, &gcc_holi_desc, regmap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register GCC clocks\n");
		return ret;
	}

	dev_info(&pdev->dev, "Registered GCC clocks\n");

	return ret;
}

static void gcc_holi_sync_state(struct device *dev)
{
	qcom_cc_sync_state(dev, &gcc_holi_desc);
}

static struct platform_driver gcc_holi_driver = {
	.probe = gcc_holi_probe,
	.driver = {
		.name = "gcc-holi",
		.of_match_table = gcc_holi_match_table,
		.sync_state = gcc_holi_sync_state,
	},
};

static int __init gcc_holi_init(void)
{
	return platform_driver_register(&gcc_holi_driver);
}
subsys_initcall(gcc_holi_init);

static void __exit gcc_holi_exit(void)
{
	platform_driver_unregister(&gcc_holi_driver);
}
module_exit(gcc_holi_exit);

MODULE_DESCRIPTION("QTI GCC HOLI Driver");
MODULE_LICENSE("GPL");
