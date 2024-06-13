// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

#include <dt-bindings/clock/qcom,mmcc-apq8084.h>
#include <dt-bindings/reset/qcom,mmcc-apq8084.h>

#include "common.h"
#include "clk-regmap.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-branch.h"
#include "reset.h"
#include "gdsc.h"

enum {
	P_XO,
	P_MMPLL0,
	P_EDPLINK,
	P_MMPLL1,
	P_HDMIPLL,
	P_GPLL0,
	P_EDPVCO,
	P_MMPLL4,
	P_DSI0PLL,
	P_DSI0PLL_BYTE,
	P_MMPLL2,
	P_MMPLL3,
	P_GPLL1,
	P_DSI1PLL,
	P_DSI1PLL_BYTE,
	P_MMSLEEP,
};

static const struct parent_map mmcc_xo_mmpll0_mmpll1_gpll0_map[] = {
	{ P_XO, 0 },
	{ P_MMPLL0, 1 },
	{ P_MMPLL1, 2 },
	{ P_GPLL0, 5 }
};

static const char * const mmcc_xo_mmpll0_mmpll1_gpll0[] = {
	"xo",
	"mmpll0_vote",
	"mmpll1_vote",
	"mmss_gpll0_vote",
};

static const struct parent_map mmcc_xo_mmpll0_dsi_hdmi_gpll0_map[] = {
	{ P_XO, 0 },
	{ P_MMPLL0, 1 },
	{ P_HDMIPLL, 4 },
	{ P_GPLL0, 5 },
	{ P_DSI0PLL, 2 },
	{ P_DSI1PLL, 3 }
};

static const char * const mmcc_xo_mmpll0_dsi_hdmi_gpll0[] = {
	"xo",
	"mmpll0_vote",
	"hdmipll",
	"mmss_gpll0_vote",
	"dsi0pll",
	"dsi1pll",
};

static const struct parent_map mmcc_xo_mmpll0_1_2_gpll0_map[] = {
	{ P_XO, 0 },
	{ P_MMPLL0, 1 },
	{ P_MMPLL1, 2 },
	{ P_GPLL0, 5 },
	{ P_MMPLL2, 3 }
};

static const char * const mmcc_xo_mmpll0_1_2_gpll0[] = {
	"xo",
	"mmpll0_vote",
	"mmpll1_vote",
	"mmss_gpll0_vote",
	"mmpll2",
};

static const struct parent_map mmcc_xo_mmpll0_1_3_gpll0_map[] = {
	{ P_XO, 0 },
	{ P_MMPLL0, 1 },
	{ P_MMPLL1, 2 },
	{ P_GPLL0, 5 },
	{ P_MMPLL3, 3 }
};

static const char * const mmcc_xo_mmpll0_1_3_gpll0[] = {
	"xo",
	"mmpll0_vote",
	"mmpll1_vote",
	"mmss_gpll0_vote",
	"mmpll3",
};

static const struct parent_map mmcc_xo_dsi_hdmi_edp_map[] = {
	{ P_XO, 0 },
	{ P_EDPLINK, 4 },
	{ P_HDMIPLL, 3 },
	{ P_EDPVCO, 5 },
	{ P_DSI0PLL, 1 },
	{ P_DSI1PLL, 2 }
};

static const char * const mmcc_xo_dsi_hdmi_edp[] = {
	"xo",
	"edp_link_clk",
	"hdmipll",
	"edp_vco_div",
	"dsi0pll",
	"dsi1pll",
};

static const struct parent_map mmcc_xo_dsi_hdmi_edp_gpll0_map[] = {
	{ P_XO, 0 },
	{ P_EDPLINK, 4 },
	{ P_HDMIPLL, 3 },
	{ P_GPLL0, 5 },
	{ P_DSI0PLL, 1 },
	{ P_DSI1PLL, 2 }
};

static const char * const mmcc_xo_dsi_hdmi_edp_gpll0[] = {
	"xo",
	"edp_link_clk",
	"hdmipll",
	"gpll0_vote",
	"dsi0pll",
	"dsi1pll",
};

static const struct parent_map mmcc_xo_dsibyte_hdmi_edp_gpll0_map[] = {
	{ P_XO, 0 },
	{ P_EDPLINK, 4 },
	{ P_HDMIPLL, 3 },
	{ P_GPLL0, 5 },
	{ P_DSI0PLL_BYTE, 1 },
	{ P_DSI1PLL_BYTE, 2 }
};

static const char * const mmcc_xo_dsibyte_hdmi_edp_gpll0[] = {
	"xo",
	"edp_link_clk",
	"hdmipll",
	"gpll0_vote",
	"dsi0pllbyte",
	"dsi1pllbyte",
};

static const struct parent_map mmcc_xo_mmpll0_1_4_gpll0_map[] = {
	{ P_XO, 0 },
	{ P_MMPLL0, 1 },
	{ P_MMPLL1, 2 },
	{ P_GPLL0, 5 },
	{ P_MMPLL4, 3 }
};

static const char * const mmcc_xo_mmpll0_1_4_gpll0[] = {
	"xo",
	"mmpll0",
	"mmpll1",
	"mmpll4",
	"gpll0",
};

static const struct parent_map mmcc_xo_mmpll0_1_4_gpll1_0_map[] = {
	{ P_XO, 0 },
	{ P_MMPLL0, 1 },
	{ P_MMPLL1, 2 },
	{ P_MMPLL4, 3 },
	{ P_GPLL0, 5 },
	{ P_GPLL1, 4 }
};

static const char * const mmcc_xo_mmpll0_1_4_gpll1_0[] = {
	"xo",
	"mmpll0",
	"mmpll1",
	"mmpll4",
	"gpll1",
	"gpll0",
};

static const struct parent_map mmcc_xo_mmpll0_1_4_gpll1_0_sleep_map[] = {
	{ P_XO, 0 },
	{ P_MMPLL0, 1 },
	{ P_MMPLL1, 2 },
	{ P_MMPLL4, 3 },
	{ P_GPLL0, 5 },
	{ P_GPLL1, 4 },
	{ P_MMSLEEP, 6 }
};

static const char * const mmcc_xo_mmpll0_1_4_gpll1_0_sleep[] = {
	"xo",
	"mmpll0",
	"mmpll1",
	"mmpll4",
	"gpll1",
	"gpll0",
	"sleep_clk_src",
};

static struct clk_pll mmpll0 = {
	.l_reg = 0x0004,
	.m_reg = 0x0008,
	.n_reg = 0x000c,
	.config_reg = 0x0014,
	.mode_reg = 0x0000,
	.status_reg = 0x001c,
	.status_bit = 17,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll0",
		.parent_names = (const char *[]){ "xo" },
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

static struct clk_regmap mmpll0_vote = {
	.enable_reg = 0x0100,
	.enable_mask = BIT(0),
	.hw.init = &(struct clk_init_data){
		.name = "mmpll0_vote",
		.parent_names = (const char *[]){ "mmpll0" },
		.num_parents = 1,
		.ops = &clk_pll_vote_ops,
	},
};

static struct clk_pll mmpll1 = {
	.l_reg = 0x0044,
	.m_reg = 0x0048,
	.n_reg = 0x004c,
	.config_reg = 0x0050,
	.mode_reg = 0x0040,
	.status_reg = 0x005c,
	.status_bit = 17,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll1",
		.parent_names = (const char *[]){ "xo" },
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

static struct clk_regmap mmpll1_vote = {
	.enable_reg = 0x0100,
	.enable_mask = BIT(1),
	.hw.init = &(struct clk_init_data){
		.name = "mmpll1_vote",
		.parent_names = (const char *[]){ "mmpll1" },
		.num_parents = 1,
		.ops = &clk_pll_vote_ops,
	},
};

static struct clk_pll mmpll2 = {
	.l_reg = 0x4104,
	.m_reg = 0x4108,
	.n_reg = 0x410c,
	.config_reg = 0x4110,
	.mode_reg = 0x4100,
	.status_reg = 0x411c,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll2",
		.parent_names = (const char *[]){ "xo" },
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

static struct clk_pll mmpll3 = {
	.l_reg = 0x0084,
	.m_reg = 0x0088,
	.n_reg = 0x008c,
	.config_reg = 0x0090,
	.mode_reg = 0x0080,
	.status_reg = 0x009c,
	.status_bit = 17,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll3",
		.parent_names = (const char *[]){ "xo" },
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

static struct clk_pll mmpll4 = {
	.l_reg = 0x00a4,
	.m_reg = 0x00a8,
	.n_reg = 0x00ac,
	.config_reg = 0x00b0,
	.mode_reg = 0x0080,
	.status_reg = 0x00bc,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll4",
		.parent_names = (const char *[]){ "xo" },
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

static struct clk_rcg2 mmss_ahb_clk_src = {
	.cmd_rcgr = 0x5000,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_mmpll1_gpll0_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmss_ahb_clk_src",
		.parent_names = mmcc_xo_mmpll0_mmpll1_gpll0,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static struct freq_tbl ftbl_mmss_axi_clk[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(37500000, P_GPLL0, 16, 0, 0),
	F(50000000, P_GPLL0, 12, 0, 0),
	F(75000000, P_GPLL0, 8, 0, 0),
	F(100000000, P_GPLL0, 6, 0, 0),
	F(150000000, P_GPLL0, 4, 0, 0),
	F(333430000, P_MMPLL1, 3.5, 0, 0),
	F(400000000, P_MMPLL0, 2, 0, 0),
	F(466800000, P_MMPLL1, 2.5, 0, 0),
};

static struct clk_rcg2 mmss_axi_clk_src = {
	.cmd_rcgr = 0x5040,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_mmpll1_gpll0_map,
	.freq_tbl = ftbl_mmss_axi_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmss_axi_clk_src",
		.parent_names = mmcc_xo_mmpll0_mmpll1_gpll0,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static struct freq_tbl ftbl_ocmemnoc_clk[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(37500000, P_GPLL0, 16, 0, 0),
	F(50000000, P_GPLL0, 12, 0, 0),
	F(75000000, P_GPLL0, 8, 0, 0),
	F(109090000, P_GPLL0, 5.5, 0, 0),
	F(150000000, P_GPLL0, 4, 0, 0),
	F(228570000, P_MMPLL0, 3.5, 0, 0),
	F(320000000, P_MMPLL0, 2.5, 0, 0),
};

static struct clk_rcg2 ocmemnoc_clk_src = {
	.cmd_rcgr = 0x5090,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_mmpll1_gpll0_map,
	.freq_tbl = ftbl_ocmemnoc_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "ocmemnoc_clk_src",
		.parent_names = mmcc_xo_mmpll0_mmpll1_gpll0,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static struct freq_tbl ftbl_camss_csi0_3_clk[] = {
	F(100000000, P_GPLL0, 6, 0, 0),
	F(200000000, P_MMPLL0, 4, 0, 0),
	{ }
};

static struct clk_rcg2 csi0_clk_src = {
	.cmd_rcgr = 0x3090,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_1_4_gpll0_map,
	.freq_tbl = ftbl_camss_csi0_3_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csi0_clk_src",
		.parent_names = mmcc_xo_mmpll0_1_4_gpll0,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 csi1_clk_src = {
	.cmd_rcgr = 0x3100,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_1_4_gpll0_map,
	.freq_tbl = ftbl_camss_csi0_3_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csi1_clk_src",
		.parent_names = mmcc_xo_mmpll0_1_4_gpll0,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 csi2_clk_src = {
	.cmd_rcgr = 0x3160,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_1_4_gpll0_map,
	.freq_tbl = ftbl_camss_csi0_3_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csi2_clk_src",
		.parent_names = mmcc_xo_mmpll0_1_4_gpll0,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 csi3_clk_src = {
	.cmd_rcgr = 0x31c0,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_1_4_gpll0_map,
	.freq_tbl = ftbl_camss_csi0_3_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csi3_clk_src",
		.parent_names = mmcc_xo_mmpll0_1_4_gpll0,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static struct freq_tbl ftbl_camss_vfe_vfe0_1_clk[] = {
	F(37500000, P_GPLL0, 16, 0, 0),
	F(50000000, P_GPLL0, 12, 0, 0),
	F(60000000, P_GPLL0, 10, 0, 0),
	F(80000000, P_GPLL0, 7.5, 0, 0),
	F(100000000, P_GPLL0, 6, 0, 0),
	F(109090000, P_GPLL0, 5.5, 0, 0),
	F(133330000, P_GPLL0, 4.5, 0, 0),
	F(200000000, P_GPLL0, 3, 0, 0),
	F(228570000, P_MMPLL0, 3.5, 0, 0),
	F(266670000, P_MMPLL0, 3, 0, 0),
	F(320000000, P_MMPLL0, 2.5, 0, 0),
	F(465000000, P_MMPLL4, 2, 0, 0),
	F(600000000, P_GPLL0, 1, 0, 0),
	{ }
};

static struct clk_rcg2 vfe0_clk_src = {
	.cmd_rcgr = 0x3600,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_1_4_gpll0_map,
	.freq_tbl = ftbl_camss_vfe_vfe0_1_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "vfe0_clk_src",
		.parent_names = mmcc_xo_mmpll0_1_4_gpll0,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 vfe1_clk_src = {
	.cmd_rcgr = 0x3620,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_1_4_gpll0_map,
	.freq_tbl = ftbl_camss_vfe_vfe0_1_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "vfe1_clk_src",
		.parent_names = mmcc_xo_mmpll0_1_4_gpll0,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static struct freq_tbl ftbl_mdss_mdp_clk[] = {
	F(37500000, P_GPLL0, 16, 0, 0),
	F(60000000, P_GPLL0, 10, 0, 0),
	F(75000000, P_GPLL0, 8, 0, 0),
	F(85710000, P_GPLL0, 7, 0, 0),
	F(100000000, P_GPLL0, 6, 0, 0),
	F(150000000, P_GPLL0, 4, 0, 0),
	F(160000000, P_MMPLL0, 5, 0, 0),
	F(200000000, P_MMPLL0, 4, 0, 0),
	F(228570000, P_MMPLL0, 3.5, 0, 0),
	F(300000000, P_GPLL0, 2, 0, 0),
	F(320000000, P_MMPLL0, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 mdp_clk_src = {
	.cmd_rcgr = 0x2040,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_dsi_hdmi_gpll0_map,
	.freq_tbl = ftbl_mdss_mdp_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mdp_clk_src",
		.parent_names = mmcc_xo_mmpll0_dsi_hdmi_gpll0,
		.num_parents = 6,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gfx3d_clk_src = {
	.cmd_rcgr = 0x4000,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_1_2_gpll0_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gfx3d_clk_src",
		.parent_names = mmcc_xo_mmpll0_1_2_gpll0,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static struct freq_tbl ftbl_camss_jpeg_jpeg0_2_clk[] = {
	F(75000000, P_GPLL0, 8, 0, 0),
	F(133330000, P_GPLL0, 4.5, 0, 0),
	F(200000000, P_GPLL0, 3, 0, 0),
	F(228570000, P_MMPLL0, 3.5, 0, 0),
	F(266670000, P_MMPLL0, 3, 0, 0),
	F(320000000, P_MMPLL0, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 jpeg0_clk_src = {
	.cmd_rcgr = 0x3500,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_1_4_gpll0_map,
	.freq_tbl = ftbl_camss_jpeg_jpeg0_2_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "jpeg0_clk_src",
		.parent_names = mmcc_xo_mmpll0_1_4_gpll0,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 jpeg1_clk_src = {
	.cmd_rcgr = 0x3520,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_1_4_gpll0_map,
	.freq_tbl = ftbl_camss_jpeg_jpeg0_2_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "jpeg1_clk_src",
		.parent_names = mmcc_xo_mmpll0_1_4_gpll0,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 jpeg2_clk_src = {
	.cmd_rcgr = 0x3540,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_1_4_gpll0_map,
	.freq_tbl = ftbl_camss_jpeg_jpeg0_2_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "jpeg2_clk_src",
		.parent_names = mmcc_xo_mmpll0_1_4_gpll0,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 pclk0_clk_src = {
	.cmd_rcgr = 0x2000,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmcc_xo_dsi_hdmi_edp_gpll0_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pclk0_clk_src",
		.parent_names = mmcc_xo_dsi_hdmi_edp_gpll0,
		.num_parents = 6,
		.ops = &clk_pixel_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_rcg2 pclk1_clk_src = {
	.cmd_rcgr = 0x2020,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmcc_xo_dsi_hdmi_edp_gpll0_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pclk1_clk_src",
		.parent_names = mmcc_xo_dsi_hdmi_edp_gpll0,
		.num_parents = 6,
		.ops = &clk_pixel_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct freq_tbl ftbl_venus0_vcodec0_clk[] = {
	F(50000000, P_GPLL0, 12, 0, 0),
	F(100000000, P_GPLL0, 6, 0, 0),
	F(133330000, P_GPLL0, 4.5, 0, 0),
	F(200000000, P_MMPLL0, 4, 0, 0),
	F(266670000, P_MMPLL0, 3, 0, 0),
	F(465000000, P_MMPLL3, 2, 0, 0),
	{ }
};

static struct clk_rcg2 vcodec0_clk_src = {
	.cmd_rcgr = 0x1000,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_1_3_gpll0_map,
	.freq_tbl = ftbl_venus0_vcodec0_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "vcodec0_clk_src",
		.parent_names = mmcc_xo_mmpll0_1_3_gpll0,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static struct freq_tbl ftbl_avsync_vp_clk[] = {
	F(150000000, P_GPLL0, 4, 0, 0),
	F(320000000, P_MMPLL0, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 vp_clk_src = {
	.cmd_rcgr = 0x2430,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_mmpll1_gpll0_map,
	.freq_tbl = ftbl_avsync_vp_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "vp_clk_src",
		.parent_names = mmcc_xo_mmpll0_mmpll1_gpll0,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static struct freq_tbl ftbl_camss_cci_cci_clk[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cci_clk_src = {
	.cmd_rcgr = 0x3300,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_1_4_gpll1_0_map,
	.freq_tbl = ftbl_camss_cci_cci_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cci_clk_src",
		.parent_names = mmcc_xo_mmpll0_1_4_gpll1_0,
		.num_parents = 6,
		.ops = &clk_rcg2_ops,
	},
};

static struct freq_tbl ftbl_camss_gp0_1_clk[] = {
	F(10000, P_XO, 16, 1, 120),
	F(24000, P_XO, 16, 1, 50),
	F(6000000, P_GPLL0, 10, 1, 10),
	F(12000000, P_GPLL0, 10, 1, 5),
	F(13000000, P_GPLL0, 4, 13, 150),
	F(24000000, P_GPLL0, 5, 1, 5),
	{ }
};

static struct clk_rcg2 camss_gp0_clk_src = {
	.cmd_rcgr = 0x3420,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_1_4_gpll1_0_sleep_map,
	.freq_tbl = ftbl_camss_gp0_1_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "camss_gp0_clk_src",
		.parent_names = mmcc_xo_mmpll0_1_4_gpll1_0_sleep,
		.num_parents = 7,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 camss_gp1_clk_src = {
	.cmd_rcgr = 0x3450,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_1_4_gpll1_0_sleep_map,
	.freq_tbl = ftbl_camss_gp0_1_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "camss_gp1_clk_src",
		.parent_names = mmcc_xo_mmpll0_1_4_gpll1_0_sleep,
		.num_parents = 7,
		.ops = &clk_rcg2_ops,
	},
};

static struct freq_tbl ftbl_camss_mclk0_3_clk[] = {
	F(4800000, P_XO, 4, 0, 0),
	F(6000000, P_GPLL0, 10, 1, 10),
	F(8000000, P_GPLL0, 15, 1, 5),
	F(9600000, P_XO, 2, 0, 0),
	F(16000000, P_MMPLL0, 10, 1, 5),
	F(19200000, P_XO, 1, 0, 0),
	F(24000000, P_GPLL0, 5, 1, 5),
	F(32000000, P_MMPLL0, 5, 1, 5),
	F(48000000, P_GPLL0, 12.5, 0, 0),
	F(64000000, P_MMPLL0, 12.5, 0, 0),
	{ }
};

static struct clk_rcg2 mclk0_clk_src = {
	.cmd_rcgr = 0x3360,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_1_4_gpll1_0_map,
	.freq_tbl = ftbl_camss_mclk0_3_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mclk0_clk_src",
		.parent_names = mmcc_xo_mmpll0_1_4_gpll1_0,
		.num_parents = 6,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 mclk1_clk_src = {
	.cmd_rcgr = 0x3390,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_1_4_gpll1_0_map,
	.freq_tbl = ftbl_camss_mclk0_3_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mclk1_clk_src",
		.parent_names = mmcc_xo_mmpll0_1_4_gpll1_0,
		.num_parents = 6,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 mclk2_clk_src = {
	.cmd_rcgr = 0x33c0,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_1_4_gpll1_0_map,
	.freq_tbl = ftbl_camss_mclk0_3_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mclk2_clk_src",
		.parent_names = mmcc_xo_mmpll0_1_4_gpll1_0,
		.num_parents = 6,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 mclk3_clk_src = {
	.cmd_rcgr = 0x33f0,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_1_4_gpll1_0_map,
	.freq_tbl = ftbl_camss_mclk0_3_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mclk3_clk_src",
		.parent_names = mmcc_xo_mmpll0_1_4_gpll1_0,
		.num_parents = 6,
		.ops = &clk_rcg2_ops,
	},
};

static struct freq_tbl ftbl_camss_phy0_2_csi0_2phytimer_clk[] = {
	F(100000000, P_GPLL0, 6, 0, 0),
	F(200000000, P_MMPLL0, 4, 0, 0),
	{ }
};

static struct clk_rcg2 csi0phytimer_clk_src = {
	.cmd_rcgr = 0x3000,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_1_4_gpll0_map,
	.freq_tbl = ftbl_camss_phy0_2_csi0_2phytimer_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csi0phytimer_clk_src",
		.parent_names = mmcc_xo_mmpll0_1_4_gpll0,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 csi1phytimer_clk_src = {
	.cmd_rcgr = 0x3030,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_1_4_gpll0_map,
	.freq_tbl = ftbl_camss_phy0_2_csi0_2phytimer_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csi1phytimer_clk_src",
		.parent_names = mmcc_xo_mmpll0_mmpll1_gpll0,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 csi2phytimer_clk_src = {
	.cmd_rcgr = 0x3060,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_1_4_gpll0_map,
	.freq_tbl = ftbl_camss_phy0_2_csi0_2phytimer_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csi2phytimer_clk_src",
		.parent_names = mmcc_xo_mmpll0_1_4_gpll0,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static struct freq_tbl ftbl_camss_vfe_cpp_clk[] = {
	F(133330000, P_GPLL0, 4.5, 0, 0),
	F(266670000, P_MMPLL0, 3, 0, 0),
	F(320000000, P_MMPLL0, 2.5, 0, 0),
	F(372000000, P_MMPLL4, 2.5, 0, 0),
	F(465000000, P_MMPLL4, 2, 0, 0),
	F(600000000, P_GPLL0, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cpp_clk_src = {
	.cmd_rcgr = 0x3640,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_1_4_gpll0_map,
	.freq_tbl = ftbl_camss_vfe_cpp_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cpp_clk_src",
		.parent_names = mmcc_xo_mmpll0_1_4_gpll0,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 byte0_clk_src = {
	.cmd_rcgr = 0x2120,
	.hid_width = 5,
	.parent_map = mmcc_xo_dsibyte_hdmi_edp_gpll0_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "byte0_clk_src",
		.parent_names = mmcc_xo_dsibyte_hdmi_edp_gpll0,
		.num_parents = 6,
		.ops = &clk_byte2_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_rcg2 byte1_clk_src = {
	.cmd_rcgr = 0x2140,
	.hid_width = 5,
	.parent_map = mmcc_xo_dsibyte_hdmi_edp_gpll0_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "byte1_clk_src",
		.parent_names = mmcc_xo_dsibyte_hdmi_edp_gpll0,
		.num_parents = 6,
		.ops = &clk_byte2_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct freq_tbl ftbl_mdss_edpaux_clk[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 edpaux_clk_src = {
	.cmd_rcgr = 0x20e0,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_mmpll1_gpll0_map,
	.freq_tbl = ftbl_mdss_edpaux_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "edpaux_clk_src",
		.parent_names = mmcc_xo_mmpll0_mmpll1_gpll0,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static struct freq_tbl ftbl_mdss_edplink_clk[] = {
	F(135000000, P_EDPLINK, 2, 0, 0),
	F(270000000, P_EDPLINK, 11, 0, 0),
	{ }
};

static struct clk_rcg2 edplink_clk_src = {
	.cmd_rcgr = 0x20c0,
	.hid_width = 5,
	.parent_map = mmcc_xo_dsi_hdmi_edp_gpll0_map,
	.freq_tbl = ftbl_mdss_edplink_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "edplink_clk_src",
		.parent_names = mmcc_xo_dsi_hdmi_edp_gpll0,
		.num_parents = 6,
		.ops = &clk_rcg2_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct freq_tbl edp_pixel_freq_tbl[] = {
	{ .src = P_EDPVCO },
	{ }
};

static struct clk_rcg2 edppixel_clk_src = {
	.cmd_rcgr = 0x20a0,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmcc_xo_dsi_hdmi_edp_map,
	.freq_tbl = edp_pixel_freq_tbl,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "edppixel_clk_src",
		.parent_names = mmcc_xo_dsi_hdmi_edp,
		.num_parents = 6,
		.ops = &clk_edp_pixel_ops,
	},
};

static struct freq_tbl ftbl_mdss_esc0_1_clk[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 esc0_clk_src = {
	.cmd_rcgr = 0x2160,
	.hid_width = 5,
	.parent_map = mmcc_xo_dsibyte_hdmi_edp_gpll0_map,
	.freq_tbl = ftbl_mdss_esc0_1_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "esc0_clk_src",
		.parent_names = mmcc_xo_dsibyte_hdmi_edp_gpll0,
		.num_parents = 6,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 esc1_clk_src = {
	.cmd_rcgr = 0x2180,
	.hid_width = 5,
	.parent_map = mmcc_xo_dsibyte_hdmi_edp_gpll0_map,
	.freq_tbl = ftbl_mdss_esc0_1_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "esc1_clk_src",
		.parent_names = mmcc_xo_dsibyte_hdmi_edp_gpll0,
		.num_parents = 6,
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
	.parent_map = mmcc_xo_dsi_hdmi_edp_gpll0_map,
	.freq_tbl = extpclk_freq_tbl,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "extpclk_clk_src",
		.parent_names = mmcc_xo_dsi_hdmi_edp_gpll0,
		.num_parents = 6,
		.ops = &clk_byte_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct freq_tbl ftbl_mdss_hdmi_clk[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 hdmi_clk_src = {
	.cmd_rcgr = 0x2100,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_mmpll1_gpll0_map,
	.freq_tbl = ftbl_mdss_hdmi_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "hdmi_clk_src",
		.parent_names = mmcc_xo_mmpll0_mmpll1_gpll0,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static struct freq_tbl ftbl_mdss_vsync_clk[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 vsync_clk_src = {
	.cmd_rcgr = 0x2080,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_mmpll1_gpll0_map,
	.freq_tbl = ftbl_mdss_vsync_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "vsync_clk_src",
		.parent_names = mmcc_xo_mmpll0_mmpll1_gpll0,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static struct freq_tbl ftbl_mmss_rbcpr_clk[] = {
	F(50000000, P_GPLL0, 12, 0, 0),
	{ }
};

static struct clk_rcg2 rbcpr_clk_src = {
	.cmd_rcgr = 0x4060,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_mmpll1_gpll0_map,
	.freq_tbl = ftbl_mmss_rbcpr_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "rbcpr_clk_src",
		.parent_names = mmcc_xo_mmpll0_mmpll1_gpll0,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static struct freq_tbl ftbl_oxili_rbbmtimer_clk[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 rbbmtimer_clk_src = {
	.cmd_rcgr = 0x4090,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_mmpll1_gpll0_map,
	.freq_tbl = ftbl_oxili_rbbmtimer_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "rbbmtimer_clk_src",
		.parent_names = mmcc_xo_mmpll0_mmpll1_gpll0,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static struct freq_tbl ftbl_vpu_maple_clk[] = {
	F(50000000, P_GPLL0, 12, 0, 0),
	F(100000000, P_GPLL0, 6, 0, 0),
	F(133330000, P_GPLL0, 4.5, 0, 0),
	F(200000000, P_MMPLL0, 4, 0, 0),
	F(266670000, P_MMPLL0, 3, 0, 0),
	F(465000000, P_MMPLL3, 2, 0, 0),
	{ }
};

static struct clk_rcg2 maple_clk_src = {
	.cmd_rcgr = 0x1320,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_mmpll1_gpll0_map,
	.freq_tbl = ftbl_vpu_maple_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "maple_clk_src",
		.parent_names = mmcc_xo_mmpll0_mmpll1_gpll0,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static struct freq_tbl ftbl_vpu_vdp_clk[] = {
	F(50000000, P_GPLL0, 12, 0, 0),
	F(100000000, P_GPLL0, 6, 0, 0),
	F(200000000, P_MMPLL0, 4, 0, 0),
	F(320000000, P_MMPLL0, 2.5, 0, 0),
	F(400000000, P_MMPLL0, 2, 0, 0),
	{ }
};

static struct clk_rcg2 vdp_clk_src = {
	.cmd_rcgr = 0x1300,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_mmpll1_gpll0_map,
	.freq_tbl = ftbl_vpu_vdp_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "vdp_clk_src",
		.parent_names = mmcc_xo_mmpll0_mmpll1_gpll0,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static struct freq_tbl ftbl_vpu_bus_clk[] = {
	F(40000000, P_GPLL0, 15, 0, 0),
	F(80000000, P_MMPLL0, 10, 0, 0),
	{ }
};

static struct clk_rcg2 vpu_bus_clk_src = {
	.cmd_rcgr = 0x1340,
	.hid_width = 5,
	.parent_map = mmcc_xo_mmpll0_mmpll1_gpll0_map,
	.freq_tbl = ftbl_vpu_bus_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "vpu_bus_clk_src",
		.parent_names = mmcc_xo_mmpll0_mmpll1_gpll0,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch mmss_cxo_clk = {
	.halt_reg = 0x5104,
	.clkr = {
		.enable_reg = 0x5104,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_cxo_clk",
			.parent_names = (const char *[]){ "xo" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_sleepclk_clk = {
	.halt_reg = 0x5100,
	.clkr = {
		.enable_reg = 0x5100,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_sleepclk_clk",
			.parent_names = (const char *[]){
				"sleep_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch avsync_ahb_clk = {
	.halt_reg = 0x2414,
	.clkr = {
		.enable_reg = 0x2414,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "avsync_ahb_clk",
			.parent_names = (const char *[]){
				"mmss_ahb_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch avsync_edppixel_clk = {
	.halt_reg = 0x2418,
	.clkr = {
		.enable_reg = 0x2418,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "avsync_edppixel_clk",
			.parent_names = (const char *[]){
				"edppixel_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch avsync_extpclk_clk = {
	.halt_reg = 0x2410,
	.clkr = {
		.enable_reg = 0x2410,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "avsync_extpclk_clk",
			.parent_names = (const char *[]){
				"extpclk_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch avsync_pclk0_clk = {
	.halt_reg = 0x241c,
	.clkr = {
		.enable_reg = 0x241c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "avsync_pclk0_clk",
			.parent_names = (const char *[]){
				"pclk0_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch avsync_pclk1_clk = {
	.halt_reg = 0x2420,
	.clkr = {
		.enable_reg = 0x2420,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "avsync_pclk1_clk",
			.parent_names = (const char *[]){
				"pclk1_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch avsync_vp_clk = {
	.halt_reg = 0x2404,
	.clkr = {
		.enable_reg = 0x2404,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "avsync_vp_clk",
			.parent_names = (const char *[]){
				"vp_clk_src",
			},
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
			.parent_names = (const char *[]){
				"mmss_ahb_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_cci_cci_ahb_clk = {
	.halt_reg = 0x3348,
	.clkr = {
		.enable_reg = 0x3348,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_cci_cci_ahb_clk",
			.parent_names = (const char *[]){
				"mmss_ahb_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_cci_cci_clk = {
	.halt_reg = 0x3344,
	.clkr = {
		.enable_reg = 0x3344,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_cci_cci_clk",
			.parent_names = (const char *[]){
				"cci_clk_src",
			},
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
			.parent_names = (const char *[]){
				"mmss_ahb_clk_src",
			},
			.num_parents = 1,
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
			.parent_names = (const char *[]){
				"csi0_clk_src",
			},
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
			.parent_names = (const char *[]){
				"csi0_clk_src",
			},
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
			.parent_names = (const char *[]){
				"csi0_clk_src",
			},
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
			.parent_names = (const char *[]){
				"csi0_clk_src",
			},
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
			.parent_names = (const char *[]){
				"mmss_ahb_clk_src",
			},
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
			.parent_names = (const char *[]){
				"csi1_clk_src",
			},
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
			.parent_names = (const char *[]){
				"csi1_clk_src",
			},
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
			.parent_names = (const char *[]){
				"csi1_clk_src",
			},
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
			.parent_names = (const char *[]){
				"csi1_clk_src",
			},
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
			.parent_names = (const char *[]){
				"mmss_ahb_clk_src",
			},
			.num_parents = 1,
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
			.parent_names = (const char *[]){
				"csi2_clk_src",
			},
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
			.parent_names = (const char *[]){
				"csi2_clk_src",
			},
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
			.parent_names = (const char *[]){
				"csi2_clk_src",
			},
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
			.parent_names = (const char *[]){
				"csi2_clk_src",
			},
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
			.parent_names = (const char *[]){
				"mmss_ahb_clk_src",
			},
			.num_parents = 1,
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
			.parent_names = (const char *[]){
				"csi3_clk_src",
			},
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
			.parent_names = (const char *[]){
				"csi3_clk_src",
			},
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
			.parent_names = (const char *[]){
				"csi3_clk_src",
			},
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
			.parent_names = (const char *[]){
				"csi3_clk_src",
			},
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
			.parent_names = (const char *[]){
				"vfe0_clk_src",
			},
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
			.parent_names = (const char *[]){
				"vfe1_clk_src",
			},
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
			.parent_names = (const char *[]){
				"camss_gp0_clk_src",
			},
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
			.parent_names = (const char *[]){
				"camss_gp1_clk_src",
			},
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
			.parent_names = (const char *[]){
				"mmss_ahb_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_jpeg_jpeg0_clk = {
	.halt_reg = 0x35a8,
	.clkr = {
		.enable_reg = 0x35a8,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_jpeg_jpeg0_clk",
			.parent_names = (const char *[]){
				"jpeg0_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_jpeg_jpeg1_clk = {
	.halt_reg = 0x35ac,
	.clkr = {
		.enable_reg = 0x35ac,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_jpeg_jpeg1_clk",
			.parent_names = (const char *[]){
				"jpeg1_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_jpeg_jpeg2_clk = {
	.halt_reg = 0x35b0,
	.clkr = {
		.enable_reg = 0x35b0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_jpeg_jpeg2_clk",
			.parent_names = (const char *[]){
				"jpeg2_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_jpeg_jpeg_ahb_clk = {
	.halt_reg = 0x35b4,
	.clkr = {
		.enable_reg = 0x35b4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_jpeg_jpeg_ahb_clk",
			.parent_names = (const char *[]){
				"mmss_ahb_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_jpeg_jpeg_axi_clk = {
	.halt_reg = 0x35b8,
	.clkr = {
		.enable_reg = 0x35b8,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_jpeg_jpeg_axi_clk",
			.parent_names = (const char *[]){
				"mmss_axi_clk_src",
			},
			.num_parents = 1,
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
			.parent_names = (const char *[]){
				"mclk0_clk_src",
			},
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
			.parent_names = (const char *[]){
				"mclk1_clk_src",
			},
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
			.parent_names = (const char *[]){
				"mclk2_clk_src",
			},
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
			.parent_names = (const char *[]){
				"mclk3_clk_src",
			},
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
			.parent_names = (const char *[]){
				"mmss_ahb_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_phy0_csi0phytimer_clk = {
	.halt_reg = 0x3024,
	.clkr = {
		.enable_reg = 0x3024,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_phy0_csi0phytimer_clk",
			.parent_names = (const char *[]){
				"csi0phytimer_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_phy1_csi1phytimer_clk = {
	.halt_reg = 0x3054,
	.clkr = {
		.enable_reg = 0x3054,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_phy1_csi1phytimer_clk",
			.parent_names = (const char *[]){
				"csi1phytimer_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_phy2_csi2phytimer_clk = {
	.halt_reg = 0x3084,
	.clkr = {
		.enable_reg = 0x3084,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_phy2_csi2phytimer_clk",
			.parent_names = (const char *[]){
				"csi2phytimer_clk_src",
			},
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
			.parent_names = (const char *[]){
				"mmss_ahb_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_vfe_cpp_ahb_clk = {
	.halt_reg = 0x36b4,
	.clkr = {
		.enable_reg = 0x36b4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_vfe_cpp_ahb_clk",
			.parent_names = (const char *[]){
				"mmss_ahb_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_vfe_cpp_clk = {
	.halt_reg = 0x36b0,
	.clkr = {
		.enable_reg = 0x36b0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_vfe_cpp_clk",
			.parent_names = (const char *[]){
				"cpp_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_vfe_vfe0_clk = {
	.halt_reg = 0x36a8,
	.clkr = {
		.enable_reg = 0x36a8,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_vfe_vfe0_clk",
			.parent_names = (const char *[]){
				"vfe0_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_vfe_vfe1_clk = {
	.halt_reg = 0x36ac,
	.clkr = {
		.enable_reg = 0x36ac,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_vfe_vfe1_clk",
			.parent_names = (const char *[]){
				"vfe1_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_vfe_vfe_ahb_clk = {
	.halt_reg = 0x36b8,
	.clkr = {
		.enable_reg = 0x36b8,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_vfe_vfe_ahb_clk",
			.parent_names = (const char *[]){
				"mmss_ahb_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_vfe_vfe_axi_clk = {
	.halt_reg = 0x36bc,
	.clkr = {
		.enable_reg = 0x36bc,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_vfe_vfe_axi_clk",
			.parent_names = (const char *[]){
				"mmss_axi_clk_src",
			},
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
			.parent_names = (const char *[]){
				"mmss_ahb_clk_src",
			},
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
			.parent_names = (const char *[]){
				"mmss_axi_clk_src",
			},
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
			.parent_names = (const char *[]){
				"byte0_clk_src",
			},
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
			.parent_names = (const char *[]){
				"byte1_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_edpaux_clk = {
	.halt_reg = 0x2334,
	.clkr = {
		.enable_reg = 0x2334,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_edpaux_clk",
			.parent_names = (const char *[]){
				"edpaux_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_edplink_clk = {
	.halt_reg = 0x2330,
	.clkr = {
		.enable_reg = 0x2330,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_edplink_clk",
			.parent_names = (const char *[]){
				"edplink_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_edppixel_clk = {
	.halt_reg = 0x232c,
	.clkr = {
		.enable_reg = 0x232c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_edppixel_clk",
			.parent_names = (const char *[]){
				"edppixel_clk_src",
			},
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
			.parent_names = (const char *[]){
				"esc0_clk_src",
			},
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
			.parent_names = (const char *[]){
				"esc1_clk_src",
			},
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
			.parent_names = (const char *[]){
				"extpclk_clk_src",
			},
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
			.parent_names = (const char *[]){
				"mmss_ahb_clk_src",
			},
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
			.parent_names = (const char *[]){
				"hdmi_clk_src",
			},
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
			.parent_names = (const char *[]){
				"mdp_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_mdp_lut_clk = {
	.halt_reg = 0x2320,
	.clkr = {
		.enable_reg = 0x2320,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_mdp_lut_clk",
			.parent_names = (const char *[]){
				"mdp_clk_src",
			},
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
			.parent_names = (const char *[]){
				"pclk0_clk_src",
			},
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
			.parent_names = (const char *[]){
				"pclk1_clk_src",
			},
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
			.parent_names = (const char *[]){
				"vsync_clk_src",
			},
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
			.parent_names = (const char *[]){
				"mmss_ahb_clk_src",
			},
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
			.parent_names = (const char *[]){
				"rbcpr_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_spdm_ahb_clk = {
	.halt_reg = 0x0230,
	.clkr = {
		.enable_reg = 0x0230,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_spdm_ahb_clk",
			.parent_names = (const char *[]){
				"mmss_spdm_ahb_div_clk",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_spdm_axi_clk = {
	.halt_reg = 0x0210,
	.clkr = {
		.enable_reg = 0x0210,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_spdm_axi_clk",
			.parent_names = (const char *[]){
				"mmss_spdm_axi_div_clk",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_spdm_csi0_clk = {
	.halt_reg = 0x023c,
	.clkr = {
		.enable_reg = 0x023c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_spdm_csi0_clk",
			.parent_names = (const char *[]){
				"mmss_spdm_csi0_div_clk",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_spdm_gfx3d_clk = {
	.halt_reg = 0x022c,
	.clkr = {
		.enable_reg = 0x022c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_spdm_gfx3d_clk",
			.parent_names = (const char *[]){
				"mmss_spdm_gfx3d_div_clk",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_spdm_jpeg0_clk = {
	.halt_reg = 0x0204,
	.clkr = {
		.enable_reg = 0x0204,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_spdm_jpeg0_clk",
			.parent_names = (const char *[]){
				"mmss_spdm_jpeg0_div_clk",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_spdm_jpeg1_clk = {
	.halt_reg = 0x0208,
	.clkr = {
		.enable_reg = 0x0208,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_spdm_jpeg1_clk",
			.parent_names = (const char *[]){
				"mmss_spdm_jpeg1_div_clk",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_spdm_jpeg2_clk = {
	.halt_reg = 0x0224,
	.clkr = {
		.enable_reg = 0x0224,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_spdm_jpeg2_clk",
			.parent_names = (const char *[]){
				"mmss_spdm_jpeg2_div_clk",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_spdm_mdp_clk = {
	.halt_reg = 0x020c,
	.clkr = {
		.enable_reg = 0x020c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_spdm_mdp_clk",
			.parent_names = (const char *[]){
				"mmss_spdm_mdp_div_clk",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_spdm_pclk0_clk = {
	.halt_reg = 0x0234,
	.clkr = {
		.enable_reg = 0x0234,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_spdm_pclk0_clk",
			.parent_names = (const char *[]){
				"mmss_spdm_pclk0_div_clk",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_spdm_pclk1_clk = {
	.halt_reg = 0x0228,
	.clkr = {
		.enable_reg = 0x0228,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_spdm_pclk1_clk",
			.parent_names = (const char *[]){
				"mmss_spdm_pclk1_div_clk",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_spdm_vcodec0_clk = {
	.halt_reg = 0x0214,
	.clkr = {
		.enable_reg = 0x0214,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_spdm_vcodec0_clk",
			.parent_names = (const char *[]){
				"mmss_spdm_vcodec0_div_clk",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_spdm_vfe0_clk = {
	.halt_reg = 0x0218,
	.clkr = {
		.enable_reg = 0x0218,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_spdm_vfe0_clk",
			.parent_names = (const char *[]){
				"mmss_spdm_vfe0_div_clk",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_spdm_vfe1_clk = {
	.halt_reg = 0x021c,
	.clkr = {
		.enable_reg = 0x021c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_spdm_vfe1_clk",
			.parent_names = (const char *[]){
				"mmss_spdm_vfe1_div_clk",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_spdm_rm_axi_clk = {
	.halt_reg = 0x0304,
	.clkr = {
		.enable_reg = 0x0304,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_spdm_rm_axi_clk",
			.parent_names = (const char *[]){
				"mmss_axi_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_spdm_rm_ocmemnoc_clk = {
	.halt_reg = 0x0308,
	.clkr = {
		.enable_reg = 0x0308,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_spdm_rm_ocmemnoc_clk",
			.parent_names = (const char *[]){
				"ocmemnoc_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};


static struct clk_branch mmss_misc_ahb_clk = {
	.halt_reg = 0x502c,
	.clkr = {
		.enable_reg = 0x502c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_misc_ahb_clk",
			.parent_names = (const char *[]){
				"mmss_ahb_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_mmssnoc_ahb_clk = {
	.halt_reg = 0x5024,
	.clkr = {
		.enable_reg = 0x5024,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_mmssnoc_ahb_clk",
			.parent_names = (const char *[]){
				"mmss_ahb_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
		},
	},
};

static struct clk_branch mmss_mmssnoc_bto_ahb_clk = {
	.halt_reg = 0x5028,
	.clkr = {
		.enable_reg = 0x5028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_mmssnoc_bto_ahb_clk",
			.parent_names = (const char *[]){
				"mmss_ahb_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
		},
	},
};

static struct clk_branch mmss_mmssnoc_axi_clk = {
	.halt_reg = 0x506c,
	.clkr = {
		.enable_reg = 0x506c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_mmssnoc_axi_clk",
			.parent_names = (const char *[]){
				"mmss_axi_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_s0_axi_clk = {
	.halt_reg = 0x5064,
	.clkr = {
		.enable_reg = 0x5064,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_s0_axi_clk",
			.parent_names = (const char *[]){
				"mmss_axi_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
		},
	},
};

static struct clk_branch ocmemcx_ahb_clk = {
	.halt_reg = 0x405c,
	.clkr = {
		.enable_reg = 0x405c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "ocmemcx_ahb_clk",
			.parent_names = (const char *[]){
				"mmss_ahb_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ocmemcx_ocmemnoc_clk = {
	.halt_reg = 0x4058,
	.clkr = {
		.enable_reg = 0x4058,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "ocmemcx_ocmemnoc_clk",
			.parent_names = (const char *[]){
				"ocmemnoc_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch oxili_ocmemgx_clk = {
	.halt_reg = 0x402c,
	.clkr = {
		.enable_reg = 0x402c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "oxili_ocmemgx_clk",
			.parent_names = (const char *[]){
				"gfx3d_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch oxili_gfx3d_clk = {
	.halt_reg = 0x4028,
	.clkr = {
		.enable_reg = 0x4028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "oxili_gfx3d_clk",
			.parent_names = (const char *[]){
				"gfx3d_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch oxili_rbbmtimer_clk = {
	.halt_reg = 0x40b0,
	.clkr = {
		.enable_reg = 0x40b0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "oxili_rbbmtimer_clk",
			.parent_names = (const char *[]){
				"rbbmtimer_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch oxilicx_ahb_clk = {
	.halt_reg = 0x403c,
	.clkr = {
		.enable_reg = 0x403c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "oxilicx_ahb_clk",
			.parent_names = (const char *[]){
				"mmss_ahb_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch venus0_ahb_clk = {
	.halt_reg = 0x1030,
	.clkr = {
		.enable_reg = 0x1030,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "venus0_ahb_clk",
			.parent_names = (const char *[]){
				"mmss_ahb_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch venus0_axi_clk = {
	.halt_reg = 0x1034,
	.clkr = {
		.enable_reg = 0x1034,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "venus0_axi_clk",
			.parent_names = (const char *[]){
				"mmss_axi_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch venus0_core0_vcodec_clk = {
	.halt_reg = 0x1048,
	.clkr = {
		.enable_reg = 0x1048,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "venus0_core0_vcodec_clk",
			.parent_names = (const char *[]){
				"vcodec0_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch venus0_core1_vcodec_clk = {
	.halt_reg = 0x104c,
	.clkr = {
		.enable_reg = 0x104c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "venus0_core1_vcodec_clk",
			.parent_names = (const char *[]){
				"vcodec0_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch venus0_ocmemnoc_clk = {
	.halt_reg = 0x1038,
	.clkr = {
		.enable_reg = 0x1038,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "venus0_ocmemnoc_clk",
			.parent_names = (const char *[]){
				"ocmemnoc_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch venus0_vcodec0_clk = {
	.halt_reg = 0x1028,
	.clkr = {
		.enable_reg = 0x1028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "venus0_vcodec0_clk",
			.parent_names = (const char *[]){
				"vcodec0_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch vpu_ahb_clk = {
	.halt_reg = 0x1430,
	.clkr = {
		.enable_reg = 0x1430,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "vpu_ahb_clk",
			.parent_names = (const char *[]){
				"mmss_ahb_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch vpu_axi_clk = {
	.halt_reg = 0x143c,
	.clkr = {
		.enable_reg = 0x143c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "vpu_axi_clk",
			.parent_names = (const char *[]){
				"mmss_axi_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch vpu_bus_clk = {
	.halt_reg = 0x1440,
	.clkr = {
		.enable_reg = 0x1440,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "vpu_bus_clk",
			.parent_names = (const char *[]){
				"vpu_bus_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch vpu_cxo_clk = {
	.halt_reg = 0x1434,
	.clkr = {
		.enable_reg = 0x1434,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "vpu_cxo_clk",
			.parent_names = (const char *[]){ "xo" },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch vpu_maple_clk = {
	.halt_reg = 0x142c,
	.clkr = {
		.enable_reg = 0x142c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "vpu_maple_clk",
			.parent_names = (const char *[]){
				"maple_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch vpu_sleep_clk = {
	.halt_reg = 0x1438,
	.clkr = {
		.enable_reg = 0x1438,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "vpu_sleep_clk",
			.parent_names = (const char *[]){
				"sleep_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch vpu_vdp_clk = {
	.halt_reg = 0x1428,
	.clkr = {
		.enable_reg = 0x1428,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "vpu_vdp_clk",
			.parent_names = (const char *[]){
				"vdp_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static const struct pll_config mmpll1_config = {
	.l = 60,
	.m = 25,
	.n = 32,
	.vco_val = 0x0,
	.vco_mask = 0x3 << 20,
	.pre_div_val = 0x0,
	.pre_div_mask = 0x7 << 12,
	.post_div_val = 0x0,
	.post_div_mask = 0x3 << 8,
	.mn_ena_mask = BIT(24),
	.main_output_mask = BIT(0),
};

static const struct pll_config mmpll3_config = {
	.l = 48,
	.m = 7,
	.n = 16,
	.vco_val = 0x0,
	.vco_mask = 0x3 << 20,
	.pre_div_val = 0x0,
	.pre_div_mask = 0x7 << 12,
	.post_div_val = 0x0,
	.post_div_mask = 0x3 << 8,
	.mn_ena_mask = BIT(24),
	.main_output_mask = BIT(0),
	.aux_output_mask = BIT(1),
};

static struct gdsc venus0_gdsc = {
	.gdscr = 0x1024,
	.pd = {
		.name = "venus0",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc venus0_core0_gdsc = {
	.gdscr = 0x1040,
	.pd = {
		.name = "venus0_core0",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc venus0_core1_gdsc = {
	.gdscr = 0x1044,
	.pd = {
		.name = "venus0_core1",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc mdss_gdsc = {
	.gdscr = 0x2304,
	.cxcs = (unsigned int []){ 0x231c, 0x2320 },
	.cxc_count = 2,
	.pd = {
		.name = "mdss",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc camss_jpeg_gdsc = {
	.gdscr = 0x35a4,
	.pd = {
		.name = "camss_jpeg",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc camss_vfe_gdsc = {
	.gdscr = 0x36a4,
	.cxcs = (unsigned int []){ 0x36a8, 0x36ac, 0x36b0 },
	.cxc_count = 3,
	.pd = {
		.name = "camss_vfe",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc oxili_gdsc = {
	.gdscr = 0x4024,
	.cxcs = (unsigned int []){ 0x4028 },
	.cxc_count = 1,
	.pd = {
		.name = "oxili",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc oxilicx_gdsc = {
	.gdscr = 0x4034,
	.pd = {
		.name = "oxilicx",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct clk_regmap *mmcc_apq8084_clocks[] = {
	[MMSS_AHB_CLK_SRC] = &mmss_ahb_clk_src.clkr,
	[MMSS_AXI_CLK_SRC] = &mmss_axi_clk_src.clkr,
	[MMPLL0] = &mmpll0.clkr,
	[MMPLL0_VOTE] = &mmpll0_vote,
	[MMPLL1] = &mmpll1.clkr,
	[MMPLL1_VOTE] = &mmpll1_vote,
	[MMPLL2] = &mmpll2.clkr,
	[MMPLL3] = &mmpll3.clkr,
	[MMPLL4] = &mmpll4.clkr,
	[CSI0_CLK_SRC] = &csi0_clk_src.clkr,
	[CSI1_CLK_SRC] = &csi1_clk_src.clkr,
	[CSI2_CLK_SRC] = &csi2_clk_src.clkr,
	[CSI3_CLK_SRC] = &csi3_clk_src.clkr,
	[VCODEC0_CLK_SRC] = &vcodec0_clk_src.clkr,
	[VFE0_CLK_SRC] = &vfe0_clk_src.clkr,
	[VFE1_CLK_SRC] = &vfe1_clk_src.clkr,
	[MDP_CLK_SRC] = &mdp_clk_src.clkr,
	[PCLK0_CLK_SRC] = &pclk0_clk_src.clkr,
	[PCLK1_CLK_SRC] = &pclk1_clk_src.clkr,
	[OCMEMNOC_CLK_SRC] = &ocmemnoc_clk_src.clkr,
	[GFX3D_CLK_SRC] = &gfx3d_clk_src.clkr,
	[JPEG0_CLK_SRC] = &jpeg0_clk_src.clkr,
	[JPEG1_CLK_SRC] = &jpeg1_clk_src.clkr,
	[JPEG2_CLK_SRC] = &jpeg2_clk_src.clkr,
	[EDPPIXEL_CLK_SRC] = &edppixel_clk_src.clkr,
	[EXTPCLK_CLK_SRC] = &extpclk_clk_src.clkr,
	[VP_CLK_SRC] = &vp_clk_src.clkr,
	[CCI_CLK_SRC] = &cci_clk_src.clkr,
	[CAMSS_GP0_CLK_SRC] = &camss_gp0_clk_src.clkr,
	[CAMSS_GP1_CLK_SRC] = &camss_gp1_clk_src.clkr,
	[MCLK0_CLK_SRC] = &mclk0_clk_src.clkr,
	[MCLK1_CLK_SRC] = &mclk1_clk_src.clkr,
	[MCLK2_CLK_SRC] = &mclk2_clk_src.clkr,
	[MCLK3_CLK_SRC] = &mclk3_clk_src.clkr,
	[CSI0PHYTIMER_CLK_SRC] = &csi0phytimer_clk_src.clkr,
	[CSI1PHYTIMER_CLK_SRC] = &csi1phytimer_clk_src.clkr,
	[CSI2PHYTIMER_CLK_SRC] = &csi2phytimer_clk_src.clkr,
	[CPP_CLK_SRC] = &cpp_clk_src.clkr,
	[BYTE0_CLK_SRC] = &byte0_clk_src.clkr,
	[BYTE1_CLK_SRC] = &byte1_clk_src.clkr,
	[EDPAUX_CLK_SRC] = &edpaux_clk_src.clkr,
	[EDPLINK_CLK_SRC] = &edplink_clk_src.clkr,
	[ESC0_CLK_SRC] = &esc0_clk_src.clkr,
	[ESC1_CLK_SRC] = &esc1_clk_src.clkr,
	[HDMI_CLK_SRC] = &hdmi_clk_src.clkr,
	[VSYNC_CLK_SRC] = &vsync_clk_src.clkr,
	[MMSS_RBCPR_CLK_SRC] = &rbcpr_clk_src.clkr,
	[RBBMTIMER_CLK_SRC] = &rbbmtimer_clk_src.clkr,
	[MAPLE_CLK_SRC] = &maple_clk_src.clkr,
	[VDP_CLK_SRC] = &vdp_clk_src.clkr,
	[VPU_BUS_CLK_SRC] = &vpu_bus_clk_src.clkr,
	[MMSS_CXO_CLK] = &mmss_cxo_clk.clkr,
	[MMSS_SLEEPCLK_CLK] = &mmss_sleepclk_clk.clkr,
	[AVSYNC_AHB_CLK] = &avsync_ahb_clk.clkr,
	[AVSYNC_EDPPIXEL_CLK] = &avsync_edppixel_clk.clkr,
	[AVSYNC_EXTPCLK_CLK] = &avsync_extpclk_clk.clkr,
	[AVSYNC_PCLK0_CLK] = &avsync_pclk0_clk.clkr,
	[AVSYNC_PCLK1_CLK] = &avsync_pclk1_clk.clkr,
	[AVSYNC_VP_CLK] = &avsync_vp_clk.clkr,
	[CAMSS_AHB_CLK] = &camss_ahb_clk.clkr,
	[CAMSS_CCI_CCI_AHB_CLK] = &camss_cci_cci_ahb_clk.clkr,
	[CAMSS_CCI_CCI_CLK] = &camss_cci_cci_clk.clkr,
	[CAMSS_CSI0_AHB_CLK] = &camss_csi0_ahb_clk.clkr,
	[CAMSS_CSI0_CLK] = &camss_csi0_clk.clkr,
	[CAMSS_CSI0PHY_CLK] = &camss_csi0phy_clk.clkr,
	[CAMSS_CSI0PIX_CLK] = &camss_csi0pix_clk.clkr,
	[CAMSS_CSI0RDI_CLK] = &camss_csi0rdi_clk.clkr,
	[CAMSS_CSI1_AHB_CLK] = &camss_csi1_ahb_clk.clkr,
	[CAMSS_CSI1_CLK] = &camss_csi1_clk.clkr,
	[CAMSS_CSI1PHY_CLK] = &camss_csi1phy_clk.clkr,
	[CAMSS_CSI1PIX_CLK] = &camss_csi1pix_clk.clkr,
	[CAMSS_CSI1RDI_CLK] = &camss_csi1rdi_clk.clkr,
	[CAMSS_CSI2_AHB_CLK] = &camss_csi2_ahb_clk.clkr,
	[CAMSS_CSI2_CLK] = &camss_csi2_clk.clkr,
	[CAMSS_CSI2PHY_CLK] = &camss_csi2phy_clk.clkr,
	[CAMSS_CSI2PIX_CLK] = &camss_csi2pix_clk.clkr,
	[CAMSS_CSI2RDI_CLK] = &camss_csi2rdi_clk.clkr,
	[CAMSS_CSI3_AHB_CLK] = &camss_csi3_ahb_clk.clkr,
	[CAMSS_CSI3_CLK] = &camss_csi3_clk.clkr,
	[CAMSS_CSI3PHY_CLK] = &camss_csi3phy_clk.clkr,
	[CAMSS_CSI3PIX_CLK] = &camss_csi3pix_clk.clkr,
	[CAMSS_CSI3RDI_CLK] = &camss_csi3rdi_clk.clkr,
	[CAMSS_CSI_VFE0_CLK] = &camss_csi_vfe0_clk.clkr,
	[CAMSS_CSI_VFE1_CLK] = &camss_csi_vfe1_clk.clkr,
	[CAMSS_GP0_CLK] = &camss_gp0_clk.clkr,
	[CAMSS_GP1_CLK] = &camss_gp1_clk.clkr,
	[CAMSS_ISPIF_AHB_CLK] = &camss_ispif_ahb_clk.clkr,
	[CAMSS_JPEG_JPEG0_CLK] = &camss_jpeg_jpeg0_clk.clkr,
	[CAMSS_JPEG_JPEG1_CLK] = &camss_jpeg_jpeg1_clk.clkr,
	[CAMSS_JPEG_JPEG2_CLK] = &camss_jpeg_jpeg2_clk.clkr,
	[CAMSS_JPEG_JPEG_AHB_CLK] = &camss_jpeg_jpeg_ahb_clk.clkr,
	[CAMSS_JPEG_JPEG_AXI_CLK] = &camss_jpeg_jpeg_axi_clk.clkr,
	[CAMSS_MCLK0_CLK] = &camss_mclk0_clk.clkr,
	[CAMSS_MCLK1_CLK] = &camss_mclk1_clk.clkr,
	[CAMSS_MCLK2_CLK] = &camss_mclk2_clk.clkr,
	[CAMSS_MCLK3_CLK] = &camss_mclk3_clk.clkr,
	[CAMSS_MICRO_AHB_CLK] = &camss_micro_ahb_clk.clkr,
	[CAMSS_PHY0_CSI0PHYTIMER_CLK] = &camss_phy0_csi0phytimer_clk.clkr,
	[CAMSS_PHY1_CSI1PHYTIMER_CLK] = &camss_phy1_csi1phytimer_clk.clkr,
	[CAMSS_PHY2_CSI2PHYTIMER_CLK] = &camss_phy2_csi2phytimer_clk.clkr,
	[CAMSS_TOP_AHB_CLK] = &camss_top_ahb_clk.clkr,
	[CAMSS_VFE_CPP_AHB_CLK] = &camss_vfe_cpp_ahb_clk.clkr,
	[CAMSS_VFE_CPP_CLK] = &camss_vfe_cpp_clk.clkr,
	[CAMSS_VFE_VFE0_CLK] = &camss_vfe_vfe0_clk.clkr,
	[CAMSS_VFE_VFE1_CLK] = &camss_vfe_vfe1_clk.clkr,
	[CAMSS_VFE_VFE_AHB_CLK] = &camss_vfe_vfe_ahb_clk.clkr,
	[CAMSS_VFE_VFE_AXI_CLK] = &camss_vfe_vfe_axi_clk.clkr,
	[MDSS_AHB_CLK] = &mdss_ahb_clk.clkr,
	[MDSS_AXI_CLK] = &mdss_axi_clk.clkr,
	[MDSS_BYTE0_CLK] = &mdss_byte0_clk.clkr,
	[MDSS_BYTE1_CLK] = &mdss_byte1_clk.clkr,
	[MDSS_EDPAUX_CLK] = &mdss_edpaux_clk.clkr,
	[MDSS_EDPLINK_CLK] = &mdss_edplink_clk.clkr,
	[MDSS_EDPPIXEL_CLK] = &mdss_edppixel_clk.clkr,
	[MDSS_ESC0_CLK] = &mdss_esc0_clk.clkr,
	[MDSS_ESC1_CLK] = &mdss_esc1_clk.clkr,
	[MDSS_EXTPCLK_CLK] = &mdss_extpclk_clk.clkr,
	[MDSS_HDMI_AHB_CLK] = &mdss_hdmi_ahb_clk.clkr,
	[MDSS_HDMI_CLK] = &mdss_hdmi_clk.clkr,
	[MDSS_MDP_CLK] = &mdss_mdp_clk.clkr,
	[MDSS_MDP_LUT_CLK] = &mdss_mdp_lut_clk.clkr,
	[MDSS_PCLK0_CLK] = &mdss_pclk0_clk.clkr,
	[MDSS_PCLK1_CLK] = &mdss_pclk1_clk.clkr,
	[MDSS_VSYNC_CLK] = &mdss_vsync_clk.clkr,
	[MMSS_RBCPR_AHB_CLK] = &mmss_rbcpr_ahb_clk.clkr,
	[MMSS_RBCPR_CLK] = &mmss_rbcpr_clk.clkr,
	[MMSS_SPDM_AHB_CLK] = &mmss_spdm_ahb_clk.clkr,
	[MMSS_SPDM_AXI_CLK] = &mmss_spdm_axi_clk.clkr,
	[MMSS_SPDM_CSI0_CLK] = &mmss_spdm_csi0_clk.clkr,
	[MMSS_SPDM_GFX3D_CLK] = &mmss_spdm_gfx3d_clk.clkr,
	[MMSS_SPDM_JPEG0_CLK] = &mmss_spdm_jpeg0_clk.clkr,
	[MMSS_SPDM_JPEG1_CLK] = &mmss_spdm_jpeg1_clk.clkr,
	[MMSS_SPDM_JPEG2_CLK] = &mmss_spdm_jpeg2_clk.clkr,
	[MMSS_SPDM_MDP_CLK] = &mmss_spdm_mdp_clk.clkr,
	[MMSS_SPDM_PCLK0_CLK] = &mmss_spdm_pclk0_clk.clkr,
	[MMSS_SPDM_PCLK1_CLK] = &mmss_spdm_pclk1_clk.clkr,
	[MMSS_SPDM_VCODEC0_CLK] = &mmss_spdm_vcodec0_clk.clkr,
	[MMSS_SPDM_VFE0_CLK] = &mmss_spdm_vfe0_clk.clkr,
	[MMSS_SPDM_VFE1_CLK] = &mmss_spdm_vfe1_clk.clkr,
	[MMSS_SPDM_RM_AXI_CLK] = &mmss_spdm_rm_axi_clk.clkr,
	[MMSS_SPDM_RM_OCMEMNOC_CLK] = &mmss_spdm_rm_ocmemnoc_clk.clkr,
	[MMSS_MISC_AHB_CLK] = &mmss_misc_ahb_clk.clkr,
	[MMSS_MMSSNOC_AHB_CLK] = &mmss_mmssnoc_ahb_clk.clkr,
	[MMSS_MMSSNOC_BTO_AHB_CLK] = &mmss_mmssnoc_bto_ahb_clk.clkr,
	[MMSS_MMSSNOC_AXI_CLK] = &mmss_mmssnoc_axi_clk.clkr,
	[MMSS_S0_AXI_CLK] = &mmss_s0_axi_clk.clkr,
	[OCMEMCX_AHB_CLK] = &ocmemcx_ahb_clk.clkr,
	[OCMEMCX_OCMEMNOC_CLK] = &ocmemcx_ocmemnoc_clk.clkr,
	[OXILI_OCMEMGX_CLK] = &oxili_ocmemgx_clk.clkr,
	[OXILI_GFX3D_CLK] = &oxili_gfx3d_clk.clkr,
	[OXILI_RBBMTIMER_CLK] = &oxili_rbbmtimer_clk.clkr,
	[OXILICX_AHB_CLK] = &oxilicx_ahb_clk.clkr,
	[VENUS0_AHB_CLK] = &venus0_ahb_clk.clkr,
	[VENUS0_AXI_CLK] = &venus0_axi_clk.clkr,
	[VENUS0_CORE0_VCODEC_CLK] = &venus0_core0_vcodec_clk.clkr,
	[VENUS0_CORE1_VCODEC_CLK] = &venus0_core1_vcodec_clk.clkr,
	[VENUS0_OCMEMNOC_CLK] = &venus0_ocmemnoc_clk.clkr,
	[VENUS0_VCODEC0_CLK] = &venus0_vcodec0_clk.clkr,
	[VPU_AHB_CLK] = &vpu_ahb_clk.clkr,
	[VPU_AXI_CLK] = &vpu_axi_clk.clkr,
	[VPU_BUS_CLK] = &vpu_bus_clk.clkr,
	[VPU_CXO_CLK] = &vpu_cxo_clk.clkr,
	[VPU_MAPLE_CLK] = &vpu_maple_clk.clkr,
	[VPU_SLEEP_CLK] = &vpu_sleep_clk.clkr,
	[VPU_VDP_CLK] = &vpu_vdp_clk.clkr,
};

static const struct qcom_reset_map mmcc_apq8084_resets[] = {
	[MMSS_SPDM_RESET] = { 0x0200 },
	[MMSS_SPDM_RM_RESET] = { 0x0300 },
	[VENUS0_RESET] = { 0x1020 },
	[VPU_RESET] = { 0x1400 },
	[MDSS_RESET] = { 0x2300 },
	[AVSYNC_RESET] = { 0x2400 },
	[CAMSS_PHY0_RESET] = { 0x3020 },
	[CAMSS_PHY1_RESET] = { 0x3050 },
	[CAMSS_PHY2_RESET] = { 0x3080 },
	[CAMSS_CSI0_RESET] = { 0x30b0 },
	[CAMSS_CSI0PHY_RESET] = { 0x30c0 },
	[CAMSS_CSI0RDI_RESET] = { 0x30d0 },
	[CAMSS_CSI0PIX_RESET] = { 0x30e0 },
	[CAMSS_CSI1_RESET] = { 0x3120 },
	[CAMSS_CSI1PHY_RESET] = { 0x3130 },
	[CAMSS_CSI1RDI_RESET] = { 0x3140 },
	[CAMSS_CSI1PIX_RESET] = { 0x3150 },
	[CAMSS_CSI2_RESET] = { 0x3180 },
	[CAMSS_CSI2PHY_RESET] = { 0x3190 },
	[CAMSS_CSI2RDI_RESET] = { 0x31a0 },
	[CAMSS_CSI2PIX_RESET] = { 0x31b0 },
	[CAMSS_CSI3_RESET] = { 0x31e0 },
	[CAMSS_CSI3PHY_RESET] = { 0x31f0 },
	[CAMSS_CSI3RDI_RESET] = { 0x3200 },
	[CAMSS_CSI3PIX_RESET] = { 0x3210 },
	[CAMSS_ISPIF_RESET] = { 0x3220 },
	[CAMSS_CCI_RESET] = { 0x3340 },
	[CAMSS_MCLK0_RESET] = { 0x3380 },
	[CAMSS_MCLK1_RESET] = { 0x33b0 },
	[CAMSS_MCLK2_RESET] = { 0x33e0 },
	[CAMSS_MCLK3_RESET] = { 0x3410 },
	[CAMSS_GP0_RESET] = { 0x3440 },
	[CAMSS_GP1_RESET] = { 0x3470 },
	[CAMSS_TOP_RESET] = { 0x3480 },
	[CAMSS_AHB_RESET] = { 0x3488 },
	[CAMSS_MICRO_RESET] = { 0x3490 },
	[CAMSS_JPEG_RESET] = { 0x35a0 },
	[CAMSS_VFE_RESET] = { 0x36a0 },
	[CAMSS_CSI_VFE0_RESET] = { 0x3700 },
	[CAMSS_CSI_VFE1_RESET] = { 0x3710 },
	[OXILI_RESET] = { 0x4020 },
	[OXILICX_RESET] = { 0x4030 },
	[OCMEMCX_RESET] = { 0x4050 },
	[MMSS_RBCRP_RESET] = { 0x4080 },
	[MMSSNOCAHB_RESET] = { 0x5020 },
	[MMSSNOCAXI_RESET] = { 0x5060 },
};

static struct gdsc *mmcc_apq8084_gdscs[] = {
	[VENUS0_GDSC] = &venus0_gdsc,
	[VENUS0_CORE0_GDSC] = &venus0_core0_gdsc,
	[VENUS0_CORE1_GDSC] = &venus0_core1_gdsc,
	[MDSS_GDSC] = &mdss_gdsc,
	[CAMSS_JPEG_GDSC] = &camss_jpeg_gdsc,
	[CAMSS_VFE_GDSC] = &camss_vfe_gdsc,
	[OXILI_GDSC] = &oxili_gdsc,
	[OXILICX_GDSC] = &oxilicx_gdsc,
};

static const struct regmap_config mmcc_apq8084_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x5104,
	.fast_io	= true,
};

static const struct qcom_cc_desc mmcc_apq8084_desc = {
	.config = &mmcc_apq8084_regmap_config,
	.clks = mmcc_apq8084_clocks,
	.num_clks = ARRAY_SIZE(mmcc_apq8084_clocks),
	.resets = mmcc_apq8084_resets,
	.num_resets = ARRAY_SIZE(mmcc_apq8084_resets),
	.gdscs = mmcc_apq8084_gdscs,
	.num_gdscs = ARRAY_SIZE(mmcc_apq8084_gdscs),
};

static const struct of_device_id mmcc_apq8084_match_table[] = {
	{ .compatible = "qcom,mmcc-apq8084" },
	{ }
};
MODULE_DEVICE_TABLE(of, mmcc_apq8084_match_table);

static int mmcc_apq8084_probe(struct platform_device *pdev)
{
	int ret;
	struct regmap *regmap;

	ret = qcom_cc_probe(pdev, &mmcc_apq8084_desc);
	if (ret)
		return ret;

	regmap = dev_get_regmap(&pdev->dev, NULL);
	clk_pll_configure_sr_hpm_lp(&mmpll1, regmap, &mmpll1_config, true);
	clk_pll_configure_sr_hpm_lp(&mmpll3, regmap, &mmpll3_config, false);

	return 0;
}

static struct platform_driver mmcc_apq8084_driver = {
	.probe		= mmcc_apq8084_probe,
	.driver		= {
		.name	= "mmcc-apq8084",
		.of_match_table = mmcc_apq8084_match_table,
	},
};
module_platform_driver(mmcc_apq8084_driver);

MODULE_DESCRIPTION("QCOM MMCC APQ8084 Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:mmcc-apq8084");
