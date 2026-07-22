// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,shikra-gcc.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "clk-regmap-phy-mux.h"
#include "common.h"
#include "gdsc.h"
#include "reset.h"

enum {
	DT_BI_TCXO,
	DT_SLEEP_CLK,
	DT_EMAC0_SGMIIPHY_RCLK,
	DT_EMAC0_SGMIIPHY_TCLK,
	DT_EMAC1_SGMIIPHY_RCLK,
	DT_EMAC1_SGMIIPHY_TCLK,
	DT_PCIE_PIPE_CLK,
	DT_USB3_PHY_WRAPPER_GCC_USB30_PIPE_CLK,
};

enum {
	P_BI_TCXO,
	P_EMAC0_SGMIIPHY_RCLK,
	P_EMAC0_SGMIIPHY_TCLK,
	P_EMAC1_SGMIIPHY_RCLK,
	P_EMAC1_SGMIIPHY_TCLK,
	P_GPLL0_OUT_AUX2,
	P_GPLL0_OUT_EARLY,
	P_GPLL10_OUT_MAIN,
	P_GPLL11_OUT_AUX,
	P_GPLL11_OUT_AUX2,
	P_GPLL11_OUT_MAIN,
	P_GPLL12_OUT_AUX2,
	P_GPLL12_OUT_EARLY,
	P_GPLL3_OUT_EARLY,
	P_GPLL3_OUT_MAIN,
	P_GPLL4_OUT_MAIN,
	P_GPLL5_OUT_MAIN,
	P_GPLL6_OUT_EARLY,
	P_GPLL6_OUT_MAIN,
	P_GPLL7_OUT_MAIN,
	P_GPLL8_OUT_EARLY,
	P_GPLL8_OUT_MAIN,
	P_GPLL9_OUT_EARLY,
	P_GPLL9_OUT_MAIN,
	P_PCIE_PIPE_CLK,
	P_SLEEP_CLK,
	P_USB3_PHY_WRAPPER_GCC_USB30_PIPE_CLK,
};

static const struct pll_vco brammo_vco[] = {
	{ 500000000, 1250000000, 0 },
};

static const struct pll_vco default_vco[] = {
	{ 500000000, 1000000000, 2 },
};

static const struct pll_vco spark_vco[] = {
	{ 750000000, 1500000000, 1 },
};

static struct clk_alpha_pll gpll0 = {
	.offset = 0x0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpll0",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_ops,
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
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpll0_out_aux2",
		.parent_hws = (const struct clk_hw*[]) {
			&gpll0.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ro_ops,
	},
};

/* 1152.0 MHz Configuration */
static const struct alpha_pll_config gpll10_config = {
	.l = 0x3c,
	.alpha = 0x0,
	.vco_val = BIT(20),
	.vco_mask = GENMASK(21, 20),
	.main_output_mask = BIT(0),
	.config_ctl_val = 0x4001055b,
	.test_ctl_hi1_val = 0x1,
};

static struct clk_alpha_pll gpll10 = {
	.offset = 0xa000,
	.config = &gpll10_config,
	.vco_table = spark_vco,
	.num_vco = ARRAY_SIZE(spark_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpll10",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

/* 600.0 MHz Configuration */
static const struct alpha_pll_config gpll11_config = {
	.l = 0x1f,
	.alpha = 0x0,
	.alpha_hi = 0x40,
	.alpha_en_mask = BIT(24),
	.vco_val = BIT(21),
	.vco_mask = GENMASK(21, 20),
	.main_output_mask = BIT(0),
	.config_ctl_val = 0x4001055b,
	.test_ctl_hi1_val = 0x1,
};

static struct clk_alpha_pll gpll11 = {
	.offset = 0xb000,
	.config = &gpll11_config,
	.vco_table = default_vco,
	.num_vco = ARRAY_SIZE(default_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.flags = SUPPORTS_DYNAMIC_UPDATE,
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(11),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpll11",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct clk_alpha_pll gpll12 = {
	.offset = 0xc000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(12),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpll12",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_ops,
		},
	},
};

static const struct clk_div_table post_div_table_gpll12_out_aux2[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll12_out_aux2 = {
	.offset = 0xc000,
	.post_div_shift = 8,
	.post_div_table = post_div_table_gpll12_out_aux2,
	.num_post_div = ARRAY_SIZE(post_div_table_gpll12_out_aux2),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpll12_out_aux2",
		.parent_hws = (const struct clk_hw*[]) {
			&gpll12.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ro_ops,
	},
};

static struct clk_alpha_pll gpll3 = {
	.offset = 0x3000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(3),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpll3",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_ops,
		},
	},
};

static const struct clk_div_table post_div_table_gpll3_out_main[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll3_out_main = {
	.offset = 0x3000,
	.post_div_shift = 8,
	.post_div_table = post_div_table_gpll3_out_main,
	.num_post_div = ARRAY_SIZE(post_div_table_gpll3_out_main),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpll3_out_main",
		.parent_hws = (const struct clk_hw*[]) {
			&gpll3.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ro_ops,
	},
};

static struct clk_alpha_pll gpll4 = {
	.offset = 0x4000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpll4",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_ops,
		},
	},
};

static struct clk_alpha_pll gpll5 = {
	.offset = 0x5000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpll5",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_ops,
		},
	},
};

static struct clk_alpha_pll gpll6 = {
	.offset = 0x6000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(6),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpll6",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_ops,
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
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpll6_out_main",
		.parent_hws = (const struct clk_hw*[]) {
			&gpll6.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ro_ops,
	},
};

static struct clk_alpha_pll gpll7 = {
	.offset = 0x7000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpll7",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_ops,
		},
	},
};

/* 533.2 MHz Configuration */
static const struct alpha_pll_config gpll8_config = {
	.l = 0x1b,
	.alpha = 0x55555555,
	.alpha_hi = 0xc5,
	.alpha_en_mask = BIT(24),
	.vco_val = BIT(21),
	.vco_mask = GENMASK(21, 20),
	.main_output_mask = BIT(0),
	.early_output_mask = BIT(3),
	.post_div_val = BIT(8),
	.post_div_mask = GENMASK(11, 8),
	.config_ctl_val = 0x4001055b,
	.test_ctl_hi1_val = 0x1,
};

static struct clk_alpha_pll gpll8 = {
	.offset = 0x8000,
	.config = &gpll8_config,
	.vco_table = default_vco,
	.num_vco = ARRAY_SIZE(default_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.flags = SUPPORTS_DYNAMIC_UPDATE,
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpll8",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
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
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpll8_out_main",
		.parent_hws = (const struct clk_hw*[]) {
			&gpll8.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_ro_ops,
	},
};

/* 1152.0 MHz Configuration */
static const struct alpha_pll_config gpll9_config = {
	.l = 0x3c,
	.alpha = 0x0,
	.post_div_val = BIT(8),
	.post_div_mask = GENMASK(9, 8),
	.main_output_mask = BIT(0),
	.early_output_mask = BIT(3),
	.config_ctl_val = 0x00004289,
	.test_ctl_val = 0x08000000,
};

static struct clk_alpha_pll gpll9 = {
	.offset = 0x9000,
	.config = &gpll9_config,
	.vco_table = brammo_vco,
	.num_vco = ARRAY_SIZE(brammo_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_BRAMMO_EVO],
	.clkr = {
		.enable_reg = 0x79000,
		.enable_mask = BIT(9),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpll9",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
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
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpll9_out_main",
		.parent_hws = (const struct clk_hw*[]) {
			&gpll9.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_ro_ops,
	},
};

static const struct parent_map gcc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL0_OUT_AUX2, 2 },
};

static const struct clk_parent_data gcc_parent_data_0[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_aux2.clkr.hw },
};

static const struct parent_map gcc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL0_OUT_AUX2, 2 },
	{ P_GPLL6_OUT_MAIN, 4 },
};

static const struct clk_parent_data gcc_parent_data_1[] = {
	{ .index = DT_BI_TCXO },
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

static const struct clk_parent_data gcc_parent_data_2[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_aux2.clkr.hw },
	{ .index = DT_SLEEP_CLK },
};

static const struct parent_map gcc_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL9_OUT_EARLY, 2 },
	{ P_GPLL10_OUT_MAIN, 3 },
	{ P_GPLL9_OUT_MAIN, 5 },
	{ P_GPLL3_OUT_MAIN, 6 },
};

static const struct clk_parent_data gcc_parent_data_3[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll9.clkr.hw },
	{ .hw = &gpll10.clkr.hw },
	{ .hw = &gpll9_out_main.clkr.hw },
	{ .hw = &gpll3_out_main.clkr.hw },
};

static const struct parent_map gcc_parent_map_4[] = {
	{ P_BI_TCXO, 0 },
	{ P_SLEEP_CLK, 5 },
};

static const struct clk_parent_data gcc_parent_data_4[] = {
	{ .index = DT_BI_TCXO },
	{ .index = DT_SLEEP_CLK },
};

static const struct parent_map gcc_parent_map_5[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL0_OUT_AUX2, 2 },
	{ P_GPLL10_OUT_MAIN, 3 },
	{ P_GPLL4_OUT_MAIN, 5 },
	{ P_GPLL3_OUT_EARLY, 6 },
};

static const struct clk_parent_data gcc_parent_data_5[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_aux2.clkr.hw },
	{ .hw = &gpll10.clkr.hw },
	{ .hw = &gpll4.clkr.hw },
	{ .hw = &gpll3.clkr.hw },
};

static const struct parent_map gcc_parent_map_6[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL0_OUT_AUX2, 2 },
	{ P_GPLL4_OUT_MAIN, 5 },
	{ P_GPLL3_OUT_MAIN, 6 },
};

static const struct clk_parent_data gcc_parent_data_6[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_aux2.clkr.hw },
	{ .hw = &gpll4.clkr.hw },
	{ .hw = &gpll3_out_main.clkr.hw },
};

static const struct parent_map gcc_parent_map_7[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL8_OUT_EARLY, 2 },
	{ P_GPLL10_OUT_MAIN, 3 },
	{ P_GPLL8_OUT_MAIN, 4 },
	{ P_GPLL9_OUT_MAIN, 5 },
	{ P_GPLL3_OUT_EARLY, 6 },
};

static const struct clk_parent_data gcc_parent_data_7[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll8.clkr.hw },
	{ .hw = &gpll10.clkr.hw },
	{ .hw = &gpll8_out_main.clkr.hw },
	{ .hw = &gpll9.clkr.hw },
	{ .hw = &gpll3.clkr.hw },
};

static const struct parent_map gcc_parent_map_8[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL8_OUT_EARLY, 2 },
	{ P_GPLL10_OUT_MAIN, 3 },
	{ P_GPLL8_OUT_MAIN, 4 },
	{ P_GPLL9_OUT_MAIN, 5 },
	{ P_GPLL3_OUT_MAIN, 6 },
};

static const struct clk_parent_data gcc_parent_data_8[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll8.clkr.hw },
	{ .hw = &gpll10.clkr.hw },
	{ .hw = &gpll8_out_main.clkr.hw },
	{ .hw = &gpll9.clkr.hw },
	{ .hw = &gpll3_out_main.clkr.hw },
};

static const struct parent_map gcc_parent_map_9[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL8_OUT_EARLY, 2 },
	{ P_GPLL10_OUT_MAIN, 3 },
	{ P_GPLL6_OUT_MAIN, 4 },
	{ P_GPLL9_OUT_MAIN, 5 },
	{ P_GPLL3_OUT_EARLY, 6 },
};

static const struct clk_parent_data gcc_parent_data_9[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll8.clkr.hw },
	{ .hw = &gpll10.clkr.hw },
	{ .hw = &gpll6_out_main.clkr.hw },
	{ .hw = &gpll9.clkr.hw },
	{ .hw = &gpll3.clkr.hw },
};

static const struct parent_map gcc_parent_map_10[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_SLEEP_CLK, 5 },
};

static const struct clk_parent_data gcc_parent_data_10[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll0.clkr.hw },
	{ .index = DT_SLEEP_CLK },
};

static const struct parent_map gcc_parent_map_11[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL12_OUT_EARLY, 1 },
	{ P_GPLL12_OUT_AUX2, 4 },
	{ P_GPLL3_OUT_EARLY, 6 },
};

static const struct clk_parent_data gcc_parent_data_11[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll12.clkr.hw },
	{ .hw = &gpll12_out_aux2.clkr.hw },
	{ .hw = &gpll3.clkr.hw },
};

static const struct parent_map gcc_parent_map_12[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL12_OUT_EARLY, 1 },
	{ P_GPLL0_OUT_AUX2, 2 },
	{ P_GPLL12_OUT_AUX2, 4 },
};

static const struct clk_parent_data gcc_parent_data_12[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll12.clkr.hw },
	{ .hw = &gpll0_out_aux2.clkr.hw },
	{ .hw = &gpll12_out_aux2.clkr.hw },
};

static const struct parent_map gcc_parent_map_13[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
};

static const struct clk_parent_data gcc_parent_data_13[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll0.clkr.hw },
};

static const struct parent_map gcc_parent_map_14[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL0_OUT_AUX2, 2 },
	{ P_GPLL10_OUT_MAIN, 3 },
	{ P_GPLL8_OUT_MAIN, 4 },
	{ P_GPLL9_OUT_MAIN, 5 },
	{ P_GPLL3_OUT_EARLY, 6 },
};

static const struct clk_parent_data gcc_parent_data_14[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_aux2.clkr.hw },
	{ .hw = &gpll10.clkr.hw },
	{ .hw = &gpll8_out_main.clkr.hw },
	{ .hw = &gpll9.clkr.hw },
	{ .hw = &gpll3.clkr.hw },
};

static const struct parent_map gcc_parent_map_15[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL8_OUT_EARLY, 2 },
	{ P_GPLL10_OUT_MAIN, 3 },
	{ P_GPLL6_OUT_EARLY, 5 },
	{ P_GPLL3_OUT_MAIN, 6 },
};

static const struct clk_parent_data gcc_parent_data_15[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll8.clkr.hw },
	{ .hw = &gpll10.clkr.hw },
	{ .hw = &gpll6.clkr.hw },
	{ .hw = &gpll3_out_main.clkr.hw },
};

static const struct parent_map gcc_parent_map_21[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL0_OUT_AUX2, 2 },
	{ P_GPLL7_OUT_MAIN, 3 },
	{ P_GPLL4_OUT_MAIN, 5 },
};

static const struct clk_parent_data gcc_parent_data_21[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_aux2.clkr.hw },
	{ .hw = &gpll7.clkr.hw },
	{ .hw = &gpll4.clkr.hw },
};

static const struct parent_map gcc_parent_map_22[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL12_OUT_EARLY, 1 },
	{ P_GPLL5_OUT_MAIN, 3 },
	{ P_GPLL12_OUT_AUX2, 4 },
	{ P_GPLL3_OUT_EARLY, 6 },
};

static const struct clk_parent_data gcc_parent_data_22[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll12.clkr.hw },
	{ .hw = &gpll5.clkr.hw },
	{ .hw = &gpll12_out_aux2.clkr.hw },
	{ .hw = &gpll3.clkr.hw },
};

static const struct parent_map gcc_parent_map_24[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL11_OUT_MAIN, 1 },
	{ P_GPLL11_OUT_AUX, 2 },
	{ P_GPLL11_OUT_AUX2, 3 },
};

static const struct clk_parent_data gcc_parent_data_24[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll11.clkr.hw },
	{ .hw = &gpll11.clkr.hw },
	{ .hw = &gpll11.clkr.hw },
};

static struct clk_regmap_phy_mux gcc_emac0_cc_sgmiiphy_rx_clk_src = {
	.reg = 0xad048,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_cc_sgmiiphy_rx_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_EMAC0_SGMIIPHY_RCLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_emac0_cc_sgmiiphy_tx_clk_src = {
	.reg = 0xad040,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_cc_sgmiiphy_tx_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_EMAC0_SGMIIPHY_TCLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_emac1_cc_sgmiiphy_rx_clk_src = {
	.reg = 0xae048,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_cc_sgmiiphy_rx_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_EMAC1_SGMIIPHY_RCLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_emac1_cc_sgmiiphy_tx_clk_src = {
	.reg = 0xae040,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_cc_sgmiiphy_tx_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_EMAC1_SGMIIPHY_TCLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_pcie_pipe_clk_src = {
	.reg = 0xaf058,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_PCIE_PIPE_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_usb3_prim_phy_pipe_clk_src = {
	.reg = 0x1a05c,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_prim_phy_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_USB3_PHY_WRAPPER_GCC_USB30_PIPE_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
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
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_camss_axi_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_camss_axi_clk_src",
		.parent_data = gcc_parent_data_5,
		.num_parents = ARRAY_SIZE(gcc_parent_data_5),
		.ops = &clk_rcg2_shared_ops,
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
	.parent_map = gcc_parent_map_14,
	.freq_tbl = ftbl_gcc_camss_cci_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_camss_cci_clk_src",
		.parent_data = gcc_parent_data_14,
		.num_parents = ARRAY_SIZE(gcc_parent_data_14),
		.ops = &clk_rcg2_shared_ops,
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
	.cmd_rcgr = 0x45000,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_6,
	.freq_tbl = ftbl_gcc_camss_csi0phytimer_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_camss_csi0phytimer_clk_src",
		.parent_data = gcc_parent_data_6,
		.num_parents = ARRAY_SIZE(gcc_parent_data_6),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_camss_csi1phytimer_clk_src = {
	.cmd_rcgr = 0x4501c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_6,
	.freq_tbl = ftbl_gcc_camss_csi0phytimer_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_camss_csi1phytimer_clk_src",
		.parent_data = gcc_parent_data_6,
		.num_parents = ARRAY_SIZE(gcc_parent_data_6),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_camss_mclk0_clk_src[] = {
	F(19200000, P_GPLL9_OUT_EARLY, 1, 1, 60),
	F(24000000, P_GPLL9_OUT_MAIN, 1, 1, 24),
	F(64000000, P_GPLL9_OUT_EARLY, 9, 1, 2),
	{ }
};

static struct clk_rcg2 gcc_camss_mclk0_clk_src = {
	.cmd_rcgr = 0x51000,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_camss_mclk0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_camss_mclk0_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_camss_mclk1_clk_src = {
	.cmd_rcgr = 0x5101c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_camss_mclk0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_camss_mclk1_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_camss_mclk2_clk_src = {
	.cmd_rcgr = 0x51038,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_camss_mclk0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_camss_mclk2_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_camss_mclk3_clk_src = {
	.cmd_rcgr = 0x51054,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_camss_mclk0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_camss_mclk3_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
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
	.parent_map = gcc_parent_map_7,
	.freq_tbl = ftbl_gcc_camss_ope_ahb_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_camss_ope_ahb_clk_src",
		.parent_data = gcc_parent_data_7,
		.num_parents = ARRAY_SIZE(gcc_parent_data_7),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_camss_ope_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(200000000, P_GPLL8_OUT_MAIN, 2, 0, 0),
	F(266600000, P_GPLL8_OUT_MAIN, 1, 0, 0),
	F(465000000, P_GPLL8_OUT_MAIN, 1, 0, 0),
	F(580000000, P_GPLL8_OUT_EARLY, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_ope_clk_src = {
	.cmd_rcgr = 0x55004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_7,
	.freq_tbl = ftbl_gcc_camss_ope_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_camss_ope_clk_src",
		.parent_data = gcc_parent_data_7,
		.num_parents = ARRAY_SIZE(gcc_parent_data_7),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
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
	.parent_map = gcc_parent_map_8,
	.freq_tbl = ftbl_gcc_camss_tfe_0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_camss_tfe_0_clk_src",
		.parent_data = gcc_parent_data_8,
		.num_parents = ARRAY_SIZE(gcc_parent_data_8),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
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
	.parent_map = gcc_parent_map_9,
	.freq_tbl = ftbl_gcc_camss_tfe_0_csid_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_camss_tfe_0_csid_clk_src",
		.parent_data = gcc_parent_data_9,
		.num_parents = ARRAY_SIZE(gcc_parent_data_9),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_camss_tfe_1_clk_src = {
	.cmd_rcgr = 0x52024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_8,
	.freq_tbl = ftbl_gcc_camss_tfe_0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_camss_tfe_1_clk_src",
		.parent_data = gcc_parent_data_8,
		.num_parents = ARRAY_SIZE(gcc_parent_data_8),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_camss_tfe_1_csid_clk_src = {
	.cmd_rcgr = 0x520b4,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_9,
	.freq_tbl = ftbl_gcc_camss_tfe_0_csid_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_camss_tfe_1_csid_clk_src",
		.parent_data = gcc_parent_data_9,
		.num_parents = ARRAY_SIZE(gcc_parent_data_9),
		.ops = &clk_rcg2_shared_ops,
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
	.parent_map = gcc_parent_map_15,
	.freq_tbl = ftbl_gcc_camss_tfe_cphy_rx_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_camss_tfe_cphy_rx_clk_src",
		.parent_data = gcc_parent_data_15,
		.num_parents = ARRAY_SIZE(gcc_parent_data_15),
		.ops = &clk_rcg2_shared_ops,
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
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_camss_top_ahb_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_camss_top_ahb_clk_src",
		.parent_data = gcc_parent_data_5,
		.num_parents = ARRAY_SIZE(gcc_parent_data_5),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_emac0_axi_clk_src[] = {
	F(75000000, P_GPLL0_OUT_AUX2, 4, 0, 0),
	F(120000000, P_GPLL0_OUT_AUX2, 2.5, 0, 0),
	F(150000000, P_GPLL0_OUT_AUX2, 2, 0, 0),
	F(200000000, P_GPLL0_OUT_AUX2, 1.5, 0, 0),
	F(240000000, P_GPLL0_OUT_EARLY, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_emac0_axi_clk_src = {
	.cmd_rcgr = 0x109dc,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_emac0_axi_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_emac0_axi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_emac0_phy_aux_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_emac0_phy_aux_clk_src = {
	.cmd_rcgr = 0xad01c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_10,
	.freq_tbl = ftbl_gcc_emac0_phy_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_emac0_phy_aux_clk_src",
		.parent_data = gcc_parent_data_10,
		.num_parents = ARRAY_SIZE(gcc_parent_data_10),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_emac0_ptp_clk_src[] = {
	F(250000000, P_GPLL12_OUT_AUX2, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_emac0_ptp_clk_src = {
	.cmd_rcgr = 0xad064,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_11,
	.freq_tbl = ftbl_gcc_emac0_ptp_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_emac0_ptp_clk_src",
		.parent_data = gcc_parent_data_11,
		.num_parents = ARRAY_SIZE(gcc_parent_data_11),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_emac0_rgmii_clk_src[] = {
	F(2500000, P_GPLL0_OUT_AUX2, 10, 1, 12),
	F(25000000, P_GPLL0_OUT_AUX2, 12, 0, 0),
	F(50000000, P_GPLL0_OUT_AUX2, 6, 0, 0),
	F(125000000, P_GPLL12_OUT_AUX2, 4, 0, 0),
	F(250000000, P_GPLL12_OUT_EARLY, 4, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_emac0_rgmii_clk_src = {
	.cmd_rcgr = 0xad04c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_12,
	.freq_tbl = ftbl_gcc_emac0_rgmii_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_emac0_rgmii_clk_src",
		.parent_data = gcc_parent_data_12,
		.num_parents = ARRAY_SIZE(gcc_parent_data_12),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_emac1_axi_clk_src = {
	.cmd_rcgr = 0x109fc,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_emac0_axi_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_emac1_axi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_emac1_phy_aux_clk_src = {
	.cmd_rcgr = 0xae01c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_10,
	.freq_tbl = ftbl_gcc_emac0_phy_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_emac1_phy_aux_clk_src",
		.parent_data = gcc_parent_data_10,
		.num_parents = ARRAY_SIZE(gcc_parent_data_10),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_emac1_ptp_clk_src = {
	.cmd_rcgr = 0xae064,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_11,
	.freq_tbl = ftbl_gcc_emac0_ptp_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_emac1_ptp_clk_src",
		.parent_data = gcc_parent_data_11,
		.num_parents = ARRAY_SIZE(gcc_parent_data_11),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_emac1_rgmii_clk_src = {
	.cmd_rcgr = 0xae04c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_12,
	.freq_tbl = ftbl_gcc_emac0_rgmii_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_emac1_rgmii_clk_src",
		.parent_data = gcc_parent_data_12,
		.num_parents = ARRAY_SIZE(gcc_parent_data_12),
		.ops = &clk_rcg2_shared_ops,
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
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp1_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_gp2_clk_src = {
	.cmd_rcgr = 0x4e004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp2_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_gp3_clk_src = {
	.cmd_rcgr = 0x4f004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp3_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_aux_clk_src = {
	.cmd_rcgr = 0xaf074,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_emac0_phy_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_aux_clk_src",
		.parent_data = gcc_parent_data_4,
		.num_parents = ARRAY_SIZE(gcc_parent_data_4),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_aux_phy_clk_src = {
	.cmd_rcgr = 0xaf05c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_emac0_phy_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_aux_phy_clk_src",
		.parent_data = gcc_parent_data_4,
		.num_parents = ARRAY_SIZE(gcc_parent_data_4),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcie_rchng_phy_clk_src[] = {
	F(100000000, P_GPLL0_OUT_AUX2, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie_rchng_phy_clk_src = {
	.cmd_rcgr = 0xaf028,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_rchng_phy_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_rchng_phy_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
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
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pdm2_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
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
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s0_clk_src = {
	.cmd_rcgr = 0x1f148,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &gcc_qupv3_wrap0_s0_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s1_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s1_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s1_clk_src = {
	.cmd_rcgr = 0x1f278,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &gcc_qupv3_wrap0_s1_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s2_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s2_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s2_clk_src = {
	.cmd_rcgr = 0x1f3a8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &gcc_qupv3_wrap0_s2_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s3_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s3_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s3_clk_src = {
	.cmd_rcgr = 0x1f4d8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &gcc_qupv3_wrap0_s3_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s4_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s4_clk_src = {
	.cmd_rcgr = 0x1f608,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &gcc_qupv3_wrap0_s4_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s5_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s5_clk_src = {
	.cmd_rcgr = 0x1f738,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &gcc_qupv3_wrap0_s5_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s6_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s6_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s6_clk_src = {
	.cmd_rcgr = 0x1f868,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &gcc_qupv3_wrap0_s6_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s7_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s7_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s7_clk_src = {
	.cmd_rcgr = 0x1f998,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &gcc_qupv3_wrap0_s7_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s8_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s8_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s8_clk_src = {
	.cmd_rcgr = 0x1fac8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &gcc_qupv3_wrap0_s8_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s9_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s9_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s9_clk_src = {
	.cmd_rcgr = 0x1fbf8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &gcc_qupv3_wrap0_s9_clk_src_init,
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
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_sdcc1_apps_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_shared_floor_ops,
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
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_sdcc1_ice_core_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_floor_ops,
	},
};

static const struct freq_tbl ftbl_gcc_sdcc2_apps_clk_src[] = {
	F(400000, P_BI_TCXO, 12, 1, 4),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(25000000, P_GPLL0_OUT_AUX2, 12, 0, 0),
	F(50000000, P_GPLL0_OUT_AUX2, 6, 0, 0),
	F(100000000, P_GPLL0_OUT_AUX2, 3, 0, 0),
	F(202000000, P_GPLL7_OUT_MAIN, 4, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc2_apps_clk_src = {
	.cmd_rcgr = 0x1e00c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_21,
	.freq_tbl = ftbl_gcc_sdcc2_apps_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_sdcc2_apps_clk_src",
		.parent_data = gcc_parent_data_21,
		.num_parents = ARRAY_SIZE(gcc_parent_data_21),
		.ops = &clk_rcg2_shared_floor_ops,
	},
};

static struct clk_rcg2 gcc_tscss_clk_src = {
	.cmd_rcgr = 0xac004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_22,
	.freq_tbl = ftbl_gcc_emac0_ptp_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_tscss_clk_src",
		.parent_data = gcc_parent_data_22,
		.num_parents = ARRAY_SIZE(gcc_parent_data_22),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb20_master_clk_src[] = {
	F(60000000, P_GPLL0_OUT_AUX2, 5, 0, 0),
	F(120000000, P_GPLL0_OUT_EARLY, 5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb20_master_clk_src = {
	.cmd_rcgr = 0xb003c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb20_master_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb20_master_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_usb20_mock_utmi_clk_src = {
	.cmd_rcgr = 0xb0020,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_13,
	.freq_tbl = ftbl_gcc_emac0_phy_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb20_mock_utmi_clk_src",
		.parent_data = gcc_parent_data_13,
		.num_parents = ARRAY_SIZE(gcc_parent_data_13),
		.ops = &clk_rcg2_shared_ops,
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
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_prim_master_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_usb30_prim_mock_utmi_clk_src = {
	.cmd_rcgr = 0x1a034,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_emac0_phy_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_prim_mock_utmi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_usb3_prim_phy_aux_clk_src = {
	.cmd_rcgr = 0x1a060,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_emac0_phy_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb3_prim_phy_aux_clk_src",
		.parent_data = gcc_parent_data_4,
		.num_parents = ARRAY_SIZE(gcc_parent_data_4),
		.ops = &clk_rcg2_shared_ops,
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
	.cmd_rcgr = 0x6d000,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_24,
	.freq_tbl = ftbl_gcc_video_venus_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_video_venus_clk_src",
		.parent_data = gcc_parent_data_24,
		.num_parents = ARRAY_SIZE(gcc_parent_data_24),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_regmap_div gcc_disp_gpll0_clk_src = {
	.reg = 0x17058,
	.shift = 0,
	.width = 2,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_disp_gpll0_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gpll0.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_regmap_div_ops,
	},
};

static struct clk_regmap_div gcc_usb20_mock_utmi_postdiv_clk_src = {
	.reg = 0xb0038,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb20_mock_utmi_postdiv_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_usb20_mock_utmi_clk_src.clkr.hw,
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
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_usb30_prim_mock_utmi_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch gcc_ahb2phy_csi_clk = {
	.halt_reg = 0x1d004,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x1d004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1d004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ahb2phy_usb_clk",
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
		.enable_reg = 0x7900c,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
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
		.enable_mask = BIT(16),
		.hw.init = &(const struct clk_init_data) {
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
		.enable_mask = BIT(15),
		.hw.init = &(const struct clk_init_data) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camss_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camss_camnoc_atb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_camnoc_dragonlink_atb_clk = {
	.halt_reg = 0x58060,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x58060,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x58060,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camss_camnoc_dragonlink_atb_clk",
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
		.hw.init = &(const struct clk_init_data) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camss_cci_0_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camss_cphy_0_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camss_cphy_1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_camss_tfe_cphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi0phytimer_clk = {
	.halt_reg = 0x45018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x45018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camss_csi0phytimer_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_camss_csi0phytimer_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi1phytimer_clk = {
	.halt_reg = 0x45034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x45034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camss_csi1phytimer_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_camss_csi1phytimer_clk_src.clkr.hw,
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camss_mclk0_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camss_mclk1_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camss_mclk2_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camss_mclk3_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.hw.init = &(const struct clk_init_data) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camss_ope_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camss_ope_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.hw.init = &(const struct clk_init_data) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camss_tfe_0_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camss_tfe_0_cphy_rx_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camss_tfe_0_csid_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camss_tfe_1_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camss_tfe_1_cphy_rx_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camss_tfe_1_csid_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_camss_tfe_1_csid_clk_src.clkr.hw,
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camss_top_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_camss_top_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_usb2_prim_axi_clk = {
	.halt_reg = 0x111c4,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x111c4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x111c4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cfg_noc_usb2_prim_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb20_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_usb3_prim_axi_clk = {
	.halt_reg = 0x1a07c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x1a07c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1a07c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cfg_noc_usb3_prim_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb30_prim_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ddrss_gpu_axi_clk = {
	.halt_reg = 0x71000,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x71000,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x71000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ddrss_gpu_axi_clk",
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch gcc_ddrss_memnoc_pcie_sf_clk = {
	.halt_reg = 0x29044,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x29044,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x29044,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ddrss_memnoc_pcie_sf_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_gpll0_div_clk_src = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x79004,
		.enable_mask = BIT(11),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_disp_gpll0_div_clk_src",
			.parent_hws = (const struct clk_hw*[]) {
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
		.hw.init = &(const struct clk_init_data) {
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
		.enable_reg = 0x79004,
		.enable_mask = BIT(13),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_disp_throttle_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_ahb_clk = {
	.halt_reg = 0xad010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xad010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xad010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_axi_clk = {
	.halt_reg = 0xad014,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xad014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xad014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac0_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_axi_sys_noc_clk = {
	.halt_reg = 0x109d4,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x109d4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x109d4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_axi_sys_noc_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac0_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_cc_sgmiiphy_rx_clk = {
	.halt_reg = 0xad044,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0xad044,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_cc_sgmiiphy_rx_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac0_cc_sgmiiphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_cc_sgmiiphy_tx_clk = {
	.halt_reg = 0xad03c,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0xad03c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xad03c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_cc_sgmiiphy_tx_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac0_cc_sgmiiphy_tx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_phy_aux_clk = {
	.halt_reg = 0xad018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xad018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac0_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_ptp_clk = {
	.halt_reg = 0xad034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xad034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_ptp_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac0_ptp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_rgmii_clk = {
	.halt_reg = 0xad038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xad038,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_rgmii_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac0_rgmii_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac1_ahb_clk = {
	.halt_reg = 0xae010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xae010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xae010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac1_axi_clk = {
	.halt_reg = 0xae014,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xae014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xae014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac1_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac1_axi_sys_noc_clk = {
	.halt_reg = 0x109f4,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x109f4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x109f4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_axi_sys_noc_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac1_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac1_cc_sgmiiphy_rx_clk = {
	.halt_reg = 0xae044,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0xae044,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_cc_sgmiiphy_rx_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac1_cc_sgmiiphy_rx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac1_cc_sgmiiphy_tx_clk = {
	.halt_reg = 0xae03c,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0xae03c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xae03c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_cc_sgmiiphy_tx_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac1_cc_sgmiiphy_tx_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac1_phy_aux_clk = {
	.halt_reg = 0xae018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xae018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac1_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac1_ptp_clk = {
	.halt_reg = 0xae034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xae034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_ptp_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac1_ptp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac1_rgmii_clk = {
	.halt_reg = 0xae038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xae038,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_rgmii_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac1_rgmii_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gp1_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gp2_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gp3_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_gp3_clk_src.clkr.hw,
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
		.enable_reg = 0x7900c,
		.enable_mask = BIT(18),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpu_gpll0_clk_src",
			.parent_hws = (const struct clk_hw*[]) {
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
		.enable_reg = 0x7900c,
		.enable_mask = BIT(19),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpu_gpll0_div_clk_src",
			.parent_hws = (const struct clk_hw*[]) {
				&gpll0_out_aux2.clkr.hw,
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpu_memnoc_gfx_clk",
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch gcc_gpu_smmu_vote_clk = {
	.halt_reg = 0x7d000,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpu_smmu_vote_clk",
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
		.hw.init = &(const struct clk_init_data) {
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
		.enable_reg = 0x7900c,
		.enable_mask = BIT(21),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpu_throttle_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mmu_tcu_vote_clk = {
	.halt_reg = 0x7d06c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d06c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_mmu_tcu_vote_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_aux_clk = {
	.halt_reg = 0xaf044,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xaf044,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x79018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_cfg_ahb_clk = {
	.halt_reg = 0xaf010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xaf010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(27),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_clkref_en = {
	.halt_reg = 0xb8000,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0xb8000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_mstr_axi_clk = {
	.halt_reg = 0xaf020,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xaf020,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(30),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_pipe_clk = {
	.halt_reg = 0xaf050,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0xaf050,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x79018,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_rchng_phy_clk = {
	.halt_reg = 0xaf040,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xaf040,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(31),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_rchng_phy_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_rchng_phy_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_sleep_clk = {
	.halt_reg = 0xaf04c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xaf04c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x79018,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_sleep_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_aux_phy_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_slv_axi_clk = {
	.halt_reg = 0xaf018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(29),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_slv_q2a_axi_clk = {
	.halt_reg = 0xaf014,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xaf014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7900c,
		.enable_mask = BIT(28),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_tbu_clk = {
	.halt_reg = 0xaf098,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xaf098,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x79018,
		.enable_mask = BIT(6),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_tbu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_throttle_core_clk = {
	.halt_reg = 0xaf094,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xaf094,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x79018,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_throttle_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_throttle_xo_clk = {
	.halt_reg = 0xaf090,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x79018,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_throttle_xo_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_tile_axi_sys_noc_clk = {
	.halt_reg = 0x10f2c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x10f2c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x10f2c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_tile_axi_sys_noc_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac0_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pdm2_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.hw.init = &(const struct clk_init_data) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pdm_xo4_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pwm0_xo512_clk = {
	.halt_reg = 0x2002c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2002c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pwm0_xo512_clk",
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
		.enable_reg = 0x79004,
		.enable_mask = BIT(9),
		.hw.init = &(const struct clk_init_data) {
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
		.enable_reg = 0x79004,
		.enable_mask = BIT(12),
		.hw.init = &(const struct clk_init_data) {
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
		.enable_reg = 0x79004,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data) {
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
		.enable_mask = BIT(20),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_gpu_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_pcie_cfg_ahb_clk = {
	.halt_reg = 0xaf08c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xaf08c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x79018,
		.enable_mask = BIT(3),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_pcie_cfg_ahb_clk",
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
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_video_vcodec_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_core_2x_clk = {
	.halt_reg = 0x1f014,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x79004,
		.enable_mask = BIT(21),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_core_clk = {
	.halt_reg = 0x1f00c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x79004,
		.enable_mask = BIT(20),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s0_clk = {
	.halt_reg = 0x1f144,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x79004,
		.enable_mask = BIT(22),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s0_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.enable_reg = 0x79004,
		.enable_mask = BIT(23),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s1_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.enable_reg = 0x79004,
		.enable_mask = BIT(24),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s2_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.enable_reg = 0x79004,
		.enable_mask = BIT(25),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s3_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.enable_reg = 0x79004,
		.enable_mask = BIT(26),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s4_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.enable_reg = 0x79004,
		.enable_mask = BIT(27),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s5_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap0_s5_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s6_clk = {
	.halt_reg = 0x1f864,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x79004,
		.enable_mask = BIT(28),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s6_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap0_s6_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s7_clk = {
	.halt_reg = 0x1f994,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x79004,
		.enable_mask = BIT(29),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s7_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap0_s7_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s8_clk = {
	.halt_reg = 0x1fac4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x79004,
		.enable_mask = BIT(30),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s8_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap0_s8_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s9_clk = {
	.halt_reg = 0x1fbf4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x79004,
		.enable_mask = BIT(31),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s9_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap0_s9_clk_src.clkr.hw,
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
		.enable_reg = 0x79004,
		.enable_mask = BIT(18),
		.hw.init = &(const struct clk_init_data) {
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
		.enable_reg = 0x79004,
		.enable_mask = BIT(19),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_0_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_ahb_clk = {
	.halt_reg = 0x38008,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x38008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x38008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sdcc1_apps_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_sdcc1_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sdcc1_ice_core_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
	.hwcg_reg = 0x1e008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1e008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sdcc2_apps_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_sdcc2_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sys_noc_usb2_prim_axi_clk = {
	.halt_reg = 0x10a14,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x10a14,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x10a14,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sys_noc_usb2_prim_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb20_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sys_noc_usb3_prim_axi_clk = {
	.halt_reg = 0x1a078,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x1a078,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1a078,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sys_noc_usb3_prim_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb30_prim_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_tscss_ahb_clk = {
	.halt_reg = 0xac024,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xac024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xac024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_tscss_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_tscss_cntr_clk = {
	.halt_reg = 0xac020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xac020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_tscss_cntr_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_tscss_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_tscss_etu_clk = {
	.halt_reg = 0xac01c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xac01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_tscss_etu_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_tscss_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_clkref_en = {
	.halt_reg = 0x8c000,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x8c000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb20_master_clk = {
	.halt_reg = 0xb0010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xb0010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xb0010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb20_master_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb20_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb20_mock_utmi_clk = {
	.halt_reg = 0xb001c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0xb001c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb20_mock_utmi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb20_mock_utmi_postdiv_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb20_sleep_clk = {
	.halt_reg = 0xb0018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0xb0018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb20_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_prim_master_clk = {
	.halt_reg = 0x1a010,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x1a010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1a010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_prim_master_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_prim_mock_utmi_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_prim_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_clkref_en = {
	.halt_reg = 0x9f000,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x9f000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_prim_clkref_en",
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_prim_phy_com_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x1a058,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1a058,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_prim_phy_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb3_prim_phy_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
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
		.hw.init = &(const struct clk_init_data) {
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
		.hw.init = &(const struct clk_init_data) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_venus_ctl_axi_clk",
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
		.hw.init = &(const struct clk_init_data) {
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
		.enable_mask = BIT(14),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_video_throttle_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_vcodec0_sys_clk = {
	.halt_reg = 0x6d044,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x6d044,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x6d044,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_video_vcodec0_sys_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_video_venus_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_venus_ctl_clk = {
	.halt_reg = 0x6d02c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6d02c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_video_venus_ctl_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_video_venus_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc gcc_camss_top_gdsc = {
	.gdscr = 0x58004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_camss_top_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc gcc_emac0_gdsc = {
	.gdscr = 0xad004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "gcc_emac0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc gcc_emac1_gdsc = {
	.gdscr = 0xae004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "gcc_emac1_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc gcc_pcie_gdsc = {
	.gdscr = 0xaf004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_pcie_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc gcc_usb20_gdsc = {
	.gdscr = 0xb0004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_usb20_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc gcc_usb30_prim_gdsc = {
	.gdscr = 0x1a004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "gcc_usb30_prim_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc gcc_venus_gdsc = {
	.gdscr = 0x6d01c,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_venus_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc gcc_vcodec0_gdsc = {
	.gdscr = 0x6d038,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_vcodec0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.parent = &gcc_venus_gdsc.pd,
	.flags = HW_CTRL_TRIGGER | POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct clk_regmap *gcc_shikra_clocks[] = {
	[GCC_AHB2PHY_CSI_CLK] = &gcc_ahb2phy_csi_clk.clkr,
	[GCC_AHB2PHY_USB_CLK] = &gcc_ahb2phy_usb_clk.clkr,
	[GCC_BOOT_ROM_AHB_CLK] = &gcc_boot_rom_ahb_clk.clkr,
	[GCC_CAM_THROTTLE_NRT_CLK] = &gcc_cam_throttle_nrt_clk.clkr,
	[GCC_CAM_THROTTLE_RT_CLK] = &gcc_cam_throttle_rt_clk.clkr,
	[GCC_CAMSS_AXI_CLK] = &gcc_camss_axi_clk.clkr,
	[GCC_CAMSS_AXI_CLK_SRC] = &gcc_camss_axi_clk_src.clkr,
	[GCC_CAMSS_CAMNOC_ATB_CLK] = &gcc_camss_camnoc_atb_clk.clkr,
	[GCC_CAMSS_CAMNOC_DRAGONLINK_ATB_CLK] = &gcc_camss_camnoc_dragonlink_atb_clk.clkr,
	[GCC_CAMSS_CAMNOC_NTS_XO_CLK] = &gcc_camss_camnoc_nts_xo_clk.clkr,
	[GCC_CAMSS_CCI_0_CLK] = &gcc_camss_cci_0_clk.clkr,
	[GCC_CAMSS_CCI_CLK_SRC] = &gcc_camss_cci_clk_src.clkr,
	[GCC_CAMSS_CPHY_0_CLK] = &gcc_camss_cphy_0_clk.clkr,
	[GCC_CAMSS_CPHY_1_CLK] = &gcc_camss_cphy_1_clk.clkr,
	[GCC_CAMSS_CSI0PHYTIMER_CLK] = &gcc_camss_csi0phytimer_clk.clkr,
	[GCC_CAMSS_CSI0PHYTIMER_CLK_SRC] = &gcc_camss_csi0phytimer_clk_src.clkr,
	[GCC_CAMSS_CSI1PHYTIMER_CLK] = &gcc_camss_csi1phytimer_clk.clkr,
	[GCC_CAMSS_CSI1PHYTIMER_CLK_SRC] = &gcc_camss_csi1phytimer_clk_src.clkr,
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
	[GCC_CAMSS_TFE_CPHY_RX_CLK_SRC] = &gcc_camss_tfe_cphy_rx_clk_src.clkr,
	[GCC_CAMSS_TOP_AHB_CLK] = &gcc_camss_top_ahb_clk.clkr,
	[GCC_CAMSS_TOP_AHB_CLK_SRC] = &gcc_camss_top_ahb_clk_src.clkr,
	[GCC_CFG_NOC_USB2_PRIM_AXI_CLK] = &gcc_cfg_noc_usb2_prim_axi_clk.clkr,
	[GCC_CFG_NOC_USB3_PRIM_AXI_CLK] = &gcc_cfg_noc_usb3_prim_axi_clk.clkr,
	[GCC_DDRSS_GPU_AXI_CLK] = &gcc_ddrss_gpu_axi_clk.clkr,
	[GCC_DDRSS_MEMNOC_PCIE_SF_CLK] = &gcc_ddrss_memnoc_pcie_sf_clk.clkr,
	[GCC_DISP_GPLL0_CLK_SRC] = &gcc_disp_gpll0_clk_src.clkr,
	[GCC_DISP_GPLL0_DIV_CLK_SRC] = &gcc_disp_gpll0_div_clk_src.clkr,
	[GCC_DISP_HF_AXI_CLK] = &gcc_disp_hf_axi_clk.clkr,
	[GCC_DISP_THROTTLE_CORE_CLK] = &gcc_disp_throttle_core_clk.clkr,
	[GCC_EMAC0_AHB_CLK] = &gcc_emac0_ahb_clk.clkr,
	[GCC_EMAC0_AXI_CLK] = &gcc_emac0_axi_clk.clkr,
	[GCC_EMAC0_AXI_CLK_SRC] = &gcc_emac0_axi_clk_src.clkr,
	[GCC_EMAC0_AXI_SYS_NOC_CLK] = &gcc_emac0_axi_sys_noc_clk.clkr,
	[GCC_EMAC0_CC_SGMIIPHY_RX_CLK] = &gcc_emac0_cc_sgmiiphy_rx_clk.clkr,
	[GCC_EMAC0_CC_SGMIIPHY_RX_CLK_SRC] = &gcc_emac0_cc_sgmiiphy_rx_clk_src.clkr,
	[GCC_EMAC0_CC_SGMIIPHY_TX_CLK] = &gcc_emac0_cc_sgmiiphy_tx_clk.clkr,
	[GCC_EMAC0_CC_SGMIIPHY_TX_CLK_SRC] = &gcc_emac0_cc_sgmiiphy_tx_clk_src.clkr,
	[GCC_EMAC0_PHY_AUX_CLK] = &gcc_emac0_phy_aux_clk.clkr,
	[GCC_EMAC0_PHY_AUX_CLK_SRC] = &gcc_emac0_phy_aux_clk_src.clkr,
	[GCC_EMAC0_PTP_CLK] = &gcc_emac0_ptp_clk.clkr,
	[GCC_EMAC0_PTP_CLK_SRC] = &gcc_emac0_ptp_clk_src.clkr,
	[GCC_EMAC0_RGMII_CLK] = &gcc_emac0_rgmii_clk.clkr,
	[GCC_EMAC0_RGMII_CLK_SRC] = &gcc_emac0_rgmii_clk_src.clkr,
	[GCC_EMAC1_AHB_CLK] = &gcc_emac1_ahb_clk.clkr,
	[GCC_EMAC1_AXI_CLK] = &gcc_emac1_axi_clk.clkr,
	[GCC_EMAC1_AXI_CLK_SRC] = &gcc_emac1_axi_clk_src.clkr,
	[GCC_EMAC1_AXI_SYS_NOC_CLK] = &gcc_emac1_axi_sys_noc_clk.clkr,
	[GCC_EMAC1_CC_SGMIIPHY_RX_CLK] = &gcc_emac1_cc_sgmiiphy_rx_clk.clkr,
	[GCC_EMAC1_CC_SGMIIPHY_RX_CLK_SRC] = &gcc_emac1_cc_sgmiiphy_rx_clk_src.clkr,
	[GCC_EMAC1_CC_SGMIIPHY_TX_CLK] = &gcc_emac1_cc_sgmiiphy_tx_clk.clkr,
	[GCC_EMAC1_CC_SGMIIPHY_TX_CLK_SRC] = &gcc_emac1_cc_sgmiiphy_tx_clk_src.clkr,
	[GCC_EMAC1_PHY_AUX_CLK] = &gcc_emac1_phy_aux_clk.clkr,
	[GCC_EMAC1_PHY_AUX_CLK_SRC] = &gcc_emac1_phy_aux_clk_src.clkr,
	[GCC_EMAC1_PTP_CLK] = &gcc_emac1_ptp_clk.clkr,
	[GCC_EMAC1_PTP_CLK_SRC] = &gcc_emac1_ptp_clk_src.clkr,
	[GCC_EMAC1_RGMII_CLK] = &gcc_emac1_rgmii_clk.clkr,
	[GCC_EMAC1_RGMII_CLK_SRC] = &gcc_emac1_rgmii_clk_src.clkr,
	[GCC_GP1_CLK] = &gcc_gp1_clk.clkr,
	[GCC_GP1_CLK_SRC] = &gcc_gp1_clk_src.clkr,
	[GCC_GP2_CLK] = &gcc_gp2_clk.clkr,
	[GCC_GP2_CLK_SRC] = &gcc_gp2_clk_src.clkr,
	[GCC_GP3_CLK] = &gcc_gp3_clk.clkr,
	[GCC_GP3_CLK_SRC] = &gcc_gp3_clk_src.clkr,
	[GCC_GPU_GPLL0_CLK_SRC] = &gcc_gpu_gpll0_clk_src.clkr,
	[GCC_GPU_GPLL0_DIV_CLK_SRC] = &gcc_gpu_gpll0_div_clk_src.clkr,
	[GCC_GPU_MEMNOC_GFX_CLK] = &gcc_gpu_memnoc_gfx_clk.clkr,
	[GCC_GPU_SMMU_VOTE_CLK] = &gcc_gpu_smmu_vote_clk.clkr,
	[GCC_GPU_SNOC_DVM_GFX_CLK] = &gcc_gpu_snoc_dvm_gfx_clk.clkr,
	[GCC_GPU_THROTTLE_CORE_CLK] = &gcc_gpu_throttle_core_clk.clkr,
	[GCC_MMU_TCU_VOTE_CLK] = &gcc_mmu_tcu_vote_clk.clkr,
	[GCC_PCIE_AUX_CLK] = &gcc_pcie_aux_clk.clkr,
	[GCC_PCIE_AUX_CLK_SRC] = &gcc_pcie_aux_clk_src.clkr,
	[GCC_PCIE_AUX_PHY_CLK_SRC] = &gcc_pcie_aux_phy_clk_src.clkr,
	[GCC_PCIE_CFG_AHB_CLK] = &gcc_pcie_cfg_ahb_clk.clkr,
	[GCC_PCIE_CLKREF_EN] = &gcc_pcie_clkref_en.clkr,
	[GCC_PCIE_MSTR_AXI_CLK] = &gcc_pcie_mstr_axi_clk.clkr,
	[GCC_PCIE_PIPE_CLK] = &gcc_pcie_pipe_clk.clkr,
	[GCC_PCIE_PIPE_CLK_SRC] = &gcc_pcie_pipe_clk_src.clkr,
	[GCC_PCIE_RCHNG_PHY_CLK] = &gcc_pcie_rchng_phy_clk.clkr,
	[GCC_PCIE_RCHNG_PHY_CLK_SRC] = &gcc_pcie_rchng_phy_clk_src.clkr,
	[GCC_PCIE_SLEEP_CLK] = &gcc_pcie_sleep_clk.clkr,
	[GCC_PCIE_SLV_AXI_CLK] = &gcc_pcie_slv_axi_clk.clkr,
	[GCC_PCIE_SLV_Q2A_AXI_CLK] = &gcc_pcie_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_TBU_CLK] = &gcc_pcie_tbu_clk.clkr,
	[GCC_PCIE_THROTTLE_CORE_CLK] = &gcc_pcie_throttle_core_clk.clkr,
	[GCC_PCIE_THROTTLE_XO_CLK] = &gcc_pcie_throttle_xo_clk.clkr,
	[GCC_PCIE_TILE_AXI_SYS_NOC_CLK] = &gcc_pcie_tile_axi_sys_noc_clk.clkr,
	[GCC_PDM2_CLK] = &gcc_pdm2_clk.clkr,
	[GCC_PDM2_CLK_SRC] = &gcc_pdm2_clk_src.clkr,
	[GCC_PDM_AHB_CLK] = &gcc_pdm_ahb_clk.clkr,
	[GCC_PDM_XO4_CLK] = &gcc_pdm_xo4_clk.clkr,
	[GCC_PWM0_XO512_CLK] = &gcc_pwm0_xo512_clk.clkr,
	[GCC_QMIP_CAMERA_NRT_AHB_CLK] = &gcc_qmip_camera_nrt_ahb_clk.clkr,
	[GCC_QMIP_CAMERA_RT_AHB_CLK] = &gcc_qmip_camera_rt_ahb_clk.clkr,
	[GCC_QMIP_DISP_AHB_CLK] = &gcc_qmip_disp_ahb_clk.clkr,
	[GCC_QMIP_GPU_CFG_AHB_CLK] = &gcc_qmip_gpu_cfg_ahb_clk.clkr,
	[GCC_QMIP_PCIE_CFG_AHB_CLK] = &gcc_qmip_pcie_cfg_ahb_clk.clkr,
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
	[GCC_QUPV3_WRAP0_S6_CLK] = &gcc_qupv3_wrap0_s6_clk.clkr,
	[GCC_QUPV3_WRAP0_S6_CLK_SRC] = &gcc_qupv3_wrap0_s6_clk_src.clkr,
	[GCC_QUPV3_WRAP0_S7_CLK] = &gcc_qupv3_wrap0_s7_clk.clkr,
	[GCC_QUPV3_WRAP0_S7_CLK_SRC] = &gcc_qupv3_wrap0_s7_clk_src.clkr,
	[GCC_QUPV3_WRAP0_S8_CLK] = &gcc_qupv3_wrap0_s8_clk.clkr,
	[GCC_QUPV3_WRAP0_S8_CLK_SRC] = &gcc_qupv3_wrap0_s8_clk_src.clkr,
	[GCC_QUPV3_WRAP0_S9_CLK] = &gcc_qupv3_wrap0_s9_clk.clkr,
	[GCC_QUPV3_WRAP0_S9_CLK_SRC] = &gcc_qupv3_wrap0_s9_clk_src.clkr,
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
	[GCC_SYS_NOC_USB2_PRIM_AXI_CLK] = &gcc_sys_noc_usb2_prim_axi_clk.clkr,
	[GCC_SYS_NOC_USB3_PRIM_AXI_CLK] = &gcc_sys_noc_usb3_prim_axi_clk.clkr,
	[GCC_TSCSS_AHB_CLK] = &gcc_tscss_ahb_clk.clkr,
	[GCC_TSCSS_CLK_SRC] = &gcc_tscss_clk_src.clkr,
	[GCC_TSCSS_CNTR_CLK] = &gcc_tscss_cntr_clk.clkr,
	[GCC_TSCSS_ETU_CLK] = &gcc_tscss_etu_clk.clkr,
	[GCC_UFS_CLKREF_EN] = &gcc_ufs_clkref_en.clkr,
	[GCC_USB20_MASTER_CLK] = &gcc_usb20_master_clk.clkr,
	[GCC_USB20_MASTER_CLK_SRC] = &gcc_usb20_master_clk_src.clkr,
	[GCC_USB20_MOCK_UTMI_CLK] = &gcc_usb20_mock_utmi_clk.clkr,
	[GCC_USB20_MOCK_UTMI_CLK_SRC] = &gcc_usb20_mock_utmi_clk_src.clkr,
	[GCC_USB20_MOCK_UTMI_POSTDIV_CLK_SRC] = &gcc_usb20_mock_utmi_postdiv_clk_src.clkr,
	[GCC_USB20_SLEEP_CLK] = &gcc_usb20_sleep_clk.clkr,
	[GCC_USB30_PRIM_MASTER_CLK] = &gcc_usb30_prim_master_clk.clkr,
	[GCC_USB30_PRIM_MASTER_CLK_SRC] = &gcc_usb30_prim_master_clk_src.clkr,
	[GCC_USB30_PRIM_MOCK_UTMI_CLK] = &gcc_usb30_prim_mock_utmi_clk.clkr,
	[GCC_USB30_PRIM_MOCK_UTMI_CLK_SRC] = &gcc_usb30_prim_mock_utmi_clk_src.clkr,
	[GCC_USB30_PRIM_MOCK_UTMI_POSTDIV_CLK_SRC] = &gcc_usb30_prim_mock_utmi_postdiv_clk_src.clkr,
	[GCC_USB30_PRIM_SLEEP_CLK] = &gcc_usb30_prim_sleep_clk.clkr,
	[GCC_USB3_PRIM_CLKREF_EN] = &gcc_usb3_prim_clkref_en.clkr,
	[GCC_USB3_PRIM_PHY_AUX_CLK_SRC] = &gcc_usb3_prim_phy_aux_clk_src.clkr,
	[GCC_USB3_PRIM_PHY_COM_AUX_CLK] = &gcc_usb3_prim_phy_com_aux_clk.clkr,
	[GCC_USB3_PRIM_PHY_PIPE_CLK] = &gcc_usb3_prim_phy_pipe_clk.clkr,
	[GCC_USB3_PRIM_PHY_PIPE_CLK_SRC] = &gcc_usb3_prim_phy_pipe_clk_src.clkr,
	[GCC_VCODEC0_AXI_CLK] = &gcc_vcodec0_axi_clk.clkr,
	[GCC_VENUS_AHB_CLK] = &gcc_venus_ahb_clk.clkr,
	[GCC_VENUS_CTL_AXI_CLK] = &gcc_venus_ctl_axi_clk.clkr,
	[GCC_VIDEO_AXI0_CLK] = &gcc_video_axi0_clk.clkr,
	[GCC_VIDEO_THROTTLE_CORE_CLK] = &gcc_video_throttle_core_clk.clkr,
	[GCC_VIDEO_VCODEC0_SYS_CLK] = &gcc_video_vcodec0_sys_clk.clkr,
	[GCC_VIDEO_VENUS_CLK_SRC] = &gcc_video_venus_clk_src.clkr,
	[GCC_VIDEO_VENUS_CTL_CLK] = &gcc_video_venus_ctl_clk.clkr,
	[GPLL0] = &gpll0.clkr,
	[GPLL0_OUT_AUX2] = &gpll0_out_aux2.clkr,
	[GPLL10] = &gpll10.clkr,
	[GPLL11] = &gpll11.clkr,
	[GPLL12] = &gpll12.clkr,
	[GPLL12_OUT_AUX2] = &gpll12_out_aux2.clkr,
	[GPLL3] = &gpll3.clkr,
	[GPLL3_OUT_MAIN] = &gpll3_out_main.clkr,
	[GPLL4] = &gpll4.clkr,
	[GPLL5] = &gpll5.clkr,
	[GPLL6] = &gpll6.clkr,
	[GPLL6_OUT_MAIN] = &gpll6_out_main.clkr,
	[GPLL7] = &gpll7.clkr,
	[GPLL8] = &gpll8.clkr,
	[GPLL8_OUT_MAIN] = &gpll8_out_main.clkr,
	[GPLL9] = &gpll9.clkr,
	[GPLL9_OUT_MAIN] = &gpll9_out_main.clkr,
};

static struct gdsc *gcc_shikra_gdscs[] = {
	[GCC_CAMSS_TOP_GDSC] = &gcc_camss_top_gdsc,
	[GCC_EMAC0_GDSC] = &gcc_emac0_gdsc,
	[GCC_EMAC1_GDSC] = &gcc_emac1_gdsc,
	[GCC_PCIE_GDSC] = &gcc_pcie_gdsc,
	[GCC_USB20_GDSC] = &gcc_usb20_gdsc,
	[GCC_USB30_PRIM_GDSC] = &gcc_usb30_prim_gdsc,
	[GCC_VCODEC0_GDSC] = &gcc_vcodec0_gdsc,
	[GCC_VENUS_GDSC] = &gcc_venus_gdsc,
};

static const struct qcom_reset_map gcc_shikra_resets[] = {
	[GCC_CAMSS_OPE_BCR] = { 0x55000 },
	[GCC_CAMSS_TFE_BCR] = { 0x52000 },
	[GCC_CAMSS_TOP_BCR] = { 0x58000 },
	[GCC_EMAC0_BCR] = { 0xad000 },
	[GCC_EMAC1_BCR] = { 0xae000 },
	[GCC_GPU_BCR] = { 0x36000 },
	[GCC_MMSS_BCR] = { 0x17000 },
	[GCC_PCIE_BCR] = { 0xaf000 },
	[GCC_PCIE_PHY_BCR] = { 0xb1000 },
	[GCC_PDM_BCR] = { 0x20000 },
	[GCC_QUPV3_WRAPPER_0_BCR] = { 0x1f000 },
	[GCC_QUSB2PHY_PRIM_BCR] = { 0x1c000 },
	[GCC_QUSB2PHY_SEC_BCR] = { 0x1c004 },
	[GCC_SDCC1_BCR] = { 0x38000 },
	[GCC_SDCC2_BCR] = { 0x1e000 },
	[GCC_TSCSS_BCR] = { 0xac000 },
	[GCC_USB20_BCR] = { 0xb0000 },
	[GCC_USB30_PRIM_BCR] = { 0x1a000 },
	[GCC_USB3PHY_PHY_PRIM_SP0_BCR] = { 0x1b008 },
	[GCC_USB3_DP_PHY_PRIM_BCR] = { 0x1b020 },
	[GCC_USB3_PHY_PRIM_SP0_BCR] = { 0x1b000 },
	[GCC_USB_PHY_CFG_AHB2PHY_BCR] = { 0x1d000 },
	[GCC_VCODEC0_BCR] = { 0x6d034 },
	[GCC_VENUS_BCR] = { 0x6d018 },
	[GCC_VIDEO_INTERFACE_BCR] = { 0x6e000 },
};

static struct clk_alpha_pll *gcc_shikra_plls[] = {
	&gpll10,
	&gpll11,
	&gpll8,
	&gpll9,
};

static const u32 gcc_shikra_critical_cbcrs[] = {
	0x17008, /* GCC_CAMERA_AHB_CLK */
	0x17028, /* GCC_CAMERA_XO_CLK */
	0x1700c, /* GCC_DISP_AHB_CLK */
	0x1702c, /* GCC_DISP_XO_CLK */
	0x36004, /* GCC_GPU_CFG_AHB_CLK */
	0x36100, /* GCC_GPU_IREF_CLK */
	0x3a00c, /* GCC_LPASS_CONFIG_CLK */
	0x3a008, /* GCC_LPASS_CORE_AXIM_CLK */
	0x79004, /* GCC_SYS_NOC_CPUSS_AHB_CLK */
	0x17004, /* GCC_VIDEO_AHB_CLK */
	0x17024, /* GCC_VIDEO_XO_CLK */
};

static const struct clk_rcg_dfs_data gcc_shikra_dfs_clocks[] = {
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s0_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s1_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s2_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s3_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s4_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s5_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s6_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s7_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s8_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s9_clk_src),
};

static const struct regmap_config gcc_shikra_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xc7000,
	.fast_io = true,
};

static const struct qcom_cc_driver_data gcc_shikra_driver_data = {
	.alpha_plls = gcc_shikra_plls,
	.num_alpha_plls = ARRAY_SIZE(gcc_shikra_plls),
	.clk_cbcrs = gcc_shikra_critical_cbcrs,
	.num_clk_cbcrs = ARRAY_SIZE(gcc_shikra_critical_cbcrs),
	.dfs_rcgs = gcc_shikra_dfs_clocks,
	.num_dfs_rcgs = ARRAY_SIZE(gcc_shikra_dfs_clocks),
};

static const struct qcom_cc_desc gcc_shikra_desc = {
	.config = &gcc_shikra_regmap_config,
	.clks = gcc_shikra_clocks,
	.num_clks = ARRAY_SIZE(gcc_shikra_clocks),
	.resets = gcc_shikra_resets,
	.num_resets = ARRAY_SIZE(gcc_shikra_resets),
	.gdscs = gcc_shikra_gdscs,
	.num_gdscs = ARRAY_SIZE(gcc_shikra_gdscs),
	.driver_data = &gcc_shikra_driver_data,
};

static const struct of_device_id gcc_shikra_match_table[] = {
	{ .compatible = "qcom,shikra-gcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_shikra_match_table);

static int gcc_shikra_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &gcc_shikra_desc);
}

static struct platform_driver gcc_shikra_driver = {
	.probe = gcc_shikra_probe,
	.driver = {
		.name = "gcc-shikra",
		.of_match_table = gcc_shikra_match_table,
	},
};

static int __init gcc_shikra_init(void)
{
	return platform_driver_register(&gcc_shikra_driver);
}
subsys_initcall(gcc_shikra_init);

static void __exit gcc_shikra_exit(void)
{
	platform_driver_unregister(&gcc_shikra_driver);
}
module_exit(gcc_shikra_exit);

MODULE_DESCRIPTION("QTI GCC Shikra Driver");
MODULE_LICENSE("GPL");
