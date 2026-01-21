// SPDX-License-Identifier: (GPL-2.0-only OR MIT)
/*
 * Copyright (C) 2024-2025 Amlogic, Inc. All rights reserved.
 * Author: Jian Hu <jian.hu@amlogic.com>
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include "clk-dualdiv.h"
#include "clk-regmap.h"
#include "meson-clkc-utils.h"
#include <dt-bindings/clock/amlogic,t7-peripherals-clkc.h>

#define RTC_BY_OSCIN_CTRL0	0x8
#define RTC_BY_OSCIN_CTRL1	0xc
#define RTC_CTRL		0x10
#define SYS_CLK_CTRL0		0x40
#define SYS_CLK_EN0_REG0	0x44
#define SYS_CLK_EN0_REG1	0x48
#define SYS_CLK_EN0_REG2	0x4c
#define SYS_CLK_EN0_REG3	0x50
#define CECA_CTRL0		0x88
#define CECA_CTRL1		0x8c
#define CECB_CTRL0		0x90
#define CECB_CTRL1		0x94
#define SC_CLK_CTRL		0x98
#define DSPA_CLK_CTRL0		0x9c
#define DSPB_CLK_CTRL0		0xa0
#define CLK12_24_CTRL		0xa8
#define ANAKIN_CLK_CTRL		0xac
#define MIPI_CSI_PHY_CLK_CTRL	0x10c
#define MIPI_ISP_CLK_CTRL	0x110
#define TS_CLK_CTRL		0x158
#define MALI_CLK_CTRL		0x15c
#define ETH_CLK_CTRL		0x164
#define NAND_CLK_CTRL		0x168
#define SD_EMMC_CLK_CTRL	0x16c
#define SPICC_CLK_CTRL		0x174
#define SAR_CLK_CTRL0		0x17c
#define PWM_CLK_AB_CTRL		0x180
#define PWM_CLK_CD_CTRL		0x184
#define PWM_CLK_EF_CTRL		0x188
#define PWM_CLK_AO_AB_CTRL	0x1a0
#define PWM_CLK_AO_CD_CTRL	0x1a4
#define PWM_CLK_AO_EF_CTRL	0x1a8
#define PWM_CLK_AO_GH_CTRL	0x1ac
#define SPICC_CLK_CTRL1		0x1c0
#define SPICC_CLK_CTRL2		0x1c4

#define T7_COMP_SEL(_name, _reg, _shift, _mask, _pdata) \
	MESON_COMP_SEL(t7_, _name, _reg, _shift, _mask, _pdata, NULL, 0, 0)

#define T7_COMP_DIV(_name, _reg, _shift, _width) \
	MESON_COMP_DIV(t7_, _name, _reg, _shift, _width, 0, CLK_SET_RATE_PARENT)

#define T7_COMP_GATE(_name, _reg, _bit, _iflags) \
	MESON_COMP_GATE(t7_, _name, _reg, _bit, CLK_SET_RATE_PARENT | (_iflags))

static struct clk_regmap t7_rtc_dualdiv_in = {
	.data = &(struct clk_regmap_gate_data){
		.offset = RTC_BY_OSCIN_CTRL0,
		.bit_idx = 31,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "rtc_duandiv_in",
		.ops = &clk_regmap_gate_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static const struct meson_clk_dualdiv_param t7_dualdiv_table[] = {
	{
		.n1	= 733, .m1	= 8,
		.n2	= 732, .m2	= 11,
		.dual	= 1,
	},
	{}
};

static struct clk_regmap t7_rtc_dualdiv_div = {
	.data = &(struct meson_clk_dualdiv_data){
		.n1 = {
			.reg_off = RTC_BY_OSCIN_CTRL0,
			.shift   = 0,
			.width   = 12,
		},
		.n2 = {
			.reg_off = RTC_BY_OSCIN_CTRL0,
			.shift   = 12,
			.width   = 12,
		},
		.m1 = {
			.reg_off = RTC_BY_OSCIN_CTRL1,
			.shift   = 0,
			.width   = 12,
		},
		.m2 = {
			.reg_off = RTC_BY_OSCIN_CTRL1,
			.shift   = 12,
			.width   = 12,
		},
		.dual = {
			.reg_off = RTC_BY_OSCIN_CTRL0,
			.shift   = 28,
			.width   = 1,
		},
		.table = t7_dualdiv_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "rtc_dualdiv_div",
		.ops = &meson_clk_dualdiv_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&t7_rtc_dualdiv_in.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap t7_rtc_dualdiv_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = RTC_BY_OSCIN_CTRL1,
		.mask = 0x1,
		.shift = 24,
	},
	.hw.init = &(struct clk_init_data){
		.name = "rtc_dualdiv_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&t7_rtc_dualdiv_div.hw,
			&t7_rtc_dualdiv_in.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap t7_rtc_dualdiv = {
	.data = &(struct clk_regmap_gate_data){
		.offset = RTC_BY_OSCIN_CTRL0,
		.bit_idx = 30,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "rtc_dualdiv",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&t7_rtc_dualdiv_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap t7_rtc = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = RTC_CTRL,
		.mask = 0x3,
		.shift = 0,
	},
	.hw.init = &(struct clk_init_data){
		.name = "rtc",
		.ops = &clk_regmap_mux_ops,
		/*
		 * xtal is also on parent input #3 but that it is not useful to CCF since
		 * the same parent is available with parent input #0
		 */
		.parent_data = (const struct clk_parent_data []) {
			{ .fw_name = "xtal", },
			{ .hw = &t7_rtc_dualdiv.hw },
			{ .fw_name = "ext_rtc", },
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_NO_REPARENT,
	},
};

static struct clk_regmap t7_ceca_dualdiv_in = {
	.data = &(struct clk_regmap_gate_data){
		.offset = CECA_CTRL0,
		.bit_idx = 31,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "ceca_dualdiv_in",
		.ops = &clk_regmap_gate_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap t7_ceca_dualdiv_div = {
	.data = &(struct meson_clk_dualdiv_data){
		.n1 = {
			.reg_off = CECA_CTRL0,
			.shift   = 0,
			.width   = 12,
		},
		.n2 = {
			.reg_off = CECA_CTRL0,
			.shift   = 12,
			.width   = 12,
		},
		.m1 = {
			.reg_off = CECA_CTRL1,
			.shift   = 0,
			.width   = 12,
		},
		.m2 = {
			.reg_off = CECA_CTRL1,
			.shift   = 12,
			.width   = 12,
		},
		.dual = {
			.reg_off = CECA_CTRL0,
			.shift   = 28,
			.width   = 1,
		},
		.table = t7_dualdiv_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ceca_dualdiv_div",
		.ops = &meson_clk_dualdiv_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&t7_ceca_dualdiv_in.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap t7_ceca_dualdiv_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = CECA_CTRL1,
		.mask = 0x1,
		.shift = 24,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ceca_dualdiv_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&t7_ceca_dualdiv_div.hw,
			&t7_ceca_dualdiv_in.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap t7_ceca_dualdiv = {
	.data = &(struct clk_regmap_gate_data){
		.offset = CECA_CTRL0,
		.bit_idx = 30,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ceca_dualdiv",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&t7_ceca_dualdiv_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap t7_ceca = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = CECA_CTRL1,
		.mask = 0x1,
		.shift = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ceca",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&t7_ceca_dualdiv.hw,
			&t7_rtc.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap t7_cecb_dualdiv_in = {
	.data = &(struct clk_regmap_gate_data){
		.offset = CECB_CTRL0,
		.bit_idx = 31,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "cecb_dualdiv_in",
		.ops = &clk_regmap_gate_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap t7_cecb_dualdiv_div = {
	.data = &(struct meson_clk_dualdiv_data){
		.n1 = {
			.reg_off = CECB_CTRL0,
			.shift   = 0,
			.width   = 12,
		},
		.n2 = {
			.reg_off = CECB_CTRL0,
			.shift   = 12,
			.width   = 12,
		},
		.m1 = {
			.reg_off = CECB_CTRL1,
			.shift   = 0,
			.width   = 12,
		},
		.m2 = {
			.reg_off = CECB_CTRL1,
			.shift   = 12,
			.width   = 12,
		},
		.dual = {
			.reg_off = CECB_CTRL0,
			.shift   = 28,
			.width   = 1,
		},
		.table = t7_dualdiv_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cecb_dualdiv_div",
		.ops = &meson_clk_dualdiv_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&t7_cecb_dualdiv_in.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap t7_cecb_dualdiv_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = CECB_CTRL1,
		.mask = 0x1,
		.shift = 24,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cecb_dualdiv_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&t7_cecb_dualdiv_div.hw,
			&t7_cecb_dualdiv_in.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap t7_cecb_dualdiv = {
	.data = &(struct clk_regmap_gate_data){
		.offset = CECB_CTRL0,
		.bit_idx = 30,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cecb_dualdiv",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&t7_cecb_dualdiv_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap t7_cecb = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = CECB_CTRL1,
		.mask = 0x1,
		.shift = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cecb",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&t7_cecb_dualdiv.hw,
			&t7_rtc.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data t7_sc_parents[] = {
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "xtal", },
};

static T7_COMP_SEL(sc, SC_CLK_CTRL, 9, 0x3, t7_sc_parents);
static T7_COMP_DIV(sc, SC_CLK_CTRL, 0, 8);
static T7_COMP_GATE(sc, SC_CLK_CTRL, 8, 0);

static const struct clk_parent_data t7_dsp_parents[] = {
	{ .fw_name = "xtal", },
	{ .fw_name = "fdiv2p5", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "hifi", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv7", },
	{ .hw = &t7_rtc.hw },
};

static T7_COMP_SEL(dspa_0, DSPA_CLK_CTRL0, 10, 0x7, t7_dsp_parents);
static T7_COMP_DIV(dspa_0, DSPA_CLK_CTRL0, 0, 10);
static T7_COMP_GATE(dspa_0, DSPA_CLK_CTRL0, 13, CLK_SET_RATE_GATE);

static T7_COMP_SEL(dspa_1, DSPA_CLK_CTRL0, 26, 0x7, t7_dsp_parents);
static T7_COMP_DIV(dspa_1, DSPA_CLK_CTRL0, 16, 10);
static T7_COMP_GATE(dspa_1, DSPA_CLK_CTRL0, 29, CLK_SET_RATE_GATE);

static struct clk_regmap t7_dspa = {
	.data = &(struct clk_regmap_mux_data){
		.offset = DSPA_CLK_CTRL0,
		.mask = 0x1,
		.shift = 15,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dspa",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&t7_dspa_0.hw,
			&t7_dspa_1.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static T7_COMP_SEL(dspb_0, DSPB_CLK_CTRL0, 10, 0x7, t7_dsp_parents);
static T7_COMP_DIV(dspb_0, DSPB_CLK_CTRL0, 0, 10);
static T7_COMP_GATE(dspb_0, DSPB_CLK_CTRL0, 13, CLK_SET_RATE_GATE);

static T7_COMP_SEL(dspb_1, DSPB_CLK_CTRL0, 26, 0x7, t7_dsp_parents);
static T7_COMP_DIV(dspb_1, DSPB_CLK_CTRL0, 16, 10);
static T7_COMP_GATE(dspb_1, DSPB_CLK_CTRL0, 29, CLK_SET_RATE_GATE);

static struct clk_regmap t7_dspb = {
	.data = &(struct clk_regmap_mux_data){
		.offset = DSPB_CLK_CTRL0,
		.mask = 0x1,
		.shift = 15,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dspb",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&t7_dspb_0.hw,
			&t7_dspb_1.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap t7_24m = {
	.data = &(struct clk_regmap_gate_data){
		.offset = CLK12_24_CTRL,
		.bit_idx = 11,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "24m",
		.ops = &clk_regmap_gate_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor t7_24m_div2 = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "24m_div2",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&t7_24m.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap t7_12m = {
	.data = &(struct clk_regmap_gate_data){
		.offset = CLK12_24_CTRL,
		.bit_idx = 10,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "12m",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&t7_24m_div2.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap t7_25m_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = CLK12_24_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "25m_div",
		.ops = &clk_regmap_divider_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "fix",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap t7_25m = {
	.data = &(struct clk_regmap_gate_data){
		.offset = CLK12_24_CTRL,
		.bit_idx = 12,
	},
	.hw.init = &(struct clk_init_data){
		.name = "25m",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&t7_25m_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data t7_anakin_parents[] = {
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "fdiv2", },
	{ .fw_name = "vid_pll0", },
	{ .fw_name = "mpll1", },
	{ .fw_name = "mpll2", },
	{ .fw_name = "fdiv2p5", },
};

static T7_COMP_SEL(anakin_0, ANAKIN_CLK_CTRL, 9, 0x7, t7_anakin_parents);
static T7_COMP_DIV(anakin_0, ANAKIN_CLK_CTRL, 0, 7);
static T7_COMP_GATE(anakin_0, ANAKIN_CLK_CTRL, 8, CLK_SET_RATE_GATE);

static T7_COMP_SEL(anakin_1, ANAKIN_CLK_CTRL, 25, 0x7, t7_anakin_parents);
static T7_COMP_DIV(anakin_1, ANAKIN_CLK_CTRL, 16, 7);
static T7_COMP_GATE(anakin_1, ANAKIN_CLK_CTRL, 24, CLK_SET_RATE_GATE);

static struct clk_regmap t7_anakin_01_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = ANAKIN_CLK_CTRL,
		.mask = 1,
		.shift = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "anakin_01_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&t7_anakin_0.hw,
			&t7_anakin_1.hw
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT
	},
};

static struct clk_regmap t7_anakin = {
	.data = &(struct clk_regmap_gate_data){
		.offset = ANAKIN_CLK_CTRL,
		.bit_idx = 30,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "anakin",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&t7_anakin_01_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT
	},
};

static const struct clk_parent_data t7_mipi_csi_phy_parents[] = {
	{ .fw_name = "xtal", },
	{ .fw_name = "gp1", },
	{ .fw_name = "mpll1", },
	{ .fw_name = "mpll2", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "fdiv7", },
};

static T7_COMP_SEL(mipi_csi_phy_0, MIPI_CSI_PHY_CLK_CTRL, 9, 0x7, t7_mipi_csi_phy_parents);
static T7_COMP_DIV(mipi_csi_phy_0, MIPI_CSI_PHY_CLK_CTRL, 0, 7);
static T7_COMP_GATE(mipi_csi_phy_0, MIPI_CSI_PHY_CLK_CTRL, 8, CLK_SET_RATE_GATE);

static T7_COMP_SEL(mipi_csi_phy_1, MIPI_CSI_PHY_CLK_CTRL, 25, 0x7, t7_mipi_csi_phy_parents);
static T7_COMP_DIV(mipi_csi_phy_1, MIPI_CSI_PHY_CLK_CTRL, 16, 7);
static T7_COMP_GATE(mipi_csi_phy_1, MIPI_CSI_PHY_CLK_CTRL, 24, CLK_SET_RATE_GATE);

static struct clk_regmap t7_mipi_csi_phy = {
	.data = &(struct clk_regmap_mux_data){
		.offset = MIPI_CSI_PHY_CLK_CTRL,
		.mask = 0x1,
		.shift = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mipi_csi_phy",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&t7_mipi_csi_phy_0.hw,
			&t7_mipi_csi_phy_1.hw
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data t7_mipi_isp_parents[] = {
	{ .fw_name = "xtal", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "fdiv7", },
	{ .fw_name = "mpll2", },
	{ .fw_name = "mpll3", },
	{ .fw_name = "gp1", },
};

static T7_COMP_SEL(mipi_isp, MIPI_ISP_CLK_CTRL, 9, 0x7, t7_mipi_isp_parents);
static T7_COMP_DIV(mipi_isp, MIPI_ISP_CLK_CTRL, 0, 7);
static T7_COMP_GATE(mipi_isp, MIPI_ISP_CLK_CTRL, 8, 0);

static struct clk_regmap t7_ts_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = TS_CLK_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ts_div",
		.ops = &clk_regmap_divider_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap t7_ts = {
	.data = &(struct clk_regmap_gate_data){
		.offset = TS_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ts",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&t7_ts_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data t7_mali_parents[] = {
	{ .fw_name = "xtal", },
	{ .fw_name = "gp0", },
	{ .fw_name = "gp1", },
	{ .fw_name = "fdiv2p5", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "fdiv7", },
};

static T7_COMP_SEL(mali_0, MALI_CLK_CTRL, 9, 0x7, t7_mali_parents);
static T7_COMP_DIV(mali_0, MALI_CLK_CTRL, 0, 7);
static T7_COMP_GATE(mali_0, MALI_CLK_CTRL, 8, CLK_SET_RATE_GATE);

static T7_COMP_SEL(mali_1, MALI_CLK_CTRL, 25, 0x7, t7_mali_parents);
static T7_COMP_DIV(mali_1, MALI_CLK_CTRL, 16, 7);
static T7_COMP_GATE(mali_1, MALI_CLK_CTRL, 24, CLK_SET_RATE_GATE);

static struct clk_regmap t7_mali = {
	.data = &(struct clk_regmap_mux_data){
		.offset = MALI_CLK_CTRL,
		.mask = 1,
		.shift = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mali",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&t7_mali_0.hw,
			&t7_mali_1.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

/*
 * parent index 2, 3, 4, 5, 6 not connect any clock signal,
 * the last parent connect external PAD
 */
static u32 t7_eth_rmii_parents_val_table[] = { 0, 1, 7 };
static const struct clk_parent_data t7_eth_rmii_parents[] = {
	{ .fw_name = "fdiv2", },
	{ .fw_name = "gp1", },
	{ .fw_name = "ext_rmii", },
};

static struct clk_regmap t7_eth_rmii_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = ETH_CLK_CTRL,
		.mask = 0x7,
		.shift = 9,
		.table = t7_eth_rmii_parents_val_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "eth_rmii_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = t7_eth_rmii_parents,
		.num_parents = ARRAY_SIZE(t7_eth_rmii_parents),
		.flags = CLK_SET_RATE_NO_REPARENT,
	},
};

static struct clk_regmap t7_eth_rmii_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = ETH_CLK_CTRL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "eth_rmii_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&t7_eth_rmii_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap t7_eth_rmii = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = ETH_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "eth_rmii",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&t7_eth_rmii_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_fixed_factor t7_fdiv2_div8 = {
	.mult = 1,
	.div = 8,
	.hw.init = &(struct clk_init_data){
		.name = "fdiv2_div8",
		.ops = &clk_fixed_factor_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "fdiv2",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap t7_eth_125m = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = ETH_CLK_CTRL,
		.bit_idx = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "eth_125m",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&t7_fdiv2_div8.hw
		},
		.num_parents = 1,
	},
};

static const struct clk_parent_data t7_sd_emmc_parents[] = {
	{ .fw_name = "xtal", },
	{ .fw_name = "fdiv2", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "hifi", },
	{ .fw_name = "fdiv2p5", },
	{ .fw_name = "mpll2", },
	{ .fw_name = "mpll3", },
	{ .fw_name = "gp0", },
};

static T7_COMP_SEL(sd_emmc_a, SD_EMMC_CLK_CTRL, 9, 0x7, t7_sd_emmc_parents);
static T7_COMP_DIV(sd_emmc_a, SD_EMMC_CLK_CTRL, 0, 7);
static T7_COMP_GATE(sd_emmc_a, SD_EMMC_CLK_CTRL, 7, 0);

static T7_COMP_SEL(sd_emmc_b, SD_EMMC_CLK_CTRL, 25, 0x7, t7_sd_emmc_parents);
static T7_COMP_DIV(sd_emmc_b, SD_EMMC_CLK_CTRL, 16, 7);
static T7_COMP_GATE(sd_emmc_b, SD_EMMC_CLK_CTRL, 23, 0);

static T7_COMP_SEL(sd_emmc_c, NAND_CLK_CTRL, 9, 0x7, t7_sd_emmc_parents);
static T7_COMP_DIV(sd_emmc_c, NAND_CLK_CTRL, 0, 7);
static T7_COMP_GATE(sd_emmc_c, NAND_CLK_CTRL, 7, 0);

static const struct clk_parent_data t7_spicc_parents[] = {
	{ .fw_name = "xtal", },
	{ .fw_name = "sys", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv2", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "fdiv7", },
	{ .fw_name = "gp1", },
};

static T7_COMP_SEL(spicc0, SPICC_CLK_CTRL, 7, 0x7, t7_spicc_parents);
static T7_COMP_DIV(spicc0, SPICC_CLK_CTRL, 0, 6);
static T7_COMP_GATE(spicc0, SPICC_CLK_CTRL, 6, 0);

static T7_COMP_SEL(spicc1, SPICC_CLK_CTRL, 23, 0x7, t7_spicc_parents);
static T7_COMP_DIV(spicc1, SPICC_CLK_CTRL, 16, 6);
static T7_COMP_GATE(spicc1, SPICC_CLK_CTRL, 22, 0);

static T7_COMP_SEL(spicc2, SPICC_CLK_CTRL1, 7, 0x7, t7_spicc_parents);
static T7_COMP_DIV(spicc2, SPICC_CLK_CTRL1, 0, 6);
static T7_COMP_GATE(spicc2, SPICC_CLK_CTRL1, 6, 0);

static T7_COMP_SEL(spicc3, SPICC_CLK_CTRL1, 23, 0x7, t7_spicc_parents);
static T7_COMP_DIV(spicc3, SPICC_CLK_CTRL1, 16, 6);
static T7_COMP_GATE(spicc3, SPICC_CLK_CTRL1, 22, 0);

static T7_COMP_SEL(spicc4, SPICC_CLK_CTRL2, 7, 0x7, t7_spicc_parents);
static T7_COMP_DIV(spicc4, SPICC_CLK_CTRL2, 0, 6);
static T7_COMP_GATE(spicc4, SPICC_CLK_CTRL2, 6, 0);

static T7_COMP_SEL(spicc5, SPICC_CLK_CTRL2, 23, 0x7, t7_spicc_parents);
static T7_COMP_DIV(spicc5, SPICC_CLK_CTRL2, 16, 6);
static T7_COMP_GATE(spicc5, SPICC_CLK_CTRL2, 22, 0);

static const struct clk_parent_data t7_saradc_parents[] = {
	{ .fw_name = "xtal" },
	{ .fw_name = "sys" },
};

static T7_COMP_SEL(saradc, SAR_CLK_CTRL0, 9, 0x1, t7_saradc_parents);
static T7_COMP_DIV(saradc, SAR_CLK_CTRL0, 0, 8);
static T7_COMP_GATE(saradc, SAR_CLK_CTRL0, 8, 0);

static const struct clk_parent_data t7_pwm_parents[]  = {
	{ .fw_name = "xtal", },
	{ .fw_name = "vid_pll0", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv3", },
};

static T7_COMP_SEL(pwm_a, PWM_CLK_AB_CTRL, 9, 0x3, t7_pwm_parents);
static T7_COMP_DIV(pwm_a, PWM_CLK_AB_CTRL, 0, 8);
static T7_COMP_GATE(pwm_a, PWM_CLK_AB_CTRL, 8, 0);

static T7_COMP_SEL(pwm_b, PWM_CLK_AB_CTRL, 25, 0x3, t7_pwm_parents);
static T7_COMP_DIV(pwm_b, PWM_CLK_AB_CTRL, 16, 8);
static T7_COMP_GATE(pwm_b, PWM_CLK_AB_CTRL, 24, 0);

static T7_COMP_SEL(pwm_c, PWM_CLK_CD_CTRL, 9, 0x3, t7_pwm_parents);
static T7_COMP_DIV(pwm_c, PWM_CLK_CD_CTRL, 0, 8);
static T7_COMP_GATE(pwm_c, PWM_CLK_CD_CTRL, 8, 0);

static T7_COMP_SEL(pwm_d, PWM_CLK_CD_CTRL, 25, 0x3, t7_pwm_parents);
static T7_COMP_DIV(pwm_d, PWM_CLK_CD_CTRL, 16, 8);
static T7_COMP_GATE(pwm_d, PWM_CLK_CD_CTRL, 24, 0);

static T7_COMP_SEL(pwm_e, PWM_CLK_EF_CTRL, 9, 0x3, t7_pwm_parents);
static T7_COMP_DIV(pwm_e, PWM_CLK_EF_CTRL, 0, 8);
static T7_COMP_GATE(pwm_e, PWM_CLK_EF_CTRL, 8, 0);

static T7_COMP_SEL(pwm_f, PWM_CLK_EF_CTRL, 25, 0x3, t7_pwm_parents);
static T7_COMP_DIV(pwm_f, PWM_CLK_EF_CTRL, 16, 8);
static T7_COMP_GATE(pwm_f, PWM_CLK_EF_CTRL, 24, 0);

static T7_COMP_SEL(pwm_ao_a, PWM_CLK_AO_AB_CTRL, 9, 0x3, t7_pwm_parents);
static T7_COMP_DIV(pwm_ao_a, PWM_CLK_AO_AB_CTRL, 0, 8);
static T7_COMP_GATE(pwm_ao_a, PWM_CLK_AO_AB_CTRL, 8, 0);

static T7_COMP_SEL(pwm_ao_b, PWM_CLK_AO_AB_CTRL, 25, 0x3, t7_pwm_parents);
static T7_COMP_DIV(pwm_ao_b, PWM_CLK_AO_AB_CTRL, 16, 8);
static T7_COMP_GATE(pwm_ao_b, PWM_CLK_AO_AB_CTRL, 24, 0);

static T7_COMP_SEL(pwm_ao_c, PWM_CLK_AO_CD_CTRL, 9, 0x3, t7_pwm_parents);
static T7_COMP_DIV(pwm_ao_c, PWM_CLK_AO_CD_CTRL, 0, 8);
static T7_COMP_GATE(pwm_ao_c, PWM_CLK_AO_CD_CTRL, 8, 0);

static T7_COMP_SEL(pwm_ao_d, PWM_CLK_AO_CD_CTRL, 25, 0x3, t7_pwm_parents);
static T7_COMP_DIV(pwm_ao_d, PWM_CLK_AO_CD_CTRL, 16, 8);
static T7_COMP_GATE(pwm_ao_d, PWM_CLK_AO_CD_CTRL, 24, 0);

static T7_COMP_SEL(pwm_ao_e, PWM_CLK_AO_EF_CTRL, 9, 0x3, t7_pwm_parents);
static T7_COMP_DIV(pwm_ao_e, PWM_CLK_AO_EF_CTRL, 0, 8);
static T7_COMP_GATE(pwm_ao_e, PWM_CLK_AO_EF_CTRL, 8, 0);

static T7_COMP_SEL(pwm_ao_f, PWM_CLK_AO_EF_CTRL, 25, 0x3, t7_pwm_parents);
static T7_COMP_DIV(pwm_ao_f, PWM_CLK_AO_EF_CTRL, 16, 8);
static T7_COMP_GATE(pwm_ao_f, PWM_CLK_AO_EF_CTRL, 24, 0);

static T7_COMP_SEL(pwm_ao_g, PWM_CLK_AO_GH_CTRL, 9, 0x3, t7_pwm_parents);
static T7_COMP_DIV(pwm_ao_g, PWM_CLK_AO_GH_CTRL, 0, 8);
static T7_COMP_GATE(pwm_ao_g, PWM_CLK_AO_GH_CTRL, 8, 0);

static T7_COMP_SEL(pwm_ao_h, PWM_CLK_AO_GH_CTRL, 25, 0x3, t7_pwm_parents);
static T7_COMP_DIV(pwm_ao_h, PWM_CLK_AO_GH_CTRL, 16, 8);
static T7_COMP_GATE(pwm_ao_h, PWM_CLK_AO_GH_CTRL, 24, 0);

static const struct clk_parent_data t7_sys_pclk_parents = { .fw_name = "sys" };

#define T7_SYS_PCLK(_name, _reg, _bit, _flags) \
	MESON_PCLK(t7_##_name, _reg, _bit, &t7_sys_pclk_parents, _flags)

static T7_SYS_PCLK(sys_ddr,		SYS_CLK_EN0_REG0, 0,	0);
static T7_SYS_PCLK(sys_dos,		SYS_CLK_EN0_REG0, 1,	0);
static T7_SYS_PCLK(sys_mipi_dsi_a,	SYS_CLK_EN0_REG0, 2,	0);
static T7_SYS_PCLK(sys_mipi_dsi_b,	SYS_CLK_EN0_REG0, 3,	0);
static T7_SYS_PCLK(sys_ethphy,		SYS_CLK_EN0_REG0, 4,	0);
static T7_SYS_PCLK(sys_mali,		SYS_CLK_EN0_REG0, 6,	0);
static T7_SYS_PCLK(sys_aocpu,		SYS_CLK_EN0_REG0, 13,	0);
static T7_SYS_PCLK(sys_aucpu,		SYS_CLK_EN0_REG0, 14,	0);
static T7_SYS_PCLK(sys_cec,		SYS_CLK_EN0_REG0, 16,	0);
static T7_SYS_PCLK(sys_gdc,		SYS_CLK_EN0_REG0, 17,	0);
static T7_SYS_PCLK(sys_deswarp,		SYS_CLK_EN0_REG0, 18,	0);
static T7_SYS_PCLK(sys_ampipe_nand,	SYS_CLK_EN0_REG0, 19,	0);
static T7_SYS_PCLK(sys_ampipe_eth,	SYS_CLK_EN0_REG0, 20,	0);
static T7_SYS_PCLK(sys_am2axi0,		SYS_CLK_EN0_REG0, 21,	0);
static T7_SYS_PCLK(sys_am2axi1,		SYS_CLK_EN0_REG0, 22,	0);
static T7_SYS_PCLK(sys_am2axi2,		SYS_CLK_EN0_REG0, 23,	0);
static T7_SYS_PCLK(sys_sd_emmc_a,	SYS_CLK_EN0_REG0, 24,	0);
static T7_SYS_PCLK(sys_sd_emmc_b,	SYS_CLK_EN0_REG0, 25,	0);
static T7_SYS_PCLK(sys_sd_emmc_c,	SYS_CLK_EN0_REG0, 26,	0);
static T7_SYS_PCLK(sys_smartcard,	SYS_CLK_EN0_REG0, 27,	0);
static T7_SYS_PCLK(sys_acodec,		SYS_CLK_EN0_REG0, 28,	0);
static T7_SYS_PCLK(sys_spifc,		SYS_CLK_EN0_REG0, 29,	0);
static T7_SYS_PCLK(sys_msr_clk,		SYS_CLK_EN0_REG0, 30,	0);
static T7_SYS_PCLK(sys_ir_ctrl,		SYS_CLK_EN0_REG0, 31,	0);
static T7_SYS_PCLK(sys_audio,		SYS_CLK_EN0_REG1, 0,	0);
static T7_SYS_PCLK(sys_eth,		SYS_CLK_EN0_REG1, 3,	0);
static T7_SYS_PCLK(sys_uart_a,		SYS_CLK_EN0_REG1, 5,	0);
static T7_SYS_PCLK(sys_uart_b,		SYS_CLK_EN0_REG1, 6,	0);
static T7_SYS_PCLK(sys_uart_c,		SYS_CLK_EN0_REG1, 7,	0);
static T7_SYS_PCLK(sys_uart_d,		SYS_CLK_EN0_REG1, 8,	0);
static T7_SYS_PCLK(sys_uart_e,		SYS_CLK_EN0_REG1, 9,	0);
static T7_SYS_PCLK(sys_uart_f,		SYS_CLK_EN0_REG1, 10,	0);
static T7_SYS_PCLK(sys_aififo,		SYS_CLK_EN0_REG1, 11,	0);
static T7_SYS_PCLK(sys_spicc2,		SYS_CLK_EN0_REG1, 12,	0);
static T7_SYS_PCLK(sys_spicc3,		SYS_CLK_EN0_REG1, 13,	0);
static T7_SYS_PCLK(sys_spicc4,		SYS_CLK_EN0_REG1, 14,	0);
static T7_SYS_PCLK(sys_ts_a73,		SYS_CLK_EN0_REG1, 15,	0);
static T7_SYS_PCLK(sys_ts_a53,		SYS_CLK_EN0_REG1, 16,	0);
static T7_SYS_PCLK(sys_spicc5,		SYS_CLK_EN0_REG1, 17,	0);
static T7_SYS_PCLK(sys_g2d,		SYS_CLK_EN0_REG1, 20,	0);
static T7_SYS_PCLK(sys_spicc0,		SYS_CLK_EN0_REG1, 21,	0);
static T7_SYS_PCLK(sys_spicc1,		SYS_CLK_EN0_REG1, 22,	0);
static T7_SYS_PCLK(sys_pcie,		SYS_CLK_EN0_REG1, 24,	0);
static T7_SYS_PCLK(sys_usb,		SYS_CLK_EN0_REG1, 26,	0);
static T7_SYS_PCLK(sys_pcie_phy,	SYS_CLK_EN0_REG1, 27,	0);
static T7_SYS_PCLK(sys_i2c_ao_a,	SYS_CLK_EN0_REG1, 28,	0);
static T7_SYS_PCLK(sys_i2c_ao_b,	SYS_CLK_EN0_REG1, 29,	0);
static T7_SYS_PCLK(sys_i2c_m_a,		SYS_CLK_EN0_REG1, 30,	0);
static T7_SYS_PCLK(sys_i2c_m_b,		SYS_CLK_EN0_REG1, 31,	0);
static T7_SYS_PCLK(sys_i2c_m_c,		SYS_CLK_EN0_REG2, 0,	0);
static T7_SYS_PCLK(sys_i2c_m_d,		SYS_CLK_EN0_REG2, 1,	0);
static T7_SYS_PCLK(sys_i2c_m_e,		SYS_CLK_EN0_REG2, 2,	0);
static T7_SYS_PCLK(sys_i2c_m_f,		SYS_CLK_EN0_REG2, 3,	0);
static T7_SYS_PCLK(sys_hdmitx_apb,	SYS_CLK_EN0_REG2, 4,	0);
static T7_SYS_PCLK(sys_i2c_s_a,		SYS_CLK_EN0_REG2, 5,	0);
static T7_SYS_PCLK(sys_hdmirx_pclk,	SYS_CLK_EN0_REG2, 8,	0);
static T7_SYS_PCLK(sys_mmc_apb,		SYS_CLK_EN0_REG2, 11,	0);
static T7_SYS_PCLK(sys_mipi_isp_pclk,	SYS_CLK_EN0_REG2, 17,	0);
static T7_SYS_PCLK(sys_rsa,		SYS_CLK_EN0_REG2, 18,	0);
static T7_SYS_PCLK(sys_pclk_sys_apb,	SYS_CLK_EN0_REG2, 19,	0);
static T7_SYS_PCLK(sys_a73pclk_apb,	SYS_CLK_EN0_REG2, 20,	0);
static T7_SYS_PCLK(sys_dspa,		SYS_CLK_EN0_REG2, 21,	0);
static T7_SYS_PCLK(sys_dspb,		SYS_CLK_EN0_REG2, 22,	0);
static T7_SYS_PCLK(sys_vpu_intr,	SYS_CLK_EN0_REG2, 25,	0);
static T7_SYS_PCLK(sys_sar_adc,		SYS_CLK_EN0_REG2, 28,	0);
/*
 * sys_gic provides the clock for GIC(Generic Interrupt Controller).
 * After clock is disabled, The GIC cannot work properly. At present, the driver
 * used by our GIC is the public driver in kernel, and there is no management
 * clock in the driver.
 */
static T7_SYS_PCLK(sys_gic,		SYS_CLK_EN0_REG2, 30,	CLK_IS_CRITICAL);
static T7_SYS_PCLK(sys_ts_gpu,		SYS_CLK_EN0_REG2, 31,	0);
static T7_SYS_PCLK(sys_ts_nna,		SYS_CLK_EN0_REG3, 0,	0);
static T7_SYS_PCLK(sys_ts_vpu,		SYS_CLK_EN0_REG3, 1,	0);
static T7_SYS_PCLK(sys_ts_hevc,		SYS_CLK_EN0_REG3, 2,	0);
static T7_SYS_PCLK(sys_pwm_ao_ab,	SYS_CLK_EN0_REG3, 3,	0);
static T7_SYS_PCLK(sys_pwm_ao_cd,	SYS_CLK_EN0_REG3, 4,	0);
static T7_SYS_PCLK(sys_pwm_ao_ef,	SYS_CLK_EN0_REG3, 5,	0);
static T7_SYS_PCLK(sys_pwm_ao_gh,	SYS_CLK_EN0_REG3, 6,	0);
static T7_SYS_PCLK(sys_pwm_ab,		SYS_CLK_EN0_REG3, 7,	0);
static T7_SYS_PCLK(sys_pwm_cd,		SYS_CLK_EN0_REG3, 8,	0);
static T7_SYS_PCLK(sys_pwm_ef,		SYS_CLK_EN0_REG3, 9,	0);

/* Array of all clocks registered by this provider */
static struct clk_hw *t7_peripherals_hw_clks[] = {
	[CLKID_RTC_DUALDIV_IN]		= &t7_rtc_dualdiv_in.hw,
	[CLKID_RTC_DUALDIV_DIV]		= &t7_rtc_dualdiv_div.hw,
	[CLKID_RTC_DUALDIV_SEL]		= &t7_rtc_dualdiv_sel.hw,
	[CLKID_RTC_DUALDIV]		= &t7_rtc_dualdiv.hw,
	[CLKID_RTC]			= &t7_rtc.hw,
	[CLKID_CECA_DUALDIV_IN]		= &t7_ceca_dualdiv_in.hw,
	[CLKID_CECA_DUALDIV_DIV]	= &t7_ceca_dualdiv_div.hw,
	[CLKID_CECA_DUALDIV_SEL]	= &t7_ceca_dualdiv_sel.hw,
	[CLKID_CECA_DUALDIV]		= &t7_ceca_dualdiv.hw,
	[CLKID_CECA]			= &t7_ceca.hw,
	[CLKID_CECB_DUALDIV_IN]		= &t7_cecb_dualdiv_in.hw,
	[CLKID_CECB_DUALDIV_DIV]	= &t7_cecb_dualdiv_div.hw,
	[CLKID_CECB_DUALDIV_SEL]	= &t7_cecb_dualdiv_sel.hw,
	[CLKID_CECB_DUALDIV]		= &t7_cecb_dualdiv.hw,
	[CLKID_CECB]			= &t7_cecb.hw,
	[CLKID_SC_SEL]			= &t7_sc_sel.hw,
	[CLKID_SC_DIV]			= &t7_sc_div.hw,
	[CLKID_SC]			= &t7_sc.hw,
	[CLKID_DSPA_0_SEL]		= &t7_dspa_0_sel.hw,
	[CLKID_DSPA_0_DIV]		= &t7_dspa_0_div.hw,
	[CLKID_DSPA_0]			= &t7_dspa_0.hw,
	[CLKID_DSPA_1_SEL]		= &t7_dspa_1_sel.hw,
	[CLKID_DSPA_1_DIV]		= &t7_dspa_1_div.hw,
	[CLKID_DSPA_1]			= &t7_dspa_1.hw,
	[CLKID_DSPA]			= &t7_dspa.hw,
	[CLKID_DSPB_0_SEL]		= &t7_dspb_0_sel.hw,
	[CLKID_DSPB_0_DIV]		= &t7_dspb_0_div.hw,
	[CLKID_DSPB_0]			= &t7_dspb_0.hw,
	[CLKID_DSPB_1_SEL]		= &t7_dspb_1_sel.hw,
	[CLKID_DSPB_1_DIV]		= &t7_dspb_1_div.hw,
	[CLKID_DSPB_1]			= &t7_dspb_1.hw,
	[CLKID_DSPB]			= &t7_dspb.hw,
	[CLKID_24M]			= &t7_24m.hw,
	[CLKID_24M_DIV2]		= &t7_24m_div2.hw,
	[CLKID_12M]			= &t7_12m.hw,
	[CLKID_25M_DIV]			= &t7_25m_div.hw,
	[CLKID_25M]			= &t7_25m.hw,
	[CLKID_ANAKIN_0_SEL]		= &t7_anakin_0_sel.hw,
	[CLKID_ANAKIN_0_DIV]		= &t7_anakin_0_div.hw,
	[CLKID_ANAKIN_0]		= &t7_anakin_0.hw,
	[CLKID_ANAKIN_1_SEL]		= &t7_anakin_1_sel.hw,
	[CLKID_ANAKIN_1_DIV]		= &t7_anakin_1_div.hw,
	[CLKID_ANAKIN_1]		= &t7_anakin_1.hw,
	[CLKID_ANAKIN_01_SEL]		= &t7_anakin_01_sel.hw,
	[CLKID_ANAKIN]			= &t7_anakin.hw,
	[CLKID_MIPI_CSI_PHY_0_SEL]	= &t7_mipi_csi_phy_0_sel.hw,
	[CLKID_MIPI_CSI_PHY_0_DIV]	= &t7_mipi_csi_phy_0_div.hw,
	[CLKID_MIPI_CSI_PHY_0]		= &t7_mipi_csi_phy_0.hw,
	[CLKID_MIPI_CSI_PHY_1_SEL]	= &t7_mipi_csi_phy_1_sel.hw,
	[CLKID_MIPI_CSI_PHY_1_DIV]	= &t7_mipi_csi_phy_1_div.hw,
	[CLKID_MIPI_CSI_PHY_1]		= &t7_mipi_csi_phy_1.hw,
	[CLKID_MIPI_CSI_PHY]		= &t7_mipi_csi_phy.hw,
	[CLKID_MIPI_ISP_SEL]		= &t7_mipi_isp_sel.hw,
	[CLKID_MIPI_ISP_DIV]		= &t7_mipi_isp_div.hw,
	[CLKID_MIPI_ISP]		= &t7_mipi_isp.hw,
	[CLKID_TS_DIV]			= &t7_ts_div.hw,
	[CLKID_TS]			= &t7_ts.hw,
	[CLKID_MALI_0_SEL]		= &t7_mali_0_sel.hw,
	[CLKID_MALI_0_DIV]		= &t7_mali_0_div.hw,
	[CLKID_MALI_0]			= &t7_mali_0.hw,
	[CLKID_MALI_1_SEL]		= &t7_mali_1_sel.hw,
	[CLKID_MALI_1_DIV]		= &t7_mali_1_div.hw,
	[CLKID_MALI_1]			= &t7_mali_1.hw,
	[CLKID_MALI]			= &t7_mali.hw,
	[CLKID_ETH_RMII_SEL]		= &t7_eth_rmii_sel.hw,
	[CLKID_ETH_RMII_DIV]		= &t7_eth_rmii_div.hw,
	[CLKID_ETH_RMII]		= &t7_eth_rmii.hw,
	[CLKID_FCLK_DIV2_DIV8]		= &t7_fdiv2_div8.hw,
	[CLKID_ETH_125M]		= &t7_eth_125m.hw,
	[CLKID_SD_EMMC_A_SEL]		= &t7_sd_emmc_a_sel.hw,
	[CLKID_SD_EMMC_A_DIV]		= &t7_sd_emmc_a_div.hw,
	[CLKID_SD_EMMC_A]		= &t7_sd_emmc_a.hw,
	[CLKID_SD_EMMC_B_SEL]		= &t7_sd_emmc_b_sel.hw,
	[CLKID_SD_EMMC_B_DIV]		= &t7_sd_emmc_b_div.hw,
	[CLKID_SD_EMMC_B]		= &t7_sd_emmc_b.hw,
	[CLKID_SD_EMMC_C_SEL]		= &t7_sd_emmc_c_sel.hw,
	[CLKID_SD_EMMC_C_DIV]		= &t7_sd_emmc_c_div.hw,
	[CLKID_SD_EMMC_C]		= &t7_sd_emmc_c.hw,
	[CLKID_SPICC0_SEL]		= &t7_spicc0_sel.hw,
	[CLKID_SPICC0_DIV]		= &t7_spicc0_div.hw,
	[CLKID_SPICC0]			= &t7_spicc0.hw,
	[CLKID_SPICC1_SEL]		= &t7_spicc1_sel.hw,
	[CLKID_SPICC1_DIV]		= &t7_spicc1_div.hw,
	[CLKID_SPICC1]			= &t7_spicc1.hw,
	[CLKID_SPICC2_SEL]		= &t7_spicc2_sel.hw,
	[CLKID_SPICC2_DIV]		= &t7_spicc2_div.hw,
	[CLKID_SPICC2]			= &t7_spicc2.hw,
	[CLKID_SPICC3_SEL]		= &t7_spicc3_sel.hw,
	[CLKID_SPICC3_DIV]		= &t7_spicc3_div.hw,
	[CLKID_SPICC3]			= &t7_spicc3.hw,
	[CLKID_SPICC4_SEL]		= &t7_spicc4_sel.hw,
	[CLKID_SPICC4_DIV]		= &t7_spicc4_div.hw,
	[CLKID_SPICC4]			= &t7_spicc4.hw,
	[CLKID_SPICC5_SEL]		= &t7_spicc5_sel.hw,
	[CLKID_SPICC5_DIV]		= &t7_spicc5_div.hw,
	[CLKID_SPICC5]			= &t7_spicc5.hw,
	[CLKID_SARADC_SEL]		= &t7_saradc_sel.hw,
	[CLKID_SARADC_DIV]		= &t7_saradc_div.hw,
	[CLKID_SARADC]			= &t7_saradc.hw,
	[CLKID_PWM_A_SEL]		= &t7_pwm_a_sel.hw,
	[CLKID_PWM_A_DIV]		= &t7_pwm_a_div.hw,
	[CLKID_PWM_A]			= &t7_pwm_a.hw,
	[CLKID_PWM_B_SEL]		= &t7_pwm_b_sel.hw,
	[CLKID_PWM_B_DIV]		= &t7_pwm_b_div.hw,
	[CLKID_PWM_B]			= &t7_pwm_b.hw,
	[CLKID_PWM_C_SEL]		= &t7_pwm_c_sel.hw,
	[CLKID_PWM_C_DIV]		= &t7_pwm_c_div.hw,
	[CLKID_PWM_C]			= &t7_pwm_c.hw,
	[CLKID_PWM_D_SEL]		= &t7_pwm_d_sel.hw,
	[CLKID_PWM_D_DIV]		= &t7_pwm_d_div.hw,
	[CLKID_PWM_D]			= &t7_pwm_d.hw,
	[CLKID_PWM_E_SEL]		= &t7_pwm_e_sel.hw,
	[CLKID_PWM_E_DIV]		= &t7_pwm_e_div.hw,
	[CLKID_PWM_E]			= &t7_pwm_e.hw,
	[CLKID_PWM_F_SEL]		= &t7_pwm_f_sel.hw,
	[CLKID_PWM_F_DIV]		= &t7_pwm_f_div.hw,
	[CLKID_PWM_F]			= &t7_pwm_f.hw,
	[CLKID_PWM_AO_A_SEL]		= &t7_pwm_ao_a_sel.hw,
	[CLKID_PWM_AO_A_DIV]		= &t7_pwm_ao_a_div.hw,
	[CLKID_PWM_AO_A]		= &t7_pwm_ao_a.hw,
	[CLKID_PWM_AO_B_SEL]		= &t7_pwm_ao_b_sel.hw,
	[CLKID_PWM_AO_B_DIV]		= &t7_pwm_ao_b_div.hw,
	[CLKID_PWM_AO_B]		= &t7_pwm_ao_b.hw,
	[CLKID_PWM_AO_C_SEL]		= &t7_pwm_ao_c_sel.hw,
	[CLKID_PWM_AO_C_DIV]		= &t7_pwm_ao_c_div.hw,
	[CLKID_PWM_AO_C]		= &t7_pwm_ao_c.hw,
	[CLKID_PWM_AO_D_SEL]		= &t7_pwm_ao_d_sel.hw,
	[CLKID_PWM_AO_D_DIV]		= &t7_pwm_ao_d_div.hw,
	[CLKID_PWM_AO_D]		= &t7_pwm_ao_d.hw,
	[CLKID_PWM_AO_E_SEL]		= &t7_pwm_ao_e_sel.hw,
	[CLKID_PWM_AO_E_DIV]		= &t7_pwm_ao_e_div.hw,
	[CLKID_PWM_AO_E]		= &t7_pwm_ao_e.hw,
	[CLKID_PWM_AO_F_SEL]		= &t7_pwm_ao_f_sel.hw,
	[CLKID_PWM_AO_F_DIV]		= &t7_pwm_ao_f_div.hw,
	[CLKID_PWM_AO_F]		= &t7_pwm_ao_f.hw,
	[CLKID_PWM_AO_G_SEL]		= &t7_pwm_ao_g_sel.hw,
	[CLKID_PWM_AO_G_DIV]		= &t7_pwm_ao_g_div.hw,
	[CLKID_PWM_AO_G]		= &t7_pwm_ao_g.hw,
	[CLKID_PWM_AO_H_SEL]		= &t7_pwm_ao_h_sel.hw,
	[CLKID_PWM_AO_H_DIV]		= &t7_pwm_ao_h_div.hw,
	[CLKID_PWM_AO_H]		= &t7_pwm_ao_h.hw,
	[CLKID_SYS_DDR]			= &t7_sys_ddr.hw,
	[CLKID_SYS_DOS]			= &t7_sys_dos.hw,
	[CLKID_SYS_MIPI_DSI_A]		= &t7_sys_mipi_dsi_a.hw,
	[CLKID_SYS_MIPI_DSI_B]		= &t7_sys_mipi_dsi_b.hw,
	[CLKID_SYS_ETHPHY]		= &t7_sys_ethphy.hw,
	[CLKID_SYS_MALI]		= &t7_sys_mali.hw,
	[CLKID_SYS_AOCPU]		= &t7_sys_aocpu.hw,
	[CLKID_SYS_AUCPU]		= &t7_sys_aucpu.hw,
	[CLKID_SYS_CEC]			= &t7_sys_cec.hw,
	[CLKID_SYS_GDC]			= &t7_sys_gdc.hw,
	[CLKID_SYS_DESWARP]		= &t7_sys_deswarp.hw,
	[CLKID_SYS_AMPIPE_NAND]		= &t7_sys_ampipe_nand.hw,
	[CLKID_SYS_AMPIPE_ETH]		= &t7_sys_ampipe_eth.hw,
	[CLKID_SYS_AM2AXI0]		= &t7_sys_am2axi0.hw,
	[CLKID_SYS_AM2AXI1]		= &t7_sys_am2axi1.hw,
	[CLKID_SYS_AM2AXI2]		= &t7_sys_am2axi2.hw,
	[CLKID_SYS_SD_EMMC_A]		= &t7_sys_sd_emmc_a.hw,
	[CLKID_SYS_SD_EMMC_B]		= &t7_sys_sd_emmc_b.hw,
	[CLKID_SYS_SD_EMMC_C]		= &t7_sys_sd_emmc_c.hw,
	[CLKID_SYS_SMARTCARD]		= &t7_sys_smartcard.hw,
	[CLKID_SYS_ACODEC]		= &t7_sys_acodec.hw,
	[CLKID_SYS_SPIFC]		= &t7_sys_spifc.hw,
	[CLKID_SYS_MSR_CLK]		= &t7_sys_msr_clk.hw,
	[CLKID_SYS_IR_CTRL]		= &t7_sys_ir_ctrl.hw,
	[CLKID_SYS_AUDIO]		= &t7_sys_audio.hw,
	[CLKID_SYS_ETH]			= &t7_sys_eth.hw,
	[CLKID_SYS_UART_A]		= &t7_sys_uart_a.hw,
	[CLKID_SYS_UART_B]		= &t7_sys_uart_b.hw,
	[CLKID_SYS_UART_C]		= &t7_sys_uart_c.hw,
	[CLKID_SYS_UART_D]		= &t7_sys_uart_d.hw,
	[CLKID_SYS_UART_E]		= &t7_sys_uart_e.hw,
	[CLKID_SYS_UART_F]		= &t7_sys_uart_f.hw,
	[CLKID_SYS_AIFIFO]		= &t7_sys_aififo.hw,
	[CLKID_SYS_SPICC2]		= &t7_sys_spicc2.hw,
	[CLKID_SYS_SPICC3]		= &t7_sys_spicc3.hw,
	[CLKID_SYS_SPICC4]		= &t7_sys_spicc4.hw,
	[CLKID_SYS_TS_A73]		= &t7_sys_ts_a73.hw,
	[CLKID_SYS_TS_A53]		= &t7_sys_ts_a53.hw,
	[CLKID_SYS_SPICC5]		= &t7_sys_spicc5.hw,
	[CLKID_SYS_G2D]			= &t7_sys_g2d.hw,
	[CLKID_SYS_SPICC0]		= &t7_sys_spicc0.hw,
	[CLKID_SYS_SPICC1]		= &t7_sys_spicc1.hw,
	[CLKID_SYS_PCIE]		= &t7_sys_pcie.hw,
	[CLKID_SYS_USB]			= &t7_sys_usb.hw,
	[CLKID_SYS_PCIE_PHY]		= &t7_sys_pcie_phy.hw,
	[CLKID_SYS_I2C_AO_A]		= &t7_sys_i2c_ao_a.hw,
	[CLKID_SYS_I2C_AO_B]		= &t7_sys_i2c_ao_b.hw,
	[CLKID_SYS_I2C_M_A]		= &t7_sys_i2c_m_a.hw,
	[CLKID_SYS_I2C_M_B]		= &t7_sys_i2c_m_b.hw,
	[CLKID_SYS_I2C_M_C]		= &t7_sys_i2c_m_c.hw,
	[CLKID_SYS_I2C_M_D]		= &t7_sys_i2c_m_d.hw,
	[CLKID_SYS_I2C_M_E]		= &t7_sys_i2c_m_e.hw,
	[CLKID_SYS_I2C_M_F]		= &t7_sys_i2c_m_f.hw,
	[CLKID_SYS_HDMITX_APB]		= &t7_sys_hdmitx_apb.hw,
	[CLKID_SYS_I2C_S_A]		= &t7_sys_i2c_s_a.hw,
	[CLKID_SYS_HDMIRX_PCLK]		= &t7_sys_hdmirx_pclk.hw,
	[CLKID_SYS_MMC_APB]		= &t7_sys_mmc_apb.hw,
	[CLKID_SYS_MIPI_ISP_PCLK]	= &t7_sys_mipi_isp_pclk.hw,
	[CLKID_SYS_RSA]			= &t7_sys_rsa.hw,
	[CLKID_SYS_PCLK_SYS_APB]	= &t7_sys_pclk_sys_apb.hw,
	[CLKID_SYS_A73PCLK_APB]		= &t7_sys_a73pclk_apb.hw,
	[CLKID_SYS_DSPA]		= &t7_sys_dspa.hw,
	[CLKID_SYS_DSPB]		= &t7_sys_dspb.hw,
	[CLKID_SYS_VPU_INTR]		= &t7_sys_vpu_intr.hw,
	[CLKID_SYS_SAR_ADC]		= &t7_sys_sar_adc.hw,
	[CLKID_SYS_GIC]			= &t7_sys_gic.hw,
	[CLKID_SYS_TS_GPU]		= &t7_sys_ts_gpu.hw,
	[CLKID_SYS_TS_NNA]		= &t7_sys_ts_nna.hw,
	[CLKID_SYS_TS_VPU]		= &t7_sys_ts_vpu.hw,
	[CLKID_SYS_TS_HEVC]		= &t7_sys_ts_hevc.hw,
	[CLKID_SYS_PWM_AO_AB]		= &t7_sys_pwm_ao_ab.hw,
	[CLKID_SYS_PWM_AO_CD]		= &t7_sys_pwm_ao_cd.hw,
	[CLKID_SYS_PWM_AO_EF]		= &t7_sys_pwm_ao_ef.hw,
	[CLKID_SYS_PWM_AO_GH]		= &t7_sys_pwm_ao_gh.hw,
	[CLKID_SYS_PWM_AB]		= &t7_sys_pwm_ab.hw,
	[CLKID_SYS_PWM_CD]		= &t7_sys_pwm_cd.hw,
	[CLKID_SYS_PWM_EF]		= &t7_sys_pwm_ef.hw,
};

static const struct meson_clkc_data t7_peripherals_data = {
	.hw_clks = {
		.hws = t7_peripherals_hw_clks,
		.num = ARRAY_SIZE(t7_peripherals_hw_clks),
	},
};

static const struct of_device_id t7_peripherals_clkc_match_table[] = {
	{
		.compatible = "amlogic,t7-peripherals-clkc",
		.data = &t7_peripherals_data
	},
	{}
};
MODULE_DEVICE_TABLE(of, t7_peripherals_clkc_match_table);

static struct platform_driver t7_peripherals_clkc_driver = {
	.probe = meson_clkc_mmio_probe,
	.driver = {
		.name = "t7-peripherals-clkc",
		.of_match_table = t7_peripherals_clkc_match_table,
	},
};
module_platform_driver(t7_peripherals_clkc_driver);

MODULE_DESCRIPTION("Amlogic T7 Peripherals Clock Controller driver");
MODULE_AUTHOR("Jian Hu <jian.hu@amlogic.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("CLK_MESON");
