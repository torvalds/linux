/*x
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
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
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>
#include <linux/clk.h>

#include <dt-bindings/clock/qcom,mmcc-msm8996.h>

#include "common.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-alpha-pll.h"
#include "clk-rcg.h"
#include "clk-branch.h"
#include "reset.h"
#include "gdsc.h"

enum {
	P_XO,
	P_MMPLL0,
	P_GPLL0,
	P_GPLL0_DIV,
	P_MMPLL1,
	P_MMPLL9,
	P_MMPLL2,
	P_MMPLL8,
	P_MMPLL3,
	P_DSI0PLL,
	P_DSI1PLL,
	P_MMPLL5,
	P_HDMIPLL,
	P_DSI0PLL_BYTE,
	P_DSI1PLL_BYTE,
	P_MMPLL4,
};

static const struct parent_map mmss_xo_hdmi_map[] = {
	{ P_XO, 0 },
	{ P_HDMIPLL, 1 }
};

static const char * const mmss_xo_hdmi[] = {
	"xo",
	"hdmipll"
};

static const struct parent_map mmss_xo_dsi0pll_dsi1pll_map[] = {
	{ P_XO, 0 },
	{ P_DSI0PLL, 1 },
	{ P_DSI1PLL, 2 }
};

static const char * const mmss_xo_dsi0pll_dsi1pll[] = {
	"xo",
	"dsi0pll",
	"dsi1pll"
};

static const struct parent_map mmss_xo_gpll0_gpll0_div_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 5 },
	{ P_GPLL0_DIV, 6 }
};

static const char * const mmss_xo_gpll0_gpll0_div[] = {
	"xo",
	"gpll0",
	"gpll0_div"
};

static const struct parent_map mmss_xo_dsibyte_map[] = {
	{ P_XO, 0 },
	{ P_DSI0PLL_BYTE, 1 },
	{ P_DSI1PLL_BYTE, 2 }
};

static const char * const mmss_xo_dsibyte[] = {
	"xo",
	"dsi0pllbyte",
	"dsi1pllbyte"
};

static const struct parent_map mmss_xo_mmpll0_gpll0_gpll0_div_map[] = {
	{ P_XO, 0 },
	{ P_MMPLL0, 1 },
	{ P_GPLL0, 5 },
	{ P_GPLL0_DIV, 6 }
};

static const char * const mmss_xo_mmpll0_gpll0_gpll0_div[] = {
	"xo",
	"mmpll0",
	"gpll0",
	"gpll0_div"
};

static const struct parent_map mmss_xo_mmpll0_mmpll1_gpll0_gpll0_div_map[] = {
	{ P_XO, 0 },
	{ P_MMPLL0, 1 },
	{ P_MMPLL1, 2 },
	{ P_GPLL0, 5 },
	{ P_GPLL0_DIV, 6 }
};

static const char * const mmss_xo_mmpll0_mmpll1_gpll0_gpll0_div[] = {
	"xo",
	"mmpll0",
	"mmpll1",
	"gpll0",
	"gpll0_div"
};

static const struct parent_map mmss_xo_mmpll0_mmpll3_gpll0_gpll0_div_map[] = {
	{ P_XO, 0 },
	{ P_MMPLL0, 1 },
	{ P_MMPLL3, 3 },
	{ P_GPLL0, 5 },
	{ P_GPLL0_DIV, 6 }
};

static const char * const mmss_xo_mmpll0_mmpll3_gpll0_gpll0_div[] = {
	"xo",
	"mmpll0",
	"mmpll3",
	"gpll0",
	"gpll0_div"
};

static const struct parent_map mmss_xo_mmpll0_mmpll5_gpll0_gpll0_div_map[] = {
	{ P_XO, 0 },
	{ P_MMPLL0, 1 },
	{ P_MMPLL5, 2 },
	{ P_GPLL0, 5 },
	{ P_GPLL0_DIV, 6 }
};

static const char * const mmss_xo_mmpll0_mmpll5_gpll0_gpll0_div[] = {
	"xo",
	"mmpll0",
	"mmpll5",
	"gpll0",
	"gpll0_div"
};

static const struct parent_map mmss_xo_mmpll0_mmpll4_gpll0_gpll0_div_map[] = {
	{ P_XO, 0 },
	{ P_MMPLL0, 1 },
	{ P_MMPLL4, 3 },
	{ P_GPLL0, 5 },
	{ P_GPLL0_DIV, 6 }
};

static const char * const mmss_xo_mmpll0_mmpll4_gpll0_gpll0_div[] = {
	"xo",
	"mmpll0",
	"mmpll4",
	"gpll0",
	"gpll0_div"
};

static const struct parent_map mmss_xo_mmpll0_mmpll9_mmpll2_mmpll8_gpll0_map[] = {
	{ P_XO, 0 },
	{ P_MMPLL0, 1 },
	{ P_MMPLL9, 2 },
	{ P_MMPLL2, 3 },
	{ P_MMPLL8, 4 },
	{ P_GPLL0, 5 }
};

static const char * const mmss_xo_mmpll0_mmpll9_mmpll2_mmpll8_gpll0[] = {
	"xo",
	"mmpll0",
	"mmpll9",
	"mmpll2",
	"mmpll8",
	"gpll0"
};

static const struct parent_map mmss_xo_mmpll0_mmpll9_mmpll2_mmpll8_gpll0_gpll0_div_map[] = {
	{ P_XO, 0 },
	{ P_MMPLL0, 1 },
	{ P_MMPLL9, 2 },
	{ P_MMPLL2, 3 },
	{ P_MMPLL8, 4 },
	{ P_GPLL0, 5 },
	{ P_GPLL0_DIV, 6 }
};

static const char * const mmss_xo_mmpll0_mmpll9_mmpll2_mmpll8_gpll0_gpll0_div[] = {
	"xo",
	"mmpll0",
	"mmpll9",
	"mmpll2",
	"mmpll8",
	"gpll0",
	"gpll0_div"
};

static const struct parent_map mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div_map[] = {
	{ P_XO, 0 },
	{ P_MMPLL0, 1 },
	{ P_MMPLL1, 2 },
	{ P_MMPLL4, 3 },
	{ P_MMPLL3, 4 },
	{ P_GPLL0, 5 },
	{ P_GPLL0_DIV, 6 }
};

static const char * const mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div[] = {
	"xo",
	"mmpll0",
	"mmpll1",
	"mmpll4",
	"mmpll3",
	"gpll0",
	"gpll0_div"
};

static struct clk_fixed_factor gpll0_div = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "gpll0_div",
		.parent_names = (const char *[]){ "gpll0" },
		.num_parents = 1,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct pll_vco mmpll_p_vco[] = {
	{ 250000000, 500000000, 3 },
	{ 500000000, 1000000000, 2 },
	{ 1000000000, 1500000000, 1 },
	{ 1500000000, 2000000000, 0 },
};

static struct pll_vco mmpll_gfx_vco[] = {
	{ 400000000, 1000000000, 2 },
	{ 1000000000, 1500000000, 1 },
	{ 1500000000, 2000000000, 0 },
};

static struct pll_vco mmpll_t_vco[] = {
	{ 500000000, 1500000000, 0 },
};

static struct clk_alpha_pll mmpll0_early = {
	.offset = 0x0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.vco_table = mmpll_p_vco,
	.num_vco = ARRAY_SIZE(mmpll_p_vco),
	.clkr = {
		.enable_reg = 0x100,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmpll0_early",
			.parent_names = (const char *[]){ "xo" },
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct clk_alpha_pll_postdiv mmpll0 = {
	.offset = 0x0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll0",
		.parent_names = (const char *[]){ "mmpll0_early" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_alpha_pll mmpll1_early = {
	.offset = 0x30,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.vco_table = mmpll_p_vco,
	.num_vco = ARRAY_SIZE(mmpll_p_vco),
	.clkr = {
		.enable_reg = 0x100,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "mmpll1_early",
			.parent_names = (const char *[]){ "xo" },
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		}
	},
};

static struct clk_alpha_pll_postdiv mmpll1 = {
	.offset = 0x30,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll1",
		.parent_names = (const char *[]){ "mmpll1_early" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_alpha_pll mmpll2_early = {
	.offset = 0x4100,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.vco_table = mmpll_gfx_vco,
	.num_vco = ARRAY_SIZE(mmpll_gfx_vco),
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll2_early",
		.parent_names = (const char *[]){ "xo" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_ops,
	},
};

static struct clk_alpha_pll_postdiv mmpll2 = {
	.offset = 0x4100,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll2",
		.parent_names = (const char *[]){ "mmpll2_early" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_alpha_pll mmpll3_early = {
	.offset = 0x60,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.vco_table = mmpll_p_vco,
	.num_vco = ARRAY_SIZE(mmpll_p_vco),
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll3_early",
		.parent_names = (const char *[]){ "xo" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_ops,
	},
};

static struct clk_alpha_pll_postdiv mmpll3 = {
	.offset = 0x60,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll3",
		.parent_names = (const char *[]){ "mmpll3_early" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_alpha_pll mmpll4_early = {
	.offset = 0x90,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.vco_table = mmpll_t_vco,
	.num_vco = ARRAY_SIZE(mmpll_t_vco),
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll4_early",
		.parent_names = (const char *[]){ "xo" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_ops,
	},
};

static struct clk_alpha_pll_postdiv mmpll4 = {
	.offset = 0x90,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.width = 2,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll4",
		.parent_names = (const char *[]){ "mmpll4_early" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_alpha_pll mmpll5_early = {
	.offset = 0xc0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.vco_table = mmpll_p_vco,
	.num_vco = ARRAY_SIZE(mmpll_p_vco),
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll5_early",
		.parent_names = (const char *[]){ "xo" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_ops,
	},
};

static struct clk_alpha_pll_postdiv mmpll5 = {
	.offset = 0xc0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll5",
		.parent_names = (const char *[]){ "mmpll5_early" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_alpha_pll mmpll8_early = {
	.offset = 0x4130,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.vco_table = mmpll_gfx_vco,
	.num_vco = ARRAY_SIZE(mmpll_gfx_vco),
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll8_early",
		.parent_names = (const char *[]){ "xo" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_ops,
	},
};

static struct clk_alpha_pll_postdiv mmpll8 = {
	.offset = 0x4130,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll8",
		.parent_names = (const char *[]){ "mmpll8_early" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_alpha_pll mmpll9_early = {
	.offset = 0x4200,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.vco_table = mmpll_t_vco,
	.num_vco = ARRAY_SIZE(mmpll_t_vco),
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll9_early",
		.parent_names = (const char *[]){ "xo" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_ops,
	},
};

static struct clk_alpha_pll_postdiv mmpll9 = {
	.offset = 0x4200,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.width = 2,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll9",
		.parent_names = (const char *[]){ "mmpll9_early" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct freq_tbl ftbl_ahb_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(40000000, P_GPLL0_DIV, 7.5, 0, 0),
	F(80000000, P_MMPLL0, 10, 0, 0),
	{ }
};

static struct clk_rcg2 ahb_clk_src = {
	.cmd_rcgr = 0x5000,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_ahb_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "ahb_clk_src",
		.parent_names = mmss_xo_mmpll0_gpll0_gpll0_div,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_axi_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(75000000, P_GPLL0_DIV, 4, 0, 0),
	F(100000000, P_GPLL0, 6, 0, 0),
	F(171430000, P_GPLL0, 3.5, 0, 0),
	F(200000000, P_GPLL0, 3, 0, 0),
	F(320000000, P_MMPLL0, 2.5, 0, 0),
	F(400000000, P_MMPLL0, 2, 0, 0),
	{ }
};

static struct clk_rcg2 axi_clk_src = {
	.cmd_rcgr = 0x5040,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll1_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_axi_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "axi_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll1_gpll0_gpll0_div,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 maxi_clk_src = {
	.cmd_rcgr = 0x5090,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll1_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_axi_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "maxi_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll1_gpll0_gpll0_div,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gfx3d_clk_src = {
	.cmd_rcgr = 0x4000,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll9_mmpll2_mmpll8_gpll0_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gfx3d_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll9_mmpll2_mmpll8_gpll0,
		.num_parents = 6,
		.ops = &clk_gfx3d_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct freq_tbl ftbl_rbbmtimer_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 rbbmtimer_clk_src = {
	.cmd_rcgr = 0x4090,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_rbbmtimer_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "rbbmtimer_clk_src",
		.parent_names = mmss_xo_mmpll0_gpll0_gpll0_div,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 isense_clk_src = {
	.cmd_rcgr = 0x4010,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll9_mmpll2_mmpll8_gpll0_gpll0_div_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "isense_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll9_mmpll2_mmpll8_gpll0_gpll0_div,
		.num_parents = 7,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_rbcpr_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(50000000, P_GPLL0, 12, 0, 0),
	{ }
};

static struct clk_rcg2 rbcpr_clk_src = {
	.cmd_rcgr = 0x4060,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_rbcpr_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "rbcpr_clk_src",
		.parent_names = mmss_xo_mmpll0_gpll0_gpll0_div,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_video_core_clk_src[] = {
	F(75000000, P_GPLL0_DIV, 4, 0, 0),
	F(150000000, P_GPLL0, 4, 0, 0),
	F(346666667, P_MMPLL3, 3, 0, 0),
	F(520000000, P_MMPLL3, 2, 0, 0),
	{ }
};

static struct clk_rcg2 video_core_clk_src = {
	.cmd_rcgr = 0x1000,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll3_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_video_core_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "video_core_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll3_gpll0_gpll0_div,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 video_subcore0_clk_src = {
	.cmd_rcgr = 0x1060,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll3_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_video_core_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "video_subcore0_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll3_gpll0_gpll0_div,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 video_subcore1_clk_src = {
	.cmd_rcgr = 0x1080,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll3_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_video_core_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "video_subcore1_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll3_gpll0_gpll0_div,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 pclk0_clk_src = {
	.cmd_rcgr = 0x2000,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmss_xo_dsi0pll_dsi1pll_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pclk0_clk_src",
		.parent_names = mmss_xo_dsi0pll_dsi1pll,
		.num_parents = 3,
		.ops = &clk_pixel_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_rcg2 pclk1_clk_src = {
	.cmd_rcgr = 0x2020,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmss_xo_dsi0pll_dsi1pll_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pclk1_clk_src",
		.parent_names = mmss_xo_dsi0pll_dsi1pll,
		.num_parents = 3,
		.ops = &clk_pixel_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct freq_tbl ftbl_mdp_clk_src[] = {
	F(85714286, P_GPLL0, 7, 0, 0),
	F(100000000, P_GPLL0, 6, 0, 0),
	F(150000000, P_GPLL0, 4, 0, 0),
	F(171428571, P_GPLL0, 3.5, 0, 0),
	F(200000000, P_GPLL0, 3, 0, 0),
	F(275000000, P_MMPLL5, 3, 0, 0),
	F(300000000, P_GPLL0, 2, 0, 0),
	F(330000000, P_MMPLL5, 2.5, 0, 0),
	F(412500000, P_MMPLL5, 2, 0, 0),
	{ }
};

static struct clk_rcg2 mdp_clk_src = {
	.cmd_rcgr = 0x2040,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll5_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_mdp_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mdp_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll5_gpll0_gpll0_div,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static struct freq_tbl extpclk_freq_tbl[] = {
	{ .src = P_HDMIPLL },
	{ }
};

static struct clk_rcg2 extpclk_clk_src = {
	.cmd_rcgr = 0x2060,
	.hid_width = 5,
	.parent_map = mmss_xo_hdmi_map,
	.freq_tbl = extpclk_freq_tbl,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "extpclk_clk_src",
		.parent_names = mmss_xo_hdmi,
		.num_parents = 2,
		.ops = &clk_byte_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct freq_tbl ftbl_mdss_vsync_clk[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 vsync_clk_src = {
	.cmd_rcgr = 0x2080,
	.hid_width = 5,
	.parent_map = mmss_xo_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_mdss_vsync_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "vsync_clk_src",
		.parent_names = mmss_xo_gpll0_gpll0_div,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static struct freq_tbl ftbl_mdss_hdmi_clk[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 hdmi_clk_src = {
	.cmd_rcgr = 0x2100,
	.hid_width = 5,
	.parent_map = mmss_xo_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_mdss_hdmi_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "hdmi_clk_src",
		.parent_names = mmss_xo_gpll0_gpll0_div,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 byte0_clk_src = {
	.cmd_rcgr = 0x2120,
	.hid_width = 5,
	.parent_map = mmss_xo_dsibyte_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "byte0_clk_src",
		.parent_names = mmss_xo_dsibyte,
		.num_parents = 3,
		.ops = &clk_byte2_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_rcg2 byte1_clk_src = {
	.cmd_rcgr = 0x2140,
	.hid_width = 5,
	.parent_map = mmss_xo_dsibyte_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "byte1_clk_src",
		.parent_names = mmss_xo_dsibyte,
		.num_parents = 3,
		.ops = &clk_byte2_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct freq_tbl ftbl_mdss_esc0_1_clk[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 esc0_clk_src = {
	.cmd_rcgr = 0x2160,
	.hid_width = 5,
	.parent_map = mmss_xo_dsibyte_map,
	.freq_tbl = ftbl_mdss_esc0_1_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "esc0_clk_src",
		.parent_names = mmss_xo_dsibyte,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 esc1_clk_src = {
	.cmd_rcgr = 0x2180,
	.hid_width = 5,
	.parent_map = mmss_xo_dsibyte_map,
	.freq_tbl = ftbl_mdss_esc0_1_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "esc1_clk_src",
		.parent_names = mmss_xo_dsibyte,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_camss_gp0_clk_src[] = {
	F(10000, P_XO, 16, 1, 120),
	F(24000, P_XO, 16, 1, 50),
	F(6000000, P_GPLL0_DIV, 10, 1, 5),
	F(12000000, P_GPLL0_DIV, 1, 1, 25),
	F(13000000, P_GPLL0_DIV, 2, 13, 150),
	F(24000000, P_GPLL0_DIV, 1, 2, 25),
	{ }
};

static struct clk_rcg2 camss_gp0_clk_src = {
	.cmd_rcgr = 0x3420,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll4_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_camss_gp0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "camss_gp0_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll4_gpll0_gpll0_div,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 camss_gp1_clk_src = {
	.cmd_rcgr = 0x3450,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll4_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_camss_gp0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "camss_gp1_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll4_gpll0_gpll0_div,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_mclk0_clk_src[] = {
	F(4800000, P_XO, 4, 0, 0),
	F(6000000, P_GPLL0_DIV, 10, 1, 5),
	F(8000000, P_GPLL0_DIV, 1, 2, 75),
	F(9600000, P_XO, 2, 0, 0),
	F(16666667, P_GPLL0_DIV, 2, 1, 9),
	F(19200000, P_XO, 1, 0, 0),
	F(24000000, P_GPLL0_DIV, 1, 2, 25),
	F(33333333, P_GPLL0_DIV, 1, 1, 9),
	F(48000000, P_GPLL0, 1, 2, 25),
	F(66666667, P_GPLL0, 1, 1, 9),
	{ }
};

static struct clk_rcg2 mclk0_clk_src = {
	.cmd_rcgr = 0x3360,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll4_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_mclk0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mclk0_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll4_gpll0_gpll0_div,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 mclk1_clk_src = {
	.cmd_rcgr = 0x3390,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll4_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_mclk0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mclk1_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll4_gpll0_gpll0_div,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 mclk2_clk_src = {
	.cmd_rcgr = 0x33c0,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll4_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_mclk0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mclk2_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll4_gpll0_gpll0_div,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 mclk3_clk_src = {
	.cmd_rcgr = 0x33f0,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll4_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_mclk0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mclk3_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll4_gpll0_gpll0_div,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_cci_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(37500000, P_GPLL0, 16, 0, 0),
	F(50000000, P_GPLL0, 12, 0, 0),
	F(100000000, P_GPLL0, 6, 0, 0),
	{ }
};

static struct clk_rcg2 cci_clk_src = {
	.cmd_rcgr = 0x3300,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll4_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_cci_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cci_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll4_gpll0_gpll0_div,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_csi0phytimer_clk_src[] = {
	F(100000000, P_GPLL0_DIV, 3, 0, 0),
	F(200000000, P_GPLL0, 3, 0, 0),
	F(266666667, P_MMPLL0, 3, 0, 0),
	{ }
};

static struct clk_rcg2 csi0phytimer_clk_src = {
	.cmd_rcgr = 0x3000,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_csi0phytimer_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csi0phytimer_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div,
		.num_parents = 7,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 csi1phytimer_clk_src = {
	.cmd_rcgr = 0x3030,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_csi0phytimer_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csi1phytimer_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div,
		.num_parents = 7,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 csi2phytimer_clk_src = {
	.cmd_rcgr = 0x3060,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_csi0phytimer_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csi2phytimer_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div,
		.num_parents = 7,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_csiphy0_3p_clk_src[] = {
	F(100000000, P_GPLL0_DIV, 3, 0, 0),
	F(200000000, P_GPLL0, 3, 0, 0),
	F(320000000, P_MMPLL4, 3, 0, 0),
	F(384000000, P_MMPLL4, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 csiphy0_3p_clk_src = {
	.cmd_rcgr = 0x3240,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_csiphy0_3p_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csiphy0_3p_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div,
		.num_parents = 7,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 csiphy1_3p_clk_src = {
	.cmd_rcgr = 0x3260,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_csiphy0_3p_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csiphy1_3p_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div,
		.num_parents = 7,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 csiphy2_3p_clk_src = {
	.cmd_rcgr = 0x3280,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_csiphy0_3p_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csiphy2_3p_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div,
		.num_parents = 7,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_jpeg0_clk_src[] = {
	F(75000000, P_GPLL0_DIV, 4, 0, 0),
	F(150000000, P_GPLL0, 4, 0, 0),
	F(228571429, P_MMPLL0, 3.5, 0, 0),
	F(266666667, P_MMPLL0, 3, 0, 0),
	F(320000000, P_MMPLL0, 2.5, 0, 0),
	F(480000000, P_MMPLL4, 2, 0, 0),
	{ }
};

static struct clk_rcg2 jpeg0_clk_src = {
	.cmd_rcgr = 0x3500,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_jpeg0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "jpeg0_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div,
		.num_parents = 7,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_jpeg2_clk_src[] = {
	F(75000000, P_GPLL0_DIV, 4, 0, 0),
	F(150000000, P_GPLL0, 4, 0, 0),
	F(228571429, P_MMPLL0, 3.5, 0, 0),
	F(266666667, P_MMPLL0, 3, 0, 0),
	F(320000000, P_MMPLL0, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 jpeg2_clk_src = {
	.cmd_rcgr = 0x3540,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_jpeg2_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "jpeg2_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div,
		.num_parents = 7,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 jpeg_dma_clk_src = {
	.cmd_rcgr = 0x3560,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_jpeg0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "jpeg_dma_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div,
		.num_parents = 7,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_vfe0_clk_src[] = {
	F(75000000, P_GPLL0_DIV, 4, 0, 0),
	F(100000000, P_GPLL0_DIV, 3, 0, 0),
	F(300000000, P_GPLL0, 2, 0, 0),
	F(320000000, P_MMPLL0, 2.5, 0, 0),
	F(480000000, P_MMPLL4, 2, 0, 0),
	F(600000000, P_GPLL0, 1, 0, 0),
	{ }
};

static struct clk_rcg2 vfe0_clk_src = {
	.cmd_rcgr = 0x3600,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_vfe0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "vfe0_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div,
		.num_parents = 7,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 vfe1_clk_src = {
	.cmd_rcgr = 0x3620,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_vfe0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "vfe1_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div,
		.num_parents = 7,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_cpp_clk_src[] = {
	F(100000000, P_GPLL0_DIV, 3, 0, 0),
	F(200000000, P_GPLL0, 3, 0, 0),
	F(320000000, P_MMPLL0, 2.5, 0, 0),
	F(480000000, P_MMPLL4, 2, 0, 0),
	F(640000000, P_MMPLL4, 1.5, 0, 0),
	{ }
};

static struct clk_rcg2 cpp_clk_src = {
	.cmd_rcgr = 0x3640,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_cpp_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cpp_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div,
		.num_parents = 7,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_csi0_clk_src[] = {
	F(100000000, P_GPLL0_DIV, 3, 0, 0),
	F(200000000, P_GPLL0, 3, 0, 0),
	F(266666667, P_MMPLL0, 3, 0, 0),
	F(480000000, P_MMPLL4, 2, 0, 0),
	F(600000000, P_GPLL0, 1, 0, 0),
	{ }
};

static struct clk_rcg2 csi0_clk_src = {
	.cmd_rcgr = 0x3090,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_csi0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csi0_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div,
		.num_parents = 7,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 csi1_clk_src = {
	.cmd_rcgr = 0x3100,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_csi0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csi1_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div,
		.num_parents = 7,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 csi2_clk_src = {
	.cmd_rcgr = 0x3160,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_csi0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csi2_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div,
		.num_parents = 7,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 csi3_clk_src = {
	.cmd_rcgr = 0x31c0,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_csi0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csi3_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll1_mmpll4_mmpll3_gpll0_gpll0_div,
		.num_parents = 7,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_fd_core_clk_src[] = {
	F(100000000, P_GPLL0_DIV, 3, 0, 0),
	F(200000000, P_GPLL0, 3, 0, 0),
	F(400000000, P_MMPLL0, 2, 0, 0),
	{ }
};

static struct clk_rcg2 fd_core_clk_src = {
	.cmd_rcgr = 0x3b00,
	.hid_width = 5,
	.parent_map = mmss_xo_mmpll0_mmpll4_gpll0_gpll0_div_map,
	.freq_tbl = ftbl_fd_core_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "fd_core_clk_src",
		.parent_names = mmss_xo_mmpll0_mmpll4_gpll0_gpll0_div,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch mmss_mmagic_ahb_clk = {
	.halt_reg = 0x5024,
	.clkr = {
		.enable_reg = 0x5024,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_mmagic_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_mmagic_cfg_ahb_clk = {
	.halt_reg = 0x5054,
	.clkr = {
		.enable_reg = 0x5054,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_mmagic_cfg_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_misc_ahb_clk = {
	.halt_reg = 0x5018,
	.clkr = {
		.enable_reg = 0x5018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_misc_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_misc_cxo_clk = {
	.halt_reg = 0x5014,
	.clkr = {
		.enable_reg = 0x5014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_misc_cxo_clk",
			.parent_names = (const char *[]){ "xo" },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_mmagic_maxi_clk = {
	.halt_reg = 0x5074,
	.clkr = {
		.enable_reg = 0x5074,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_mmagic_maxi_clk",
			.parent_names = (const char *[]){ "maxi_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmagic_camss_axi_clk = {
	.halt_reg = 0x3c44,
	.clkr = {
		.enable_reg = 0x3c44,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmagic_camss_axi_clk",
			.parent_names = (const char *[]){ "axi_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmagic_camss_noc_cfg_ahb_clk = {
	.halt_reg = 0x3c48,
	.clkr = {
		.enable_reg = 0x3c48,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmagic_camss_noc_cfg_ahb_clk",
			.parent_names = (const char *[]){ "gcc_mmss_noc_cfg_ahb_clk" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch smmu_vfe_ahb_clk = {
	.halt_reg = 0x3c04,
	.clkr = {
		.enable_reg = 0x3c04,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "smmu_vfe_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch smmu_vfe_axi_clk = {
	.halt_reg = 0x3c08,
	.clkr = {
		.enable_reg = 0x3c08,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "smmu_vfe_axi_clk",
			.parent_names = (const char *[]){ "axi_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch smmu_cpp_ahb_clk = {
	.halt_reg = 0x3c14,
	.clkr = {
		.enable_reg = 0x3c14,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "smmu_cpp_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch smmu_cpp_axi_clk = {
	.halt_reg = 0x3c18,
	.clkr = {
		.enable_reg = 0x3c18,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "smmu_cpp_axi_clk",
			.parent_names = (const char *[]){ "axi_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch smmu_jpeg_ahb_clk = {
	.halt_reg = 0x3c24,
	.clkr = {
		.enable_reg = 0x3c24,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "smmu_jpeg_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch smmu_jpeg_axi_clk = {
	.halt_reg = 0x3c28,
	.clkr = {
		.enable_reg = 0x3c28,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "smmu_jpeg_axi_clk",
			.parent_names = (const char *[]){ "axi_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmagic_mdss_axi_clk = {
	.halt_reg = 0x2474,
	.clkr = {
		.enable_reg = 0x2474,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmagic_mdss_axi_clk",
			.parent_names = (const char *[]){ "axi_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmagic_mdss_noc_cfg_ahb_clk = {
	.halt_reg = 0x2478,
	.clkr = {
		.enable_reg = 0x2478,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmagic_mdss_noc_cfg_ahb_clk",
			.parent_names = (const char *[]){ "gcc_mmss_noc_cfg_ahb_clk" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch smmu_rot_ahb_clk = {
	.halt_reg = 0x2444,
	.clkr = {
		.enable_reg = 0x2444,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "smmu_rot_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch smmu_rot_axi_clk = {
	.halt_reg = 0x2448,
	.clkr = {
		.enable_reg = 0x2448,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "smmu_rot_axi_clk",
			.parent_names = (const char *[]){ "axi_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch smmu_mdp_ahb_clk = {
	.halt_reg = 0x2454,
	.clkr = {
		.enable_reg = 0x2454,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "smmu_mdp_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch smmu_mdp_axi_clk = {
	.halt_reg = 0x2458,
	.clkr = {
		.enable_reg = 0x2458,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "smmu_mdp_axi_clk",
			.parent_names = (const char *[]){ "axi_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmagic_video_axi_clk = {
	.halt_reg = 0x1194,
	.clkr = {
		.enable_reg = 0x1194,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmagic_video_axi_clk",
			.parent_names = (const char *[]){ "axi_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmagic_video_noc_cfg_ahb_clk = {
	.halt_reg = 0x1198,
	.clkr = {
		.enable_reg = 0x1198,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmagic_video_noc_cfg_ahb_clk",
			.parent_names = (const char *[]){ "gcc_mmss_noc_cfg_ahb_clk" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch smmu_video_ahb_clk = {
	.halt_reg = 0x1174,
	.clkr = {
		.enable_reg = 0x1174,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "smmu_video_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch smmu_video_axi_clk = {
	.halt_reg = 0x1178,
	.clkr = {
		.enable_reg = 0x1178,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "smmu_video_axi_clk",
			.parent_names = (const char *[]){ "axi_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmagic_bimc_noc_cfg_ahb_clk = {
	.halt_reg = 0x5298,
	.clkr = {
		.enable_reg = 0x5298,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmagic_bimc_noc_cfg_ahb_clk",
			.parent_names = (const char *[]){ "gcc_mmss_noc_cfg_ahb_clk" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_gx_gfx3d_clk = {
	.halt_reg = 0x4028,
	.clkr = {
		.enable_reg = 0x4028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_gx_gfx3d_clk",
			.parent_names = (const char *[]){ "gfx3d_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_gx_rbbmtimer_clk = {
	.halt_reg = 0x40b0,
	.clkr = {
		.enable_reg = 0x40b0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_gx_rbbmtimer_clk",
			.parent_names = (const char *[]){ "rbbmtimer_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_ahb_clk = {
	.halt_reg = 0x403c,
	.clkr = {
		.enable_reg = 0x403c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_aon_isense_clk = {
	.halt_reg = 0x4044,
	.clkr = {
		.enable_reg = 0x4044,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_aon_isense_clk",
			.parent_names = (const char *[]){ "isense_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch vmem_maxi_clk = {
	.halt_reg = 0x1204,
	.clkr = {
		.enable_reg = 0x1204,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "vmem_maxi_clk",
			.parent_names = (const char *[]){ "maxi_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch vmem_ahb_clk = {
	.halt_reg = 0x1208,
	.clkr = {
		.enable_reg = 0x1208,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "vmem_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_rbcpr_clk = {
	.halt_reg = 0x4084,
	.clkr = {
		.enable_reg = 0x4084,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_rbcpr_clk",
			.parent_names = (const char *[]){ "rbcpr_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_rbcpr_ahb_clk = {
	.halt_reg = 0x4088,
	.clkr = {
		.enable_reg = 0x4088,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_rbcpr_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_core_clk = {
	.halt_reg = 0x1028,
	.clkr = {
		.enable_reg = 0x1028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_core_clk",
			.parent_names = (const char *[]){ "video_core_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_axi_clk = {
	.halt_reg = 0x1034,
	.clkr = {
		.enable_reg = 0x1034,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_axi_clk",
			.parent_names = (const char *[]){ "axi_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_maxi_clk = {
	.halt_reg = 0x1038,
	.clkr = {
		.enable_reg = 0x1038,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_maxi_clk",
			.parent_names = (const char *[]){ "maxi_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_ahb_clk = {
	.halt_reg = 0x1030,
	.clkr = {
		.enable_reg = 0x1030,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_subcore0_clk = {
	.halt_reg = 0x1048,
	.clkr = {
		.enable_reg = 0x1048,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_subcore0_clk",
			.parent_names = (const char *[]){ "video_subcore0_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_subcore1_clk = {
	.halt_reg = 0x104c,
	.clkr = {
		.enable_reg = 0x104c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_subcore1_clk",
			.parent_names = (const char *[]){ "video_subcore1_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_ahb_clk = {
	.halt_reg = 0x2308,
	.clkr = {
		.enable_reg = 0x2308,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_hdmi_ahb_clk = {
	.halt_reg = 0x230c,
	.clkr = {
		.enable_reg = 0x230c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_hdmi_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_axi_clk = {
	.halt_reg = 0x2310,
	.clkr = {
		.enable_reg = 0x2310,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_axi_clk",
			.parent_names = (const char *[]){ "axi_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_pclk0_clk = {
	.halt_reg = 0x2314,
	.clkr = {
		.enable_reg = 0x2314,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_pclk0_clk",
			.parent_names = (const char *[]){ "pclk0_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_pclk1_clk = {
	.halt_reg = 0x2318,
	.clkr = {
		.enable_reg = 0x2318,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_pclk1_clk",
			.parent_names = (const char *[]){ "pclk1_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_mdp_clk = {
	.halt_reg = 0x231c,
	.clkr = {
		.enable_reg = 0x231c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_mdp_clk",
			.parent_names = (const char *[]){ "mdp_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_extpclk_clk = {
	.halt_reg = 0x2324,
	.clkr = {
		.enable_reg = 0x2324,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_extpclk_clk",
			.parent_names = (const char *[]){ "extpclk_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_vsync_clk = {
	.halt_reg = 0x2328,
	.clkr = {
		.enable_reg = 0x2328,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_vsync_clk",
			.parent_names = (const char *[]){ "vsync_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_hdmi_clk = {
	.halt_reg = 0x2338,
	.clkr = {
		.enable_reg = 0x2338,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_hdmi_clk",
			.parent_names = (const char *[]){ "hdmi_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_byte0_clk = {
	.halt_reg = 0x233c,
	.clkr = {
		.enable_reg = 0x233c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_byte0_clk",
			.parent_names = (const char *[]){ "byte0_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_byte1_clk = {
	.halt_reg = 0x2340,
	.clkr = {
		.enable_reg = 0x2340,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_byte1_clk",
			.parent_names = (const char *[]){ "byte1_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_esc0_clk = {
	.halt_reg = 0x2344,
	.clkr = {
		.enable_reg = 0x2344,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_esc0_clk",
			.parent_names = (const char *[]){ "esc0_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_esc1_clk = {
	.halt_reg = 0x2348,
	.clkr = {
		.enable_reg = 0x2348,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_esc1_clk",
			.parent_names = (const char *[]){ "esc1_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_top_ahb_clk = {
	.halt_reg = 0x3484,
	.clkr = {
		.enable_reg = 0x3484,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_top_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_ahb_clk = {
	.halt_reg = 0x348c,
	.clkr = {
		.enable_reg = 0x348c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_micro_ahb_clk = {
	.halt_reg = 0x3494,
	.clkr = {
		.enable_reg = 0x3494,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_micro_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_gp0_clk = {
	.halt_reg = 0x3444,
	.clkr = {
		.enable_reg = 0x3444,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_gp0_clk",
			.parent_names = (const char *[]){ "camss_gp0_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_gp1_clk = {
	.halt_reg = 0x3474,
	.clkr = {
		.enable_reg = 0x3474,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_gp1_clk",
			.parent_names = (const char *[]){ "camss_gp1_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_mclk0_clk = {
	.halt_reg = 0x3384,
	.clkr = {
		.enable_reg = 0x3384,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_mclk0_clk",
			.parent_names = (const char *[]){ "mclk0_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_mclk1_clk = {
	.halt_reg = 0x33b4,
	.clkr = {
		.enable_reg = 0x33b4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_mclk1_clk",
			.parent_names = (const char *[]){ "mclk1_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_mclk2_clk = {
	.halt_reg = 0x33e4,
	.clkr = {
		.enable_reg = 0x33e4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_mclk2_clk",
			.parent_names = (const char *[]){ "mclk2_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_mclk3_clk = {
	.halt_reg = 0x3414,
	.clkr = {
		.enable_reg = 0x3414,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_mclk3_clk",
			.parent_names = (const char *[]){ "mclk3_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_cci_clk = {
	.halt_reg = 0x3344,
	.clkr = {
		.enable_reg = 0x3344,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_cci_clk",
			.parent_names = (const char *[]){ "cci_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_cci_ahb_clk = {
	.halt_reg = 0x3348,
	.clkr = {
		.enable_reg = 0x3348,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_cci_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi0phytimer_clk = {
	.halt_reg = 0x3024,
	.clkr = {
		.enable_reg = 0x3024,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi0phytimer_clk",
			.parent_names = (const char *[]){ "csi0phytimer_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi1phytimer_clk = {
	.halt_reg = 0x3054,
	.clkr = {
		.enable_reg = 0x3054,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi1phytimer_clk",
			.parent_names = (const char *[]){ "csi1phytimer_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi2phytimer_clk = {
	.halt_reg = 0x3084,
	.clkr = {
		.enable_reg = 0x3084,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi2phytimer_clk",
			.parent_names = (const char *[]){ "csi2phytimer_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csiphy0_3p_clk = {
	.halt_reg = 0x3234,
	.clkr = {
		.enable_reg = 0x3234,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csiphy0_3p_clk",
			.parent_names = (const char *[]){ "csiphy0_3p_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csiphy1_3p_clk = {
	.halt_reg = 0x3254,
	.clkr = {
		.enable_reg = 0x3254,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csiphy1_3p_clk",
			.parent_names = (const char *[]){ "csiphy1_3p_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csiphy2_3p_clk = {
	.halt_reg = 0x3274,
	.clkr = {
		.enable_reg = 0x3274,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csiphy2_3p_clk",
			.parent_names = (const char *[]){ "csiphy2_3p_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_jpeg0_clk = {
	.halt_reg = 0x35a8,
	.clkr = {
		.enable_reg = 0x35a8,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_jpeg0_clk",
			.parent_names = (const char *[]){ "jpeg0_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_jpeg2_clk = {
	.halt_reg = 0x35b0,
	.clkr = {
		.enable_reg = 0x35b0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_jpeg2_clk",
			.parent_names = (const char *[]){ "jpeg2_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_jpeg_dma_clk = {
	.halt_reg = 0x35c0,
	.clkr = {
		.enable_reg = 0x35c0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_jpeg_dma_clk",
			.parent_names = (const char *[]){ "jpeg_dma_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_jpeg_ahb_clk = {
	.halt_reg = 0x35b4,
	.clkr = {
		.enable_reg = 0x35b4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_jpeg_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_jpeg_axi_clk = {
	.halt_reg = 0x35b8,
	.clkr = {
		.enable_reg = 0x35b8,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_jpeg_axi_clk",
			.parent_names = (const char *[]){ "axi_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_vfe_ahb_clk = {
	.halt_reg = 0x36b8,
	.clkr = {
		.enable_reg = 0x36b8,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_vfe_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_vfe_axi_clk = {
	.halt_reg = 0x36bc,
	.clkr = {
		.enable_reg = 0x36bc,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_vfe_axi_clk",
			.parent_names = (const char *[]){ "axi_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_vfe0_clk = {
	.halt_reg = 0x36a8,
	.clkr = {
		.enable_reg = 0x36a8,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_vfe0_clk",
			.parent_names = (const char *[]){ "vfe0_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_vfe0_stream_clk = {
	.halt_reg = 0x3720,
	.clkr = {
		.enable_reg = 0x3720,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_vfe0_stream_clk",
			.parent_names = (const char *[]){ "vfe0_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_vfe0_ahb_clk = {
	.halt_reg = 0x3668,
	.clkr = {
		.enable_reg = 0x3668,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_vfe0_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_vfe1_clk = {
	.halt_reg = 0x36ac,
	.clkr = {
		.enable_reg = 0x36ac,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_vfe1_clk",
			.parent_names = (const char *[]){ "vfe1_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_vfe1_stream_clk = {
	.halt_reg = 0x3724,
	.clkr = {
		.enable_reg = 0x3724,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_vfe1_stream_clk",
			.parent_names = (const char *[]){ "vfe1_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_vfe1_ahb_clk = {
	.halt_reg = 0x3678,
	.clkr = {
		.enable_reg = 0x3678,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_vfe1_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi_vfe0_clk = {
	.halt_reg = 0x3704,
	.clkr = {
		.enable_reg = 0x3704,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi_vfe0_clk",
			.parent_names = (const char *[]){ "vfe0_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi_vfe1_clk = {
	.halt_reg = 0x3714,
	.clkr = {
		.enable_reg = 0x3714,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi_vfe1_clk",
			.parent_names = (const char *[]){ "vfe1_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_cpp_vbif_ahb_clk = {
	.halt_reg = 0x36c8,
	.clkr = {
		.enable_reg = 0x36c8,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_cpp_vbif_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_cpp_axi_clk = {
	.halt_reg = 0x36c4,
	.clkr = {
		.enable_reg = 0x36c4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_cpp_axi_clk",
			.parent_names = (const char *[]){ "axi_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_cpp_clk = {
	.halt_reg = 0x36b0,
	.clkr = {
		.enable_reg = 0x36b0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_cpp_clk",
			.parent_names = (const char *[]){ "cpp_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_cpp_ahb_clk = {
	.halt_reg = 0x36b4,
	.clkr = {
		.enable_reg = 0x36b4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_cpp_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi0_clk = {
	.halt_reg = 0x30b4,
	.clkr = {
		.enable_reg = 0x30b4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi0_clk",
			.parent_names = (const char *[]){ "csi0_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi0_ahb_clk = {
	.halt_reg = 0x30bc,
	.clkr = {
		.enable_reg = 0x30bc,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi0_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi0phy_clk = {
	.halt_reg = 0x30c4,
	.clkr = {
		.enable_reg = 0x30c4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi0phy_clk",
			.parent_names = (const char *[]){ "csi0_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi0rdi_clk = {
	.halt_reg = 0x30d4,
	.clkr = {
		.enable_reg = 0x30d4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi0rdi_clk",
			.parent_names = (const char *[]){ "csi0_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi0pix_clk = {
	.halt_reg = 0x30e4,
	.clkr = {
		.enable_reg = 0x30e4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi0pix_clk",
			.parent_names = (const char *[]){ "csi0_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi1_clk = {
	.halt_reg = 0x3124,
	.clkr = {
		.enable_reg = 0x3124,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi1_clk",
			.parent_names = (const char *[]){ "csi1_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi1_ahb_clk = {
	.halt_reg = 0x3128,
	.clkr = {
		.enable_reg = 0x3128,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi1_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi1phy_clk = {
	.halt_reg = 0x3134,
	.clkr = {
		.enable_reg = 0x3134,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi1phy_clk",
			.parent_names = (const char *[]){ "csi1_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi1rdi_clk = {
	.halt_reg = 0x3144,
	.clkr = {
		.enable_reg = 0x3144,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi1rdi_clk",
			.parent_names = (const char *[]){ "csi1_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi1pix_clk = {
	.halt_reg = 0x3154,
	.clkr = {
		.enable_reg = 0x3154,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi1pix_clk",
			.parent_names = (const char *[]){ "csi1_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi2_clk = {
	.halt_reg = 0x3184,
	.clkr = {
		.enable_reg = 0x3184,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi2_clk",
			.parent_names = (const char *[]){ "csi2_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi2_ahb_clk = {
	.halt_reg = 0x3188,
	.clkr = {
		.enable_reg = 0x3188,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi2_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi2phy_clk = {
	.halt_reg = 0x3194,
	.clkr = {
		.enable_reg = 0x3194,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi2phy_clk",
			.parent_names = (const char *[]){ "csi2_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi2rdi_clk = {
	.halt_reg = 0x31a4,
	.clkr = {
		.enable_reg = 0x31a4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi2rdi_clk",
			.parent_names = (const char *[]){ "csi2_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi2pix_clk = {
	.halt_reg = 0x31b4,
	.clkr = {
		.enable_reg = 0x31b4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi2pix_clk",
			.parent_names = (const char *[]){ "csi2_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi3_clk = {
	.halt_reg = 0x31e4,
	.clkr = {
		.enable_reg = 0x31e4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi3_clk",
			.parent_names = (const char *[]){ "csi3_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi3_ahb_clk = {
	.halt_reg = 0x31e8,
	.clkr = {
		.enable_reg = 0x31e8,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi3_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi3phy_clk = {
	.halt_reg = 0x31f4,
	.clkr = {
		.enable_reg = 0x31f4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi3phy_clk",
			.parent_names = (const char *[]){ "csi3_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi3rdi_clk = {
	.halt_reg = 0x3204,
	.clkr = {
		.enable_reg = 0x3204,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi3rdi_clk",
			.parent_names = (const char *[]){ "csi3_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi3pix_clk = {
	.halt_reg = 0x3214,
	.clkr = {
		.enable_reg = 0x3214,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi3pix_clk",
			.parent_names = (const char *[]){ "csi3_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_ispif_ahb_clk = {
	.halt_reg = 0x3224,
	.clkr = {
		.enable_reg = 0x3224,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_ispif_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch fd_core_clk = {
	.halt_reg = 0x3b68,
	.clkr = {
		.enable_reg = 0x3b68,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "fd_core_clk",
			.parent_names = (const char *[]){ "fd_core_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch fd_core_uar_clk = {
	.halt_reg = 0x3b6c,
	.clkr = {
		.enable_reg = 0x3b6c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "fd_core_uar_clk",
			.parent_names = (const char *[]){ "fd_core_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch fd_ahb_clk = {
	.halt_reg = 0x3ba74,
	.clkr = {
		.enable_reg = 0x3ba74,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "fd_ahb_clk",
			.parent_names = (const char *[]){ "ahb_clk_src" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_hw *mmcc_msm8996_hws[] = {
	&gpll0_div.hw,
};

static struct gdsc mmagic_bimc_gdsc = {
	.gdscr = 0x529c,
	.pd = {
		.name = "mmagic_bimc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = ALWAYS_ON,
};

static struct gdsc mmagic_video_gdsc = {
	.gdscr = 0x119c,
	.gds_hw_ctrl = 0x120c,
	.pd = {
		.name = "mmagic_video",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE | ALWAYS_ON,
};

static struct gdsc mmagic_mdss_gdsc = {
	.gdscr = 0x247c,
	.gds_hw_ctrl = 0x2480,
	.pd = {
		.name = "mmagic_mdss",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE | ALWAYS_ON,
};

static struct gdsc mmagic_camss_gdsc = {
	.gdscr = 0x3c4c,
	.gds_hw_ctrl = 0x3c50,
	.pd = {
		.name = "mmagic_camss",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE | ALWAYS_ON,
};

static struct gdsc venus_gdsc = {
	.gdscr = 0x1024,
	.cxcs = (unsigned int []){ 0x1028, 0x1034, 0x1038 },
	.cxc_count = 3,
	.pd = {
		.name = "venus",
	},
	.parent = &mmagic_video_gdsc.pd,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc venus_core0_gdsc = {
	.gdscr = 0x1040,
	.cxcs = (unsigned int []){ 0x1048 },
	.cxc_count = 1,
	.pd = {
		.name = "venus_core0",
	},
	.parent = &venus_gdsc.pd,
	.pwrsts = PWRSTS_OFF_ON,
	.flags = HW_CTRL,
};

static struct gdsc venus_core1_gdsc = {
	.gdscr = 0x1044,
	.cxcs = (unsigned int []){ 0x104c },
	.cxc_count = 1,
	.pd = {
		.name = "venus_core1",
	},
	.parent = &venus_gdsc.pd,
	.pwrsts = PWRSTS_OFF_ON,
	.flags = HW_CTRL,
};

static struct gdsc camss_gdsc = {
	.gdscr = 0x34a0,
	.cxcs = (unsigned int []){ 0x36bc, 0x36c4 },
	.cxc_count = 2,
	.pd = {
		.name = "camss",
	},
	.parent = &mmagic_camss_gdsc.pd,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc vfe0_gdsc = {
	.gdscr = 0x3664,
	.cxcs = (unsigned int []){ 0x36a8 },
	.cxc_count = 1,
	.pd = {
		.name = "vfe0",
	},
	.parent = &camss_gdsc.pd,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc vfe1_gdsc = {
	.gdscr = 0x3674,
	.cxcs = (unsigned int []){ 0x36ac },
	.cxc_count = 1,
	.pd = {
		.name = "vfe1",
	},
	.parent = &camss_gdsc.pd,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc jpeg_gdsc = {
	.gdscr = 0x35a4,
	.cxcs = (unsigned int []){ 0x35a8, 0x35b0, 0x35c0, 0x35b8 },
	.cxc_count = 4,
	.pd = {
		.name = "jpeg",
	},
	.parent = &camss_gdsc.pd,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc cpp_gdsc = {
	.gdscr = 0x36d4,
	.cxcs = (unsigned int []){ 0x36b0 },
	.cxc_count = 1,
	.pd = {
		.name = "cpp",
	},
	.parent = &camss_gdsc.pd,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc fd_gdsc = {
	.gdscr = 0x3b64,
	.cxcs = (unsigned int []){ 0x3b68, 0x3b6c },
	.cxc_count = 2,
	.pd = {
		.name = "fd",
	},
	.parent = &camss_gdsc.pd,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc mdss_gdsc = {
	.gdscr = 0x2304,
	.cxcs = (unsigned int []){ 0x2310, 0x231c },
	.cxc_count = 2,
	.pd = {
		.name = "mdss",
	},
	.parent = &mmagic_mdss_gdsc.pd,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc gpu_gdsc = {
	.gdscr = 0x4034,
	.gds_hw_ctrl = 0x4038,
	.pd = {
		.name = "gpu",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc gpu_gx_gdsc = {
	.gdscr = 0x4024,
	.clamp_io_ctrl = 0x4300,
	.cxcs = (unsigned int []){ 0x4028 },
	.cxc_count = 1,
	.pd = {
		.name = "gpu_gx",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = CLAMP_IO,
};

static struct clk_regmap *mmcc_msm8996_clocks[] = {
	[MMPLL0_EARLY] = &mmpll0_early.clkr,
	[MMPLL0_PLL] = &mmpll0.clkr,
	[MMPLL1_EARLY] = &mmpll1_early.clkr,
	[MMPLL1_PLL] = &mmpll1.clkr,
	[MMPLL2_EARLY] = &mmpll2_early.clkr,
	[MMPLL2_PLL] = &mmpll2.clkr,
	[MMPLL3_EARLY] = &mmpll3_early.clkr,
	[MMPLL3_PLL] = &mmpll3.clkr,
	[MMPLL4_EARLY] = &mmpll4_early.clkr,
	[MMPLL4_PLL] = &mmpll4.clkr,
	[MMPLL5_EARLY] = &mmpll5_early.clkr,
	[MMPLL5_PLL] = &mmpll5.clkr,
	[MMPLL8_EARLY] = &mmpll8_early.clkr,
	[MMPLL8_PLL] = &mmpll8.clkr,
	[MMPLL9_EARLY] = &mmpll9_early.clkr,
	[MMPLL9_PLL] = &mmpll9.clkr,
	[AHB_CLK_SRC] = &ahb_clk_src.clkr,
	[AXI_CLK_SRC] = &axi_clk_src.clkr,
	[MAXI_CLK_SRC] = &maxi_clk_src.clkr,
	[GFX3D_CLK_SRC] = &gfx3d_clk_src.clkr,
	[RBBMTIMER_CLK_SRC] = &rbbmtimer_clk_src.clkr,
	[ISENSE_CLK_SRC] = &isense_clk_src.clkr,
	[RBCPR_CLK_SRC] = &rbcpr_clk_src.clkr,
	[VIDEO_CORE_CLK_SRC] = &video_core_clk_src.clkr,
	[VIDEO_SUBCORE0_CLK_SRC] = &video_subcore0_clk_src.clkr,
	[VIDEO_SUBCORE1_CLK_SRC] = &video_subcore1_clk_src.clkr,
	[PCLK0_CLK_SRC] = &pclk0_clk_src.clkr,
	[PCLK1_CLK_SRC] = &pclk1_clk_src.clkr,
	[MDP_CLK_SRC] = &mdp_clk_src.clkr,
	[EXTPCLK_CLK_SRC] = &extpclk_clk_src.clkr,
	[VSYNC_CLK_SRC] = &vsync_clk_src.clkr,
	[HDMI_CLK_SRC] = &hdmi_clk_src.clkr,
	[BYTE0_CLK_SRC] = &byte0_clk_src.clkr,
	[BYTE1_CLK_SRC] = &byte1_clk_src.clkr,
	[ESC0_CLK_SRC] = &esc0_clk_src.clkr,
	[ESC1_CLK_SRC] = &esc1_clk_src.clkr,
	[CAMSS_GP0_CLK_SRC] = &camss_gp0_clk_src.clkr,
	[CAMSS_GP1_CLK_SRC] = &camss_gp1_clk_src.clkr,
	[MCLK0_CLK_SRC] = &mclk0_clk_src.clkr,
	[MCLK1_CLK_SRC] = &mclk1_clk_src.clkr,
	[MCLK2_CLK_SRC] = &mclk2_clk_src.clkr,
	[MCLK3_CLK_SRC] = &mclk3_clk_src.clkr,
	[CCI_CLK_SRC] = &cci_clk_src.clkr,
	[CSI0PHYTIMER_CLK_SRC] = &csi0phytimer_clk_src.clkr,
	[CSI1PHYTIMER_CLK_SRC] = &csi1phytimer_clk_src.clkr,
	[CSI2PHYTIMER_CLK_SRC] = &csi2phytimer_clk_src.clkr,
	[CSIPHY0_3P_CLK_SRC] = &csiphy0_3p_clk_src.clkr,
	[CSIPHY1_3P_CLK_SRC] = &csiphy1_3p_clk_src.clkr,
	[CSIPHY2_3P_CLK_SRC] = &csiphy2_3p_clk_src.clkr,
	[JPEG0_CLK_SRC] = &jpeg0_clk_src.clkr,
	[JPEG2_CLK_SRC] = &jpeg2_clk_src.clkr,
	[JPEG_DMA_CLK_SRC] = &jpeg_dma_clk_src.clkr,
	[VFE0_CLK_SRC] = &vfe0_clk_src.clkr,
	[VFE1_CLK_SRC] = &vfe1_clk_src.clkr,
	[CPP_CLK_SRC] = &cpp_clk_src.clkr,
	[CSI0_CLK_SRC] = &csi0_clk_src.clkr,
	[CSI1_CLK_SRC] = &csi1_clk_src.clkr,
	[CSI2_CLK_SRC] = &csi2_clk_src.clkr,
	[CSI3_CLK_SRC] = &csi3_clk_src.clkr,
	[FD_CORE_CLK_SRC] = &fd_core_clk_src.clkr,
	[MMSS_MMAGIC_AHB_CLK] = &mmss_mmagic_ahb_clk.clkr,
	[MMSS_MMAGIC_CFG_AHB_CLK] = &mmss_mmagic_cfg_ahb_clk.clkr,
	[MMSS_MISC_AHB_CLK] = &mmss_misc_ahb_clk.clkr,
	[MMSS_MISC_CXO_CLK] = &mmss_misc_cxo_clk.clkr,
	[MMSS_MMAGIC_MAXI_CLK] = &mmss_mmagic_maxi_clk.clkr,
	[MMAGIC_CAMSS_AXI_CLK] = &mmagic_camss_axi_clk.clkr,
	[MMAGIC_CAMSS_NOC_CFG_AHB_CLK] = &mmagic_camss_noc_cfg_ahb_clk.clkr,
	[SMMU_VFE_AHB_CLK] = &smmu_vfe_ahb_clk.clkr,
	[SMMU_VFE_AXI_CLK] = &smmu_vfe_axi_clk.clkr,
	[SMMU_CPP_AHB_CLK] = &smmu_cpp_ahb_clk.clkr,
	[SMMU_CPP_AXI_CLK] = &smmu_cpp_axi_clk.clkr,
	[SMMU_JPEG_AHB_CLK] = &smmu_jpeg_ahb_clk.clkr,
	[SMMU_JPEG_AXI_CLK] = &smmu_jpeg_axi_clk.clkr,
	[MMAGIC_MDSS_AXI_CLK] = &mmagic_mdss_axi_clk.clkr,
	[MMAGIC_MDSS_NOC_CFG_AHB_CLK] = &mmagic_mdss_noc_cfg_ahb_clk.clkr,
	[SMMU_ROT_AHB_CLK] = &smmu_rot_ahb_clk.clkr,
	[SMMU_ROT_AXI_CLK] = &smmu_rot_axi_clk.clkr,
	[SMMU_MDP_AHB_CLK] = &smmu_mdp_ahb_clk.clkr,
	[SMMU_MDP_AXI_CLK] = &smmu_mdp_axi_clk.clkr,
	[MMAGIC_VIDEO_AXI_CLK] = &mmagic_video_axi_clk.clkr,
	[MMAGIC_VIDEO_NOC_CFG_AHB_CLK] = &mmagic_video_noc_cfg_ahb_clk.clkr,
	[SMMU_VIDEO_AHB_CLK] = &smmu_video_ahb_clk.clkr,
	[SMMU_VIDEO_AXI_CLK] = &smmu_video_axi_clk.clkr,
	[MMAGIC_BIMC_NOC_CFG_AHB_CLK] = &mmagic_bimc_noc_cfg_ahb_clk.clkr,
	[GPU_GX_GFX3D_CLK] = &gpu_gx_gfx3d_clk.clkr,
	[GPU_GX_RBBMTIMER_CLK] = &gpu_gx_rbbmtimer_clk.clkr,
	[GPU_AHB_CLK] = &gpu_ahb_clk.clkr,
	[GPU_AON_ISENSE_CLK] = &gpu_aon_isense_clk.clkr,
	[VMEM_MAXI_CLK] = &vmem_maxi_clk.clkr,
	[VMEM_AHB_CLK] = &vmem_ahb_clk.clkr,
	[MMSS_RBCPR_CLK] = &mmss_rbcpr_clk.clkr,
	[MMSS_RBCPR_AHB_CLK] = &mmss_rbcpr_ahb_clk.clkr,
	[VIDEO_CORE_CLK] = &video_core_clk.clkr,
	[VIDEO_AXI_CLK] = &video_axi_clk.clkr,
	[VIDEO_MAXI_CLK] = &video_maxi_clk.clkr,
	[VIDEO_AHB_CLK] = &video_ahb_clk.clkr,
	[VIDEO_SUBCORE0_CLK] = &video_subcore0_clk.clkr,
	[VIDEO_SUBCORE1_CLK] = &video_subcore1_clk.clkr,
	[MDSS_AHB_CLK] = &mdss_ahb_clk.clkr,
	[MDSS_HDMI_AHB_CLK] = &mdss_hdmi_ahb_clk.clkr,
	[MDSS_AXI_CLK] = &mdss_axi_clk.clkr,
	[MDSS_PCLK0_CLK] = &mdss_pclk0_clk.clkr,
	[MDSS_PCLK1_CLK] = &mdss_pclk1_clk.clkr,
	[MDSS_MDP_CLK] = &mdss_mdp_clk.clkr,
	[MDSS_EXTPCLK_CLK] = &mdss_extpclk_clk.clkr,
	[MDSS_VSYNC_CLK] = &mdss_vsync_clk.clkr,
	[MDSS_HDMI_CLK] = &mdss_hdmi_clk.clkr,
	[MDSS_BYTE0_CLK] = &mdss_byte0_clk.clkr,
	[MDSS_BYTE1_CLK] = &mdss_byte1_clk.clkr,
	[MDSS_ESC0_CLK] = &mdss_esc0_clk.clkr,
	[MDSS_ESC1_CLK] = &mdss_esc1_clk.clkr,
	[CAMSS_TOP_AHB_CLK] = &camss_top_ahb_clk.clkr,
	[CAMSS_AHB_CLK] = &camss_ahb_clk.clkr,
	[CAMSS_MICRO_AHB_CLK] = &camss_micro_ahb_clk.clkr,
	[CAMSS_GP0_CLK] = &camss_gp0_clk.clkr,
	[CAMSS_GP1_CLK] = &camss_gp1_clk.clkr,
	[CAMSS_MCLK0_CLK] = &camss_mclk0_clk.clkr,
	[CAMSS_MCLK1_CLK] = &camss_mclk1_clk.clkr,
	[CAMSS_MCLK2_CLK] = &camss_mclk2_clk.clkr,
	[CAMSS_MCLK3_CLK] = &camss_mclk3_clk.clkr,
	[CAMSS_CCI_CLK] = &camss_cci_clk.clkr,
	[CAMSS_CCI_AHB_CLK] = &camss_cci_ahb_clk.clkr,
	[CAMSS_CSI0PHYTIMER_CLK] = &camss_csi0phytimer_clk.clkr,
	[CAMSS_CSI1PHYTIMER_CLK] = &camss_csi1phytimer_clk.clkr,
	[CAMSS_CSI2PHYTIMER_CLK] = &camss_csi2phytimer_clk.clkr,
	[CAMSS_CSIPHY0_3P_CLK] = &camss_csiphy0_3p_clk.clkr,
	[CAMSS_CSIPHY1_3P_CLK] = &camss_csiphy1_3p_clk.clkr,
	[CAMSS_CSIPHY2_3P_CLK] = &camss_csiphy2_3p_clk.clkr,
	[CAMSS_JPEG0_CLK] = &camss_jpeg0_clk.clkr,
	[CAMSS_JPEG2_CLK] = &camss_jpeg2_clk.clkr,
	[CAMSS_JPEG_DMA_CLK] = &camss_jpeg_dma_clk.clkr,
	[CAMSS_JPEG_AHB_CLK] = &camss_jpeg_ahb_clk.clkr,
	[CAMSS_JPEG_AXI_CLK] = &camss_jpeg_axi_clk.clkr,
	[CAMSS_VFE_AHB_CLK] = &camss_vfe_ahb_clk.clkr,
	[CAMSS_VFE_AXI_CLK] = &camss_vfe_axi_clk.clkr,
	[CAMSS_VFE0_CLK] = &camss_vfe0_clk.clkr,
	[CAMSS_VFE0_STREAM_CLK] = &camss_vfe0_stream_clk.clkr,
	[CAMSS_VFE0_AHB_CLK] = &camss_vfe0_ahb_clk.clkr,
	[CAMSS_VFE1_CLK] = &camss_vfe1_clk.clkr,
	[CAMSS_VFE1_STREAM_CLK] = &camss_vfe1_stream_clk.clkr,
	[CAMSS_VFE1_AHB_CLK] = &camss_vfe1_ahb_clk.clkr,
	[CAMSS_CSI_VFE0_CLK] = &camss_csi_vfe0_clk.clkr,
	[CAMSS_CSI_VFE1_CLK] = &camss_csi_vfe1_clk.clkr,
	[CAMSS_CPP_VBIF_AHB_CLK] = &camss_cpp_vbif_ahb_clk.clkr,
	[CAMSS_CPP_AXI_CLK] = &camss_cpp_axi_clk.clkr,
	[CAMSS_CPP_CLK] = &camss_cpp_clk.clkr,
	[CAMSS_CPP_AHB_CLK] = &camss_cpp_ahb_clk.clkr,
	[CAMSS_CSI0_CLK] = &camss_csi0_clk.clkr,
	[CAMSS_CSI0_AHB_CLK] = &camss_csi0_ahb_clk.clkr,
	[CAMSS_CSI0PHY_CLK] = &camss_csi0phy_clk.clkr,
	[CAMSS_CSI0RDI_CLK] = &camss_csi0rdi_clk.clkr,
	[CAMSS_CSI0PIX_CLK] = &camss_csi0pix_clk.clkr,
	[CAMSS_CSI1_CLK] = &camss_csi1_clk.clkr,
	[CAMSS_CSI1_AHB_CLK] = &camss_csi1_ahb_clk.clkr,
	[CAMSS_CSI1PHY_CLK] = &camss_csi1phy_clk.clkr,
	[CAMSS_CSI1RDI_CLK] = &camss_csi1rdi_clk.clkr,
	[CAMSS_CSI1PIX_CLK] = &camss_csi1pix_clk.clkr,
	[CAMSS_CSI2_CLK] = &camss_csi2_clk.clkr,
	[CAMSS_CSI2_AHB_CLK] = &camss_csi2_ahb_clk.clkr,
	[CAMSS_CSI2PHY_CLK] = &camss_csi2phy_clk.clkr,
	[CAMSS_CSI2RDI_CLK] = &camss_csi2rdi_clk.clkr,
	[CAMSS_CSI2PIX_CLK] = &camss_csi2pix_clk.clkr,
	[CAMSS_CSI3_CLK] = &camss_csi3_clk.clkr,
	[CAMSS_CSI3_AHB_CLK] = &camss_csi3_ahb_clk.clkr,
	[CAMSS_CSI3PHY_CLK] = &camss_csi3phy_clk.clkr,
	[CAMSS_CSI3RDI_CLK] = &camss_csi3rdi_clk.clkr,
	[CAMSS_CSI3PIX_CLK] = &camss_csi3pix_clk.clkr,
	[CAMSS_ISPIF_AHB_CLK] = &camss_ispif_ahb_clk.clkr,
	[FD_CORE_CLK] = &fd_core_clk.clkr,
	[FD_CORE_UAR_CLK] = &fd_core_uar_clk.clkr,
	[FD_AHB_CLK] = &fd_ahb_clk.clkr,
};

static struct gdsc *mmcc_msm8996_gdscs[] = {
	[MMAGIC_BIMC_GDSC] = &mmagic_bimc_gdsc,
	[MMAGIC_VIDEO_GDSC] = &mmagic_video_gdsc,
	[MMAGIC_MDSS_GDSC] = &mmagic_mdss_gdsc,
	[MMAGIC_CAMSS_GDSC] = &mmagic_camss_gdsc,
	[VENUS_GDSC] = &venus_gdsc,
	[VENUS_CORE0_GDSC] = &venus_core0_gdsc,
	[VENUS_CORE1_GDSC] = &venus_core1_gdsc,
	[CAMSS_GDSC] = &camss_gdsc,
	[VFE0_GDSC] = &vfe0_gdsc,
	[VFE1_GDSC] = &vfe1_gdsc,
	[JPEG_GDSC] = &jpeg_gdsc,
	[CPP_GDSC] = &cpp_gdsc,
	[FD_GDSC] = &fd_gdsc,
	[MDSS_GDSC] = &mdss_gdsc,
	[GPU_GDSC] = &gpu_gdsc,
	[GPU_GX_GDSC] = &gpu_gx_gdsc,
};

static const struct qcom_reset_map mmcc_msm8996_resets[] = {
	[MMAGICAHB_BCR] = { 0x5020 },
	[MMAGIC_CFG_BCR] = { 0x5050 },
	[MISC_BCR] = { 0x5010 },
	[BTO_BCR] = { 0x5030 },
	[MMAGICAXI_BCR] = { 0x5060 },
	[MMAGICMAXI_BCR] = { 0x5070 },
	[DSA_BCR] = { 0x50a0 },
	[MMAGIC_CAMSS_BCR] = { 0x3c40 },
	[THROTTLE_CAMSS_BCR] = { 0x3c30 },
	[SMMU_VFE_BCR] = { 0x3c00 },
	[SMMU_CPP_BCR] = { 0x3c10 },
	[SMMU_JPEG_BCR] = { 0x3c20 },
	[MMAGIC_MDSS_BCR] = { 0x2470 },
	[THROTTLE_MDSS_BCR] = { 0x2460 },
	[SMMU_ROT_BCR] = { 0x2440 },
	[SMMU_MDP_BCR] = { 0x2450 },
	[MMAGIC_VIDEO_BCR] = { 0x1190 },
	[THROTTLE_VIDEO_BCR] = { 0x1180 },
	[SMMU_VIDEO_BCR] = { 0x1170 },
	[MMAGIC_BIMC_BCR] = { 0x5290 },
	[GPU_GX_BCR] = { 0x4020 },
	[GPU_BCR] = { 0x4030 },
	[GPU_AON_BCR] = { 0x4040 },
	[VMEM_BCR] = { 0x1200 },
	[MMSS_RBCPR_BCR] = { 0x4080 },
	[VIDEO_BCR] = { 0x1020 },
	[MDSS_BCR] = { 0x2300 },
	[CAMSS_TOP_BCR] = { 0x3480 },
	[CAMSS_AHB_BCR] = { 0x3488 },
	[CAMSS_MICRO_BCR] = { 0x3490 },
	[CAMSS_CCI_BCR] = { 0x3340 },
	[CAMSS_PHY0_BCR] = { 0x3020 },
	[CAMSS_PHY1_BCR] = { 0x3050 },
	[CAMSS_PHY2_BCR] = { 0x3080 },
	[CAMSS_CSIPHY0_3P_BCR] = { 0x3230 },
	[CAMSS_CSIPHY1_3P_BCR] = { 0x3250 },
	[CAMSS_CSIPHY2_3P_BCR] = { 0x3270 },
	[CAMSS_JPEG_BCR] = { 0x35a0 },
	[CAMSS_VFE_BCR] = { 0x36a0 },
	[CAMSS_VFE0_BCR] = { 0x3660 },
	[CAMSS_VFE1_BCR] = { 0x3670 },
	[CAMSS_CSI_VFE0_BCR] = { 0x3700 },
	[CAMSS_CSI_VFE1_BCR] = { 0x3710 },
	[CAMSS_CPP_TOP_BCR] = { 0x36c0 },
	[CAMSS_CPP_BCR] = { 0x36d0 },
	[CAMSS_CSI0_BCR] = { 0x30b0 },
	[CAMSS_CSI0RDI_BCR] = { 0x30d0 },
	[CAMSS_CSI0PIX_BCR] = { 0x30e0 },
	[CAMSS_CSI1_BCR] = { 0x3120 },
	[CAMSS_CSI1RDI_BCR] = { 0x3140 },
	[CAMSS_CSI1PIX_BCR] = { 0x3150 },
	[CAMSS_CSI2_BCR] = { 0x3180 },
	[CAMSS_CSI2RDI_BCR] = { 0x31a0 },
	[CAMSS_CSI2PIX_BCR] = { 0x31b0 },
	[CAMSS_CSI3_BCR] = { 0x31e0 },
	[CAMSS_CSI3RDI_BCR] = { 0x3200 },
	[CAMSS_CSI3PIX_BCR] = { 0x3210 },
	[CAMSS_ISPIF_BCR] = { 0x3220 },
	[FD_BCR] = { 0x3b60 },
	[MMSS_SPDM_RM_BCR] = { 0x300 },
};

static const struct regmap_config mmcc_msm8996_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0xb008,
	.fast_io	= true,
};

static const struct qcom_cc_desc mmcc_msm8996_desc = {
	.config = &mmcc_msm8996_regmap_config,
	.clks = mmcc_msm8996_clocks,
	.num_clks = ARRAY_SIZE(mmcc_msm8996_clocks),
	.resets = mmcc_msm8996_resets,
	.num_resets = ARRAY_SIZE(mmcc_msm8996_resets),
	.gdscs = mmcc_msm8996_gdscs,
	.num_gdscs = ARRAY_SIZE(mmcc_msm8996_gdscs),
};

static const struct of_device_id mmcc_msm8996_match_table[] = {
	{ .compatible = "qcom,mmcc-msm8996" },
	{ }
};
MODULE_DEVICE_TABLE(of, mmcc_msm8996_match_table);

static int mmcc_msm8996_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int i, ret;
	struct regmap *regmap;

	regmap = qcom_cc_map(pdev, &mmcc_msm8996_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/* Disable the AHB DCD */
	regmap_update_bits(regmap, 0x50d8, BIT(31), 0);
	/* Disable the NoC FSM for mmss_mmagic_cfg_ahb_clk */
	regmap_update_bits(regmap, 0x5054, BIT(15), 0);

	for (i = 0; i < ARRAY_SIZE(mmcc_msm8996_hws); i++) {
		ret = devm_clk_hw_register(dev, mmcc_msm8996_hws[i]);
		if (ret)
			return ret;
	}

	return qcom_cc_really_probe(pdev, &mmcc_msm8996_desc, regmap);
}

static struct platform_driver mmcc_msm8996_driver = {
	.probe		= mmcc_msm8996_probe,
	.driver		= {
		.name	= "mmcc-msm8996",
		.of_match_table = mmcc_msm8996_match_table,
	},
};
module_platform_driver(mmcc_msm8996_driver);

MODULE_DESCRIPTION("QCOM MMCC MSM8996 Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:mmcc-msm8996");
