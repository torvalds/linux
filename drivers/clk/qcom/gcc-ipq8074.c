// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017, The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,gcc-ipq8074.h>

#include "common.h"
#include "clk-regmap.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-branch.h"
#include "clk-alpha-pll.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "gdsc.h"
#include "reset.h"

enum {
	P_XO,
	P_GPLL0,
	P_GPLL0_DIV2,
	P_GPLL2,
	P_GPLL4,
	P_GPLL6,
	P_SLEEP_CLK,
	P_PCIE20_PHY0_PIPE,
	P_PCIE20_PHY1_PIPE,
	P_USB3PHY_0_PIPE,
	P_USB3PHY_1_PIPE,
	P_UBI32_PLL,
	P_NSS_CRYPTO_PLL,
	P_BIAS_PLL,
	P_BIAS_PLL_NSS_NOC,
	P_UNIPHY0_RX,
	P_UNIPHY0_TX,
	P_UNIPHY1_RX,
	P_UNIPHY1_TX,
	P_UNIPHY2_RX,
	P_UNIPHY2_TX,
};

static struct clk_alpha_pll gpll0_main = {
	.offset = 0x21000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		.enable_reg = 0x0b000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpll0_main",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo",
				.name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct clk_fixed_factor gpll0_out_main_div2 = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "gpll0_out_main_div2",
		.parent_hws = (const struct clk_hw *[]){
				&gpll0_main.clkr.hw },
		.num_parents = 1,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct clk_alpha_pll_postdiv gpll0 = {
	.offset = 0x21000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll0",
		.parent_hws = (const struct clk_hw *[]){
				&gpll0_main.clkr.hw },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ro_ops,
	},
};

static struct clk_alpha_pll gpll2_main = {
	.offset = 0x4a000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		.enable_reg = 0x0b000,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "gpll2_main",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo",
				.name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
			.flags = CLK_IS_CRITICAL,
		},
	},
};

static struct clk_alpha_pll_postdiv gpll2 = {
	.offset = 0x4a000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll2",
		.parent_hws = (const struct clk_hw *[]){
				&gpll2_main.clkr.hw },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ro_ops,
	},
};

static struct clk_alpha_pll gpll4_main = {
	.offset = 0x24000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		.enable_reg = 0x0b000,
		.enable_mask = BIT(5),
		.hw.init = &(struct clk_init_data){
			.name = "gpll4_main",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo",
				.name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
			.flags = CLK_IS_CRITICAL,
		},
	},
};

static struct clk_alpha_pll_postdiv gpll4 = {
	.offset = 0x24000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll4",
		.parent_hws = (const struct clk_hw *[]){
				&gpll4_main.clkr.hw },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ro_ops,
	},
};

static struct clk_alpha_pll gpll6_main = {
	.offset = 0x37000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_BRAMMO],
	.flags = SUPPORTS_DYNAMIC_UPDATE,
	.clkr = {
		.enable_reg = 0x0b000,
		.enable_mask = BIT(7),
		.hw.init = &(struct clk_init_data){
			.name = "gpll6_main",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo",
				.name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
			.flags = CLK_IS_CRITICAL,
		},
	},
};

static struct clk_alpha_pll_postdiv gpll6 = {
	.offset = 0x37000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_BRAMMO],
	.width = 2,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll6",
		.parent_hws = (const struct clk_hw *[]){
				&gpll6_main.clkr.hw },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ro_ops,
	},
};

static struct clk_fixed_factor gpll6_out_main_div2 = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "gpll6_out_main_div2",
		.parent_hws = (const struct clk_hw *[]){
				&gpll6_main.clkr.hw },
		.num_parents = 1,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct clk_alpha_pll ubi32_pll_main = {
	.offset = 0x25000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_HUAYRA],
	.flags = SUPPORTS_DYNAMIC_UPDATE,
	.clkr = {
		.enable_reg = 0x0b000,
		.enable_mask = BIT(6),
		.hw.init = &(struct clk_init_data){
			.name = "ubi32_pll_main",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo",
				.name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_huayra_ops,
		},
	},
};

static struct clk_alpha_pll_postdiv ubi32_pll = {
	.offset = 0x25000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_HUAYRA],
	.width = 2,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "ubi32_pll",
		.parent_hws = (const struct clk_hw *[]){
				&ubi32_pll_main.clkr.hw },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ro_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_alpha_pll nss_crypto_pll_main = {
	.offset = 0x22000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		.enable_reg = 0x0b000,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "nss_crypto_pll_main",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo",
				.name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct clk_alpha_pll_postdiv nss_crypto_pll = {
	.offset = 0x22000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "nss_crypto_pll",
		.parent_hws = (const struct clk_hw *[]){
				&nss_crypto_pll_main.clkr.hw },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ro_ops,
	},
};

static const struct freq_tbl ftbl_pcnoc_bfdcd_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(50000000, P_GPLL0, 16, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	{ }
};

static const struct clk_parent_data gcc_xo_gpll0_gpll0_out_main_div2[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .hw = &gpll0.clkr.hw},
	{ .hw = &gpll0_out_main_div2.hw},
};

static const struct parent_map gcc_xo_gpll0_gpll0_out_main_div2_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL0_DIV2, 4 },
};

static struct clk_rcg2 pcnoc_bfdcd_clk_src = {
	.cmd_rcgr = 0x27000,
	.freq_tbl = ftbl_pcnoc_bfdcd_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pcnoc_bfdcd_clk_src",
		.parent_data = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll0_out_main_div2),
		.ops = &clk_rcg2_ops,
		.flags = CLK_IS_CRITICAL,
	},
};

static struct clk_fixed_factor pcnoc_clk_src = {
	.mult = 1,
	.div = 1,
	.hw.init = &(struct clk_init_data){
		.name = "pcnoc_clk_src",
		.parent_hws = (const struct clk_hw *[]){
				&pcnoc_bfdcd_clk_src.clkr.hw },
		.num_parents = 1,
		.ops = &clk_fixed_factor_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_branch gcc_sleep_clk_src = {
	.halt_reg = 0x30000,
	.clkr = {
		.enable_reg = 0x30000,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sleep_clk_src",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "sleep_clk",
				.name = "sleep_clk",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_IS_CRITICAL,
		},
	},
};

static const struct freq_tbl ftbl_blsp1_qup_i2c_apps_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(25000000, P_GPLL0_DIV2, 16, 0, 0),
	F(50000000, P_GPLL0, 16, 0, 0),
	{ }
};

static struct clk_rcg2 blsp1_qup1_i2c_apps_clk_src = {
	.cmd_rcgr = 0x0200c,
	.freq_tbl = ftbl_blsp1_qup_i2c_apps_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup1_i2c_apps_clk_src",
		.parent_data = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll0_out_main_div2),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_blsp1_qup_spi_apps_clk_src[] = {
	F(960000, P_XO, 10, 1, 2),
	F(4800000, P_XO, 4, 0, 0),
	F(9600000, P_XO, 2, 0, 0),
	F(12500000, P_GPLL0_DIV2, 16, 1, 2),
	F(16000000, P_GPLL0, 10, 1, 5),
	F(19200000, P_XO, 1, 0, 0),
	F(25000000, P_GPLL0, 16, 1, 2),
	F(50000000, P_GPLL0, 16, 0, 0),
	{ }
};

static struct clk_rcg2 blsp1_qup1_spi_apps_clk_src = {
	.cmd_rcgr = 0x02024,
	.freq_tbl = ftbl_blsp1_qup_spi_apps_clk_src,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup1_spi_apps_clk_src",
		.parent_data = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll0_out_main_div2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup2_i2c_apps_clk_src = {
	.cmd_rcgr = 0x03000,
	.freq_tbl = ftbl_blsp1_qup_i2c_apps_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup2_i2c_apps_clk_src",
		.parent_data = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll0_out_main_div2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup2_spi_apps_clk_src = {
	.cmd_rcgr = 0x03014,
	.freq_tbl = ftbl_blsp1_qup_spi_apps_clk_src,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup2_spi_apps_clk_src",
		.parent_data = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll0_out_main_div2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup3_i2c_apps_clk_src = {
	.cmd_rcgr = 0x04000,
	.freq_tbl = ftbl_blsp1_qup_i2c_apps_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup3_i2c_apps_clk_src",
		.parent_data = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll0_out_main_div2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup3_spi_apps_clk_src = {
	.cmd_rcgr = 0x04014,
	.freq_tbl = ftbl_blsp1_qup_spi_apps_clk_src,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup3_spi_apps_clk_src",
		.parent_data = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll0_out_main_div2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup4_i2c_apps_clk_src = {
	.cmd_rcgr = 0x05000,
	.freq_tbl = ftbl_blsp1_qup_i2c_apps_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup4_i2c_apps_clk_src",
		.parent_data = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll0_out_main_div2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup4_spi_apps_clk_src = {
	.cmd_rcgr = 0x05014,
	.freq_tbl = ftbl_blsp1_qup_spi_apps_clk_src,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup4_spi_apps_clk_src",
		.parent_data = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll0_out_main_div2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup5_i2c_apps_clk_src = {
	.cmd_rcgr = 0x06000,
	.freq_tbl = ftbl_blsp1_qup_i2c_apps_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup5_i2c_apps_clk_src",
		.parent_data = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll0_out_main_div2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup5_spi_apps_clk_src = {
	.cmd_rcgr = 0x06014,
	.freq_tbl = ftbl_blsp1_qup_spi_apps_clk_src,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup5_spi_apps_clk_src",
		.parent_data = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll0_out_main_div2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup6_i2c_apps_clk_src = {
	.cmd_rcgr = 0x07000,
	.freq_tbl = ftbl_blsp1_qup_i2c_apps_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup6_i2c_apps_clk_src",
		.parent_data = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll0_out_main_div2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup6_spi_apps_clk_src = {
	.cmd_rcgr = 0x07014,
	.freq_tbl = ftbl_blsp1_qup_spi_apps_clk_src,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup6_spi_apps_clk_src",
		.parent_data = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll0_out_main_div2),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_blsp1_uart_apps_clk_src[] = {
	F(3686400, P_GPLL0_DIV2, 1, 144, 15625),
	F(7372800, P_GPLL0_DIV2, 1, 288, 15625),
	F(14745600, P_GPLL0_DIV2, 1, 576, 15625),
	F(16000000, P_GPLL0_DIV2, 5, 1, 5),
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
	F(64000000, P_GPLL0, 12.5, 1, 1),
	{ }
};

static struct clk_rcg2 blsp1_uart1_apps_clk_src = {
	.cmd_rcgr = 0x02044,
	.freq_tbl = ftbl_blsp1_uart_apps_clk_src,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart1_apps_clk_src",
		.parent_data = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll0_out_main_div2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_uart2_apps_clk_src = {
	.cmd_rcgr = 0x03034,
	.freq_tbl = ftbl_blsp1_uart_apps_clk_src,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart2_apps_clk_src",
		.parent_data = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll0_out_main_div2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_uart3_apps_clk_src = {
	.cmd_rcgr = 0x04034,
	.freq_tbl = ftbl_blsp1_uart_apps_clk_src,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart3_apps_clk_src",
		.parent_data = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll0_out_main_div2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_uart4_apps_clk_src = {
	.cmd_rcgr = 0x05034,
	.freq_tbl = ftbl_blsp1_uart_apps_clk_src,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart4_apps_clk_src",
		.parent_data = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll0_out_main_div2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_uart5_apps_clk_src = {
	.cmd_rcgr = 0x06034,
	.freq_tbl = ftbl_blsp1_uart_apps_clk_src,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart5_apps_clk_src",
		.parent_data = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll0_out_main_div2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_uart6_apps_clk_src = {
	.cmd_rcgr = 0x07034,
	.freq_tbl = ftbl_blsp1_uart_apps_clk_src,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart6_apps_clk_src",
		.parent_data = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll0_out_main_div2),
		.ops = &clk_rcg2_ops,
	},
};

static const struct clk_parent_data gcc_xo_gpll0[] = {
	{ .fw_name = "xo" },
	{ .hw = &gpll0.clkr.hw },
};

static const struct parent_map gcc_xo_gpll0_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
};

static const struct freq_tbl ftbl_pcie_axi_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	{ }
};

static struct clk_rcg2 pcie0_axi_clk_src = {
	.cmd_rcgr = 0x75054,
	.freq_tbl = ftbl_pcie_axi_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pcie0_axi_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_pcie_aux_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static const struct clk_parent_data gcc_xo_gpll0_sleep_clk[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .hw = &gpll0.clkr.hw },
	{ .fw_name = "sleep_clk", .name = "sleep_clk" },
};

static const struct parent_map gcc_xo_gpll0_sleep_clk_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 2 },
	{ P_SLEEP_CLK, 6 },
};

static struct clk_rcg2 pcie0_aux_clk_src = {
	.cmd_rcgr = 0x75024,
	.freq_tbl = ftbl_pcie_aux_clk_src,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_sleep_clk_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pcie0_aux_clk_src",
		.parent_data = gcc_xo_gpll0_sleep_clk,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_sleep_clk),
		.ops = &clk_rcg2_ops,
	},
};

static const struct clk_parent_data gcc_pcie20_phy0_pipe_clk_xo[] = {
	{ .fw_name = "pcie0_pipe", .name = "pcie20_phy0_pipe_clk" },
	{ .fw_name = "xo", .name = "xo" },
};

static const struct parent_map gcc_pcie20_phy0_pipe_clk_xo_map[] = {
	{ P_PCIE20_PHY0_PIPE, 0 },
	{ P_XO, 2 },
};

static struct clk_regmap_mux pcie0_pipe_clk_src = {
	.reg = 0x7501c,
	.shift = 8,
	.width = 2,
	.parent_map = gcc_pcie20_phy0_pipe_clk_xo_map,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "pcie0_pipe_clk_src",
			.parent_data = gcc_pcie20_phy0_pipe_clk_xo,
			.num_parents = ARRAY_SIZE(gcc_pcie20_phy0_pipe_clk_xo),
			.ops = &clk_regmap_mux_closest_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg2 pcie1_axi_clk_src = {
	.cmd_rcgr = 0x76054,
	.freq_tbl = ftbl_pcie_axi_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pcie1_axi_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 pcie1_aux_clk_src = {
	.cmd_rcgr = 0x76024,
	.freq_tbl = ftbl_pcie_aux_clk_src,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_sleep_clk_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pcie1_aux_clk_src",
		.parent_data = gcc_xo_gpll0_sleep_clk,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_sleep_clk),
		.ops = &clk_rcg2_ops,
	},
};

static const struct clk_parent_data gcc_pcie20_phy1_pipe_clk_xo[] = {
	{ .fw_name = "pcie1_pipe", .name = "pcie20_phy1_pipe_clk" },
	{ .fw_name = "xo", .name = "xo" },
};

static const struct parent_map gcc_pcie20_phy1_pipe_clk_xo_map[] = {
	{ P_PCIE20_PHY1_PIPE, 0 },
	{ P_XO, 2 },
};

static struct clk_regmap_mux pcie1_pipe_clk_src = {
	.reg = 0x7601c,
	.shift = 8,
	.width = 2,
	.parent_map = gcc_pcie20_phy1_pipe_clk_xo_map,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "pcie1_pipe_clk_src",
			.parent_data = gcc_pcie20_phy1_pipe_clk_xo,
			.num_parents = ARRAY_SIZE(gcc_pcie20_phy1_pipe_clk_xo),
			.ops = &clk_regmap_mux_closest_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl ftbl_sdcc_apps_clk_src[] = {
	F(144000, P_XO, 16, 3, 25),
	F(400000, P_XO, 12, 1, 4),
	F(24000000, P_GPLL2, 12, 1, 4),
	F(48000000, P_GPLL2, 12, 1, 2),
	F(96000000, P_GPLL2, 12, 0, 0),
	F(177777778, P_GPLL0, 4.5, 0, 0),
	F(192000000, P_GPLL2, 6, 0, 0),
	F(384000000, P_GPLL2, 3, 0, 0),
	{ }
};

static const struct clk_parent_data gcc_xo_gpll0_gpll2_gpll0_out_main_div2[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll2.clkr.hw },
	{ .hw = &gpll0_out_main_div2.hw },
};

static const struct parent_map gcc_xo_gpll0_gpll2_gpll0_out_main_div2_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL2, 2 },
	{ P_GPLL0_DIV2, 4 },
};

static struct clk_rcg2 sdcc1_apps_clk_src = {
	.cmd_rcgr = 0x42004,
	.freq_tbl = ftbl_sdcc_apps_clk_src,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll2_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "sdcc1_apps_clk_src",
		.parent_data = gcc_xo_gpll0_gpll2_gpll0_out_main_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll2_gpll0_out_main_div2),
		.ops = &clk_rcg2_floor_ops,
	},
};

static const struct freq_tbl ftbl_sdcc_ice_core_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(160000000, P_GPLL0, 5, 0, 0),
	F(308570000, P_GPLL6, 3.5, 0, 0),
	{ }
};

static const struct clk_parent_data gcc_xo_gpll0_gpll6_gpll0_div2[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll6.clkr.hw },
	{ .hw = &gpll0_out_main_div2.hw },
};

static const struct parent_map gcc_xo_gpll0_gpll6_gpll0_div2_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL6, 2 },
	{ P_GPLL0_DIV2, 4 },
};

static struct clk_rcg2 sdcc1_ice_core_clk_src = {
	.cmd_rcgr = 0x5d000,
	.freq_tbl = ftbl_sdcc_ice_core_clk_src,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll6_gpll0_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "sdcc1_ice_core_clk_src",
		.parent_data = gcc_xo_gpll0_gpll6_gpll0_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll6_gpll0_div2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 sdcc2_apps_clk_src = {
	.cmd_rcgr = 0x43004,
	.freq_tbl = ftbl_sdcc_apps_clk_src,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll2_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "sdcc2_apps_clk_src",
		.parent_data = gcc_xo_gpll0_gpll2_gpll0_out_main_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll2_gpll0_out_main_div2),
		.ops = &clk_rcg2_floor_ops,
	},
};

static const struct freq_tbl ftbl_usb_master_clk_src[] = {
	F(80000000, P_GPLL0_DIV2, 5, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	F(133330000, P_GPLL0, 6, 0, 0),
	{ }
};

static const struct clk_parent_data gcc_xo_gpll0_out_main_div2_gpll0[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .hw = &gpll0_out_main_div2.hw },
	{ .hw = &gpll0.clkr.hw },
};

static const struct parent_map gcc_xo_gpll0_out_main_div2_gpll0_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0_DIV2, 2 },
	{ P_GPLL0, 1 },
};

static struct clk_rcg2 usb0_master_clk_src = {
	.cmd_rcgr = 0x3e00c,
	.freq_tbl = ftbl_usb_master_clk_src,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_out_main_div2_gpll0_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "usb0_master_clk_src",
		.parent_data = gcc_xo_gpll0_out_main_div2_gpll0,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_out_main_div2_gpll0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_usb_aux_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 usb0_aux_clk_src = {
	.cmd_rcgr = 0x3e05c,
	.freq_tbl = ftbl_usb_aux_clk_src,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_sleep_clk_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "usb0_aux_clk_src",
		.parent_data = gcc_xo_gpll0_sleep_clk,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_sleep_clk),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_usb_mock_utmi_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(20000000, P_GPLL6, 6, 1, 9),
	F(60000000, P_GPLL6, 6, 1, 3),
	{ }
};

static const struct clk_parent_data gcc_xo_gpll6_gpll0_gpll0_out_main_div2[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .hw = &gpll6.clkr.hw },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_main_div2.hw },
};

static const struct parent_map gcc_xo_gpll6_gpll0_gpll0_out_main_div2_map[] = {
	{ P_XO, 0 },
	{ P_GPLL6, 1 },
	{ P_GPLL0, 3 },
	{ P_GPLL0_DIV2, 4 },
};

static struct clk_rcg2 usb0_mock_utmi_clk_src = {
	.cmd_rcgr = 0x3e020,
	.freq_tbl = ftbl_usb_mock_utmi_clk_src,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll6_gpll0_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "usb0_mock_utmi_clk_src",
		.parent_data = gcc_xo_gpll6_gpll0_gpll0_out_main_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll6_gpll0_gpll0_out_main_div2),
		.ops = &clk_rcg2_ops,
	},
};

static const struct clk_parent_data gcc_usb3phy_0_cc_pipe_clk_xo[] = {
	{ .fw_name = "usb3phy_0_cc_pipe_clk", .name = "usb3phy_0_cc_pipe_clk" },
	{ .fw_name = "xo", .name = "xo" },
};

static const struct parent_map gcc_usb3phy_0_cc_pipe_clk_xo_map[] = {
	{ P_USB3PHY_0_PIPE, 0 },
	{ P_XO, 2 },
};

static struct clk_regmap_mux usb0_pipe_clk_src = {
	.reg = 0x3e048,
	.shift = 8,
	.width = 2,
	.parent_map = gcc_usb3phy_0_cc_pipe_clk_xo_map,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "usb0_pipe_clk_src",
			.parent_data = gcc_usb3phy_0_cc_pipe_clk_xo,
			.num_parents = ARRAY_SIZE(gcc_usb3phy_0_cc_pipe_clk_xo),
			.ops = &clk_regmap_mux_closest_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg2 usb1_master_clk_src = {
	.cmd_rcgr = 0x3f00c,
	.freq_tbl = ftbl_usb_master_clk_src,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_out_main_div2_gpll0_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "usb1_master_clk_src",
		.parent_data = gcc_xo_gpll0_out_main_div2_gpll0,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_out_main_div2_gpll0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 usb1_aux_clk_src = {
	.cmd_rcgr = 0x3f05c,
	.freq_tbl = ftbl_usb_aux_clk_src,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_sleep_clk_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "usb1_aux_clk_src",
		.parent_data = gcc_xo_gpll0_sleep_clk,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_sleep_clk),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 usb1_mock_utmi_clk_src = {
	.cmd_rcgr = 0x3f020,
	.freq_tbl = ftbl_usb_mock_utmi_clk_src,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll6_gpll0_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "usb1_mock_utmi_clk_src",
		.parent_data = gcc_xo_gpll6_gpll0_gpll0_out_main_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll6_gpll0_gpll0_out_main_div2),
		.ops = &clk_rcg2_ops,
	},
};

static const struct clk_parent_data gcc_usb3phy_1_cc_pipe_clk_xo[] = {
	{ .fw_name = "usb3phy_1_cc_pipe_clk", .name = "usb3phy_1_cc_pipe_clk" },
	{ .fw_name = "xo", .name = "xo" },
};

static const struct parent_map gcc_usb3phy_1_cc_pipe_clk_xo_map[] = {
	{ P_USB3PHY_1_PIPE, 0 },
	{ P_XO, 2 },
};

static struct clk_regmap_mux usb1_pipe_clk_src = {
	.reg = 0x3f048,
	.shift = 8,
	.width = 2,
	.parent_map = gcc_usb3phy_1_cc_pipe_clk_xo_map,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "usb1_pipe_clk_src",
			.parent_data = gcc_usb3phy_1_cc_pipe_clk_xo,
			.num_parents = ARRAY_SIZE(gcc_usb3phy_1_cc_pipe_clk_xo),
			.ops = &clk_regmap_mux_closest_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch gcc_xo_clk_src = {
	.halt_reg = 0x30018,
	.clkr = {
		.enable_reg = 0x30018,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_xo_clk_src",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo",
				.name = "xo",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_fixed_factor gcc_xo_div4_clk_src = {
	.mult = 1,
	.div = 4,
	.hw.init = &(struct clk_init_data){
		.name = "gcc_xo_div4_clk_src",
		.parent_hws = (const struct clk_hw *[]){
				&gcc_xo_clk_src.clkr.hw },
		.num_parents = 1,
		.ops = &clk_fixed_factor_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct freq_tbl ftbl_system_noc_bfdcd_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(50000000, P_GPLL0_DIV2, 8, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	F(133333333, P_GPLL0, 6, 0, 0),
	F(160000000, P_GPLL0, 5, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266666667, P_GPLL0, 3, 0, 0),
	{ }
};

static const struct clk_parent_data gcc_xo_gpll0_gpll6_gpll0_out_main_div2[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll6.clkr.hw },
	{ .hw = &gpll0_out_main_div2.hw },
};

static const struct parent_map gcc_xo_gpll0_gpll6_gpll0_out_main_div2_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL6, 2 },
	{ P_GPLL0_DIV2, 3 },
};

static struct clk_rcg2 system_noc_bfdcd_clk_src = {
	.cmd_rcgr = 0x26004,
	.freq_tbl = ftbl_system_noc_bfdcd_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll6_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "system_noc_bfdcd_clk_src",
		.parent_data = gcc_xo_gpll0_gpll6_gpll0_out_main_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll6_gpll0_out_main_div2),
		.ops = &clk_rcg2_ops,
		.flags = CLK_IS_CRITICAL,
	},
};

static struct clk_fixed_factor system_noc_clk_src = {
	.mult = 1,
	.div = 1,
	.hw.init = &(struct clk_init_data){
		.name = "system_noc_clk_src",
		.parent_hws = (const struct clk_hw *[]){
				&system_noc_bfdcd_clk_src.clkr.hw },
		.num_parents = 1,
		.ops = &clk_fixed_factor_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct freq_tbl ftbl_nss_ce_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	{ }
};

static struct clk_rcg2 nss_ce_clk_src = {
	.cmd_rcgr = 0x68098,
	.freq_tbl = ftbl_nss_ce_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "nss_ce_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_nss_noc_bfdcd_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(461500000, P_BIAS_PLL_NSS_NOC, 1, 0, 0),
	{ }
};

static const struct clk_parent_data gcc_xo_bias_pll_nss_noc_clk_gpll0_gpll2[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .fw_name = "bias_pll_nss_noc_clk", .name = "bias_pll_nss_noc_clk" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll2.clkr.hw },
};

static const struct parent_map gcc_xo_bias_pll_nss_noc_clk_gpll0_gpll2_map[] = {
	{ P_XO, 0 },
	{ P_BIAS_PLL_NSS_NOC, 1 },
	{ P_GPLL0, 2 },
	{ P_GPLL2, 3 },
};

static struct clk_rcg2 nss_noc_bfdcd_clk_src = {
	.cmd_rcgr = 0x68088,
	.freq_tbl = ftbl_nss_noc_bfdcd_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_bias_pll_nss_noc_clk_gpll0_gpll2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "nss_noc_bfdcd_clk_src",
		.parent_data = gcc_xo_bias_pll_nss_noc_clk_gpll0_gpll2,
		.num_parents = ARRAY_SIZE(gcc_xo_bias_pll_nss_noc_clk_gpll0_gpll2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_fixed_factor nss_noc_clk_src = {
	.mult = 1,
	.div = 1,
	.hw.init = &(struct clk_init_data){
		.name = "nss_noc_clk_src",
		.parent_hws = (const struct clk_hw *[]){
				&nss_noc_bfdcd_clk_src.clkr.hw },
		.num_parents = 1,
		.ops = &clk_fixed_factor_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct freq_tbl ftbl_nss_crypto_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(600000000, P_NSS_CRYPTO_PLL, 1, 0, 0),
	{ }
};

static const struct clk_parent_data gcc_xo_nss_crypto_pll_gpll0[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .hw = &nss_crypto_pll.clkr.hw },
	{ .hw = &gpll0.clkr.hw },
};

static const struct parent_map gcc_xo_nss_crypto_pll_gpll0_map[] = {
	{ P_XO, 0 },
	{ P_NSS_CRYPTO_PLL, 1 },
	{ P_GPLL0, 2 },
};

static struct clk_rcg2 nss_crypto_clk_src = {
	.cmd_rcgr = 0x68144,
	.freq_tbl = ftbl_nss_crypto_clk_src,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_xo_nss_crypto_pll_gpll0_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "nss_crypto_clk_src",
		.parent_data = gcc_xo_nss_crypto_pll_gpll0,
		.num_parents = ARRAY_SIZE(gcc_xo_nss_crypto_pll_gpll0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_nss_ubi_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(187200000, P_UBI32_PLL, 8, 0, 0),
	F(748800000, P_UBI32_PLL, 2, 0, 0),
	F(1497600000, P_UBI32_PLL, 1, 0, 0),
	F(1689600000, P_UBI32_PLL, 1, 0, 0),
	{ }
};

static const struct clk_parent_data gcc_xo_ubi32_pll_gpll0_gpll2_gpll4_gpll6[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .hw = &ubi32_pll.clkr.hw },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll2.clkr.hw },
	{ .hw = &gpll4.clkr.hw },
	{ .hw = &gpll6.clkr.hw },
};

static const struct parent_map gcc_xo_ubi32_gpll0_gpll2_gpll4_gpll6_map[] = {
	{ P_XO, 0 },
	{ P_UBI32_PLL, 1 },
	{ P_GPLL0, 2 },
	{ P_GPLL2, 3 },
	{ P_GPLL4, 4 },
	{ P_GPLL6, 5 },
};

static struct clk_rcg2 nss_ubi0_clk_src = {
	.cmd_rcgr = 0x68104,
	.freq_tbl = ftbl_nss_ubi_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_ubi32_gpll0_gpll2_gpll4_gpll6_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "nss_ubi0_clk_src",
		.parent_data = gcc_xo_ubi32_pll_gpll0_gpll2_gpll4_gpll6,
		.num_parents = ARRAY_SIZE(gcc_xo_ubi32_pll_gpll0_gpll2_gpll4_gpll6),
		.ops = &clk_rcg2_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap_div nss_ubi0_div_clk_src = {
	.reg = 0x68118,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "nss_ubi0_div_clk_src",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ubi0_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_regmap_div_ro_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg2 nss_ubi1_clk_src = {
	.cmd_rcgr = 0x68124,
	.freq_tbl = ftbl_nss_ubi_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_ubi32_gpll0_gpll2_gpll4_gpll6_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "nss_ubi1_clk_src",
		.parent_data = gcc_xo_ubi32_pll_gpll0_gpll2_gpll4_gpll6,
		.num_parents = ARRAY_SIZE(gcc_xo_ubi32_pll_gpll0_gpll2_gpll4_gpll6),
		.ops = &clk_rcg2_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap_div nss_ubi1_div_clk_src = {
	.reg = 0x68138,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "nss_ubi1_div_clk_src",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ubi1_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_regmap_div_ro_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl ftbl_ubi_mpt_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(25000000, P_GPLL0_DIV2, 16, 0, 0),
	{ }
};

static const struct clk_parent_data gcc_xo_gpll0_out_main_div2[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .hw = &gpll0_out_main_div2.hw },
};

static const struct parent_map gcc_xo_gpll0_out_main_div2_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0_DIV2, 1 },
};

static struct clk_rcg2 ubi_mpt_clk_src = {
	.cmd_rcgr = 0x68090,
	.freq_tbl = ftbl_ubi_mpt_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "ubi_mpt_clk_src",
		.parent_data = gcc_xo_gpll0_out_main_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_out_main_div2),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_nss_imem_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	{ }
};

static const struct clk_parent_data gcc_xo_gpll0_gpll4[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll4.clkr.hw },
};

static const struct parent_map gcc_xo_gpll0_gpll4_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL4, 2 },
};

static struct clk_rcg2 nss_imem_clk_src = {
	.cmd_rcgr = 0x68158,
	.freq_tbl = ftbl_nss_imem_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll4_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "nss_imem_clk_src",
		.parent_data = gcc_xo_gpll0_gpll4,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll4),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_nss_ppe_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(300000000, P_BIAS_PLL, 1, 0, 0),
	{ }
};

static const struct clk_parent_data gcc_xo_bias_gpll0_gpll4_nss_ubi32[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .fw_name = "bias_pll_cc_clk", .name = "bias_pll_cc_clk" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll4.clkr.hw },
	{ .hw = &nss_crypto_pll.clkr.hw },
	{ .hw = &ubi32_pll.clkr.hw },
};

static const struct parent_map gcc_xo_bias_gpll0_gpll4_nss_ubi32_map[] = {
	{ P_XO, 0 },
	{ P_BIAS_PLL, 1 },
	{ P_GPLL0, 2 },
	{ P_GPLL4, 3 },
	{ P_NSS_CRYPTO_PLL, 4 },
	{ P_UBI32_PLL, 5 },
};

static struct clk_rcg2 nss_ppe_clk_src = {
	.cmd_rcgr = 0x68080,
	.freq_tbl = ftbl_nss_ppe_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_bias_gpll0_gpll4_nss_ubi32_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "nss_ppe_clk_src",
		.parent_data = gcc_xo_bias_gpll0_gpll4_nss_ubi32,
		.num_parents = ARRAY_SIZE(gcc_xo_bias_gpll0_gpll4_nss_ubi32),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_fixed_factor nss_ppe_cdiv_clk_src = {
	.mult = 1,
	.div = 4,
	.hw.init = &(struct clk_init_data){
		.name = "nss_ppe_cdiv_clk_src",
		.parent_hws = (const struct clk_hw *[]){
				&nss_ppe_clk_src.clkr.hw },
		.num_parents = 1,
		.ops = &clk_fixed_factor_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct freq_tbl ftbl_nss_port1_rx_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(25000000, P_UNIPHY0_RX, 5, 0, 0),
	F(125000000, P_UNIPHY0_RX, 1, 0, 0),
	{ }
};

static const struct clk_parent_data gcc_xo_uniphy0_rx_tx_ubi32_bias[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .fw_name = "uniphy0_gcc_rx_clk", .name = "uniphy0_gcc_rx_clk" },
	{ .fw_name = "uniphy0_gcc_tx_clk", .name = "uniphy0_gcc_tx_clk" },
	{ .hw = &ubi32_pll.clkr.hw },
	{ .fw_name = "bias_pll_cc_clk", .name = "bias_pll_cc_clk" },
};

static const struct parent_map gcc_xo_uniphy0_rx_tx_ubi32_bias_map[] = {
	{ P_XO, 0 },
	{ P_UNIPHY0_RX, 1 },
	{ P_UNIPHY0_TX, 2 },
	{ P_UBI32_PLL, 5 },
	{ P_BIAS_PLL, 6 },
};

static struct clk_rcg2 nss_port1_rx_clk_src = {
	.cmd_rcgr = 0x68020,
	.freq_tbl = ftbl_nss_port1_rx_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_uniphy0_rx_tx_ubi32_bias_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "nss_port1_rx_clk_src",
		.parent_data = gcc_xo_uniphy0_rx_tx_ubi32_bias,
		.num_parents = ARRAY_SIZE(gcc_xo_uniphy0_rx_tx_ubi32_bias),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div nss_port1_rx_div_clk_src = {
	.reg = 0x68400,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "nss_port1_rx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port1_rx_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_regmap_div_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl ftbl_nss_port1_tx_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(25000000, P_UNIPHY0_TX, 5, 0, 0),
	F(125000000, P_UNIPHY0_TX, 1, 0, 0),
	{ }
};

static const struct clk_parent_data gcc_xo_uniphy0_tx_rx_ubi32_bias[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .fw_name = "uniphy0_gcc_tx_clk", .name = "uniphy0_gcc_tx_clk" },
	{ .fw_name = "uniphy0_gcc_rx_clk", .name = "uniphy0_gcc_rx_clk" },
	{ .hw = &ubi32_pll.clkr.hw },
	{ .fw_name = "bias_pll_cc_clk", .name = "bias_pll_cc_clk" },
};

static const struct parent_map gcc_xo_uniphy0_tx_rx_ubi32_bias_map[] = {
	{ P_XO, 0 },
	{ P_UNIPHY0_TX, 1 },
	{ P_UNIPHY0_RX, 2 },
	{ P_UBI32_PLL, 5 },
	{ P_BIAS_PLL, 6 },
};

static struct clk_rcg2 nss_port1_tx_clk_src = {
	.cmd_rcgr = 0x68028,
	.freq_tbl = ftbl_nss_port1_tx_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_uniphy0_tx_rx_ubi32_bias_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "nss_port1_tx_clk_src",
		.parent_data = gcc_xo_uniphy0_tx_rx_ubi32_bias,
		.num_parents = ARRAY_SIZE(gcc_xo_uniphy0_tx_rx_ubi32_bias),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div nss_port1_tx_div_clk_src = {
	.reg = 0x68404,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "nss_port1_tx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port1_tx_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_regmap_div_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg2 nss_port2_rx_clk_src = {
	.cmd_rcgr = 0x68030,
	.freq_tbl = ftbl_nss_port1_rx_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_uniphy0_rx_tx_ubi32_bias_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "nss_port2_rx_clk_src",
		.parent_data = gcc_xo_uniphy0_rx_tx_ubi32_bias,
		.num_parents = ARRAY_SIZE(gcc_xo_uniphy0_rx_tx_ubi32_bias),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div nss_port2_rx_div_clk_src = {
	.reg = 0x68410,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "nss_port2_rx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port2_rx_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_regmap_div_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg2 nss_port2_tx_clk_src = {
	.cmd_rcgr = 0x68038,
	.freq_tbl = ftbl_nss_port1_tx_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_uniphy0_tx_rx_ubi32_bias_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "nss_port2_tx_clk_src",
		.parent_data = gcc_xo_uniphy0_tx_rx_ubi32_bias,
		.num_parents = ARRAY_SIZE(gcc_xo_uniphy0_tx_rx_ubi32_bias),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div nss_port2_tx_div_clk_src = {
	.reg = 0x68414,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "nss_port2_tx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port2_tx_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_regmap_div_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg2 nss_port3_rx_clk_src = {
	.cmd_rcgr = 0x68040,
	.freq_tbl = ftbl_nss_port1_rx_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_uniphy0_rx_tx_ubi32_bias_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "nss_port3_rx_clk_src",
		.parent_data = gcc_xo_uniphy0_rx_tx_ubi32_bias,
		.num_parents = ARRAY_SIZE(gcc_xo_uniphy0_rx_tx_ubi32_bias),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div nss_port3_rx_div_clk_src = {
	.reg = 0x68420,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "nss_port3_rx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port3_rx_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_regmap_div_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg2 nss_port3_tx_clk_src = {
	.cmd_rcgr = 0x68048,
	.freq_tbl = ftbl_nss_port1_tx_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_uniphy0_tx_rx_ubi32_bias_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "nss_port3_tx_clk_src",
		.parent_data = gcc_xo_uniphy0_tx_rx_ubi32_bias,
		.num_parents = ARRAY_SIZE(gcc_xo_uniphy0_tx_rx_ubi32_bias),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div nss_port3_tx_div_clk_src = {
	.reg = 0x68424,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "nss_port3_tx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port3_tx_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_regmap_div_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg2 nss_port4_rx_clk_src = {
	.cmd_rcgr = 0x68050,
	.freq_tbl = ftbl_nss_port1_rx_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_uniphy0_rx_tx_ubi32_bias_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "nss_port4_rx_clk_src",
		.parent_data = gcc_xo_uniphy0_rx_tx_ubi32_bias,
		.num_parents = ARRAY_SIZE(gcc_xo_uniphy0_rx_tx_ubi32_bias),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div nss_port4_rx_div_clk_src = {
	.reg = 0x68430,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "nss_port4_rx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port4_rx_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_regmap_div_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg2 nss_port4_tx_clk_src = {
	.cmd_rcgr = 0x68058,
	.freq_tbl = ftbl_nss_port1_tx_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_uniphy0_tx_rx_ubi32_bias_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "nss_port4_tx_clk_src",
		.parent_data = gcc_xo_uniphy0_tx_rx_ubi32_bias,
		.num_parents = ARRAY_SIZE(gcc_xo_uniphy0_tx_rx_ubi32_bias),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div nss_port4_tx_div_clk_src = {
	.reg = 0x68434,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "nss_port4_tx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port4_tx_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_regmap_div_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_conf ftbl_nss_port5_rx_clk_src_25[] = {
	C(P_UNIPHY1_RX, 12.5, 0, 0),
	C(P_UNIPHY0_RX, 5, 0, 0),
};

static const struct freq_conf ftbl_nss_port5_rx_clk_src_125[] = {
	C(P_UNIPHY1_RX, 2.5, 0, 0),
	C(P_UNIPHY0_RX, 1, 0, 0),
};

static const struct freq_multi_tbl ftbl_nss_port5_rx_clk_src[] = {
	FMS(19200000, P_XO, 1, 0, 0),
	FM(25000000, ftbl_nss_port5_rx_clk_src_25),
	FMS(78125000, P_UNIPHY1_RX, 4, 0, 0),
	FM(125000000, ftbl_nss_port5_rx_clk_src_125),
	FMS(156250000, P_UNIPHY1_RX, 2, 0, 0),
	FMS(312500000, P_UNIPHY1_RX, 1, 0, 0),
	{ }
};

static const struct clk_parent_data gcc_xo_uniphy0_rx_tx_uniphy1_rx_tx_ubi32_bias[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .fw_name = "uniphy0_gcc_rx_clk", .name = "uniphy0_gcc_rx_clk" },
	{ .fw_name = "uniphy0_gcc_tx_clk", .name = "uniphy0_gcc_tx_clk" },
	{ .fw_name = "uniphy1_gcc_rx_clk", .name = "uniphy1_gcc_rx_clk" },
	{ .fw_name = "uniphy1_gcc_tx_clk", .name = "uniphy1_gcc_tx_clk" },
	{ .hw = &ubi32_pll.clkr.hw },
	{ .fw_name = "bias_pll_cc_clk", .name = "bias_pll_cc_clk" },
};

static const struct parent_map
gcc_xo_uniphy0_rx_tx_uniphy1_rx_tx_ubi32_bias_map[] = {
	{ P_XO, 0 },
	{ P_UNIPHY0_RX, 1 },
	{ P_UNIPHY0_TX, 2 },
	{ P_UNIPHY1_RX, 3 },
	{ P_UNIPHY1_TX, 4 },
	{ P_UBI32_PLL, 5 },
	{ P_BIAS_PLL, 6 },
};

static struct clk_rcg2 nss_port5_rx_clk_src = {
	.cmd_rcgr = 0x68060,
	.freq_multi_tbl = ftbl_nss_port5_rx_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_uniphy0_rx_tx_uniphy1_rx_tx_ubi32_bias_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "nss_port5_rx_clk_src",
		.parent_data = gcc_xo_uniphy0_rx_tx_uniphy1_rx_tx_ubi32_bias,
		.num_parents = ARRAY_SIZE(gcc_xo_uniphy0_rx_tx_uniphy1_rx_tx_ubi32_bias),
		.ops = &clk_rcg2_fm_ops,
	},
};

static struct clk_regmap_div nss_port5_rx_div_clk_src = {
	.reg = 0x68440,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "nss_port5_rx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port5_rx_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_regmap_div_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_conf ftbl_nss_port5_tx_clk_src_25[] = {
	C(P_UNIPHY1_TX, 12.5, 0, 0),
	C(P_UNIPHY0_TX, 5, 0, 0),
};

static const struct freq_conf ftbl_nss_port5_tx_clk_src_125[] = {
	C(P_UNIPHY1_TX, 2.5, 0, 0),
	C(P_UNIPHY0_TX, 1, 0, 0),
};

static const struct freq_multi_tbl ftbl_nss_port5_tx_clk_src[] = {
	FMS(19200000, P_XO, 1, 0, 0),
	FM(25000000, ftbl_nss_port5_tx_clk_src_25),
	FMS(78125000, P_UNIPHY1_TX, 4, 0, 0),
	FM(125000000, ftbl_nss_port5_tx_clk_src_125),
	FMS(156250000, P_UNIPHY1_TX, 2, 0, 0),
	FMS(312500000, P_UNIPHY1_TX, 1, 0, 0),
	{ }
};

static const struct clk_parent_data gcc_xo_uniphy0_tx_rx_uniphy1_tx_rx_ubi32_bias[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .fw_name = "uniphy0_gcc_tx_clk", .name = "uniphy0_gcc_tx_clk" },
	{ .fw_name = "uniphy0_gcc_rx_clk", .name = "uniphy0_gcc_rx_clk" },
	{ .fw_name = "uniphy1_gcc_tx_clk", .name = "uniphy1_gcc_tx_clk" },
	{ .fw_name = "uniphy1_gcc_rx_clk", .name = "uniphy1_gcc_rx_clk" },
	{ .hw = &ubi32_pll.clkr.hw },
	{ .fw_name = "bias_pll_cc_clk", .name = "bias_pll_cc_clk" },
};

static const struct parent_map
gcc_xo_uniphy0_tx_rx_uniphy1_tx_rx_ubi32_bias_map[] = {
	{ P_XO, 0 },
	{ P_UNIPHY0_TX, 1 },
	{ P_UNIPHY0_RX, 2 },
	{ P_UNIPHY1_TX, 3 },
	{ P_UNIPHY1_RX, 4 },
	{ P_UBI32_PLL, 5 },
	{ P_BIAS_PLL, 6 },
};

static struct clk_rcg2 nss_port5_tx_clk_src = {
	.cmd_rcgr = 0x68068,
	.freq_multi_tbl = ftbl_nss_port5_tx_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_uniphy0_tx_rx_uniphy1_tx_rx_ubi32_bias_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "nss_port5_tx_clk_src",
		.parent_data = gcc_xo_uniphy0_tx_rx_uniphy1_tx_rx_ubi32_bias,
		.num_parents = ARRAY_SIZE(gcc_xo_uniphy0_tx_rx_uniphy1_tx_rx_ubi32_bias),
		.ops = &clk_rcg2_fm_ops,
	},
};

static struct clk_regmap_div nss_port5_tx_div_clk_src = {
	.reg = 0x68444,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "nss_port5_tx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port5_tx_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_regmap_div_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_conf ftbl_nss_port6_rx_clk_src_25[] = {
	C(P_UNIPHY2_RX, 5, 0, 0),
	C(P_UNIPHY2_RX, 12.5, 0, 0),
};

static const struct freq_conf ftbl_nss_port6_rx_clk_src_125[] = {
	C(P_UNIPHY2_RX, 1, 0, 0),
	C(P_UNIPHY2_RX, 2.5, 0, 0),
};

static const struct freq_multi_tbl ftbl_nss_port6_rx_clk_src[] = {
	FMS(19200000, P_XO, 1, 0, 0),
	FM(25000000, ftbl_nss_port6_rx_clk_src_25),
	FMS(78125000, P_UNIPHY2_RX, 4, 0, 0),
	FM(125000000, ftbl_nss_port6_rx_clk_src_125),
	FMS(156250000, P_UNIPHY2_RX, 2, 0, 0),
	FMS(312500000, P_UNIPHY2_RX, 1, 0, 0),
	{ }
};

static const struct clk_parent_data gcc_xo_uniphy2_rx_tx_ubi32_bias[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .fw_name = "uniphy2_gcc_rx_clk", .name = "uniphy2_gcc_rx_clk" },
	{ .fw_name = "uniphy2_gcc_tx_clk", .name = "uniphy2_gcc_tx_clk" },
	{ .hw = &ubi32_pll.clkr.hw },
	{ .fw_name = "bias_pll_cc_clk", .name = "bias_pll_cc_clk" },
};

static const struct parent_map gcc_xo_uniphy2_rx_tx_ubi32_bias_map[] = {
	{ P_XO, 0 },
	{ P_UNIPHY2_RX, 1 },
	{ P_UNIPHY2_TX, 2 },
	{ P_UBI32_PLL, 5 },
	{ P_BIAS_PLL, 6 },
};

static struct clk_rcg2 nss_port6_rx_clk_src = {
	.cmd_rcgr = 0x68070,
	.freq_multi_tbl = ftbl_nss_port6_rx_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_uniphy2_rx_tx_ubi32_bias_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "nss_port6_rx_clk_src",
		.parent_data = gcc_xo_uniphy2_rx_tx_ubi32_bias,
		.num_parents = ARRAY_SIZE(gcc_xo_uniphy2_rx_tx_ubi32_bias),
		.ops = &clk_rcg2_fm_ops,
	},
};

static struct clk_regmap_div nss_port6_rx_div_clk_src = {
	.reg = 0x68450,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "nss_port6_rx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port6_rx_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_regmap_div_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_conf ftbl_nss_port6_tx_clk_src_25[] = {
	C(P_UNIPHY2_TX, 5, 0, 0),
	C(P_UNIPHY2_TX, 12.5, 0, 0),
};

static const struct freq_conf ftbl_nss_port6_tx_clk_src_125[] = {
	C(P_UNIPHY2_TX, 1, 0, 0),
	C(P_UNIPHY2_TX, 2.5, 0, 0),
};

static const struct freq_multi_tbl ftbl_nss_port6_tx_clk_src[] = {
	FMS(19200000, P_XO, 1, 0, 0),
	FM(25000000, ftbl_nss_port6_tx_clk_src_25),
	FMS(78125000, P_UNIPHY2_TX, 4, 0, 0),
	FM(125000000, ftbl_nss_port6_tx_clk_src_125),
	FMS(156250000, P_UNIPHY2_TX, 2, 0, 0),
	FMS(312500000, P_UNIPHY2_TX, 1, 0, 0),
	{ }
};

static const struct clk_parent_data gcc_xo_uniphy2_tx_rx_ubi32_bias[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .fw_name = "uniphy2_gcc_tx_clk", .name = "uniphy2_gcc_tx_clk" },
	{ .fw_name = "uniphy2_gcc_rx_clk", .name = "uniphy2_gcc_rx_clk" },
	{ .hw = &ubi32_pll.clkr.hw },
	{ .fw_name = "bias_pll_cc_clk", .name = "bias_pll_cc_clk" },
};

static const struct parent_map gcc_xo_uniphy2_tx_rx_ubi32_bias_map[] = {
	{ P_XO, 0 },
	{ P_UNIPHY2_TX, 1 },
	{ P_UNIPHY2_RX, 2 },
	{ P_UBI32_PLL, 5 },
	{ P_BIAS_PLL, 6 },
};

static struct clk_rcg2 nss_port6_tx_clk_src = {
	.cmd_rcgr = 0x68078,
	.freq_multi_tbl = ftbl_nss_port6_tx_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_uniphy2_tx_rx_ubi32_bias_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "nss_port6_tx_clk_src",
		.parent_data = gcc_xo_uniphy2_tx_rx_ubi32_bias,
		.num_parents = ARRAY_SIZE(gcc_xo_uniphy2_tx_rx_ubi32_bias),
		.ops = &clk_rcg2_fm_ops,
	},
};

static struct clk_regmap_div nss_port6_tx_div_clk_src = {
	.reg = 0x68454,
	.shift = 0,
	.width = 4,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "nss_port6_tx_div_clk_src",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port6_tx_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_regmap_div_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl ftbl_crypto_clk_src[] = {
	F(40000000, P_GPLL0_DIV2, 10, 0, 0),
	F(80000000, P_GPLL0, 10, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	F(160000000, P_GPLL0, 5, 0, 0),
	{ }
};

static struct clk_rcg2 crypto_clk_src = {
	.cmd_rcgr = 0x16004,
	.freq_tbl = ftbl_crypto_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "crypto_clk_src",
		.parent_data = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll0_out_main_div2),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gp_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static const struct clk_parent_data gcc_xo_gpll0_gpll6_gpll0_sleep_clk[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll6.clkr.hw },
	{ .hw = &gpll0_out_main_div2.hw },
	{ .fw_name = "sleep_clk", .name = "sleep_clk" },
};

static const struct parent_map gcc_xo_gpll0_gpll6_gpll0_sleep_clk_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL6, 2 },
	{ P_GPLL0_DIV2, 4 },
	{ P_SLEEP_CLK, 6 },
};

static struct clk_rcg2 gp1_clk_src = {
	.cmd_rcgr = 0x08004,
	.freq_tbl = ftbl_gp_clk_src,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll6_gpll0_sleep_clk_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gp1_clk_src",
		.parent_data = gcc_xo_gpll0_gpll6_gpll0_sleep_clk,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll6_gpll0_sleep_clk),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gp2_clk_src = {
	.cmd_rcgr = 0x09004,
	.freq_tbl = ftbl_gp_clk_src,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll6_gpll0_sleep_clk_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gp2_clk_src",
		.parent_data = gcc_xo_gpll0_gpll6_gpll0_sleep_clk,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll6_gpll0_sleep_clk),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gp3_clk_src = {
	.cmd_rcgr = 0x0a004,
	.freq_tbl = ftbl_gp_clk_src,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_gpll6_gpll0_sleep_clk_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gp3_clk_src",
		.parent_data = gcc_xo_gpll0_gpll6_gpll0_sleep_clk,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0_gpll6_gpll0_sleep_clk),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_blsp1_ahb_clk = {
	.halt_reg = 0x01008,
	.clkr = {
		.enable_reg = 0x01008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
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
			.parent_hws = (const struct clk_hw *[]){
				&blsp1_qup1_i2c_apps_clk_src.clkr.hw },
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
			.parent_hws = (const struct clk_hw *[]){
				&blsp1_qup1_spi_apps_clk_src.clkr.hw },
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
			.parent_hws = (const struct clk_hw *[]){
				&blsp1_qup2_i2c_apps_clk_src.clkr.hw },
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
			.parent_hws = (const struct clk_hw *[]){
				&blsp1_qup2_spi_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup3_i2c_apps_clk = {
	.halt_reg = 0x04010,
	.clkr = {
		.enable_reg = 0x04010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup3_i2c_apps_clk",
			.parent_hws = (const struct clk_hw *[]){
				&blsp1_qup3_i2c_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup3_spi_apps_clk = {
	.halt_reg = 0x0400c,
	.clkr = {
		.enable_reg = 0x0400c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup3_spi_apps_clk",
			.parent_hws = (const struct clk_hw *[]){
				&blsp1_qup3_spi_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup4_i2c_apps_clk = {
	.halt_reg = 0x05010,
	.clkr = {
		.enable_reg = 0x05010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup4_i2c_apps_clk",
			.parent_hws = (const struct clk_hw *[]){
				&blsp1_qup4_i2c_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup4_spi_apps_clk = {
	.halt_reg = 0x0500c,
	.clkr = {
		.enable_reg = 0x0500c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup4_spi_apps_clk",
			.parent_hws = (const struct clk_hw *[]){
				&blsp1_qup4_spi_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup5_i2c_apps_clk = {
	.halt_reg = 0x06010,
	.clkr = {
		.enable_reg = 0x06010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup5_i2c_apps_clk",
			.parent_hws = (const struct clk_hw *[]){
				&blsp1_qup5_i2c_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup5_spi_apps_clk = {
	.halt_reg = 0x0600c,
	.clkr = {
		.enable_reg = 0x0600c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup5_spi_apps_clk",
			.parent_hws = (const struct clk_hw *[]){
				&blsp1_qup5_spi_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup6_i2c_apps_clk = {
	.halt_reg = 0x07010,
	.clkr = {
		.enable_reg = 0x07010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup6_i2c_apps_clk",
			.parent_hws = (const struct clk_hw *[]){
				&blsp1_qup6_i2c_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup6_spi_apps_clk = {
	.halt_reg = 0x0700c,
	.clkr = {
		.enable_reg = 0x0700c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup6_spi_apps_clk",
			.parent_hws = (const struct clk_hw *[]){
				&blsp1_qup6_spi_apps_clk_src.clkr.hw },
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
			.parent_hws = (const struct clk_hw *[]){
				&blsp1_uart1_apps_clk_src.clkr.hw },
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
			.parent_hws = (const struct clk_hw *[]){
				&blsp1_uart2_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart3_apps_clk = {
	.halt_reg = 0x0402c,
	.clkr = {
		.enable_reg = 0x0402c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart3_apps_clk",
			.parent_hws = (const struct clk_hw *[]){
				&blsp1_uart3_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart4_apps_clk = {
	.halt_reg = 0x0502c,
	.clkr = {
		.enable_reg = 0x0502c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart4_apps_clk",
			.parent_hws = (const struct clk_hw *[]){
				&blsp1_uart4_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart5_apps_clk = {
	.halt_reg = 0x0602c,
	.clkr = {
		.enable_reg = 0x0602c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart5_apps_clk",
			.parent_hws = (const struct clk_hw *[]){
				&blsp1_uart5_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart6_apps_clk = {
	.halt_reg = 0x0702c,
	.clkr = {
		.enable_reg = 0x0702c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart6_apps_clk",
			.parent_hws = (const struct clk_hw *[]){
				&blsp1_uart6_apps_clk_src.clkr.hw },
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
		.enable_reg = 0x0b004,
		.enable_mask = BIT(8),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_prng_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qpic_ahb_clk = {
	.halt_reg = 0x57024,
	.clkr = {
		.enable_reg = 0x57024,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qpic_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qpic_clk = {
	.halt_reg = 0x57020,
	.clkr = {
		.enable_reg = 0x57020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qpic_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie0_ahb_clk = {
	.halt_reg = 0x75010,
	.clkr = {
		.enable_reg = 0x75010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie0_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie0_aux_clk = {
	.halt_reg = 0x75014,
	.clkr = {
		.enable_reg = 0x75014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie0_aux_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcie0_aux_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie0_axi_m_clk = {
	.halt_reg = 0x75008,
	.clkr = {
		.enable_reg = 0x75008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie0_axi_m_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcie0_axi_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie0_axi_s_clk = {
	.halt_reg = 0x7500c,
	.clkr = {
		.enable_reg = 0x7500c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie0_axi_s_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcie0_axi_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie0_pipe_clk = {
	.halt_reg = 0x75018,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x75018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie0_pipe_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcie0_pipe_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sys_noc_pcie0_axi_clk = {
	.halt_reg = 0x26048,
	.clkr = {
		.enable_reg = 0x26048,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sys_noc_pcie0_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcie0_axi_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie1_ahb_clk = {
	.halt_reg = 0x76010,
	.clkr = {
		.enable_reg = 0x76010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie1_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie1_aux_clk = {
	.halt_reg = 0x76014,
	.clkr = {
		.enable_reg = 0x76014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie1_aux_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcie1_aux_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie1_axi_m_clk = {
	.halt_reg = 0x76008,
	.clkr = {
		.enable_reg = 0x76008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie1_axi_m_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcie1_axi_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie1_axi_s_clk = {
	.halt_reg = 0x7600c,
	.clkr = {
		.enable_reg = 0x7600c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie1_axi_s_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcie1_axi_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie1_pipe_clk = {
	.halt_reg = 0x76018,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x76018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie1_pipe_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcie1_pipe_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sys_noc_pcie1_axi_clk = {
	.halt_reg = 0x2604c,
	.clkr = {
		.enable_reg = 0x2604c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sys_noc_pcie1_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcie1_axi_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb0_aux_clk = {
	.halt_reg = 0x3e044,
	.clkr = {
		.enable_reg = 0x3e044,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb0_aux_clk",
			.parent_hws = (const struct clk_hw *[]){
				&usb0_aux_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sys_noc_usb0_axi_clk = {
	.halt_reg = 0x26040,
	.clkr = {
		.enable_reg = 0x26040,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sys_noc_usb0_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				&usb0_master_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb0_master_clk = {
	.halt_reg = 0x3e000,
	.clkr = {
		.enable_reg = 0x3e000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb0_master_clk",
			.parent_hws = (const struct clk_hw *[]){
				&usb0_master_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb0_mock_utmi_clk = {
	.halt_reg = 0x3e008,
	.clkr = {
		.enable_reg = 0x3e008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb0_mock_utmi_clk",
			.parent_hws = (const struct clk_hw *[]){
				&usb0_mock_utmi_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb0_phy_cfg_ahb_clk = {
	.halt_reg = 0x3e080,
	.clkr = {
		.enable_reg = 0x3e080,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb0_phy_cfg_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb0_pipe_clk = {
	.halt_reg = 0x3e040,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x3e040,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb0_pipe_clk",
			.parent_hws = (const struct clk_hw *[]){
				&usb0_pipe_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb0_sleep_clk = {
	.halt_reg = 0x3e004,
	.clkr = {
		.enable_reg = 0x3e004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb0_sleep_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_sleep_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb1_aux_clk = {
	.halt_reg = 0x3f044,
	.clkr = {
		.enable_reg = 0x3f044,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb1_aux_clk",
			.parent_hws = (const struct clk_hw *[]){
				&usb1_aux_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sys_noc_usb1_axi_clk = {
	.halt_reg = 0x26044,
	.clkr = {
		.enable_reg = 0x26044,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sys_noc_usb1_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				&usb1_master_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb1_master_clk = {
	.halt_reg = 0x3f000,
	.clkr = {
		.enable_reg = 0x3f000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb1_master_clk",
			.parent_hws = (const struct clk_hw *[]){
				&usb1_master_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb1_mock_utmi_clk = {
	.halt_reg = 0x3f008,
	.clkr = {
		.enable_reg = 0x3f008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb1_mock_utmi_clk",
			.parent_hws = (const struct clk_hw *[]){
				&usb1_mock_utmi_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb1_phy_cfg_ahb_clk = {
	.halt_reg = 0x3f080,
	.clkr = {
		.enable_reg = 0x3f080,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb1_phy_cfg_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb1_pipe_clk = {
	.halt_reg = 0x3f040,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x3f040,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb1_pipe_clk",
			.parent_hws = (const struct clk_hw *[]){
				&usb1_pipe_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb1_sleep_clk = {
	.halt_reg = 0x3f004,
	.clkr = {
		.enable_reg = 0x3f004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb1_sleep_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_sleep_clk_src.clkr.hw },
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
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.hw },
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
			.parent_hws = (const struct clk_hw *[]){
				&sdcc1_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_ice_core_clk = {
	.halt_reg = 0x5d014,
	.clkr = {
		.enable_reg = 0x5d014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc1_ice_core_clk",
			.parent_hws = (const struct clk_hw *[]){
				&sdcc1_ice_core_clk_src.clkr.hw },
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
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.hw },
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
			.parent_hws = (const struct clk_hw *[]){
				&sdcc2_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mem_noc_nss_axi_clk = {
	.halt_reg = 0x1d03c,
	.clkr = {
		.enable_reg = 0x1d03c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mem_noc_nss_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_noc_clk_src.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_ce_apb_clk = {
	.halt_reg = 0x68174,
	.clkr = {
		.enable_reg = 0x68174,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nss_ce_apb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ce_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_ce_axi_clk = {
	.halt_reg = 0x68170,
	.clkr = {
		.enable_reg = 0x68170,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nss_ce_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ce_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_cfg_clk = {
	.halt_reg = 0x68160,
	.clkr = {
		.enable_reg = 0x68160,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nss_cfg_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_crypto_clk = {
	.halt_reg = 0x68164,
	.clkr = {
		.enable_reg = 0x68164,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nss_crypto_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_crypto_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_csr_clk = {
	.halt_reg = 0x68318,
	.clkr = {
		.enable_reg = 0x68318,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nss_csr_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ce_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_edma_cfg_clk = {
	.halt_reg = 0x6819c,
	.clkr = {
		.enable_reg = 0x6819c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nss_edma_cfg_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ppe_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_edma_clk = {
	.halt_reg = 0x68198,
	.clkr = {
		.enable_reg = 0x68198,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nss_edma_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ppe_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_imem_clk = {
	.halt_reg = 0x68178,
	.clkr = {
		.enable_reg = 0x68178,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nss_imem_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_imem_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_noc_clk = {
	.halt_reg = 0x68168,
	.clkr = {
		.enable_reg = 0x68168,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nss_noc_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_noc_clk_src.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_ppe_btq_clk = {
	.halt_reg = 0x6833c,
	.clkr = {
		.enable_reg = 0x6833c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nss_ppe_btq_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ppe_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_ppe_cfg_clk = {
	.halt_reg = 0x68194,
	.clkr = {
		.enable_reg = 0x68194,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nss_ppe_cfg_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ppe_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_ppe_clk = {
	.halt_reg = 0x68190,
	.clkr = {
		.enable_reg = 0x68190,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nss_ppe_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ppe_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_ppe_ipe_clk = {
	.halt_reg = 0x68338,
	.clkr = {
		.enable_reg = 0x68338,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nss_ppe_ipe_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ppe_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_ptp_ref_clk = {
	.halt_reg = 0x6816c,
	.clkr = {
		.enable_reg = 0x6816c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nss_ptp_ref_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ppe_cdiv_clk_src.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_crypto_ppe_clk = {
	.halt_reg = 0x68310,
	.halt_bit = 31,
	.clkr = {
		.enable_reg = 0x68310,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_crypto_ppe_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ppe_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_ce_apb_clk = {
	.halt_reg = 0x6830c,
	.clkr = {
		.enable_reg = 0x6830c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nssnoc_ce_apb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ce_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_ce_axi_clk = {
	.halt_reg = 0x68308,
	.clkr = {
		.enable_reg = 0x68308,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nssnoc_ce_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ce_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_crypto_clk = {
	.halt_reg = 0x68314,
	.clkr = {
		.enable_reg = 0x68314,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nssnoc_crypto_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_crypto_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_ppe_cfg_clk = {
	.halt_reg = 0x68304,
	.clkr = {
		.enable_reg = 0x68304,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nssnoc_ppe_cfg_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ppe_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_ppe_clk = {
	.halt_reg = 0x68300,
	.clkr = {
		.enable_reg = 0x68300,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nssnoc_ppe_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ppe_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_qosgen_ref_clk = {
	.halt_reg = 0x68180,
	.clkr = {
		.enable_reg = 0x68180,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nssnoc_qosgen_ref_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_xo_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_snoc_clk = {
	.halt_reg = 0x68188,
	.clkr = {
		.enable_reg = 0x68188,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nssnoc_snoc_clk",
			.parent_hws = (const struct clk_hw *[]){
				&system_noc_clk_src.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_timeout_ref_clk = {
	.halt_reg = 0x68184,
	.clkr = {
		.enable_reg = 0x68184,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nssnoc_timeout_ref_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_xo_div4_clk_src.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_ubi0_ahb_clk = {
	.halt_reg = 0x68270,
	.clkr = {
		.enable_reg = 0x68270,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nssnoc_ubi0_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ce_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_ubi1_ahb_clk = {
	.halt_reg = 0x68274,
	.clkr = {
		.enable_reg = 0x68274,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nssnoc_ubi1_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ce_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ubi0_ahb_clk = {
	.halt_reg = 0x6820c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x6820c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ubi0_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ce_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ubi0_axi_clk = {
	.halt_reg = 0x68200,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x68200,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ubi0_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_noc_clk_src.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ubi0_nc_axi_clk = {
	.halt_reg = 0x68204,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x68204,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ubi0_nc_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_noc_clk_src.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ubi0_core_clk = {
	.halt_reg = 0x68210,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x68210,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ubi0_core_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ubi0_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ubi0_mpt_clk = {
	.halt_reg = 0x68208,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x68208,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ubi0_mpt_clk",
			.parent_hws = (const struct clk_hw *[]){
				&ubi_mpt_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ubi1_ahb_clk = {
	.halt_reg = 0x6822c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x6822c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ubi1_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ce_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ubi1_axi_clk = {
	.halt_reg = 0x68220,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x68220,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ubi1_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_noc_clk_src.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ubi1_nc_axi_clk = {
	.halt_reg = 0x68224,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x68224,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ubi1_nc_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_noc_clk_src.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ubi1_core_clk = {
	.halt_reg = 0x68230,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x68230,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ubi1_core_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ubi1_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ubi1_mpt_clk = {
	.halt_reg = 0x68228,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x68228,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ubi1_mpt_clk",
			.parent_hws = (const struct clk_hw *[]){
				&ubi_mpt_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cmn_12gpll_ahb_clk = {
	.halt_reg = 0x56308,
	.clkr = {
		.enable_reg = 0x56308,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_cmn_12gpll_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cmn_12gpll_sys_clk = {
	.halt_reg = 0x5630c,
	.clkr = {
		.enable_reg = 0x5630c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_cmn_12gpll_sys_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_xo_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdio_ahb_clk = {
	.halt_reg = 0x58004,
	.clkr = {
		.enable_reg = 0x58004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_mdio_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy0_ahb_clk = {
	.halt_reg = 0x56008,
	.clkr = {
		.enable_reg = 0x56008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_uniphy0_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy0_sys_clk = {
	.halt_reg = 0x5600c,
	.clkr = {
		.enable_reg = 0x5600c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_uniphy0_sys_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_xo_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy1_ahb_clk = {
	.halt_reg = 0x56108,
	.clkr = {
		.enable_reg = 0x56108,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_uniphy1_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy1_sys_clk = {
	.halt_reg = 0x5610c,
	.clkr = {
		.enable_reg = 0x5610c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_uniphy1_sys_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_xo_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy2_ahb_clk = {
	.halt_reg = 0x56208,
	.clkr = {
		.enable_reg = 0x56208,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_uniphy2_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy2_sys_clk = {
	.halt_reg = 0x5620c,
	.clkr = {
		.enable_reg = 0x5620c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_uniphy2_sys_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_xo_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_port1_rx_clk = {
	.halt_reg = 0x68240,
	.clkr = {
		.enable_reg = 0x68240,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nss_port1_rx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port1_rx_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_port1_tx_clk = {
	.halt_reg = 0x68244,
	.clkr = {
		.enable_reg = 0x68244,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nss_port1_tx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port1_tx_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_port2_rx_clk = {
	.halt_reg = 0x68248,
	.clkr = {
		.enable_reg = 0x68248,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nss_port2_rx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port2_rx_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_port2_tx_clk = {
	.halt_reg = 0x6824c,
	.clkr = {
		.enable_reg = 0x6824c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nss_port2_tx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port2_tx_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_port3_rx_clk = {
	.halt_reg = 0x68250,
	.clkr = {
		.enable_reg = 0x68250,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nss_port3_rx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port3_rx_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_port3_tx_clk = {
	.halt_reg = 0x68254,
	.clkr = {
		.enable_reg = 0x68254,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nss_port3_tx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port3_tx_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_port4_rx_clk = {
	.halt_reg = 0x68258,
	.clkr = {
		.enable_reg = 0x68258,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nss_port4_rx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port4_rx_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_port4_tx_clk = {
	.halt_reg = 0x6825c,
	.clkr = {
		.enable_reg = 0x6825c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nss_port4_tx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port4_tx_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_port5_rx_clk = {
	.halt_reg = 0x68260,
	.clkr = {
		.enable_reg = 0x68260,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nss_port5_rx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port5_rx_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_port5_tx_clk = {
	.halt_reg = 0x68264,
	.clkr = {
		.enable_reg = 0x68264,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nss_port5_tx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port5_tx_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_port6_rx_clk = {
	.halt_reg = 0x68268,
	.clkr = {
		.enable_reg = 0x68268,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nss_port6_rx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port6_rx_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_port6_tx_clk = {
	.halt_reg = 0x6826c,
	.clkr = {
		.enable_reg = 0x6826c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_nss_port6_tx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port6_tx_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_port1_mac_clk = {
	.halt_reg = 0x68320,
	.clkr = {
		.enable_reg = 0x68320,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_port1_mac_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ppe_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_port2_mac_clk = {
	.halt_reg = 0x68324,
	.clkr = {
		.enable_reg = 0x68324,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_port2_mac_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ppe_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_port3_mac_clk = {
	.halt_reg = 0x68328,
	.clkr = {
		.enable_reg = 0x68328,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_port3_mac_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ppe_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_port4_mac_clk = {
	.halt_reg = 0x6832c,
	.clkr = {
		.enable_reg = 0x6832c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_port4_mac_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ppe_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_port5_mac_clk = {
	.halt_reg = 0x68330,
	.clkr = {
		.enable_reg = 0x68330,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_port5_mac_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ppe_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_port6_mac_clk = {
	.halt_reg = 0x68334,
	.clkr = {
		.enable_reg = 0x68334,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_port6_mac_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_ppe_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy0_port1_rx_clk = {
	.halt_reg = 0x56010,
	.clkr = {
		.enable_reg = 0x56010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_uniphy0_port1_rx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port1_rx_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy0_port1_tx_clk = {
	.halt_reg = 0x56014,
	.clkr = {
		.enable_reg = 0x56014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_uniphy0_port1_tx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port1_tx_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy0_port2_rx_clk = {
	.halt_reg = 0x56018,
	.clkr = {
		.enable_reg = 0x56018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_uniphy0_port2_rx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port2_rx_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy0_port2_tx_clk = {
	.halt_reg = 0x5601c,
	.clkr = {
		.enable_reg = 0x5601c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_uniphy0_port2_tx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port2_tx_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy0_port3_rx_clk = {
	.halt_reg = 0x56020,
	.clkr = {
		.enable_reg = 0x56020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_uniphy0_port3_rx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port3_rx_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy0_port3_tx_clk = {
	.halt_reg = 0x56024,
	.clkr = {
		.enable_reg = 0x56024,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_uniphy0_port3_tx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port3_tx_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy0_port4_rx_clk = {
	.halt_reg = 0x56028,
	.clkr = {
		.enable_reg = 0x56028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_uniphy0_port4_rx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port4_rx_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy0_port4_tx_clk = {
	.halt_reg = 0x5602c,
	.clkr = {
		.enable_reg = 0x5602c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_uniphy0_port4_tx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port4_tx_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy0_port5_rx_clk = {
	.halt_reg = 0x56030,
	.clkr = {
		.enable_reg = 0x56030,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_uniphy0_port5_rx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port5_rx_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy0_port5_tx_clk = {
	.halt_reg = 0x56034,
	.clkr = {
		.enable_reg = 0x56034,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_uniphy0_port5_tx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port5_tx_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy1_port5_rx_clk = {
	.halt_reg = 0x56110,
	.clkr = {
		.enable_reg = 0x56110,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_uniphy1_port5_rx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port5_rx_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy1_port5_tx_clk = {
	.halt_reg = 0x56114,
	.clkr = {
		.enable_reg = 0x56114,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_uniphy1_port5_tx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port5_tx_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy2_port6_rx_clk = {
	.halt_reg = 0x56210,
	.clkr = {
		.enable_reg = 0x56210,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_uniphy2_port6_rx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port6_rx_div_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy2_port6_tx_clk = {
	.halt_reg = 0x56214,
	.clkr = {
		.enable_reg = 0x56214,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_uniphy2_port6_tx_clk",
			.parent_hws = (const struct clk_hw *[]){
				&nss_port6_tx_div_clk_src.clkr.hw },
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
		.enable_reg = 0x0b004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_crypto_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.hw },
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
		.enable_reg = 0x0b004,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_crypto_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.hw },
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
		.enable_reg = 0x0b004,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_crypto_clk",
			.parent_hws = (const struct clk_hw *[]){
				&crypto_clk_src.clkr.hw },
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
			.parent_hws = (const struct clk_hw *[]){
				&gp1_clk_src.clkr.hw },
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
			.parent_hws = (const struct clk_hw *[]){
				&gp2_clk_src.clkr.hw },
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
			.parent_hws = (const struct clk_hw *[]){
				&gp3_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static const struct freq_tbl ftbl_pcie_rchng_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	{ }
};

static struct clk_rcg2 pcie0_rchng_clk_src = {
	.cmd_rcgr = 0x75070,
	.freq_tbl = ftbl_pcie_rchng_clk_src,
	.hid_width = 5,
	.parent_map = gcc_xo_gpll0_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pcie0_rchng_clk_src",
		.parent_data = gcc_xo_gpll0,
		.num_parents = ARRAY_SIZE(gcc_xo_gpll0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_pcie0_rchng_clk = {
	.halt_reg = 0x75070,
	.halt_bit = 31,
	.clkr = {
		.enable_reg = 0x75070,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie0_rchng_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcie0_rchng_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie0_axi_s_bridge_clk = {
	.halt_reg = 0x75048,
	.halt_bit = 31,
	.clkr = {
		.enable_reg = 0x75048,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie0_axi_s_bridge_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcie0_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc usb0_gdsc = {
	.gdscr = 0x3e078,
	.pd = {
		.name = "usb0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc usb1_gdsc = {
	.gdscr = 0x3f078,
	.pd = {
		.name = "usb1_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static const struct alpha_pll_config ubi32_pll_config = {
	.l = 0x4e,
	.config_ctl_val = 0x200d4aa8,
	.config_ctl_hi_val = 0x3c2,
	.main_output_mask = BIT(0),
	.aux_output_mask = BIT(1),
	.pre_div_val = 0x0,
	.pre_div_mask = BIT(12),
	.post_div_val = 0x0,
	.post_div_mask = GENMASK(9, 8),
};

static const struct alpha_pll_config nss_crypto_pll_config = {
	.l = 0x3e,
	.alpha = 0x0,
	.alpha_hi = 0x80,
	.config_ctl_val = 0x4001055b,
	.main_output_mask = BIT(0),
	.pre_div_val = 0x0,
	.pre_div_mask = GENMASK(14, 12),
	.post_div_val = 0x1 << 8,
	.post_div_mask = GENMASK(11, 8),
	.vco_mask = GENMASK(21, 20),
	.vco_val = 0x0,
	.alpha_en_mask = BIT(24),
};

static struct clk_hw *gcc_ipq8074_hws[] = {
	&gpll0_out_main_div2.hw,
	&gpll6_out_main_div2.hw,
	&pcnoc_clk_src.hw,
	&system_noc_clk_src.hw,
	&gcc_xo_div4_clk_src.hw,
	&nss_noc_clk_src.hw,
	&nss_ppe_cdiv_clk_src.hw,
};

static struct clk_regmap *gcc_ipq8074_clks[] = {
	[GPLL0_MAIN] = &gpll0_main.clkr,
	[GPLL0] = &gpll0.clkr,
	[GPLL2_MAIN] = &gpll2_main.clkr,
	[GPLL2] = &gpll2.clkr,
	[GPLL4_MAIN] = &gpll4_main.clkr,
	[GPLL4] = &gpll4.clkr,
	[GPLL6_MAIN] = &gpll6_main.clkr,
	[GPLL6] = &gpll6.clkr,
	[UBI32_PLL_MAIN] = &ubi32_pll_main.clkr,
	[UBI32_PLL] = &ubi32_pll.clkr,
	[NSS_CRYPTO_PLL_MAIN] = &nss_crypto_pll_main.clkr,
	[NSS_CRYPTO_PLL] = &nss_crypto_pll.clkr,
	[PCNOC_BFDCD_CLK_SRC] = &pcnoc_bfdcd_clk_src.clkr,
	[GCC_SLEEP_CLK_SRC] = &gcc_sleep_clk_src.clkr,
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
	[PCIE0_AXI_CLK_SRC] = &pcie0_axi_clk_src.clkr,
	[PCIE0_AUX_CLK_SRC] = &pcie0_aux_clk_src.clkr,
	[PCIE0_PIPE_CLK_SRC] = &pcie0_pipe_clk_src.clkr,
	[PCIE1_AXI_CLK_SRC] = &pcie1_axi_clk_src.clkr,
	[PCIE1_AUX_CLK_SRC] = &pcie1_aux_clk_src.clkr,
	[PCIE1_PIPE_CLK_SRC] = &pcie1_pipe_clk_src.clkr,
	[SDCC1_APPS_CLK_SRC] = &sdcc1_apps_clk_src.clkr,
	[SDCC1_ICE_CORE_CLK_SRC] = &sdcc1_ice_core_clk_src.clkr,
	[SDCC2_APPS_CLK_SRC] = &sdcc2_apps_clk_src.clkr,
	[USB0_MASTER_CLK_SRC] = &usb0_master_clk_src.clkr,
	[USB0_AUX_CLK_SRC] = &usb0_aux_clk_src.clkr,
	[USB0_MOCK_UTMI_CLK_SRC] = &usb0_mock_utmi_clk_src.clkr,
	[USB0_PIPE_CLK_SRC] = &usb0_pipe_clk_src.clkr,
	[USB1_MASTER_CLK_SRC] = &usb1_master_clk_src.clkr,
	[USB1_AUX_CLK_SRC] = &usb1_aux_clk_src.clkr,
	[USB1_MOCK_UTMI_CLK_SRC] = &usb1_mock_utmi_clk_src.clkr,
	[USB1_PIPE_CLK_SRC] = &usb1_pipe_clk_src.clkr,
	[GCC_XO_CLK_SRC] = &gcc_xo_clk_src.clkr,
	[SYSTEM_NOC_BFDCD_CLK_SRC] = &system_noc_bfdcd_clk_src.clkr,
	[NSS_CE_CLK_SRC] = &nss_ce_clk_src.clkr,
	[NSS_NOC_BFDCD_CLK_SRC] = &nss_noc_bfdcd_clk_src.clkr,
	[NSS_CRYPTO_CLK_SRC] = &nss_crypto_clk_src.clkr,
	[NSS_UBI0_CLK_SRC] = &nss_ubi0_clk_src.clkr,
	[NSS_UBI0_DIV_CLK_SRC] = &nss_ubi0_div_clk_src.clkr,
	[NSS_UBI1_CLK_SRC] = &nss_ubi1_clk_src.clkr,
	[NSS_UBI1_DIV_CLK_SRC] = &nss_ubi1_div_clk_src.clkr,
	[UBI_MPT_CLK_SRC] = &ubi_mpt_clk_src.clkr,
	[NSS_IMEM_CLK_SRC] = &nss_imem_clk_src.clkr,
	[NSS_PPE_CLK_SRC] = &nss_ppe_clk_src.clkr,
	[NSS_PORT1_RX_CLK_SRC] = &nss_port1_rx_clk_src.clkr,
	[NSS_PORT1_RX_DIV_CLK_SRC] = &nss_port1_rx_div_clk_src.clkr,
	[NSS_PORT1_TX_CLK_SRC] = &nss_port1_tx_clk_src.clkr,
	[NSS_PORT1_TX_DIV_CLK_SRC] = &nss_port1_tx_div_clk_src.clkr,
	[NSS_PORT2_RX_CLK_SRC] = &nss_port2_rx_clk_src.clkr,
	[NSS_PORT2_RX_DIV_CLK_SRC] = &nss_port2_rx_div_clk_src.clkr,
	[NSS_PORT2_TX_CLK_SRC] = &nss_port2_tx_clk_src.clkr,
	[NSS_PORT2_TX_DIV_CLK_SRC] = &nss_port2_tx_div_clk_src.clkr,
	[NSS_PORT3_RX_CLK_SRC] = &nss_port3_rx_clk_src.clkr,
	[NSS_PORT3_RX_DIV_CLK_SRC] = &nss_port3_rx_div_clk_src.clkr,
	[NSS_PORT3_TX_CLK_SRC] = &nss_port3_tx_clk_src.clkr,
	[NSS_PORT3_TX_DIV_CLK_SRC] = &nss_port3_tx_div_clk_src.clkr,
	[NSS_PORT4_RX_CLK_SRC] = &nss_port4_rx_clk_src.clkr,
	[NSS_PORT4_RX_DIV_CLK_SRC] = &nss_port4_rx_div_clk_src.clkr,
	[NSS_PORT4_TX_CLK_SRC] = &nss_port4_tx_clk_src.clkr,
	[NSS_PORT4_TX_DIV_CLK_SRC] = &nss_port4_tx_div_clk_src.clkr,
	[NSS_PORT5_RX_CLK_SRC] = &nss_port5_rx_clk_src.clkr,
	[NSS_PORT5_RX_DIV_CLK_SRC] = &nss_port5_rx_div_clk_src.clkr,
	[NSS_PORT5_TX_CLK_SRC] = &nss_port5_tx_clk_src.clkr,
	[NSS_PORT5_TX_DIV_CLK_SRC] = &nss_port5_tx_div_clk_src.clkr,
	[NSS_PORT6_RX_CLK_SRC] = &nss_port6_rx_clk_src.clkr,
	[NSS_PORT6_RX_DIV_CLK_SRC] = &nss_port6_rx_div_clk_src.clkr,
	[NSS_PORT6_TX_CLK_SRC] = &nss_port6_tx_clk_src.clkr,
	[NSS_PORT6_TX_DIV_CLK_SRC] = &nss_port6_tx_div_clk_src.clkr,
	[CRYPTO_CLK_SRC] = &crypto_clk_src.clkr,
	[GP1_CLK_SRC] = &gp1_clk_src.clkr,
	[GP2_CLK_SRC] = &gp2_clk_src.clkr,
	[GP3_CLK_SRC] = &gp3_clk_src.clkr,
	[GCC_BLSP1_AHB_CLK] = &gcc_blsp1_ahb_clk.clkr,
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
	[GCC_PRNG_AHB_CLK] = &gcc_prng_ahb_clk.clkr,
	[GCC_QPIC_AHB_CLK] = &gcc_qpic_ahb_clk.clkr,
	[GCC_QPIC_CLK] = &gcc_qpic_clk.clkr,
	[GCC_PCIE0_AHB_CLK] = &gcc_pcie0_ahb_clk.clkr,
	[GCC_PCIE0_AUX_CLK] = &gcc_pcie0_aux_clk.clkr,
	[GCC_PCIE0_AXI_M_CLK] = &gcc_pcie0_axi_m_clk.clkr,
	[GCC_PCIE0_AXI_S_CLK] = &gcc_pcie0_axi_s_clk.clkr,
	[GCC_PCIE0_PIPE_CLK] = &gcc_pcie0_pipe_clk.clkr,
	[GCC_SYS_NOC_PCIE0_AXI_CLK] = &gcc_sys_noc_pcie0_axi_clk.clkr,
	[GCC_PCIE1_AHB_CLK] = &gcc_pcie1_ahb_clk.clkr,
	[GCC_PCIE1_AUX_CLK] = &gcc_pcie1_aux_clk.clkr,
	[GCC_PCIE1_AXI_M_CLK] = &gcc_pcie1_axi_m_clk.clkr,
	[GCC_PCIE1_AXI_S_CLK] = &gcc_pcie1_axi_s_clk.clkr,
	[GCC_PCIE1_PIPE_CLK] = &gcc_pcie1_pipe_clk.clkr,
	[GCC_SYS_NOC_PCIE1_AXI_CLK] = &gcc_sys_noc_pcie1_axi_clk.clkr,
	[GCC_USB0_AUX_CLK] = &gcc_usb0_aux_clk.clkr,
	[GCC_SYS_NOC_USB0_AXI_CLK] = &gcc_sys_noc_usb0_axi_clk.clkr,
	[GCC_USB0_MASTER_CLK] = &gcc_usb0_master_clk.clkr,
	[GCC_USB0_MOCK_UTMI_CLK] = &gcc_usb0_mock_utmi_clk.clkr,
	[GCC_USB0_PHY_CFG_AHB_CLK] = &gcc_usb0_phy_cfg_ahb_clk.clkr,
	[GCC_USB0_PIPE_CLK] = &gcc_usb0_pipe_clk.clkr,
	[GCC_USB0_SLEEP_CLK] = &gcc_usb0_sleep_clk.clkr,
	[GCC_USB1_AUX_CLK] = &gcc_usb1_aux_clk.clkr,
	[GCC_SYS_NOC_USB1_AXI_CLK] = &gcc_sys_noc_usb1_axi_clk.clkr,
	[GCC_USB1_MASTER_CLK] = &gcc_usb1_master_clk.clkr,
	[GCC_USB1_MOCK_UTMI_CLK] = &gcc_usb1_mock_utmi_clk.clkr,
	[GCC_USB1_PHY_CFG_AHB_CLK] = &gcc_usb1_phy_cfg_ahb_clk.clkr,
	[GCC_USB1_PIPE_CLK] = &gcc_usb1_pipe_clk.clkr,
	[GCC_USB1_SLEEP_CLK] = &gcc_usb1_sleep_clk.clkr,
	[GCC_SDCC1_AHB_CLK] = &gcc_sdcc1_ahb_clk.clkr,
	[GCC_SDCC1_APPS_CLK] = &gcc_sdcc1_apps_clk.clkr,
	[GCC_SDCC1_ICE_CORE_CLK] = &gcc_sdcc1_ice_core_clk.clkr,
	[GCC_SDCC2_AHB_CLK] = &gcc_sdcc2_ahb_clk.clkr,
	[GCC_SDCC2_APPS_CLK] = &gcc_sdcc2_apps_clk.clkr,
	[GCC_MEM_NOC_NSS_AXI_CLK] = &gcc_mem_noc_nss_axi_clk.clkr,
	[GCC_NSS_CE_APB_CLK] = &gcc_nss_ce_apb_clk.clkr,
	[GCC_NSS_CE_AXI_CLK] = &gcc_nss_ce_axi_clk.clkr,
	[GCC_NSS_CFG_CLK] = &gcc_nss_cfg_clk.clkr,
	[GCC_NSS_CRYPTO_CLK] = &gcc_nss_crypto_clk.clkr,
	[GCC_NSS_CSR_CLK] = &gcc_nss_csr_clk.clkr,
	[GCC_NSS_EDMA_CFG_CLK] = &gcc_nss_edma_cfg_clk.clkr,
	[GCC_NSS_EDMA_CLK] = &gcc_nss_edma_clk.clkr,
	[GCC_NSS_IMEM_CLK] = &gcc_nss_imem_clk.clkr,
	[GCC_NSS_NOC_CLK] = &gcc_nss_noc_clk.clkr,
	[GCC_NSS_PPE_BTQ_CLK] = &gcc_nss_ppe_btq_clk.clkr,
	[GCC_NSS_PPE_CFG_CLK] = &gcc_nss_ppe_cfg_clk.clkr,
	[GCC_NSS_PPE_CLK] = &gcc_nss_ppe_clk.clkr,
	[GCC_NSS_PPE_IPE_CLK] = &gcc_nss_ppe_ipe_clk.clkr,
	[GCC_NSS_PTP_REF_CLK] = &gcc_nss_ptp_ref_clk.clkr,
	[GCC_NSSNOC_CE_APB_CLK] = &gcc_nssnoc_ce_apb_clk.clkr,
	[GCC_NSSNOC_CE_AXI_CLK] = &gcc_nssnoc_ce_axi_clk.clkr,
	[GCC_NSSNOC_CRYPTO_CLK] = &gcc_nssnoc_crypto_clk.clkr,
	[GCC_NSSNOC_PPE_CFG_CLK] = &gcc_nssnoc_ppe_cfg_clk.clkr,
	[GCC_NSSNOC_PPE_CLK] = &gcc_nssnoc_ppe_clk.clkr,
	[GCC_NSSNOC_QOSGEN_REF_CLK] = &gcc_nssnoc_qosgen_ref_clk.clkr,
	[GCC_NSSNOC_SNOC_CLK] = &gcc_nssnoc_snoc_clk.clkr,
	[GCC_NSSNOC_TIMEOUT_REF_CLK] = &gcc_nssnoc_timeout_ref_clk.clkr,
	[GCC_NSSNOC_UBI0_AHB_CLK] = &gcc_nssnoc_ubi0_ahb_clk.clkr,
	[GCC_NSSNOC_UBI1_AHB_CLK] = &gcc_nssnoc_ubi1_ahb_clk.clkr,
	[GCC_UBI0_AHB_CLK] = &gcc_ubi0_ahb_clk.clkr,
	[GCC_UBI0_AXI_CLK] = &gcc_ubi0_axi_clk.clkr,
	[GCC_UBI0_NC_AXI_CLK] = &gcc_ubi0_nc_axi_clk.clkr,
	[GCC_UBI0_CORE_CLK] = &gcc_ubi0_core_clk.clkr,
	[GCC_UBI0_MPT_CLK] = &gcc_ubi0_mpt_clk.clkr,
	[GCC_UBI1_AHB_CLK] = &gcc_ubi1_ahb_clk.clkr,
	[GCC_UBI1_AXI_CLK] = &gcc_ubi1_axi_clk.clkr,
	[GCC_UBI1_NC_AXI_CLK] = &gcc_ubi1_nc_axi_clk.clkr,
	[GCC_UBI1_CORE_CLK] = &gcc_ubi1_core_clk.clkr,
	[GCC_UBI1_MPT_CLK] = &gcc_ubi1_mpt_clk.clkr,
	[GCC_CMN_12GPLL_AHB_CLK] = &gcc_cmn_12gpll_ahb_clk.clkr,
	[GCC_CMN_12GPLL_SYS_CLK] = &gcc_cmn_12gpll_sys_clk.clkr,
	[GCC_MDIO_AHB_CLK] = &gcc_mdio_ahb_clk.clkr,
	[GCC_UNIPHY0_AHB_CLK] = &gcc_uniphy0_ahb_clk.clkr,
	[GCC_UNIPHY0_SYS_CLK] = &gcc_uniphy0_sys_clk.clkr,
	[GCC_UNIPHY1_AHB_CLK] = &gcc_uniphy1_ahb_clk.clkr,
	[GCC_UNIPHY1_SYS_CLK] = &gcc_uniphy1_sys_clk.clkr,
	[GCC_UNIPHY2_AHB_CLK] = &gcc_uniphy2_ahb_clk.clkr,
	[GCC_UNIPHY2_SYS_CLK] = &gcc_uniphy2_sys_clk.clkr,
	[GCC_NSS_PORT1_RX_CLK] = &gcc_nss_port1_rx_clk.clkr,
	[GCC_NSS_PORT1_TX_CLK] = &gcc_nss_port1_tx_clk.clkr,
	[GCC_NSS_PORT2_RX_CLK] = &gcc_nss_port2_rx_clk.clkr,
	[GCC_NSS_PORT2_TX_CLK] = &gcc_nss_port2_tx_clk.clkr,
	[GCC_NSS_PORT3_RX_CLK] = &gcc_nss_port3_rx_clk.clkr,
	[GCC_NSS_PORT3_TX_CLK] = &gcc_nss_port3_tx_clk.clkr,
	[GCC_NSS_PORT4_RX_CLK] = &gcc_nss_port4_rx_clk.clkr,
	[GCC_NSS_PORT4_TX_CLK] = &gcc_nss_port4_tx_clk.clkr,
	[GCC_NSS_PORT5_RX_CLK] = &gcc_nss_port5_rx_clk.clkr,
	[GCC_NSS_PORT5_TX_CLK] = &gcc_nss_port5_tx_clk.clkr,
	[GCC_NSS_PORT6_RX_CLK] = &gcc_nss_port6_rx_clk.clkr,
	[GCC_NSS_PORT6_TX_CLK] = &gcc_nss_port6_tx_clk.clkr,
	[GCC_PORT1_MAC_CLK] = &gcc_port1_mac_clk.clkr,
	[GCC_PORT2_MAC_CLK] = &gcc_port2_mac_clk.clkr,
	[GCC_PORT3_MAC_CLK] = &gcc_port3_mac_clk.clkr,
	[GCC_PORT4_MAC_CLK] = &gcc_port4_mac_clk.clkr,
	[GCC_PORT5_MAC_CLK] = &gcc_port5_mac_clk.clkr,
	[GCC_PORT6_MAC_CLK] = &gcc_port6_mac_clk.clkr,
	[GCC_UNIPHY0_PORT1_RX_CLK] = &gcc_uniphy0_port1_rx_clk.clkr,
	[GCC_UNIPHY0_PORT1_TX_CLK] = &gcc_uniphy0_port1_tx_clk.clkr,
	[GCC_UNIPHY0_PORT2_RX_CLK] = &gcc_uniphy0_port2_rx_clk.clkr,
	[GCC_UNIPHY0_PORT2_TX_CLK] = &gcc_uniphy0_port2_tx_clk.clkr,
	[GCC_UNIPHY0_PORT3_RX_CLK] = &gcc_uniphy0_port3_rx_clk.clkr,
	[GCC_UNIPHY0_PORT3_TX_CLK] = &gcc_uniphy0_port3_tx_clk.clkr,
	[GCC_UNIPHY0_PORT4_RX_CLK] = &gcc_uniphy0_port4_rx_clk.clkr,
	[GCC_UNIPHY0_PORT4_TX_CLK] = &gcc_uniphy0_port4_tx_clk.clkr,
	[GCC_UNIPHY0_PORT5_RX_CLK] = &gcc_uniphy0_port5_rx_clk.clkr,
	[GCC_UNIPHY0_PORT5_TX_CLK] = &gcc_uniphy0_port5_tx_clk.clkr,
	[GCC_UNIPHY1_PORT5_RX_CLK] = &gcc_uniphy1_port5_rx_clk.clkr,
	[GCC_UNIPHY1_PORT5_TX_CLK] = &gcc_uniphy1_port5_tx_clk.clkr,
	[GCC_UNIPHY2_PORT6_RX_CLK] = &gcc_uniphy2_port6_rx_clk.clkr,
	[GCC_UNIPHY2_PORT6_TX_CLK] = &gcc_uniphy2_port6_tx_clk.clkr,
	[GCC_CRYPTO_AHB_CLK] = &gcc_crypto_ahb_clk.clkr,
	[GCC_CRYPTO_AXI_CLK] = &gcc_crypto_axi_clk.clkr,
	[GCC_CRYPTO_CLK] = &gcc_crypto_clk.clkr,
	[GCC_GP1_CLK] = &gcc_gp1_clk.clkr,
	[GCC_GP2_CLK] = &gcc_gp2_clk.clkr,
	[GCC_GP3_CLK] = &gcc_gp3_clk.clkr,
	[GCC_PCIE0_RCHNG_CLK_SRC] = &pcie0_rchng_clk_src.clkr,
	[GCC_PCIE0_RCHNG_CLK] = &gcc_pcie0_rchng_clk.clkr,
	[GCC_PCIE0_AXI_S_BRIDGE_CLK] = &gcc_pcie0_axi_s_bridge_clk.clkr,
	[GCC_CRYPTO_PPE_CLK] = &gcc_crypto_ppe_clk.clkr,
};

static const struct qcom_reset_map gcc_ipq8074_resets[] = {
	[GCC_BLSP1_BCR] = { 0x01000, 0 },
	[GCC_BLSP1_QUP1_BCR] = { 0x02000, 0 },
	[GCC_BLSP1_UART1_BCR] = { 0x02038, 0 },
	[GCC_BLSP1_QUP2_BCR] = { 0x03008, 0 },
	[GCC_BLSP1_UART2_BCR] = { 0x03028, 0 },
	[GCC_BLSP1_QUP3_BCR] = { 0x04008, 0 },
	[GCC_BLSP1_UART3_BCR] = { 0x04028, 0 },
	[GCC_BLSP1_QUP4_BCR] = { 0x05008, 0 },
	[GCC_BLSP1_UART4_BCR] = { 0x05028, 0 },
	[GCC_BLSP1_QUP5_BCR] = { 0x06008, 0 },
	[GCC_BLSP1_UART5_BCR] = { 0x06028, 0 },
	[GCC_BLSP1_QUP6_BCR] = { 0x07008, 0 },
	[GCC_BLSP1_UART6_BCR] = { 0x07028, 0 },
	[GCC_IMEM_BCR] = { 0x0e000, 0 },
	[GCC_SMMU_BCR] = { 0x12000, 0 },
	[GCC_APSS_TCU_BCR] = { 0x12050, 0 },
	[GCC_SMMU_XPU_BCR] = { 0x12054, 0 },
	[GCC_PCNOC_TBU_BCR] = { 0x12058, 0 },
	[GCC_SMMU_CFG_BCR] = { 0x1208c, 0 },
	[GCC_PRNG_BCR] = { 0x13000, 0 },
	[GCC_BOOT_ROM_BCR] = { 0x13008, 0 },
	[GCC_CRYPTO_BCR] = { 0x16000, 0 },
	[GCC_WCSS_BCR] = { 0x18000, 0 },
	[GCC_WCSS_Q6_BCR] = { 0x18100, 0 },
	[GCC_NSS_BCR] = { 0x19000, 0 },
	[GCC_SEC_CTRL_BCR] = { 0x1a000, 0 },
	[GCC_ADSS_BCR] = { 0x1c000, 0 },
	[GCC_DDRSS_BCR] = { 0x1e000, 0 },
	[GCC_SYSTEM_NOC_BCR] = { 0x26000, 0 },
	[GCC_PCNOC_BCR] = { 0x27018, 0 },
	[GCC_TCSR_BCR] = { 0x28000, 0 },
	[GCC_QDSS_BCR] = { 0x29000, 0 },
	[GCC_DCD_BCR] = { 0x2a000, 0 },
	[GCC_MSG_RAM_BCR] = { 0x2b000, 0 },
	[GCC_MPM_BCR] = { 0x2c000, 0 },
	[GCC_SPMI_BCR] = { 0x2e000, 0 },
	[GCC_SPDM_BCR] = { 0x2f000, 0 },
	[GCC_RBCPR_BCR] = { 0x33000, 0 },
	[GCC_RBCPR_MX_BCR] = { 0x33014, 0 },
	[GCC_TLMM_BCR] = { 0x34000, 0 },
	[GCC_RBCPR_WCSS_BCR] = { 0x3a000, 0 },
	[GCC_USB0_PHY_BCR] = { 0x3e034, 0 },
	[GCC_USB3PHY_0_PHY_BCR] = { 0x3e03c, 0 },
	[GCC_USB0_BCR] = { 0x3e070, 0 },
	[GCC_USB1_PHY_BCR] = { 0x3f034, 0 },
	[GCC_USB3PHY_1_PHY_BCR] = { 0x3f03c, 0 },
	[GCC_USB1_BCR] = { 0x3f070, 0 },
	[GCC_QUSB2_0_PHY_BCR] = { 0x4103c, 0 },
	[GCC_QUSB2_1_PHY_BCR] = { 0x41040, 0 },
	[GCC_SDCC1_BCR] = { 0x42000, 0 },
	[GCC_SDCC2_BCR] = { 0x43000, 0 },
	[GCC_SNOC_BUS_TIMEOUT0_BCR] = { 0x47000, 0 },
	[GCC_SNOC_BUS_TIMEOUT2_BCR] = { 0x47008, 0 },
	[GCC_SNOC_BUS_TIMEOUT3_BCR] = { 0x47010, 0 },
	[GCC_PCNOC_BUS_TIMEOUT0_BCR] = { 0x48000, 0 },
	[GCC_PCNOC_BUS_TIMEOUT1_BCR] = { 0x48008, 0 },
	[GCC_PCNOC_BUS_TIMEOUT2_BCR] = { 0x48010, 0 },
	[GCC_PCNOC_BUS_TIMEOUT3_BCR] = { 0x48018, 0 },
	[GCC_PCNOC_BUS_TIMEOUT4_BCR] = { 0x48020, 0 },
	[GCC_PCNOC_BUS_TIMEOUT5_BCR] = { 0x48028, 0 },
	[GCC_PCNOC_BUS_TIMEOUT6_BCR] = { 0x48030, 0 },
	[GCC_PCNOC_BUS_TIMEOUT7_BCR] = { 0x48038, 0 },
	[GCC_PCNOC_BUS_TIMEOUT8_BCR] = { 0x48040, 0 },
	[GCC_PCNOC_BUS_TIMEOUT9_BCR] = { 0x48048, 0 },
	[GCC_UNIPHY0_BCR] = { 0x56000, 0 },
	[GCC_UNIPHY1_BCR] = { 0x56100, 0 },
	[GCC_UNIPHY2_BCR] = { 0x56200, 0 },
	[GCC_CMN_12GPLL_BCR] = { 0x56300, 0 },
	[GCC_QPIC_BCR] = { 0x57018, 0 },
	[GCC_MDIO_BCR] = { 0x58000, 0 },
	[GCC_PCIE1_TBU_BCR] = { 0x65000, 0 },
	[GCC_WCSS_CORE_TBU_BCR] = { 0x66000, 0 },
	[GCC_WCSS_Q6_TBU_BCR] = { 0x67000, 0 },
	[GCC_USB0_TBU_BCR] = { 0x6a000, 0 },
	[GCC_USB1_TBU_BCR] = { 0x6a004, 0 },
	[GCC_PCIE0_TBU_BCR] = { 0x6b000, 0 },
	[GCC_NSS_NOC_TBU_BCR] = { 0x6e000, 0 },
	[GCC_PCIE0_BCR] = { 0x75004, 0 },
	[GCC_PCIE0_PHY_BCR] = { 0x75038, 0 },
	[GCC_PCIE0PHY_PHY_BCR] = { 0x7503c, 0 },
	[GCC_PCIE0_LINK_DOWN_BCR] = { 0x75044, 0 },
	[GCC_PCIE1_BCR] = { 0x76004, 0 },
	[GCC_PCIE1_PHY_BCR] = { 0x76038, 0 },
	[GCC_PCIE1PHY_PHY_BCR] = { 0x7603c, 0 },
	[GCC_PCIE1_LINK_DOWN_BCR] = { 0x76044, 0 },
	[GCC_DCC_BCR] = { 0x77000, 0 },
	[GCC_APC0_VOLTAGE_DROOP_DETECTOR_BCR] = { 0x78000, 0 },
	[GCC_APC1_VOLTAGE_DROOP_DETECTOR_BCR] = { 0x79000, 0 },
	[GCC_SMMU_CATS_BCR] = { 0x7c000, 0 },
	[GCC_UBI0_AXI_ARES] = { 0x68010, 0 },
	[GCC_UBI0_AHB_ARES] = { 0x68010, 1 },
	[GCC_UBI0_NC_AXI_ARES] = { 0x68010, 2 },
	[GCC_UBI0_DBG_ARES] = { 0x68010, 3 },
	[GCC_UBI0_CORE_CLAMP_ENABLE] = { 0x68010, 4 },
	[GCC_UBI0_CLKRST_CLAMP_ENABLE] = { 0x68010, 5 },
	[GCC_UBI1_AXI_ARES] = { 0x68010, 8 },
	[GCC_UBI1_AHB_ARES] = { 0x68010, 9 },
	[GCC_UBI1_NC_AXI_ARES] = { 0x68010, 10 },
	[GCC_UBI1_DBG_ARES] = { 0x68010, 11 },
	[GCC_UBI1_CORE_CLAMP_ENABLE] = { 0x68010, 12 },
	[GCC_UBI1_CLKRST_CLAMP_ENABLE] = { 0x68010, 13 },
	[GCC_NSS_CFG_ARES] = { 0x68010, 16 },
	[GCC_NSS_IMEM_ARES] = { 0x68010, 17 },
	[GCC_NSS_NOC_ARES] = { 0x68010, 18 },
	[GCC_NSS_CRYPTO_ARES] = { 0x68010, 19 },
	[GCC_NSS_CSR_ARES] = { 0x68010, 20 },
	[GCC_NSS_CE_APB_ARES] = { 0x68010, 21 },
	[GCC_NSS_CE_AXI_ARES] = { 0x68010, 22 },
	[GCC_NSSNOC_CE_APB_ARES] = { 0x68010, 23 },
	[GCC_NSSNOC_CE_AXI_ARES] = { 0x68010, 24 },
	[GCC_NSSNOC_UBI0_AHB_ARES] = { 0x68010, 25 },
	[GCC_NSSNOC_UBI1_AHB_ARES] = { 0x68010, 26 },
	[GCC_NSSNOC_SNOC_ARES] = { 0x68010, 27 },
	[GCC_NSSNOC_CRYPTO_ARES] = { 0x68010, 28 },
	[GCC_NSSNOC_ATB_ARES] = { 0x68010, 29 },
	[GCC_NSSNOC_QOSGEN_REF_ARES] = { 0x68010, 30 },
	[GCC_NSSNOC_TIMEOUT_REF_ARES] = { 0x68010, 31 },
	[GCC_PCIE0_PIPE_ARES] = { 0x75040, 0 },
	[GCC_PCIE0_SLEEP_ARES] = { 0x75040, 1 },
	[GCC_PCIE0_CORE_STICKY_ARES] = { 0x75040, 2 },
	[GCC_PCIE0_AXI_MASTER_ARES] = { 0x75040, 3 },
	[GCC_PCIE0_AXI_SLAVE_ARES] = { 0x75040, 4 },
	[GCC_PCIE0_AHB_ARES] = { 0x75040, 5 },
	[GCC_PCIE0_AXI_MASTER_STICKY_ARES] = { 0x75040, 6 },
	[GCC_PCIE0_AXI_SLAVE_STICKY_ARES] = { 0x75040, 7 },
	[GCC_PCIE1_PIPE_ARES] = { 0x76040, 0 },
	[GCC_PCIE1_SLEEP_ARES] = { 0x76040, 1 },
	[GCC_PCIE1_CORE_STICKY_ARES] = { 0x76040, 2 },
	[GCC_PCIE1_AXI_MASTER_ARES] = { 0x76040, 3 },
	[GCC_PCIE1_AXI_SLAVE_ARES] = { 0x76040, 4 },
	[GCC_PCIE1_AHB_ARES] = { 0x76040, 5 },
	[GCC_PCIE1_AXI_MASTER_STICKY_ARES] = { 0x76040, 6 },
	[GCC_PPE_FULL_RESET] = { .reg = 0x68014, .bitmask = GENMASK(19, 16) },
	[GCC_UNIPHY0_SOFT_RESET] = { .reg = 0x56004, .bitmask = GENMASK(13, 4) | BIT(1) },
	[GCC_UNIPHY0_XPCS_RESET] = { 0x56004, 2 },
	[GCC_UNIPHY1_SOFT_RESET] = { .reg = 0x56104, .bitmask = GENMASK(5, 4) | BIT(1) },
	[GCC_UNIPHY1_XPCS_RESET] = { 0x56104, 2 },
	[GCC_UNIPHY2_SOFT_RESET] = { .reg = 0x56204, .bitmask = GENMASK(5, 4) | BIT(1) },
	[GCC_UNIPHY2_XPCS_RESET] = { 0x56204, 2 },
	[GCC_EDMA_HW_RESET] = { .reg = 0x68014, .bitmask = GENMASK(21, 20) },
	[GCC_NSSPORT1_RESET] = { .reg = 0x68014, .bitmask = BIT(24) | GENMASK(1, 0) },
	[GCC_NSSPORT2_RESET] = { .reg = 0x68014, .bitmask = BIT(25) | GENMASK(3, 2) },
	[GCC_NSSPORT3_RESET] = { .reg = 0x68014, .bitmask = BIT(26) | GENMASK(5, 4) },
	[GCC_NSSPORT4_RESET] = { .reg = 0x68014, .bitmask = BIT(27) | GENMASK(9, 8) },
	[GCC_NSSPORT5_RESET] = { .reg = 0x68014, .bitmask = BIT(28) | GENMASK(11, 10) },
	[GCC_NSSPORT6_RESET] = { .reg = 0x68014, .bitmask = BIT(29) | GENMASK(13, 12) },
};

static struct gdsc *gcc_ipq8074_gdscs[] = {
	[USB0_GDSC] = &usb0_gdsc,
	[USB1_GDSC] = &usb1_gdsc,
};

static const struct of_device_id gcc_ipq8074_match_table[] = {
	{ .compatible = "qcom,gcc-ipq8074" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_ipq8074_match_table);

static const struct regmap_config gcc_ipq8074_regmap_config = {
	.reg_bits       = 32,
	.reg_stride     = 4,
	.val_bits       = 32,
	.max_register   = 0x7fffc,
	.fast_io	= true,
};

static const struct qcom_cc_desc gcc_ipq8074_desc = {
	.config = &gcc_ipq8074_regmap_config,
	.clks = gcc_ipq8074_clks,
	.num_clks = ARRAY_SIZE(gcc_ipq8074_clks),
	.resets = gcc_ipq8074_resets,
	.num_resets = ARRAY_SIZE(gcc_ipq8074_resets),
	.clk_hws = gcc_ipq8074_hws,
	.num_clk_hws = ARRAY_SIZE(gcc_ipq8074_hws),
	.gdscs = gcc_ipq8074_gdscs,
	.num_gdscs = ARRAY_SIZE(gcc_ipq8074_gdscs),
};

static int gcc_ipq8074_probe(struct platform_device *pdev)
{
	struct regmap *regmap;

	regmap = qcom_cc_map(pdev, &gcc_ipq8074_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/* SW Workaround for UBI32 Huayra PLL */
	regmap_update_bits(regmap, 0x2501c, BIT(26), BIT(26));

	clk_alpha_pll_configure(&ubi32_pll_main, regmap, &ubi32_pll_config);
	clk_alpha_pll_configure(&nss_crypto_pll_main, regmap,
				&nss_crypto_pll_config);

	return qcom_cc_really_probe(&pdev->dev, &gcc_ipq8074_desc, regmap);
}

static struct platform_driver gcc_ipq8074_driver = {
	.probe = gcc_ipq8074_probe,
	.driver = {
		.name   = "qcom,gcc-ipq8074",
		.of_match_table = gcc_ipq8074_match_table,
	},
};

static int __init gcc_ipq8074_init(void)
{
	return platform_driver_register(&gcc_ipq8074_driver);
}
core_initcall(gcc_ipq8074_init);

static void __exit gcc_ipq8074_exit(void)
{
	platform_driver_unregister(&gcc_ipq8074_driver);
}
module_exit(gcc_ipq8074_exit);

MODULE_DESCRIPTION("QCOM GCC IPQ8074 Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:gcc-ipq8074");
