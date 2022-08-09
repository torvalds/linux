// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

#include <dt-bindings/clock/qcom,gcc-msm8996.h>

#include "common.h"
#include "clk-regmap.h"
#include "clk-alpha-pll.h"
#include "clk-rcg.h"
#include "clk-branch.h"
#include "reset.h"
#include "gdsc.h"

enum {
	P_XO,
	P_GPLL0,
	P_GPLL2,
	P_GPLL3,
	P_GPLL1,
	P_GPLL2_EARLY,
	P_GPLL0_EARLY_DIV,
	P_SLEEP_CLK,
	P_GPLL4,
	P_AUD_REF_CLK,
	P_GPLL1_EARLY_DIV
};

static const struct parent_map gcc_sleep_clk_map[] = {
	{ P_SLEEP_CLK, 5 }
};

static const char * const gcc_sleep_clk[] = {
	"sleep_clk"
};

static const struct parent_map gcc_xo_gpll0_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 }
};

static const char * const gcc_xo_gpll0[] = {
	"xo",
	"gpll0"
};

static const struct parent_map gcc_xo_sleep_clk_map[] = {
	{ P_XO, 0 },
	{ P_SLEEP_CLK, 5 }
};

static const char * const gcc_xo_sleep_clk[] = {
	"xo",
	"sleep_clk"
};

static const struct parent_map gcc_xo_gpll0_gpll0_early_div_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL0_EARLY_DIV, 6 }
};

static const char * const gcc_xo_gpll0_gpll0_early_div[] = {
	"xo",
	"gpll0",
	"gpll0_early_div"
};

static const struct parent_map gcc_xo_gpll0_gpll4_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL4, 5 }
};

static const char * const gcc_xo_gpll0_gpll4[] = {
	"xo",
	"gpll0",
	"gpll4"
};

static const struct parent_map gcc_xo_gpll0_aud_ref_clk_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_AUD_REF_CLK, 2 }
};

static const char * const gcc_xo_gpll0_aud_ref_clk[] = {
	"xo",
	"gpll0",
	"aud_ref_clk"
};

static const struct parent_map gcc_xo_gpll0_sleep_clk_gpll0_early_div_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_SLEEP_CLK, 5 },
	{ P_GPLL0_EARLY_DIV, 6 }
};

static const char * const gcc_xo_gpll0_sleep_clk_gpll0_early_div[] = {
	"xo",
	"gpll0",
	"sleep_clk",
	"gpll0_early_div"
};

static const struct parent_map gcc_xo_gpll0_gpll4_gpll0_early_div_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL4, 5 },
	{ P_GPLL0_EARLY_DIV, 6 }
};

static const char * const gcc_xo_gpll0_gpll4_gpll0_early_div[] = {
	"xo",
	"gpll0",
	"gpll4",
	"gpll0_early_div"
};

static const struct parent_map gcc_xo_gpll0_gpll1_early_div_gpll1_gpll4_gpll0_early_div_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL1_EARLY_DIV, 3 },
	{ P_GPLL1, 4 },
	{ P_GPLL4, 5 },
	{ P_GPLL0_EARLY_DIV, 6 }
};

static const char * const gcc_xo_gpll0_gpll1_early_div_gpll1_gpll4_gpll0_early_div[] = {
	"xo",
	"gpll0",
	"gpll1_early_div",
	"gpll1",
	"gpll4",
	"gpll0_early_div"
};

static const struct parent_map gcc_xo_gpll0_gpll2_gpll3_gpll1_gpll2_early_gpll0_early_div_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL2, 2 },
	{ P_GPLL3, 3 },
	{ P_GPLL1, 4 },
	{ P_GPLL2_EARLY, 5 },
	{ P_GPLL0_EARLY_DIV, 6 }
};

static const char * const gcc_xo_gpll0_gpll2_gpll3_gpll1_gpll2_early_gpll0_early_div[] = {
	"xo",
	"gpll0",
	"gpll2",
	"gpll3",
	"gpll1",
	"gpll2_early",
	"gpll0_early_div"
};

static struct clk_fixed_factor xo = {
	.mult = 1,
	.div = 1,
	.hw.init = &(struct clk_init_data){
		.name = "xo",
		.parent_names = (const char *[]){ "xo_board" },
		.num_parents = 1,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct clk_alpha_pll gpll0_early = {
	.offset = 0x00000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpll0_early",
			.parent_names = (const char *[]){ "xo" },
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct clk_fixed_factor gpll0_early_div = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "gpll0_early_div",
		.parent_names = (const char *[]){ "gpll0_early" },
		.num_parents = 1,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct clk_alpha_pll_postdiv gpll0 = {
	.offset = 0x00000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll0",
		.parent_names = (const char *[]){ "gpll0_early" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ops,
	},
};

static struct clk_branch gcc_mmss_gpll0_div_clk = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x5200c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mmss_gpll0_div_clk",
			.parent_names = (const char *[]){ "gpll0" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mss_gpll0_div_clk = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x5200c,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mss_gpll0_div_clk",
			.parent_names = (const char *[]){ "gpll0" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops
		},
	},
};

static struct clk_alpha_pll gpll4_early = {
	.offset = 0x77000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gpll4_early",
			.parent_names = (const char *[]){ "xo" },
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct clk_alpha_pll_postdiv gpll4 = {
	.offset = 0x77000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll4",
		.parent_names = (const char *[]){ "gpll4_early" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ops,
	},
};

static const struct freq_tbl ftbl_system_noc_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(50000000, P_GPLL0_EARLY_DIV, 6, 0, 0),
	F(100000000, P_GPLL0, 6, 0, 0),
	F(150000000, P_GPLL0, 4, 0, 0),
	F(200000000, P_GPLL0, 3, 0, 0),
	F(240000000, P_GPLL0, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 system_noc_clk_src = {
	.cmd_rcgr = 0x0401c,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll2_gpll3_gpll1_gpll2_early_gpll0_early_div_map,
	.freq_tbl = ftbl_system_noc_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "system_noc_clk_src",
		.parent_names = gcc_xo_gpll0_gpll2_gpll3_gpll1_gpll2_early_gpll0_early_div,
		.num_parents = 7,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_config_noc_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(37500000, P_GPLL0, 16, 0, 0),
	F(75000000, P_GPLL0, 8, 0, 0),
	{ }
};

static struct clk_rcg2 config_noc_clk_src = {
	.cmd_rcgr = 0x0500c,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_config_noc_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "config_noc_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_periph_noc_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(37500000, P_GPLL0, 16, 0, 0),
	F(50000000, P_GPLL0, 12, 0, 0),
	F(75000000, P_GPLL0, 8, 0, 0),
	F(100000000, P_GPLL0, 6, 0, 0),
	{ }
};

static struct clk_rcg2 periph_noc_clk_src = {
	.cmd_rcgr = 0x06014,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_periph_noc_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "periph_noc_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_usb30_master_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(120000000, P_GPLL0, 5, 0, 0),
	F(150000000, P_GPLL0, 4, 0, 0),
	{ }
};

static struct clk_rcg2 usb30_master_clk_src = {
	.cmd_rcgr = 0x0f014,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll0_early_div_map,
	.freq_tbl = ftbl_usb30_master_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "usb30_master_clk_src",
		.parent_names = gcc_xo_gpll0_gpll0_early_div,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_usb30_mock_utmi_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 usb30_mock_utmi_clk_src = {
	.cmd_rcgr = 0x0f028,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll0_early_div_map,
	.freq_tbl = ftbl_usb30_mock_utmi_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "usb30_mock_utmi_clk_src",
		.parent_names = gcc_xo_gpll0_gpll0_early_div,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_usb3_phy_aux_clk_src[] = {
	F(1200000, P_XO, 16, 0, 0),
	{ }
};

static struct clk_rcg2 usb3_phy_aux_clk_src = {
	.cmd_rcgr = 0x5000c,
	.hid_width = 5,
	.parent_map = gcc_xo_sleep_clk_map,
	.freq_tbl = ftbl_usb3_phy_aux_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "usb3_phy_aux_clk_src",
		.parent_names = gcc_xo_sleep_clk,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_usb20_master_clk_src[] = {
	F(120000000, P_GPLL0, 5, 0, 0),
	{ }
};

static struct clk_rcg2 usb20_master_clk_src = {
	.cmd_rcgr = 0x12010,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll0_early_div_map,
	.freq_tbl = ftbl_usb20_master_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "usb20_master_clk_src",
		.parent_names = gcc_xo_gpll0_gpll0_early_div,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 usb20_mock_utmi_clk_src = {
	.cmd_rcgr = 0x12024,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll0_early_div_map,
	.freq_tbl = ftbl_usb30_mock_utmi_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "usb20_mock_utmi_clk_src",
		.parent_names = gcc_xo_gpll0_gpll0_early_div,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_sdcc1_apps_clk_src[] = {
	F(144000, P_XO, 16, 3, 25),
	F(400000, P_XO, 12, 1, 4),
	F(20000000, P_GPLL0, 15, 1, 2),
	F(25000000, P_GPLL0, 12, 1, 2),
	F(50000000, P_GPLL0, 12, 0, 0),
	F(96000000, P_GPLL4, 4, 0, 0),
	F(192000000, P_GPLL4, 2, 0, 0),
	F(384000000, P_GPLL4, 1, 0, 0),
	{ }
};

static struct clk_rcg2 sdcc1_apps_clk_src = {
	.cmd_rcgr = 0x13010,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll4_gpll0_early_div_map,
	.freq_tbl = ftbl_sdcc1_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "sdcc1_apps_clk_src",
		.parent_names = gcc_xo_gpll0_gpll4_gpll0_early_div,
		.num_parents = 4,
		.ops = &clk_rcg2_floor_ops,
	},
};

static struct freq_tbl ftbl_sdcc1_ice_core_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(150000000, P_GPLL0, 4, 0, 0),
	F(300000000, P_GPLL0, 2, 0, 0),
	{ }
};

static struct clk_rcg2 sdcc1_ice_core_clk_src = {
	.cmd_rcgr = 0x13024,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll4_gpll0_early_div_map,
	.freq_tbl = ftbl_sdcc1_ice_core_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "sdcc1_ice_core_clk_src",
		.parent_names = gcc_xo_gpll0_gpll4_gpll0_early_div,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_sdcc2_apps_clk_src[] = {
	F(144000, P_XO, 16, 3, 25),
	F(400000, P_XO, 12, 1, 4),
	F(20000000, P_GPLL0, 15, 1, 2),
	F(25000000, P_GPLL0, 12, 1, 2),
	F(50000000, P_GPLL0, 12, 0, 0),
	F(100000000, P_GPLL0, 6, 0, 0),
	F(200000000, P_GPLL0, 3, 0, 0),
	{ }
};

static struct clk_rcg2 sdcc2_apps_clk_src = {
	.cmd_rcgr = 0x14010,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll4_map,
	.freq_tbl = ftbl_sdcc2_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "sdcc2_apps_clk_src",
		.parent_names = gcc_xo_gpll0_gpll4,
		.num_parents = 3,
		.ops = &clk_rcg2_floor_ops,
	},
};

static struct clk_rcg2 sdcc3_apps_clk_src = {
	.cmd_rcgr = 0x15010,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll4_map,
	.freq_tbl = ftbl_sdcc2_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "sdcc3_apps_clk_src",
		.parent_names = gcc_xo_gpll0_gpll4,
		.num_parents = 3,
		.ops = &clk_rcg2_floor_ops,
	},
};

static const struct freq_tbl ftbl_sdcc4_apps_clk_src[] = {
	F(144000, P_XO, 16, 3, 25),
	F(400000, P_XO, 12, 1, 4),
	F(20000000, P_GPLL0, 15, 1, 2),
	F(25000000, P_GPLL0, 12, 1, 2),
	F(50000000, P_GPLL0, 12, 0, 0),
	F(100000000, P_GPLL0, 6, 0, 0),
	{ }
};

static struct clk_rcg2 sdcc4_apps_clk_src = {
	.cmd_rcgr = 0x16010,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_sdcc4_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "sdcc4_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_floor_ops,
	},
};

static const struct freq_tbl ftbl_blsp1_qup1_spi_apps_clk_src[] = {
	F(960000, P_XO, 10, 1, 2),
	F(4800000, P_XO, 4, 0, 0),
	F(9600000, P_XO, 2, 0, 0),
	F(15000000, P_GPLL0, 10, 1, 4),
	F(19200000, P_XO, 1, 0, 0),
	F(25000000, P_GPLL0, 12, 1, 2),
	F(50000000, P_GPLL0, 12, 0, 0),
	{ }
};

static struct clk_rcg2 blsp1_qup1_spi_apps_clk_src = {
	.cmd_rcgr = 0x1900c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_qup1_spi_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup1_spi_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_blsp1_qup1_i2c_apps_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(50000000, P_GPLL0, 12, 0, 0),
	{ }
};

static struct clk_rcg2 blsp1_qup1_i2c_apps_clk_src = {
	.cmd_rcgr = 0x19020,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_qup1_i2c_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup1_i2c_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_blsp1_uart1_apps_clk_src[] = {
	F(3686400, P_GPLL0, 1, 96, 15625),
	F(7372800, P_GPLL0, 1, 192, 15625),
	F(14745600, P_GPLL0, 1, 384, 15625),
	F(16000000, P_GPLL0, 5, 2, 15),
	F(19200000, P_XO, 1, 0, 0),
	F(24000000, P_GPLL0, 5, 1, 5),
	F(32000000, P_GPLL0, 1, 4, 75),
	F(40000000, P_GPLL0, 15, 0, 0),
	F(46400000, P_GPLL0, 1, 29, 375),
	F(48000000, P_GPLL0, 12.5, 0, 0),
	F(51200000, P_GPLL0, 1, 32, 375),
	F(56000000, P_GPLL0, 1, 7, 75),
	F(58982400, P_GPLL0, 1, 1536, 15625),
	F(60000000, P_GPLL0, 10, 0, 0),
	F(63157895, P_GPLL0, 9.5, 0, 0),
	{ }
};

static struct clk_rcg2 blsp1_uart1_apps_clk_src = {
	.cmd_rcgr = 0x1a00c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_uart1_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart1_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup2_spi_apps_clk_src = {
	.cmd_rcgr = 0x1b00c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_qup1_spi_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup2_spi_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup2_i2c_apps_clk_src = {
	.cmd_rcgr = 0x1b020,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_qup1_i2c_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup2_i2c_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_uart2_apps_clk_src = {
	.cmd_rcgr = 0x1c00c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_uart1_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart2_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup3_spi_apps_clk_src = {
	.cmd_rcgr = 0x1d00c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_qup1_spi_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup3_spi_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup3_i2c_apps_clk_src = {
	.cmd_rcgr = 0x1d020,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_qup1_i2c_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup3_i2c_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_uart3_apps_clk_src = {
	.cmd_rcgr = 0x1e00c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_uart1_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart3_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup4_spi_apps_clk_src = {
	.cmd_rcgr = 0x1f00c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_qup1_spi_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup4_spi_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup4_i2c_apps_clk_src = {
	.cmd_rcgr = 0x1f020,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_qup1_i2c_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup4_i2c_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_uart4_apps_clk_src = {
	.cmd_rcgr = 0x2000c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_uart1_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart4_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup5_spi_apps_clk_src = {
	.cmd_rcgr = 0x2100c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_qup1_spi_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup5_spi_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup5_i2c_apps_clk_src = {
	.cmd_rcgr = 0x21020,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_qup1_i2c_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup5_i2c_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_uart5_apps_clk_src = {
	.cmd_rcgr = 0x2200c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_uart1_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart5_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup6_spi_apps_clk_src = {
	.cmd_rcgr = 0x2300c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_qup1_spi_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup6_spi_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup6_i2c_apps_clk_src = {
	.cmd_rcgr = 0x23020,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_qup1_i2c_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup6_i2c_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_uart6_apps_clk_src = {
	.cmd_rcgr = 0x2400c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_uart1_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart6_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_qup1_spi_apps_clk_src = {
	.cmd_rcgr = 0x2600c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_qup1_spi_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp2_qup1_spi_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_qup1_i2c_apps_clk_src = {
	.cmd_rcgr = 0x26020,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_qup1_i2c_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp2_qup1_i2c_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_uart1_apps_clk_src = {
	.cmd_rcgr = 0x2700c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_uart1_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp2_uart1_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_qup2_spi_apps_clk_src = {
	.cmd_rcgr = 0x2800c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_qup1_spi_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp2_qup2_spi_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_qup2_i2c_apps_clk_src = {
	.cmd_rcgr = 0x28020,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_qup1_i2c_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp2_qup2_i2c_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_uart2_apps_clk_src = {
	.cmd_rcgr = 0x2900c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_uart1_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp2_uart2_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_qup3_spi_apps_clk_src = {
	.cmd_rcgr = 0x2a00c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_qup1_spi_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp2_qup3_spi_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_qup3_i2c_apps_clk_src = {
	.cmd_rcgr = 0x2a020,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_qup1_i2c_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp2_qup3_i2c_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_uart3_apps_clk_src = {
	.cmd_rcgr = 0x2b00c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_uart1_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp2_uart3_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_qup4_spi_apps_clk_src = {
	.cmd_rcgr = 0x2c00c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_qup1_spi_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp2_qup4_spi_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_qup4_i2c_apps_clk_src = {
	.cmd_rcgr = 0x2c020,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_qup1_i2c_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp2_qup4_i2c_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_uart4_apps_clk_src = {
	.cmd_rcgr = 0x2d00c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_uart1_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp2_uart4_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_qup5_spi_apps_clk_src = {
	.cmd_rcgr = 0x2e00c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_qup1_spi_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp2_qup5_spi_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_qup5_i2c_apps_clk_src = {
	.cmd_rcgr = 0x2e020,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_qup1_i2c_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp2_qup5_i2c_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_uart5_apps_clk_src = {
	.cmd_rcgr = 0x2f00c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_uart1_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp2_uart5_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_qup6_spi_apps_clk_src = {
	.cmd_rcgr = 0x3000c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_qup1_spi_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp2_qup6_spi_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_qup6_i2c_apps_clk_src = {
	.cmd_rcgr = 0x30020,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_qup1_i2c_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp2_qup6_i2c_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_uart6_apps_clk_src = {
	.cmd_rcgr = 0x3100c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_blsp1_uart1_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp2_uart6_apps_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_pdm2_clk_src[] = {
	F(60000000, P_GPLL0, 10, 0, 0),
	{ }
};

static struct clk_rcg2 pdm2_clk_src = {
	.cmd_rcgr = 0x33010,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_pdm2_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pdm2_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_tsif_ref_clk_src[] = {
	F(105495, P_XO, 1, 1, 182),
	{ }
};

static struct clk_rcg2 tsif_ref_clk_src = {
	.cmd_rcgr = 0x36010,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_aud_ref_clk_map,
	.freq_tbl = ftbl_tsif_ref_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "tsif_ref_clk_src",
		.parent_names = gcc_xo_gpll0_aud_ref_clk,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_sleep_clk_src = {
	.cmd_rcgr = 0x43014,
	.hid_width = 5,
	.parent_map = gcc_sleep_clk_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_sleep_clk_src",
		.parent_names = gcc_sleep_clk,
		.num_parents = 1,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 hmss_rbcpr_clk_src = {
	.cmd_rcgr = 0x48040,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_usb30_mock_utmi_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "hmss_rbcpr_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 hmss_gpll0_clk_src = {
	.cmd_rcgr = 0x48058,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "hmss_gpll0_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gp1_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(100000000, P_GPLL0, 6, 0, 0),
	F(200000000, P_GPLL0, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gp1_clk_src = {
	.cmd_rcgr = 0x64004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_sleep_clk_gpll0_early_div_map,
	.freq_tbl = ftbl_gp1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gp1_clk_src",
		.parent_names = gcc_xo_gpll0_sleep_clk_gpll0_early_div,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gp2_clk_src = {
	.cmd_rcgr = 0x65004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_sleep_clk_gpll0_early_div_map,
	.freq_tbl = ftbl_gp1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gp2_clk_src",
		.parent_names = gcc_xo_gpll0_sleep_clk_gpll0_early_div,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gp3_clk_src = {
	.cmd_rcgr = 0x66004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_sleep_clk_gpll0_early_div_map,
	.freq_tbl = ftbl_gp1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gp3_clk_src",
		.parent_names = gcc_xo_gpll0_sleep_clk_gpll0_early_div,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_pcie_aux_clk_src[] = {
	F(1010526, P_XO, 1, 1, 19),
	{ }
};

static struct clk_rcg2 pcie_aux_clk_src = {
	.cmd_rcgr = 0x6c000,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_sleep_clk_map,
	.freq_tbl = ftbl_pcie_aux_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pcie_aux_clk_src",
		.parent_names = gcc_xo_sleep_clk,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_ufs_axi_clk_src[] = {
	F(100000000, P_GPLL0, 6, 0, 0),
	F(200000000, P_GPLL0, 3, 0, 0),
	F(240000000, P_GPLL0, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 ufs_axi_clk_src = {
	.cmd_rcgr = 0x75024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_ufs_axi_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "ufs_axi_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_ufs_ice_core_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(150000000, P_GPLL0, 4, 0, 0),
	F(300000000, P_GPLL0, 2, 0, 0),
	{ }
};

static struct clk_rcg2 ufs_ice_core_clk_src = {
	.cmd_rcgr = 0x76014,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_ufs_ice_core_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "ufs_ice_core_clk_src",
		.parent_names = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_qspi_ser_clk_src[] = {
	F(75000000, P_GPLL0, 8, 0, 0),
	F(150000000, P_GPLL0, 4, 0, 0),
	F(256000000, P_GPLL4, 1.5, 0, 0),
	F(300000000, P_GPLL0, 2, 0, 0),
	{ }
};

static struct clk_rcg2 qspi_ser_clk_src = {
	.cmd_rcgr = 0x8b00c,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll1_early_div_gpll1_gpll4_gpll0_early_div_map,
	.freq_tbl = ftbl_qspi_ser_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "qspi_ser_clk_src",
		.parent_names = gcc_xo_gpll0_gpll1_early_div_gpll1_gpll4_gpll0_early_div,
		.num_parents = 6,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_sys_noc_usb3_axi_clk = {
	.halt_reg = 0x0f03c,
	.clkr = {
		.enable_reg = 0x0f03c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sys_noc_usb3_axi_clk",
			.parent_names = (const char *[]){ "usb30_master_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sys_noc_ufs_axi_clk = {
	.halt_reg = 0x75038,
	.clkr = {
		.enable_reg = 0x75038,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sys_noc_ufs_axi_clk",
			.parent_names = (const char *[]){ "ufs_axi_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_periph_noc_usb20_ahb_clk = {
	.halt_reg = 0x6010,
	.clkr = {
		.enable_reg = 0x6010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_periph_noc_usb20_ahb_clk",
			.parent_names = (const char *[]){ "usb20_master_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mmss_noc_cfg_ahb_clk = {
	.halt_reg = 0x9008,
	.clkr = {
		.enable_reg = 0x9008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mmss_noc_cfg_ahb_clk",
			.parent_names = (const char *[]){ "config_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mmss_bimc_gfx_clk = {
	.halt_reg = 0x9010,
	.clkr = {
		.enable_reg = 0x9010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mmss_bimc_gfx_clk",
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_master_clk = {
	.halt_reg = 0x0f008,
	.clkr = {
		.enable_reg = 0x0f008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb30_master_clk",
			.parent_names = (const char *[]){ "usb30_master_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_sleep_clk = {
	.halt_reg = 0x0f00c,
	.clkr = {
		.enable_reg = 0x0f00c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb30_sleep_clk",
			.parent_names = (const char *[]){ "gcc_sleep_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_mock_utmi_clk = {
	.halt_reg = 0x0f010,
	.clkr = {
		.enable_reg = 0x0f010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb30_mock_utmi_clk",
			.parent_names = (const char *[]){ "usb30_mock_utmi_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_phy_aux_clk = {
	.halt_reg = 0x50000,
	.clkr = {
		.enable_reg = 0x50000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_phy_aux_clk",
			.parent_names = (const char *[]){ "usb3_phy_aux_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_phy_pipe_clk = {
	.halt_reg = 0x50004,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x50004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_phy_pipe_clk",
			.parent_names = (const char *[]){ "usb3_phy_pipe_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb20_master_clk = {
	.halt_reg = 0x12004,
	.clkr = {
		.enable_reg = 0x12004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb20_master_clk",
			.parent_names = (const char *[]){ "usb20_master_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb20_sleep_clk = {
	.halt_reg = 0x12008,
	.clkr = {
		.enable_reg = 0x12008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb20_sleep_clk",
			.parent_names = (const char *[]){ "gcc_sleep_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb20_mock_utmi_clk = {
	.halt_reg = 0x1200c,
	.clkr = {
		.enable_reg = 0x1200c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb20_mock_utmi_clk",
			.parent_names = (const char *[]){ "usb20_mock_utmi_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb_phy_cfg_ahb2phy_clk = {
	.halt_reg = 0x6a004,
	.clkr = {
		.enable_reg = 0x6a004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb_phy_cfg_ahb2phy_clk",
			.parent_names = (const char *[]){ "periph_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_apps_clk = {
	.halt_reg = 0x13004,
	.clkr = {
		.enable_reg = 0x13004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc1_apps_clk",
			.parent_names = (const char *[]){ "sdcc1_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_ahb_clk = {
	.halt_reg = 0x13008,
	.clkr = {
		.enable_reg = 0x13008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc1_ahb_clk",
			.parent_names = (const char *[]){ "periph_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_ice_core_clk = {
	.halt_reg = 0x13038,
	.clkr = {
		.enable_reg = 0x13038,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc1_ice_core_clk",
			.parent_names = (const char *[]){ "sdcc1_ice_core_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc2_apps_clk = {
	.halt_reg = 0x14004,
	.clkr = {
		.enable_reg = 0x14004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc2_apps_clk",
			.parent_names = (const char *[]){ "sdcc2_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc2_ahb_clk = {
	.halt_reg = 0x14008,
	.clkr = {
		.enable_reg = 0x14008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc2_ahb_clk",
			.parent_names = (const char *[]){ "periph_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc3_apps_clk = {
	.halt_reg = 0x15004,
	.clkr = {
		.enable_reg = 0x15004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc3_apps_clk",
			.parent_names = (const char *[]){ "sdcc3_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc3_ahb_clk = {
	.halt_reg = 0x15008,
	.clkr = {
		.enable_reg = 0x15008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc3_ahb_clk",
			.parent_names = (const char *[]){ "periph_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc4_apps_clk = {
	.halt_reg = 0x16004,
	.clkr = {
		.enable_reg = 0x16004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc4_apps_clk",
			.parent_names = (const char *[]){ "sdcc4_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc4_ahb_clk = {
	.halt_reg = 0x16008,
	.clkr = {
		.enable_reg = 0x16008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc4_ahb_clk",
			.parent_names = (const char *[]){ "periph_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_ahb_clk = {
	.halt_reg = 0x17004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52004,
		.enable_mask = BIT(17),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_ahb_clk",
			.parent_names = (const char *[]){ "periph_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_sleep_clk = {
	.halt_reg = 0x17008,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52004,
		.enable_mask = BIT(16),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_sleep_clk",
			.parent_names = (const char *[]){ "gcc_sleep_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup1_spi_apps_clk = {
	.halt_reg = 0x19004,
	.clkr = {
		.enable_reg = 0x19004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup1_spi_apps_clk",
			.parent_names = (const char *[]){ "blsp1_qup1_spi_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup1_i2c_apps_clk = {
	.halt_reg = 0x19008,
	.clkr = {
		.enable_reg = 0x19008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup1_i2c_apps_clk",
			.parent_names = (const char *[]){ "blsp1_qup1_i2c_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart1_apps_clk = {
	.halt_reg = 0x1a004,
	.clkr = {
		.enable_reg = 0x1a004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart1_apps_clk",
			.parent_names = (const char *[]){ "blsp1_uart1_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup2_spi_apps_clk = {
	.halt_reg = 0x1b004,
	.clkr = {
		.enable_reg = 0x1b004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup2_spi_apps_clk",
			.parent_names = (const char *[]){ "blsp1_qup2_spi_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup2_i2c_apps_clk = {
	.halt_reg = 0x1b008,
	.clkr = {
		.enable_reg = 0x1b008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup2_i2c_apps_clk",
			.parent_names = (const char *[]){ "blsp1_qup2_i2c_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart2_apps_clk = {
	.halt_reg = 0x1c004,
	.clkr = {
		.enable_reg = 0x1c004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart2_apps_clk",
			.parent_names = (const char *[]){ "blsp1_uart2_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup3_spi_apps_clk = {
	.halt_reg = 0x1d004,
	.clkr = {
		.enable_reg = 0x1d004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup3_spi_apps_clk",
			.parent_names = (const char *[]){ "blsp1_qup3_spi_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup3_i2c_apps_clk = {
	.halt_reg = 0x1d008,
	.clkr = {
		.enable_reg = 0x1d008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup3_i2c_apps_clk",
			.parent_names = (const char *[]){ "blsp1_qup3_i2c_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart3_apps_clk = {
	.halt_reg = 0x1e004,
	.clkr = {
		.enable_reg = 0x1e004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart3_apps_clk",
			.parent_names = (const char *[]){ "blsp1_uart3_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup4_spi_apps_clk = {
	.halt_reg = 0x1f004,
	.clkr = {
		.enable_reg = 0x1f004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup4_spi_apps_clk",
			.parent_names = (const char *[]){ "blsp1_qup4_spi_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup4_i2c_apps_clk = {
	.halt_reg = 0x1f008,
	.clkr = {
		.enable_reg = 0x1f008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup4_i2c_apps_clk",
			.parent_names = (const char *[]){ "blsp1_qup4_i2c_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart4_apps_clk = {
	.halt_reg = 0x20004,
	.clkr = {
		.enable_reg = 0x20004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart4_apps_clk",
			.parent_names = (const char *[]){ "blsp1_uart4_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup5_spi_apps_clk = {
	.halt_reg = 0x21004,
	.clkr = {
		.enable_reg = 0x21004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup5_spi_apps_clk",
			.parent_names = (const char *[]){ "blsp1_qup5_spi_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup5_i2c_apps_clk = {
	.halt_reg = 0x21008,
	.clkr = {
		.enable_reg = 0x21008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup5_i2c_apps_clk",
			.parent_names = (const char *[]){ "blsp1_qup5_i2c_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart5_apps_clk = {
	.halt_reg = 0x22004,
	.clkr = {
		.enable_reg = 0x22004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart5_apps_clk",
			.parent_names = (const char *[]){ "blsp1_uart5_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup6_spi_apps_clk = {
	.halt_reg = 0x23004,
	.clkr = {
		.enable_reg = 0x23004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup6_spi_apps_clk",
			.parent_names = (const char *[]){ "blsp1_qup6_spi_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup6_i2c_apps_clk = {
	.halt_reg = 0x23008,
	.clkr = {
		.enable_reg = 0x23008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup6_i2c_apps_clk",
			.parent_names = (const char *[]){ "blsp1_qup6_i2c_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart6_apps_clk = {
	.halt_reg = 0x24004,
	.clkr = {
		.enable_reg = 0x24004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart6_apps_clk",
			.parent_names = (const char *[]){ "blsp1_uart6_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_ahb_clk = {
	.halt_reg = 0x25004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52004,
		.enable_mask = BIT(15),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp2_ahb_clk",
			.parent_names = (const char *[]){ "periph_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_sleep_clk = {
	.halt_reg = 0x25008,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52004,
		.enable_mask = BIT(14),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp2_sleep_clk",
			.parent_names = (const char *[]){ "gcc_sleep_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_qup1_spi_apps_clk = {
	.halt_reg = 0x26004,
	.clkr = {
		.enable_reg = 0x26004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp2_qup1_spi_apps_clk",
			.parent_names = (const char *[]){ "blsp2_qup1_spi_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_qup1_i2c_apps_clk = {
	.halt_reg = 0x26008,
	.clkr = {
		.enable_reg = 0x26008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp2_qup1_i2c_apps_clk",
			.parent_names = (const char *[]){ "blsp2_qup1_i2c_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_uart1_apps_clk = {
	.halt_reg = 0x27004,
	.clkr = {
		.enable_reg = 0x27004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp2_uart1_apps_clk",
			.parent_names = (const char *[]){ "blsp2_uart1_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_qup2_spi_apps_clk = {
	.halt_reg = 0x28004,
	.clkr = {
		.enable_reg = 0x28004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp2_qup2_spi_apps_clk",
			.parent_names = (const char *[]){ "blsp2_qup2_spi_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_qup2_i2c_apps_clk = {
	.halt_reg = 0x28008,
	.clkr = {
		.enable_reg = 0x28008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp2_qup2_i2c_apps_clk",
			.parent_names = (const char *[]){ "blsp2_qup2_i2c_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_uart2_apps_clk = {
	.halt_reg = 0x29004,
	.clkr = {
		.enable_reg = 0x29004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp2_uart2_apps_clk",
			.parent_names = (const char *[]){ "blsp2_uart2_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_qup3_spi_apps_clk = {
	.halt_reg = 0x2a004,
	.clkr = {
		.enable_reg = 0x2a004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp2_qup3_spi_apps_clk",
			.parent_names = (const char *[]){ "blsp2_qup3_spi_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_qup3_i2c_apps_clk = {
	.halt_reg = 0x2a008,
	.clkr = {
		.enable_reg = 0x2a008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp2_qup3_i2c_apps_clk",
			.parent_names = (const char *[]){ "blsp2_qup3_i2c_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_uart3_apps_clk = {
	.halt_reg = 0x2b004,
	.clkr = {
		.enable_reg = 0x2b004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp2_uart3_apps_clk",
			.parent_names = (const char *[]){ "blsp2_uart3_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_qup4_spi_apps_clk = {
	.halt_reg = 0x2c004,
	.clkr = {
		.enable_reg = 0x2c004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp2_qup4_spi_apps_clk",
			.parent_names = (const char *[]){ "blsp2_qup4_spi_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_qup4_i2c_apps_clk = {
	.halt_reg = 0x2c008,
	.clkr = {
		.enable_reg = 0x2c008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp2_qup4_i2c_apps_clk",
			.parent_names = (const char *[]){ "blsp2_qup4_i2c_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_uart4_apps_clk = {
	.halt_reg = 0x2d004,
	.clkr = {
		.enable_reg = 0x2d004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp2_uart4_apps_clk",
			.parent_names = (const char *[]){ "blsp2_uart4_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_qup5_spi_apps_clk = {
	.halt_reg = 0x2e004,
	.clkr = {
		.enable_reg = 0x2e004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp2_qup5_spi_apps_clk",
			.parent_names = (const char *[]){ "blsp2_qup5_spi_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_qup5_i2c_apps_clk = {
	.halt_reg = 0x2e008,
	.clkr = {
		.enable_reg = 0x2e008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp2_qup5_i2c_apps_clk",
			.parent_names = (const char *[]){ "blsp2_qup5_i2c_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_uart5_apps_clk = {
	.halt_reg = 0x2f004,
	.clkr = {
		.enable_reg = 0x2f004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp2_uart5_apps_clk",
			.parent_names = (const char *[]){ "blsp2_uart5_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_qup6_spi_apps_clk = {
	.halt_reg = 0x30004,
	.clkr = {
		.enable_reg = 0x30004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp2_qup6_spi_apps_clk",
			.parent_names = (const char *[]){ "blsp2_qup6_spi_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_qup6_i2c_apps_clk = {
	.halt_reg = 0x30008,
	.clkr = {
		.enable_reg = 0x30008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp2_qup6_i2c_apps_clk",
			.parent_names = (const char *[]){ "blsp2_qup6_i2c_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_uart6_apps_clk = {
	.halt_reg = 0x31004,
	.clkr = {
		.enable_reg = 0x31004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp2_uart6_apps_clk",
			.parent_names = (const char *[]){ "blsp2_uart6_apps_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm_ahb_clk = {
	.halt_reg = 0x33004,
	.clkr = {
		.enable_reg = 0x33004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pdm_ahb_clk",
			.parent_names = (const char *[]){ "periph_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm2_clk = {
	.halt_reg = 0x3300c,
	.clkr = {
		.enable_reg = 0x3300c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pdm2_clk",
			.parent_names = (const char *[]){ "pdm2_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_prng_ahb_clk = {
	.halt_reg = 0x34004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52004,
		.enable_mask = BIT(13),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_prng_ahb_clk",
			.parent_names = (const char *[]){ "config_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_tsif_ahb_clk = {
	.halt_reg = 0x36004,
	.clkr = {
		.enable_reg = 0x36004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_tsif_ahb_clk",
			.parent_names = (const char *[]){ "periph_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_tsif_ref_clk = {
	.halt_reg = 0x36008,
	.clkr = {
		.enable_reg = 0x36008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_tsif_ref_clk",
			.parent_names = (const char *[]){ "tsif_ref_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_tsif_inactivity_timers_clk = {
	.halt_reg = 0x3600c,
	.clkr = {
		.enable_reg = 0x3600c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_tsif_inactivity_timers_clk",
			.parent_names = (const char *[]){ "gcc_sleep_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_boot_rom_ahb_clk = {
	.halt_reg = 0x38004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52004,
		.enable_mask = BIT(10),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_boot_rom_ahb_clk",
			.parent_names = (const char *[]){ "config_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_bimc_gfx_clk = {
	.halt_reg = 0x46018,
	.clkr = {
		.enable_reg = 0x46018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_bimc_gfx_clk",
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_hmss_rbcpr_clk = {
	.halt_reg = 0x4800c,
	.clkr = {
		.enable_reg = 0x4800c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_hmss_rbcpr_clk",
			.parent_names = (const char *[]){ "hmss_rbcpr_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp1_clk = {
	.halt_reg = 0x64000,
	.clkr = {
		.enable_reg = 0x64000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gp1_clk",
			.parent_names = (const char *[]){ "gp1_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp2_clk = {
	.halt_reg = 0x65000,
	.clkr = {
		.enable_reg = 0x65000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gp2_clk",
			.parent_names = (const char *[]){ "gp2_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp3_clk = {
	.halt_reg = 0x66000,
	.clkr = {
		.enable_reg = 0x66000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gp3_clk",
			.parent_names = (const char *[]){ "gp3_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_slv_axi_clk = {
	.halt_reg = 0x6b008,
	.clkr = {
		.enable_reg = 0x6b008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_0_slv_axi_clk",
			.parent_names = (const char *[]){ "system_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_mstr_axi_clk = {
	.halt_reg = 0x6b00c,
	.clkr = {
		.enable_reg = 0x6b00c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_0_mstr_axi_clk",
			.parent_names = (const char *[]){ "system_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_cfg_ahb_clk = {
	.halt_reg = 0x6b010,
	.clkr = {
		.enable_reg = 0x6b010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_0_cfg_ahb_clk",
			.parent_names = (const char *[]){ "config_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_aux_clk = {
	.halt_reg = 0x6b014,
	.clkr = {
		.enable_reg = 0x6b014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_0_aux_clk",
			.parent_names = (const char *[]){ "pcie_aux_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_pipe_clk = {
	.halt_reg = 0x6b018,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x6b018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_0_pipe_clk",
			.parent_names = (const char *[]){ "pcie_0_pipe_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_slv_axi_clk = {
	.halt_reg = 0x6d008,
	.clkr = {
		.enable_reg = 0x6d008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_1_slv_axi_clk",
			.parent_names = (const char *[]){ "system_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_mstr_axi_clk = {
	.halt_reg = 0x6d00c,
	.clkr = {
		.enable_reg = 0x6d00c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_1_mstr_axi_clk",
			.parent_names = (const char *[]){ "system_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_cfg_ahb_clk = {
	.halt_reg = 0x6d010,
	.clkr = {
		.enable_reg = 0x6d010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_1_cfg_ahb_clk",
			.parent_names = (const char *[]){ "config_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_aux_clk = {
	.halt_reg = 0x6d014,
	.clkr = {
		.enable_reg = 0x6d014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_1_aux_clk",
			.parent_names = (const char *[]){ "pcie_aux_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_pipe_clk = {
	.halt_reg = 0x6d018,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x6d018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_1_pipe_clk",
			.parent_names = (const char *[]){ "pcie_1_pipe_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_slv_axi_clk = {
	.halt_reg = 0x6e008,
	.clkr = {
		.enable_reg = 0x6e008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_2_slv_axi_clk",
			.parent_names = (const char *[]){ "system_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_mstr_axi_clk = {
	.halt_reg = 0x6e00c,
	.clkr = {
		.enable_reg = 0x6e00c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_2_mstr_axi_clk",
			.parent_names = (const char *[]){ "system_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_cfg_ahb_clk = {
	.halt_reg = 0x6e010,
	.clkr = {
		.enable_reg = 0x6e010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_2_cfg_ahb_clk",
			.parent_names = (const char *[]){ "config_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_aux_clk = {
	.halt_reg = 0x6e014,
	.clkr = {
		.enable_reg = 0x6e014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_2_aux_clk",
			.parent_names = (const char *[]){ "pcie_aux_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_pipe_clk = {
	.halt_reg = 0x6e018,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x6e018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_2_pipe_clk",
			.parent_names = (const char *[]){ "pcie_2_pipe_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_phy_cfg_ahb_clk = {
	.halt_reg = 0x6f004,
	.clkr = {
		.enable_reg = 0x6f004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_phy_cfg_ahb_clk",
			.parent_names = (const char *[]){ "config_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_phy_aux_clk = {
	.halt_reg = 0x6f008,
	.clkr = {
		.enable_reg = 0x6f008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_phy_aux_clk",
			.parent_names = (const char *[]){ "pcie_aux_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_axi_clk = {
	.halt_reg = 0x75008,
	.clkr = {
		.enable_reg = 0x75008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_axi_clk",
			.parent_names = (const char *[]){ "ufs_axi_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_ahb_clk = {
	.halt_reg = 0x7500c,
	.clkr = {
		.enable_reg = 0x7500c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_ahb_clk",
			.parent_names = (const char *[]){ "config_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_fixed_factor ufs_tx_cfg_clk_src = {
	.mult = 1,
	.div = 16,
	.hw.init = &(struct clk_init_data){
		.name = "ufs_tx_cfg_clk_src",
		.parent_names = (const char *[]){ "ufs_axi_clk_src" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct clk_branch gcc_ufs_tx_cfg_clk = {
	.halt_reg = 0x75010,
	.clkr = {
		.enable_reg = 0x75010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_tx_cfg_clk",
			.parent_names = (const char *[]){ "ufs_tx_cfg_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_fixed_factor ufs_rx_cfg_clk_src = {
	.mult = 1,
	.div = 16,
	.hw.init = &(struct clk_init_data){
		.name = "ufs_rx_cfg_clk_src",
		.parent_names = (const char *[]){ "ufs_axi_clk_src" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct clk_branch gcc_hlos1_vote_lpass_core_smmu_clk = {
	.halt_reg = 0x7d010,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "hlos1_vote_lpass_core_smmu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_hlos1_vote_lpass_adsp_smmu_clk = {
	.halt_reg = 0x7d014,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "hlos1_vote_lpass_adsp_smmu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_rx_cfg_clk = {
	.halt_reg = 0x75014,
	.clkr = {
		.enable_reg = 0x75014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_rx_cfg_clk",
			.parent_names = (const char *[]){ "ufs_rx_cfg_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_tx_symbol_0_clk = {
	.halt_reg = 0x75018,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x75018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_tx_symbol_0_clk",
			.parent_names = (const char *[]){ "ufs_tx_symbol_0_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_rx_symbol_0_clk = {
	.halt_reg = 0x7501c,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x7501c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_rx_symbol_0_clk",
			.parent_names = (const char *[]){ "ufs_rx_symbol_0_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_rx_symbol_1_clk = {
	.halt_reg = 0x75020,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x75020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_rx_symbol_1_clk",
			.parent_names = (const char *[]){ "ufs_rx_symbol_1_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_fixed_factor ufs_ice_core_postdiv_clk_src = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "ufs_ice_core_postdiv_clk_src",
		.parent_names = (const char *[]){ "ufs_ice_core_clk_src" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct clk_branch gcc_ufs_unipro_core_clk = {
	.halt_reg = 0x7600c,
	.clkr = {
		.enable_reg = 0x7600c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_unipro_core_clk",
			.parent_names = (const char *[]){ "ufs_ice_core_postdiv_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_ice_core_clk = {
	.halt_reg = 0x76010,
	.clkr = {
		.enable_reg = 0x76010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_ice_core_clk",
			.parent_names = (const char *[]){ "ufs_ice_core_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_sys_clk_core_clk = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x76030,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_sys_clk_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_tx_symbol_clk_core_clk = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x76034,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_tx_symbol_clk_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre0_snoc_axi_clk = {
	.halt_reg = 0x81008,
	.clkr = {
		.enable_reg = 0x81008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_aggre0_snoc_axi_clk",
			.parent_names = (const char *[]){ "system_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre0_cnoc_ahb_clk = {
	.halt_reg = 0x8100c,
	.clkr = {
		.enable_reg = 0x8100c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_aggre0_cnoc_ahb_clk",
			.parent_names = (const char *[]){ "config_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_smmu_aggre0_axi_clk = {
	.halt_reg = 0x81014,
	.clkr = {
		.enable_reg = 0x81014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_smmu_aggre0_axi_clk",
			.parent_names = (const char *[]){ "system_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_smmu_aggre0_ahb_clk = {
	.halt_reg = 0x81018,
	.clkr = {
		.enable_reg = 0x81018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_smmu_aggre0_ahb_clk",
			.parent_names = (const char *[]){ "config_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre2_ufs_axi_clk = {
	.halt_reg = 0x83014,
	.clkr = {
		.enable_reg = 0x83014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_aggre2_ufs_axi_clk",
			.parent_names = (const char *[]){ "ufs_axi_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre2_usb3_axi_clk = {
	.halt_reg = 0x83018,
	.clkr = {
		.enable_reg = 0x83018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_aggre2_usb3_axi_clk",
			.parent_names = (const char *[]){ "usb30_master_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_dcc_ahb_clk = {
	.halt_reg = 0x84004,
	.clkr = {
		.enable_reg = 0x84004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_dcc_ahb_clk",
			.parent_names = (const char *[]){ "config_noc_clk_src" },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre0_noc_mpu_cfg_ahb_clk = {
	.halt_reg = 0x85000,
	.clkr = {
		.enable_reg = 0x85000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_aggre0_noc_mpu_cfg_ahb_clk",
			.parent_names = (const char *[]){ "config_noc_clk_src" },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qspi_ahb_clk = {
	.halt_reg = 0x8b004,
	.clkr = {
		.enable_reg = 0x8b004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qspi_ahb_clk",
			.parent_names = (const char *[]){ "periph_noc_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qspi_ser_clk = {
	.halt_reg = 0x8b008,
	.clkr = {
		.enable_reg = 0x8b008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qspi_ser_clk",
			.parent_names = (const char *[]){ "qspi_ser_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_clkref_clk = {
	.halt_reg = 0x8800C,
	.clkr = {
		.enable_reg = 0x8800C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_clkref_clk",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "cxo2",
				.name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_hdmi_clkref_clk = {
	.halt_reg = 0x88000,
	.clkr = {
		.enable_reg = 0x88000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_hdmi_clkref_clk",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "cxo2",
				.name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_edp_clkref_clk = {
	.halt_reg = 0x88004,
	.clkr = {
		.enable_reg = 0x88004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_edp_clkref_clk",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "cxo2",
				.name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_clkref_clk = {
	.halt_reg = 0x88008,
	.clkr = {
		.enable_reg = 0x88008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_clkref_clk",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "cxo2",
				.name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_clkref_clk = {
	.halt_reg = 0x88010,
	.clkr = {
		.enable_reg = 0x88010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_clkref_clk",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "cxo2",
				.name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_rx2_usb2_clkref_clk = {
	.halt_reg = 0x88014,
	.clkr = {
		.enable_reg = 0x88014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_rx2_usb2_clkref_clk",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "cxo2",
				.name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_rx1_usb2_clkref_clk = {
	.halt_reg = 0x88018,
	.clkr = {
		.enable_reg = 0x88018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_rx1_usb2_clkref_clk",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "cxo2",
				.name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mss_cfg_ahb_clk = {
	.halt_reg = 0x8a000,
	.clkr = {
		.enable_reg = 0x8a000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mss_cfg_ahb_clk",
			.parent_names = (const char *[]){ "config_noc_clk_src" },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mss_mnoc_bimc_axi_clk = {
	.halt_reg = 0x8a004,
	.clkr = {
		.enable_reg = 0x8a004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mss_mnoc_bimc_axi_clk",
			.parent_names = (const char *[]){ "system_noc_clk_src" },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mss_snoc_axi_clk = {
	.halt_reg = 0x8a024,
	.clkr = {
		.enable_reg = 0x8a024,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mss_snoc_axi_clk",
			.parent_names = (const char *[]){ "system_noc_clk_src" },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mss_q6_bimc_axi_clk = {
	.halt_reg = 0x8a028,
	.clkr = {
		.enable_reg = 0x8a028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mss_q6_bimc_axi_clk",
			.parent_names = (const char *[]){ "system_noc_clk_src" },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_hw *gcc_msm8996_hws[] = {
	&xo.hw,
	&gpll0_early_div.hw,
	&ufs_tx_cfg_clk_src.hw,
	&ufs_rx_cfg_clk_src.hw,
	&ufs_ice_core_postdiv_clk_src.hw,
};

static struct gdsc aggre0_noc_gdsc = {
	.gdscr = 0x81004,
	.gds_hw_ctrl = 0x81028,
	.pd = {
		.name = "aggre0_noc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE | ALWAYS_ON,
};

static struct gdsc hlos1_vote_aggre0_noc_gdsc = {
	.gdscr = 0x7d024,
	.pd = {
		.name = "hlos1_vote_aggre0_noc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc hlos1_vote_lpass_adsp_gdsc = {
	.gdscr = 0x7d034,
	.pd = {
		.name = "hlos1_vote_lpass_adsp",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc hlos1_vote_lpass_core_gdsc = {
	.gdscr = 0x7d038,
	.pd = {
		.name = "hlos1_vote_lpass_core",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc usb30_gdsc = {
	.gdscr = 0xf004,
	.pd = {
		.name = "usb30",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc pcie0_gdsc = {
	.gdscr = 0x6b004,
	.pd = {
		.name = "pcie0",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc pcie1_gdsc = {
	.gdscr = 0x6d004,
	.pd = {
		.name = "pcie1",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc pcie2_gdsc = {
	.gdscr = 0x6e004,
	.pd = {
		.name = "pcie2",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc ufs_gdsc = {
	.gdscr = 0x75004,
	.pd = {
		.name = "ufs",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct clk_regmap *gcc_msm8996_clocks[] = {
	[GPLL0_EARLY] = &gpll0_early.clkr,
	[GPLL0] = &gpll0.clkr,
	[GPLL4_EARLY] = &gpll4_early.clkr,
	[GPLL4] = &gpll4.clkr,
	[SYSTEM_NOC_CLK_SRC] = &system_noc_clk_src.clkr,
	[CONFIG_NOC_CLK_SRC] = &config_noc_clk_src.clkr,
	[PERIPH_NOC_CLK_SRC] = &periph_noc_clk_src.clkr,
	[USB30_MASTER_CLK_SRC] = &usb30_master_clk_src.clkr,
	[USB30_MOCK_UTMI_CLK_SRC] = &usb30_mock_utmi_clk_src.clkr,
	[USB3_PHY_AUX_CLK_SRC] = &usb3_phy_aux_clk_src.clkr,
	[USB20_MASTER_CLK_SRC] = &usb20_master_clk_src.clkr,
	[USB20_MOCK_UTMI_CLK_SRC] = &usb20_mock_utmi_clk_src.clkr,
	[SDCC1_APPS_CLK_SRC] = &sdcc1_apps_clk_src.clkr,
	[SDCC1_ICE_CORE_CLK_SRC] = &sdcc1_ice_core_clk_src.clkr,
	[SDCC2_APPS_CLK_SRC] = &sdcc2_apps_clk_src.clkr,
	[SDCC3_APPS_CLK_SRC] = &sdcc3_apps_clk_src.clkr,
	[SDCC4_APPS_CLK_SRC] = &sdcc4_apps_clk_src.clkr,
	[BLSP1_QUP1_SPI_APPS_CLK_SRC] = &blsp1_qup1_spi_apps_clk_src.clkr,
	[BLSP1_QUP1_I2C_APPS_CLK_SRC] = &blsp1_qup1_i2c_apps_clk_src.clkr,
	[BLSP1_UART1_APPS_CLK_SRC] = &blsp1_uart1_apps_clk_src.clkr,
	[BLSP1_QUP2_SPI_APPS_CLK_SRC] = &blsp1_qup2_spi_apps_clk_src.clkr,
	[BLSP1_QUP2_I2C_APPS_CLK_SRC] = &blsp1_qup2_i2c_apps_clk_src.clkr,
	[BLSP1_UART2_APPS_CLK_SRC] = &blsp1_uart2_apps_clk_src.clkr,
	[BLSP1_QUP3_SPI_APPS_CLK_SRC] = &blsp1_qup3_spi_apps_clk_src.clkr,
	[BLSP1_QUP3_I2C_APPS_CLK_SRC] = &blsp1_qup3_i2c_apps_clk_src.clkr,
	[BLSP1_UART3_APPS_CLK_SRC] = &blsp1_uart3_apps_clk_src.clkr,
	[BLSP1_QUP4_SPI_APPS_CLK_SRC] = &blsp1_qup4_spi_apps_clk_src.clkr,
	[BLSP1_QUP4_I2C_APPS_CLK_SRC] = &blsp1_qup4_i2c_apps_clk_src.clkr,
	[BLSP1_UART4_APPS_CLK_SRC] = &blsp1_uart4_apps_clk_src.clkr,
	[BLSP1_QUP5_SPI_APPS_CLK_SRC] = &blsp1_qup5_spi_apps_clk_src.clkr,
	[BLSP1_QUP5_I2C_APPS_CLK_SRC] = &blsp1_qup5_i2c_apps_clk_src.clkr,
	[BLSP1_UART5_APPS_CLK_SRC] = &blsp1_uart5_apps_clk_src.clkr,
	[BLSP1_QUP6_SPI_APPS_CLK_SRC] = &blsp1_qup6_spi_apps_clk_src.clkr,
	[BLSP1_QUP6_I2C_APPS_CLK_SRC] = &blsp1_qup6_i2c_apps_clk_src.clkr,
	[BLSP1_UART6_APPS_CLK_SRC] = &blsp1_uart6_apps_clk_src.clkr,
	[BLSP2_QUP1_SPI_APPS_CLK_SRC] = &blsp2_qup1_spi_apps_clk_src.clkr,
	[BLSP2_QUP1_I2C_APPS_CLK_SRC] = &blsp2_qup1_i2c_apps_clk_src.clkr,
	[BLSP2_UART1_APPS_CLK_SRC] = &blsp2_uart1_apps_clk_src.clkr,
	[BLSP2_QUP2_SPI_APPS_CLK_SRC] = &blsp2_qup2_spi_apps_clk_src.clkr,
	[BLSP2_QUP2_I2C_APPS_CLK_SRC] = &blsp2_qup2_i2c_apps_clk_src.clkr,
	[BLSP2_UART2_APPS_CLK_SRC] = &blsp2_uart2_apps_clk_src.clkr,
	[BLSP2_QUP3_SPI_APPS_CLK_SRC] = &blsp2_qup3_spi_apps_clk_src.clkr,
	[BLSP2_QUP3_I2C_APPS_CLK_SRC] = &blsp2_qup3_i2c_apps_clk_src.clkr,
	[BLSP2_UART3_APPS_CLK_SRC] = &blsp2_uart3_apps_clk_src.clkr,
	[BLSP2_QUP4_SPI_APPS_CLK_SRC] = &blsp2_qup4_spi_apps_clk_src.clkr,
	[BLSP2_QUP4_I2C_APPS_CLK_SRC] = &blsp2_qup4_i2c_apps_clk_src.clkr,
	[BLSP2_UART4_APPS_CLK_SRC] = &blsp2_uart4_apps_clk_src.clkr,
	[BLSP2_QUP5_SPI_APPS_CLK_SRC] = &blsp2_qup5_spi_apps_clk_src.clkr,
	[BLSP2_QUP5_I2C_APPS_CLK_SRC] = &blsp2_qup5_i2c_apps_clk_src.clkr,
	[BLSP2_UART5_APPS_CLK_SRC] = &blsp2_uart5_apps_clk_src.clkr,
	[BLSP2_QUP6_SPI_APPS_CLK_SRC] = &blsp2_qup6_spi_apps_clk_src.clkr,
	[BLSP2_QUP6_I2C_APPS_CLK_SRC] = &blsp2_qup6_i2c_apps_clk_src.clkr,
	[BLSP2_UART6_APPS_CLK_SRC] = &blsp2_uart6_apps_clk_src.clkr,
	[PDM2_CLK_SRC] = &pdm2_clk_src.clkr,
	[TSIF_REF_CLK_SRC] = &tsif_ref_clk_src.clkr,
	[GCC_SLEEP_CLK_SRC] = &gcc_sleep_clk_src.clkr,
	[HMSS_RBCPR_CLK_SRC] = &hmss_rbcpr_clk_src.clkr,
	[HMSS_GPLL0_CLK_SRC] = &hmss_gpll0_clk_src.clkr,
	[GP1_CLK_SRC] = &gp1_clk_src.clkr,
	[GP2_CLK_SRC] = &gp2_clk_src.clkr,
	[GP3_CLK_SRC] = &gp3_clk_src.clkr,
	[PCIE_AUX_CLK_SRC] = &pcie_aux_clk_src.clkr,
	[UFS_AXI_CLK_SRC] = &ufs_axi_clk_src.clkr,
	[UFS_ICE_CORE_CLK_SRC] = &ufs_ice_core_clk_src.clkr,
	[QSPI_SER_CLK_SRC] = &qspi_ser_clk_src.clkr,
	[GCC_SYS_NOC_USB3_AXI_CLK] = &gcc_sys_noc_usb3_axi_clk.clkr,
	[GCC_SYS_NOC_UFS_AXI_CLK] = &gcc_sys_noc_ufs_axi_clk.clkr,
	[GCC_PERIPH_NOC_USB20_AHB_CLK] = &gcc_periph_noc_usb20_ahb_clk.clkr,
	[GCC_MMSS_NOC_CFG_AHB_CLK] = &gcc_mmss_noc_cfg_ahb_clk.clkr,
	[GCC_MMSS_BIMC_GFX_CLK] = &gcc_mmss_bimc_gfx_clk.clkr,
	[GCC_USB30_MASTER_CLK] = &gcc_usb30_master_clk.clkr,
	[GCC_USB30_SLEEP_CLK] = &gcc_usb30_sleep_clk.clkr,
	[GCC_USB30_MOCK_UTMI_CLK] = &gcc_usb30_mock_utmi_clk.clkr,
	[GCC_USB3_PHY_AUX_CLK] = &gcc_usb3_phy_aux_clk.clkr,
	[GCC_USB3_PHY_PIPE_CLK] = &gcc_usb3_phy_pipe_clk.clkr,
	[GCC_USB20_MASTER_CLK] = &gcc_usb20_master_clk.clkr,
	[GCC_USB20_SLEEP_CLK] = &gcc_usb20_sleep_clk.clkr,
	[GCC_USB20_MOCK_UTMI_CLK] = &gcc_usb20_mock_utmi_clk.clkr,
	[GCC_USB_PHY_CFG_AHB2PHY_CLK] = &gcc_usb_phy_cfg_ahb2phy_clk.clkr,
	[GCC_SDCC1_APPS_CLK] = &gcc_sdcc1_apps_clk.clkr,
	[GCC_SDCC1_AHB_CLK] = &gcc_sdcc1_ahb_clk.clkr,
	[GCC_SDCC1_ICE_CORE_CLK] = &gcc_sdcc1_ice_core_clk.clkr,
	[GCC_SDCC2_APPS_CLK] = &gcc_sdcc2_apps_clk.clkr,
	[GCC_SDCC2_AHB_CLK] = &gcc_sdcc2_ahb_clk.clkr,
	[GCC_SDCC3_APPS_CLK] = &gcc_sdcc3_apps_clk.clkr,
	[GCC_SDCC3_AHB_CLK] = &gcc_sdcc3_ahb_clk.clkr,
	[GCC_SDCC4_APPS_CLK] = &gcc_sdcc4_apps_clk.clkr,
	[GCC_SDCC4_AHB_CLK] = &gcc_sdcc4_ahb_clk.clkr,
	[GCC_BLSP1_AHB_CLK] = &gcc_blsp1_ahb_clk.clkr,
	[GCC_BLSP1_SLEEP_CLK] = &gcc_blsp1_sleep_clk.clkr,
	[GCC_BLSP1_QUP1_SPI_APPS_CLK] = &gcc_blsp1_qup1_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP1_I2C_APPS_CLK] = &gcc_blsp1_qup1_i2c_apps_clk.clkr,
	[GCC_BLSP1_UART1_APPS_CLK] = &gcc_blsp1_uart1_apps_clk.clkr,
	[GCC_BLSP1_QUP2_SPI_APPS_CLK] = &gcc_blsp1_qup2_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP2_I2C_APPS_CLK] = &gcc_blsp1_qup2_i2c_apps_clk.clkr,
	[GCC_BLSP1_UART2_APPS_CLK] = &gcc_blsp1_uart2_apps_clk.clkr,
	[GCC_BLSP1_QUP3_SPI_APPS_CLK] = &gcc_blsp1_qup3_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP3_I2C_APPS_CLK] = &gcc_blsp1_qup3_i2c_apps_clk.clkr,
	[GCC_BLSP1_UART3_APPS_CLK] = &gcc_blsp1_uart3_apps_clk.clkr,
	[GCC_BLSP1_QUP4_SPI_APPS_CLK] = &gcc_blsp1_qup4_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP4_I2C_APPS_CLK] = &gcc_blsp1_qup4_i2c_apps_clk.clkr,
	[GCC_BLSP1_UART4_APPS_CLK] = &gcc_blsp1_uart4_apps_clk.clkr,
	[GCC_BLSP1_QUP5_SPI_APPS_CLK] = &gcc_blsp1_qup5_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP5_I2C_APPS_CLK] = &gcc_blsp1_qup5_i2c_apps_clk.clkr,
	[GCC_BLSP1_UART5_APPS_CLK] = &gcc_blsp1_uart5_apps_clk.clkr,
	[GCC_BLSP1_QUP6_SPI_APPS_CLK] = &gcc_blsp1_qup6_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP6_I2C_APPS_CLK] = &gcc_blsp1_qup6_i2c_apps_clk.clkr,
	[GCC_BLSP1_UART6_APPS_CLK] = &gcc_blsp1_uart6_apps_clk.clkr,
	[GCC_BLSP2_AHB_CLK] = &gcc_blsp2_ahb_clk.clkr,
	[GCC_BLSP2_SLEEP_CLK] = &gcc_blsp2_sleep_clk.clkr,
	[GCC_BLSP2_QUP1_SPI_APPS_CLK] = &gcc_blsp2_qup1_spi_apps_clk.clkr,
	[GCC_BLSP2_QUP1_I2C_APPS_CLK] = &gcc_blsp2_qup1_i2c_apps_clk.clkr,
	[GCC_BLSP2_UART1_APPS_CLK] = &gcc_blsp2_uart1_apps_clk.clkr,
	[GCC_BLSP2_QUP2_SPI_APPS_CLK] = &gcc_blsp2_qup2_spi_apps_clk.clkr,
	[GCC_BLSP2_QUP2_I2C_APPS_CLK] = &gcc_blsp2_qup2_i2c_apps_clk.clkr,
	[GCC_BLSP2_UART2_APPS_CLK] = &gcc_blsp2_uart2_apps_clk.clkr,
	[GCC_BLSP2_QUP3_SPI_APPS_CLK] = &gcc_blsp2_qup3_spi_apps_clk.clkr,
	[GCC_BLSP2_QUP3_I2C_APPS_CLK] = &gcc_blsp2_qup3_i2c_apps_clk.clkr,
	[GCC_BLSP2_UART3_APPS_CLK] = &gcc_blsp2_uart3_apps_clk.clkr,
	[GCC_BLSP2_QUP4_SPI_APPS_CLK] = &gcc_blsp2_qup4_spi_apps_clk.clkr,
	[GCC_BLSP2_QUP4_I2C_APPS_CLK] = &gcc_blsp2_qup4_i2c_apps_clk.clkr,
	[GCC_BLSP2_UART4_APPS_CLK] = &gcc_blsp2_uart4_apps_clk.clkr,
	[GCC_BLSP2_QUP5_SPI_APPS_CLK] = &gcc_blsp2_qup5_spi_apps_clk.clkr,
	[GCC_BLSP2_QUP5_I2C_APPS_CLK] = &gcc_blsp2_qup5_i2c_apps_clk.clkr,
	[GCC_BLSP2_UART5_APPS_CLK] = &gcc_blsp2_uart5_apps_clk.clkr,
	[GCC_BLSP2_QUP6_SPI_APPS_CLK] = &gcc_blsp2_qup6_spi_apps_clk.clkr,
	[GCC_BLSP2_QUP6_I2C_APPS_CLK] = &gcc_blsp2_qup6_i2c_apps_clk.clkr,
	[GCC_BLSP2_UART6_APPS_CLK] = &gcc_blsp2_uart6_apps_clk.clkr,
	[GCC_PDM_AHB_CLK] = &gcc_pdm_ahb_clk.clkr,
	[GCC_PDM2_CLK] = &gcc_pdm2_clk.clkr,
	[GCC_PRNG_AHB_CLK] = &gcc_prng_ahb_clk.clkr,
	[GCC_TSIF_AHB_CLK] = &gcc_tsif_ahb_clk.clkr,
	[GCC_TSIF_REF_CLK] = &gcc_tsif_ref_clk.clkr,
	[GCC_TSIF_INACTIVITY_TIMERS_CLK] = &gcc_tsif_inactivity_timers_clk.clkr,
	[GCC_BOOT_ROM_AHB_CLK] = &gcc_boot_rom_ahb_clk.clkr,
	[GCC_BIMC_GFX_CLK] = &gcc_bimc_gfx_clk.clkr,
	[GCC_HMSS_RBCPR_CLK] = &gcc_hmss_rbcpr_clk.clkr,
	[GCC_GP1_CLK] = &gcc_gp1_clk.clkr,
	[GCC_GP2_CLK] = &gcc_gp2_clk.clkr,
	[GCC_GP3_CLK] = &gcc_gp3_clk.clkr,
	[GCC_PCIE_0_SLV_AXI_CLK] = &gcc_pcie_0_slv_axi_clk.clkr,
	[GCC_PCIE_0_MSTR_AXI_CLK] = &gcc_pcie_0_mstr_axi_clk.clkr,
	[GCC_PCIE_0_CFG_AHB_CLK] = &gcc_pcie_0_cfg_ahb_clk.clkr,
	[GCC_PCIE_0_AUX_CLK] = &gcc_pcie_0_aux_clk.clkr,
	[GCC_PCIE_0_PIPE_CLK] = &gcc_pcie_0_pipe_clk.clkr,
	[GCC_PCIE_1_SLV_AXI_CLK] = &gcc_pcie_1_slv_axi_clk.clkr,
	[GCC_PCIE_1_MSTR_AXI_CLK] = &gcc_pcie_1_mstr_axi_clk.clkr,
	[GCC_PCIE_1_CFG_AHB_CLK] = &gcc_pcie_1_cfg_ahb_clk.clkr,
	[GCC_PCIE_1_AUX_CLK] = &gcc_pcie_1_aux_clk.clkr,
	[GCC_PCIE_1_PIPE_CLK] = &gcc_pcie_1_pipe_clk.clkr,
	[GCC_PCIE_2_SLV_AXI_CLK] = &gcc_pcie_2_slv_axi_clk.clkr,
	[GCC_PCIE_2_MSTR_AXI_CLK] = &gcc_pcie_2_mstr_axi_clk.clkr,
	[GCC_PCIE_2_CFG_AHB_CLK] = &gcc_pcie_2_cfg_ahb_clk.clkr,
	[GCC_PCIE_2_AUX_CLK] = &gcc_pcie_2_aux_clk.clkr,
	[GCC_PCIE_2_PIPE_CLK] = &gcc_pcie_2_pipe_clk.clkr,
	[GCC_PCIE_PHY_CFG_AHB_CLK] = &gcc_pcie_phy_cfg_ahb_clk.clkr,
	[GCC_PCIE_PHY_AUX_CLK] = &gcc_pcie_phy_aux_clk.clkr,
	[GCC_UFS_AXI_CLK] = &gcc_ufs_axi_clk.clkr,
	[GCC_UFS_AHB_CLK] = &gcc_ufs_ahb_clk.clkr,
	[GCC_UFS_TX_CFG_CLK] = &gcc_ufs_tx_cfg_clk.clkr,
	[GCC_UFS_RX_CFG_CLK] = &gcc_ufs_rx_cfg_clk.clkr,
	[GCC_HLOS1_VOTE_LPASS_CORE_SMMU_CLK] = &gcc_hlos1_vote_lpass_core_smmu_clk.clkr,
	[GCC_HLOS1_VOTE_LPASS_ADSP_SMMU_CLK] = &gcc_hlos1_vote_lpass_adsp_smmu_clk.clkr,
	[GCC_UFS_TX_SYMBOL_0_CLK] = &gcc_ufs_tx_symbol_0_clk.clkr,
	[GCC_UFS_RX_SYMBOL_0_CLK] = &gcc_ufs_rx_symbol_0_clk.clkr,
	[GCC_UFS_RX_SYMBOL_1_CLK] = &gcc_ufs_rx_symbol_1_clk.clkr,
	[GCC_UFS_UNIPRO_CORE_CLK] = &gcc_ufs_unipro_core_clk.clkr,
	[GCC_UFS_ICE_CORE_CLK] = &gcc_ufs_ice_core_clk.clkr,
	[GCC_UFS_SYS_CLK_CORE_CLK] = &gcc_ufs_sys_clk_core_clk.clkr,
	[GCC_UFS_TX_SYMBOL_CLK_CORE_CLK] = &gcc_ufs_tx_symbol_clk_core_clk.clkr,
	[GCC_AGGRE0_SNOC_AXI_CLK] = &gcc_aggre0_snoc_axi_clk.clkr,
	[GCC_AGGRE0_CNOC_AHB_CLK] = &gcc_aggre0_cnoc_ahb_clk.clkr,
	[GCC_SMMU_AGGRE0_AXI_CLK] = &gcc_smmu_aggre0_axi_clk.clkr,
	[GCC_SMMU_AGGRE0_AHB_CLK] = &gcc_smmu_aggre0_ahb_clk.clkr,
	[GCC_AGGRE2_UFS_AXI_CLK] = &gcc_aggre2_ufs_axi_clk.clkr,
	[GCC_AGGRE2_USB3_AXI_CLK] = &gcc_aggre2_usb3_axi_clk.clkr,
	[GCC_QSPI_AHB_CLK] = &gcc_qspi_ahb_clk.clkr,
	[GCC_QSPI_SER_CLK] = &gcc_qspi_ser_clk.clkr,
	[GCC_USB3_CLKREF_CLK] = &gcc_usb3_clkref_clk.clkr,
	[GCC_HDMI_CLKREF_CLK] = &gcc_hdmi_clkref_clk.clkr,
	[GCC_UFS_CLKREF_CLK] = &gcc_ufs_clkref_clk.clkr,
	[GCC_PCIE_CLKREF_CLK] = &gcc_pcie_clkref_clk.clkr,
	[GCC_RX2_USB2_CLKREF_CLK] = &gcc_rx2_usb2_clkref_clk.clkr,
	[GCC_RX1_USB2_CLKREF_CLK] = &gcc_rx1_usb2_clkref_clk.clkr,
	[GCC_EDP_CLKREF_CLK] = &gcc_edp_clkref_clk.clkr,
	[GCC_MSS_CFG_AHB_CLK] = &gcc_mss_cfg_ahb_clk.clkr,
	[GCC_MSS_Q6_BIMC_AXI_CLK] = &gcc_mss_q6_bimc_axi_clk.clkr,
	[GCC_MSS_SNOC_AXI_CLK] = &gcc_mss_snoc_axi_clk.clkr,
	[GCC_MSS_MNOC_BIMC_AXI_CLK] = &gcc_mss_mnoc_bimc_axi_clk.clkr,
	[GCC_DCC_AHB_CLK] = &gcc_dcc_ahb_clk.clkr,
	[GCC_AGGRE0_NOC_MPU_CFG_AHB_CLK] = &gcc_aggre0_noc_mpu_cfg_ahb_clk.clkr,
	[GCC_MMSS_GPLL0_DIV_CLK] = &gcc_mmss_gpll0_div_clk.clkr,
	[GCC_MSS_GPLL0_DIV_CLK] = &gcc_mss_gpll0_div_clk.clkr,
};

static struct gdsc *gcc_msm8996_gdscs[] = {
	[AGGRE0_NOC_GDSC] = &aggre0_noc_gdsc,
	[HLOS1_VOTE_AGGRE0_NOC_GDSC] = &hlos1_vote_aggre0_noc_gdsc,
	[HLOS1_VOTE_LPASS_ADSP_GDSC] = &hlos1_vote_lpass_adsp_gdsc,
	[HLOS1_VOTE_LPASS_CORE_GDSC] = &hlos1_vote_lpass_core_gdsc,
	[USB30_GDSC] = &usb30_gdsc,
	[PCIE0_GDSC] = &pcie0_gdsc,
	[PCIE1_GDSC] = &pcie1_gdsc,
	[PCIE2_GDSC] = &pcie2_gdsc,
	[UFS_GDSC] = &ufs_gdsc,
};

static const struct qcom_reset_map gcc_msm8996_resets[] = {
	[GCC_SYSTEM_NOC_BCR] = { 0x4000 },
	[GCC_CONFIG_NOC_BCR] = { 0x5000 },
	[GCC_PERIPH_NOC_BCR] = { 0x6000 },
	[GCC_IMEM_BCR] = { 0x8000 },
	[GCC_MMSS_BCR] = { 0x9000 },
	[GCC_PIMEM_BCR] = { 0x0a000 },
	[GCC_QDSS_BCR] = { 0x0c000 },
	[GCC_USB_30_BCR] = { 0x0f000 },
	[GCC_USB_20_BCR] = { 0x12000 },
	[GCC_QUSB2PHY_PRIM_BCR] = { 0x12038 },
	[GCC_QUSB2PHY_SEC_BCR] = { 0x1203c },
	[GCC_USB3_PHY_BCR] = { 0x50020 },
	[GCC_USB3PHY_PHY_BCR] = { 0x50024 },
	[GCC_USB_PHY_CFG_AHB2PHY_BCR] = { 0x6a000 },
	[GCC_SDCC1_BCR] = { 0x13000 },
	[GCC_SDCC2_BCR] = { 0x14000 },
	[GCC_SDCC3_BCR] = { 0x15000 },
	[GCC_SDCC4_BCR] = { 0x16000 },
	[GCC_BLSP1_BCR] = { 0x17000 },
	[GCC_BLSP1_QUP1_BCR] = { 0x19000 },
	[GCC_BLSP1_UART1_BCR] = { 0x1a000 },
	[GCC_BLSP1_QUP2_BCR] = { 0x1b000 },
	[GCC_BLSP1_UART2_BCR] = { 0x1c000 },
	[GCC_BLSP1_QUP3_BCR] = { 0x1d000 },
	[GCC_BLSP1_UART3_BCR] = { 0x1e000 },
	[GCC_BLSP1_QUP4_BCR] = { 0x1f000 },
	[GCC_BLSP1_UART4_BCR] = { 0x20000 },
	[GCC_BLSP1_QUP5_BCR] = { 0x21000 },
	[GCC_BLSP1_UART5_BCR] = { 0x22000 },
	[GCC_BLSP1_QUP6_BCR] = { 0x23000 },
	[GCC_BLSP1_UART6_BCR] = { 0x24000 },
	[GCC_BLSP2_BCR] = { 0x25000 },
	[GCC_BLSP2_QUP1_BCR] = { 0x26000 },
	[GCC_BLSP2_UART1_BCR] = { 0x27000 },
	[GCC_BLSP2_QUP2_BCR] = { 0x28000 },
	[GCC_BLSP2_UART2_BCR] = { 0x29000 },
	[GCC_BLSP2_QUP3_BCR] = { 0x2a000 },
	[GCC_BLSP2_UART3_BCR] = { 0x2b000 },
	[GCC_BLSP2_QUP4_BCR] = { 0x2c000 },
	[GCC_BLSP2_UART4_BCR] = { 0x2d000 },
	[GCC_BLSP2_QUP5_BCR] = { 0x2e000 },
	[GCC_BLSP2_UART5_BCR] = { 0x2f000 },
	[GCC_BLSP2_QUP6_BCR] = { 0x30000 },
	[GCC_BLSP2_UART6_BCR] = { 0x31000 },
	[GCC_PDM_BCR] = { 0x33000 },
	[GCC_PRNG_BCR] = { 0x34000 },
	[GCC_TSIF_BCR] = { 0x36000 },
	[GCC_TCSR_BCR] = { 0x37000 },
	[GCC_BOOT_ROM_BCR] = { 0x38000 },
	[GCC_MSG_RAM_BCR] = { 0x39000 },
	[GCC_TLMM_BCR] = { 0x3a000 },
	[GCC_MPM_BCR] = { 0x3b000 },
	[GCC_SEC_CTRL_BCR] = { 0x3d000 },
	[GCC_SPMI_BCR] = { 0x3f000 },
	[GCC_SPDM_BCR] = { 0x40000 },
	[GCC_CE1_BCR] = { 0x41000 },
	[GCC_BIMC_BCR] = { 0x44000 },
	[GCC_SNOC_BUS_TIMEOUT0_BCR] = { 0x49000 },
	[GCC_SNOC_BUS_TIMEOUT2_BCR] = { 0x49008 },
	[GCC_SNOC_BUS_TIMEOUT1_BCR] = { 0x49010 },
	[GCC_SNOC_BUS_TIMEOUT3_BCR] = { 0x49018 },
	[GCC_SNOC_BUS_TIMEOUT_EXTREF_BCR] = { 0x49020 },
	[GCC_PNOC_BUS_TIMEOUT0_BCR] = { 0x4a000 },
	[GCC_PNOC_BUS_TIMEOUT1_BCR] = { 0x4a008 },
	[GCC_PNOC_BUS_TIMEOUT2_BCR] = { 0x4a010 },
	[GCC_PNOC_BUS_TIMEOUT3_BCR] = { 0x4a018 },
	[GCC_PNOC_BUS_TIMEOUT4_BCR] = { 0x4a020 },
	[GCC_CNOC_BUS_TIMEOUT0_BCR] = { 0x4b000 },
	[GCC_CNOC_BUS_TIMEOUT1_BCR] = { 0x4b008 },
	[GCC_CNOC_BUS_TIMEOUT2_BCR] = { 0x4b010 },
	[GCC_CNOC_BUS_TIMEOUT3_BCR] = { 0x4b018 },
	[GCC_CNOC_BUS_TIMEOUT4_BCR] = { 0x4b020 },
	[GCC_CNOC_BUS_TIMEOUT5_BCR] = { 0x4b028 },
	[GCC_CNOC_BUS_TIMEOUT6_BCR] = { 0x4b030 },
	[GCC_CNOC_BUS_TIMEOUT7_BCR] = { 0x4b038 },
	[GCC_CNOC_BUS_TIMEOUT8_BCR] = { 0x80000 },
	[GCC_CNOC_BUS_TIMEOUT9_BCR] = { 0x80008 },
	[GCC_CNOC_BUS_TIMEOUT_EXTREF_BCR] = { 0x80010 },
	[GCC_APB2JTAG_BCR] = { 0x4c000 },
	[GCC_RBCPR_CX_BCR] = { 0x4e000 },
	[GCC_RBCPR_MX_BCR] = { 0x4f000 },
	[GCC_PCIE_0_BCR] = { 0x6b000 },
	[GCC_PCIE_0_PHY_BCR] = { 0x6c01c },
	[GCC_PCIE_1_BCR] = { 0x6d000 },
	[GCC_PCIE_1_PHY_BCR] = { 0x6d038 },
	[GCC_PCIE_2_BCR] = { 0x6e000 },
	[GCC_PCIE_2_PHY_BCR] = { 0x6e038 },
	[GCC_PCIE_PHY_BCR] = { 0x6f000 },
	[GCC_PCIE_PHY_COM_BCR] = { 0x6f014 },
	[GCC_PCIE_PHY_COM_NOCSR_BCR] = { 0x6f00c },
	[GCC_DCD_BCR] = { 0x70000 },
	[GCC_OBT_ODT_BCR] = { 0x73000 },
	[GCC_UFS_BCR] = { 0x75000 },
	[GCC_SSC_BCR] = { 0x63000 },
	[GCC_VS_BCR] = { 0x7a000 },
	[GCC_AGGRE0_NOC_BCR] = { 0x81000 },
	[GCC_AGGRE1_NOC_BCR] = { 0x82000 },
	[GCC_AGGRE2_NOC_BCR] = { 0x83000 },
	[GCC_DCC_BCR] = { 0x84000 },
	[GCC_IPA_BCR] = { 0x89000 },
	[GCC_QSPI_BCR] = { 0x8b000 },
	[GCC_SKL_BCR] = { 0x8c000 },
	[GCC_MSMPU_BCR] = { 0x8d000 },
	[GCC_MSS_Q6_BCR] = { 0x8e000 },
	[GCC_QREFS_VBG_CAL_BCR] = { 0x88020 },
	[GCC_MSS_RESTART] = { 0x8f008 },
};

static const struct regmap_config gcc_msm8996_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x8f010,
	.fast_io	= true,
};

static const struct qcom_cc_desc gcc_msm8996_desc = {
	.config = &gcc_msm8996_regmap_config,
	.clks = gcc_msm8996_clocks,
	.num_clks = ARRAY_SIZE(gcc_msm8996_clocks),
	.resets = gcc_msm8996_resets,
	.num_resets = ARRAY_SIZE(gcc_msm8996_resets),
	.gdscs = gcc_msm8996_gdscs,
	.num_gdscs = ARRAY_SIZE(gcc_msm8996_gdscs),
	.clk_hws = gcc_msm8996_hws,
	.num_clk_hws = ARRAY_SIZE(gcc_msm8996_hws),
};

static const struct of_device_id gcc_msm8996_match_table[] = {
	{ .compatible = "qcom,gcc-msm8996" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_msm8996_match_table);

static int gcc_msm8996_probe(struct platform_device *pdev)
{
	struct regmap *regmap;

	regmap = qcom_cc_map(pdev, &gcc_msm8996_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/*
	 * Set the HMSS_AHB_CLK_SLEEP_ENA bit to allow the hmss_ahb_clk to be
	 * turned off by hardware during certain apps low power modes.
	 */
	regmap_update_bits(regmap, 0x52008, BIT(21), BIT(21));

	return qcom_cc_really_probe(pdev, &gcc_msm8996_desc, regmap);
}

static struct platform_driver gcc_msm8996_driver = {
	.probe		= gcc_msm8996_probe,
	.driver		= {
		.name	= "gcc-msm8996",
		.of_match_table = gcc_msm8996_match_table,
	},
};

static int __init gcc_msm8996_init(void)
{
	return platform_driver_register(&gcc_msm8996_driver);
}
core_initcall(gcc_msm8996_init);

static void __exit gcc_msm8996_exit(void)
{
	platform_driver_unregister(&gcc_msm8996_driver);
}
module_exit(gcc_msm8996_exit);

MODULE_DESCRIPTION("QCOM GCC MSM8996 Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:gcc-msm8996");
