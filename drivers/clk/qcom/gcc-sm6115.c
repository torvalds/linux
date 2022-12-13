// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

#include <dt-bindings/clock/qcom,gcc-sm6115.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "common.h"
#include "gdsc.h"
#include "reset.h"

enum {
	P_BI_TCXO,
	P_GPLL0_OUT_AUX2,
	P_GPLL0_OUT_EARLY,
	P_GPLL10_OUT_MAIN,
	P_GPLL11_OUT_MAIN,
	P_GPLL3_OUT_EARLY,
	P_GPLL4_OUT_MAIN,
	P_GPLL6_OUT_EARLY,
	P_GPLL6_OUT_MAIN,
	P_GPLL7_OUT_MAIN,
	P_GPLL8_OUT_EARLY,
	P_GPLL8_OUT_MAIN,
	P_GPLL9_OUT_EARLY,
	P_GPLL9_OUT_MAIN,
	P_SLEEP_CLK,
};

static struct pll_vco default_vco[] = {
	{ 500000000, 1000000000, 2 },
};

static struct pll_vco gpll9_vco[] = {
	{ 500000000, 1250000000, 0 },
};

static struct pll_vco gpll10_vco[] = {
	{ 750000000, 1500000000, 1 },
};

static struct clk_alpha_pll gpll0 = {
	.offset = 0x0,
	.vco_table = default_vco,
	.num_vco = ARRAY_SIZE(default_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpll0",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static const struct clk_div_table post_div_table_gpll0_out_aux2[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll0_out_aux2 = {
	.offset = 0x0,
	.post_div_shift = 8,
	.post_div_table = post_div_table_gpll0_out_aux2,
	.num_post_div = ARRAY_SIZE(post_div_table_gpll0_out_aux2),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll0_out_aux2",
		.parent_hws = (const struct clk_hw *[]){ &gpll0.clkr.hw },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ro_ops,
	},
};

static const struct clk_div_table post_div_table_gpll0_out_main[] = {
	{ 0x0, 1 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll0_out_main = {
	.offset = 0x0,
	.post_div_shift = 8,
	.post_div_table = post_div_table_gpll0_out_main,
	.num_post_div = ARRAY_SIZE(post_div_table_gpll0_out_main),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll0_out_main",
		.parent_hws = (const struct clk_hw *[]){ &gpll0.clkr.hw },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ro_ops,
	},
};

/* 1152MHz configuration */
static const struct alpha_pll_config gpll10_config = {
	.l = 0x3c,
	.vco_val = 0x1 << 20,
	.vco_mask = GENMASK(21, 20),
	.main_output_mask = BIT(0),
	.config_ctl_val = 0x4001055b,
};

static struct clk_alpha_pll gpll10 = {
	.offset = 0xa000,
	.vco_table = gpll10_vco,
	.num_vco = ARRAY_SIZE(gpll10_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(10),
		.hw.init = &(struct clk_init_data){
			.name = "gpll10",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static const struct clk_div_table post_div_table_gpll10_out_main[] = {
	{ 0x0, 1 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll10_out_main = {
	.offset = 0xa000,
	.post_div_shift = 8,
	.post_div_table = post_div_table_gpll10_out_main,
	.num_post_div = ARRAY_SIZE(post_div_table_gpll10_out_main),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll10_out_main",
		.parent_hws = (const struct clk_hw *[]){ &gpll10.clkr.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_ops,
	},
};

/* 600MHz configuration */
static const struct alpha_pll_config gpll11_config = {
	.l = 0x1F,
	.alpha = 0x0,
	.alpha_hi = 0x40,
	.alpha_en_mask = BIT(24),
	.vco_val = 0x2 << 20,
	.vco_mask = GENMASK(21, 20),
	.config_ctl_val = 0x4001055b,
};

static struct clk_alpha_pll gpll11 = {
	.offset = 0xb000,
	.vco_table = default_vco,
	.num_vco = ARRAY_SIZE(default_vco),
	.flags = SUPPORTS_DYNAMIC_UPDATE,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gpll11",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static const struct clk_div_table post_div_table_gpll11_out_main[] = {
	{ 0x0, 1 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll11_out_main = {
	.offset = 0xb000,
	.post_div_shift = 8,
	.post_div_table = post_div_table_gpll11_out_main,
	.num_post_div = ARRAY_SIZE(post_div_table_gpll11_out_main),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll11_out_main",
		.parent_hws = (const struct clk_hw *[]){ &gpll11.clkr.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_ops,
	},
};

static struct clk_alpha_pll gpll3 = {
	.offset = 0x3000,
	.vco_table = default_vco,
	.num_vco = ARRAY_SIZE(default_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(3),
		.hw.init = &(struct clk_init_data){
			.name = "gpll3",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct clk_alpha_pll gpll4 = {
	.offset = 0x4000,
	.vco_table = default_vco,
	.num_vco = ARRAY_SIZE(default_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gpll4",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static const struct clk_div_table post_div_table_gpll4_out_main[] = {
	{ 0x0, 1 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll4_out_main = {
	.offset = 0x4000,
	.post_div_shift = 8,
	.post_div_table = post_div_table_gpll4_out_main,
	.num_post_div = ARRAY_SIZE(post_div_table_gpll4_out_main),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll4_out_main",
		.parent_hws = (const struct clk_hw *[]){ &gpll4.clkr.hw },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ro_ops,
	},
};

static struct clk_alpha_pll gpll6 = {
	.offset = 0x6000,
	.vco_table = default_vco,
	.num_vco = ARRAY_SIZE(default_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(6),
		.hw.init = &(struct clk_init_data){
			.name = "gpll6",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static const struct clk_div_table post_div_table_gpll6_out_main[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll6_out_main = {
	.offset = 0x6000,
	.post_div_shift = 8,
	.post_div_table = post_div_table_gpll6_out_main,
	.num_post_div = ARRAY_SIZE(post_div_table_gpll6_out_main),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll6_out_main",
		.parent_hws = (const struct clk_hw *[]){ &gpll6.clkr.hw },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ro_ops,
	},
};

static struct clk_alpha_pll gpll7 = {
	.offset = 0x7000,
	.vco_table = default_vco,
	.num_vco = ARRAY_SIZE(default_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(7),
		.hw.init = &(struct clk_init_data){
			.name = "gpll7",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static const struct clk_div_table post_div_table_gpll7_out_main[] = {
	{ 0x0, 1 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll7_out_main = {
	.offset = 0x7000,
	.post_div_shift = 8,
	.post_div_table = post_div_table_gpll7_out_main,
	.num_post_div = ARRAY_SIZE(post_div_table_gpll7_out_main),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll7_out_main",
		.parent_hws = (const struct clk_hw *[]){ &gpll7.clkr.hw },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ro_ops,
	},
};

/* 800MHz configuration */
static const struct alpha_pll_config gpll8_config = {
	.l = 0x29,
	.alpha = 0xAAAAAAAA,
	.alpha_hi = 0xAA,
	.alpha_en_mask = BIT(24),
	.vco_val = 0x2 << 20,
	.vco_mask = GENMASK(21, 20),
	.main_output_mask = BIT(0),
	.early_output_mask = BIT(3),
	.post_div_val = 0x1 << 8,
	.post_div_mask = GENMASK(11, 8),
	.config_ctl_val = 0x4001055b,
};

static struct clk_alpha_pll gpll8 = {
	.offset = 0x8000,
	.vco_table = default_vco,
	.num_vco = ARRAY_SIZE(default_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.flags = SUPPORTS_DYNAMIC_UPDATE,
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(8),
		.hw.init = &(struct clk_init_data){
			.name = "gpll8",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static const struct clk_div_table post_div_table_gpll8_out_main[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll8_out_main = {
	.offset = 0x8000,
	.post_div_shift = 8,
	.post_div_table = post_div_table_gpll8_out_main,
	.num_post_div = ARRAY_SIZE(post_div_table_gpll8_out_main),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll8_out_main",
		.parent_hws = (const struct clk_hw *[]){ &gpll8.clkr.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_ro_ops,
	},
};

/* 1152MHz configuration */
static const struct alpha_pll_config gpll9_config = {
	.l = 0x3C,
	.alpha = 0x0,
	.post_div_val = 0x1 << 8,
	.post_div_mask = GENMASK(9, 8),
	.main_output_mask = BIT(0),
	.config_ctl_val = 0x00004289,
};

static struct clk_alpha_pll gpll9 = {
	.offset = 0x9000,
	.vco_table = gpll9_vco,
	.num_vco = ARRAY_SIZE(gpll9_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_BRAMMO_EVO],
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gpll9",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static const struct clk_div_table post_div_table_gpll9_out_main[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll9_out_main = {
	.offset = 0x9000,
	.post_div_shift = 8,
	.post_div_table = post_div_table_gpll9_out_main,
	.num_post_div = ARRAY_SIZE(post_div_table_gpll9_out_main),
	.width = 2,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_BRAMMO_EVO],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll9_out_main",
		.parent_hws = (const struct clk_hw *[]){ &gpll9.clkr.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_ops,
	},
};

static const struct parent_map gcc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL0_OUT_AUX2, 2 },
};

static const struct clk_parent_data gcc_parents_0[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_aux2.clkr.hw },
};

static const struct parent_map gcc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL0_OUT_AUX2, 2 },
	{ P_GPLL6_OUT_MAIN, 4 },
};

static const struct clk_parent_data gcc_parents_1[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_aux2.clkr.hw },
	{ .hw = &gpll6_out_main.clkr.hw },
};

static const struct parent_map gcc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL0_OUT_AUX2, 2 },
	{ P_SLEEP_CLK, 5 },
};

static const struct clk_parent_data gcc_parents_2[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_aux2.clkr.hw },
	{ .fw_name = "sleep_clk" },
};

static const struct parent_map gcc_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL9_OUT_EARLY, 2 },
	{ P_GPLL10_OUT_MAIN, 3 },
	{ P_GPLL9_OUT_MAIN, 5 },
};

static const struct clk_parent_data gcc_parents_3[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll9.clkr.hw },
	{ .hw = &gpll10_out_main.clkr.hw },
	{ .hw = &gpll9_out_main.clkr.hw },
};

static const struct parent_map gcc_parent_map_4[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL0_OUT_AUX2, 2 },
	{ P_GPLL4_OUT_MAIN, 5 },
};

static const struct clk_parent_data gcc_parents_4[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_aux2.clkr.hw },
	{ .hw = &gpll4_out_main.clkr.hw },
};

static const struct parent_map gcc_parent_map_5[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL8_OUT_EARLY, 2 },
	{ P_GPLL10_OUT_MAIN, 3 },
	{ P_GPLL8_OUT_MAIN, 4 },
	{ P_GPLL9_OUT_MAIN, 5 },
};

static const struct clk_parent_data gcc_parents_5[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll8.clkr.hw },
	{ .hw = &gpll10_out_main.clkr.hw },
	{ .hw = &gpll8_out_main.clkr.hw },
	{ .hw = &gpll9_out_main.clkr.hw },
};

static const struct parent_map gcc_parent_map_6[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL8_OUT_EARLY, 2 },
	{ P_GPLL10_OUT_MAIN, 3 },
	{ P_GPLL6_OUT_MAIN, 4 },
	{ P_GPLL9_OUT_MAIN, 5 },
	{ P_GPLL3_OUT_EARLY, 6 },
};

static const struct clk_parent_data gcc_parents_6[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll8.clkr.hw },
	{ .hw = &gpll10_out_main.clkr.hw },
	{ .hw = &gpll6_out_main.clkr.hw },
	{ .hw = &gpll9_out_main.clkr.hw },
	{ .hw = &gpll3.clkr.hw },
};

static const struct parent_map gcc_parent_map_7[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL0_OUT_AUX2, 2 },
	{ P_GPLL10_OUT_MAIN, 3 },
	{ P_GPLL4_OUT_MAIN, 5 },
	{ P_GPLL3_OUT_EARLY, 6 },
};

static const struct clk_parent_data gcc_parents_7[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_aux2.clkr.hw },
	{ .hw = &gpll10_out_main.clkr.hw },
	{ .hw = &gpll4_out_main.clkr.hw },
	{ .hw = &gpll3.clkr.hw },
};

static const struct parent_map gcc_parent_map_8[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL8_OUT_EARLY, 2 },
	{ P_GPLL10_OUT_MAIN, 3 },
	{ P_GPLL8_OUT_MAIN, 4 },
	{ P_GPLL9_OUT_MAIN, 5 },
	{ P_GPLL3_OUT_EARLY, 6 },
};

static const struct clk_parent_data gcc_parents_8[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll8.clkr.hw },
	{ .hw = &gpll10_out_main.clkr.hw },
	{ .hw = &gpll8_out_main.clkr.hw },
	{ .hw = &gpll9_out_main.clkr.hw },
	{ .hw = &gpll3.clkr.hw },
};

static const struct parent_map gcc_parent_map_9[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL0_OUT_AUX2, 2 },
	{ P_GPLL10_OUT_MAIN, 3 },
	{ P_GPLL8_OUT_MAIN, 4 },
	{ P_GPLL9_OUT_MAIN, 5 },
	{ P_GPLL3_OUT_EARLY, 6 },
};

static const struct clk_parent_data gcc_parents_9[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_aux2.clkr.hw },
	{ .hw = &gpll10_out_main.clkr.hw },
	{ .hw = &gpll8_out_main.clkr.hw },
	{ .hw = &gpll9_out_main.clkr.hw },
	{ .hw = &gpll3.clkr.hw },
};

static const struct parent_map gcc_parent_map_10[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL8_OUT_EARLY, 2 },
	{ P_GPLL10_OUT_MAIN, 3 },
	{ P_GPLL6_OUT_EARLY, 4 },
	{ P_GPLL9_OUT_MAIN, 5 },
};

static const struct clk_parent_data gcc_parents_10[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll8.clkr.hw },
	{ .hw = &gpll10_out_main.clkr.hw },
	{ .hw = &gpll6.clkr.hw },
	{ .hw = &gpll9_out_main.clkr.hw },
};

static const struct parent_map gcc_parent_map_11[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL0_OUT_AUX2, 2 },
	{ P_GPLL7_OUT_MAIN, 3 },
	{ P_GPLL4_OUT_MAIN, 5 },
};

static const struct clk_parent_data gcc_parents_11[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_aux2.clkr.hw },
	{ .hw = &gpll7_out_main.clkr.hw },
	{ .hw = &gpll4_out_main.clkr.hw },
};

static const struct parent_map gcc_parent_map_12[] = {
	{ P_BI_TCXO, 0 },
	{ P_SLEEP_CLK, 5 },
};

static const struct clk_parent_data gcc_parents_12[] = {
	{ .fw_name = "bi_tcxo" },
	{ .fw_name = "sleep_clk" },
};

static const struct parent_map gcc_parent_map_13[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL11_OUT_MAIN, 1 },
};

static const struct clk_parent_data gcc_parents_13[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll11_out_main.clkr.hw },
};

static const struct freq_tbl ftbl_gcc_camss_axi_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(150000000, P_GPLL0_OUT_AUX2, 2, 0, 0),
	F(200000000, P_GPLL0_OUT_AUX2, 1.5, 0, 0),
	F(300000000, P_GPLL0_OUT_AUX2, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_axi_clk_src = {
	.cmd_rcgr = 0x5802c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_7,
	.freq_tbl = ftbl_gcc_camss_axi_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_axi_clk_src",
		.parent_data = gcc_parents_7,
		.num_parents = ARRAY_SIZE(gcc_parents_7),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_camss_cci_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(37500000, P_GPLL0_OUT_AUX2, 8, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_cci_clk_src = {
	.cmd_rcgr = 0x56000,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_9,
	.freq_tbl = ftbl_gcc_camss_cci_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_cci_clk_src",
		.parent_data = gcc_parents_9,
		.num_parents = ARRAY_SIZE(gcc_parents_9),
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_camss_csi0phytimer_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(100000000, P_GPLL0_OUT_AUX2, 3, 0, 0),
	F(200000000, P_GPLL0_OUT_AUX2, 1.5, 0, 0),
	F(268800000, P_GPLL4_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_csi0phytimer_clk_src = {
	.cmd_rcgr = 0x59000,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_camss_csi0phytimer_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_csi0phytimer_clk_src",
		.parent_data = gcc_parents_4,
		.num_parents = ARRAY_SIZE(gcc_parents_4),
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_camss_csi1phytimer_clk_src = {
	.cmd_rcgr = 0x5901c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_camss_csi0phytimer_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_csi1phytimer_clk_src",
		.parent_data = gcc_parents_4,
		.num_parents = ARRAY_SIZE(gcc_parents_4),
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_camss_csi2phytimer_clk_src = {
	.cmd_rcgr = 0x59038,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_camss_csi0phytimer_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_csi2phytimer_clk_src",
		.parent_data = gcc_parents_4,
		.num_parents = ARRAY_SIZE(gcc_parents_4),
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_camss_mclk0_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(24000000, P_GPLL9_OUT_MAIN, 1, 1, 24),
	F(64000000, P_GPLL9_OUT_MAIN, 1, 1, 9),
	{ }
};

static struct clk_rcg2 gcc_camss_mclk0_clk_src = {
	.cmd_rcgr = 0x51000,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_camss_mclk0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_mclk0_clk_src",
		.parent_data = gcc_parents_3,
		.num_parents = ARRAY_SIZE(gcc_parents_3),
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_camss_mclk1_clk_src = {
	.cmd_rcgr = 0x5101c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_camss_mclk0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_mclk1_clk_src",
		.parent_data = gcc_parents_3,
		.num_parents = ARRAY_SIZE(gcc_parents_3),
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_camss_mclk2_clk_src = {
	.cmd_rcgr = 0x51038,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_camss_mclk0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_mclk2_clk_src",
		.parent_data = gcc_parents_3,
		.num_parents = ARRAY_SIZE(gcc_parents_3),
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_camss_mclk3_clk_src = {
	.cmd_rcgr = 0x51054,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_camss_mclk0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_mclk3_clk_src",
		.parent_data = gcc_parents_3,
		.num_parents = ARRAY_SIZE(gcc_parents_3),
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_camss_ope_ahb_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(171428571, P_GPLL0_OUT_EARLY, 3.5, 0, 0),
	F(240000000, P_GPLL0_OUT_EARLY, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_ope_ahb_clk_src = {
	.cmd_rcgr = 0x55024,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_8,
	.freq_tbl = ftbl_gcc_camss_ope_ahb_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_ope_ahb_clk_src",
		.parent_data = gcc_parents_8,
		.num_parents = ARRAY_SIZE(gcc_parents_8),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_camss_ope_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(200000000, P_GPLL8_OUT_MAIN, 2, 0, 0),
	F(266600000, P_GPLL8_OUT_MAIN, 1, 0, 0),
	F(465000000, P_GPLL8_OUT_MAIN, 1, 0, 0),
	F(576000000, P_GPLL9_OUT_MAIN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_ope_clk_src = {
	.cmd_rcgr = 0x55004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_8,
	.freq_tbl = ftbl_gcc_camss_ope_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_ope_clk_src",
		.parent_data = gcc_parents_8,
		.num_parents = ARRAY_SIZE(gcc_parents_8),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_camss_tfe_0_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(128000000, P_GPLL10_OUT_MAIN, 9, 0, 0),
	F(135529412, P_GPLL10_OUT_MAIN, 8.5, 0, 0),
	F(144000000, P_GPLL10_OUT_MAIN, 8, 0, 0),
	F(153600000, P_GPLL10_OUT_MAIN, 7.5, 0, 0),
	F(164571429, P_GPLL10_OUT_MAIN, 7, 0, 0),
	F(177230769, P_GPLL10_OUT_MAIN, 6.5, 0, 0),
	F(192000000, P_GPLL10_OUT_MAIN, 6, 0, 0),
	F(209454545, P_GPLL10_OUT_MAIN, 5.5, 0, 0),
	F(230400000, P_GPLL10_OUT_MAIN, 5, 0, 0),
	F(256000000, P_GPLL10_OUT_MAIN, 4.5, 0, 0),
	F(288000000, P_GPLL10_OUT_MAIN, 4, 0, 0),
	F(329142857, P_GPLL10_OUT_MAIN, 3.5, 0, 0),
	F(384000000, P_GPLL10_OUT_MAIN, 3, 0, 0),
	F(460800000, P_GPLL10_OUT_MAIN, 2.5, 0, 0),
	F(576000000, P_GPLL10_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_tfe_0_clk_src = {
	.cmd_rcgr = 0x52004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_camss_tfe_0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_tfe_0_clk_src",
		.parent_data = gcc_parents_5,
		.num_parents = ARRAY_SIZE(gcc_parents_5),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_camss_tfe_0_csid_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(120000000, P_GPLL0_OUT_EARLY, 5, 0, 0),
	F(192000000, P_GPLL6_OUT_MAIN, 2, 0, 0),
	F(240000000, P_GPLL0_OUT_EARLY, 2.5, 0, 0),
	F(384000000, P_GPLL6_OUT_MAIN, 1, 0, 0),
	F(426400000, P_GPLL3_OUT_EARLY, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_tfe_0_csid_clk_src = {
	.cmd_rcgr = 0x52094,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_6,
	.freq_tbl = ftbl_gcc_camss_tfe_0_csid_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_tfe_0_csid_clk_src",
		.parent_data = gcc_parents_6,
		.num_parents = ARRAY_SIZE(gcc_parents_6),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_camss_tfe_1_clk_src = {
	.cmd_rcgr = 0x52024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_camss_tfe_0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_tfe_1_clk_src",
		.parent_data = gcc_parents_5,
		.num_parents = ARRAY_SIZE(gcc_parents_5),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_camss_tfe_1_csid_clk_src = {
	.cmd_rcgr = 0x520b4,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_6,
	.freq_tbl = ftbl_gcc_camss_tfe_0_csid_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_tfe_1_csid_clk_src",
		.parent_data = gcc_parents_6,
		.num_parents = ARRAY_SIZE(gcc_parents_6),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_camss_tfe_2_clk_src = {
	.cmd_rcgr = 0x52044,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_camss_tfe_0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_tfe_2_clk_src",
		.parent_data = gcc_parents_5,
		.num_parents = ARRAY_SIZE(gcc_parents_5),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_camss_tfe_2_csid_clk_src = {
	.cmd_rcgr = 0x520d4,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_6,
	.freq_tbl = ftbl_gcc_camss_tfe_0_csid_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_tfe_2_csid_clk_src",
		.parent_data = gcc_parents_6,
		.num_parents = ARRAY_SIZE(gcc_parents_6),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_camss_tfe_cphy_rx_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(240000000, P_GPLL0_OUT_EARLY, 2.5, 0, 0),
	F(341333333, P_GPLL6_OUT_EARLY, 1, 4, 9),
	F(384000000, P_GPLL6_OUT_EARLY, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_tfe_cphy_rx_clk_src = {
	.cmd_rcgr = 0x52064,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_10,
	.freq_tbl = ftbl_gcc_camss_tfe_cphy_rx_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_tfe_cphy_rx_clk_src",
		.parent_data = gcc_parents_10,
		.num_parents = ARRAY_SIZE(gcc_parents_10),
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_camss_top_ahb_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(40000000, P_GPLL0_OUT_AUX2, 7.5, 0, 0),
	F(80000000, P_GPLL0_OUT_EARLY, 7.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_top_ahb_clk_src = {
	.cmd_rcgr = 0x58010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_7,
	.freq_tbl = ftbl_gcc_camss_top_ahb_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_top_ahb_clk_src",
		.parent_data = gcc_parents_7,
		.num_parents = ARRAY_SIZE(gcc_parents_7),
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_gp1_clk_src[] = {
	F(25000000, P_GPLL0_OUT_AUX2, 12, 0, 0),
	F(50000000, P_GPLL0_OUT_AUX2, 6, 0, 0),
	F(100000000, P_GPLL0_OUT_AUX2, 3, 0, 0),
	F(200000000, P_GPLL0_OUT_AUX2, 1.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_gp1_clk_src = {
	.cmd_rcgr = 0x4d004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_gp1_clk_src",
		.parent_data = gcc_parents_2,
		.num_parents = ARRAY_SIZE(gcc_parents_2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_gp2_clk_src = {
	.cmd_rcgr = 0x4e004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_gp2_clk_src",
		.parent_data = gcc_parents_2,
		.num_parents = ARRAY_SIZE(gcc_parents_2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_gp3_clk_src = {
	.cmd_rcgr = 0x4f004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_gp3_clk_src",
		.parent_data = gcc_parents_2,
		.num_parents = ARRAY_SIZE(gcc_parents_2),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pdm2_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(60000000, P_GPLL0_OUT_AUX2, 5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pdm2_clk_src = {
	.cmd_rcgr = 0x20010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pdm2_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_pdm2_clk_src",
		.parent_data = gcc_parents_0,
		.num_parents = ARRAY_SIZE(gcc_parents_0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap0_s0_clk_src[] = {
	F(7372800, P_GPLL0_OUT_AUX2, 1, 384, 15625),
	F(14745600, P_GPLL0_OUT_AUX2, 1, 768, 15625),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(29491200, P_GPLL0_OUT_AUX2, 1, 1536, 15625),
	F(32000000, P_GPLL0_OUT_AUX2, 1, 8, 75),
	F(48000000, P_GPLL0_OUT_AUX2, 1, 4, 25),
	F(64000000, P_GPLL0_OUT_AUX2, 1, 16, 75),
	F(75000000, P_GPLL0_OUT_AUX2, 4, 0, 0),
	F(80000000, P_GPLL0_OUT_AUX2, 1, 4, 15),
	F(96000000, P_GPLL0_OUT_AUX2, 1, 8, 25),
	F(100000000, P_GPLL0_OUT_AUX2, 3, 0, 0),
	F(102400000, P_GPLL0_OUT_AUX2, 1, 128, 375),
	F(112000000, P_GPLL0_OUT_AUX2, 1, 28, 75),
	F(117964800, P_GPLL0_OUT_AUX2, 1, 6144, 15625),
	F(120000000, P_GPLL0_OUT_AUX2, 2.5, 0, 0),
	F(128000000, P_GPLL6_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_init_data gcc_qupv3_wrap0_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s0_clk_src",
	.parent_data = gcc_parents_1,
	.num_parents = ARRAY_SIZE(gcc_parents_1),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s0_clk_src = {
	.cmd_rcgr = 0x1f148,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s0_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s1_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s1_clk_src",
	.parent_data = gcc_parents_1,
	.num_parents = ARRAY_SIZE(gcc_parents_1),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s1_clk_src = {
	.cmd_rcgr = 0x1f278,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s1_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s2_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s2_clk_src",
	.parent_data = gcc_parents_1,
	.num_parents = ARRAY_SIZE(gcc_parents_1),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s2_clk_src = {
	.cmd_rcgr = 0x1f3a8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s2_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s3_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s3_clk_src",
	.parent_data = gcc_parents_1,
	.num_parents = ARRAY_SIZE(gcc_parents_1),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s3_clk_src = {
	.cmd_rcgr = 0x1f4d8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s3_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s4_clk_src",
	.parent_data = gcc_parents_1,
	.num_parents = ARRAY_SIZE(gcc_parents_1),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s4_clk_src = {
	.cmd_rcgr = 0x1f608,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s4_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s5_clk_src",
	.parent_data = gcc_parents_1,
	.num_parents = ARRAY_SIZE(gcc_parents_1),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s5_clk_src = {
	.cmd_rcgr = 0x1f738,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s5_clk_src_init,
};

static const struct freq_tbl ftbl_gcc_sdcc1_apps_clk_src[] = {
	F(144000, P_BI_TCXO, 16, 3, 25),
	F(400000, P_BI_TCXO, 12, 1, 4),
	F(20000000, P_GPLL0_OUT_AUX2, 5, 1, 3),
	F(25000000, P_GPLL0_OUT_AUX2, 6, 1, 2),
	F(50000000, P_GPLL0_OUT_AUX2, 6, 0, 0),
	F(100000000, P_GPLL0_OUT_AUX2, 3, 0, 0),
	F(192000000, P_GPLL6_OUT_MAIN, 2, 0, 0),
	F(384000000, P_GPLL6_OUT_MAIN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc1_apps_clk_src = {
	.cmd_rcgr = 0x38028,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_sdcc1_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_sdcc1_apps_clk_src",
		.parent_data = gcc_parents_1,
		.num_parents = ARRAY_SIZE(gcc_parents_1),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_sdcc1_ice_core_clk_src[] = {
	F(75000000, P_GPLL0_OUT_AUX2, 4, 0, 0),
	F(100000000, P_GPLL0_OUT_AUX2, 3, 0, 0),
	F(150000000, P_GPLL0_OUT_AUX2, 2, 0, 0),
	F(200000000, P_GPLL0_OUT_EARLY, 3, 0, 0),
	F(300000000, P_GPLL0_OUT_AUX2, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc1_ice_core_clk_src = {
	.cmd_rcgr = 0x38010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_sdcc1_ice_core_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_sdcc1_ice_core_clk_src",
		.parent_data = gcc_parents_0,
		.num_parents = ARRAY_SIZE(gcc_parents_0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_sdcc2_apps_clk_src[] = {
	F(400000, P_BI_TCXO, 12, 1, 4),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(25000000, P_GPLL0_OUT_AUX2, 12, 0, 0),
	F(50000000, P_GPLL0_OUT_AUX2, 6, 0, 0),
	F(100000000, P_GPLL0_OUT_AUX2, 3, 0, 0),
	F(200000000, P_GPLL0_OUT_EARLY, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc2_apps_clk_src = {
	.cmd_rcgr = 0x1e00c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_11,
	.freq_tbl = ftbl_gcc_sdcc2_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_sdcc2_apps_clk_src",
		.parent_data = gcc_parents_11,
		.num_parents = ARRAY_SIZE(gcc_parents_11),
		.ops = &clk_rcg2_ops,
		.flags = CLK_OPS_PARENT_ENABLE,
	},
};

static const struct freq_tbl ftbl_gcc_ufs_phy_axi_clk_src[] = {
	F(25000000, P_GPLL0_OUT_AUX2, 12, 0, 0),
	F(50000000, P_GPLL0_OUT_AUX2, 6, 0, 0),
	F(100000000, P_GPLL0_OUT_AUX2, 3, 0, 0),
	F(200000000, P_GPLL0_OUT_EARLY, 3, 0, 0),
	F(240000000, P_GPLL0_OUT_EARLY, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_phy_axi_clk_src = {
	.cmd_rcgr = 0x45020,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_phy_axi_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_phy_axi_clk_src",
		.parent_data = gcc_parents_0,
		.num_parents = ARRAY_SIZE(gcc_parents_0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_ufs_phy_ice_core_clk_src[] = {
	F(37500000, P_GPLL0_OUT_AUX2, 8, 0, 0),
	F(75000000, P_GPLL0_OUT_AUX2, 4, 0, 0),
	F(150000000, P_GPLL0_OUT_AUX2, 2, 0, 0),
	F(300000000, P_GPLL0_OUT_AUX2, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_phy_ice_core_clk_src = {
	.cmd_rcgr = 0x45048,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_phy_ice_core_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_phy_ice_core_clk_src",
		.parent_data = gcc_parents_0,
		.num_parents = ARRAY_SIZE(gcc_parents_0),
		.ops = &clk_rcg2_ops,
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
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_phy_phy_aux_clk_src",
		.parent_data = gcc_parents_0,
		.num_parents = ARRAY_SIZE(gcc_parents_0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_ufs_phy_unipro_core_clk_src[] = {
	F(37500000, P_GPLL0_OUT_AUX2, 8, 0, 0),
	F(75000000, P_GPLL0_OUT_AUX2, 4, 0, 0),
	F(150000000, P_GPLL0_OUT_AUX2, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_phy_unipro_core_clk_src = {
	.cmd_rcgr = 0x45060,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_phy_unipro_core_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_phy_unipro_core_clk_src",
		.parent_data = gcc_parents_0,
		.num_parents = ARRAY_SIZE(gcc_parents_0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb30_prim_master_clk_src[] = {
	F(66666667, P_GPLL0_OUT_AUX2, 4.5, 0, 0),
	F(133333333, P_GPLL0_OUT_EARLY, 4.5, 0, 0),
	F(200000000, P_GPLL0_OUT_EARLY, 3, 0, 0),
	F(240000000, P_GPLL0_OUT_EARLY, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb30_prim_master_clk_src = {
	.cmd_rcgr = 0x1a01c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb30_prim_master_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_usb30_prim_master_clk_src",
		.parent_data = gcc_parents_0,
		.num_parents = ARRAY_SIZE(gcc_parents_0),
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
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb30_prim_mock_utmi_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_usb30_prim_mock_utmi_clk_src",
		.parent_data = gcc_parents_0,
		.num_parents = ARRAY_SIZE(gcc_parents_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div gcc_usb30_prim_mock_utmi_postdiv_clk_src = {
	.reg = 0x1a04c,
	.shift = 0,
	.width = 2,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "gcc_usb30_prim_mock_utmi_postdiv_clk_src",
		.parent_hws = (const struct clk_hw *[]) {
			&gcc_usb30_prim_mock_utmi_clk_src.clkr.hw },
		.num_parents = 1,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_rcg2 gcc_usb3_prim_phy_aux_clk_src = {
	.cmd_rcgr = 0x1a060,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_12,
	.freq_tbl = ftbl_gcc_usb30_prim_mock_utmi_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_usb3_prim_phy_aux_clk_src",
		.parent_data = gcc_parents_12,
		.num_parents = ARRAY_SIZE(gcc_parents_12),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_video_venus_clk_src[] = {
	F(133333333, P_GPLL11_OUT_MAIN, 4.5, 0, 0),
	F(240000000, P_GPLL11_OUT_MAIN, 2.5, 0, 0),
	F(300000000, P_GPLL11_OUT_MAIN, 2, 0, 0),
	F(384000000, P_GPLL11_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_video_venus_clk_src = {
	.cmd_rcgr = 0x58060,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_13,
	.freq_tbl = ftbl_gcc_video_venus_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_video_venus_clk_src",
		.parent_data = gcc_parents_13,
		.num_parents = ARRAY_SIZE(gcc_parents_13),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_ahb2phy_csi_clk = {
	.halt_reg = 0x1d004,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x1d004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1d004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ahb2phy_csi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ahb2phy_usb_clk = {
	.halt_reg = 0x1d008,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x1d008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1d008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ahb2phy_usb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_bimc_gpu_axi_clk = {
	.halt_reg = 0x71154,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x71154,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x71154,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_cam_throttle_rt_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_ahb_clk = {
	.halt_reg = 0x17008,
	.halt_check = BRANCH_HALT_DELAY,
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

static struct clk_branch gcc_camera_xo_clk = {
	.halt_reg = 0x17028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camera_xo_clk",
			.flags = CLK_IS_CRITICAL,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_camss_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_camnoc_atb_clk = {
	.halt_reg = 0x5804c,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x5804c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x5804c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_camnoc_atb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_camnoc_nts_xo_clk = {
	.halt_reg = 0x58050,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x58050,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x58050,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_cci_0_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_camss_cci_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_cphy_0_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_camss_tfe_cphy_rx_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_cphy_1_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_camss_tfe_cphy_rx_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_cphy_2_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_camss_tfe_cphy_rx_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_csi0phytimer_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_camss_csi0phytimer_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_csi1phytimer_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_camss_csi1phytimer_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_csi2phytimer_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_camss_csi2phytimer_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_mclk0_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_camss_mclk0_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_mclk1_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_camss_mclk1_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_mclk2_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_camss_mclk2_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_mclk3_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_camss_mclk3_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_ope_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_camss_ope_ahb_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_ope_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_camss_ope_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_tfe_0_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_camss_tfe_0_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_tfe_0_cphy_rx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_camss_tfe_cphy_rx_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_tfe_0_csid_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_camss_tfe_0_csid_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_tfe_1_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_camss_tfe_1_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_tfe_1_cphy_rx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_camss_tfe_cphy_rx_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_tfe_1_csid_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_camss_tfe_1_csid_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_tfe_2_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_camss_tfe_2_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_tfe_2_cphy_rx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_camss_tfe_cphy_rx_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_tfe_2_csid_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_camss_tfe_2_csid_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_top_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_camss_top_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_usb3_prim_axi_clk = {
	.halt_reg = 0x1a084,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x1a084,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1a084,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_cfg_noc_usb3_prim_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_usb30_prim_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cpuss_gnoc_clk = {
	.halt_reg = 0x2b004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2b004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x79004,
		.enable_mask = BIT(22),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_cpuss_gnoc_clk",
			.flags = CLK_IS_CRITICAL,
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

static struct clk_regmap_div gcc_disp_gpll0_clk_src = {
	.reg = 0x17058,
	.shift = 0,
	.width = 2,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "gcc_disp_gpll0_clk_src",
		.parent_hws = (const struct clk_hw *[]){ &gpll0.clkr.hw },
		.num_parents = 1,
		.ops = &clk_regmap_div_ops,
	},
};

static struct clk_branch gcc_disp_gpll0_div_clk_src = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x79004,
		.enable_mask = BIT(20),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_disp_gpll0_div_clk_src",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_disp_gpll0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_hf_axi_clk = {
	.halt_reg = 0x17020,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x17020,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x17020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_disp_hf_axi_clk",
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_disp_throttle_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_xo_clk = {
	.halt_reg = 0x1702c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1702c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_disp_xo_clk",
			.flags = CLK_IS_CRITICAL,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gp1_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_gp1_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gp2_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_gp2_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gp3_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_gp3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_cfg_ahb_clk = {
	.halt_reg = 0x36004,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x36004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x36004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gpu_cfg_ahb_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_gpll0_clk_src = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x79004,
		.enable_mask = BIT(15),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gpu_gpll0_clk_src",
			.parent_hws = (const struct clk_hw *[]){
				&gpll0.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gpu_gpll0_div_clk_src",
			.parent_hws = (const struct clk_hw *[]){
				&gpll0_out_aux2.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_iref_clk = {
	.halt_reg = 0x36100,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x36100,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gpu_iref_clk",
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gpu_memnoc_gfx_clk",
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
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pdm2_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_pdm2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm_ahb_clk = {
	.halt_reg = 0x20004,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x20004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x20004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s0_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_qupv3_wrap0_s0_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s1_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_qupv3_wrap0_s1_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s2_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_qupv3_wrap0_s2_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s3_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_qupv3_wrap0_s3_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s4_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_qupv3_wrap0_s4_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap0_s5_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_qupv3_wrap0_s5_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qupv3_wrap_0_s_ahb_clk",
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
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc1_apps_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_sdcc1_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT /* | CLK_ENABLE_HAND_OFF */,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_ice_core_clk = {
	.halt_reg = 0x3800c,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x3800c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3800c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc1_ice_core_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_sdcc1_ice_core_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc2_apps_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_sdcc2_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sys_noc_cpuss_ahb_clk = {
	.halt_reg = 0x2b06c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2b06c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x79004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sys_noc_cpuss_ahb_clk",
			.flags = CLK_IS_CRITICAL,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sys_noc_ufs_phy_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_ufs_phy_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sys_noc_usb3_prim_axi_clk = {
	.halt_reg = 0x1a080,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x1a080,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1a080,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sys_noc_usb3_prim_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_usb30_prim_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_clkref_clk = {
	.halt_reg = 0x8c000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_clkref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_ahb_clk = {
	.halt_reg = 0x45014,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x45014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x45014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_axi_clk = {
	.halt_reg = 0x45010,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x45010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x45010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_ufs_phy_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_ice_core_clk = {
	.halt_reg = 0x45044,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x45044,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x45044,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_ice_core_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_ufs_phy_ice_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_phy_aux_clk = {
	.halt_reg = 0x45078,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x45078,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x45078,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_phy_aux_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_ufs_phy_phy_aux_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_tx_symbol_0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_unipro_core_clk = {
	.halt_reg = 0x45040,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x45040,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x45040,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_unipro_core_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_ufs_phy_unipro_core_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb30_prim_master_clk",
			.parent_hws = (const struct clk_hw *[]){
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
			.parent_hws = (const struct clk_hw *[]){
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb30_prim_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_clkref_clk = {
	.halt_reg = 0x9f000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9f000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_prim_phy_com_aux_clk",
			.parent_hws = (const struct clk_hw *[]){
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

static struct clk_branch gcc_vcodec0_axi_clk = {
	.halt_reg = 0x6e008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6e008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_venus_ctl_axi_clk",
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
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_axi0_clk = {
	.halt_reg = 0x1701c,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x1701c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1701c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_video_throttle_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_vcodec0_sys_clk = {
	.halt_reg = 0x580a4,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x580a4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x580a4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_video_vcodec0_sys_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_video_venus_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_video_venus_ctl_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_video_venus_clk_src.clkr.hw,
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
		.hw.init = &(struct clk_init_data){
			.name = "gcc_video_xo_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc gcc_camss_top_gdsc = {
	.gdscr = 0x58004,
	.pd = {
		.name = "gcc_camss_top",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc gcc_ufs_phy_gdsc = {
	.gdscr = 0x45004,
	.pd = {
		.name = "gcc_ufs_phy",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc gcc_usb30_prim_gdsc = {
	.gdscr = 0x1a004,
	.pd = {
		.name = "gcc_usb30_prim",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc gcc_vcodec0_gdsc = {
	.gdscr = 0x58098,
	.pd = {
		.name = "gcc_vcodec0",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc gcc_venus_gdsc = {
	.gdscr = 0x5807c,
	.pd = {
		.name = "gcc_venus",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc hlos1_vote_turing_mmu_tbu1_gdsc = {
	.gdscr = 0x7d060,
	.pd = {
		.name = "hlos1_vote_turing_mmu_tbu1",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc hlos1_vote_turing_mmu_tbu0_gdsc = {
	.gdscr = 0x7d07c,
	.pd = {
		.name = "hlos1_vote_turing_mmu_tbu0",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc hlos1_vote_mm_snoc_mmu_tbu_rt_gdsc = {
	.gdscr = 0x7d074,
	.pd = {
		.name = "hlos1_vote_mm_snoc_mmu_tbu_rt",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc hlos1_vote_mm_snoc_mmu_tbu_nrt_gdsc = {
	.gdscr = 0x7d078,
	.pd = {
		.name = "hlos1_vote_mm_snoc_mmu_tbu_nrt",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct clk_regmap *gcc_sm6115_clocks[] = {
	[GCC_AHB2PHY_CSI_CLK] = &gcc_ahb2phy_csi_clk.clkr,
	[GCC_AHB2PHY_USB_CLK] = &gcc_ahb2phy_usb_clk.clkr,
	[GCC_BIMC_GPU_AXI_CLK] = &gcc_bimc_gpu_axi_clk.clkr,
	[GCC_BOOT_ROM_AHB_CLK] = &gcc_boot_rom_ahb_clk.clkr,
	[GCC_CAM_THROTTLE_NRT_CLK] = &gcc_cam_throttle_nrt_clk.clkr,
	[GCC_CAM_THROTTLE_RT_CLK] = &gcc_cam_throttle_rt_clk.clkr,
	[GCC_CAMERA_AHB_CLK] = &gcc_camera_ahb_clk.clkr,
	[GCC_CAMERA_XO_CLK] = &gcc_camera_xo_clk.clkr,
	[GCC_CAMSS_AXI_CLK] = &gcc_camss_axi_clk.clkr,
	[GCC_CAMSS_AXI_CLK_SRC] = &gcc_camss_axi_clk_src.clkr,
	[GCC_CAMSS_CAMNOC_ATB_CLK] = &gcc_camss_camnoc_atb_clk.clkr,
	[GCC_CAMSS_CAMNOC_NTS_XO_CLK] = &gcc_camss_camnoc_nts_xo_clk.clkr,
	[GCC_CAMSS_CCI_0_CLK] = &gcc_camss_cci_0_clk.clkr,
	[GCC_CAMSS_CCI_CLK_SRC] = &gcc_camss_cci_clk_src.clkr,
	[GCC_CAMSS_CPHY_0_CLK] = &gcc_camss_cphy_0_clk.clkr,
	[GCC_CAMSS_CPHY_1_CLK] = &gcc_camss_cphy_1_clk.clkr,
	[GCC_CAMSS_CPHY_2_CLK] = &gcc_camss_cphy_2_clk.clkr,
	[GCC_CAMSS_CSI0PHYTIMER_CLK] = &gcc_camss_csi0phytimer_clk.clkr,
	[GCC_CAMSS_CSI0PHYTIMER_CLK_SRC] = &gcc_camss_csi0phytimer_clk_src.clkr,
	[GCC_CAMSS_CSI1PHYTIMER_CLK] = &gcc_camss_csi1phytimer_clk.clkr,
	[GCC_CAMSS_CSI1PHYTIMER_CLK_SRC] = &gcc_camss_csi1phytimer_clk_src.clkr,
	[GCC_CAMSS_CSI2PHYTIMER_CLK] = &gcc_camss_csi2phytimer_clk.clkr,
	[GCC_CAMSS_CSI2PHYTIMER_CLK_SRC] = &gcc_camss_csi2phytimer_clk_src.clkr,
	[GCC_CAMSS_MCLK0_CLK] = &gcc_camss_mclk0_clk.clkr,
	[GCC_CAMSS_MCLK0_CLK_SRC] = &gcc_camss_mclk0_clk_src.clkr,
	[GCC_CAMSS_MCLK1_CLK] = &gcc_camss_mclk1_clk.clkr,
	[GCC_CAMSS_MCLK1_CLK_SRC] = &gcc_camss_mclk1_clk_src.clkr,
	[GCC_CAMSS_MCLK2_CLK] = &gcc_camss_mclk2_clk.clkr,
	[GCC_CAMSS_MCLK2_CLK_SRC] = &gcc_camss_mclk2_clk_src.clkr,
	[GCC_CAMSS_MCLK3_CLK] = &gcc_camss_mclk3_clk.clkr,
	[GCC_CAMSS_MCLK3_CLK_SRC] = &gcc_camss_mclk3_clk_src.clkr,
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
	[GCC_CPUSS_GNOC_CLK] = &gcc_cpuss_gnoc_clk.clkr,
	[GCC_DISP_AHB_CLK] = &gcc_disp_ahb_clk.clkr,
	[GCC_DISP_GPLL0_CLK_SRC] = &gcc_disp_gpll0_clk_src.clkr,
	[GCC_DISP_GPLL0_DIV_CLK_SRC] = &gcc_disp_gpll0_div_clk_src.clkr,
	[GCC_DISP_HF_AXI_CLK] = &gcc_disp_hf_axi_clk.clkr,
	[GCC_DISP_THROTTLE_CORE_CLK] = &gcc_disp_throttle_core_clk.clkr,
	[GCC_DISP_XO_CLK] = &gcc_disp_xo_clk.clkr,
	[GCC_GP1_CLK] = &gcc_gp1_clk.clkr,
	[GCC_GP1_CLK_SRC] = &gcc_gp1_clk_src.clkr,
	[GCC_GP2_CLK] = &gcc_gp2_clk.clkr,
	[GCC_GP2_CLK_SRC] = &gcc_gp2_clk_src.clkr,
	[GCC_GP3_CLK] = &gcc_gp3_clk.clkr,
	[GCC_GP3_CLK_SRC] = &gcc_gp3_clk_src.clkr,
	[GCC_GPU_CFG_AHB_CLK] = &gcc_gpu_cfg_ahb_clk.clkr,
	[GCC_GPU_GPLL0_CLK_SRC] = &gcc_gpu_gpll0_clk_src.clkr,
	[GCC_GPU_GPLL0_DIV_CLK_SRC] = &gcc_gpu_gpll0_div_clk_src.clkr,
	[GCC_GPU_IREF_CLK] = &gcc_gpu_iref_clk.clkr,
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
	[GCC_QUPV3_WRAP_0_M_AHB_CLK] = &gcc_qupv3_wrap_0_m_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_0_S_AHB_CLK] = &gcc_qupv3_wrap_0_s_ahb_clk.clkr,
	[GCC_SDCC1_AHB_CLK] = &gcc_sdcc1_ahb_clk.clkr,
	[GCC_SDCC1_APPS_CLK] = &gcc_sdcc1_apps_clk.clkr,
	[GCC_SDCC1_APPS_CLK_SRC] = &gcc_sdcc1_apps_clk_src.clkr,
	[GCC_SDCC1_ICE_CORE_CLK] = &gcc_sdcc1_ice_core_clk.clkr,
	[GCC_SDCC1_ICE_CORE_CLK_SRC] = &gcc_sdcc1_ice_core_clk_src.clkr,
	[GCC_SDCC2_AHB_CLK] = &gcc_sdcc2_ahb_clk.clkr,
	[GCC_SDCC2_APPS_CLK] = &gcc_sdcc2_apps_clk.clkr,
	[GCC_SDCC2_APPS_CLK_SRC] = &gcc_sdcc2_apps_clk_src.clkr,
	[GCC_SYS_NOC_CPUSS_AHB_CLK] = &gcc_sys_noc_cpuss_ahb_clk.clkr,
	[GCC_SYS_NOC_UFS_PHY_AXI_CLK] = &gcc_sys_noc_ufs_phy_axi_clk.clkr,
	[GCC_SYS_NOC_USB3_PRIM_AXI_CLK] = &gcc_sys_noc_usb3_prim_axi_clk.clkr,
	[GCC_UFS_CLKREF_CLK] = &gcc_ufs_clkref_clk.clkr,
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
	[GCC_VIDEO_AHB_CLK] = &gcc_video_ahb_clk.clkr,
	[GCC_VIDEO_AXI0_CLK] = &gcc_video_axi0_clk.clkr,
	[GCC_VIDEO_THROTTLE_CORE_CLK] = &gcc_video_throttle_core_clk.clkr,
	[GCC_VIDEO_VCODEC0_SYS_CLK] = &gcc_video_vcodec0_sys_clk.clkr,
	[GCC_VIDEO_VENUS_CLK_SRC] = &gcc_video_venus_clk_src.clkr,
	[GCC_VIDEO_VENUS_CTL_CLK] = &gcc_video_venus_ctl_clk.clkr,
	[GCC_VIDEO_XO_CLK] = &gcc_video_xo_clk.clkr,
	[GPLL0] = &gpll0.clkr,
	[GPLL0_OUT_AUX2] = &gpll0_out_aux2.clkr,
	[GPLL0_OUT_MAIN] = &gpll0_out_main.clkr,
	[GPLL10] = &gpll10.clkr,
	[GPLL10_OUT_MAIN] = &gpll10_out_main.clkr,
	[GPLL11] = &gpll11.clkr,
	[GPLL11_OUT_MAIN] = &gpll11_out_main.clkr,
	[GPLL3] = &gpll3.clkr,
	[GPLL4] = &gpll4.clkr,
	[GPLL4_OUT_MAIN] = &gpll4_out_main.clkr,
	[GPLL6] = &gpll6.clkr,
	[GPLL6_OUT_MAIN] = &gpll6_out_main.clkr,
	[GPLL7] = &gpll7.clkr,
	[GPLL7_OUT_MAIN] = &gpll7_out_main.clkr,
	[GPLL8] = &gpll8.clkr,
	[GPLL8_OUT_MAIN] = &gpll8_out_main.clkr,
	[GPLL9] = &gpll9.clkr,
	[GPLL9_OUT_MAIN] = &gpll9_out_main.clkr,
};

static const struct qcom_reset_map gcc_sm6115_resets[] = {
	[GCC_QUSB2PHY_PRIM_BCR] = { 0x1c000 },
	[GCC_QUSB2PHY_SEC_BCR] = { 0x1c004 },
	[GCC_SDCC1_BCR] = { 0x38000 },
	[GCC_SDCC2_BCR] = { 0x1e000 },
	[GCC_UFS_PHY_BCR] = { 0x45000 },
	[GCC_USB30_PRIM_BCR] = { 0x1a000 },
	[GCC_USB_PHY_CFG_AHB2PHY_BCR] = { 0x1d000 },
	[GCC_USB3PHY_PHY_PRIM_SP0_BCR] = { 0x1b008 },
	[GCC_USB3_PHY_PRIM_SP0_BCR] = { 0x1b000 },
	[GCC_VCODEC0_BCR] = { 0x58094 },
	[GCC_VENUS_BCR] = { 0x58078 },
	[GCC_VIDEO_INTERFACE_BCR] = { 0x6e000 },
};

static struct gdsc *gcc_sm6115_gdscs[] = {
	[GCC_CAMSS_TOP_GDSC] = &gcc_camss_top_gdsc,
	[GCC_UFS_PHY_GDSC] = &gcc_ufs_phy_gdsc,
	[GCC_USB30_PRIM_GDSC] = &gcc_usb30_prim_gdsc,
	[GCC_VCODEC0_GDSC] = &gcc_vcodec0_gdsc,
	[GCC_VENUS_GDSC] = &gcc_venus_gdsc,
	[HLOS1_VOTE_TURING_MMU_TBU1_GDSC] = &hlos1_vote_turing_mmu_tbu1_gdsc,
	[HLOS1_VOTE_TURING_MMU_TBU0_GDSC] = &hlos1_vote_turing_mmu_tbu0_gdsc,
	[HLOS1_VOTE_MM_SNOC_MMU_TBU_RT_GDSC] = &hlos1_vote_mm_snoc_mmu_tbu_rt_gdsc,
	[HLOS1_VOTE_MM_SNOC_MMU_TBU_NRT_GDSC] = &hlos1_vote_mm_snoc_mmu_tbu_nrt_gdsc,
};

static const struct clk_rcg_dfs_data gcc_dfs_clocks[] = {
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s0_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s1_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s2_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s3_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s4_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s5_clk_src),
};

static const struct regmap_config gcc_sm6115_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xc7000,
	.fast_io = true,
};

static const struct qcom_cc_desc gcc_sm6115_desc = {
	.config = &gcc_sm6115_regmap_config,
	.clks = gcc_sm6115_clocks,
	.num_clks = ARRAY_SIZE(gcc_sm6115_clocks),
	.resets = gcc_sm6115_resets,
	.num_resets = ARRAY_SIZE(gcc_sm6115_resets),
	.gdscs = gcc_sm6115_gdscs,
	.num_gdscs = ARRAY_SIZE(gcc_sm6115_gdscs),
};

static const struct of_device_id gcc_sm6115_match_table[] = {
	{ .compatible = "qcom,gcc-sm6115" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_sm6115_match_table);

static int gcc_sm6115_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret;

	regmap = qcom_cc_map(pdev, &gcc_sm6115_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ret = qcom_cc_register_rcg_dfs(regmap, gcc_dfs_clocks,
			ARRAY_SIZE(gcc_dfs_clocks));
	if (ret)
		return ret;

	clk_alpha_pll_configure(&gpll8, regmap, &gpll8_config);
	clk_alpha_pll_configure(&gpll9, regmap, &gpll9_config);
	clk_alpha_pll_configure(&gpll10, regmap, &gpll10_config);
	clk_alpha_pll_configure(&gpll11, regmap, &gpll11_config);

	return qcom_cc_really_probe(pdev, &gcc_sm6115_desc, regmap);
}

static struct platform_driver gcc_sm6115_driver = {
	.probe = gcc_sm6115_probe,
	.driver = {
		.name = "gcc-sm6115",
		.of_match_table = gcc_sm6115_match_table,
	},
};

static int __init gcc_sm6115_init(void)
{
	return platform_driver_register(&gcc_sm6115_driver);
}
subsys_initcall(gcc_sm6115_init);

static void __exit gcc_sm6115_exit(void)
{
	platform_driver_unregister(&gcc_sm6115_driver);
}
module_exit(gcc_sm6115_exit);

MODULE_DESCRIPTION("QTI GCC SM6115 and SM4250 Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:gcc-sm6115");
