// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,ipq5210-gcc.h>
#include <dt-bindings/reset/qcom,ipq5210-gcc.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "clk-regmap-phy-mux.h"
#include "reset.h"

enum {
	DT_XO,
	DT_SLEEP_CLK,
	DT_PCIE30_PHY0_PIPE_CLK,
	DT_PCIE30_PHY1_PIPE_CLK,
	DT_USB3_PHY0_CC_PIPE_CLK,
	DT_NSS_CMN_CLK,
};

enum {
	P_GCC_GPLL0_OUT_MAIN_DIV_CLK_SRC,
	P_GPLL0_OUT_AUX,
	P_GPLL0_OUT_MAIN,
	P_GPLL2_OUT_AUX,
	P_GPLL2_OUT_MAIN,
	P_GPLL4_OUT_AUX,
	P_GPLL4_OUT_MAIN,
	P_NSS_CMN_CLK,
	P_SLEEP_CLK,
	P_USB3PHY_0_PIPE,
	P_XO,
};

static const struct clk_parent_data gcc_parent_data_xo = { .index = DT_XO };

static struct clk_alpha_pll gpll0_main = {
	.offset = 0x20000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr = {
		.enable_reg = 0xb000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpll0_main",
			.parent_data = &gcc_parent_data_xo,
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct clk_fixed_factor gpll0_div2 = {
	.mult = 1,
	.div = 2,
	.hw.init = &(const struct clk_init_data) {
		.name = "gpll0_div2",
		.parent_hws = (const struct clk_hw *[]) {
			&gpll0_main.clkr.hw
		},
		.num_parents = 1,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct clk_alpha_pll_postdiv gpll0 = {
	.offset = 0x20000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpll0",
		.parent_hws = (const struct clk_hw *[]) {
			&gpll0_main.clkr.hw
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ro_ops,
	},
};

static struct clk_alpha_pll gpll2_main = {
	.offset = 0x21000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr = {
		.enable_reg = 0xb000,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpll2_main",
			.parent_data = &gcc_parent_data_xo,
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static const struct clk_div_table post_div_table_gpll2[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll2 = {
	.offset = 0x21000,
	.post_div_table = post_div_table_gpll2,
	.num_post_div = ARRAY_SIZE(post_div_table_gpll2),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpll2",
		.parent_hws = (const struct clk_hw*[]) {
			&gpll2_main.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ro_ops,
	},
};

static struct clk_alpha_pll gpll4_main = {
	.offset = 0x22000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr = {
		.enable_reg = 0xb000,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpll4_main",
			.parent_data = &gcc_parent_data_xo,
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
			/*
			 * There are no consumers for this GPLL in kernel yet,
			 * (will be added soon), so the clock framework
			 * disables this source. But some of the clocks
			 * initialized by boot loaders uses this source. So we
			 * need to keep this clock ON. Add the
			 * CLK_IGNORE_UNUSED flag so the clock will not be
			 * disabled. Once the consumer in kernel is added, we
			 * can get rid of this flag.
			 */
			.flags = CLK_IS_CRITICAL,
		},
	},
};
static const struct parent_map gcc_parent_map_xo[] = {
	{ P_XO, 0 },
};

static const struct parent_map gcc_parent_map_0[] = {
	{ P_XO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL0_OUT_MAIN_DIV_CLK_SRC, 4 },
};

static const struct clk_parent_data gcc_parent_data_0[] = {
	{ .index = DT_XO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_div2.hw },
};

static const struct parent_map gcc_parent_map_1[] = {
	{ P_XO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
};

static const struct clk_parent_data gcc_parent_data_1[] = {
	{ .index = DT_XO },
	{ .hw = &gpll0.clkr.hw },
};

static const struct parent_map gcc_parent_map_2[] = {
	{ P_XO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL4_OUT_MAIN, 2 },
};

static const struct clk_parent_data gcc_parent_data_2[] = {
	{ .index = DT_XO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll4_main.clkr.hw },
};

static const struct parent_map gcc_parent_map_3[] = {
	{ P_XO, 0 },
};

static const struct clk_parent_data gcc_parent_data_3[] = {
	{ .index = DT_XO },
};

static const struct parent_map gcc_parent_map_4[] = {
	{ P_XO, 0 },
	{ P_NSS_CMN_CLK, 1 },
	{ P_GPLL0_OUT_AUX, 2 },
	{ P_GPLL2_OUT_AUX, 3 },
};

static const struct clk_parent_data gcc_parent_data_4[] = {
	{ .index = DT_XO },
	{ .index = DT_NSS_CMN_CLK },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll2_main.clkr.hw },
};

static const struct parent_map gcc_parent_map_5[] = {
	{ P_XO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL0_OUT_AUX, 2 },
	{ P_SLEEP_CLK, 6 },
};

static const struct clk_parent_data gcc_parent_data_5[] = {
	{ .index = DT_XO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0.clkr.hw },
	{ .index = DT_SLEEP_CLK },
};

static const struct parent_map gcc_parent_map_6[] = {
	{ P_XO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL2_OUT_MAIN, 2 },
	{ P_GCC_GPLL0_OUT_MAIN_DIV_CLK_SRC, 4 },
};

static const struct clk_parent_data gcc_parent_data_6[] = {
	{ .index = DT_XO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll2.clkr.hw },
	{ .hw = &gpll0_div2.hw },
};

static const struct parent_map gcc_parent_map_7[] = {
	{ P_XO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL4_OUT_MAIN, 2 },
	{ P_GCC_GPLL0_OUT_MAIN_DIV_CLK_SRC, 4 },
};

static const struct clk_parent_data gcc_parent_data_7[] = {
	{ .index = DT_XO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll4_main.clkr.hw },
	{ .hw = &gpll0_div2.hw },
};

static const struct parent_map gcc_parent_map_8[] = {
	{ P_XO, 0 },
	{ P_GPLL0_OUT_AUX, 2 },
	{ P_SLEEP_CLK, 6 },
};

static const struct clk_parent_data gcc_parent_data_8[] = {
	{ .index = DT_XO },
	{ .hw = &gpll0.clkr.hw },
	{ .index = DT_SLEEP_CLK },
};

static const struct parent_map gcc_parent_map_9[] = {
	{ P_XO, 0 },
	{ P_GPLL4_OUT_AUX, 1 },
	{ P_GPLL0_OUT_MAIN, 3 },
	{ P_GCC_GPLL0_OUT_MAIN_DIV_CLK_SRC, 4 },
};

static const struct clk_parent_data gcc_parent_data_9[] = {
	{ .index = DT_XO },
	{ .hw = &gpll4_main.clkr.hw },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_div2.hw },
};

static const struct parent_map gcc_parent_map_10[] = {
	{ P_XO, 0 },
	{ P_GPLL4_OUT_MAIN, 1 },
	{ P_GPLL0_OUT_AUX, 2 },
	{ P_GCC_GPLL0_OUT_MAIN_DIV_CLK_SRC, 4 },
};

static const struct clk_parent_data gcc_parent_data_10[] = {
	{ .index = DT_XO },
	{ .hw = &gpll4_main.clkr.hw },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_div2.hw },
};

static const struct parent_map gcc_parent_map_11[] = {
	{ P_XO, 0 },
	{ P_GPLL4_OUT_MAIN, 1 },
	{ P_GPLL0_OUT_AUX, 2 },
	{ P_GCC_GPLL0_OUT_MAIN_DIV_CLK_SRC, 4 },
};

static const struct clk_parent_data gcc_parent_data_11[] = {
	{ .index = DT_XO },
	{ .hw = &gpll4_main.clkr.hw },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_div2.hw },
};

static const struct parent_map gcc_parent_map_12[] = {
	{ P_XO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL2_OUT_AUX, 2 },
};

static const struct clk_parent_data gcc_parent_data_12[] = {
	{ .index = DT_XO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll2_main.clkr.hw },
};

static const struct parent_map gcc_parent_map_13[] = {
	{ P_SLEEP_CLK, 6 },
};

static const struct clk_parent_data gcc_parent_data_13[] = {
	{ .index = DT_SLEEP_CLK },
};

static const struct freq_tbl ftbl_gcc_adss_pwm_clk_src[] = {
	F(24000000, P_XO, 1, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 8, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_adss_pwm_clk_src = {
	.cmd_rcgr = 0x1c004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_adss_pwm_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_adss_pwm_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_nss_ts_clk_src[] = {
	F(24000000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_nss_ts_clk_src = {
	.cmd_rcgr = 0x17088,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_nss_ts_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_nss_ts_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_system_noc_bfdcd_clk_src[] = {
	F(24000000, P_XO, 1, 0, 0),
	F(133333333, P_GPLL0_OUT_MAIN, 6, 0, 0),
	F(200000000, P_GPLL0_OUT_MAIN, 4, 0, 0),
	F(266666667, P_GPLL4_OUT_MAIN, 4.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_system_noc_bfdcd_clk_src = {
	.cmd_rcgr = 0x2e004,
	.freq_tbl = ftbl_gcc_system_noc_bfdcd_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_7,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_system_noc_bfdcd_clk_src",
		.parent_data = gcc_parent_data_7,
		.num_parents = ARRAY_SIZE(gcc_parent_data_7),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_nssnoc_memnoc_bfdcd_clk_src[] = {
	F(429000000, P_NSS_CMN_CLK, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_nssnoc_memnoc_bfdcd_clk_src = {
	.cmd_rcgr = 0x17004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_nssnoc_memnoc_bfdcd_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_nssnoc_memnoc_bfdcd_clk_src",
		.parent_data = gcc_parent_data_4,
		.num_parents = ARRAY_SIZE(gcc_parent_data_4),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcie0_axi_m_clk_src[] = {
	F(200000000, P_GPLL4_OUT_MAIN, 6, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie0_axi_m_clk_src = {
	.cmd_rcgr = 0x28018,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie0_axi_m_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie0_axi_m_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_pcie0_axi_s_clk_src = {
	.cmd_rcgr = 0x28020,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie0_axi_m_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie0_axi_s_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_pcie0_rchng_clk_src = {
	.cmd_rcgr = 0x28028,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_adss_pwm_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie0_rchng_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcie1_axi_m_clk_src[] = {
	F(266666667, P_GPLL4_OUT_MAIN, 4.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie1_axi_m_clk_src = {
	.cmd_rcgr = 0x29018,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie1_axi_m_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie1_axi_m_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_pcie1_axi_s_clk_src = {
	.cmd_rcgr = 0x29020,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie0_axi_m_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie1_axi_s_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_pcie1_rchng_clk_src = {
	.cmd_rcgr = 0x29028,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_adss_pwm_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie1_rchng_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcie_aux_clk_src[] = {
	F(20000000, P_GPLL0_OUT_MAIN, 10, 1, 4),
	{ }
};

static struct clk_rcg2 gcc_pcie_aux_clk_src = {
	.cmd_rcgr = 0x28004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_pcie_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_aux_clk_src",
		.parent_data = gcc_parent_data_5,
		.num_parents = ARRAY_SIZE(gcc_parent_data_5),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap_se0_clk_src[] = {
	F(960000, P_XO, 10, 2, 5),
	F(3686636, P_GCC_GPLL0_OUT_MAIN_DIV_CLK_SRC, 1, 2, 217),
	F(4800000, P_XO, 5, 0, 0),
	F(7373272, P_GCC_GPLL0_OUT_MAIN_DIV_CLK_SRC, 1, 4, 217),
	F(9600000, P_XO, 2.5, 0, 0),
	F(14746544, P_GCC_GPLL0_OUT_MAIN_DIV_CLK_SRC, 1, 8, 217),
	F(16000000, P_GPLL0_OUT_MAIN, 10, 1, 5),
	F(24000000, P_XO, 1, 0, 0),
	F(25000000, P_GPLL0_OUT_MAIN, 16, 1, 2),
	F(32000000, P_GPLL0_OUT_MAIN, 1, 1, 25),
	F(40000000, P_GPLL0_OUT_MAIN, 1, 1, 20),
	F(46400000, P_GPLL0_OUT_MAIN, 2, 29, 250),
	F(48000000, P_GPLL0_OUT_MAIN, 1, 3, 50),
	F(50000000, P_GPLL0_OUT_MAIN, 16, 0, 0),
	F(51200000, P_GPLL0_OUT_MAIN, 1, 8, 125),
	F(56000000, P_GPLL0_OUT_MAIN, 1, 7, 100),
	F(58986175, P_GPLL0_OUT_MAIN, 1, 16, 217),
	F(60000000, P_GPLL0_OUT_MAIN, 1, 3, 40),
	F(64000000, P_GPLL0_OUT_MAIN, 12.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_qupv3_wrap_se0_clk_src = {
	.cmd_rcgr = 0x4004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap_se0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_wrap_se0_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_qupv3_wrap_se1_clk_src = {
	.cmd_rcgr = 0x5004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap_se0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_wrap_se1_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_qupv3_wrap_se2_clk_src = {
	.cmd_rcgr = 0x2018,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap_se0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_wrap_se2_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_qupv3_wrap_se3_clk_src = {
	.cmd_rcgr = 0x2034,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap_se0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_wrap_se3_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_qupv3_wrap_se4_clk_src = {
	.cmd_rcgr = 0x3018,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap_se0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_wrap_se4_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_qupv3_wrap_se5_clk_src = {
	.cmd_rcgr = 0x3034,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap_se0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_wrap_se5_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_sdcc1_apps_clk_src[] = {
	F(144000, P_XO, 16, 12, 125),
	F(400000, P_XO, 12, 1, 5),
	F(24000000, P_GPLL2_OUT_MAIN, 12, 1, 2),
	F(48000000, P_GPLL2_OUT_MAIN, 12, 0, 0),
	F(96000000, P_GPLL2_OUT_MAIN, 6, 0, 0),
	F(177777778, P_GPLL0_OUT_MAIN, 4.5, 0, 0),
	F(192000000, P_GPLL2_OUT_MAIN, 3, 0, 0),
	F(200000000, P_GPLL0_OUT_MAIN, 4, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc1_apps_clk_src = {
	.cmd_rcgr = 0x33004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_6,
	.freq_tbl = ftbl_gcc_sdcc1_apps_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_sdcc1_apps_clk_src",
		.parent_data = gcc_parent_data_6,
		.num_parents = ARRAY_SIZE(gcc_parent_data_6),
		.ops = &clk_rcg2_floor_ops,
	},
};

static const struct freq_tbl ftbl_gcc_sdcc1_ice_core_clk_src[] = {
	F(300000000, P_GPLL4_OUT_MAIN, 4, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc1_ice_core_clk_src = {
	.cmd_rcgr = 0x33018,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_7,
	.freq_tbl = ftbl_gcc_sdcc1_ice_core_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_sdcc1_ice_core_clk_src",
		.parent_data = gcc_parent_data_7,
		.num_parents = ARRAY_SIZE(gcc_parent_data_7),
		.ops = &clk_rcg2_floor_ops,
	},
};

static struct clk_rcg2 gcc_uniphy_sys_clk_src = {
	.cmd_rcgr = 0x17090,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_nss_ts_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_uniphy_sys_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_usb0_aux_clk_src = {
	.cmd_rcgr = 0x2c018,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_8,
	.freq_tbl = ftbl_gcc_nss_ts_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb0_aux_clk_src",
		.parent_data = gcc_parent_data_8,
		.num_parents = ARRAY_SIZE(gcc_parent_data_8),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb0_master_clk_src[] = {
	F(200000000, P_GPLL0_OUT_MAIN, 4, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb0_master_clk_src = {
	.cmd_rcgr = 0x2c004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb0_master_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb0_master_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb0_mock_utmi_clk_src[] = {
	F(24000000, P_XO, 1, 0, 0),
	F(60000000, P_GPLL4_OUT_AUX, 10, 1, 2),
	{ }
};

static struct clk_rcg2 gcc_usb0_mock_utmi_clk_src = {
	.cmd_rcgr = 0x2c02c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_9,
	.freq_tbl = ftbl_gcc_usb0_mock_utmi_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb0_mock_utmi_clk_src",
		.parent_data = gcc_parent_data_9,
		.num_parents = ARRAY_SIZE(gcc_parent_data_9),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_qdss_at_clk_src[] = {
	F(240000000, P_GPLL4_OUT_MAIN, 5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_qdss_at_clk_src = {
	.cmd_rcgr = 0x2d004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_10,
	.freq_tbl = ftbl_gcc_qdss_at_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qdss_at_clk_src",
		.parent_data = gcc_parent_data_10,
		.num_parents = ARRAY_SIZE(gcc_parent_data_10),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_qdss_tsctr_clk_src[] = {
	F(600000000, P_GPLL4_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_qdss_tsctr_clk_src = {
	.cmd_rcgr = 0x2d01c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_10,
	.freq_tbl = ftbl_gcc_qdss_tsctr_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qdss_tsctr_clk_src",
		.parent_data = gcc_parent_data_10,
		.num_parents = ARRAY_SIZE(gcc_parent_data_10),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcnoc_bfdcd_clk_src[] = {
	F(24000000, P_XO, 1, 0, 0),
	F(50000000, P_GPLL0_OUT_MAIN, 16, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 8, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcnoc_bfdcd_clk_src = {
	.cmd_rcgr = 0x31004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcnoc_bfdcd_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcnoc_bfdcd_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
		/*
		 * There are no consumers for this source in kernel yet,
		 * (will be added soon), so the clock framework
		 * disables this source. But some of the clocks
		 * initialized by boot loaders uses this source. So we
		 * need to keep this clock ON. Add the
		 * CLK_IGNORE_UNUSED flag so the clock will not be
		 * disabled. Once the consumer in kernel is added, we
		 * can get rid of this flag.
		 */
		.flags = CLK_IS_CRITICAL,
	},
};

static const struct freq_tbl ftbl_gcc_qpic_io_macro_clk_src[] = {
	F(24000000, P_XO, 1, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 8, 0, 0),
	F(200000000, P_GPLL0_OUT_MAIN, 4, 0, 0),
	F(320000000, P_GPLL0_OUT_MAIN, 2.5, 0, 0),
	F(400000000, P_GPLL0_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_qpic_io_macro_clk_src = {
	.cmd_rcgr = 0x32004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_12,
	.freq_tbl = ftbl_gcc_qpic_io_macro_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qpic_io_macro_clk_src",
		.parent_data = gcc_parent_data_12,
		.num_parents = ARRAY_SIZE(gcc_parent_data_12),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_qpic_clk_src[] = {
	F(24000000, P_XO, 1, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 8, 0, 0),
	F(200000000, P_GPLL0_OUT_MAIN, 4, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_qpic_clk_src = {
	.cmd_rcgr = 0x32020,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_12,
	.freq_tbl = ftbl_gcc_qpic_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qpic_clk_src",
		.parent_data = gcc_parent_data_12,
		.num_parents = ARRAY_SIZE(gcc_parent_data_12),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pon_tm2x_clk_src[] = {
	F(342860000, P_GPLL4_OUT_MAIN, 3.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pon_tm2x_clk_src = {
	.cmd_rcgr = 0x3c004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_11,
	.freq_tbl = ftbl_gcc_pon_tm2x_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pon_tm2x_clk_src",
		.parent_data = gcc_parent_data_11,
		.num_parents = ARRAY_SIZE(gcc_parent_data_11),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_sleep_clk_src[] = {
	F(32000, P_SLEEP_CLK, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sleep_clk_src = {
	.cmd_rcgr = 0x3400c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_13,
	.freq_tbl = ftbl_gcc_sleep_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_sleep_clk_src",
		.parent_data = gcc_parent_data_13,
		.num_parents = ARRAY_SIZE(gcc_parent_data_13),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_lpass_sway_clk_src[] = {
	F(133333333, P_GPLL0_OUT_MAIN, 6, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_lpass_sway_clk_src = {
	.cmd_rcgr = 0x27004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_lpass_sway_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_lpass_sway_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_lpass_axim_clk_src = {
	.cmd_rcgr = 0x2700c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_lpass_sway_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_lpass_axim_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div gcc_nssnoc_memnoc_div_clk_src = {
	.reg = 0x1700c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_nssnoc_memnoc_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_nssnoc_memnoc_bfdcd_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_usb0_mock_utmi_div_clk_src = {
	.reg = 0x2c040,
	.shift = 0,
	.width = 2,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb0_mock_utmi_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_usb0_mock_utmi_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_fixed_factor gcc_pon_tm_div_clk_src = {
	.mult = 1,
	.div = 2,
	.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pon_tm_div_clk_src",
		.parent_hws = (const struct clk_hw *[]) {
			&gcc_pon_tm2x_clk_src.clkr.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct clk_branch gcc_adss_pwm_clk = {
	.halt_reg = 0x1c00c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1c00c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_adss_pwm_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_adss_pwm_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cnoc_pcie0_1lane_s_clk = {
	.halt_reg = 0x31088,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x31088,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cnoc_pcie0_1lane_s_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie0_axi_s_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cnoc_pcie1_2lane_s_clk = {
	.halt_reg = 0x3108c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3108c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cnoc_pcie1_2lane_s_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie1_axi_s_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cnoc_usb_clk = {
	.halt_reg = 0x310a8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x310a8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cnoc_usb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb0_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdio_ahb_clk = {
	.halt_reg = 0x17040,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17040,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_mdio_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdio_gephy_ahb_clk = {
	.halt_reg = 0x17098,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17098,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_mdio_gephy_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_ts_clk = {
	.halt_reg = 0x17018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_nss_ts_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_nss_ts_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nsscc_clk = {
	.halt_reg = 0x17034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_nsscc_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nsscfg_clk = {
	.halt_reg = 0x1702c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1702c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_nsscfg_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_atb_clk = {
	.halt_reg = 0x17014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_nssnoc_atb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qdss_at_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_memnoc_1_clk = {
	.halt_reg = 0x17084,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17084,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_nssnoc_memnoc_1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_nssnoc_memnoc_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_memnoc_clk = {
	.halt_reg = 0x17024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_nssnoc_memnoc_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_nssnoc_memnoc_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_nsscc_clk = {
	.halt_reg = 0x17030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_nssnoc_nsscc_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_rcg2 gcc_xo_clk_src = {
	.cmd_rcgr = 0x34004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_xo,
	.freq_tbl = ftbl_gcc_nss_ts_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_xo_clk_src",
		.parent_data = &gcc_parent_data_xo,
		.num_parents = 1,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_fixed_factor gcc_xo_div4_clk_src = {
	.mult = 1,
	.div = 4,
	.hw.init = &(const struct clk_init_data) {
		.name = "gcc_xo_div4_clk_src",
		.parent_hws = (const struct clk_hw *[]) {
			&gcc_xo_clk_src.clkr.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct clk_branch gcc_gephy_sys_clk = {
	.halt_reg = 0x2a004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2a004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gephy_sys_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_xo_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_pcnoc_1_clk = {
	.halt_reg = 0x17080,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17080,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_nssnoc_pcnoc_1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_qosgen_ref_clk = {
	.halt_reg = 0x1701c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1701c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_nssnoc_qosgen_ref_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_xo_div4_clk_src.hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_snoc_1_clk = {
	.halt_reg = 0x1707c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1707c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_nssnoc_snoc_1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_system_noc_bfdcd_clk_src.clkr.hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_snoc_clk = {
	.halt_reg = 0x17028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_nssnoc_snoc_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_system_noc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_timeout_ref_clk = {
	.halt_reg = 0x17020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_nssnoc_timeout_ref_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_xo_div4_clk_src.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_xo_dcd_clk = {
	.halt_reg = 0x17074,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17074,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_nssnoc_xo_dcd_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_xo_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie0_ahb_clk = {
	.halt_reg = 0x28030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x28030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie0_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie0_aux_clk = {
	.halt_reg = 0x28070,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x28070,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie0_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie0_axi_m_clk = {
	.halt_reg = 0x28038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x28038,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie0_axi_m_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie0_axi_m_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie0_axi_s_bridge_clk = {
	.halt_reg = 0x28048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x28048,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie0_axi_s_bridge_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie0_axi_s_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie0_axi_s_clk = {
	.halt_reg = 0x28040,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x28040,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie0_axi_s_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie0_axi_s_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_pcie0_pipe_clk_src = {
	.reg = 0x28064,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "pcie0_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_PCIE30_PHY0_PIPE_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_branch gcc_pcie0_pipe_clk = {
	.halt_reg = 0x28068,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x28068,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie0_pipe_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&gcc_pcie0_pipe_clk_src.clkr.hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie1_ahb_clk = {
	.halt_reg = 0x29030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x29030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie1_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie1_aux_clk = {
	.halt_reg = 0x29074,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x29074,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie1_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie1_axi_m_clk = {
	.halt_reg = 0x29038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x29038,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie1_axi_m_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie1_axi_m_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie1_axi_s_bridge_clk = {
	.halt_reg = 0x29048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x29048,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie1_axi_s_bridge_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie1_axi_s_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie1_axi_s_clk = {
	.halt_reg = 0x29040,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x29040,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie1_axi_s_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie1_axi_s_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_pcie1_pipe_clk_src = {
	.reg = 0x29064,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "pcie1_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_PCIE30_PHY1_PIPE_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_branch gcc_pcie1_pipe_clk = {
	.halt_reg = 0x29068,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x29068,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie1_pipe_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&gcc_pcie1_pipe_clk_src.clkr.hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qrng_ahb_clk = {
	.halt_reg = 0x13024,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0xb004,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qrng_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_ahb_mst_clk = {
	.halt_reg = 0x1014,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0xb004,
		.enable_mask = BIT(14),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_ahb_mst_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_ahb_slv_clk = {
	.halt_reg = 0x102c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0xb004,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_ahb_slv_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_se0_clk = {
	.halt_reg = 0x4020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_se0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap_se0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_se1_clk = {
	.halt_reg = 0x5020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_se1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap_se1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_se2_clk = {
	.halt_reg = 0x202c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x202c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_se2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap_se2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_se3_clk = {
	.halt_reg = 0x2048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2048,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_se3_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap_se3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_se4_clk = {
	.halt_reg = 0x302c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x302c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_se4_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap_se4_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_se5_clk = {
	.halt_reg = 0x3048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3048,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_se5_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap_se5_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_ahb_clk = {
	.halt_reg = 0x3303c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3303c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sdcc1_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_apps_clk = {
	.halt_reg = 0x3302c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3302c,
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
	.halt_reg = 0x33034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x33034,
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

static struct clk_branch gcc_snoc_pcie0_axi_m_clk = {
	.halt_reg = 0x2e04c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2e04c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_snoc_pcie0_axi_m_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie0_axi_m_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_snoc_pcie1_axi_m_clk = {
	.halt_reg = 0x2e050,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2e050,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_snoc_pcie1_axi_m_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie1_axi_m_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy0_ahb_clk = {
	.halt_reg = 0x1704c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1704c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_uniphy0_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy0_sys_clk = {
	.halt_reg = 0x17048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17048,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_uniphy0_sys_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_uniphy_sys_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy1_ahb_clk = {
	.halt_reg = 0x1705c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1705c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_uniphy1_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy1_sys_clk = {
	.halt_reg = 0x17058,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17058,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_uniphy1_sys_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_uniphy_sys_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy2_ahb_clk = {
	.halt_reg = 0x1706c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1706c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_uniphy2_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy2_sys_clk = {
	.halt_reg = 0x17068,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17068,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_uniphy2_sys_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_uniphy_sys_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb0_aux_clk = {
	.halt_reg = 0x2c04c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2c04c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb0_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb0_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb0_master_clk = {
	.halt_reg = 0x2c044,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2c044,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb0_master_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb0_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb0_mock_utmi_clk = {
	.halt_reg = 0x2c050,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2c050,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb0_mock_utmi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb0_mock_utmi_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb0_phy_cfg_ahb_clk = {
	.halt_reg = 0x2c05c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2c05c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb0_phy_cfg_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_usb0_pipe_clk_src = {
	.reg = 0x2c074,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb0_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_USB3_PHY0_CC_PIPE_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_branch gcc_usb0_pipe_clk = {
	.halt_reg = 0x2c054,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x2c054,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb0_pipe_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&gcc_usb0_pipe_clk_src.clkr.hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb0_sleep_clk = {
	.halt_reg = 0x2c058,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2c058,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb0_sleep_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_sleep_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie0_rchng_clk = {
	.halt_reg = 0x28028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x28028,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie0_rchng_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&gcc_pcie0_rchng_clk_src.clkr.hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie1_rchng_clk = {
	.halt_reg = 0x29028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x29028,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie1_rchng_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&gcc_pcie1_rchng_clk_src.clkr.hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qpic_ahb_clk = {
	.halt_reg = 0x32010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x32010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qpic_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qpic_clk = {
	.halt_reg = 0x32028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x32028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qpic_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qpic_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qpic_io_macro_clk = {
	.halt_reg = 0x3200c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3200c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qpic_io_macro_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qpic_io_macro_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cmn_12gpll_ahb_clk = {
	.halt_reg = 0x3a004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3a004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cmn_12gpll_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cmn_12gpll_sys_clk = {
	.halt_reg = 0x3a008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3a008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cmn_12gpll_sys_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_uniphy_sys_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qdss_at_clk = {
	.halt_reg = 0x2d034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2d034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qdss_at_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qdss_at_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qdss_dap_clk = {
	.halt_reg = 0x2d058,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0xb004,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qdss_dap_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qdss_tsctr_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pon_apb_clk = {
	.halt_reg = 0x3c01c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3c01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pon_apb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pon_tm_clk = {
	.halt_reg = 0x3c014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3c014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pon_tm_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pon_tm_div_clk_src.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pon_tm2x_clk = {
	.halt_reg = 0x3c00c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3c00c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pon_tm2x_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pon_tm2x_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_snoc_lpass_clk = {
	.halt_reg = 0x2e028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2e028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_snoc_lpass_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_lpass_axim_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_lpass_sway_clk = {
	.halt_reg = 0x27014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x27014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_lpass_sway_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_lpass_sway_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cnoc_lpass_cfg_clk = {
	.halt_reg = 0x31020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x31020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cnoc_lpass_cfg_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_lpass_sway_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_lpass_core_axim_clk = {
	.halt_reg = 0x27018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x27018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_lpass_core_axim_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_lpass_axim_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static __maybe_unused struct clk_regmap *gcc_ipq5210_clocks[] = {
	[GCC_ADSS_PWM_CLK] = &gcc_adss_pwm_clk.clkr,
	[GCC_ADSS_PWM_CLK_SRC] = &gcc_adss_pwm_clk_src.clkr,
	[GCC_CMN_12GPLL_AHB_CLK] = &gcc_cmn_12gpll_ahb_clk.clkr,
	[GCC_CMN_12GPLL_SYS_CLK] = &gcc_cmn_12gpll_sys_clk.clkr,
	[GCC_CNOC_LPASS_CFG_CLK] = &gcc_cnoc_lpass_cfg_clk.clkr,
	[GCC_CNOC_PCIE0_1LANE_S_CLK] = &gcc_cnoc_pcie0_1lane_s_clk.clkr,
	[GCC_CNOC_PCIE1_2LANE_S_CLK] = &gcc_cnoc_pcie1_2lane_s_clk.clkr,
	[GCC_CNOC_USB_CLK] = &gcc_cnoc_usb_clk.clkr,
	[GCC_GEPHY_SYS_CLK] = &gcc_gephy_sys_clk.clkr,
	[GCC_LPASS_AXIM_CLK_SRC] = &gcc_lpass_axim_clk_src.clkr,
	[GCC_LPASS_CORE_AXIM_CLK] = &gcc_lpass_core_axim_clk.clkr,
	[GCC_LPASS_SWAY_CLK] = &gcc_lpass_sway_clk.clkr,
	[GCC_LPASS_SWAY_CLK_SRC] = &gcc_lpass_sway_clk_src.clkr,
	[GCC_MDIO_AHB_CLK] = &gcc_mdio_ahb_clk.clkr,
	[GCC_MDIO_GEPHY_AHB_CLK] = &gcc_mdio_gephy_ahb_clk.clkr,
	[GCC_NSS_TS_CLK] = &gcc_nss_ts_clk.clkr,
	[GCC_NSS_TS_CLK_SRC] = &gcc_nss_ts_clk_src.clkr,
	[GCC_NSSCC_CLK] = &gcc_nsscc_clk.clkr,
	[GCC_NSSCFG_CLK] = &gcc_nsscfg_clk.clkr,
	[GCC_NSSNOC_ATB_CLK] = &gcc_nssnoc_atb_clk.clkr,
	[GCC_NSSNOC_MEMNOC_1_CLK] = &gcc_nssnoc_memnoc_1_clk.clkr,
	[GCC_NSSNOC_MEMNOC_BFDCD_CLK_SRC] = &gcc_nssnoc_memnoc_bfdcd_clk_src.clkr,
	[GCC_NSSNOC_MEMNOC_CLK] = &gcc_nssnoc_memnoc_clk.clkr,
	[GCC_NSSNOC_MEMNOC_DIV_CLK_SRC] = &gcc_nssnoc_memnoc_div_clk_src.clkr,
	[GCC_NSSNOC_NSSCC_CLK] = &gcc_nssnoc_nsscc_clk.clkr,
	[GCC_NSSNOC_PCNOC_1_CLK] = &gcc_nssnoc_pcnoc_1_clk.clkr,
	[GCC_NSSNOC_QOSGEN_REF_CLK] = &gcc_nssnoc_qosgen_ref_clk.clkr,
	[GCC_NSSNOC_SNOC_1_CLK] = &gcc_nssnoc_snoc_1_clk.clkr,
	[GCC_NSSNOC_SNOC_CLK] = &gcc_nssnoc_snoc_clk.clkr,
	[GCC_NSSNOC_TIMEOUT_REF_CLK] = &gcc_nssnoc_timeout_ref_clk.clkr,
	[GCC_NSSNOC_XO_DCD_CLK] = &gcc_nssnoc_xo_dcd_clk.clkr,
	[GCC_PCIE0_AHB_CLK] = &gcc_pcie0_ahb_clk.clkr,
	[GCC_PCIE0_AUX_CLK] = &gcc_pcie0_aux_clk.clkr,
	[GCC_PCIE0_AXI_M_CLK] = &gcc_pcie0_axi_m_clk.clkr,
	[GCC_PCIE0_AXI_M_CLK_SRC] = &gcc_pcie0_axi_m_clk_src.clkr,
	[GCC_PCIE0_AXI_S_BRIDGE_CLK] = &gcc_pcie0_axi_s_bridge_clk.clkr,
	[GCC_PCIE0_AXI_S_CLK] = &gcc_pcie0_axi_s_clk.clkr,
	[GCC_PCIE0_AXI_S_CLK_SRC] = &gcc_pcie0_axi_s_clk_src.clkr,
	[GCC_PCIE0_PIPE_CLK] = &gcc_pcie0_pipe_clk.clkr,
	[GCC_PCIE0_PIPE_CLK_SRC] = &gcc_pcie0_pipe_clk_src.clkr,
	[GCC_PCIE0_RCHNG_CLK] = &gcc_pcie0_rchng_clk.clkr,
	[GCC_PCIE0_RCHNG_CLK_SRC] = &gcc_pcie0_rchng_clk_src.clkr,
	[GCC_PCIE1_AHB_CLK] = &gcc_pcie1_ahb_clk.clkr,
	[GCC_PCIE1_AUX_CLK] = &gcc_pcie1_aux_clk.clkr,
	[GCC_PCIE1_AXI_M_CLK] = &gcc_pcie1_axi_m_clk.clkr,
	[GCC_PCIE1_AXI_M_CLK_SRC] = &gcc_pcie1_axi_m_clk_src.clkr,
	[GCC_PCIE1_AXI_S_BRIDGE_CLK] = &gcc_pcie1_axi_s_bridge_clk.clkr,
	[GCC_PCIE1_AXI_S_CLK] = &gcc_pcie1_axi_s_clk.clkr,
	[GCC_PCIE1_AXI_S_CLK_SRC] = &gcc_pcie1_axi_s_clk_src.clkr,
	[GCC_PCIE1_PIPE_CLK] = &gcc_pcie1_pipe_clk.clkr,
	[GCC_PCIE1_PIPE_CLK_SRC] = &gcc_pcie1_pipe_clk_src.clkr,
	[GCC_PCIE1_RCHNG_CLK] = &gcc_pcie1_rchng_clk.clkr,
	[GCC_PCIE1_RCHNG_CLK_SRC] = &gcc_pcie1_rchng_clk_src.clkr,
	[GCC_PCIE_AUX_CLK_SRC] = &gcc_pcie_aux_clk_src.clkr,
	[GCC_PCNOC_BFDCD_CLK_SRC] = &gcc_pcnoc_bfdcd_clk_src.clkr,
	[GCC_PON_APB_CLK] = &gcc_pon_apb_clk.clkr,
	[GCC_PON_TM_CLK] = &gcc_pon_tm_clk.clkr,
	[GCC_PON_TM2X_CLK] = &gcc_pon_tm2x_clk.clkr,
	[GCC_PON_TM2X_CLK_SRC] = &gcc_pon_tm2x_clk_src.clkr,
	[GCC_QDSS_AT_CLK] = &gcc_qdss_at_clk.clkr,
	[GCC_QDSS_AT_CLK_SRC] = &gcc_qdss_at_clk_src.clkr,
	[GCC_QDSS_DAP_CLK] = &gcc_qdss_dap_clk.clkr,
	[GCC_QDSS_TSCTR_CLK_SRC] = &gcc_qdss_tsctr_clk_src.clkr,
	[GCC_QPIC_AHB_CLK] = &gcc_qpic_ahb_clk.clkr,
	[GCC_QPIC_CLK] = &gcc_qpic_clk.clkr,
	[GCC_QPIC_CLK_SRC] = &gcc_qpic_clk_src.clkr,
	[GCC_QPIC_IO_MACRO_CLK] = &gcc_qpic_io_macro_clk.clkr,
	[GCC_QPIC_IO_MACRO_CLK_SRC] = &gcc_qpic_io_macro_clk_src.clkr,
	[GCC_QRNG_AHB_CLK] = &gcc_qrng_ahb_clk.clkr,
	[GCC_QUPV3_AHB_MST_CLK] = &gcc_qupv3_ahb_mst_clk.clkr,
	[GCC_QUPV3_AHB_SLV_CLK] = &gcc_qupv3_ahb_slv_clk.clkr,
	[GCC_QUPV3_WRAP_SE0_CLK] = &gcc_qupv3_wrap_se0_clk.clkr,
	[GCC_QUPV3_WRAP_SE0_CLK_SRC] = &gcc_qupv3_wrap_se0_clk_src.clkr,
	[GCC_QUPV3_WRAP_SE1_CLK] = &gcc_qupv3_wrap_se1_clk.clkr,
	[GCC_QUPV3_WRAP_SE1_CLK_SRC] = &gcc_qupv3_wrap_se1_clk_src.clkr,
	[GCC_QUPV3_WRAP_SE2_CLK] = &gcc_qupv3_wrap_se2_clk.clkr,
	[GCC_QUPV3_WRAP_SE2_CLK_SRC] = &gcc_qupv3_wrap_se2_clk_src.clkr,
	[GCC_QUPV3_WRAP_SE3_CLK] = &gcc_qupv3_wrap_se3_clk.clkr,
	[GCC_QUPV3_WRAP_SE3_CLK_SRC] = &gcc_qupv3_wrap_se3_clk_src.clkr,
	[GCC_QUPV3_WRAP_SE4_CLK] = &gcc_qupv3_wrap_se4_clk.clkr,
	[GCC_QUPV3_WRAP_SE4_CLK_SRC] = &gcc_qupv3_wrap_se4_clk_src.clkr,
	[GCC_QUPV3_WRAP_SE5_CLK] = &gcc_qupv3_wrap_se5_clk.clkr,
	[GCC_QUPV3_WRAP_SE5_CLK_SRC] = &gcc_qupv3_wrap_se5_clk_src.clkr,
	[GCC_SDCC1_AHB_CLK] = &gcc_sdcc1_ahb_clk.clkr,
	[GCC_SDCC1_APPS_CLK] = &gcc_sdcc1_apps_clk.clkr,
	[GCC_SDCC1_APPS_CLK_SRC] = &gcc_sdcc1_apps_clk_src.clkr,
	[GCC_SDCC1_ICE_CORE_CLK] = &gcc_sdcc1_ice_core_clk.clkr,
	[GCC_SDCC1_ICE_CORE_CLK_SRC] = &gcc_sdcc1_ice_core_clk_src.clkr,
	[GCC_SLEEP_CLK_SRC] = &gcc_sleep_clk_src.clkr,
	[GCC_SNOC_LPASS_CLK] = &gcc_snoc_lpass_clk.clkr,
	[GCC_SNOC_PCIE0_AXI_M_CLK] = &gcc_snoc_pcie0_axi_m_clk.clkr,
	[GCC_SNOC_PCIE1_AXI_M_CLK] = &gcc_snoc_pcie1_axi_m_clk.clkr,
	[GCC_SYSTEM_NOC_BFDCD_CLK_SRC] = &gcc_system_noc_bfdcd_clk_src.clkr,
	[GCC_UNIPHY0_AHB_CLK] = &gcc_uniphy0_ahb_clk.clkr,
	[GCC_UNIPHY0_SYS_CLK] = &gcc_uniphy0_sys_clk.clkr,
	[GCC_UNIPHY1_AHB_CLK] = &gcc_uniphy1_ahb_clk.clkr,
	[GCC_UNIPHY1_SYS_CLK] = &gcc_uniphy1_sys_clk.clkr,
	[GCC_UNIPHY2_AHB_CLK] = &gcc_uniphy2_ahb_clk.clkr,
	[GCC_UNIPHY2_SYS_CLK] = &gcc_uniphy2_sys_clk.clkr,
	[GCC_UNIPHY_SYS_CLK_SRC] = &gcc_uniphy_sys_clk_src.clkr,
	[GCC_USB0_AUX_CLK] = &gcc_usb0_aux_clk.clkr,
	[GCC_USB0_AUX_CLK_SRC] = &gcc_usb0_aux_clk_src.clkr,
	[GCC_USB0_MASTER_CLK] = &gcc_usb0_master_clk.clkr,
	[GCC_USB0_MASTER_CLK_SRC] = &gcc_usb0_master_clk_src.clkr,
	[GCC_USB0_MOCK_UTMI_CLK] = &gcc_usb0_mock_utmi_clk.clkr,
	[GCC_USB0_MOCK_UTMI_CLK_SRC] = &gcc_usb0_mock_utmi_clk_src.clkr,
	[GCC_USB0_MOCK_UTMI_DIV_CLK_SRC] = &gcc_usb0_mock_utmi_div_clk_src.clkr,
	[GCC_USB0_PHY_CFG_AHB_CLK] = &gcc_usb0_phy_cfg_ahb_clk.clkr,
	[GCC_USB0_PIPE_CLK] = &gcc_usb0_pipe_clk.clkr,
	[GCC_USB0_PIPE_CLK_SRC] = &gcc_usb0_pipe_clk_src.clkr,
	[GCC_USB0_SLEEP_CLK] = &gcc_usb0_sleep_clk.clkr,
	[GCC_XO_CLK_SRC] = &gcc_xo_clk_src.clkr,
	[GPLL0_MAIN] = &gpll0_main.clkr,
	[GPLL0] = &gpll0.clkr,
	[GPLL2_MAIN] = &gpll2_main.clkr,
	[GPLL2] = &gpll2.clkr,
	[GPLL4_MAIN] = &gpll4_main.clkr,
};

static const struct qcom_reset_map gcc_ipq5210_resets[] = {
	[GCC_ADSS_BCR] = { 0x1c000 },
	[GCC_ADSS_PWM_ARES] = { 0x1c00c, 2 },
	[GCC_APC0_VOLTAGE_DROOP_DETECTOR_BCR] = { 0x38000 },
	[GCC_APC0_VOLTAGE_DROOP_DETECTOR_GPLL0_ARES] = { 0x3800c, 2 },
	[GCC_APSS_AHB_ARES] = { 0x24014, 2 },
	[GCC_APSS_ATB_ARES] = { 0x24034, 2 },
	[GCC_APSS_AXI_ARES] = { 0x24018, 2 },
	[GCC_APSS_TS_ARES] = { 0x24030, 2 },
	[GCC_BOOT_ROM_AHB_ARES] = { 0x1302c, 2 },
	[GCC_BOOT_ROM_BCR] = { 0x13028 },
	[GCC_GEPHY_BCR] = { 0x2a000 },
	[GCC_GEPHY_SYS_ARES] = { 0x2a004, 2 },
	[GCC_GP1_ARES] = { 0x8018, 2 },
	[GCC_GP2_ARES] = { 0x9018, 2 },
	[GCC_GP3_ARES] = { 0xa018, 2 },
	[GCC_MDIO_AHB_ARES] = { 0x17040, 2 },
	[GCC_MDIO_BCR] = { 0x1703c },
	[GCC_MDIO_GEPHY_AHB_ARES] = { 0x17098, 2 },
	[GCC_NSS_BCR] = { 0x17000 },
	[GCC_NSS_TS_ARES] = { 0x17018, 2 },
	[GCC_NSSCC_ARES] = { 0x17034, 2 },
	[GCC_NSSCFG_ARES] = { 0x1702c, 2 },
	[GCC_NSSNOC_ATB_ARES] = { 0x17014, 2 },
	[GCC_NSSNOC_MEMNOC_1_ARES] = { 0x17084, 2 },
	[GCC_NSSNOC_MEMNOC_ARES] = { 0x17024, 2 },
	[GCC_NSSNOC_NSSCC_ARES] = { 0x17030, 2 },
	[GCC_NSSNOC_PCNOC_1_ARES] = { 0x17080, 2 },
	[GCC_NSSNOC_QOSGEN_REF_ARES] = { 0x1701c, 2 },
	[GCC_NSSNOC_SNOC_1_ARES] = { 0x1707c, 2 },
	[GCC_NSSNOC_SNOC_ARES] = { 0x17028, 2 },
	[GCC_NSSNOC_TIMEOUT_REF_ARES] = { 0x17020, 2 },
	[GCC_NSSNOC_XO_DCD_ARES] = { 0x17074, 2 },
	[GCC_PCIE0_AHB_ARES] = { 0x28030, 2 },
	[GCC_PCIE0_AUX_ARES] = { 0x28070, 2 },
	[GCC_PCIE0_AXI_M_ARES] = { 0x28038, 2 },
	[GCC_PCIE0_AXI_S_BRIDGE_ARES] = { 0x28048, 2 },
	[GCC_PCIE0_AXI_S_ARES] = { 0x28040, 2 },
	[GCC_PCIE0_BCR] = { 0x28000 },
	[GCC_PCIE0_LINK_DOWN_BCR] = { 0x28054 },
	[GCC_PCIE0_PIPE_RESET] = { 0x28058, 0 },
	[GCC_PCIE0_CORE_STICKY_RESET] = { 0x28058, 1 },
	[GCC_PCIE0_AXI_S_STICKY_RESET] = { 0x28058, 2 },
	[GCC_PCIE0_AXI_S_RESET] = { 0x28058, 3 },
	[GCC_PCIE0_AXI_M_STICKY_RESET] = { 0x28058, 4 },
	[GCC_PCIE0_AXI_M_RESET] = { 0x28058, 5 },
	[GCC_PCIE0_AUX_RESET] = { 0x28058, 6 },
	[GCC_PCIE0_AHB_RESET] = { 0x28058, 7 },
	[GCC_PCIE0_PHY_BCR] = { 0x28060 },
	[GCC_PCIE0_PIPE_ARES] = { 0x28068, 2 },
	[GCC_PCIE0PHY_PHY_BCR] = { 0x2805c },
	[GCC_PCIE1_AHB_ARES] = { 0x29030, 2 },
	[GCC_PCIE1_AUX_ARES] = { 0x29074, 2 },
	[GCC_PCIE1_AXI_M_ARES] = { 0x29038, 2 },
	[GCC_PCIE1_AXI_S_BRIDGE_ARES] = { 0x29048, 2 },
	[GCC_PCIE1_AXI_S_ARES] = { 0x29040, 2 },
	[GCC_PCIE1_BCR] = { 0x29000 },
	[GCC_PCIE1_LINK_DOWN_BCR] = { 0x29054 },
	[GCC_PCIE1_PIPE_RESET] = { 0x29058, 0 },
	[GCC_PCIE1_CORE_STICKY_RESET] = { 0x29058, 1 },
	[GCC_PCIE1_AXI_S_STICKY_RESET] = { 0x29058, 2 },
	[GCC_PCIE1_AXI_S_RESET] = { 0x29058, 3 },
	[GCC_PCIE1_AXI_M_STICKY_RESET] = { 0x29058, 4 },
	[GCC_PCIE1_AXI_M_RESET] = { 0x29058, 5 },
	[GCC_PCIE1_AUX_RESET] = { 0x29058, 6 },
	[GCC_PCIE1_AHB_RESET] = { 0x29058, 7 },
	[GCC_PCIE1_PHY_BCR] = { 0x29060 },
	[GCC_PCIE1_PIPE_ARES] = { 0x29068, 2 },
	[GCC_PCIE1PHY_PHY_BCR] = { 0x2905c },
	[GCC_QRNG_AHB_ARES] = { 0x13024, 2 },
	[GCC_QRNG_BCR] = { 0x13020 },
	[GCC_QUPV3_2X_CORE_ARES] = { 0x1020, 2 },
	[GCC_QUPV3_AHB_MST_ARES] = { 0x1014, 2 },
	[GCC_QUPV3_AHB_SLV_ARES] = { 0x102c, 2 },
	[GCC_QUPV3_BCR] = { 0x1000 },
	[GCC_QUPV3_CORE_ARES] = { 0x1018, 2 },
	[GCC_QUPV3_WRAP_SE0_ARES] = { 0x4020, 2 },
	[GCC_QUPV3_WRAP_SE0_BCR] = { 0x4000 },
	[GCC_QUPV3_WRAP_SE1_ARES] = { 0x5020, 2 },
	[GCC_QUPV3_WRAP_SE1_BCR] = { 0x5000 },
	[GCC_QUPV3_WRAP_SE2_ARES] = { 0x202c, 2 },
	[GCC_QUPV3_WRAP_SE2_BCR] = { 0x2000 },
	[GCC_QUPV3_WRAP_SE3_ARES] = { 0x2048, 2 },
	[GCC_QUPV3_WRAP_SE3_BCR] = { 0x2030 },
	[GCC_QUPV3_WRAP_SE4_ARES] = { 0x302c, 2 },
	[GCC_QUPV3_WRAP_SE4_BCR] = { 0x3000 },
	[GCC_QUPV3_WRAP_SE5_ARES] = { 0x3048, 2 },
	[GCC_QUPV3_WRAP_SE5_BCR] = { 0x3030 },
	[GCC_QUSB2_0_PHY_BCR] = { 0x2c068 },
	[GCC_SDCC1_AHB_ARES] = { 0x3303c, 2 },
	[GCC_SDCC1_APPS_ARES] = { 0x3302c, 2 },
	[GCC_SDCC1_ICE_CORE_ARES] = { 0x33034, 2 },
	[GCC_SDCC_BCR] = { 0x33000 },
	[GCC_TLMM_AHB_ARES] = { 0x3e004, 2 },
	[GCC_TLMM_ARES] = { 0x3e008, 2 },
	[GCC_TLMM_BCR] = { 0x3e000 },
	[GCC_UNIPHY0_AHB_ARES] = { 0x1704c, 2 },
	[GCC_UNIPHY0_BCR] = { 0x17044 },
	[GCC_UNIPHY0_SYS_ARES] = { 0x17048, 2 },
	[GCC_UNIPHY1_AHB_ARES] = { 0x1705c, 2 },
	[GCC_UNIPHY1_BCR] = { 0x17054 },
	[GCC_UNIPHY1_SYS_ARES] = { 0x17058, 2 },
	[GCC_UNIPHY2_AHB_ARES] = { 0x1706c, 2 },
	[GCC_UNIPHY2_BCR] = { 0x17064 },
	[GCC_UNIPHY2_SYS_ARES] = { 0x17068, 2 },
	[GCC_USB0_AUX_ARES] = { 0x2c04c, 2 },
	[GCC_USB0_MASTER_ARES] = { 0x2c044, 2 },
	[GCC_USB0_MOCK_UTMI_ARES] = { 0x2c050, 2 },
	[GCC_USB0_PHY_BCR] = { 0x2c06c },
	[GCC_USB0_PHY_CFG_AHB_ARES] = { 0x2c05c, 2 },
	[GCC_USB0_PIPE_ARES] = { 0x2c054, 2 },
	[GCC_USB0_SLEEP_ARES] = { 0x2c058, 2 },
	[GCC_USB3PHY_0_PHY_BCR] = { 0x2c070 },
	[GCC_USB_BCR] = { 0x2c000 },
	[GCC_QDSS_BCR] = { 0x2d000 },
};

static const struct of_device_id gcc_ipq5210_match_table[] = {
	{ .compatible = "qcom,ipq5210-gcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_ipq5210_match_table);

static const struct regmap_config gcc_ipq5210_regmap_config = {
	.reg_bits       = 32,
	.reg_stride     = 4,
	.val_bits       = 32,
	.max_register   = 0x3f024,
	.fast_io        = true,
};

static struct clk_hw *gcc_ipq5210_hws[] = {
	&gpll0_div2.hw,
	&gcc_xo_div4_clk_src.hw,
	&gcc_pon_tm_div_clk_src.hw,
};

static const struct qcom_cc_desc gcc_ipq5210_desc = {
	.config = &gcc_ipq5210_regmap_config,
	.clks = gcc_ipq5210_clocks,
	.num_clks = ARRAY_SIZE(gcc_ipq5210_clocks),
	.resets = gcc_ipq5210_resets,
	.num_resets = ARRAY_SIZE(gcc_ipq5210_resets),
	.clk_hws = gcc_ipq5210_hws,
	.num_clk_hws = ARRAY_SIZE(gcc_ipq5210_hws),
};

static int gcc_ipq5210_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &gcc_ipq5210_desc);
}

static struct platform_driver gcc_ipq5210_driver = {
	.probe = gcc_ipq5210_probe,
	.driver = {
		.name   = "qcom,gcc-ipq5210",
		.of_match_table = gcc_ipq5210_match_table,
	},
};

static int __init gcc_ipq5210_init(void)
{
	return platform_driver_register(&gcc_ipq5210_driver);
}
core_initcall(gcc_ipq5210_init);

static void __exit gcc_ipq5210_exit(void)
{
	platform_driver_unregister(&gcc_ipq5210_driver);
}
module_exit(gcc_ipq5210_exit);

MODULE_DESCRIPTION("QTI GCC IPQ5210 Driver");
MODULE_LICENSE("GPL");
