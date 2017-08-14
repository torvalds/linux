/*
 * Copyright (c) 2017, The Linux Foundation. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,gcc-ipq8074.h>

#include "common.h"
#include "clk-regmap.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-branch.h"
#include "clk-alpha-pll.h"
#include "reset.h"

#define F(f, s, h, m, n) { (f), (s), (2 * (h) - 1), (m), (n) }

enum {
	P_XO,
	P_GPLL0,
	P_GPLL0_DIV2,
};

static const char * const gcc_xo_gpll0_gpll0_out_main_div2[] = {
	"xo",
	"gpll0",
	"gpll0_out_main_div2",
};

static const struct parent_map gcc_xo_gpll0_gpll0_out_main_div2_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL0_DIV2, 4 },
};

static struct clk_alpha_pll gpll0_main = {
	.offset = 0x21000,
	.clkr = {
		.enable_reg = 0x0b000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpll0_main",
			.parent_names = (const char *[]){
				"xo"
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
		.parent_names = (const char *[]){
			"gpll0_main"
		},
		.num_parents = 1,
		.ops = &clk_fixed_factor_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_alpha_pll_postdiv gpll0 = {
	.offset = 0x21000,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll0",
		.parent_names = (const char *[]){
			"gpll0_main"
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ops,
	},
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
	.parent_map = gcc_xo_gpll0_gpll0_out_main_div2_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pcnoc_bfdcd_clk_src",
		.parent_names = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
		.flags = CLK_IS_CRITICAL,
	},
};

static struct clk_fixed_factor pcnoc_clk_src = {
	.mult = 1,
	.div = 1,
	.hw.init = &(struct clk_init_data){
		.name = "pcnoc_clk_src",
		.parent_names = (const char *[]){
			"pcnoc_bfdcd_clk_src"
		},
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
			.parent_names = (const char *[]){
				"sleep_clk"
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
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
		.parent_names = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = 3,
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
		.parent_names = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = 3,
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
		.parent_names = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = 3,
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
		.parent_names = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = 3,
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
		.parent_names = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = 3,
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
		.parent_names = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = 3,
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
		.parent_names = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = 3,
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
		.parent_names = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = 3,
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
		.parent_names = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = 3,
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
		.parent_names = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = 3,
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
		.parent_names = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = 3,
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
		.parent_names = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = 3,
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
		.parent_names = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = 3,
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
		.parent_names = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = 3,
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
		.parent_names = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = 3,
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
		.parent_names = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = 3,
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
		.parent_names = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = 3,
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
		.parent_names = gcc_xo_gpll0_gpll0_out_main_div2,
		.num_parents = 3,
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
			.parent_names = (const char *[]){
				"pcnoc_clk_src"
			},
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
			.parent_names = (const char *[]){
				"blsp1_qup1_i2c_apps_clk_src"
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
			.parent_names = (const char *[]){
				"blsp1_qup1_spi_apps_clk_src"
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
			.parent_names = (const char *[]){
				"blsp1_qup2_i2c_apps_clk_src"
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
			.parent_names = (const char *[]){
				"blsp1_qup2_spi_apps_clk_src"
			},
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
			.parent_names = (const char *[]){
				"blsp1_qup3_i2c_apps_clk_src"
			},
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
			.parent_names = (const char *[]){
				"blsp1_qup3_spi_apps_clk_src"
			},
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
			.parent_names = (const char *[]){
				"blsp1_qup4_i2c_apps_clk_src"
			},
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
			.parent_names = (const char *[]){
				"blsp1_qup4_spi_apps_clk_src"
			},
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
			.parent_names = (const char *[]){
				"blsp1_qup5_i2c_apps_clk_src"
			},
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
			.parent_names = (const char *[]){
				"blsp1_qup5_spi_apps_clk_src"
			},
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
			.parent_names = (const char *[]){
				"blsp1_qup6_i2c_apps_clk_src"
			},
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
			.parent_names = (const char *[]){
				"blsp1_qup6_spi_apps_clk_src"
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
			.parent_names = (const char *[]){
				"blsp1_uart1_apps_clk_src"
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
			.parent_names = (const char *[]){
				"blsp1_uart2_apps_clk_src"
			},
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
			.parent_names = (const char *[]){
				"blsp1_uart3_apps_clk_src"
			},
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
			.parent_names = (const char *[]){
				"blsp1_uart4_apps_clk_src"
			},
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
			.parent_names = (const char *[]){
				"blsp1_uart5_apps_clk_src"
			},
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
			.parent_names = (const char *[]){
				"blsp1_uart6_apps_clk_src"
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
		.enable_reg = 0x0b004,
		.enable_mask = BIT(8),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_prng_ahb_clk",
			.parent_names = (const char *[]){
				"pcnoc_clk_src"
			},
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
			.parent_names = (const char *[]){
				"pcnoc_clk_src"
			},
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
			.parent_names = (const char *[]){
				"pcnoc_clk_src"
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_hw *gcc_ipq8074_hws[] = {
	&gpll0_out_main_div2.hw,
	&pcnoc_clk_src.hw,
};

static struct clk_regmap *gcc_ipq8074_clks[] = {
	[GPLL0_MAIN] = &gpll0_main.clkr,
	[GPLL0] = &gpll0.clkr,
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
};

static int gcc_ipq8074_probe(struct platform_device *pdev)
{
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(gcc_ipq8074_hws); i++) {
		ret = devm_clk_hw_register(&pdev->dev, gcc_ipq8074_hws[i]);
		if (ret)
			return ret;
	}

	return qcom_cc_probe(pdev, &gcc_ipq8074_desc);
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
