/*
 * Copyright (c) 2015 The Linux Foundation. All rights reserved.
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
#include <linux/reset-controller.h>

#include <dt-bindings/clock/qcom,gcc-ipq4019.h>

#include "common.h"
#include "clk-regmap.h"
#include "clk-rcg.h"
#include "clk-branch.h"
#include "reset.h"

enum {
	P_XO,
	P_FEPLL200,
	P_FEPLL500,
	P_DDRPLL,
	P_FEPLLWCSS2G,
	P_FEPLLWCSS5G,
	P_FEPLL125DLY,
	P_DDRPLLAPSS,
};

static struct parent_map gcc_xo_200_500_map[] = {
	{ P_XO, 0 },
	{ P_FEPLL200, 1 },
	{ P_FEPLL500, 2 },
};

static const char * const gcc_xo_200_500[] = {
	"xo",
	"fepll200",
	"fepll500",
};

static struct parent_map gcc_xo_200_map[] = {
	{  P_XO, 0 },
	{  P_FEPLL200, 1 },
};

static const char * const gcc_xo_200[] = {
	"xo",
	"fepll200",
};

static struct parent_map gcc_xo_200_spi_map[] = {
	{  P_XO, 0 },
	{  P_FEPLL200, 2 },
};

static const char * const gcc_xo_200_spi[] = {
	"xo",
	"fepll200",
};

static struct parent_map gcc_xo_sdcc1_500_map[] = {
	{  P_XO, 0 },
	{  P_DDRPLL, 1 },
	{  P_FEPLL500, 2 },
};

static const char * const gcc_xo_sdcc1_500[] = {
	"xo",
	"ddrpll",
	"fepll500",
};

static struct parent_map gcc_xo_wcss2g_map[] = {
	{  P_XO, 0 },
	{  P_FEPLLWCSS2G, 1 },
};

static const char * const gcc_xo_wcss2g[] = {
	"xo",
	"fepllwcss2g",
};

static struct parent_map gcc_xo_wcss5g_map[] = {
	{  P_XO, 0 },
	{  P_FEPLLWCSS5G, 1 },
};

static const char * const gcc_xo_wcss5g[] = {
	"xo",
	"fepllwcss5g",
};

static struct parent_map gcc_xo_125_dly_map[] = {
	{  P_XO, 0 },
	{  P_FEPLL125DLY, 1 },
};

static const char * const gcc_xo_125_dly[] = {
	"xo",
	"fepll125dly",
};

static struct parent_map gcc_xo_ddr_500_200_map[] = {
	{  P_XO, 0 },
	{  P_FEPLL200, 3 },
	{  P_FEPLL500, 2 },
	{  P_DDRPLLAPSS, 1 },
};

static const char * const gcc_xo_ddr_500_200[] = {
	"xo",
	"fepll200",
	"fepll500",
	"ddrpllapss",
};

#define F(f, s, h, m, n) { (f), (s), (2 * (h) - 1), (m), (n) }
#define P_XO 0
#define FE_PLL_200 1
#define FE_PLL_500 2
#define DDRC_PLL_666  3

#define DDRC_PLL_666_SDCC  1
#define FE_PLL_125_DLY 1

#define FE_PLL_WCSS2G 1
#define FE_PLL_WCSS5G 1

static const struct freq_tbl ftbl_gcc_audio_pwm_clk[] = {
	F(48000000, P_XO, 1, 0, 0),
	F(200000000, FE_PLL_200, 1, 0, 0),
	{ }
};

static struct clk_rcg2 audio_clk_src = {
	.cmd_rcgr = 0x1b000,
	.hid_width = 5,
	.parent_map = gcc_xo_200_map,
	.freq_tbl = ftbl_gcc_audio_pwm_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "audio_clk_src",
		.parent_names = gcc_xo_200,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,

	},
};

static struct clk_branch gcc_audio_ahb_clk = {
	.halt_reg = 0x1b010,
	.clkr = {
		.enable_reg = 0x1b010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_audio_ahb_clk",
			.parent_names = (const char *[]){
				"pcnoc_clk_src",
			},
			.flags = CLK_SET_RATE_PARENT,
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_audio_pwm_clk = {
	.halt_reg = 0x1b00C,
	.clkr = {
		.enable_reg = 0x1b00C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_audio_pwm_clk",
			.parent_names = (const char *[]){
				"audio_clk_src",
			},
			.flags = CLK_SET_RATE_PARENT,
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static const struct freq_tbl ftbl_gcc_blsp1_qup1_2_i2c_apps_clk[] = {
	F(19200000, P_XO, 1, 2, 5),
	F(24000000, P_XO, 1, 1, 2),
	{ }
};

static struct clk_rcg2 blsp1_qup1_i2c_apps_clk_src = {
	.cmd_rcgr = 0x200c,
	.hid_width = 5,
	.parent_map = gcc_xo_200_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_2_i2c_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup1_i2c_apps_clk_src",
		.parent_names = gcc_xo_200,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_blsp1_qup1_i2c_apps_clk = {
	.halt_reg = 0x2008,
	.clkr = {
		.enable_reg = 0x2008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup1_i2c_apps_clk",
			.parent_names = (const char *[]){
				"blsp1_qup1_i2c_apps_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg2 blsp1_qup2_i2c_apps_clk_src = {
	.cmd_rcgr = 0x3000,
	.hid_width = 5,
	.parent_map = gcc_xo_200_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_2_i2c_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup2_i2c_apps_clk_src",
		.parent_names = gcc_xo_200,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_blsp1_qup2_i2c_apps_clk = {
	.halt_reg = 0x3010,
	.clkr = {
		.enable_reg = 0x3010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup2_i2c_apps_clk",
			.parent_names = (const char *[]){
				"blsp1_qup2_i2c_apps_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl ftbl_gcc_blsp1_qup1_2_spi_apps_clk[] = {
	F(960000, P_XO, 12, 1, 4),
	F(4800000, P_XO, 1, 1, 10),
	F(9600000, P_XO, 1, 1, 5),
	F(15000000, P_XO, 1, 1, 3),
	F(19200000, P_XO, 1, 2, 5),
	F(24000000, P_XO, 1, 1, 2),
	F(48000000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 blsp1_qup1_spi_apps_clk_src = {
	.cmd_rcgr = 0x2024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_200_spi_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_2_spi_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup1_spi_apps_clk_src",
		.parent_names = gcc_xo_200_spi,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_blsp1_qup1_spi_apps_clk = {
	.halt_reg = 0x2004,
	.clkr = {
		.enable_reg = 0x2004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup1_spi_apps_clk",
			.parent_names = (const char *[]){
				"blsp1_qup1_spi_apps_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg2 blsp1_qup2_spi_apps_clk_src = {
	.cmd_rcgr = 0x3014,
	.mnd_width = 8,
	.hid_width = 5,
	.freq_tbl = ftbl_gcc_blsp1_qup1_2_spi_apps_clk,
	.parent_map = gcc_xo_200_spi_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup2_spi_apps_clk_src",
		.parent_names = gcc_xo_200_spi,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_blsp1_qup2_spi_apps_clk = {
	.halt_reg = 0x300c,
	.clkr = {
		.enable_reg = 0x300c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup2_spi_apps_clk",
			.parent_names = (const char *[]){
				"blsp1_qup2_spi_apps_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl ftbl_gcc_blsp1_uart1_2_apps_clk[] = {
	F(1843200, FE_PLL_200, 1, 144, 15625),
	F(3686400, FE_PLL_200, 1, 288, 15625),
	F(7372800, FE_PLL_200, 1, 576, 15625),
	F(14745600, FE_PLL_200, 1, 1152, 15625),
	F(16000000, FE_PLL_200, 1, 2, 25),
	F(24000000, P_XO, 1, 1, 2),
	F(32000000, FE_PLL_200, 1, 4, 25),
	F(40000000, FE_PLL_200, 1, 1, 5),
	F(46400000, FE_PLL_200, 1, 29, 125),
	F(48000000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 blsp1_uart1_apps_clk_src = {
	.cmd_rcgr = 0x2044,
	.mnd_width = 16,
	.hid_width = 5,
	.freq_tbl = ftbl_gcc_blsp1_uart1_2_apps_clk,
	.parent_map = gcc_xo_200_spi_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart1_apps_clk_src",
		.parent_names = gcc_xo_200_spi,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_blsp1_uart1_apps_clk = {
	.halt_reg = 0x203c,
	.clkr = {
		.enable_reg = 0x203c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart1_apps_clk",
			.parent_names = (const char *[]){
				"blsp1_uart1_apps_clk_src",
			},
			.flags = CLK_SET_RATE_PARENT,
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_rcg2 blsp1_uart2_apps_clk_src = {
	.cmd_rcgr = 0x3034,
	.mnd_width = 16,
	.hid_width = 5,
	.freq_tbl = ftbl_gcc_blsp1_uart1_2_apps_clk,
	.parent_map = gcc_xo_200_spi_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart2_apps_clk_src",
		.parent_names = gcc_xo_200_spi,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_blsp1_uart2_apps_clk = {
	.halt_reg = 0x302c,
	.clkr = {
		.enable_reg = 0x302c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart2_apps_clk",
			.parent_names = (const char *[]){
				"blsp1_uart2_apps_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl ftbl_gcc_gp_clk[] = {
	F(1250000,  FE_PLL_200, 1, 16, 0),
	F(2500000,  FE_PLL_200, 1,  8, 0),
	F(5000000,  FE_PLL_200, 1,  4, 0),
	{ }
};

static struct clk_rcg2 gp1_clk_src = {
	.cmd_rcgr = 0x8004,
	.mnd_width = 8,
	.hid_width = 5,
	.freq_tbl = ftbl_gcc_gp_clk,
	.parent_map = gcc_xo_200_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gp1_clk_src",
		.parent_names = gcc_xo_200,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_gp1_clk = {
	.halt_reg = 0x8000,
	.clkr = {
		.enable_reg = 0x8000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gp1_clk",
			.parent_names = (const char *[]){
				"gp1_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg2 gp2_clk_src = {
	.cmd_rcgr = 0x9004,
	.mnd_width = 8,
	.hid_width = 5,
	.freq_tbl = ftbl_gcc_gp_clk,
	.parent_map = gcc_xo_200_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gp2_clk_src",
		.parent_names = gcc_xo_200,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_gp2_clk = {
	.halt_reg = 0x9000,
	.clkr = {
		.enable_reg = 0x9000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gp2_clk",
			.parent_names = (const char *[]){
				"gp2_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg2 gp3_clk_src = {
	.cmd_rcgr = 0xa004,
	.mnd_width = 8,
	.hid_width = 5,
	.freq_tbl = ftbl_gcc_gp_clk,
	.parent_map = gcc_xo_200_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gp3_clk_src",
		.parent_names = gcc_xo_200,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_gp3_clk = {
	.halt_reg = 0xa000,
	.clkr = {
		.enable_reg = 0xa000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gp3_clk",
			.parent_names = (const char *[]){
				"gp3_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl ftbl_gcc_sdcc1_apps_clk[] = {
	F(144000,    P_XO,			1,  3, 240),
	F(400000,    P_XO,			1,  1, 0),
	F(20000000,  FE_PLL_500,		1,  1, 25),
	F(25000000,  FE_PLL_500,		1,  1, 20),
	F(50000000,  FE_PLL_500,		1,  1, 10),
	F(100000000, FE_PLL_500,		1,  1, 5),
	F(193000000, DDRC_PLL_666_SDCC,		1,  0, 0),
	{ }
};

static struct clk_rcg2  sdcc1_apps_clk_src = {
	.cmd_rcgr = 0x18004,
	.hid_width = 5,
	.freq_tbl = ftbl_gcc_sdcc1_apps_clk,
	.parent_map = gcc_xo_sdcc1_500_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "sdcc1_apps_clk_src",
		.parent_names = gcc_xo_sdcc1_500,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct freq_tbl ftbl_gcc_apps_clk[] = {
	F(48000000, P_XO,	   1, 0, 0),
	F(200000000, FE_PLL_200,   1, 0, 0),
	F(500000000, FE_PLL_500,   1, 0, 0),
	F(626000000, DDRC_PLL_666, 1, 0, 0),
	{ }
};

static struct clk_rcg2 apps_clk_src = {
	.cmd_rcgr = 0x1900c,
	.hid_width = 5,
	.freq_tbl = ftbl_gcc_apps_clk,
	.parent_map = gcc_xo_ddr_500_200_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "apps_clk_src",
		.parent_names = gcc_xo_ddr_500_200,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_apps_ahb_clk[] = {
	F(48000000, P_XO,	   1, 0, 0),
	F(100000000, FE_PLL_200,   2, 0, 0),
	{ }
};

static struct clk_rcg2 apps_ahb_clk_src = {
	.cmd_rcgr = 0x19014,
	.hid_width = 5,
	.parent_map = gcc_xo_200_500_map,
	.freq_tbl = ftbl_gcc_apps_ahb_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "apps_ahb_clk_src",
		.parent_names = gcc_xo_200_500,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_apss_ahb_clk = {
	.halt_reg = 0x19004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x6000,
		.enable_mask = BIT(14),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_apss_ahb_clk",
			.parent_names = (const char *[]){
				"apps_ahb_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch gcc_blsp1_ahb_clk = {
	.halt_reg = 0x1008,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x6000,
		.enable_mask = BIT(10),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_ahb_clk",
			.parent_names = (const char *[]){
				"pcnoc_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_dcd_xo_clk = {
	.halt_reg = 0x2103c,
	.clkr = {
		.enable_reg = 0x2103c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_dcd_xo_clk",
			.parent_names = (const char *[]){
				"xo",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_boot_rom_ahb_clk = {
	.halt_reg = 0x1300c,
	.clkr = {
		.enable_reg = 0x1300c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_boot_rom_ahb_clk",
			.parent_names = (const char *[]){
				"pcnoc_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch gcc_crypto_ahb_clk = {
	.halt_reg = 0x16024,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x6000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_crypto_ahb_clk",
			.parent_names = (const char *[]){
				"pcnoc_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_crypto_axi_clk = {
	.halt_reg = 0x16020,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x6000,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_crypto_axi_clk",
			.parent_names = (const char *[]){
				"fepll125",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_crypto_clk = {
	.halt_reg = 0x1601c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x6000,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_crypto_clk",
			.parent_names = (const char *[]){
				"fepll125",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ess_clk = {
	.halt_reg = 0x12010,
	.clkr = {
		.enable_reg = 0x12010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ess_clk",
			.parent_names = (const char *[]){
				"fephy_125m_dly_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch gcc_imem_axi_clk = {
	.halt_reg = 0xe004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x6000,
		.enable_mask = BIT(17),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_imem_axi_clk",
			.parent_names = (const char *[]){
				"fepll200",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_imem_cfg_ahb_clk = {
	.halt_reg = 0xe008,
	.clkr = {
		.enable_reg = 0xe008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_imem_cfg_ahb_clk",
			.parent_names = (const char *[]){
				"pcnoc_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_ahb_clk = {
	.halt_reg = 0x1d00c,
	.clkr = {
		.enable_reg = 0x1d00c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_ahb_clk",
			.parent_names = (const char *[]){
				"pcnoc_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_axi_m_clk = {
	.halt_reg = 0x1d004,
	.clkr = {
		.enable_reg = 0x1d004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_axi_m_clk",
			.parent_names = (const char *[]){
				"fepll200",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_axi_s_clk = {
	.halt_reg = 0x1d008,
	.clkr = {
		.enable_reg = 0x1d008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_axi_s_clk",
			.parent_names = (const char *[]){
				"fepll200",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_prng_ahb_clk = {
	.halt_reg = 0x13004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x6000,
		.enable_mask = BIT(8),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_prng_ahb_clk",
			.parent_names = (const char *[]){
				"pcnoc_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qpic_ahb_clk = {
	.halt_reg = 0x1c008,
	.clkr = {
		.enable_reg = 0x1c008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qpic_ahb_clk",
			.parent_names = (const char *[]){
				"pcnoc_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qpic_clk = {
	.halt_reg = 0x1c004,
	.clkr = {
		.enable_reg = 0x1c004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qpic_clk",
			.parent_names = (const char *[]){
				"pcnoc_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_ahb_clk = {
	.halt_reg = 0x18010,
	.clkr = {
		.enable_reg = 0x18010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc1_ahb_clk",
			.parent_names = (const char *[]){
				"pcnoc_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_apps_clk = {
	.halt_reg = 0x1800c,
	.clkr = {
		.enable_reg = 0x1800c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc1_apps_clk",
			.parent_names = (const char *[]){
				"sdcc1_apps_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch gcc_tlmm_ahb_clk = {
	.halt_reg = 0x5004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x6000,
		.enable_mask = BIT(5),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_tlmm_ahb_clk",
			.parent_names = (const char *[]){
				"pcnoc_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb2_master_clk = {
	.halt_reg = 0x1e00c,
	.clkr = {
		.enable_reg = 0x1e00c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb2_master_clk",
			.parent_names = (const char *[]){
				"pcnoc_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb2_sleep_clk = {
	.halt_reg = 0x1e010,
	.clkr = {
		.enable_reg = 0x1e010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb2_sleep_clk",
			.parent_names = (const char *[]){
				"gcc_sleep_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb2_mock_utmi_clk = {
	.halt_reg = 0x1e014,
	.clkr = {
		.enable_reg = 0x1e014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb2_mock_utmi_clk",
			.parent_names = (const char *[]){
				"usb30_mock_utmi_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl ftbl_gcc_usb30_mock_utmi_clk[] = {
	F(2000000, FE_PLL_200, 10, 0, 0),
	{ }
};

static struct clk_rcg2 usb30_mock_utmi_clk_src = {
	.cmd_rcgr = 0x1e000,
	.hid_width = 5,
	.parent_map = gcc_xo_200_map,
	.freq_tbl = ftbl_gcc_usb30_mock_utmi_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "usb30_mock_utmi_clk_src",
		.parent_names = gcc_xo_200,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_usb3_master_clk = {
	.halt_reg = 0x1e028,
	.clkr = {
		.enable_reg = 0x1e028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_master_clk",
			.parent_names = (const char *[]){
				"fepll125",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_sleep_clk = {
	.halt_reg = 0x1e02C,
	.clkr = {
		.enable_reg = 0x1e02C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_sleep_clk",
			.parent_names = (const char *[]){
				"gcc_sleep_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_mock_utmi_clk = {
	.halt_reg = 0x1e030,
	.clkr = {
		.enable_reg = 0x1e030,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_mock_utmi_clk",
			.parent_names = (const char *[]){
				"usb30_mock_utmi_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl ftbl_gcc_fephy_dly_clk[] = {
	F(125000000, FE_PLL_125_DLY, 1, 0, 0),
	{ }
};

static struct clk_rcg2 fephy_125m_dly_clk_src = {
	.cmd_rcgr = 0x12000,
	.hid_width = 5,
	.parent_map = gcc_xo_125_dly_map,
	.freq_tbl = ftbl_gcc_fephy_dly_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "fephy_125m_dly_clk_src",
		.parent_names = gcc_xo_125_dly,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};


static const struct freq_tbl ftbl_gcc_wcss2g_clk[] = {
	F(48000000, P_XO, 1, 0, 0),
	F(250000000, FE_PLL_WCSS2G, 1, 0, 0),
	{ }
};

static struct clk_rcg2 wcss2g_clk_src = {
	.cmd_rcgr = 0x1f000,
	.hid_width = 5,
	.freq_tbl = ftbl_gcc_wcss2g_clk,
	.parent_map = gcc_xo_wcss2g_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "wcss2g_clk_src",
		.parent_names = gcc_xo_wcss2g,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_branch gcc_wcss2g_clk = {
	.halt_reg = 0x1f00C,
	.clkr = {
		.enable_reg = 0x1f00C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_wcss2g_clk",
			.parent_names = (const char *[]){
				"wcss2g_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch gcc_wcss2g_ref_clk = {
	.halt_reg = 0x1f00C,
	.clkr = {
		.enable_reg = 0x1f00C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_wcss2g_ref_clk",
			.parent_names = (const char *[]){
				"xo",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch gcc_wcss2g_rtc_clk = {
	.halt_reg = 0x1f010,
	.clkr = {
		.enable_reg = 0x1f010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_wcss2g_rtc_clk",
			.parent_names = (const char *[]){
				"gcc_sleep_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static const struct freq_tbl ftbl_gcc_wcss5g_clk[] = {
	F(48000000, P_XO, 1, 0, 0),
	F(250000000, FE_PLL_WCSS5G, 1, 0, 0),
	{ }
};

static struct clk_rcg2 wcss5g_clk_src = {
	.cmd_rcgr = 0x20000,
	.hid_width = 5,
	.parent_map = gcc_xo_wcss5g_map,
	.freq_tbl = ftbl_gcc_wcss5g_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "wcss5g_clk_src",
		.parent_names = gcc_xo_wcss5g,
		.num_parents = 2,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_wcss5g_clk = {
	.halt_reg = 0x2000c,
	.clkr = {
		.enable_reg = 0x2000c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_wcss5g_clk",
			.parent_names = (const char *[]){
				"wcss5g_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch gcc_wcss5g_ref_clk = {
	.halt_reg = 0x2000c,
	.clkr = {
		.enable_reg = 0x2000c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_wcss5g_ref_clk",
			.parent_names = (const char *[]){
				"xo",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch gcc_wcss5g_rtc_clk = {
	.halt_reg = 0x20010,
	.clkr = {
		.enable_reg = 0x20010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_wcss5g_rtc_clk",
			.parent_names = (const char *[]){
				"gcc_sleep_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_regmap *gcc_ipq4019_clocks[] = {
	[AUDIO_CLK_SRC] = &audio_clk_src.clkr,
	[BLSP1_QUP1_I2C_APPS_CLK_SRC] = &blsp1_qup1_i2c_apps_clk_src.clkr,
	[BLSP1_QUP1_SPI_APPS_CLK_SRC] = &blsp1_qup1_spi_apps_clk_src.clkr,
	[BLSP1_QUP2_I2C_APPS_CLK_SRC] = &blsp1_qup2_i2c_apps_clk_src.clkr,
	[BLSP1_QUP2_SPI_APPS_CLK_SRC] = &blsp1_qup2_spi_apps_clk_src.clkr,
	[BLSP1_UART1_APPS_CLK_SRC] = &blsp1_uart1_apps_clk_src.clkr,
	[BLSP1_UART2_APPS_CLK_SRC] = &blsp1_uart2_apps_clk_src.clkr,
	[GCC_USB3_MOCK_UTMI_CLK_SRC] = &usb30_mock_utmi_clk_src.clkr,
	[GCC_APPS_CLK_SRC] = &apps_clk_src.clkr,
	[GCC_APPS_AHB_CLK_SRC] = &apps_ahb_clk_src.clkr,
	[GP1_CLK_SRC] = &gp1_clk_src.clkr,
	[GP2_CLK_SRC] = &gp2_clk_src.clkr,
	[GP3_CLK_SRC] = &gp3_clk_src.clkr,
	[SDCC1_APPS_CLK_SRC] = &sdcc1_apps_clk_src.clkr,
	[FEPHY_125M_DLY_CLK_SRC] = &fephy_125m_dly_clk_src.clkr,
	[WCSS2G_CLK_SRC] = &wcss2g_clk_src.clkr,
	[WCSS5G_CLK_SRC] = &wcss5g_clk_src.clkr,
	[GCC_APSS_AHB_CLK] = &gcc_apss_ahb_clk.clkr,
	[GCC_AUDIO_AHB_CLK] = &gcc_audio_ahb_clk.clkr,
	[GCC_AUDIO_PWM_CLK] = &gcc_audio_pwm_clk.clkr,
	[GCC_BLSP1_AHB_CLK] = &gcc_blsp1_ahb_clk.clkr,
	[GCC_BLSP1_QUP1_I2C_APPS_CLK] = &gcc_blsp1_qup1_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP1_SPI_APPS_CLK] = &gcc_blsp1_qup1_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP2_I2C_APPS_CLK] = &gcc_blsp1_qup2_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP2_SPI_APPS_CLK] = &gcc_blsp1_qup2_spi_apps_clk.clkr,
	[GCC_BLSP1_UART1_APPS_CLK] = &gcc_blsp1_uart1_apps_clk.clkr,
	[GCC_BLSP1_UART2_APPS_CLK] = &gcc_blsp1_uart2_apps_clk.clkr,
	[GCC_DCD_XO_CLK] = &gcc_dcd_xo_clk.clkr,
	[GCC_GP1_CLK] = &gcc_gp1_clk.clkr,
	[GCC_GP2_CLK] = &gcc_gp2_clk.clkr,
	[GCC_GP3_CLK] = &gcc_gp3_clk.clkr,
	[GCC_BOOT_ROM_AHB_CLK] = &gcc_boot_rom_ahb_clk.clkr,
	[GCC_CRYPTO_AHB_CLK] = &gcc_crypto_ahb_clk.clkr,
	[GCC_CRYPTO_AXI_CLK] = &gcc_crypto_axi_clk.clkr,
	[GCC_CRYPTO_CLK] = &gcc_crypto_clk.clkr,
	[GCC_ESS_CLK] = &gcc_ess_clk.clkr,
	[GCC_IMEM_AXI_CLK] = &gcc_imem_axi_clk.clkr,
	[GCC_IMEM_CFG_AHB_CLK] = &gcc_imem_cfg_ahb_clk.clkr,
	[GCC_PCIE_AHB_CLK] = &gcc_pcie_ahb_clk.clkr,
	[GCC_PCIE_AXI_M_CLK] = &gcc_pcie_axi_m_clk.clkr,
	[GCC_PCIE_AXI_S_CLK] = &gcc_pcie_axi_s_clk.clkr,
	[GCC_PRNG_AHB_CLK] = &gcc_prng_ahb_clk.clkr,
	[GCC_QPIC_AHB_CLK] = &gcc_qpic_ahb_clk.clkr,
	[GCC_QPIC_CLK] = &gcc_qpic_clk.clkr,
	[GCC_SDCC1_AHB_CLK] = &gcc_sdcc1_ahb_clk.clkr,
	[GCC_SDCC1_APPS_CLK] = &gcc_sdcc1_apps_clk.clkr,
	[GCC_TLMM_AHB_CLK] = &gcc_tlmm_ahb_clk.clkr,
	[GCC_USB2_MASTER_CLK] = &gcc_usb2_master_clk.clkr,
	[GCC_USB2_SLEEP_CLK] = &gcc_usb2_sleep_clk.clkr,
	[GCC_USB2_MOCK_UTMI_CLK] = &gcc_usb2_mock_utmi_clk.clkr,
	[GCC_USB3_MASTER_CLK] = &gcc_usb3_master_clk.clkr,
	[GCC_USB3_SLEEP_CLK] = &gcc_usb3_sleep_clk.clkr,
	[GCC_USB3_MOCK_UTMI_CLK] = &gcc_usb3_mock_utmi_clk.clkr,
	[GCC_WCSS2G_CLK] = &gcc_wcss2g_clk.clkr,
	[GCC_WCSS2G_REF_CLK] = &gcc_wcss2g_ref_clk.clkr,
	[GCC_WCSS2G_RTC_CLK] = &gcc_wcss2g_rtc_clk.clkr,
	[GCC_WCSS5G_CLK] = &gcc_wcss5g_clk.clkr,
	[GCC_WCSS5G_REF_CLK] = &gcc_wcss5g_ref_clk.clkr,
	[GCC_WCSS5G_RTC_CLK] = &gcc_wcss5g_rtc_clk.clkr,
};

static const struct qcom_reset_map gcc_ipq4019_resets[] = {
	[WIFI0_CPU_INIT_RESET] = { 0x1f008, 5 },
	[WIFI0_RADIO_SRIF_RESET] = { 0x1f008, 4 },
	[WIFI0_RADIO_WARM_RESET] = { 0x1f008, 3 },
	[WIFI0_RADIO_COLD_RESET] = { 0x1f008, 2 },
	[WIFI0_CORE_WARM_RESET] = { 0x1f008, 1 },
	[WIFI0_CORE_COLD_RESET] = { 0x1f008, 0 },
	[WIFI1_CPU_INIT_RESET] = { 0x20008, 5 },
	[WIFI1_RADIO_SRIF_RESET] = { 0x20008, 4 },
	[WIFI1_RADIO_WARM_RESET] = { 0x20008, 3 },
	[WIFI1_RADIO_COLD_RESET] = { 0x20008, 2 },
	[WIFI1_CORE_WARM_RESET] = { 0x20008, 1 },
	[WIFI1_CORE_COLD_RESET] = { 0x20008, 0 },
	[USB3_UNIPHY_PHY_ARES] = { 0x1e038, 5 },
	[USB3_HSPHY_POR_ARES] = { 0x1e038, 4 },
	[USB3_HSPHY_S_ARES] = { 0x1e038, 2 },
	[USB2_HSPHY_POR_ARES] = { 0x1e01c, 4 },
	[USB2_HSPHY_S_ARES] = { 0x1e01c, 2 },
	[PCIE_PHY_AHB_ARES] = { 0x1d010, 11 },
	[PCIE_AHB_ARES] = { 0x1d010, 10 },
	[PCIE_PWR_ARES] = { 0x1d010, 9 },
	[PCIE_PIPE_STICKY_ARES] = { 0x1d010, 8 },
	[PCIE_AXI_M_STICKY_ARES] = { 0x1d010, 7 },
	[PCIE_PHY_ARES] = { 0x1d010, 6 },
	[PCIE_PARF_XPU_ARES] = { 0x1d010, 5 },
	[PCIE_AXI_S_XPU_ARES] = { 0x1d010, 4 },
	[PCIE_AXI_M_VMIDMT_ARES] = { 0x1d010, 3 },
	[PCIE_PIPE_ARES] = { 0x1d010, 2 },
	[PCIE_AXI_S_ARES] = { 0x1d010, 1 },
	[PCIE_AXI_M_ARES] = { 0x1d010, 0 },
	[ESS_RESET] = { 0x12008, 0},
	[GCC_BLSP1_BCR] = {0x01000, 0},
	[GCC_BLSP1_QUP1_BCR] = {0x02000, 0},
	[GCC_BLSP1_UART1_BCR] = {0x02038, 0},
	[GCC_BLSP1_QUP2_BCR] = {0x03008, 0},
	[GCC_BLSP1_UART2_BCR] = {0x03028, 0},
	[GCC_BIMC_BCR] = {0x04000, 0},
	[GCC_TLMM_BCR] = {0x05000, 0},
	[GCC_IMEM_BCR] = {0x0E000, 0},
	[GCC_ESS_BCR] = {0x12008, 0},
	[GCC_PRNG_BCR] = {0x13000, 0},
	[GCC_BOOT_ROM_BCR] = {0x13008, 0},
	[GCC_CRYPTO_BCR] = {0x16000, 0},
	[GCC_SDCC1_BCR] = {0x18000, 0},
	[GCC_SEC_CTRL_BCR] = {0x1A000, 0},
	[GCC_AUDIO_BCR] = {0x1B008, 0},
	[GCC_QPIC_BCR] = {0x1C000, 0},
	[GCC_PCIE_BCR] = {0x1D000, 0},
	[GCC_USB2_BCR] = {0x1E008, 0},
	[GCC_USB2_PHY_BCR] = {0x1E018, 0},
	[GCC_USB3_BCR] = {0x1E024, 0},
	[GCC_USB3_PHY_BCR] = {0x1E034, 0},
	[GCC_SYSTEM_NOC_BCR] = {0x21000, 0},
	[GCC_PCNOC_BCR] = {0x2102C, 0},
	[GCC_DCD_BCR] = {0x21038, 0},
	[GCC_SNOC_BUS_TIMEOUT0_BCR] = {0x21064, 0},
	[GCC_SNOC_BUS_TIMEOUT1_BCR] = {0x2106C, 0},
	[GCC_SNOC_BUS_TIMEOUT2_BCR] = {0x21074, 0},
	[GCC_SNOC_BUS_TIMEOUT3_BCR] = {0x2107C, 0},
	[GCC_PCNOC_BUS_TIMEOUT0_BCR] = {0x21084, 0},
	[GCC_PCNOC_BUS_TIMEOUT1_BCR] = {0x2108C, 0},
	[GCC_PCNOC_BUS_TIMEOUT2_BCR] = {0x21094, 0},
	[GCC_PCNOC_BUS_TIMEOUT3_BCR] = {0x2109C, 0},
	[GCC_PCNOC_BUS_TIMEOUT4_BCR] = {0x210A4, 0},
	[GCC_PCNOC_BUS_TIMEOUT5_BCR] = {0x210AC, 0},
	[GCC_PCNOC_BUS_TIMEOUT6_BCR] = {0x210B4, 0},
	[GCC_PCNOC_BUS_TIMEOUT7_BCR] = {0x210BC, 0},
	[GCC_PCNOC_BUS_TIMEOUT8_BCR] = {0x210C4, 0},
	[GCC_PCNOC_BUS_TIMEOUT9_BCR] = {0x210CC, 0},
	[GCC_TCSR_BCR] = {0x22000, 0},
	[GCC_MPM_BCR] = {0x24000, 0},
	[GCC_SPDM_BCR] = {0x25000, 0},
};

static const struct regmap_config gcc_ipq4019_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x2dfff,
	.fast_io	= true,
};

static const struct qcom_cc_desc gcc_ipq4019_desc = {
	.config = &gcc_ipq4019_regmap_config,
	.clks = gcc_ipq4019_clocks,
	.num_clks = ARRAY_SIZE(gcc_ipq4019_clocks),
	.resets = gcc_ipq4019_resets,
	.num_resets = ARRAY_SIZE(gcc_ipq4019_resets),
};

static const struct of_device_id gcc_ipq4019_match_table[] = {
	{ .compatible = "qcom,gcc-ipq4019" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_ipq4019_match_table);

static int gcc_ipq4019_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &gcc_ipq4019_desc);
}

static struct platform_driver gcc_ipq4019_driver = {
	.probe		= gcc_ipq4019_probe,
	.driver		= {
		.name	= "qcom,gcc-ipq4019",
		.owner	= THIS_MODULE,
		.of_match_table = gcc_ipq4019_match_table,
	},
};

static int __init gcc_ipq4019_init(void)
{
	return platform_driver_register(&gcc_ipq4019_driver);
}
core_initcall(gcc_ipq4019_init);

static void __exit gcc_ipq4019_exit(void)
{
	platform_driver_unregister(&gcc_ipq4019_driver);
}
module_exit(gcc_ipq4019_exit);

MODULE_ALIAS("platform:gcc-ipq4019");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("QCOM GCC IPQ4019 driver");
