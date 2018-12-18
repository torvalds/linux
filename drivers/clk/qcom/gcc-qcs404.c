// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

#include <dt-bindings/clock/qcom,gcc-qcs404.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "common.h"
#include "reset.h"

enum {
	P_CORE_BI_PLL_TEST_SE,
	P_DSI0_PHY_PLL_OUT_BYTECLK,
	P_DSI0_PHY_PLL_OUT_DSICLK,
	P_GPLL0_OUT_AUX,
	P_GPLL0_OUT_MAIN,
	P_GPLL1_OUT_MAIN,
	P_GPLL3_OUT_MAIN,
	P_GPLL4_OUT_AUX,
	P_GPLL4_OUT_MAIN,
	P_GPLL6_OUT_AUX,
	P_HDMI_PHY_PLL_CLK,
	P_PCIE_0_PIPE_CLK,
	P_SLEEP_CLK,
	P_XO,
};

static const struct parent_map gcc_parent_map_0[] = {
	{ P_XO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gcc_parent_names_0[] = {
	"cxo",
	"gpll0_out_main",
	"core_bi_pll_test_se",
};

static const char * const gcc_parent_names_ao_0[] = {
	"cxo",
	"gpll0_ao_out_main",
	"core_bi_pll_test_se",
};

static const struct parent_map gcc_parent_map_1[] = {
	{ P_XO, 0 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gcc_parent_names_1[] = {
	"cxo",
	"core_bi_pll_test_se",
};

static const struct parent_map gcc_parent_map_2[] = {
	{ P_XO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL6_OUT_AUX, 2 },
	{ P_SLEEP_CLK, 6 },
};

static const char * const gcc_parent_names_2[] = {
	"cxo",
	"gpll0_out_main",
	"gpll6_out_aux",
	"sleep_clk",
};

static const struct parent_map gcc_parent_map_3[] = {
	{ P_XO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL6_OUT_AUX, 2 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gcc_parent_names_3[] = {
	"cxo",
	"gpll0_out_main",
	"gpll6_out_aux",
	"core_bi_pll_test_se",
};

static const struct parent_map gcc_parent_map_4[] = {
	{ P_XO, 0 },
	{ P_GPLL1_OUT_MAIN, 1 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gcc_parent_names_4[] = {
	"cxo",
	"gpll1_out_main",
	"core_bi_pll_test_se",
};

static const struct parent_map gcc_parent_map_5[] = {
	{ P_XO, 0 },
	{ P_DSI0_PHY_PLL_OUT_BYTECLK, 1 },
	{ P_GPLL0_OUT_AUX, 2 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gcc_parent_names_5[] = {
	"cxo",
	"dsi0pll_byteclk_src",
	"gpll0_out_aux",
	"core_bi_pll_test_se",
};

static const struct parent_map gcc_parent_map_6[] = {
	{ P_XO, 0 },
	{ P_DSI0_PHY_PLL_OUT_BYTECLK, 2 },
	{ P_GPLL0_OUT_AUX, 3 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gcc_parent_names_6[] = {
	"cxo",
	"dsi0_phy_pll_out_byteclk",
	"gpll0_out_aux",
	"core_bi_pll_test_se",
};

static const struct parent_map gcc_parent_map_7[] = {
	{ P_XO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL3_OUT_MAIN, 2 },
	{ P_GPLL6_OUT_AUX, 3 },
	{ P_GPLL4_OUT_AUX, 4 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gcc_parent_names_7[] = {
	"cxo",
	"gpll0_out_main",
	"gpll3_out_main",
	"gpll6_out_aux",
	"gpll4_out_aux",
	"core_bi_pll_test_se",
};

static const struct parent_map gcc_parent_map_8[] = {
	{ P_XO, 0 },
	{ P_HDMI_PHY_PLL_CLK, 1 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gcc_parent_names_8[] = {
	"cxo",
	"hdmi_phy_pll_clk",
	"core_bi_pll_test_se",
};

static const struct parent_map gcc_parent_map_9[] = {
	{ P_XO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_DSI0_PHY_PLL_OUT_DSICLK, 2 },
	{ P_GPLL6_OUT_AUX, 3 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gcc_parent_names_9[] = {
	"cxo",
	"gpll0_out_main",
	"dsi0_phy_pll_out_dsiclk",
	"gpll6_out_aux",
	"core_bi_pll_test_se",
};

static const struct parent_map gcc_parent_map_10[] = {
	{ P_XO, 0 },
	{ P_SLEEP_CLK, 1 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gcc_parent_names_10[] = {
	"cxo",
	"sleep_clk",
	"core_bi_pll_test_se",
};

static const struct parent_map gcc_parent_map_11[] = {
	{ P_XO, 0 },
	{ P_PCIE_0_PIPE_CLK, 1 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gcc_parent_names_11[] = {
	"cxo",
	"pcie_0_pipe_clk",
	"core_bi_pll_test_se",
};

static const struct parent_map gcc_parent_map_12[] = {
	{ P_XO, 0 },
	{ P_DSI0_PHY_PLL_OUT_DSICLK, 1 },
	{ P_GPLL0_OUT_AUX, 2 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gcc_parent_names_12[] = {
	"cxo",
	"dsi0pll_pclk_src",
	"gpll0_out_aux",
	"core_bi_pll_test_se",
};

static const struct parent_map gcc_parent_map_13[] = {
	{ P_XO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL4_OUT_MAIN, 2 },
	{ P_GPLL6_OUT_AUX, 3 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gcc_parent_names_13[] = {
	"cxo",
	"gpll0_out_main",
	"gpll4_out_main",
	"gpll6_out_aux",
	"core_bi_pll_test_se",
};

static const struct parent_map gcc_parent_map_14[] = {
	{ P_XO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL4_OUT_AUX, 2 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gcc_parent_names_14[] = {
	"cxo",
	"gpll0_out_main",
	"gpll4_out_aux",
	"core_bi_pll_test_se",
};

static const struct parent_map gcc_parent_map_15[] = {
	{ P_XO, 0 },
	{ P_GPLL0_OUT_AUX, 2 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gcc_parent_names_15[] = {
	"cxo",
	"gpll0_out_aux",
	"core_bi_pll_test_se",
};

static struct clk_fixed_factor cxo = {
	.mult = 1,
	.div = 1,
	.hw.init = &(struct clk_init_data){
		.name = "cxo",
		.parent_names = (const char *[]){ "xo-board" },
		.num_parents = 1,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct clk_alpha_pll gpll0_sleep_clk_src = {
	.offset = 0x21000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		.enable_reg = 0x45008,
		.enable_mask = BIT(23),
		.enable_is_inverted = true,
		.hw.init = &(struct clk_init_data){
			.name = "gpll0_sleep_clk_src",
			.parent_names = (const char *[]){ "cxo" },
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct clk_alpha_pll gpll0_out_main = {
	.offset = 0x21000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.flags = SUPPORTS_FSM_MODE,
	.clkr = {
		.enable_reg = 0x45000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpll0_out_main",
			.parent_names = (const char *[])
					{ "gpll0_sleep_clk_src" },
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct clk_alpha_pll gpll0_ao_out_main = {
	.offset = 0x21000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.flags = SUPPORTS_FSM_MODE,
	.clkr = {
		.enable_reg = 0x45000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpll0_ao_out_main",
			.parent_names = (const char *[]){ "cxo" },
			.num_parents = 1,
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct clk_alpha_pll gpll1_out_main = {
	.offset = 0x20000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		.enable_reg = 0x45000,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gpll1_out_main",
			.parent_names = (const char *[]){ "cxo" },
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

/* 930MHz configuration */
static const struct alpha_pll_config gpll3_config = {
	.l = 48,
	.alpha = 0x0,
	.alpha_en_mask = BIT(24),
	.post_div_mask = 0xf << 8,
	.post_div_val = 0x1 << 8,
	.vco_mask = 0x3 << 20,
	.main_output_mask = 0x1,
	.config_ctl_val = 0x4001055b,
};

static const struct pll_vco gpll3_vco[] = {
	{ 700000000, 1400000000, 0 },
};

static struct clk_alpha_pll gpll3_out_main = {
	.offset = 0x22000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.vco_table = gpll3_vco,
	.num_vco = ARRAY_SIZE(gpll3_vco),
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "gpll3_out_main",
			.parent_names = (const char *[]){ "cxo" },
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct clk_alpha_pll gpll4_out_main = {
	.offset = 0x24000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		.enable_reg = 0x45000,
		.enable_mask = BIT(5),
		.hw.init = &(struct clk_init_data){
			.name = "gpll4_out_main",
			.parent_names = (const char *[]){ "cxo" },
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct clk_pll gpll6 = {
	.l_reg = 0x37004,
	.m_reg = 0x37008,
	.n_reg = 0x3700C,
	.config_reg = 0x37014,
	.mode_reg = 0x37000,
	.status_reg = 0x3701C,
	.status_bit = 17,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll6",
		.parent_names = (const char *[]){ "cxo" },
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

static struct clk_regmap gpll6_out_aux = {
	.enable_reg = 0x45000,
	.enable_mask = BIT(7),
	.hw.init = &(struct clk_init_data){
		.name = "gpll6_out_aux",
		.parent_names = (const char *[]){ "gpll6" },
		.num_parents = 1,
		.ops = &clk_pll_vote_ops,
	},
};

static const struct freq_tbl ftbl_apss_ahb_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(50000000, P_GPLL0_OUT_MAIN, 16, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 8, 0, 0),
	F(133333333, P_GPLL0_OUT_MAIN, 6, 0, 0),
	{ }
};

static struct clk_rcg2 apss_ahb_clk_src = {
	.cmd_rcgr = 0x46000,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_apss_ahb_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "apss_ahb_clk_src",
		.parent_names = gcc_parent_names_ao_0,
		.num_parents = 3,
		.flags = CLK_IS_CRITICAL,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_blsp1_qup0_i2c_apps_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(50000000, P_GPLL0_OUT_MAIN, 16, 0, 0),
	{ }
};

static struct clk_rcg2 blsp1_qup0_i2c_apps_clk_src = {
	.cmd_rcgr = 0x602c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_blsp1_qup0_i2c_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup0_i2c_apps_clk_src",
		.parent_names = gcc_parent_names_0,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_blsp1_qup0_spi_apps_clk_src[] = {
	F(960000, P_XO, 10, 1, 2),
	F(4800000, P_XO, 4, 0, 0),
	F(9600000, P_XO, 2, 0, 0),
	F(16000000, P_GPLL0_OUT_MAIN, 10, 1, 5),
	F(19200000, P_XO, 1, 0, 0),
	F(25000000, P_GPLL0_OUT_MAIN, 16, 1, 2),
	F(50000000, P_GPLL0_OUT_MAIN, 16, 0, 0),
	{ }
};

static struct clk_rcg2 blsp1_qup0_spi_apps_clk_src = {
	.cmd_rcgr = 0x6034,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_blsp1_qup0_spi_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup0_spi_apps_clk_src",
		.parent_names = gcc_parent_names_0,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup1_i2c_apps_clk_src = {
	.cmd_rcgr = 0x200c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_blsp1_qup0_i2c_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup1_i2c_apps_clk_src",
		.parent_names = gcc_parent_names_0,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_blsp1_qup1_spi_apps_clk_src[] = {
	F(960000,   P_XO, 10, 1, 2),
	F(4800000,  P_XO, 4, 0, 0),
	F(9600000,  P_XO, 2, 0, 0),
	F(10480000, P_GPLL0_OUT_MAIN, 1, 3, 229),
	F(16000000, P_GPLL0_OUT_MAIN, 10, 1, 5),
	F(19200000, P_XO, 1, 0, 0),
	F(20961000, P_GPLL0_OUT_MAIN, 1, 6, 229),
	{ }
};

static struct clk_rcg2 blsp1_qup1_spi_apps_clk_src = {
	.cmd_rcgr = 0x2024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_blsp1_qup1_spi_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup1_spi_apps_clk_src",
		.parent_names = gcc_parent_names_0,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup2_i2c_apps_clk_src = {
	.cmd_rcgr = 0x3000,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_blsp1_qup0_i2c_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup2_i2c_apps_clk_src",
		.parent_names = gcc_parent_names_0,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_blsp1_qup2_spi_apps_clk_src[] = {
	F(960000,   P_XO, 10, 1, 2),
	F(4800000,  P_XO, 4, 0, 0),
	F(9600000,  P_XO, 2, 0, 0),
	F(15000000, P_GPLL0_OUT_MAIN, 1,  3, 160),
	F(16000000, P_GPLL0_OUT_MAIN, 10, 1, 5),
	F(19200000, P_XO, 1, 0, 0),
	F(25000000, P_GPLL0_OUT_MAIN, 16, 1, 2),
	F(30000000, P_GPLL0_OUT_MAIN, 1,  3, 80),
	{ }
};

static struct clk_rcg2 blsp1_qup2_spi_apps_clk_src = {
	.cmd_rcgr = 0x3014,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_blsp1_qup2_spi_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup2_spi_apps_clk_src",
		.parent_names = gcc_parent_names_0,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup3_i2c_apps_clk_src = {
	.cmd_rcgr = 0x4000,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_blsp1_qup0_i2c_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup3_i2c_apps_clk_src",
		.parent_names = gcc_parent_names_0,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup3_spi_apps_clk_src = {
	.cmd_rcgr = 0x4024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_blsp1_qup0_spi_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup3_spi_apps_clk_src",
		.parent_names = gcc_parent_names_0,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup4_i2c_apps_clk_src = {
	.cmd_rcgr = 0x5000,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_blsp1_qup0_i2c_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup4_i2c_apps_clk_src",
		.parent_names = gcc_parent_names_0,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup4_spi_apps_clk_src = {
	.cmd_rcgr = 0x5024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_blsp1_qup0_spi_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup4_spi_apps_clk_src",
		.parent_names = gcc_parent_names_0,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_blsp1_uart0_apps_clk_src[] = {
	F(3686400, P_GPLL0_OUT_MAIN, 1, 72, 15625),
	F(7372800, P_GPLL0_OUT_MAIN, 1, 144, 15625),
	F(14745600, P_GPLL0_OUT_MAIN, 1, 288, 15625),
	F(16000000, P_GPLL0_OUT_MAIN, 10, 1, 5),
	F(19200000, P_XO, 1, 0, 0),
	F(24000000, P_GPLL0_OUT_MAIN, 1, 3, 100),
	F(25000000, P_GPLL0_OUT_MAIN, 16, 1, 2),
	F(32000000, P_GPLL0_OUT_MAIN, 1, 1, 25),
	F(40000000, P_GPLL0_OUT_MAIN, 1, 1, 20),
	F(46400000, P_GPLL0_OUT_MAIN, 1, 29, 500),
	F(48000000, P_GPLL0_OUT_MAIN, 1, 3, 50),
	F(51200000, P_GPLL0_OUT_MAIN, 1, 8, 125),
	F(56000000, P_GPLL0_OUT_MAIN, 1, 7, 100),
	F(58982400, P_GPLL0_OUT_MAIN, 1, 1152, 15625),
	F(60000000, P_GPLL0_OUT_MAIN, 1, 3, 40),
	F(64000000, P_GPLL0_OUT_MAIN, 1, 2, 25),
	{ }
};

static struct clk_rcg2 blsp1_uart0_apps_clk_src = {
	.cmd_rcgr = 0x600c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_blsp1_uart0_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart0_apps_clk_src",
		.parent_names = gcc_parent_names_0,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_uart1_apps_clk_src = {
	.cmd_rcgr = 0x2044,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_blsp1_uart0_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart1_apps_clk_src",
		.parent_names = gcc_parent_names_0,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_uart2_apps_clk_src = {
	.cmd_rcgr = 0x3034,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_blsp1_uart0_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart2_apps_clk_src",
		.parent_names = gcc_parent_names_0,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_uart3_apps_clk_src = {
	.cmd_rcgr = 0x4014,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_blsp1_uart0_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart3_apps_clk_src",
		.parent_names = gcc_parent_names_0,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_qup0_i2c_apps_clk_src = {
	.cmd_rcgr = 0xc00c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_blsp1_qup0_i2c_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp2_qup0_i2c_apps_clk_src",
		.parent_names = gcc_parent_names_0,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_qup0_spi_apps_clk_src = {
	.cmd_rcgr = 0xc024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_blsp1_qup0_spi_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp2_qup0_spi_apps_clk_src",
		.parent_names = gcc_parent_names_0,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_uart0_apps_clk_src = {
	.cmd_rcgr = 0xc044,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_blsp1_uart0_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp2_uart0_apps_clk_src",
		.parent_names = gcc_parent_names_0,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 byte0_clk_src = {
	.cmd_rcgr = 0x4d044,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "byte0_clk_src",
		.parent_names = gcc_parent_names_5,
		.num_parents = 4,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_byte2_ops,
	},
};

static const struct freq_tbl ftbl_emac_clk_src[] = {
	F(5000000,   P_GPLL1_OUT_MAIN, 2, 1, 50),
	F(50000000,  P_GPLL1_OUT_MAIN, 10, 0, 0),
	F(125000000, P_GPLL1_OUT_MAIN, 4, 0, 0),
	F(250000000, P_GPLL1_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 emac_clk_src = {
	.cmd_rcgr = 0x4e01c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_emac_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "emac_clk_src",
		.parent_names = gcc_parent_names_4,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_emac_ptp_clk_src[] = {
	F(50000000,  P_GPLL1_OUT_MAIN, 10, 0, 0),
	F(125000000, P_GPLL1_OUT_MAIN, 4, 0, 0),
	F(250000000, P_GPLL1_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 emac_ptp_clk_src = {
	.cmd_rcgr = 0x4e014,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_emac_ptp_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "emac_ptp_clk_src",
		.parent_names = gcc_parent_names_4,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_esc0_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 esc0_clk_src = {
	.cmd_rcgr = 0x4d05c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_6,
	.freq_tbl = ftbl_esc0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "esc0_clk_src",
		.parent_names = gcc_parent_names_6,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gfx3d_clk_src[] = {
	F(19200000,  P_XO, 1, 0, 0),
	F(50000000,  P_GPLL0_OUT_MAIN, 16, 0, 0),
	F(80000000,  P_GPLL0_OUT_MAIN, 10, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 8, 0, 0),
	F(160000000, P_GPLL0_OUT_MAIN, 5, 0, 0),
	F(200000000, P_GPLL0_OUT_MAIN, 4, 0, 0),
	F(228571429, P_GPLL0_OUT_MAIN, 3.5, 0, 0),
	F(240000000, P_GPLL6_OUT_AUX,  4.5, 0, 0),
	F(266666667, P_GPLL0_OUT_MAIN, 3, 0, 0),
	F(270000000, P_GPLL6_OUT_AUX,  4, 0, 0),
	F(320000000, P_GPLL0_OUT_MAIN, 2.5, 0, 0),
	F(400000000, P_GPLL0_OUT_MAIN, 2, 0, 0),
	F(484800000, P_GPLL3_OUT_MAIN, 1, 0, 0),
	F(523200000, P_GPLL3_OUT_MAIN, 1, 0, 0),
	F(550000000, P_GPLL3_OUT_MAIN, 1, 0, 0),
	F(598000000, P_GPLL3_OUT_MAIN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gfx3d_clk_src = {
	.cmd_rcgr = 0x59000,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_7,
	.freq_tbl = ftbl_gfx3d_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gfx3d_clk_src",
		.parent_names = gcc_parent_names_7,
		.num_parents = 6,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gp1_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 8, 0, 0),
	F(200000000, P_GPLL0_OUT_MAIN, 4, 0, 0),
	{ }
};

static struct clk_rcg2 gp1_clk_src = {
	.cmd_rcgr = 0x8004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gp1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gp1_clk_src",
		.parent_names = gcc_parent_names_2,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gp2_clk_src = {
	.cmd_rcgr = 0x9004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gp1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gp2_clk_src",
		.parent_names = gcc_parent_names_2,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gp3_clk_src = {
	.cmd_rcgr = 0xa004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gp1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gp3_clk_src",
		.parent_names = gcc_parent_names_2,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 hdmi_app_clk_src = {
	.cmd_rcgr = 0x4d0e4,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_esc0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "hdmi_app_clk_src",
		.parent_names = gcc_parent_names_1,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 hdmi_pclk_clk_src = {
	.cmd_rcgr = 0x4d0dc,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_8,
	.freq_tbl = ftbl_esc0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "hdmi_pclk_clk_src",
		.parent_names = gcc_parent_names_8,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_mdp_clk_src[] = {
	F(50000000, P_GPLL0_OUT_MAIN, 16, 0, 0),
	F(80000000, P_GPLL0_OUT_MAIN, 10, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 8, 0, 0),
	F(145454545, P_GPLL0_OUT_MAIN, 5.5, 0, 0),
	F(160000000, P_GPLL0_OUT_MAIN, 5, 0, 0),
	F(177777778, P_GPLL0_OUT_MAIN, 4.5, 0, 0),
	F(200000000, P_GPLL0_OUT_MAIN, 4, 0, 0),
	F(266666667, P_GPLL0_OUT_MAIN, 3, 0, 0),
	F(320000000, P_GPLL0_OUT_MAIN, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 mdp_clk_src = {
	.cmd_rcgr = 0x4d014,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_9,
	.freq_tbl = ftbl_mdp_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mdp_clk_src",
		.parent_names = gcc_parent_names_9,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_pcie_0_aux_clk_src[] = {
	F(1200000, P_XO, 16, 0, 0),
	{ }
};

static struct clk_rcg2 pcie_0_aux_clk_src = {
	.cmd_rcgr = 0x3e024,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_10,
	.freq_tbl = ftbl_pcie_0_aux_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pcie_0_aux_clk_src",
		.parent_names = gcc_parent_names_10,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_pcie_0_pipe_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(125000000, P_PCIE_0_PIPE_CLK, 2, 0, 0),
	F(250000000, P_PCIE_0_PIPE_CLK, 1, 0, 0),
	{ }
};

static struct clk_rcg2 pcie_0_pipe_clk_src = {
	.cmd_rcgr = 0x3e01c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_11,
	.freq_tbl = ftbl_pcie_0_pipe_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pcie_0_pipe_clk_src",
		.parent_names = gcc_parent_names_11,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 pclk0_clk_src = {
	.cmd_rcgr = 0x4d000,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_12,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pclk0_clk_src",
		.parent_names = gcc_parent_names_12,
		.num_parents = 4,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_pixel_ops,
	},
};

static const struct freq_tbl ftbl_pdm2_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(64000000, P_GPLL0_OUT_MAIN, 12.5, 0, 0),
	{ }
};

static struct clk_rcg2 pdm2_clk_src = {
	.cmd_rcgr = 0x44010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_pdm2_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pdm2_clk_src",
		.parent_names = gcc_parent_names_0,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_sdcc1_apps_clk_src[] = {
	F(144000, P_XO, 16, 3, 25),
	F(400000, P_XO, 12, 1, 4),
	F(20000000, P_GPLL0_OUT_MAIN, 10, 1, 4),
	F(25000000, P_GPLL0_OUT_MAIN, 16, 1, 2),
	F(50000000, P_GPLL0_OUT_MAIN, 16, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 8, 0, 0),
	F(177777778, P_GPLL0_OUT_MAIN, 4.5, 0, 0),
	F(192000000, P_GPLL4_OUT_MAIN, 6, 0, 0),
	F(200000000, P_GPLL0_OUT_MAIN, 4, 0, 0),
	F(384000000, P_GPLL4_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 sdcc1_apps_clk_src = {
	.cmd_rcgr = 0x42004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_13,
	.freq_tbl = ftbl_sdcc1_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "sdcc1_apps_clk_src",
		.parent_names = gcc_parent_names_13,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_sdcc1_ice_core_clk_src[] = {
	F(160000000, P_GPLL0_OUT_MAIN, 5, 0, 0),
	F(266666667, P_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 sdcc1_ice_core_clk_src = {
	.cmd_rcgr = 0x5d000,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_sdcc1_ice_core_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "sdcc1_ice_core_clk_src",
		.parent_names = gcc_parent_names_3,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_sdcc2_apps_clk_src[] = {
	F(144000, P_XO, 16, 3, 25),
	F(400000, P_XO, 12, 1, 4),
	F(20000000, P_GPLL0_OUT_MAIN, 10, 1, 4),
	F(25000000, P_GPLL0_OUT_MAIN, 16, 1, 2),
	F(50000000, P_GPLL0_OUT_MAIN, 16, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 8, 0, 0),
	F(177777778, P_GPLL0_OUT_MAIN, 4.5, 0, 0),
	F(200000000, P_GPLL0_OUT_MAIN, 4, 0, 0),
	{ }
};

static struct clk_rcg2 sdcc2_apps_clk_src = {
	.cmd_rcgr = 0x43004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_14,
	.freq_tbl = ftbl_sdcc2_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "sdcc2_apps_clk_src",
		.parent_names = gcc_parent_names_14,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 usb20_mock_utmi_clk_src = {
	.cmd_rcgr = 0x41048,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_esc0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "usb20_mock_utmi_clk_src",
		.parent_names = gcc_parent_names_1,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_usb30_master_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 8, 0, 0),
	F(200000000, P_GPLL0_OUT_MAIN, 4, 0, 0),
	F(266666667, P_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 usb30_master_clk_src = {
	.cmd_rcgr = 0x39028,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_usb30_master_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "usb30_master_clk_src",
		.parent_names = gcc_parent_names_0,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 usb30_mock_utmi_clk_src = {
	.cmd_rcgr = 0x3901c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_esc0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "usb30_mock_utmi_clk_src",
		.parent_names = gcc_parent_names_1,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 usb3_phy_aux_clk_src = {
	.cmd_rcgr = 0x3903c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_pcie_0_aux_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "usb3_phy_aux_clk_src",
		.parent_names = gcc_parent_names_1,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_usb_hs_system_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(80000000, P_GPLL0_OUT_MAIN, 10, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 8, 0, 0),
	F(133333333, P_GPLL0_OUT_MAIN, 6, 0, 0),
	F(177777778, P_GPLL0_OUT_MAIN, 4.5, 0, 0),
	{ }
};

static struct clk_rcg2 usb_hs_system_clk_src = {
	.cmd_rcgr = 0x41010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_usb_hs_system_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "usb_hs_system_clk_src",
		.parent_names = gcc_parent_names_3,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 vsync_clk_src = {
	.cmd_rcgr = 0x4d02c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_15,
	.freq_tbl = ftbl_esc0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "vsync_clk_src",
		.parent_names = gcc_parent_names_15,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_apss_ahb_clk = {
	.halt_reg = 0x4601c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(14),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_apss_ahb_clk",
			.parent_names = (const char *[]){
				"apss_ahb_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_apss_tcu_clk = {
	.halt_reg = 0x5b004,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x4500c,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_apss_tcu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_bimc_gfx_clk = {
	.halt_reg = 0x59034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x59034,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_bimc_gfx_clk",
			.ops = &clk_branch2_ops,
			.parent_names = (const char *[]){
				"gcc_apss_tcu_clk",
			},

		},
	},
};

static struct clk_branch gcc_bimc_gpu_clk = {
	.halt_reg = 0x59030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x59030,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_bimc_gpu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_bimc_mdss_clk = {
	.halt_reg = 0x31038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x31038,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_bimc_mdss_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_ahb_clk = {
	.halt_reg = 0x1008,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(10),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_dcc_clk = {
	.halt_reg = 0x77004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x77004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_dcc_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_dcc_xo_clk = {
	.halt_reg = 0x77008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x77008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_dcc_xo_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup0_i2c_apps_clk = {
	.halt_reg = 0x6028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup0_i2c_apps_clk",
			.parent_names = (const char *[]){
				"blsp1_qup0_i2c_apps_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup0_spi_apps_clk = {
	.halt_reg = 0x6024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6024,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup0_spi_apps_clk",
			.parent_names = (const char *[]){
				"blsp1_qup0_spi_apps_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup1_i2c_apps_clk = {
	.halt_reg = 0x2008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup1_i2c_apps_clk",
			.parent_names = (const char *[]){
				"blsp1_qup1_i2c_apps_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup1_spi_apps_clk = {
	.halt_reg = 0x2004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup1_spi_apps_clk",
			.parent_names = (const char *[]){
				"blsp1_qup1_spi_apps_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup2_i2c_apps_clk = {
	.halt_reg = 0x3010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup2_i2c_apps_clk",
			.parent_names = (const char *[]){
				"blsp1_qup2_i2c_apps_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup2_spi_apps_clk = {
	.halt_reg = 0x300c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x300c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup2_spi_apps_clk",
			.parent_names = (const char *[]){
				"blsp1_qup2_spi_apps_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup3_i2c_apps_clk = {
	.halt_reg = 0x4020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup3_i2c_apps_clk",
			.parent_names = (const char *[]){
				"blsp1_qup3_i2c_apps_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup3_spi_apps_clk = {
	.halt_reg = 0x401c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x401c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup3_spi_apps_clk",
			.parent_names = (const char *[]){
				"blsp1_qup3_spi_apps_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup4_i2c_apps_clk = {
	.halt_reg = 0x5020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup4_i2c_apps_clk",
			.parent_names = (const char *[]){
				"blsp1_qup4_i2c_apps_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup4_spi_apps_clk = {
	.halt_reg = 0x501c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x501c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup4_spi_apps_clk",
			.parent_names = (const char *[]){
				"blsp1_qup4_spi_apps_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart0_apps_clk = {
	.halt_reg = 0x6004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart0_apps_clk",
			.parent_names = (const char *[]){
				"blsp1_uart0_apps_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart1_apps_clk = {
	.halt_reg = 0x203c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x203c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart1_apps_clk",
			.parent_names = (const char *[]){
				"blsp1_uart1_apps_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart2_apps_clk = {
	.halt_reg = 0x302c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x302c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart2_apps_clk",
			.parent_names = (const char *[]){
				"blsp1_uart2_apps_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart3_apps_clk = {
	.halt_reg = 0x400c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x400c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart3_apps_clk",
			.parent_names = (const char *[]){
				"blsp1_uart3_apps_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_ahb_clk = {
	.halt_reg = 0xb008,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(20),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp2_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_qup0_i2c_apps_clk = {
	.halt_reg = 0xc008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp2_qup0_i2c_apps_clk",
			.parent_names = (const char *[]){
				"blsp2_qup0_i2c_apps_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_qup0_spi_apps_clk = {
	.halt_reg = 0xc004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp2_qup0_spi_apps_clk",
			.parent_names = (const char *[]){
				"blsp2_qup0_spi_apps_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_uart0_apps_clk = {
	.halt_reg = 0xc03c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc03c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp2_uart0_apps_clk",
			.parent_names = (const char *[]){
				"blsp2_uart0_apps_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_boot_rom_ahb_clk = {
	.halt_reg = 0x1300c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(7),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_boot_rom_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_crypto_ahb_clk = {
	.halt_reg = 0x16024,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_crypto_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_crypto_axi_clk = {
	.halt_reg = 0x16020,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_crypto_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_crypto_clk = {
	.halt_reg = 0x1601c,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_crypto_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_eth_axi_clk = {
	.halt_reg = 0x4e010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4e010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_eth_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_eth_ptp_clk = {
	.halt_reg = 0x4e004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4e004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_eth_ptp_clk",
			.parent_names = (const char *[]){
				"emac_ptp_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_eth_rgmii_clk = {
	.halt_reg = 0x4e008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4e008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_eth_rgmii_clk",
			.parent_names = (const char *[]){
				"emac_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_eth_slave_ahb_clk = {
	.halt_reg = 0x4e00c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4e00c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_eth_slave_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_geni_ir_s_clk = {
	.halt_reg = 0xf008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_geni_ir_s_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_geni_ir_h_clk = {
	.halt_reg = 0xf004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_geni_ir_h_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gfx_tcu_clk = {
	.halt_reg = 0x12020,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x4500C,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gfx_tcu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gfx_tbu_clk = {
	.halt_reg = 0x12010,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x4500C,
		.enable_mask = BIT(3),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gfx_tbu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp1_clk = {
	.halt_reg = 0x8000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gp1_clk",
			.parent_names = (const char *[]){
				"gp1_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp2_clk = {
	.halt_reg = 0x9000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gp2_clk",
			.parent_names = (const char *[]){
				"gp2_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp3_clk = {
	.halt_reg = 0xa000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gp3_clk",
			.parent_names = (const char *[]){
				"gp3_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gtcu_ahb_clk = {
	.halt_reg = 0x12044,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x4500c,
		.enable_mask = BIT(13),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gtcu_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdp_tbu_clk = {
	.halt_reg = 0x1201c,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x4500c,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mdp_tbu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_ahb_clk = {
	.halt_reg = 0x4d07c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4d07c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mdss_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_axi_clk = {
	.halt_reg = 0x4d080,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4d080,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mdss_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_byte0_clk = {
	.halt_reg = 0x4d094,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4d094,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mdss_byte0_clk",
			.parent_names = (const char *[]){
				"byte0_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_esc0_clk = {
	.halt_reg = 0x4d098,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4d098,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mdss_esc0_clk",
			.parent_names = (const char *[]){
				"esc0_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_hdmi_app_clk = {
	.halt_reg = 0x4d0d8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4d0d8,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mdss_hdmi_app_clk",
			.parent_names = (const char *[]){
				"hdmi_app_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_hdmi_pclk_clk = {
	.halt_reg = 0x4d0d4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4d0d4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mdss_hdmi_pclk_clk",
			.parent_names = (const char *[]){
				"hdmi_pclk_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_mdp_clk = {
	.halt_reg = 0x4d088,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4d088,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mdss_mdp_clk",
			.parent_names = (const char *[]){
				"mdp_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_pclk0_clk = {
	.halt_reg = 0x4d084,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4d084,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mdss_pclk0_clk",
			.parent_names = (const char *[]){
				"pclk0_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_vsync_clk = {
	.halt_reg = 0x4d090,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4d090,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mdss_vsync_clk",
			.parent_names = (const char *[]){
				"vsync_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_oxili_ahb_clk = {
	.halt_reg = 0x59028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x59028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_oxili_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_oxili_gfx3d_clk = {
	.halt_reg = 0x59020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x59020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_oxili_gfx3d_clk",
			.parent_names = (const char *[]){
				"gfx3d_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_aux_clk = {
	.halt_reg = 0x3e014,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(27),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_0_aux_clk",
			.parent_names = (const char *[]){
				"pcie_0_aux_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_cfg_ahb_clk = {
	.halt_reg = 0x3e008,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_0_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_mstr_axi_clk = {
	.halt_reg = 0x3e018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(18),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_0_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_pipe_clk = {
	.halt_reg = 0x3e00c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(28),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_0_pipe_clk",
			.parent_names = (const char *[]){
				"pcie_0_pipe_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_slv_axi_clk = {
	.halt_reg = 0x3e010,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(22),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_0_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcnoc_usb2_clk = {
	.halt_reg = 0x27008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x27008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcnoc_usb2_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcnoc_usb3_clk = {
	.halt_reg = 0x2700c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2700c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcnoc_usb3_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm2_clk = {
	.halt_reg = 0x4400c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4400c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pdm2_clk",
			.parent_names = (const char *[]){
				"pdm2_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm_ahb_clk = {
	.halt_reg = 0x44004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x44004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pdm_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_prng_ahb_clk = {
	.halt_reg = 0x13004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(8),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_prng_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

/* PWM clks do not have XO as parent as src clk is a balance root */
static struct clk_branch gcc_pwm0_xo512_clk = {
	.halt_reg = 0x44018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x44018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pwm0_xo512_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pwm1_xo512_clk = {
	.halt_reg = 0x49004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x49004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pwm1_xo512_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pwm2_xo512_clk = {
	.halt_reg = 0x4a004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4a004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pwm2_xo512_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qdss_dap_clk = {
	.halt_reg = 0x29084,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(21),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qdss_dap_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_ahb_clk = {
	.halt_reg = 0x4201c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4201c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc1_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_apps_clk = {
	.halt_reg = 0x42018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x42018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc1_apps_clk",
			.parent_names = (const char *[]){
				"sdcc1_apps_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_ice_core_clk = {
	.halt_reg = 0x5d014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5d014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc1_ice_core_clk",
			.parent_names = (const char *[]){
				"sdcc1_ice_core_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc2_ahb_clk = {
	.halt_reg = 0x4301c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4301c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc2_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc2_apps_clk = {
	.halt_reg = 0x43018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x43018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc2_apps_clk",
			.parent_names = (const char *[]){
				"sdcc2_apps_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_smmu_cfg_clk = {
	.halt_reg = 0x12038,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x3600C,
		.enable_mask = BIT(12),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_smmu_cfg_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sys_noc_usb3_clk = {
	.halt_reg = 0x26014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x26014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sys_noc_usb3_clk",
			.parent_names = (const char *[]){
				"usb30_master_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb_hs_inactivity_timers_clk = {
	.halt_reg = 0x4100C,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4100C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb_hs_inactivity_timers_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb20_mock_utmi_clk = {
	.halt_reg = 0x41044,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x41044,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb20_mock_utmi_clk",
			.parent_names = (const char *[]){
				"usb20_mock_utmi_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb2a_phy_sleep_clk = {
	.halt_reg = 0x4102c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4102c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb2a_phy_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_master_clk = {
	.halt_reg = 0x3900c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3900c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb30_master_clk",
			.parent_names = (const char *[]){
				"usb30_master_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_mock_utmi_clk = {
	.halt_reg = 0x39014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x39014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb30_mock_utmi_clk",
			.parent_names = (const char *[]){
				"usb30_mock_utmi_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_sleep_clk = {
	.halt_reg = 0x39010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x39010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb30_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_phy_aux_clk = {
	.halt_reg = 0x39044,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x39044,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_phy_aux_clk",
			.parent_names = (const char *[]){
				"usb3_phy_aux_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_phy_pipe_clk = {
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x39018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_phy_pipe_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb_hs_phy_cfg_ahb_clk = {
	.halt_reg = 0x41030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x41030,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb_hs_phy_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb_hs_system_clk = {
	.halt_reg = 0x41004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x41004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb_hs_system_clk",
			.parent_names = (const char *[]){
				"usb_hs_system_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_hw *gcc_qcs404_hws[] = {
	&cxo.hw,
};

static struct clk_regmap *gcc_qcs404_clocks[] = {
	[GCC_APSS_AHB_CLK_SRC] = &apss_ahb_clk_src.clkr,
	[GCC_BLSP1_QUP0_I2C_APPS_CLK_SRC] = &blsp1_qup0_i2c_apps_clk_src.clkr,
	[GCC_BLSP1_QUP0_SPI_APPS_CLK_SRC] = &blsp1_qup0_spi_apps_clk_src.clkr,
	[GCC_BLSP1_QUP1_I2C_APPS_CLK_SRC] = &blsp1_qup1_i2c_apps_clk_src.clkr,
	[GCC_BLSP1_QUP1_SPI_APPS_CLK_SRC] = &blsp1_qup1_spi_apps_clk_src.clkr,
	[GCC_BLSP1_QUP2_I2C_APPS_CLK_SRC] = &blsp1_qup2_i2c_apps_clk_src.clkr,
	[GCC_BLSP1_QUP2_SPI_APPS_CLK_SRC] = &blsp1_qup2_spi_apps_clk_src.clkr,
	[GCC_BLSP1_QUP3_I2C_APPS_CLK_SRC] = &blsp1_qup3_i2c_apps_clk_src.clkr,
	[GCC_BLSP1_QUP3_SPI_APPS_CLK_SRC] = &blsp1_qup3_spi_apps_clk_src.clkr,
	[GCC_BLSP1_QUP4_I2C_APPS_CLK_SRC] = &blsp1_qup4_i2c_apps_clk_src.clkr,
	[GCC_BLSP1_QUP4_SPI_APPS_CLK_SRC] = &blsp1_qup4_spi_apps_clk_src.clkr,
	[GCC_BLSP1_UART0_APPS_CLK_SRC] = &blsp1_uart0_apps_clk_src.clkr,
	[GCC_BLSP1_UART1_APPS_CLK_SRC] = &blsp1_uart1_apps_clk_src.clkr,
	[GCC_BLSP1_UART2_APPS_CLK_SRC] = &blsp1_uart2_apps_clk_src.clkr,
	[GCC_BLSP1_UART3_APPS_CLK_SRC] = &blsp1_uart3_apps_clk_src.clkr,
	[GCC_BLSP2_QUP0_I2C_APPS_CLK_SRC] = &blsp2_qup0_i2c_apps_clk_src.clkr,
	[GCC_BLSP2_QUP0_SPI_APPS_CLK_SRC] = &blsp2_qup0_spi_apps_clk_src.clkr,
	[GCC_BLSP2_UART0_APPS_CLK_SRC] = &blsp2_uart0_apps_clk_src.clkr,
	[GCC_BYTE0_CLK_SRC] = &byte0_clk_src.clkr,
	[GCC_EMAC_CLK_SRC] = &emac_clk_src.clkr,
	[GCC_EMAC_PTP_CLK_SRC] = &emac_ptp_clk_src.clkr,
	[GCC_ESC0_CLK_SRC] = &esc0_clk_src.clkr,
	[GCC_APSS_AHB_CLK] = &gcc_apss_ahb_clk.clkr,
	[GCC_BIMC_GFX_CLK] = &gcc_bimc_gfx_clk.clkr,
	[GCC_BIMC_MDSS_CLK] = &gcc_bimc_mdss_clk.clkr,
	[GCC_BLSP1_AHB_CLK] = &gcc_blsp1_ahb_clk.clkr,
	[GCC_BLSP1_QUP0_I2C_APPS_CLK] = &gcc_blsp1_qup0_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP0_SPI_APPS_CLK] = &gcc_blsp1_qup0_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP1_I2C_APPS_CLK] = &gcc_blsp1_qup1_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP1_SPI_APPS_CLK] = &gcc_blsp1_qup1_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP2_I2C_APPS_CLK] = &gcc_blsp1_qup2_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP2_SPI_APPS_CLK] = &gcc_blsp1_qup2_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP3_I2C_APPS_CLK] = &gcc_blsp1_qup3_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP3_SPI_APPS_CLK] = &gcc_blsp1_qup3_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP4_I2C_APPS_CLK] = &gcc_blsp1_qup4_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP4_SPI_APPS_CLK] = &gcc_blsp1_qup4_spi_apps_clk.clkr,
	[GCC_BLSP1_UART0_APPS_CLK] = &gcc_blsp1_uart0_apps_clk.clkr,
	[GCC_BLSP1_UART1_APPS_CLK] = &gcc_blsp1_uart1_apps_clk.clkr,
	[GCC_BLSP1_UART2_APPS_CLK] = &gcc_blsp1_uart2_apps_clk.clkr,
	[GCC_BLSP1_UART3_APPS_CLK] = &gcc_blsp1_uart3_apps_clk.clkr,
	[GCC_BLSP2_AHB_CLK] = &gcc_blsp2_ahb_clk.clkr,
	[GCC_BLSP2_QUP0_I2C_APPS_CLK] = &gcc_blsp2_qup0_i2c_apps_clk.clkr,
	[GCC_BLSP2_QUP0_SPI_APPS_CLK] = &gcc_blsp2_qup0_spi_apps_clk.clkr,
	[GCC_BLSP2_UART0_APPS_CLK] = &gcc_blsp2_uart0_apps_clk.clkr,
	[GCC_BOOT_ROM_AHB_CLK] = &gcc_boot_rom_ahb_clk.clkr,
	[GCC_ETH_AXI_CLK] = &gcc_eth_axi_clk.clkr,
	[GCC_ETH_PTP_CLK] = &gcc_eth_ptp_clk.clkr,
	[GCC_ETH_RGMII_CLK] = &gcc_eth_rgmii_clk.clkr,
	[GCC_ETH_SLAVE_AHB_CLK] = &gcc_eth_slave_ahb_clk.clkr,
	[GCC_GENI_IR_S_CLK] = &gcc_geni_ir_s_clk.clkr,
	[GCC_GENI_IR_H_CLK] = &gcc_geni_ir_h_clk.clkr,
	[GCC_GP1_CLK] = &gcc_gp1_clk.clkr,
	[GCC_GP2_CLK] = &gcc_gp2_clk.clkr,
	[GCC_GP3_CLK] = &gcc_gp3_clk.clkr,
	[GCC_MDSS_AHB_CLK] = &gcc_mdss_ahb_clk.clkr,
	[GCC_MDSS_AXI_CLK] = &gcc_mdss_axi_clk.clkr,
	[GCC_MDSS_BYTE0_CLK] = &gcc_mdss_byte0_clk.clkr,
	[GCC_MDSS_ESC0_CLK] = &gcc_mdss_esc0_clk.clkr,
	[GCC_MDSS_HDMI_APP_CLK] = &gcc_mdss_hdmi_app_clk.clkr,
	[GCC_MDSS_HDMI_PCLK_CLK] = &gcc_mdss_hdmi_pclk_clk.clkr,
	[GCC_MDSS_MDP_CLK] = &gcc_mdss_mdp_clk.clkr,
	[GCC_MDSS_PCLK0_CLK] = &gcc_mdss_pclk0_clk.clkr,
	[GCC_MDSS_VSYNC_CLK] = &gcc_mdss_vsync_clk.clkr,
	[GCC_OXILI_AHB_CLK] = &gcc_oxili_ahb_clk.clkr,
	[GCC_OXILI_GFX3D_CLK] = &gcc_oxili_gfx3d_clk.clkr,
	[GCC_PCIE_0_AUX_CLK] = &gcc_pcie_0_aux_clk.clkr,
	[GCC_PCIE_0_CFG_AHB_CLK] = &gcc_pcie_0_cfg_ahb_clk.clkr,
	[GCC_PCIE_0_MSTR_AXI_CLK] = &gcc_pcie_0_mstr_axi_clk.clkr,
	[GCC_PCIE_0_PIPE_CLK] = &gcc_pcie_0_pipe_clk.clkr,
	[GCC_PCIE_0_SLV_AXI_CLK] = &gcc_pcie_0_slv_axi_clk.clkr,
	[GCC_PCNOC_USB2_CLK] = &gcc_pcnoc_usb2_clk.clkr,
	[GCC_PCNOC_USB3_CLK] = &gcc_pcnoc_usb3_clk.clkr,
	[GCC_PDM2_CLK] = &gcc_pdm2_clk.clkr,
	[GCC_PDM_AHB_CLK] = &gcc_pdm_ahb_clk.clkr,
	[GCC_PRNG_AHB_CLK] = &gcc_prng_ahb_clk.clkr,
	[GCC_PWM0_XO512_CLK] = &gcc_pwm0_xo512_clk.clkr,
	[GCC_PWM1_XO512_CLK] = &gcc_pwm1_xo512_clk.clkr,
	[GCC_PWM2_XO512_CLK] = &gcc_pwm2_xo512_clk.clkr,
	[GCC_SDCC1_AHB_CLK] = &gcc_sdcc1_ahb_clk.clkr,
	[GCC_SDCC1_APPS_CLK] = &gcc_sdcc1_apps_clk.clkr,
	[GCC_SDCC1_ICE_CORE_CLK] = &gcc_sdcc1_ice_core_clk.clkr,
	[GCC_SDCC2_AHB_CLK] = &gcc_sdcc2_ahb_clk.clkr,
	[GCC_SDCC2_APPS_CLK] = &gcc_sdcc2_apps_clk.clkr,
	[GCC_SYS_NOC_USB3_CLK] = &gcc_sys_noc_usb3_clk.clkr,
	[GCC_USB20_MOCK_UTMI_CLK] = &gcc_usb20_mock_utmi_clk.clkr,
	[GCC_USB2A_PHY_SLEEP_CLK] = &gcc_usb2a_phy_sleep_clk.clkr,
	[GCC_USB30_MASTER_CLK] = &gcc_usb30_master_clk.clkr,
	[GCC_USB30_MOCK_UTMI_CLK] = &gcc_usb30_mock_utmi_clk.clkr,
	[GCC_USB30_SLEEP_CLK] = &gcc_usb30_sleep_clk.clkr,
	[GCC_USB3_PHY_AUX_CLK] = &gcc_usb3_phy_aux_clk.clkr,
	[GCC_USB3_PHY_PIPE_CLK] = &gcc_usb3_phy_pipe_clk.clkr,
	[GCC_USB_HS_PHY_CFG_AHB_CLK] = &gcc_usb_hs_phy_cfg_ahb_clk.clkr,
	[GCC_USB_HS_SYSTEM_CLK] = &gcc_usb_hs_system_clk.clkr,
	[GCC_GFX3D_CLK_SRC] = &gfx3d_clk_src.clkr,
	[GCC_GP1_CLK_SRC] = &gp1_clk_src.clkr,
	[GCC_GP2_CLK_SRC] = &gp2_clk_src.clkr,
	[GCC_GP3_CLK_SRC] = &gp3_clk_src.clkr,
	[GCC_GPLL0_OUT_MAIN] = &gpll0_out_main.clkr,
	[GCC_GPLL0_AO_OUT_MAIN] = &gpll0_ao_out_main.clkr,
	[GCC_GPLL0_SLEEP_CLK_SRC] = &gpll0_sleep_clk_src.clkr,
	[GCC_GPLL1_OUT_MAIN] = &gpll1_out_main.clkr,
	[GCC_GPLL3_OUT_MAIN] = &gpll3_out_main.clkr,
	[GCC_GPLL4_OUT_MAIN] = &gpll4_out_main.clkr,
	[GCC_GPLL6] = &gpll6.clkr,
	[GCC_GPLL6_OUT_AUX] = &gpll6_out_aux,
	[GCC_HDMI_APP_CLK_SRC] = &hdmi_app_clk_src.clkr,
	[GCC_HDMI_PCLK_CLK_SRC] = &hdmi_pclk_clk_src.clkr,
	[GCC_MDP_CLK_SRC] = &mdp_clk_src.clkr,
	[GCC_PCIE_0_AUX_CLK_SRC] = &pcie_0_aux_clk_src.clkr,
	[GCC_PCIE_0_PIPE_CLK_SRC] = &pcie_0_pipe_clk_src.clkr,
	[GCC_PCLK0_CLK_SRC] = &pclk0_clk_src.clkr,
	[GCC_PDM2_CLK_SRC] = &pdm2_clk_src.clkr,
	[GCC_SDCC1_APPS_CLK_SRC] = &sdcc1_apps_clk_src.clkr,
	[GCC_SDCC1_ICE_CORE_CLK_SRC] = &sdcc1_ice_core_clk_src.clkr,
	[GCC_SDCC2_APPS_CLK_SRC] = &sdcc2_apps_clk_src.clkr,
	[GCC_USB20_MOCK_UTMI_CLK_SRC] = &usb20_mock_utmi_clk_src.clkr,
	[GCC_USB30_MASTER_CLK_SRC] = &usb30_master_clk_src.clkr,
	[GCC_USB30_MOCK_UTMI_CLK_SRC] = &usb30_mock_utmi_clk_src.clkr,
	[GCC_USB3_PHY_AUX_CLK_SRC] = &usb3_phy_aux_clk_src.clkr,
	[GCC_USB_HS_SYSTEM_CLK_SRC] = &usb_hs_system_clk_src.clkr,
	[GCC_VSYNC_CLK_SRC] = &vsync_clk_src.clkr,
	[GCC_USB_HS_INACTIVITY_TIMERS_CLK] =
			&gcc_usb_hs_inactivity_timers_clk.clkr,
	[GCC_BIMC_GPU_CLK] = &gcc_bimc_gpu_clk.clkr,
	[GCC_GTCU_AHB_CLK] = &gcc_gtcu_ahb_clk.clkr,
	[GCC_GFX_TCU_CLK] = &gcc_gfx_tcu_clk.clkr,
	[GCC_GFX_TBU_CLK] = &gcc_gfx_tbu_clk.clkr,
	[GCC_SMMU_CFG_CLK] = &gcc_smmu_cfg_clk.clkr,
	[GCC_APSS_TCU_CLK] = &gcc_apss_tcu_clk.clkr,
	[GCC_CRYPTO_AHB_CLK] = &gcc_crypto_ahb_clk.clkr,
	[GCC_CRYPTO_AXI_CLK] = &gcc_crypto_axi_clk.clkr,
	[GCC_CRYPTO_CLK] = &gcc_crypto_clk.clkr,
	[GCC_MDP_TBU_CLK] = &gcc_mdp_tbu_clk.clkr,
	[GCC_QDSS_DAP_CLK] = &gcc_qdss_dap_clk.clkr,
	[GCC_DCC_CLK] = &gcc_dcc_clk.clkr,
	[GCC_DCC_XO_CLK] = &gcc_dcc_xo_clk.clkr,
};

static const struct qcom_reset_map gcc_qcs404_resets[] = {
	[GCC_GENI_IR_BCR] = { 0x0F000 },
	[GCC_USB_HS_BCR] = { 0x41000 },
	[GCC_USB2_HS_PHY_ONLY_BCR] = { 0x41034 },
	[GCC_QUSB2_PHY_BCR] = { 0x4103c },
	[GCC_USB_HS_PHY_CFG_AHB_BCR] = { 0x0000c, 1 },
	[GCC_USB2A_PHY_BCR] = { 0x0000c, 0 },
	[GCC_USB3_PHY_BCR] = { 0x39004 },
	[GCC_USB_30_BCR] = { 0x39000 },
	[GCC_USB3PHY_PHY_BCR] = { 0x39008 },
	[GCC_PCIE_0_BCR] = { 0x3e000 },
	[GCC_PCIE_0_PHY_BCR] = { 0x3e004 },
	[GCC_PCIE_0_LINK_DOWN_BCR] = { 0x3e038 },
	[GCC_PCIEPHY_0_PHY_BCR] = { 0x3e03c },
	[GCC_EMAC_BCR] = { 0x4e000 },
};

static const struct regmap_config gcc_qcs404_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x7f000,
	.fast_io	= true,
};

static const struct qcom_cc_desc gcc_qcs404_desc = {
	.config = &gcc_qcs404_regmap_config,
	.clks = gcc_qcs404_clocks,
	.num_clks = ARRAY_SIZE(gcc_qcs404_clocks),
	.resets = gcc_qcs404_resets,
	.num_resets = ARRAY_SIZE(gcc_qcs404_resets),
};

static const struct of_device_id gcc_qcs404_match_table[] = {
	{ .compatible = "qcom,gcc-qcs404" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_qcs404_match_table);

static int gcc_qcs404_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret, i;

	regmap = qcom_cc_map(pdev, &gcc_qcs404_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	clk_alpha_pll_configure(&gpll3_out_main, regmap, &gpll3_config);

	for (i = 0; i < ARRAY_SIZE(gcc_qcs404_hws); i++) {
		ret = devm_clk_hw_register(&pdev->dev, gcc_qcs404_hws[i]);
		if (ret)
			return ret;
	}

	return qcom_cc_really_probe(pdev, &gcc_qcs404_desc, regmap);
}

static struct platform_driver gcc_qcs404_driver = {
	.probe = gcc_qcs404_probe,
	.driver = {
		.name = "gcc-qcs404",
		.of_match_table = gcc_qcs404_match_table,
	},
};

static int __init gcc_qcs404_init(void)
{
	return platform_driver_register(&gcc_qcs404_driver);
}
subsys_initcall(gcc_qcs404_init);

static void __exit gcc_qcs404_exit(void)
{
	platform_driver_unregister(&gcc_qcs404_driver);
}
module_exit(gcc_qcs404_exit);

MODULE_DESCRIPTION("Qualcomm GCC QCS404 Driver");
MODULE_LICENSE("GPL v2");
