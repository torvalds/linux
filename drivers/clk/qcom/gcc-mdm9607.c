// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, Konrad Dybcio <konrad.dybcio@somainline.org>
 */

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

#include <dt-bindings/clock/qcom,gcc-mdm9607.h>

#include "common.h"
#include "clk-regmap.h"
#include "clk-alpha-pll.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-branch.h"
#include "reset.h"
#include "gdsc.h"

enum {
	P_XO,
	P_BIMC,
	P_GPLL0,
	P_GPLL1,
	P_GPLL2,
	P_SLEEP_CLK,
};

static struct clk_alpha_pll gpll0_early = {
	.offset = 0x21000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		.enable_reg = 0x45000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data)
		{
			.name = "gpll0_early",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct clk_alpha_pll_postdiv gpll0 = {
	.offset = 0x21000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr.hw.init = &(struct clk_init_data)
	{
		.name = "gpll0",
		.parent_hws = (const struct clk_hw *[]){ &gpll0_early.clkr.hw },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ops,
	},
};

static const struct parent_map gcc_xo_gpll0_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
};

static const struct clk_parent_data gcc_xo_gpll0[] = {
	{ .fw_name = "xo" },
	{ .hw = &gpll0.clkr.hw },
};

static struct clk_pll gpll1 = {
	.l_reg = 0x20004,
	.m_reg = 0x20008,
	.n_reg = 0x2000c,
	.config_reg = 0x20010,
	.mode_reg = 0x20000,
	.status_reg = 0x2001c,
	.status_bit = 17,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll1",
		.parent_data = &(const struct clk_parent_data){
			.fw_name = "xo",
		},
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

static struct clk_regmap gpll1_vote = {
	.enable_reg = 0x45000,
	.enable_mask = BIT(1),
	.hw.init = &(struct clk_init_data){
		.name = "gpll1_vote",
		.parent_hws = (const struct clk_hw *[]){ &gpll1.clkr.hw },
		.num_parents = 1,
		.ops = &clk_pll_vote_ops,
	},
};

static const struct parent_map gcc_xo_gpll0_gpll1_sleep_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL1, 2 },
	{ P_SLEEP_CLK, 6 },
};

static const struct clk_parent_data gcc_xo_gpll0_gpll1_sleep[] = {
	{ .fw_name = "xo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll1_vote.hw },
	{ .fw_name = "sleep_clk" },
};

static struct clk_alpha_pll gpll2_early = {
	.offset = 0x25000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		.enable_reg = 0x45000,
		.enable_mask = BIT(3), /* Yeah, apparently it's not 2 */
		.hw.init = &(struct clk_init_data)
		{
			.name = "gpll2_early",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct clk_alpha_pll_postdiv gpll2 = {
	.offset = 0x25000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr.hw.init = &(struct clk_init_data)
	{
		.name = "gpll2",
		.parent_hws = (const struct clk_hw *[]){ &gpll2_early.clkr.hw },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ops,
	},
};

static const struct parent_map gcc_xo_gpll0_gpll2_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL2, 2 },
};

static const struct clk_parent_data gcc_xo_gpll0_gpll2[] = {
	{ .fw_name = "xo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll2.clkr.hw },
};

static const struct parent_map gcc_xo_gpll0_gpll1_gpll2_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL1, 2 },
	{ P_GPLL2, 3 },
};

static const struct clk_parent_data gcc_xo_gpll0_gpll1_gpll2[] = {
	{ .fw_name = "xo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll1_vote.hw },
	{ .hw = &gpll2.clkr.hw },
};

static const struct freq_tbl ftbl_apss_ahb_clk[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(50000000, P_GPLL0, 16, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	{ }
};

static struct clk_rcg2 apss_ahb_clk_src = {
	.cmd_rcgr = 0x46000,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_apss_ahb_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "apss_ahb_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_pll bimc_pll = {
	.l_reg = 0x23004,
	.m_reg = 0x23008,
	.n_reg = 0x2300c,
	.config_reg = 0x23010,
	.mode_reg = 0x23000,
	.status_reg = 0x2301c,
	.status_bit = 17,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "bimc_pll",
		.parent_data = &(const struct clk_parent_data){
			.fw_name = "xo",
		},
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

static struct clk_regmap bimc_pll_vote = {
	.enable_reg = 0x45000,
	.enable_mask = BIT(3),
	.hw.init = &(struct clk_init_data){
		.name = "bimc_pll_vote",
		.parent_hws = (const struct clk_hw *[]){ &bimc_pll.clkr.hw },
		.num_parents = 1,
		.ops = &clk_pll_vote_ops,
	},
};

static const struct parent_map gcc_xo_gpll0_bimc_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_BIMC, 2 },
};

static const struct clk_parent_data gcc_xo_gpll0_bimc[] = {
	{ .fw_name = "xo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &bimc_pll_vote.hw },
};

static const struct freq_tbl ftbl_pcnoc_bfdcd_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(50000000, P_GPLL0, 16, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	{ }
};

static struct clk_rcg2 pcnoc_bfdcd_clk_src = {
	.cmd_rcgr = 0x27000,
	.freq_tbl = ftbl_pcnoc_bfdcd_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_bimc_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pcnoc_bfdcd_clk_src",
		.parent_data = gcc_xo_gpll0_bimc,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_bimc),
		.ops = &clk_rcg2_ops,
		.flags = CLK_IS_CRITICAL,
	},
};

static struct clk_rcg2 system_noc_bfdcd_clk_src = {
	.cmd_rcgr = 0x26004,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_bimc_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "system_noc_bfdcd_clk_src",
		.parent_data = gcc_xo_gpll0_bimc,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_bimc),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_blsp1_qup1_6_i2c_apps_clk[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(50000000, P_GPLL0, 16, 0, 0),
	{ }
};

static struct clk_rcg2 blsp1_qup1_i2c_apps_clk_src = {
	.cmd_rcgr = 0x200c,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_i2c_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup1_i2c_apps_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_blsp1_qup1_6_spi_apps_clk[] = {
	F(960000, P_XO, 10, 1, 2),
	F(4800000, P_XO, 4, 0, 0),
	F(9600000, P_XO, 2, 0, 0),
	F(16000000, P_GPLL0, 10, 1, 5),
	F(19200000, P_XO, 1, 0, 0),
	F(25000000, P_GPLL0, 16, 1, 2),
	F(50000000, P_GPLL0, 16, 0, 0),
	{ }
};

static struct clk_rcg2 blsp1_qup1_spi_apps_clk_src = {
	.cmd_rcgr = 0x2024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_spi_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup1_spi_apps_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup2_i2c_apps_clk_src = {
	.cmd_rcgr = 0x3000,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_i2c_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup2_i2c_apps_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup2_spi_apps_clk_src = {
	.cmd_rcgr = 0x3014,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_spi_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup2_spi_apps_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup3_i2c_apps_clk_src = {
	.cmd_rcgr = 0x4000,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_i2c_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup3_i2c_apps_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup3_spi_apps_clk_src = {
	.cmd_rcgr = 0x4024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_spi_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup3_spi_apps_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup4_i2c_apps_clk_src = {
	.cmd_rcgr = 0x5000,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_i2c_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup4_i2c_apps_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup4_spi_apps_clk_src = {
	.cmd_rcgr = 0x5024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_spi_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup4_spi_apps_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup5_i2c_apps_clk_src = {
	.cmd_rcgr = 0x6000,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_i2c_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup5_i2c_apps_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup5_spi_apps_clk_src = {
	.cmd_rcgr = 0x6024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_spi_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup5_spi_apps_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup6_i2c_apps_clk_src = {
	.cmd_rcgr = 0x7000,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_i2c_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup6_i2c_apps_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup6_spi_apps_clk_src = {
	.cmd_rcgr = 0x7024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_spi_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup6_spi_apps_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_blsp1_uart1_6_apps_clk[] = {
	F(3686400, P_GPLL0, 1, 72, 15625),
	F(7372800, P_GPLL0, 1, 144, 15625),
	F(14745600, P_GPLL0, 1, 288, 15625),
	F(16000000, P_GPLL0, 10, 1, 5),
	F(19200000, P_XO, 1, 0, 0),
	F(24000000, P_GPLL0, 1, 3, 100),
	F(25000000, P_GPLL0, 16, 1, 2),
	F(32000000, P_GPLL0, 1, 1, 25),
	F(40000000, P_GPLL0, 1, 1, 20),
	F(46400000, P_GPLL0, 1, 29, 500),
	F(48000000, P_GPLL0, 1, 3, 50),
	F(51200000, P_GPLL0, 1, 8, 125),
	F(56000000, P_GPLL0, 1, 7, 100),
	F(58982400, P_GPLL0, 1, 1152, 15625),
	F(60000000, P_GPLL0, 1, 3, 40),
	{ }
};

static struct clk_rcg2 blsp1_uart1_apps_clk_src = {
	.cmd_rcgr = 0x2044,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_uart1_6_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart1_apps_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_uart2_apps_clk_src = {
	.cmd_rcgr = 0x3034,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_uart1_6_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart2_apps_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_uart3_apps_clk_src = {
	.cmd_rcgr = 0x4044,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_uart1_6_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart3_apps_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_uart4_apps_clk_src = {
	.cmd_rcgr = 0x5044,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_uart1_6_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart4_apps_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_uart5_apps_clk_src = {
	.cmd_rcgr = 0x6044,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_uart1_6_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart5_apps_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_uart6_apps_clk_src = {
	.cmd_rcgr = 0x6044,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_uart1_6_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart6_apps_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_crypto_clk[] = {
	F(50000000, P_GPLL0, 16, 0, 0),
	F(80000000, P_GPLL0, 10, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	F(160000000, P_GPLL0, 5, 0, 0),
	{ }
};

static struct clk_rcg2 crypto_clk_src = {
	.cmd_rcgr = 0x16004,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_crypto_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "crypto_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_gp1_3_clk[] = {
	F(19200000, P_XO, 1, 0,	0),
	{ }
};

static struct clk_rcg2 gp1_clk_src = {
	.cmd_rcgr = 0x8004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll1_sleep_map,
	.freq_tbl = ftbl_gcc_gp1_3_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gp1_clk_src",
		.parent_data = gcc_xo_gpll0_gpll1_sleep,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll1_sleep),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gp2_clk_src = {
	.cmd_rcgr = 0x09004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll1_sleep_map,
	.freq_tbl = ftbl_gcc_gp1_3_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gp2_clk_src",
		.parent_data = gcc_xo_gpll0_gpll1_sleep,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll1_sleep),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gp3_clk_src = {
	.cmd_rcgr = 0x0a004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll1_sleep_map,
	.freq_tbl = ftbl_gcc_gp1_3_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gp3_clk_src",
		.parent_data = gcc_xo_gpll0_gpll1_sleep,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll1_sleep),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pdm2_clk[] = {
	F(64000000, P_GPLL0, 12.5, 0, 0),
	{ }
};

static struct clk_rcg2 pdm2_clk_src = {
	.cmd_rcgr = 0x44010,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_pdm2_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pdm2_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_sdcc_apps_clk[] = {
	F(144000, P_XO, 16, 3, 25),
	F(400000, P_XO, 12, 1, 4),
	F(20000000, P_GPLL0, 10, 1, 4),
	F(25000000, P_GPLL0, 16, 1, 2),
	F(50000000, P_GPLL0, 16, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	F(177770000, P_GPLL0, 4.5, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	{ }
};

static struct clk_rcg2 sdcc1_apps_clk_src = {
	.cmd_rcgr = 0x42004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_sdcc_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "sdcc1_apps_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_floor_ops,
	},
};

static struct clk_rcg2 sdcc2_apps_clk_src = {
	.cmd_rcgr = 0x43004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_sdcc_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "sdcc2_apps_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_floor_ops,
	},
};

static const struct freq_tbl ftbl_gcc_apss_tcu_clk[] = {
	F(155000000, P_GPLL2, 6, 0, 0),
	F(310000000, P_GPLL2, 3, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	{ }
};

static struct clk_rcg2 apss_tcu_clk_src = {
	.cmd_rcgr = 0x1207c,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll1_gpll2_map,
	.freq_tbl = ftbl_gcc_apss_tcu_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "apss_tcu_clk_src",
		.parent_data = gcc_xo_gpll0_gpll1_gpll2,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb_hs_system_clk[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(57140000, P_GPLL0, 14, 0, 0),
	F(69565000, P_GPLL0, 11.5, 0, 0),
	F(133330000, P_GPLL0, 6, 0, 0),
	F(177778000, P_GPLL0, 4.5, 0, 0),
	{ }
};

static struct clk_rcg2 usb_hs_system_clk_src = {
	.cmd_rcgr = 0x41010,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_usb_hs_system_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "usb_hs_system_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_usb_hsic_clk_src[] = {
	F(480000000, P_GPLL2, 1, 0, 0),
	{ }
};

static struct clk_rcg2 usb_hsic_clk_src = {
	.cmd_rcgr = 0x3d018,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll2_map,
	.freq_tbl = ftbl_usb_hsic_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "usb_hsic_clk_src",
		.parent_data = gcc_xo_gpll0_gpll2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll2),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_usb_hsic_io_cal_clk_src[] = {
	F(9600000, P_XO, 2, 0, 0),
	{ }
};

static struct clk_rcg2 usb_hsic_io_cal_clk_src = {
	.cmd_rcgr = 0x3d030,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_usb_hsic_io_cal_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "usb_hsic_io_cal_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_usb_hsic_system_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(57140000, P_GPLL0, 14, 0, 0),
	F(133330000, P_GPLL0, 6, 0, 0),
	F(177778000, P_GPLL0, 4.5, 0, 0),
	{ }
};

static struct clk_rcg2 usb_hsic_system_clk_src = {
	.cmd_rcgr = 0x3d000,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_usb_hsic_system_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "usb_hsic_system_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0),
		.ops = &clk_rcg2_ops,
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
			.parent_hws = (const struct clk_hw *[]){ &pcnoc_bfdcd_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_sleep_clk = {
	.halt_reg = 0x1004,
	.clkr = {
		.enable_reg = 0x1004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_sleep_clk",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "sleep_clk",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup1_i2c_apps_clk = {
	.halt_reg = 0x2008,
	.clkr = {
		.enable_reg = 0x2008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup1_i2c_apps_clk",
			.parent_hws = (const struct clk_hw *[]){ &blsp1_qup1_i2c_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup1_spi_apps_clk = {
	.halt_reg = 0x2004,
	.clkr = {
		.enable_reg = 0x2004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup1_spi_apps_clk",
			.parent_hws = (const struct clk_hw *[]){ &blsp1_qup1_spi_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup2_i2c_apps_clk = {
	.halt_reg = 0x3010,
	.clkr = {
		.enable_reg = 0x3010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup2_i2c_apps_clk",
			.parent_hws = (const struct clk_hw *[]){ &blsp1_qup2_i2c_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup2_spi_apps_clk = {
	.halt_reg = 0x300c,
	.clkr = {
		.enable_reg = 0x300c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup2_spi_apps_clk",
			.parent_hws = (const struct clk_hw *[]){ &blsp1_qup2_spi_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup3_i2c_apps_clk = {
	.halt_reg = 0x4020,
	.clkr = {
		.enable_reg = 0x4020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup3_i2c_apps_clk",
			.parent_hws = (const struct clk_hw *[]){ &blsp1_qup3_i2c_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup3_spi_apps_clk = {
	.halt_reg = 0x401c,
	.clkr = {
		.enable_reg = 0x401c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup3_spi_apps_clk",
			.parent_hws = (const struct clk_hw *[]){ &blsp1_qup3_spi_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup4_i2c_apps_clk = {
	.halt_reg = 0x5020,
	.clkr = {
		.enable_reg = 0x5020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup4_i2c_apps_clk",
			.parent_hws = (const struct clk_hw *[]){ &blsp1_qup4_i2c_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup4_spi_apps_clk = {
	.halt_reg = 0x501c,
	.clkr = {
		.enable_reg = 0x501c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup4_spi_apps_clk",
			.parent_hws = (const struct clk_hw *[]){ &blsp1_qup4_spi_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup5_i2c_apps_clk = {
	.halt_reg = 0x6020,
	.clkr = {
		.enable_reg = 0x6020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup5_i2c_apps_clk",
			.parent_hws = (const struct clk_hw *[]){ &blsp1_qup5_i2c_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup5_spi_apps_clk = {
	.halt_reg = 0x601c,
	.clkr = {
		.enable_reg = 0x601c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup5_spi_apps_clk",
			.parent_hws = (const struct clk_hw *[]){ &blsp1_qup5_spi_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup6_i2c_apps_clk = {
	.halt_reg = 0x7020,
	.clkr = {
		.enable_reg = 0x7020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup6_i2c_apps_clk",
			.parent_hws = (const struct clk_hw *[]){ &blsp1_qup6_i2c_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup6_spi_apps_clk = {
	.halt_reg = 0x701c,
	.clkr = {
		.enable_reg = 0x701c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup6_spi_apps_clk",
			.parent_hws = (const struct clk_hw *[]){ &blsp1_qup6_spi_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart1_apps_clk = {
	.halt_reg = 0x203c,
	.clkr = {
		.enable_reg = 0x203c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart1_apps_clk",
			.parent_hws = (const struct clk_hw *[]){ &blsp1_uart1_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart2_apps_clk = {
	.halt_reg = 0x302c,
	.clkr = {
		.enable_reg = 0x302c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart2_apps_clk",
			.parent_hws = (const struct clk_hw *[]){ &blsp1_uart2_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart3_apps_clk = {
	.halt_reg = 0x403c,
	.clkr = {
		.enable_reg = 0x403c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart3_apps_clk",
			.parent_hws = (const struct clk_hw *[]){ &blsp1_uart3_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart4_apps_clk = {
	.halt_reg = 0x503c,
	.clkr = {
		.enable_reg = 0x503c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart4_apps_clk",
			.parent_hws = (const struct clk_hw *[]){ &blsp1_uart4_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart5_apps_clk = {
	.halt_reg = 0x603c,
	.clkr = {
		.enable_reg = 0x603c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart5_apps_clk",
			.parent_hws = (const struct clk_hw *[]){ &blsp1_uart5_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart6_apps_clk = {
	.halt_reg = 0x703c,
	.clkr = {
		.enable_reg = 0x703c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart6_apps_clk",
			.parent_hws = (const struct clk_hw *[]){ &blsp1_uart6_apps_clk_src.clkr.hw },
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
			.parent_hws = (const struct clk_hw *[]){ &pcnoc_bfdcd_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_crypto_ahb_clk = {
	.halt_reg = 0x16024,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_crypto_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){ &pcnoc_bfdcd_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_crypto_axi_clk = {
	.halt_reg = 0x16020,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_crypto_axi_clk",
			.parent_hws = (const struct clk_hw *[]){ &pcnoc_bfdcd_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_crypto_clk = {
	.halt_reg = 0x1601c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_crypto_clk",
			.parent_hws = (const struct clk_hw *[]){ &crypto_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp1_clk = {
	.halt_reg = 0x08000,
	.clkr = {
		.enable_reg = 0x08000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gp1_clk",
			.parent_hws = (const struct clk_hw *[]){ &gp1_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp2_clk = {
	.halt_reg = 0x09000,
	.clkr = {
		.enable_reg = 0x09000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gp2_clk",
			.parent_hws = (const struct clk_hw *[]){ &gp2_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp3_clk = {
	.halt_reg = 0x0a000,
	.clkr = {
		.enable_reg = 0x0a000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gp3_clk",
			.parent_hws = (const struct clk_hw *[]){ &gp3_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mss_cfg_ahb_clk = {
	.halt_reg = 0x49000,
	.clkr = {
		.enable_reg = 0x49000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mss_cfg_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){ &pcnoc_bfdcd_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm2_clk = {
	.halt_reg = 0x4400c,
	.clkr = {
		.enable_reg = 0x4400c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pdm2_clk",
			.parent_hws = (const struct clk_hw *[]){ &pdm2_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm_ahb_clk = {
	.halt_reg = 0x44004,
	.clkr = {
		.enable_reg = 0x44004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pdm_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){ &pcnoc_bfdcd_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
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
			.parent_hws = (const struct clk_hw *[]){ &pcnoc_bfdcd_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_ahb_clk = {
	.halt_reg = 0x4201c,
	.clkr = {
		.enable_reg = 0x4201c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc1_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){ &pcnoc_bfdcd_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_apps_clk = {
	.halt_reg = 0x42018,
	.clkr = {
		.enable_reg = 0x42018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc1_apps_clk",
			.parent_hws = (const struct clk_hw *[]){ &sdcc1_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc2_ahb_clk = {
	.halt_reg = 0x4301c,
	.clkr = {
		.enable_reg = 0x4301c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc2_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){ &pcnoc_bfdcd_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc2_apps_clk = {
	.halt_reg = 0x43018,
	.clkr = {
		.enable_reg = 0x43018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc2_apps_clk",
			.parent_hws = (const struct clk_hw *[]){ &sdcc2_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_rcg2 bimc_ddr_clk_src = {
	.cmd_rcgr = 0x32004,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_bimc_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "bimc_ddr_clk_src",
		.parent_data = gcc_xo_gpll0_bimc,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
		.flags = CLK_GET_RATE_NOCACHE,
	},
};

static struct clk_branch gcc_mss_q6_bimc_axi_clk = {
	.halt_reg = 0x49004,
	.clkr = {
		.enable_reg = 0x49004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mss_q6_bimc_axi_clk",
			.parent_hws = (const struct clk_hw *[]){ &bimc_ddr_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_apss_tcu_clk = {
	.halt_reg = 0x12018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4500c,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_apss_tcu_clk",
			.parent_hws = (const struct clk_hw *[]){ &bimc_ddr_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_smmu_cfg_clk = {
	.halt_reg = 0x12038,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4500c,
		.enable_mask = BIT(12),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_smmu_cfg_clk",
			.parent_hws = (const struct clk_hw *[]){ &pcnoc_bfdcd_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qdss_dap_clk = {
	.halt_reg = 0x29084,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(19),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qdss_dap_clk",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb2a_phy_sleep_clk = {
	.halt_reg = 0x4102c,
	.clkr = {
		.enable_reg = 0x4102c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb2a_phy_sleep_clk",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "sleep_clk",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
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
			.parent_hws = (const struct clk_hw *[]){ &pcnoc_bfdcd_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb_hs_ahb_clk = {
	.halt_reg = 0x41008,
	.clkr = {
		.enable_reg = 0x41008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb_hs_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){ &pcnoc_bfdcd_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb_hs_system_clk = {
	.halt_reg = 0x41004,
	.clkr = {
		.enable_reg = 0x41004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb_hs_system_clk",
			.parent_hws = (const struct clk_hw *[]){ &usb_hs_system_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
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
			.parent_hws = (const struct clk_hw *[]){ &pcnoc_bfdcd_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_apss_axi_clk = {
	.halt_reg = 0x4601c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(13),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_apss_axi_clk",
			.parent_hws = (const struct clk_hw *[]){ &pcnoc_bfdcd_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap *gcc_mdm9607_clocks[] = {
	[GPLL0] = &gpll0.clkr,
	[GPLL0_EARLY] = &gpll0_early.clkr,
	[GPLL1] = &gpll1.clkr,
	[GPLL1_VOTE] = &gpll1_vote,
	[GPLL2] = &gpll2.clkr,
	[GPLL2_EARLY] = &gpll2_early.clkr,
	[BIMC_PLL] = &bimc_pll.clkr,
	[BIMC_PLL_VOTE] = &bimc_pll_vote,
	[BIMC_DDR_CLK_SRC] = &bimc_ddr_clk_src.clkr,
	[PCNOC_BFDCD_CLK_SRC] = &pcnoc_bfdcd_clk_src.clkr,
	[SYSTEM_NOC_BFDCD_CLK_SRC] = &system_noc_bfdcd_clk_src.clkr,
	[APSS_AHB_CLK_SRC] = &apss_ahb_clk_src.clkr,
	[BLSP1_QUP1_I2C_APPS_CLK_SRC] = &blsp1_qup1_i2c_apps_clk_src.clkr,
	[BLSP1_QUP1_SPI_APPS_CLK_SRC] = &blsp1_qup1_spi_apps_clk_src.clkr,
	[BLSP1_QUP2_I2C_APPS_CLK_SRC] = &blsp1_qup2_i2c_apps_clk_src.clkr,
	[BLSP1_QUP2_SPI_APPS_CLK_SRC] = &blsp1_qup2_spi_apps_clk_src.clkr,
	[BLSP1_QUP3_I2C_APPS_CLK_SRC] = &blsp1_qup3_i2c_apps_clk_src.clkr,
	[BLSP1_QUP3_SPI_APPS_CLK_SRC] = &blsp1_qup3_spi_apps_clk_src.clkr,
	[BLSP1_QUP4_I2C_APPS_CLK_SRC] = &blsp1_qup4_i2c_apps_clk_src.clkr,
	[BLSP1_QUP4_SPI_APPS_CLK_SRC] = &blsp1_qup4_spi_apps_clk_src.clkr,
	[BLSP1_QUP5_I2C_APPS_CLK_SRC] = &blsp1_qup5_i2c_apps_clk_src.clkr,
	[BLSP1_QUP5_SPI_APPS_CLK_SRC] = &blsp1_qup5_spi_apps_clk_src.clkr,
	[BLSP1_QUP6_I2C_APPS_CLK_SRC] = &blsp1_qup6_i2c_apps_clk_src.clkr,
	[BLSP1_QUP6_SPI_APPS_CLK_SRC] = &blsp1_qup6_spi_apps_clk_src.clkr,
	[BLSP1_UART1_APPS_CLK_SRC] = &blsp1_uart1_apps_clk_src.clkr,
	[BLSP1_UART2_APPS_CLK_SRC] = &blsp1_uart2_apps_clk_src.clkr,
	[BLSP1_UART3_APPS_CLK_SRC] = &blsp1_uart3_apps_clk_src.clkr,
	[BLSP1_UART4_APPS_CLK_SRC] = &blsp1_uart4_apps_clk_src.clkr,
	[BLSP1_UART5_APPS_CLK_SRC] = &blsp1_uart5_apps_clk_src.clkr,
	[BLSP1_UART6_APPS_CLK_SRC] = &blsp1_uart6_apps_clk_src.clkr,
	[CRYPTO_CLK_SRC] = &crypto_clk_src.clkr,
	[GP1_CLK_SRC] = &gp1_clk_src.clkr,
	[GP2_CLK_SRC] = &gp2_clk_src.clkr,
	[GP3_CLK_SRC] = &gp3_clk_src.clkr,
	[PDM2_CLK_SRC] = &pdm2_clk_src.clkr,
	[SDCC1_APPS_CLK_SRC] = &sdcc1_apps_clk_src.clkr,
	[SDCC2_APPS_CLK_SRC] = &sdcc2_apps_clk_src.clkr,
	[APSS_TCU_CLK_SRC] = &apss_tcu_clk_src.clkr,
	[USB_HS_SYSTEM_CLK_SRC] = &usb_hs_system_clk_src.clkr,
	[GCC_BLSP1_AHB_CLK] = &gcc_blsp1_ahb_clk.clkr,
	[GCC_BLSP1_SLEEP_CLK] = &gcc_blsp1_sleep_clk.clkr,
	[GCC_BLSP1_QUP1_I2C_APPS_CLK] = &gcc_blsp1_qup1_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP1_SPI_APPS_CLK] = &gcc_blsp1_qup1_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP2_I2C_APPS_CLK] = &gcc_blsp1_qup2_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP2_SPI_APPS_CLK] = &gcc_blsp1_qup2_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP3_I2C_APPS_CLK] = &gcc_blsp1_qup3_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP3_SPI_APPS_CLK] = &gcc_blsp1_qup3_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP4_I2C_APPS_CLK] = &gcc_blsp1_qup4_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP4_SPI_APPS_CLK] = &gcc_blsp1_qup4_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP5_I2C_APPS_CLK] = &gcc_blsp1_qup5_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP5_SPI_APPS_CLK] = &gcc_blsp1_qup5_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP6_I2C_APPS_CLK] = &gcc_blsp1_qup6_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP6_SPI_APPS_CLK] = &gcc_blsp1_qup6_spi_apps_clk.clkr,
	[GCC_BLSP1_UART1_APPS_CLK] = &gcc_blsp1_uart1_apps_clk.clkr,
	[GCC_BLSP1_UART2_APPS_CLK] = &gcc_blsp1_uart2_apps_clk.clkr,
	[GCC_BLSP1_UART3_APPS_CLK] = &gcc_blsp1_uart3_apps_clk.clkr,
	[GCC_BLSP1_UART4_APPS_CLK] = &gcc_blsp1_uart4_apps_clk.clkr,
	[GCC_BLSP1_UART5_APPS_CLK] = &gcc_blsp1_uart5_apps_clk.clkr,
	[GCC_BLSP1_UART6_APPS_CLK] = &gcc_blsp1_uart6_apps_clk.clkr,
	[GCC_BOOT_ROM_AHB_CLK] = &gcc_boot_rom_ahb_clk.clkr,
	[GCC_CRYPTO_AHB_CLK] = &gcc_crypto_ahb_clk.clkr,
	[GCC_CRYPTO_AXI_CLK] = &gcc_crypto_axi_clk.clkr,
	[GCC_CRYPTO_CLK] = &gcc_crypto_clk.clkr,
	[GCC_GP1_CLK] = &gcc_gp1_clk.clkr,
	[GCC_GP2_CLK] = &gcc_gp2_clk.clkr,
	[GCC_GP3_CLK] = &gcc_gp3_clk.clkr,
	[GCC_MSS_CFG_AHB_CLK] = &gcc_mss_cfg_ahb_clk.clkr,
	[GCC_PDM2_CLK] = &gcc_pdm2_clk.clkr,
	[GCC_PDM_AHB_CLK] = &gcc_pdm_ahb_clk.clkr,
	[GCC_PRNG_AHB_CLK] = &gcc_prng_ahb_clk.clkr,
	[GCC_SDCC1_AHB_CLK] = &gcc_sdcc1_ahb_clk.clkr,
	[GCC_SDCC1_APPS_CLK] = &gcc_sdcc1_apps_clk.clkr,
	[GCC_SDCC2_AHB_CLK] = &gcc_sdcc2_ahb_clk.clkr,
	[GCC_SDCC2_APPS_CLK] = &gcc_sdcc2_apps_clk.clkr,
	[GCC_SMMU_CFG_CLK] = &gcc_smmu_cfg_clk.clkr,
	[GCC_USB2A_PHY_SLEEP_CLK] = &gcc_usb2a_phy_sleep_clk.clkr,
	[GCC_USB_HS_PHY_CFG_AHB_CLK] = &gcc_usb_hs_phy_cfg_ahb_clk.clkr,
	[GCC_USB_HS_AHB_CLK] = &gcc_usb_hs_ahb_clk.clkr,
	[GCC_USB_HS_SYSTEM_CLK] = &gcc_usb_hs_system_clk.clkr,
	[GCC_APSS_TCU_CLK] = &gcc_apss_tcu_clk.clkr,
	[GCC_MSS_Q6_BIMC_AXI_CLK] = &gcc_mss_q6_bimc_axi_clk.clkr,
	[GCC_QDSS_DAP_CLK] = &gcc_qdss_dap_clk.clkr,
	[GCC_APSS_AHB_CLK] = &gcc_apss_ahb_clk.clkr,
	[GCC_APSS_AXI_CLK] = &gcc_apss_axi_clk.clkr,
	[GCC_USB_HSIC_CLK_SRC] = &usb_hsic_clk_src.clkr,
	[GCC_USB_HSIC_IO_CAL_CLK_SRC] = &usb_hsic_io_cal_clk_src.clkr,
	[GCC_USB_HSIC_SYSTEM_CLK_SRC] = &usb_hsic_system_clk_src.clkr,
};

static const struct qcom_reset_map gcc_mdm9607_resets[] = {
	[USB_HS_HSIC_BCR] = { 0x3d05c },
	[GCC_MSS_RESTART] = { 0x3e000 },
	[USB_HS_BCR] = { 0x41000 },
	[USB2_HS_PHY_ONLY_BCR] = { 0x41034 },
	[QUSB2_PHY_BCR] = { 0x4103c },
};

static const struct regmap_config gcc_mdm9607_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x80000,
	.fast_io	= true,
};

static const struct qcom_cc_desc gcc_mdm9607_desc = {
	.config = &gcc_mdm9607_regmap_config,
	.clks = gcc_mdm9607_clocks,
	.num_clks = ARRAY_SIZE(gcc_mdm9607_clocks),
	.resets = gcc_mdm9607_resets,
	.num_resets = ARRAY_SIZE(gcc_mdm9607_resets),
};

static const struct of_device_id gcc_mdm9607_match_table[] = {
	{ .compatible = "qcom,gcc-mdm9607" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_mdm9607_match_table);

static int gcc_mdm9607_probe(struct platform_device *pdev)
{
	struct regmap *regmap;

	regmap = qcom_cc_map(pdev, &gcc_mdm9607_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/* Vote for GPLL0 to turn on. Needed by acpuclock. */
	regmap_update_bits(regmap, 0x45000, BIT(0), BIT(0));

	return qcom_cc_really_probe(pdev, &gcc_mdm9607_desc, regmap);
}

static struct platform_driver gcc_mdm9607_driver = {
	.probe		= gcc_mdm9607_probe,
	.driver		= {
		.name	= "gcc-mdm9607",
		.of_match_table = gcc_mdm9607_match_table,
	},
};

static int __init gcc_mdm9607_init(void)
{
	return platform_driver_register(&gcc_mdm9607_driver);
}
core_initcall(gcc_mdm9607_init);

static void __exit gcc_mdm9607_exit(void)
{
	platform_driver_unregister(&gcc_mdm9607_driver);
}
module_exit(gcc_mdm9607_exit);

MODULE_DESCRIPTION("Qualcomm GCC mdm9607 Driver");
MODULE_LICENSE("GPL v2");
