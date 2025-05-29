// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2020 Linaro Limited
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

#include <dt-bindings/clock/qcom,gcc-msm8939.h>
#include <dt-bindings/reset/qcom,gcc-msm8939.h>

#include "common.h"
#include "clk-regmap.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-branch.h"
#include "reset.h"
#include "gdsc.h"

enum {
	P_XO,
	P_GPLL0,
	P_GPLL0_AUX,
	P_BIMC,
	P_GPLL1,
	P_GPLL1_AUX,
	P_GPLL2,
	P_GPLL2_AUX,
	P_GPLL3,
	P_GPLL3_AUX,
	P_GPLL4,
	P_GPLL5,
	P_GPLL5_AUX,
	P_GPLL5_EARLY,
	P_GPLL6,
	P_GPLL6_AUX,
	P_SLEEP_CLK,
	P_DSI0_PHYPLL_BYTE,
	P_DSI0_PHYPLL_DSI,
	P_EXT_PRI_I2S,
	P_EXT_SEC_I2S,
	P_EXT_MCLK,
};

static struct clk_pll gpll0 = {
	.l_reg = 0x21004,
	.m_reg = 0x21008,
	.n_reg = 0x2100c,
	.config_reg = 0x21010,
	.mode_reg = 0x21000,
	.status_reg = 0x2101c,
	.status_bit = 17,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll0",
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xo",
		},
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

static struct clk_regmap gpll0_vote = {
	.enable_reg = 0x45000,
	.enable_mask = BIT(0),
	.hw.init = &(struct clk_init_data){
		.name = "gpll0_vote",
		.parent_hws = (const struct clk_hw*[]) {
			&gpll0.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_pll_vote_ops,
	},
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
		.parent_data = &(const struct clk_parent_data) {
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
		.parent_hws = (const struct clk_hw*[]) {
			&gpll1.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_pll_vote_ops,
	},
};

static struct clk_pll gpll2 = {
	.l_reg = 0x4a004,
	.m_reg = 0x4a008,
	.n_reg = 0x4a00c,
	.config_reg = 0x4a010,
	.mode_reg = 0x4a000,
	.status_reg = 0x4a01c,
	.status_bit = 17,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll2",
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xo",
		},
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

static struct clk_regmap gpll2_vote = {
	.enable_reg = 0x45000,
	.enable_mask = BIT(2),
	.hw.init = &(struct clk_init_data){
		.name = "gpll2_vote",
		.parent_hws = (const struct clk_hw*[]) {
			&gpll2.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_pll_vote_ops,
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
		.parent_data = &(const struct clk_parent_data) {
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
		.parent_hws = (const struct clk_hw*[]) {
			&bimc_pll.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_pll_vote_ops,
	},
};

static struct clk_pll gpll3 = {
	.l_reg = 0x22004,
	.m_reg = 0x22008,
	.n_reg = 0x2200c,
	.config_reg = 0x22010,
	.mode_reg = 0x22000,
	.status_reg = 0x2201c,
	.status_bit = 17,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll3",
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xo",
		},
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

static struct clk_regmap gpll3_vote = {
	.enable_reg = 0x45000,
	.enable_mask = BIT(4),
	.hw.init = &(struct clk_init_data){
		.name = "gpll3_vote",
		.parent_hws = (const struct clk_hw*[]) {
			&gpll3.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_pll_vote_ops,
	},
};

/* GPLL3 at 1100 MHz, main output enabled. */
static const struct pll_config gpll3_config = {
	.l = 57,
	.m = 7,
	.n = 24,
	.vco_val = 0x0,
	.vco_mask = BIT(20),
	.pre_div_val = 0x0,
	.pre_div_mask = BIT(12),
	.post_div_val = 0x0,
	.post_div_mask = BIT(9) | BIT(8),
	.mn_ena_mask = BIT(24),
	.main_output_mask = BIT(0),
	.aux_output_mask = BIT(1),
};

static struct clk_pll gpll4 = {
	.l_reg = 0x24004,
	.m_reg = 0x24008,
	.n_reg = 0x2400c,
	.config_reg = 0x24010,
	.mode_reg = 0x24000,
	.status_reg = 0x2401c,
	.status_bit = 17,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll4",
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xo",
		},
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

static struct clk_regmap gpll4_vote = {
	.enable_reg = 0x45000,
	.enable_mask = BIT(5),
	.hw.init = &(struct clk_init_data){
		.name = "gpll4_vote",
		.parent_hws = (const struct clk_hw*[]) {
			&gpll4.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_pll_vote_ops,
	},
};

/* GPLL4 at 1200 MHz, main output enabled. */
static struct pll_config gpll4_config = {
	.l = 62,
	.m = 1,
	.n = 2,
	.vco_val = 0x0,
	.vco_mask = BIT(20),
	.pre_div_val = 0x0,
	.pre_div_mask = BIT(12),
	.post_div_val = 0x0,
	.post_div_mask = BIT(9) | BIT(8),
	.mn_ena_mask = BIT(24),
	.main_output_mask = BIT(0),
};

static struct clk_pll gpll5 = {
	.l_reg = 0x25004,
	.m_reg = 0x25008,
	.n_reg = 0x2500c,
	.config_reg = 0x25010,
	.mode_reg = 0x25000,
	.status_reg = 0x2501c,
	.status_bit = 17,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll5",
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xo",
		},
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

static struct clk_regmap gpll5_vote = {
	.enable_reg = 0x45000,
	.enable_mask = BIT(6),
	.hw.init = &(struct clk_init_data){
		.name = "gpll5_vote",
		.parent_hws = (const struct clk_hw*[]) {
			&gpll5.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_pll_vote_ops,
	},
};

static struct clk_pll gpll6 = {
	.l_reg = 0x37004,
	.m_reg = 0x37008,
	.n_reg = 0x3700c,
	.config_reg = 0x37010,
	.mode_reg = 0x37000,
	.status_reg = 0x3701c,
	.status_bit = 17,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll6",
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xo",
		},
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

static struct clk_regmap gpll6_vote = {
	.enable_reg = 0x45000,
	.enable_mask = BIT(7),
	.hw.init = &(struct clk_init_data){
		.name = "gpll6_vote",
		.parent_hws = (const struct clk_hw*[]) {
			&gpll6.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_pll_vote_ops,
	},
};

static const struct parent_map gcc_xo_gpll0_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
};

static const struct clk_parent_data gcc_xo_gpll0_parent_data[] = {
	{ .fw_name = "xo" },
	{ .hw = &gpll0_vote.hw },
};

static const struct parent_map gcc_xo_gpll0_bimc_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_BIMC, 2 },
};

static const struct clk_parent_data gcc_xo_gpll0_bimc_parent_data[] = {
	{ .fw_name = "xo" },
	{ .hw = &gpll0_vote.hw },
	{ .hw = &bimc_pll_vote.hw },
};

static const struct parent_map gcc_xo_gpll0_gpll6a_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL6_AUX, 2 },
};

static const struct clk_parent_data gcc_xo_gpll0_gpll6a_parent_data[] = {
	{ .fw_name = "xo" },
	{ .hw = &gpll0_vote.hw },
	{ .hw = &gpll6_vote.hw },
};

static const struct parent_map gcc_xo_gpll0_gpll2a_gpll3_gpll6a_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL2_AUX, 4 },
	{ P_GPLL3, 2 },
	{ P_GPLL6_AUX, 3 },
};

static const struct clk_parent_data gcc_xo_gpll0_gpll2a_gpll3_gpll6a_parent_data[] = {
	{ .fw_name = "xo" },
	{ .hw = &gpll0_vote.hw },
	{ .hw = &gpll2_vote.hw },
	{ .hw = &gpll3_vote.hw },
	{ .hw = &gpll6_vote.hw },
};

static const struct parent_map gcc_xo_gpll0_gpll2_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL2, 2 },
};

static const struct clk_parent_data gcc_xo_gpll0_gpll2_parent_data[] = {
	{ .fw_name = "xo" },
	{ .hw = &gpll0_vote.hw },
	{ .hw = &gpll2_vote.hw },
};

static const struct parent_map gcc_xo_gpll0_gpll2_gpll4_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL2, 3 },
	{ P_GPLL4, 2 },
};

static const struct clk_parent_data gcc_xo_gpll0_gpll2_gpll4_parent_data[] = {
	{ .fw_name = "xo" },
	{ .hw = &gpll0_vote.hw },
	{ .hw = &gpll2_vote.hw },
	{ .hw = &gpll4_vote.hw },
};

static const struct parent_map gcc_xo_gpll0a_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0_AUX, 2 },
};

static const struct clk_parent_data gcc_xo_gpll0a_parent_data[] = {
	{ .fw_name = "xo" },
	{ .hw = &gpll0_vote.hw },
};

static const struct parent_map gcc_xo_gpll0_gpll1a_sleep_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL1_AUX, 2 },
	{ P_SLEEP_CLK, 6 },
};

static const struct clk_parent_data gcc_xo_gpll0_gpll1a_sleep_parent_data[] = {
	{ .fw_name = "xo" },
	{ .hw = &gpll0_vote.hw },
	{ .hw = &gpll1_vote.hw },
	{ .fw_name = "sleep_clk", .name = "sleep_clk" },
};

static const struct parent_map gcc_xo_gpll0_gpll1a_gpll6_sleep_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL1_AUX, 2 },
	{ P_GPLL6, 3 },
	{ P_SLEEP_CLK, 6 },
};

static const struct clk_parent_data gcc_xo_gpll0_gpll1a_gpll6_sleep_parent_data[] = {
	{ .fw_name = "xo" },
	{ .hw = &gpll0_vote.hw },
	{ .hw = &gpll1_vote.hw },
	{ .hw = &gpll6_vote.hw },
	{ .fw_name = "sleep_clk", .name = "sleep_clk" },
};

static const struct parent_map gcc_xo_gpll0_gpll1a_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL1_AUX, 2 },
};

static const struct clk_parent_data gcc_xo_gpll0_gpll1a_parent_data[] = {
	{ .fw_name = "xo" },
	{ .hw = &gpll0_vote.hw },
	{ .hw = &gpll1_vote.hw },
};

static const struct parent_map gcc_xo_dsibyte_map[] = {
	{ P_XO, 0, },
	{ P_DSI0_PHYPLL_BYTE, 2 },
};

static const struct clk_parent_data gcc_xo_dsibyte_parent_data[] = {
	{ .fw_name = "xo" },
	{ .fw_name = "dsi0pllbyte", .name = "dsi0pllbyte" },
};

static const struct parent_map gcc_xo_gpll0a_dsibyte_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0_AUX, 2 },
	{ P_DSI0_PHYPLL_BYTE, 1 },
};

static const struct clk_parent_data gcc_xo_gpll0a_dsibyte_parent_data[] = {
	{ .fw_name = "xo" },
	{ .hw = &gpll0_vote.hw },
	{ .fw_name = "dsi0pllbyte", .name = "dsi0pllbyte" },
};

static const struct parent_map gcc_xo_gpll1_dsiphy_gpll6_gpll3a_gpll0a_map[] = {
	{ P_XO, 0 },
	{ P_GPLL1, 1 },
	{ P_DSI0_PHYPLL_DSI, 2 },
	{ P_GPLL6, 3 },
	{ P_GPLL3_AUX, 4 },
	{ P_GPLL0_AUX, 5 },
};

static const struct clk_parent_data gcc_xo_gpll1_dsiphy_gpll6_gpll3a_gpll0a_parent_data[] = {
	{ .fw_name = "xo" },
	{ .hw = &gpll1_vote.hw },
	{ .fw_name = "dsi0pll", .name = "dsi0pll" },
	{ .hw = &gpll6_vote.hw },
	{ .hw = &gpll3_vote.hw },
	{ .hw = &gpll0_vote.hw },
};

static const struct parent_map gcc_xo_gpll0a_dsiphy_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0_AUX, 2 },
	{ P_DSI0_PHYPLL_DSI, 1 },
};

static const struct clk_parent_data gcc_xo_gpll0a_dsiphy_parent_data[] = {
	{ .fw_name = "xo" },
	{ .hw = &gpll0_vote.hw },
	{ .fw_name = "dsi0pll", .name = "dsi0pll" },
};

static const struct parent_map gcc_xo_gpll0_gpll5a_gpll6_bimc_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL5_AUX, 3 },
	{ P_GPLL6, 2 },
	{ P_BIMC, 4 },
};

static const struct clk_parent_data gcc_xo_gpll0_gpll5a_gpll6_bimc_parent_data[] = {
	{ .fw_name = "xo" },
	{ .hw = &gpll0_vote.hw },
	{ .hw = &gpll5_vote.hw },
	{ .hw = &gpll6_vote.hw },
	{ .hw = &bimc_pll_vote.hw },
};

static const struct parent_map gcc_xo_gpll0_gpll1_sleep_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL1, 2 },
	{ P_SLEEP_CLK, 6 }
};

static const struct clk_parent_data gcc_xo_gpll0_gpll1_sleep_parent_data[] = {
	{ .fw_name = "xo" },
	{ .hw = &gpll0_vote.hw },
	{ .hw = &gpll1_vote.hw },
	{ .fw_name = "sleep_clk", .name = "sleep_clk" },
};

static const struct parent_map gcc_xo_gpll1_epi2s_emclk_sleep_map[] = {
	{ P_XO, 0 },
	{ P_GPLL1, 1 },
	{ P_EXT_PRI_I2S, 2 },
	{ P_EXT_MCLK, 3 },
	{ P_SLEEP_CLK, 6 }
};

static const struct clk_parent_data gcc_xo_gpll1_epi2s_emclk_sleep_parent_data[] = {
	{ .fw_name = "xo" },
	{ .hw = &gpll0_vote.hw },
	{ .fw_name = "ext_pri_i2s", .name = "ext_pri_i2s" },
	{ .fw_name = "ext_mclk", .name = "ext_mclk" },
	{ .fw_name = "sleep_clk", .name = "sleep_clk" },
};

static const struct parent_map gcc_xo_gpll1_esi2s_emclk_sleep_map[] = {
	{ P_XO, 0 },
	{ P_GPLL1, 1 },
	{ P_EXT_SEC_I2S, 2 },
	{ P_EXT_MCLK, 3 },
	{ P_SLEEP_CLK, 6 }
};

static const struct clk_parent_data gcc_xo_gpll1_esi2s_emclk_sleep_parent_data[] = {
	{ .fw_name = "xo" },
	{ .hw = &gpll1_vote.hw },
	{ .fw_name = "ext_sec_i2s", .name = "ext_sec_i2s" },
	{ .fw_name = "ext_mclk", .name = "ext_mclk" },
	{ .fw_name = "sleep_clk", .name = "sleep_clk" },
};

static const struct parent_map gcc_xo_sleep_map[] = {
	{ P_XO, 0 },
	{ P_SLEEP_CLK, 6 }
};

static const struct clk_parent_data gcc_xo_sleep_parent_data[] = {
	{ .fw_name = "xo" },
	{ .fw_name = "sleep_clk", .name = "sleep_clk" },
};

static const struct parent_map gcc_xo_gpll1_emclk_sleep_map[] = {
	{ P_XO, 0 },
	{ P_GPLL1, 1 },
	{ P_EXT_MCLK, 2 },
	{ P_SLEEP_CLK, 6 }
};

static const struct clk_parent_data gcc_xo_gpll1_emclk_sleep_parent_data[] = {
	{ .fw_name = "xo" },
	{ .hw = &gpll1_vote.hw },
	{ .fw_name = "ext_mclk", .name = "ext_mclk" },
	{ .fw_name = "sleep_clk", .name = "sleep_clk" },
};

static const struct clk_parent_data gcc_xo_gpll6_gpll0_parent_data[] = {
	{ .fw_name = "xo" },
	{ .hw = &gpll6_vote.hw },
	{ .hw = &gpll0_vote.hw },
};

static const struct clk_parent_data gcc_xo_gpll6_gpll0a_parent_data[] = {
	{ .fw_name = "xo" },
	{ .hw = &gpll6_vote.hw },
	{ .hw = &gpll0_vote.hw },
};

static struct clk_rcg2 pcnoc_bfdcd_clk_src = {
	.cmd_rcgr = 0x27000,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pcnoc_bfdcd_clk_src",
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 system_noc_bfdcd_clk_src = {
	.cmd_rcgr = 0x26004,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll6a_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "system_noc_bfdcd_clk_src",
		.parent_data = gcc_xo_gpll0_gpll6a_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll6a_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 bimc_ddr_clk_src = {
	.cmd_rcgr = 0x32024,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_bimc_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "bimc_ddr_clk_src",
		.parent_data = gcc_xo_gpll0_bimc_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_bimc_parent_data),
		.ops = &clk_rcg2_ops,
		.flags = CLK_GET_RATE_NOCACHE,
	},
};

static struct clk_rcg2 system_mm_noc_bfdcd_clk_src = {
	.cmd_rcgr = 0x2600c,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll6a_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "system_mm_noc_bfdcd_clk_src",
		.parent_data = gcc_xo_gpll0_gpll6a_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll6a_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_camss_ahb_clk[] = {
	F(40000000, P_GPLL0, 10, 1, 2),
	F(80000000, P_GPLL0, 10, 0, 0),
	{ }
};

static struct clk_rcg2 camss_ahb_clk_src = {
	.cmd_rcgr = 0x5a000,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_camss_ahb_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "camss_ahb_clk_src",
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_apss_ahb_clk[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(50000000, P_GPLL0, 16, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	F(133330000, P_GPLL0, 6, 0, 0),
	{ }
};

static struct clk_rcg2 apss_ahb_clk_src = {
	.cmd_rcgr = 0x46000,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_apss_ahb_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "apss_ahb_clk_src",
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_camss_csi0_1_2_clk[] = {
	F(100000000, P_GPLL0, 8, 0,	0),
	F(200000000, P_GPLL0, 4, 0,	0),
	{ }
};

static struct clk_rcg2 csi0_clk_src = {
	.cmd_rcgr = 0x4e020,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_camss_csi0_1_2_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csi0_clk_src",
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 csi1_clk_src = {
	.cmd_rcgr = 0x4f020,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_camss_csi0_1_2_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csi1_clk_src",
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 csi2_clk_src = {
	.cmd_rcgr = 0x3c020,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_camss_csi0_1_2_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csi2_clk_src",
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_oxili_gfx3d_clk[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(50000000, P_GPLL0, 16, 0, 0),
	F(80000000, P_GPLL0, 10, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	F(160000000, P_GPLL0, 5, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(220000000, P_GPLL3, 5, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	F(310000000, P_GPLL2_AUX, 3, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	F(465000000, P_GPLL2_AUX, 2, 0, 0),
	F(550000000, P_GPLL3, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gfx3d_clk_src = {
	.cmd_rcgr = 0x59000,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll2a_gpll3_gpll6a_map,
	.freq_tbl = ftbl_gcc_oxili_gfx3d_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gfx3d_clk_src",
		.parent_data = gcc_xo_gpll0_gpll2a_gpll3_gpll6a_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll2a_gpll3_gpll6a_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_camss_vfe0_clk[] = {
	F(50000000, P_GPLL0, 16, 0, 0),
	F(80000000, P_GPLL0, 10, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	F(160000000, P_GPLL0, 5, 0, 0),
	F(177780000, P_GPLL0, 4.5, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	F(320000000, P_GPLL0, 2.5, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	F(465000000, P_GPLL2, 2, 0, 0),
	F(480000000, P_GPLL4, 2.5, 0, 0),
	F(600000000, P_GPLL4, 2, 0, 0),
	{ }
};

static struct clk_rcg2 vfe0_clk_src = {
	.cmd_rcgr = 0x58000,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll2_gpll4_map,
	.freq_tbl = ftbl_gcc_camss_vfe0_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "vfe0_clk_src",
		.parent_data = gcc_xo_gpll0_gpll2_gpll4_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll2_gpll4_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_blsp1_qup1_6_i2c_apps_clk[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(50000000, P_GPLL0, 16, 0, 0),
	{ }
};

static struct clk_rcg2 blsp1_qup1_i2c_apps_clk_src = {
	.cmd_rcgr = 0x0200c,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_i2c_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup1_i2c_apps_clk_src",
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
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
	.cmd_rcgr = 0x02024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_spi_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup1_spi_apps_clk_src",
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup2_i2c_apps_clk_src = {
	.cmd_rcgr = 0x03000,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_i2c_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup2_i2c_apps_clk_src",
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup2_spi_apps_clk_src = {
	.cmd_rcgr = 0x03014,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_spi_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup2_spi_apps_clk_src",
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup3_i2c_apps_clk_src = {
	.cmd_rcgr = 0x04000,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_i2c_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup3_i2c_apps_clk_src",
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup3_spi_apps_clk_src = {
	.cmd_rcgr = 0x04024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_spi_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup3_spi_apps_clk_src",
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup4_i2c_apps_clk_src = {
	.cmd_rcgr = 0x05000,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_i2c_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup4_i2c_apps_clk_src",
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup4_spi_apps_clk_src = {
	.cmd_rcgr = 0x05024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_spi_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup4_spi_apps_clk_src",
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup5_i2c_apps_clk_src = {
	.cmd_rcgr = 0x06000,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_i2c_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup5_i2c_apps_clk_src",
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup5_spi_apps_clk_src = {
	.cmd_rcgr = 0x06024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_spi_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup5_spi_apps_clk_src",
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup6_i2c_apps_clk_src = {
	.cmd_rcgr = 0x07000,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_i2c_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup6_i2c_apps_clk_src",
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup6_spi_apps_clk_src = {
	.cmd_rcgr = 0x07024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_spi_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup6_spi_apps_clk_src",
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
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
	.cmd_rcgr = 0x02044,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_uart1_6_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart1_apps_clk_src",
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_uart2_apps_clk_src = {
	.cmd_rcgr = 0x03034,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_blsp1_uart1_6_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart2_apps_clk_src",
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_camss_cci_clk[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(37500000, P_GPLL0, 1, 3, 64),
	{ }
};

static struct clk_rcg2 cci_clk_src = {
	.cmd_rcgr = 0x51000,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0a_map,
	.freq_tbl = ftbl_gcc_camss_cci_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cci_clk_src",
		.parent_data = gcc_xo_gpll0a_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0a_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

/*
 * This is a frequency table for "General Purpose" clocks.
 * These clocks can be muxed to the SoC pins and may be used by
 * external devices. They're often used as PWM source.
 *
 * See comment at ftbl_gcc_gp1_3_clk.
 */
static const struct freq_tbl ftbl_gcc_camss_gp0_1_clk[] = {
	F(10000,   P_XO,    16,  1, 120),
	F(100000,  P_XO,    16,  1,  12),
	F(500000,  P_GPLL0, 16,  1, 100),
	F(1000000, P_GPLL0, 16,  1,  50),
	F(2500000, P_GPLL0, 16,  1,  20),
	F(5000000, P_GPLL0, 16,  1,  10),
	F(100000000, P_GPLL0, 8, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	{ }
};

static struct clk_rcg2 camss_gp0_clk_src = {
	.cmd_rcgr = 0x54000,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll1a_sleep_map,
	.freq_tbl = ftbl_gcc_camss_gp0_1_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "camss_gp0_clk_src",
		.parent_data = gcc_xo_gpll0_gpll1a_sleep_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll1a_sleep_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 camss_gp1_clk_src = {
	.cmd_rcgr = 0x55000,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll1a_sleep_map,
	.freq_tbl = ftbl_gcc_camss_gp0_1_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "camss_gp1_clk_src",
		.parent_data = gcc_xo_gpll0_gpll1a_sleep_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll1a_sleep_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_camss_jpeg0_clk[] = {
	F(133330000, P_GPLL0, 6, 0,	0),
	F(266670000, P_GPLL0, 3, 0,	0),
	F(320000000, P_GPLL0, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 jpeg0_clk_src = {
	.cmd_rcgr = 0x57000,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_camss_jpeg0_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "jpeg0_clk_src",
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_camss_mclk0_1_clk[] = {
	F(24000000, P_GPLL6, 1, 1, 45),
	F(66670000, P_GPLL0, 12, 0, 0),
	{ }
};

static struct clk_rcg2 mclk0_clk_src = {
	.cmd_rcgr = 0x52000,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll1a_gpll6_sleep_map,
	.freq_tbl = ftbl_gcc_camss_mclk0_1_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mclk0_clk_src",
		.parent_data = gcc_xo_gpll0_gpll1a_gpll6_sleep_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll1a_gpll6_sleep_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 mclk1_clk_src = {
	.cmd_rcgr = 0x53000,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll1a_gpll6_sleep_map,
	.freq_tbl = ftbl_gcc_camss_mclk0_1_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mclk1_clk_src",
		.parent_data = gcc_xo_gpll0_gpll1a_gpll6_sleep_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll1a_gpll6_sleep_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_camss_csi0_1phytimer_clk[] = {
	F(100000000, P_GPLL0, 8, 0,	0),
	F(200000000, P_GPLL0, 4, 0,	0),
	{ }
};

static struct clk_rcg2 csi0phytimer_clk_src = {
	.cmd_rcgr = 0x4e000,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll1a_map,
	.freq_tbl = ftbl_gcc_camss_csi0_1phytimer_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csi0phytimer_clk_src",
		.parent_data = gcc_xo_gpll0_gpll1a_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll1a_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 csi1phytimer_clk_src = {
	.cmd_rcgr = 0x4f000,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll1a_map,
	.freq_tbl = ftbl_gcc_camss_csi0_1phytimer_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csi1phytimer_clk_src",
		.parent_data = gcc_xo_gpll0_gpll1a_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll1a_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_camss_cpp_clk[] = {
	F(160000000, P_GPLL0, 5, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(228570000, P_GPLL0, 3.5, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	F(320000000, P_GPLL0, 2.5, 0, 0),
	F(465000000, P_GPLL2, 2, 0, 0),
	{ }
};

static struct clk_rcg2 cpp_clk_src = {
	.cmd_rcgr = 0x58018,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll2_map,
	.freq_tbl = ftbl_gcc_camss_cpp_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cpp_clk_src",
		.parent_data = gcc_xo_gpll0_gpll2_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll2_parent_data),
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

/* This is not in the documentation but is in the downstream driver */
static struct clk_rcg2 crypto_clk_src = {
	.cmd_rcgr = 0x16004,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_crypto_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "crypto_clk_src",
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

/*
 * This is a frequency table for "General Purpose" clocks.
 * These clocks can be muxed to the SoC pins and may be used by
 * external devices. They're often used as PWM source.
 *
 * Please note that MND divider must be enabled for duty-cycle
 * control to be possible. (M != N) Also since D register is configured
 * with a value multiplied by 2, and duty cycle is calculated as
 *                             (2 * D) % 2^W
 *                DutyCycle = ----------------
 *                              2 * (N % 2^W)
 * (where W = .mnd_width)
 * N must be half or less than maximum value for the register.
 * Otherwise duty-cycle control would be limited.
 * (e.g. for 8-bit NMD N should be less than 128)
 */
static const struct freq_tbl ftbl_gcc_gp1_3_clk[] = {
	F(10000,   P_XO,    16,  1, 120),
	F(100000,  P_XO,    16,  1,  12),
	F(500000,  P_GPLL0, 16,  1, 100),
	F(1000000, P_GPLL0, 16,  1,  50),
	F(2500000, P_GPLL0, 16,  1,  20),
	F(5000000, P_GPLL0, 16,  1,  10),
	F(19200000, P_XO, 1, 0,	0),
	{ }
};

static struct clk_rcg2 gp1_clk_src = {
	.cmd_rcgr = 0x08004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll1a_sleep_map,
	.freq_tbl = ftbl_gcc_gp1_3_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gp1_clk_src",
		.parent_data = gcc_xo_gpll0_gpll1a_sleep_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll1a_sleep_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gp2_clk_src = {
	.cmd_rcgr = 0x09004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll1a_sleep_map,
	.freq_tbl = ftbl_gcc_gp1_3_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gp2_clk_src",
		.parent_data = gcc_xo_gpll0_gpll1a_sleep_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll1a_sleep_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gp3_clk_src = {
	.cmd_rcgr = 0x0a004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll1a_sleep_map,
	.freq_tbl = ftbl_gcc_gp1_3_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gp3_clk_src",
		.parent_data = gcc_xo_gpll0_gpll1a_sleep_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll1a_sleep_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 byte0_clk_src = {
	.cmd_rcgr = 0x4d044,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0a_dsibyte_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "byte0_clk_src",
		.parent_data = gcc_xo_gpll0a_dsibyte_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0a_dsibyte_parent_data),
		.ops = &clk_byte2_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_rcg2 byte1_clk_src = {
	.cmd_rcgr = 0x4d0b0,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0a_dsibyte_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "byte1_clk_src",
		.parent_data = gcc_xo_gpll0a_dsibyte_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0a_dsibyte_parent_data),
		.ops = &clk_byte2_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct freq_tbl ftbl_gcc_mdss_esc_clk[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 esc0_clk_src = {
	.cmd_rcgr = 0x4d060,
	.hid_width = 5,
	.parent_map = gcc_xo_dsibyte_map,
	.freq_tbl = ftbl_gcc_mdss_esc_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "esc0_clk_src",
		.parent_data = gcc_xo_dsibyte_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_dsibyte_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 esc1_clk_src = {
	.cmd_rcgr = 0x4d0a8,
	.hid_width = 5,
	.parent_map = gcc_xo_dsibyte_map,
	.freq_tbl = ftbl_gcc_mdss_esc_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "esc1_clk_src",
		.parent_data = gcc_xo_dsibyte_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_dsibyte_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_mdss_mdp_clk[] = {
	F(50000000, P_GPLL0_AUX, 16, 0, 0),
	F(80000000, P_GPLL0_AUX, 10, 0, 0),
	F(100000000, P_GPLL0_AUX, 8, 0, 0),
	F(145500000, P_GPLL0_AUX, 5.5, 0, 0),
	F(153600000, P_GPLL0, 4, 0, 0),
	F(160000000, P_GPLL0_AUX, 5, 0, 0),
	F(177780000, P_GPLL0_AUX, 4.5, 0, 0),
	F(200000000, P_GPLL0_AUX, 4, 0, 0),
	F(266670000, P_GPLL0_AUX, 3, 0, 0),
	F(307200000, P_GPLL1, 2, 0, 0),
	F(366670000, P_GPLL3_AUX, 3, 0, 0),
	{ }
};

static struct clk_rcg2 mdp_clk_src = {
	.cmd_rcgr = 0x4d014,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll1_dsiphy_gpll6_gpll3a_gpll0a_map,
	.freq_tbl = ftbl_gcc_mdss_mdp_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mdp_clk_src",
		.parent_data = gcc_xo_gpll1_dsiphy_gpll6_gpll3a_gpll0a_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll1_dsiphy_gpll6_gpll3a_gpll0a_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 pclk0_clk_src = {
	.cmd_rcgr = 0x4d000,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0a_dsiphy_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pclk0_clk_src",
		.parent_data = gcc_xo_gpll0a_dsiphy_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0a_dsiphy_parent_data),
		.ops = &clk_pixel_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_rcg2 pclk1_clk_src = {
	.cmd_rcgr = 0x4d0b8,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0a_dsiphy_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pclk1_clk_src",
		.parent_data = gcc_xo_gpll0a_dsiphy_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0a_dsiphy_parent_data),
		.ops = &clk_pixel_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct freq_tbl ftbl_gcc_mdss_vsync_clk[] = {
	F(19200000, P_XO, 1, 0,	0),
	{ }
};

static struct clk_rcg2 vsync_clk_src = {
	.cmd_rcgr = 0x4d02c,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0a_map,
	.freq_tbl = ftbl_gcc_mdss_vsync_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "vsync_clk_src",
		.parent_data = gcc_xo_gpll0a_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0a_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pdm2_clk[] = {
	F(64000000, P_GPLL0, 12.5, 0, 0),
	{ }
};

/* This is not in the documentation but is in the downstream driver */
static struct clk_rcg2 pdm2_clk_src = {
	.cmd_rcgr = 0x44010,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_pdm2_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pdm2_clk_src",
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
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
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
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
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
		.ops = &clk_rcg2_floor_ops,
	},
};

static const struct freq_tbl ftbl_gcc_apss_tcu_clk[] = {
	F(154285000, P_GPLL6, 7, 0, 0),
	F(320000000, P_GPLL0, 2.5, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	{ }
};

static struct clk_rcg2 apss_tcu_clk_src = {
	.cmd_rcgr = 0x1207c,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll5a_gpll6_bimc_map,
	.freq_tbl = ftbl_gcc_apss_tcu_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "apss_tcu_clk_src",
		.parent_data = gcc_xo_gpll0_gpll5a_gpll6_bimc_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll5a_gpll6_bimc_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_bimc_gpu_clk[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266500000, P_BIMC, 4, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	F(533000000, P_BIMC, 2, 0, 0),
	{ }
};

static struct clk_rcg2 bimc_gpu_clk_src = {
	.cmd_rcgr = 0x31028,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll5a_gpll6_bimc_map,
	.freq_tbl = ftbl_gcc_bimc_gpu_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "bimc_gpu_clk_src",
		.parent_data = gcc_xo_gpll0_gpll5a_gpll6_bimc_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll5a_gpll6_bimc_parent_data),
		.flags = CLK_GET_RATE_NOCACHE,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb_hs_system_clk[] = {
	F(57140000, P_GPLL0, 14, 0, 0),
	F(80000000, P_GPLL0, 10, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	{ }
};

static struct clk_rcg2 usb_hs_system_clk_src = {
	.cmd_rcgr = 0x41010,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_usb_hs_system_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "usb_hs_system_clk_src",
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb_fs_system_clk[] = {
	F(64000000, P_GPLL0, 12.5, 0, 0),
	{ }
};

static struct clk_rcg2 usb_fs_system_clk_src = {
	.cmd_rcgr = 0x3f010,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_usb_fs_system_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "usb_fs_system_clk_src",
		.parent_data = gcc_xo_gpll6_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll6_gpll0_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb_fs_ic_clk[] = {
	F(60000000, P_GPLL6, 1, 1, 18),
	{ }
};

static struct clk_rcg2 usb_fs_ic_clk_src = {
	.cmd_rcgr = 0x3f034,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_usb_fs_ic_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "usb_fs_ic_clk_src",
		.parent_data = gcc_xo_gpll6_gpll0a_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll6_gpll0a_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_ultaudio_ahb_clk[] = {
	F(3200000, P_XO, 6, 0, 0),
	F(6400000, P_XO, 3, 0, 0),
	F(9600000, P_XO, 2, 0, 0),
	F(19200000, P_XO, 1, 0, 0),
	F(40000000, P_GPLL0, 10, 1, 2),
	F(66670000, P_GPLL0, 12, 0, 0),
	F(80000000, P_GPLL0, 10, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	{ }
};

static struct clk_rcg2 ultaudio_ahbfabric_clk_src = {
	.cmd_rcgr = 0x1c010,
	.hid_width = 5,
	.mnd_width = 8,
	.parent_map = gcc_xo_gpll0_gpll1_sleep_map,
	.freq_tbl = ftbl_gcc_ultaudio_ahb_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "ultaudio_ahbfabric_clk_src",
		.parent_data = gcc_xo_gpll0_gpll1_sleep_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll1_sleep_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_ultaudio_ahbfabric_ixfabric_clk = {
	.halt_reg = 0x1c028,
	.clkr = {
		.enable_reg = 0x1c028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ultaudio_ahbfabric_ixfabric_clk",
			.parent_hws = (const struct clk_hw*[]){
				&ultaudio_ahbfabric_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ultaudio_ahbfabric_ixfabric_lpm_clk = {
	.halt_reg = 0x1c024,
	.clkr = {
		.enable_reg = 0x1c024,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ultaudio_ahbfabric_ixfabric_lpm_clk",
			.parent_hws = (const struct clk_hw*[]){
				&ultaudio_ahbfabric_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static const struct freq_tbl ftbl_gcc_ultaudio_lpaif_i2s_clk[] = {
	F(128000, P_XO, 10, 1, 15),
	F(256000, P_XO, 5, 1, 15),
	F(384000, P_XO, 5, 1, 10),
	F(512000, P_XO, 5, 2, 15),
	F(576000, P_XO, 5, 3, 20),
	F(705600, P_GPLL1, 16, 1, 80),
	F(768000, P_XO, 5, 1, 5),
	F(800000, P_XO, 5, 5, 24),
	F(1024000, P_XO, 5, 4, 15),
	F(1152000, P_XO, 1, 3, 50),
	F(1411200, P_GPLL1, 16, 1, 40),
	F(1536000, P_XO, 1, 2, 25),
	F(1600000, P_XO, 12, 0, 0),
	F(1728000, P_XO, 5, 9, 20),
	F(2048000, P_XO, 5, 8, 15),
	F(2304000, P_XO, 5, 3, 5),
	F(2400000, P_XO, 8, 0, 0),
	F(2822400, P_GPLL1, 16, 1, 20),
	F(3072000, P_XO, 5, 4, 5),
	F(4096000, P_GPLL1, 9, 2, 49),
	F(4800000, P_XO, 4, 0, 0),
	F(5644800, P_GPLL1, 16, 1, 10),
	F(6144000, P_GPLL1, 7, 1, 21),
	F(8192000, P_GPLL1, 9, 4, 49),
	F(9600000, P_XO, 2, 0, 0),
	F(11289600, P_GPLL1, 16, 1, 5),
	F(12288000, P_GPLL1, 7, 2, 21),
	{ }
};

static struct clk_rcg2 ultaudio_lpaif_pri_i2s_clk_src = {
	.cmd_rcgr = 0x1c054,
	.hid_width = 5,
	.mnd_width = 8,
	.parent_map = gcc_xo_gpll1_epi2s_emclk_sleep_map,
	.freq_tbl = ftbl_gcc_ultaudio_lpaif_i2s_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "ultaudio_lpaif_pri_i2s_clk_src",
		.parent_data = gcc_xo_gpll1_epi2s_emclk_sleep_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll1_epi2s_emclk_sleep_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_ultaudio_lpaif_pri_i2s_clk = {
	.halt_reg = 0x1c068,
	.clkr = {
		.enable_reg = 0x1c068,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ultaudio_lpaif_pri_i2s_clk",
			.parent_hws = (const struct clk_hw*[]){
				&ultaudio_lpaif_pri_i2s_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_rcg2 ultaudio_lpaif_sec_i2s_clk_src = {
	.cmd_rcgr = 0x1c06c,
	.hid_width = 5,
	.mnd_width = 8,
	.parent_map = gcc_xo_gpll1_esi2s_emclk_sleep_map,
	.freq_tbl = ftbl_gcc_ultaudio_lpaif_i2s_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "ultaudio_lpaif_sec_i2s_clk_src",
		.parent_data = gcc_xo_gpll1_esi2s_emclk_sleep_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll1_esi2s_emclk_sleep_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_ultaudio_lpaif_sec_i2s_clk = {
	.halt_reg = 0x1c080,
	.clkr = {
		.enable_reg = 0x1c080,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ultaudio_lpaif_sec_i2s_clk",
			.parent_hws = (const struct clk_hw*[]){
				&ultaudio_lpaif_sec_i2s_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_rcg2 ultaudio_lpaif_aux_i2s_clk_src = {
	.cmd_rcgr = 0x1c084,
	.hid_width = 5,
	.mnd_width = 8,
	.parent_map = gcc_xo_gpll1_emclk_sleep_map,
	.freq_tbl = ftbl_gcc_ultaudio_lpaif_i2s_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "ultaudio_lpaif_aux_i2s_clk_src",
		.parent_data = gcc_xo_gpll1_esi2s_emclk_sleep_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll1_esi2s_emclk_sleep_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_ultaudio_lpaif_aux_i2s_clk = {
	.halt_reg = 0x1c098,
	.clkr = {
		.enable_reg = 0x1c098,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ultaudio_lpaif_aux_i2s_clk",
			.parent_hws = (const struct clk_hw*[]){
				&ultaudio_lpaif_aux_i2s_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static const struct freq_tbl ftbl_gcc_ultaudio_xo_clk[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 ultaudio_xo_clk_src = {
	.cmd_rcgr = 0x1c034,
	.hid_width = 5,
	.parent_map = gcc_xo_sleep_map,
	.freq_tbl = ftbl_gcc_ultaudio_xo_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "ultaudio_xo_clk_src",
		.parent_data = gcc_xo_sleep_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_sleep_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_ultaudio_avsync_xo_clk = {
	.halt_reg = 0x1c04c,
	.clkr = {
		.enable_reg = 0x1c04c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ultaudio_avsync_xo_clk",
			.parent_hws = (const struct clk_hw*[]){
				&ultaudio_xo_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ultaudio_stc_xo_clk = {
	.halt_reg = 0x1c050,
	.clkr = {
		.enable_reg = 0x1c050,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ultaudio_stc_xo_clk",
			.parent_hws = (const struct clk_hw*[]){
				&ultaudio_xo_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static const struct freq_tbl ftbl_codec_clk[] = {
	F(9600000, P_XO, 2, 0, 0),
	F(12288000, P_XO, 1, 16, 25),
	F(19200000, P_XO, 1, 0, 0),
	F(11289600, P_EXT_MCLK, 1, 0, 0),
	{ }
};

static struct clk_rcg2 codec_digcodec_clk_src = {
	.cmd_rcgr = 0x1c09c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll1_emclk_sleep_map,
	.freq_tbl = ftbl_codec_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "codec_digcodec_clk_src",
		.parent_data = gcc_xo_gpll1_emclk_sleep_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll1_emclk_sleep_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_codec_digcodec_clk = {
	.halt_reg = 0x1c0b0,
	.clkr = {
		.enable_reg = 0x1c0b0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ultaudio_codec_digcodec_clk",
			.parent_hws = (const struct clk_hw*[]){
				&codec_digcodec_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ultaudio_pcnoc_mport_clk = {
	.halt_reg = 0x1c000,
	.clkr = {
		.enable_reg = 0x1c000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ultaudio_pcnoc_mport_clk",
			.parent_hws = (const struct clk_hw*[]){
				&pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ultaudio_pcnoc_sway_clk = {
	.halt_reg = 0x1c004,
	.clkr = {
		.enable_reg = 0x1c004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ultaudio_pcnoc_sway_clk",
			.parent_hws = (const struct clk_hw*[]){
				&pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static const struct freq_tbl ftbl_gcc_venus0_vcodec0_clk[] = {
	F(133330000, P_GPLL0, 6, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	{ }
};

static struct clk_rcg2 vcodec0_clk_src = {
	.cmd_rcgr = 0x4C000,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.freq_tbl = ftbl_gcc_venus0_vcodec0_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "vcodec0_clk_src",
		.parent_data = gcc_xo_gpll0_parent_data,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_parent_data),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_blsp1_ahb_clk = {
	.halt_reg = 0x01008,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(10),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_sleep_clk = {
	.halt_reg = 0x01004,
	.clkr = {
		.enable_reg = 0x01004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup1_i2c_apps_clk = {
	.halt_reg = 0x02008,
	.clkr = {
		.enable_reg = 0x02008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup1_i2c_apps_clk",
			.parent_hws = (const struct clk_hw*[]){
				&blsp1_qup1_i2c_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup1_spi_apps_clk = {
	.halt_reg = 0x02004,
	.clkr = {
		.enable_reg = 0x02004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup1_spi_apps_clk",
			.parent_hws = (const struct clk_hw*[]){
				&blsp1_qup1_spi_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup2_i2c_apps_clk = {
	.halt_reg = 0x03010,
	.clkr = {
		.enable_reg = 0x03010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup2_i2c_apps_clk",
			.parent_hws = (const struct clk_hw*[]){
				&blsp1_qup2_i2c_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup2_spi_apps_clk = {
	.halt_reg = 0x0300c,
	.clkr = {
		.enable_reg = 0x0300c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup2_spi_apps_clk",
			.parent_hws = (const struct clk_hw*[]){
				&blsp1_qup2_spi_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup3_i2c_apps_clk = {
	.halt_reg = 0x04020,
	.clkr = {
		.enable_reg = 0x04020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup3_i2c_apps_clk",
			.parent_hws = (const struct clk_hw*[]){
				&blsp1_qup3_i2c_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup3_spi_apps_clk = {
	.halt_reg = 0x0401c,
	.clkr = {
		.enable_reg = 0x0401c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup3_spi_apps_clk",
			.parent_hws = (const struct clk_hw*[]){
				&blsp1_qup3_spi_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup4_i2c_apps_clk = {
	.halt_reg = 0x05020,
	.clkr = {
		.enable_reg = 0x05020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup4_i2c_apps_clk",
			.parent_hws = (const struct clk_hw*[]){
				&blsp1_qup4_i2c_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup4_spi_apps_clk = {
	.halt_reg = 0x0501c,
	.clkr = {
		.enable_reg = 0x0501c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup4_spi_apps_clk",
			.parent_hws = (const struct clk_hw*[]){
				&blsp1_qup4_spi_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup5_i2c_apps_clk = {
	.halt_reg = 0x06020,
	.clkr = {
		.enable_reg = 0x06020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup5_i2c_apps_clk",
			.parent_hws = (const struct clk_hw*[]){
				&blsp1_qup5_i2c_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup5_spi_apps_clk = {
	.halt_reg = 0x0601c,
	.clkr = {
		.enable_reg = 0x0601c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup5_spi_apps_clk",
			.parent_hws = (const struct clk_hw*[]){
				&blsp1_qup5_spi_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup6_i2c_apps_clk = {
	.halt_reg = 0x07020,
	.clkr = {
		.enable_reg = 0x07020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup6_i2c_apps_clk",
			.parent_hws = (const struct clk_hw*[]){
				&blsp1_qup6_i2c_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup6_spi_apps_clk = {
	.halt_reg = 0x0701c,
	.clkr = {
		.enable_reg = 0x0701c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup6_spi_apps_clk",
			.parent_hws = (const struct clk_hw*[]){
				&blsp1_qup6_spi_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart1_apps_clk = {
	.halt_reg = 0x0203c,
	.clkr = {
		.enable_reg = 0x0203c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart1_apps_clk",
			.parent_hws = (const struct clk_hw*[]){
				&blsp1_uart1_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart2_apps_clk = {
	.halt_reg = 0x0302c,
	.clkr = {
		.enable_reg = 0x0302c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart2_apps_clk",
			.parent_hws = (const struct clk_hw*[]){
				&blsp1_uart2_apps_clk_src.clkr.hw,
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
			.parent_hws = (const struct clk_hw*[]){
				&pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_cci_ahb_clk = {
	.halt_reg = 0x5101c,
	.clkr = {
		.enable_reg = 0x5101c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_cci_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&camss_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_cci_clk = {
	.halt_reg = 0x51018,
	.clkr = {
		.enable_reg = 0x51018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_cci_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cci_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi0_ahb_clk = {
	.halt_reg = 0x4e040,
	.clkr = {
		.enable_reg = 0x4e040,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_csi0_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&camss_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi0_clk = {
	.halt_reg = 0x4e03c,
	.clkr = {
		.enable_reg = 0x4e03c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_csi0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&csi0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi0phy_clk = {
	.halt_reg = 0x4e048,
	.clkr = {
		.enable_reg = 0x4e048,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_csi0phy_clk",
			.parent_hws = (const struct clk_hw*[]){
				&csi0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi0pix_clk = {
	.halt_reg = 0x4e058,
	.clkr = {
		.enable_reg = 0x4e058,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_csi0pix_clk",
			.parent_hws = (const struct clk_hw*[]){
				&csi0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi0rdi_clk = {
	.halt_reg = 0x4e050,
	.clkr = {
		.enable_reg = 0x4e050,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_csi0rdi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&csi0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi1_ahb_clk = {
	.halt_reg = 0x4f040,
	.clkr = {
		.enable_reg = 0x4f040,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_csi1_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&camss_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi1_clk = {
	.halt_reg = 0x4f03c,
	.clkr = {
		.enable_reg = 0x4f03c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_csi1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&csi1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi1phy_clk = {
	.halt_reg = 0x4f048,
	.clkr = {
		.enable_reg = 0x4f048,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_csi1phy_clk",
			.parent_hws = (const struct clk_hw*[]){
				&csi1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi1pix_clk = {
	.halt_reg = 0x4f058,
	.clkr = {
		.enable_reg = 0x4f058,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_csi1pix_clk",
			.parent_hws = (const struct clk_hw*[]){
				&csi1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi1rdi_clk = {
	.halt_reg = 0x4f050,
	.clkr = {
		.enable_reg = 0x4f050,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_csi1rdi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&csi1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi2_ahb_clk = {
	.halt_reg = 0x3c040,
	.clkr = {
		.enable_reg = 0x3c040,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_csi2_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&camss_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi2_clk = {
	.halt_reg = 0x3c03c,
	.clkr = {
		.enable_reg = 0x3c03c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_csi2_clk",
			.parent_hws = (const struct clk_hw*[]){
				&csi2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi2phy_clk = {
	.halt_reg = 0x3c048,
	.clkr = {
		.enable_reg = 0x3c048,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_csi2phy_clk",
			.parent_hws = (const struct clk_hw*[]){
				&csi2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi2pix_clk = {
	.halt_reg = 0x3c058,
	.clkr = {
		.enable_reg = 0x3c058,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_csi2pix_clk",
			.parent_hws = (const struct clk_hw*[]){
				&csi2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi2rdi_clk = {
	.halt_reg = 0x3c050,
	.clkr = {
		.enable_reg = 0x3c050,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_csi2rdi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&csi2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi_vfe0_clk = {
	.halt_reg = 0x58050,
	.clkr = {
		.enable_reg = 0x58050,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_csi_vfe0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&vfe0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_gp0_clk = {
	.halt_reg = 0x54018,
	.clkr = {
		.enable_reg = 0x54018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_gp0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&camss_gp0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_gp1_clk = {
	.halt_reg = 0x55018,
	.clkr = {
		.enable_reg = 0x55018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_gp1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&camss_gp1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_ispif_ahb_clk = {
	.halt_reg = 0x50004,
	.clkr = {
		.enable_reg = 0x50004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_ispif_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&camss_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_jpeg0_clk = {
	.halt_reg = 0x57020,
	.clkr = {
		.enable_reg = 0x57020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_jpeg0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&jpeg0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_jpeg_ahb_clk = {
	.halt_reg = 0x57024,
	.clkr = {
		.enable_reg = 0x57024,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_jpeg_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&camss_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_jpeg_axi_clk = {
	.halt_reg = 0x57028,
	.clkr = {
		.enable_reg = 0x57028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_jpeg_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&system_mm_noc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_mclk0_clk = {
	.halt_reg = 0x52018,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_mclk0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mclk0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_mclk1_clk = {
	.halt_reg = 0x53018,
	.clkr = {
		.enable_reg = 0x53018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_mclk1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mclk1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_micro_ahb_clk = {
	.halt_reg = 0x5600c,
	.clkr = {
		.enable_reg = 0x5600c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_micro_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&camss_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi0phytimer_clk = {
	.halt_reg = 0x4e01c,
	.clkr = {
		.enable_reg = 0x4e01c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_csi0phytimer_clk",
			.parent_hws = (const struct clk_hw*[]){
				&csi0phytimer_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi1phytimer_clk = {
	.halt_reg = 0x4f01c,
	.clkr = {
		.enable_reg = 0x4f01c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_csi1phytimer_clk",
			.parent_hws = (const struct clk_hw*[]){
				&csi1phytimer_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_ahb_clk = {
	.halt_reg = 0x5a014,
	.clkr = {
		.enable_reg = 0x5a014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&camss_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_top_ahb_clk = {
	.halt_reg = 0x56004,
	.clkr = {
		.enable_reg = 0x56004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_top_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_cpp_ahb_clk = {
	.halt_reg = 0x58040,
	.clkr = {
		.enable_reg = 0x58040,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_cpp_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&camss_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_cpp_clk = {
	.halt_reg = 0x5803c,
	.clkr = {
		.enable_reg = 0x5803c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_cpp_clk",
			.parent_hws = (const struct clk_hw*[]){
				&cpp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_vfe0_clk = {
	.halt_reg = 0x58038,
	.clkr = {
		.enable_reg = 0x58038,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_vfe0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&vfe0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_vfe_ahb_clk = {
	.halt_reg = 0x58044,
	.clkr = {
		.enable_reg = 0x58044,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_vfe_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&camss_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_vfe_axi_clk = {
	.halt_reg = 0x58048,
	.clkr = {
		.enable_reg = 0x58048,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_vfe_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&system_mm_noc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
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
			.parent_hws = (const struct clk_hw*[]){
				&pcnoc_bfdcd_clk_src.clkr.hw,
			},
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
			.parent_hws = (const struct clk_hw*[]){
				&pcnoc_bfdcd_clk_src.clkr.hw,
			},
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
			.parent_hws = (const struct clk_hw*[]){
				&crypto_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_oxili_gmem_clk = {
	.halt_reg = 0x59024,
	.clkr = {
		.enable_reg = 0x59024,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_oxili_gmem_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gfx3d_clk_src.clkr.hw,
			},
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
			.parent_hws = (const struct clk_hw*[]){
				&gp1_clk_src.clkr.hw,
			},
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
			.parent_hws = (const struct clk_hw*[]){
				&gp2_clk_src.clkr.hw,
			},
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
			.parent_hws = (const struct clk_hw*[]){
				&gp3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_ahb_clk = {
	.halt_reg = 0x4d07c,
	.clkr = {
		.enable_reg = 0x4d07c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mdss_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_axi_clk = {
	.halt_reg = 0x4d080,
	.clkr = {
		.enable_reg = 0x4d080,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mdss_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&system_mm_noc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_byte0_clk = {
	.halt_reg = 0x4d094,
	.clkr = {
		.enable_reg = 0x4d094,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mdss_byte0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&byte0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_byte1_clk = {
	.halt_reg = 0x4d0a0,
	.clkr = {
		.enable_reg = 0x4d0a0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mdss_byte1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&byte1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_esc0_clk = {
	.halt_reg = 0x4d098,
	.clkr = {
		.enable_reg = 0x4d098,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mdss_esc0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&esc0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_esc1_clk = {
	.halt_reg = 0x4d09c,
	.clkr = {
		.enable_reg = 0x4d09c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mdss_esc1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&esc1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_mdp_clk = {
	.halt_reg = 0x4D088,
	.clkr = {
		.enable_reg = 0x4D088,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mdss_mdp_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_pclk0_clk = {
	.halt_reg = 0x4d084,
	.clkr = {
		.enable_reg = 0x4d084,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mdss_pclk0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&pclk0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_pclk1_clk = {
	.halt_reg = 0x4d0a4,
	.clkr = {
		.enable_reg = 0x4d0a4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mdss_pclk1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&pclk1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_vsync_clk = {
	.halt_reg = 0x4d090,
	.clkr = {
		.enable_reg = 0x4d090,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mdss_vsync_clk",
			.parent_hws = (const struct clk_hw*[]){
				&vsync_clk_src.clkr.hw,
			},
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
			.parent_hws = (const struct clk_hw*[]){
				&pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mss_q6_bimc_axi_clk = {
	.halt_reg = 0x49004,
	.clkr = {
		.enable_reg = 0x49004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mss_q6_bimc_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&bimc_ddr_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_oxili_ahb_clk = {
	.halt_reg = 0x59028,
	.clkr = {
		.enable_reg = 0x59028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_oxili_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_oxili_gfx3d_clk = {
	.halt_reg = 0x59020,
	.clkr = {
		.enable_reg = 0x59020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_oxili_gfx3d_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gfx3d_clk_src.clkr.hw,
			},
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
			.parent_hws = (const struct clk_hw*[]){
				&pdm2_clk_src.clkr.hw,
			},
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
			.parent_hws = (const struct clk_hw*[]){
				&pcnoc_bfdcd_clk_src.clkr.hw,
			},
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
			.parent_hws = (const struct clk_hw*[]){
				&pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
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
			.parent_hws = (const struct clk_hw*[]){
				&pcnoc_bfdcd_clk_src.clkr.hw,
			},
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
			.parent_hws = (const struct clk_hw*[]){
				&sdcc1_apps_clk_src.clkr.hw,
			},
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
			.parent_hws = (const struct clk_hw*[]){
				&pcnoc_bfdcd_clk_src.clkr.hw,
			},
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
			.parent_hws = (const struct clk_hw*[]){
				&sdcc2_apps_clk_src.clkr.hw,
			},
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
			.parent_hws = (const struct clk_hw*[]){
				&bimc_ddr_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gfx_tcu_clk = {
	.halt_reg = 0x12020,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4500c,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gfx_tcu_clk",
			.parent_hws = (const struct clk_hw*[]){
				&bimc_ddr_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gfx_tbu_clk = {
	.halt_reg = 0x12010,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4500c,
		.enable_mask = BIT(3),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gfx_tbu_clk",
			.parent_hws = (const struct clk_hw*[]){
				&bimc_ddr_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdp_tbu_clk = {
	.halt_reg = 0x1201c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4500c,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mdp_tbu_clk",
			.parent_hws = (const struct clk_hw*[]){
				&system_mm_noc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_venus_tbu_clk = {
	.halt_reg = 0x12014,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4500c,
		.enable_mask = BIT(5),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_venus_tbu_clk",
			.parent_hws = (const struct clk_hw*[]){
				&system_mm_noc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_vfe_tbu_clk = {
	.halt_reg = 0x1203c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4500c,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_vfe_tbu_clk",
			.parent_hws = (const struct clk_hw*[]){
				&system_mm_noc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_jpeg_tbu_clk = {
	.halt_reg = 0x12034,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4500c,
		.enable_mask = BIT(10),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_jpeg_tbu_clk",
			.parent_hws = (const struct clk_hw*[]){
				&system_mm_noc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
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
			.parent_hws = (const struct clk_hw*[]){
				&pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gtcu_ahb_clk = {
	.halt_reg = 0x12044,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4500c,
		.enable_mask = BIT(13),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gtcu_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cpp_tbu_clk = {
	.halt_reg = 0x12040,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4500c,
		.enable_mask = BIT(14),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_cpp_tbu_clk",
			.parent_hws = (const struct clk_hw*[]){
				&pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdp_rt_tbu_clk = {
	.halt_reg = 0x1201c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4500c,
		.enable_mask = BIT(15),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mdp_rt_tbu_clk",
			.parent_hws = (const struct clk_hw*[]){
				&pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_bimc_gfx_clk = {
	.halt_reg = 0x31024,
	.clkr = {
		.enable_reg = 0x31024,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_bimc_gfx_clk",
			.parent_hws = (const struct clk_hw*[]){
				&bimc_gpu_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_bimc_gpu_clk = {
	.halt_reg = 0x31040,
	.clkr = {
		.enable_reg = 0x31040,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_bimc_gpu_clk",
			.parent_hws = (const struct clk_hw*[]){
				&bimc_gpu_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
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
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb_fs_ahb_clk = {
	.halt_reg = 0x3f008,
	.clkr = {
		.enable_reg = 0x3f008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb_fs_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb_fs_ic_clk = {
	.halt_reg = 0x3f030,
	.clkr = {
		.enable_reg = 0x3f030,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb_fs_ic_clk",
			.parent_hws = (const struct clk_hw*[]){
				&usb_fs_ic_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb_fs_system_clk = {
	.halt_reg = 0x3f004,
	.clkr = {
		.enable_reg = 0x3f004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb_fs_system_clk",
			.parent_hws = (const struct clk_hw*[]){
				&usb_fs_system_clk_src.clkr.hw,
			},
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
			.parent_hws = (const struct clk_hw*[]){
				&pcnoc_bfdcd_clk_src.clkr.hw,
			},
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
			.parent_hws = (const struct clk_hw*[]){
				&usb_hs_system_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_venus0_ahb_clk = {
	.halt_reg = 0x4c020,
	.clkr = {
		.enable_reg = 0x4c020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_venus0_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_venus0_axi_clk = {
	.halt_reg = 0x4c024,
	.clkr = {
		.enable_reg = 0x4c024,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_venus0_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&system_mm_noc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_venus0_vcodec0_clk = {
	.halt_reg = 0x4c01c,
	.clkr = {
		.enable_reg = 0x4c01c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_venus0_vcodec0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&vcodec0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_venus0_core0_vcodec0_clk = {
	.halt_reg = 0x4c02c,
	.clkr = {
		.enable_reg = 0x4c02c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_venus0_core0_vcodec0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&vcodec0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_venus0_core1_vcodec0_clk = {
	.halt_reg = 0x4c034,
	.clkr = {
		.enable_reg = 0x4c034,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_venus0_core1_vcodec0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&vcodec0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_oxili_timer_clk = {
	.halt_reg = 0x59040,
	.clkr = {
		.enable_reg = 0x59040,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_oxili_timer_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc venus_gdsc = {
	.gdscr = 0x4c018,
	.pd = {
		.name = "venus",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc mdss_gdsc = {
	.gdscr = 0x4d078,
	.pd = {
		.name = "mdss",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc jpeg_gdsc = {
	.gdscr = 0x5701c,
	.pd = {
		.name = "jpeg",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc vfe_gdsc = {
	.gdscr = 0x58034,
	.pd = {
		.name = "vfe",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc oxili_gdsc = {
	.gdscr = 0x5901c,
	.pd = {
		.name = "oxili",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc venus_core0_gdsc = {
	.gdscr = 0x4c028,
	.pd = {
		.name = "venus_core0",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc venus_core1_gdsc = {
	.gdscr = 0x4c030,
	.pd = {
		.name = "venus_core1",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct clk_regmap *gcc_msm8939_clocks[] = {
	[GPLL0] = &gpll0.clkr,
	[GPLL0_VOTE] = &gpll0_vote,
	[BIMC_PLL] = &bimc_pll.clkr,
	[BIMC_PLL_VOTE] = &bimc_pll_vote,
	[GPLL1] = &gpll1.clkr,
	[GPLL1_VOTE] = &gpll1_vote,
	[GPLL2] = &gpll2.clkr,
	[GPLL2_VOTE] = &gpll2_vote,
	[PCNOC_BFDCD_CLK_SRC] = &pcnoc_bfdcd_clk_src.clkr,
	[SYSTEM_NOC_BFDCD_CLK_SRC] = &system_noc_bfdcd_clk_src.clkr,
	[SYSTEM_MM_NOC_BFDCD_CLK_SRC] = &system_mm_noc_bfdcd_clk_src.clkr,
	[CAMSS_AHB_CLK_SRC] = &camss_ahb_clk_src.clkr,
	[APSS_AHB_CLK_SRC] = &apss_ahb_clk_src.clkr,
	[CSI0_CLK_SRC] = &csi0_clk_src.clkr,
	[CSI1_CLK_SRC] = &csi1_clk_src.clkr,
	[CSI2_CLK_SRC] = &csi2_clk_src.clkr,
	[GFX3D_CLK_SRC] = &gfx3d_clk_src.clkr,
	[VFE0_CLK_SRC] = &vfe0_clk_src.clkr,
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
	[CCI_CLK_SRC] = &cci_clk_src.clkr,
	[CAMSS_GP0_CLK_SRC] = &camss_gp0_clk_src.clkr,
	[CAMSS_GP1_CLK_SRC] = &camss_gp1_clk_src.clkr,
	[JPEG0_CLK_SRC] = &jpeg0_clk_src.clkr,
	[MCLK0_CLK_SRC] = &mclk0_clk_src.clkr,
	[MCLK1_CLK_SRC] = &mclk1_clk_src.clkr,
	[CSI0PHYTIMER_CLK_SRC] = &csi0phytimer_clk_src.clkr,
	[CSI1PHYTIMER_CLK_SRC] = &csi1phytimer_clk_src.clkr,
	[CPP_CLK_SRC] = &cpp_clk_src.clkr,
	[CRYPTO_CLK_SRC] = &crypto_clk_src.clkr,
	[GP1_CLK_SRC] = &gp1_clk_src.clkr,
	[GP2_CLK_SRC] = &gp2_clk_src.clkr,
	[GP3_CLK_SRC] = &gp3_clk_src.clkr,
	[BYTE0_CLK_SRC] = &byte0_clk_src.clkr,
	[ESC0_CLK_SRC] = &esc0_clk_src.clkr,
	[MDP_CLK_SRC] = &mdp_clk_src.clkr,
	[PCLK0_CLK_SRC] = &pclk0_clk_src.clkr,
	[VSYNC_CLK_SRC] = &vsync_clk_src.clkr,
	[PDM2_CLK_SRC] = &pdm2_clk_src.clkr,
	[SDCC1_APPS_CLK_SRC] = &sdcc1_apps_clk_src.clkr,
	[SDCC2_APPS_CLK_SRC] = &sdcc2_apps_clk_src.clkr,
	[APSS_TCU_CLK_SRC] = &apss_tcu_clk_src.clkr,
	[USB_HS_SYSTEM_CLK_SRC] = &usb_hs_system_clk_src.clkr,
	[VCODEC0_CLK_SRC] = &vcodec0_clk_src.clkr,
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
	[GCC_BOOT_ROM_AHB_CLK] = &gcc_boot_rom_ahb_clk.clkr,
	[GCC_CAMSS_CCI_AHB_CLK] = &gcc_camss_cci_ahb_clk.clkr,
	[GCC_CAMSS_CCI_CLK] = &gcc_camss_cci_clk.clkr,
	[GCC_CAMSS_CSI0_AHB_CLK] = &gcc_camss_csi0_ahb_clk.clkr,
	[GCC_CAMSS_CSI0_CLK] = &gcc_camss_csi0_clk.clkr,
	[GCC_CAMSS_CSI0PHY_CLK] = &gcc_camss_csi0phy_clk.clkr,
	[GCC_CAMSS_CSI0PIX_CLK] = &gcc_camss_csi0pix_clk.clkr,
	[GCC_CAMSS_CSI0RDI_CLK] = &gcc_camss_csi0rdi_clk.clkr,
	[GCC_CAMSS_CSI1_AHB_CLK] = &gcc_camss_csi1_ahb_clk.clkr,
	[GCC_CAMSS_CSI1_CLK] = &gcc_camss_csi1_clk.clkr,
	[GCC_CAMSS_CSI1PHY_CLK] = &gcc_camss_csi1phy_clk.clkr,
	[GCC_CAMSS_CSI1PIX_CLK] = &gcc_camss_csi1pix_clk.clkr,
	[GCC_CAMSS_CSI1RDI_CLK] = &gcc_camss_csi1rdi_clk.clkr,
	[GCC_CAMSS_CSI2_AHB_CLK] = &gcc_camss_csi2_ahb_clk.clkr,
	[GCC_CAMSS_CSI2_CLK] = &gcc_camss_csi2_clk.clkr,
	[GCC_CAMSS_CSI2PHY_CLK] = &gcc_camss_csi2phy_clk.clkr,
	[GCC_CAMSS_CSI2PIX_CLK] = &gcc_camss_csi2pix_clk.clkr,
	[GCC_CAMSS_CSI2RDI_CLK] = &gcc_camss_csi2rdi_clk.clkr,
	[GCC_CAMSS_CSI_VFE0_CLK] = &gcc_camss_csi_vfe0_clk.clkr,
	[GCC_CAMSS_GP0_CLK] = &gcc_camss_gp0_clk.clkr,
	[GCC_CAMSS_GP1_CLK] = &gcc_camss_gp1_clk.clkr,
	[GCC_CAMSS_ISPIF_AHB_CLK] = &gcc_camss_ispif_ahb_clk.clkr,
	[GCC_CAMSS_JPEG0_CLK] = &gcc_camss_jpeg0_clk.clkr,
	[GCC_CAMSS_JPEG_AHB_CLK] = &gcc_camss_jpeg_ahb_clk.clkr,
	[GCC_CAMSS_JPEG_AXI_CLK] = &gcc_camss_jpeg_axi_clk.clkr,
	[GCC_CAMSS_MCLK0_CLK] = &gcc_camss_mclk0_clk.clkr,
	[GCC_CAMSS_MCLK1_CLK] = &gcc_camss_mclk1_clk.clkr,
	[GCC_CAMSS_MICRO_AHB_CLK] = &gcc_camss_micro_ahb_clk.clkr,
	[GCC_CAMSS_CSI0PHYTIMER_CLK] = &gcc_camss_csi0phytimer_clk.clkr,
	[GCC_CAMSS_CSI1PHYTIMER_CLK] = &gcc_camss_csi1phytimer_clk.clkr,
	[GCC_CAMSS_AHB_CLK] = &gcc_camss_ahb_clk.clkr,
	[GCC_CAMSS_TOP_AHB_CLK] = &gcc_camss_top_ahb_clk.clkr,
	[GCC_CAMSS_CPP_AHB_CLK] = &gcc_camss_cpp_ahb_clk.clkr,
	[GCC_CAMSS_CPP_CLK] = &gcc_camss_cpp_clk.clkr,
	[GCC_CAMSS_VFE0_CLK] = &gcc_camss_vfe0_clk.clkr,
	[GCC_CAMSS_VFE_AHB_CLK] = &gcc_camss_vfe_ahb_clk.clkr,
	[GCC_CAMSS_VFE_AXI_CLK] = &gcc_camss_vfe_axi_clk.clkr,
	[GCC_CRYPTO_AHB_CLK] = &gcc_crypto_ahb_clk.clkr,
	[GCC_CRYPTO_AXI_CLK] = &gcc_crypto_axi_clk.clkr,
	[GCC_CRYPTO_CLK] = &gcc_crypto_clk.clkr,
	[GCC_OXILI_GMEM_CLK] = &gcc_oxili_gmem_clk.clkr,
	[GCC_GP1_CLK] = &gcc_gp1_clk.clkr,
	[GCC_GP2_CLK] = &gcc_gp2_clk.clkr,
	[GCC_GP3_CLK] = &gcc_gp3_clk.clkr,
	[GCC_MDSS_AHB_CLK] = &gcc_mdss_ahb_clk.clkr,
	[GCC_MDSS_AXI_CLK] = &gcc_mdss_axi_clk.clkr,
	[GCC_MDSS_BYTE0_CLK] = &gcc_mdss_byte0_clk.clkr,
	[GCC_MDSS_ESC0_CLK] = &gcc_mdss_esc0_clk.clkr,
	[GCC_MDSS_MDP_CLK] = &gcc_mdss_mdp_clk.clkr,
	[GCC_MDSS_PCLK0_CLK] = &gcc_mdss_pclk0_clk.clkr,
	[GCC_MDSS_VSYNC_CLK] = &gcc_mdss_vsync_clk.clkr,
	[GCC_MSS_CFG_AHB_CLK] = &gcc_mss_cfg_ahb_clk.clkr,
	[GCC_OXILI_AHB_CLK] = &gcc_oxili_ahb_clk.clkr,
	[GCC_OXILI_GFX3D_CLK] = &gcc_oxili_gfx3d_clk.clkr,
	[GCC_PDM2_CLK] = &gcc_pdm2_clk.clkr,
	[GCC_PDM_AHB_CLK] = &gcc_pdm_ahb_clk.clkr,
	[GCC_PRNG_AHB_CLK] = &gcc_prng_ahb_clk.clkr,
	[GCC_SDCC1_AHB_CLK] = &gcc_sdcc1_ahb_clk.clkr,
	[GCC_SDCC1_APPS_CLK] = &gcc_sdcc1_apps_clk.clkr,
	[GCC_SDCC2_AHB_CLK] = &gcc_sdcc2_ahb_clk.clkr,
	[GCC_SDCC2_APPS_CLK] = &gcc_sdcc2_apps_clk.clkr,
	[GCC_GTCU_AHB_CLK] = &gcc_gtcu_ahb_clk.clkr,
	[GCC_JPEG_TBU_CLK] = &gcc_jpeg_tbu_clk.clkr,
	[GCC_MDP_TBU_CLK] = &gcc_mdp_tbu_clk.clkr,
	[GCC_SMMU_CFG_CLK] = &gcc_smmu_cfg_clk.clkr,
	[GCC_VENUS_TBU_CLK] = &gcc_venus_tbu_clk.clkr,
	[GCC_VFE_TBU_CLK] = &gcc_vfe_tbu_clk.clkr,
	[GCC_USB2A_PHY_SLEEP_CLK] = &gcc_usb2a_phy_sleep_clk.clkr,
	[GCC_USB_HS_AHB_CLK] = &gcc_usb_hs_ahb_clk.clkr,
	[GCC_USB_HS_SYSTEM_CLK] = &gcc_usb_hs_system_clk.clkr,
	[GCC_VENUS0_AHB_CLK] = &gcc_venus0_ahb_clk.clkr,
	[GCC_VENUS0_AXI_CLK] = &gcc_venus0_axi_clk.clkr,
	[GCC_VENUS0_VCODEC0_CLK] = &gcc_venus0_vcodec0_clk.clkr,
	[BIMC_DDR_CLK_SRC] = &bimc_ddr_clk_src.clkr,
	[GCC_APSS_TCU_CLK] = &gcc_apss_tcu_clk.clkr,
	[GCC_GFX_TCU_CLK] = &gcc_gfx_tcu_clk.clkr,
	[BIMC_GPU_CLK_SRC] = &bimc_gpu_clk_src.clkr,
	[GCC_BIMC_GFX_CLK] = &gcc_bimc_gfx_clk.clkr,
	[GCC_BIMC_GPU_CLK] = &gcc_bimc_gpu_clk.clkr,
	[ULTAUDIO_AHBFABRIC_CLK_SRC] = &ultaudio_ahbfabric_clk_src.clkr,
	[ULTAUDIO_LPAIF_PRI_I2S_CLK_SRC] = &ultaudio_lpaif_pri_i2s_clk_src.clkr,
	[ULTAUDIO_LPAIF_SEC_I2S_CLK_SRC] = &ultaudio_lpaif_sec_i2s_clk_src.clkr,
	[ULTAUDIO_LPAIF_AUX_I2S_CLK_SRC] = &ultaudio_lpaif_aux_i2s_clk_src.clkr,
	[ULTAUDIO_XO_CLK_SRC] = &ultaudio_xo_clk_src.clkr,
	[CODEC_DIGCODEC_CLK_SRC] = &codec_digcodec_clk_src.clkr,
	[GCC_ULTAUDIO_PCNOC_MPORT_CLK] = &gcc_ultaudio_pcnoc_mport_clk.clkr,
	[GCC_ULTAUDIO_PCNOC_SWAY_CLK] = &gcc_ultaudio_pcnoc_sway_clk.clkr,
	[GCC_ULTAUDIO_AVSYNC_XO_CLK] = &gcc_ultaudio_avsync_xo_clk.clkr,
	[GCC_ULTAUDIO_STC_XO_CLK] = &gcc_ultaudio_stc_xo_clk.clkr,
	[GCC_ULTAUDIO_AHBFABRIC_IXFABRIC_CLK] = &gcc_ultaudio_ahbfabric_ixfabric_clk.clkr,
	[GCC_ULTAUDIO_AHBFABRIC_IXFABRIC_LPM_CLK] = &gcc_ultaudio_ahbfabric_ixfabric_lpm_clk.clkr,
	[GCC_ULTAUDIO_LPAIF_PRI_I2S_CLK] = &gcc_ultaudio_lpaif_pri_i2s_clk.clkr,
	[GCC_ULTAUDIO_LPAIF_SEC_I2S_CLK] = &gcc_ultaudio_lpaif_sec_i2s_clk.clkr,
	[GCC_ULTAUDIO_LPAIF_AUX_I2S_CLK] = &gcc_ultaudio_lpaif_aux_i2s_clk.clkr,
	[GCC_CODEC_DIGCODEC_CLK] = &gcc_codec_digcodec_clk.clkr,
	[GCC_MSS_Q6_BIMC_AXI_CLK] = &gcc_mss_q6_bimc_axi_clk.clkr,
	[GPLL3] = &gpll3.clkr,
	[GPLL3_VOTE] = &gpll3_vote,
	[GPLL4] = &gpll4.clkr,
	[GPLL4_VOTE] = &gpll4_vote,
	[GPLL5] = &gpll5.clkr,
	[GPLL5_VOTE] = &gpll5_vote,
	[GPLL6] = &gpll6.clkr,
	[GPLL6_VOTE] = &gpll6_vote,
	[BYTE1_CLK_SRC] = &byte1_clk_src.clkr,
	[GCC_MDSS_BYTE1_CLK] = &gcc_mdss_byte1_clk.clkr,
	[ESC1_CLK_SRC] = &esc1_clk_src.clkr,
	[GCC_MDSS_ESC1_CLK] = &gcc_mdss_esc1_clk.clkr,
	[PCLK1_CLK_SRC] = &pclk1_clk_src.clkr,
	[GCC_MDSS_PCLK1_CLK] = &gcc_mdss_pclk1_clk.clkr,
	[GCC_GFX_TBU_CLK] = &gcc_gfx_tbu_clk.clkr,
	[GCC_CPP_TBU_CLK] = &gcc_cpp_tbu_clk.clkr,
	[GCC_MDP_RT_TBU_CLK] = &gcc_mdp_rt_tbu_clk.clkr,
	[USB_FS_SYSTEM_CLK_SRC] = &usb_fs_system_clk_src.clkr,
	[USB_FS_IC_CLK_SRC] = &usb_fs_ic_clk_src.clkr,
	[GCC_USB_FS_AHB_CLK] = &gcc_usb_fs_ahb_clk.clkr,
	[GCC_USB_FS_IC_CLK] = &gcc_usb_fs_ic_clk.clkr,
	[GCC_USB_FS_SYSTEM_CLK] = &gcc_usb_fs_system_clk.clkr,
	[GCC_VENUS0_CORE0_VCODEC0_CLK] = &gcc_venus0_core0_vcodec0_clk.clkr,
	[GCC_VENUS0_CORE1_VCODEC0_CLK] = &gcc_venus0_core1_vcodec0_clk.clkr,
	[GCC_OXILI_TIMER_CLK] = &gcc_oxili_timer_clk.clkr,
};

static struct gdsc *gcc_msm8939_gdscs[] = {
	[VENUS_GDSC] = &venus_gdsc,
	[MDSS_GDSC] = &mdss_gdsc,
	[JPEG_GDSC] = &jpeg_gdsc,
	[VFE_GDSC] = &vfe_gdsc,
	[OXILI_GDSC] = &oxili_gdsc,
	[VENUS_CORE0_GDSC] = &venus_core0_gdsc,
	[VENUS_CORE1_GDSC] = &venus_core1_gdsc,
};

static const struct qcom_reset_map gcc_msm8939_resets[] = {
	[GCC_BLSP1_BCR] = { 0x01000 },
	[GCC_BLSP1_QUP1_BCR] = { 0x02000 },
	[GCC_BLSP1_UART1_BCR] = { 0x02038 },
	[GCC_BLSP1_QUP2_BCR] = { 0x03008 },
	[GCC_BLSP1_UART2_BCR] = { 0x03028 },
	[GCC_BLSP1_QUP3_BCR] = { 0x04018 },
	[GCC_BLSP1_UART3_BCR] = { 0x04038 },
	[GCC_BLSP1_QUP4_BCR] = { 0x05018 },
	[GCC_BLSP1_QUP5_BCR] = { 0x06018 },
	[GCC_BLSP1_QUP6_BCR] = { 0x07018 },
	[GCC_IMEM_BCR] = { 0x0e000 },
	[GCC_SMMU_BCR] = { 0x12000 },
	[GCC_APSS_TCU_BCR] = { 0x12050 },
	[GCC_SMMU_XPU_BCR] = { 0x12054 },
	[GCC_PCNOC_TBU_BCR] = { 0x12058 },
	[GCC_PRNG_BCR] = { 0x13000 },
	[GCC_BOOT_ROM_BCR] = { 0x13008 },
	[GCC_CRYPTO_BCR] = { 0x16000 },
	[GCC_SEC_CTRL_BCR] = { 0x1a000 },
	[GCC_AUDIO_CORE_BCR] = { 0x1c008 },
	[GCC_ULT_AUDIO_BCR] = { 0x1c0b4 },
	[GCC_DEHR_BCR] = { 0x1f000 },
	[GCC_SYSTEM_NOC_BCR] = { 0x26000 },
	[GCC_PCNOC_BCR] = { 0x27018 },
	[GCC_TCSR_BCR] = { 0x28000 },
	[GCC_QDSS_BCR] = { 0x29000 },
	[GCC_DCD_BCR] = { 0x2a000 },
	[GCC_MSG_RAM_BCR] = { 0x2b000 },
	[GCC_MPM_BCR] = { 0x2c000 },
	[GCC_SPMI_BCR] = { 0x2e000 },
	[GCC_SPDM_BCR] = { 0x2f000 },
	[GCC_MM_SPDM_BCR] = { 0x2f024 },
	[GCC_BIMC_BCR] = { 0x31000 },
	[GCC_RBCPR_BCR] = { 0x33000 },
	[GCC_TLMM_BCR] = { 0x34000 },
	[GCC_CAMSS_CSI2_BCR] = { 0x3c038 },
	[GCC_CAMSS_CSI2PHY_BCR] = { 0x3c044 },
	[GCC_CAMSS_CSI2RDI_BCR] = { 0x3c04c },
	[GCC_CAMSS_CSI2PIX_BCR] = { 0x3c054 },
	[GCC_USB_FS_BCR] = { 0x3f000 },
	[GCC_USB_HS_BCR] = { 0x41000 },
	[GCC_USB2A_PHY_BCR] = { 0x41028 },
	[GCC_SDCC1_BCR] = { 0x42000 },
	[GCC_SDCC2_BCR] = { 0x43000 },
	[GCC_PDM_BCR] = { 0x44000 },
	[GCC_SNOC_BUS_TIMEOUT0_BCR] = { 0x47000 },
	[GCC_PCNOC_BUS_TIMEOUT0_BCR] = { 0x48000 },
	[GCC_PCNOC_BUS_TIMEOUT1_BCR] = { 0x48008 },
	[GCC_PCNOC_BUS_TIMEOUT2_BCR] = { 0x48010 },
	[GCC_PCNOC_BUS_TIMEOUT3_BCR] = { 0x48018 },
	[GCC_PCNOC_BUS_TIMEOUT4_BCR] = { 0x48020 },
	[GCC_PCNOC_BUS_TIMEOUT5_BCR] = { 0x48028 },
	[GCC_PCNOC_BUS_TIMEOUT6_BCR] = { 0x48030 },
	[GCC_PCNOC_BUS_TIMEOUT7_BCR] = { 0x48038 },
	[GCC_PCNOC_BUS_TIMEOUT8_BCR] = { 0x48040 },
	[GCC_PCNOC_BUS_TIMEOUT9_BCR] = { 0x48048 },
	[GCC_MMSS_BCR] = { 0x4b000 },
	[GCC_VENUS0_BCR] = { 0x4c014 },
	[GCC_MDSS_BCR] = { 0x4d074 },
	[GCC_CAMSS_PHY0_BCR] = { 0x4e018 },
	[GCC_CAMSS_CSI0_BCR] = { 0x4e038 },
	[GCC_CAMSS_CSI0PHY_BCR] = { 0x4e044 },
	[GCC_CAMSS_CSI0RDI_BCR] = { 0x4e04c },
	[GCC_CAMSS_CSI0PIX_BCR] = { 0x4e054 },
	[GCC_CAMSS_PHY1_BCR] = { 0x4f018 },
	[GCC_CAMSS_CSI1_BCR] = { 0x4f038 },
	[GCC_CAMSS_CSI1PHY_BCR] = { 0x4f044 },
	[GCC_CAMSS_CSI1RDI_BCR] = { 0x4f04c },
	[GCC_CAMSS_CSI1PIX_BCR] = { 0x4f054 },
	[GCC_CAMSS_ISPIF_BCR] = { 0x50000 },
	[GCC_BLSP1_QUP4_SPI_APPS_CBCR] = { 0x0501c },
	[GCC_CAMSS_CCI_BCR] = { 0x51014 },
	[GCC_CAMSS_MCLK0_BCR] = { 0x52014 },
	[GCC_CAMSS_MCLK1_BCR] = { 0x53014 },
	[GCC_CAMSS_GP0_BCR] = { 0x54014 },
	[GCC_CAMSS_GP1_BCR] = { 0x55014 },
	[GCC_CAMSS_TOP_BCR] = { 0x56000 },
	[GCC_CAMSS_MICRO_BCR] = { 0x56008 },
	[GCC_CAMSS_JPEG_BCR] = { 0x57018 },
	[GCC_CAMSS_VFE_BCR] = { 0x58030 },
	[GCC_CAMSS_CSI_VFE0_BCR] = { 0x5804c },
	[GCC_OXILI_BCR] = { 0x59018 },
	[GCC_GMEM_BCR] = { 0x5902c },
	[GCC_CAMSS_AHB_BCR] = { 0x5a018 },
	[GCC_CAMSS_MCLK2_BCR] = { 0x5c014 },
	[GCC_MDP_TBU_BCR] = { 0x62000 },
	[GCC_GFX_TBU_BCR] = { 0x63000 },
	[GCC_GFX_TCU_BCR] = { 0x64000 },
	[GCC_MSS_TBU_AXI_BCR] = { 0x65000 },
	[GCC_MSS_TBU_GSS_AXI_BCR] = { 0x66000 },
	[GCC_MSS_TBU_Q6_AXI_BCR] = { 0x67000 },
	[GCC_GTCU_AHB_BCR] = { 0x68000 },
	[GCC_SMMU_CFG_BCR] = { 0x69000 },
	[GCC_VFE_TBU_BCR] = { 0x6a000 },
	[GCC_VENUS_TBU_BCR] = { 0x6b000 },
	[GCC_JPEG_TBU_BCR] = { 0x6c000 },
	[GCC_PRONTO_TBU_BCR] = { 0x6d000 },
	[GCC_CPP_TBU_BCR] = { 0x6e000 },
	[GCC_MDP_RT_TBU_BCR] = { 0x6f000 },
	[GCC_SMMU_CATS_BCR] = { 0x7c000 },
};

static const struct regmap_config gcc_msm8939_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x80000,
	.fast_io	= true,
};

static const struct qcom_cc_desc gcc_msm8939_desc = {
	.config = &gcc_msm8939_regmap_config,
	.clks = gcc_msm8939_clocks,
	.num_clks = ARRAY_SIZE(gcc_msm8939_clocks),
	.resets = gcc_msm8939_resets,
	.num_resets = ARRAY_SIZE(gcc_msm8939_resets),
	.gdscs = gcc_msm8939_gdscs,
	.num_gdscs = ARRAY_SIZE(gcc_msm8939_gdscs),
};

static const struct of_device_id gcc_msm8939_match_table[] = {
	{ .compatible = "qcom,gcc-msm8939" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_msm8939_match_table);

static int gcc_msm8939_probe(struct platform_device *pdev)
{
	struct regmap *regmap;

	regmap = qcom_cc_map(pdev, &gcc_msm8939_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	clk_pll_configure_sr_hpm_lp(&gpll3, regmap, &gpll3_config, true);
	clk_pll_configure_sr_hpm_lp(&gpll4, regmap, &gpll4_config, true);

	return qcom_cc_really_probe(&pdev->dev, &gcc_msm8939_desc, regmap);
}

static struct platform_driver gcc_msm8939_driver = {
	.probe		= gcc_msm8939_probe,
	.driver		= {
		.name	= "gcc-msm8939",
		.of_match_table = gcc_msm8939_match_table,
	},
};

static int __init gcc_msm8939_init(void)
{
	return platform_driver_register(&gcc_msm8939_driver);
}
core_initcall(gcc_msm8939_init);

static void __exit gcc_msm8939_exit(void)
{
	platform_driver_unregister(&gcc_msm8939_driver);
}
module_exit(gcc_msm8939_exit);

MODULE_DESCRIPTION("Qualcomm GCC MSM8939 Driver");
MODULE_LICENSE("GPL v2");
