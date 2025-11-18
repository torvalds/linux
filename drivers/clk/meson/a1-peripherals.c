// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 * Author: Jian Hu <jian.hu@amlogic.com>
 *
 * Copyright (c) 2023, SberDevices. All Rights Reserved.
 * Author: Dmitry Rokosov <ddrokosov@sberdevices.ru>
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include "clk-dualdiv.h"
#include "clk-regmap.h"
#include "meson-clkc-utils.h"

#include <dt-bindings/clock/amlogic,a1-peripherals-clkc.h>

#define SYS_OSCIN_CTRL		0x0
#define RTC_BY_OSCIN_CTRL0	0x4
#define RTC_BY_OSCIN_CTRL1	0x8
#define RTC_CTRL		0xc
#define SYS_CLK_CTRL0		0x10
#define SYS_CLK_EN0		0x1c
#define SYS_CLK_EN1		0x20
#define AXI_CLK_EN		0x24
#define DSPA_CLK_EN		0x28
#define DSPB_CLK_EN		0x2c
#define DSPA_CLK_CTRL0		0x30
#define DSPB_CLK_CTRL0		0x34
#define CLK12_24_CTRL		0x38
#define GEN_CLK_CTRL		0x3c
#define SAR_ADC_CLK_CTRL	0xc0
#define PWM_CLK_AB_CTRL		0xc4
#define PWM_CLK_CD_CTRL		0xc8
#define PWM_CLK_EF_CTRL		0xcc
#define SPICC_CLK_CTRL		0xd0
#define TS_CLK_CTRL		0xd4
#define SPIFC_CLK_CTRL		0xd8
#define USB_BUSCLK_CTRL		0xdc
#define SD_EMMC_CLK_CTRL	0xe0
#define CECA_CLK_CTRL0		0xe4
#define CECA_CLK_CTRL1		0xe8
#define CECB_CLK_CTRL0		0xec
#define CECB_CLK_CTRL1		0xf0
#define PSRAM_CLK_CTRL		0xf4
#define DMC_CLK_CTRL		0xf8

static struct clk_regmap a1_xtal_in = {
	.data = &(struct clk_regmap_gate_data){
		.offset = SYS_OSCIN_CTRL,
		.bit_idx = 0,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "xtal_in",
		.ops = &clk_regmap_gate_ro_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a1_fixpll_in = {
	.data = &(struct clk_regmap_gate_data){
		.offset = SYS_OSCIN_CTRL,
		.bit_idx = 1,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "fixpll_in",
		.ops = &clk_regmap_gate_ro_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a1_usb_phy_in = {
	.data = &(struct clk_regmap_gate_data){
		.offset = SYS_OSCIN_CTRL,
		.bit_idx = 2,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "usb_phy_in",
		.ops = &clk_regmap_gate_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a1_usb_ctrl_in = {
	.data = &(struct clk_regmap_gate_data){
		.offset = SYS_OSCIN_CTRL,
		.bit_idx = 3,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "usb_ctrl_in",
		.ops = &clk_regmap_gate_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a1_hifipll_in = {
	.data = &(struct clk_regmap_gate_data){
		.offset = SYS_OSCIN_CTRL,
		.bit_idx = 4,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "hifipll_in",
		.ops = &clk_regmap_gate_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a1_syspll_in = {
	.data = &(struct clk_regmap_gate_data){
		.offset = SYS_OSCIN_CTRL,
		.bit_idx = 5,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "syspll_in",
		.ops = &clk_regmap_gate_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a1_dds_in = {
	.data = &(struct clk_regmap_gate_data){
		.offset = SYS_OSCIN_CTRL,
		.bit_idx = 6,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "dds_in",
		.ops = &clk_regmap_gate_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a1_rtc_32k_in = {
	.data = &(struct clk_regmap_gate_data){
		.offset = RTC_BY_OSCIN_CTRL0,
		.bit_idx = 31,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "rtc_32k_in",
		.ops = &clk_regmap_gate_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static const struct meson_clk_dualdiv_param a1_32k_div_table[] = {
	{
		.dual		= 1,
		.n1		= 733,
		.m1		= 8,
		.n2		= 732,
		.m2		= 11,
	},
	{}
};

static struct clk_regmap a1_rtc_32k_div = {
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
		.table = a1_32k_div_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "rtc_32k_div",
		.ops = &meson_clk_dualdiv_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_rtc_32k_in.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a1_rtc_32k_xtal = {
	.data = &(struct clk_regmap_gate_data){
		.offset = RTC_BY_OSCIN_CTRL1,
		.bit_idx = 24,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "rtc_32k_xtal",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_rtc_32k_in.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a1_rtc_32k_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = RTC_CTRL,
		.mask = 0x3,
		.shift = 0,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "rtc_32k_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_rtc_32k_xtal.hw,
			&a1_rtc_32k_div.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_rtc = {
	.data = &(struct clk_regmap_gate_data){
		.offset = RTC_BY_OSCIN_CTRL0,
		.bit_idx = 30,
	},
	.hw.init = &(struct clk_init_data){
		.name = "rtc",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_rtc_32k_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static u32 a1_sys_parents_val_table[] = { 0, 1, 2, 3, 7 };
static const struct clk_parent_data a1_sys_parents[] = {
	{ .fw_name = "xtal" },
	{ .fw_name = "fclk_div2" },
	{ .fw_name = "fclk_div3" },
	{ .fw_name = "fclk_div5" },
	{ .hw = &a1_rtc.hw },
};

static struct clk_regmap a1_sys_b_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = SYS_CLK_CTRL0,
		.mask = 0x7,
		.shift = 26,
		.table = a1_sys_parents_val_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sys_b_sel",
		.ops = &clk_regmap_mux_ro_ops,
		.parent_data = a1_sys_parents,
		.num_parents = ARRAY_SIZE(a1_sys_parents),
	},
};

static struct clk_regmap a1_sys_b_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = SYS_CLK_CTRL0,
		.shift = 16,
		.width = 10,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sys_b_div",
		.ops = &clk_regmap_divider_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_sys_b_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_sys_b = {
	.data = &(struct clk_regmap_gate_data){
		.offset = SYS_CLK_CTRL0,
		.bit_idx = 29,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "sys_b",
		.ops = &clk_regmap_gate_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_sys_b_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_sys_a_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = SYS_CLK_CTRL0,
		.mask = 0x7,
		.shift = 10,
		.table = a1_sys_parents_val_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sys_a_sel",
		.ops = &clk_regmap_mux_ro_ops,
		.parent_data = a1_sys_parents,
		.num_parents = ARRAY_SIZE(a1_sys_parents),
	},
};

static struct clk_regmap a1_sys_a_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = SYS_CLK_CTRL0,
		.shift = 0,
		.width = 10,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sys_a_div",
		.ops = &clk_regmap_divider_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_sys_a_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_sys_a = {
	.data = &(struct clk_regmap_gate_data){
		.offset = SYS_CLK_CTRL0,
		.bit_idx = 13,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "sys_a",
		.ops = &clk_regmap_gate_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_sys_a_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_sys = {
	.data = &(struct clk_regmap_mux_data){
		.offset = SYS_CLK_CTRL0,
		.mask = 0x1,
		.shift = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sys",
		.ops = &clk_regmap_mux_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_sys_a.hw,
			&a1_sys_b.hw,
		},
		.num_parents = 2,
		/*
		 * This clock is used by APB bus which is set in boot ROM code
		 * and is required by the platform to operate correctly.
		 * Until the following condition are met, we need this clock to
		 * be marked as critical:
		 * a) Mark the clock used by a firmware resource, if possible
		 * b) CCF has a clock hand-off mechanism to make the sure the
		 *    clock stays on until the proper driver comes along
		 */
		.flags = CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
	},
};

static u32 a1_dsp_parents_val_table[] = { 0, 1, 2, 3, 4, 7 };
static const struct clk_parent_data a1_dsp_parents[] = {
	{ .fw_name = "xtal", },
	{ .fw_name = "fclk_div2", },
	{ .fw_name = "fclk_div3", },
	{ .fw_name = "fclk_div5", },
	{ .fw_name = "hifi_pll", },
	{ .hw = &a1_rtc.hw },
};

static struct clk_regmap a1_dspa_a_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = DSPA_CLK_CTRL0,
		.mask = 0x7,
		.shift = 10,
		.table = a1_dsp_parents_val_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dspa_a_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = a1_dsp_parents,
		.num_parents = ARRAY_SIZE(a1_dsp_parents),
	},
};

static struct clk_regmap a1_dspa_a_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = DSPA_CLK_CTRL0,
		.shift = 0,
		.width = 10,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dspa_a_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_dspa_a_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_dspa_a = {
	.data = &(struct clk_regmap_gate_data){
		.offset = DSPA_CLK_CTRL0,
		.bit_idx = 13,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "dspa_a",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_dspa_a_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_dspa_b_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = DSPA_CLK_CTRL0,
		.mask = 0x7,
		.shift = 26,
		.table = a1_dsp_parents_val_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dspa_b_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = a1_dsp_parents,
		.num_parents = ARRAY_SIZE(a1_dsp_parents),
	},
};

static struct clk_regmap a1_dspa_b_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = DSPA_CLK_CTRL0,
		.shift = 16,
		.width = 10,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dspa_b_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_dspa_b_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_dspa_b = {
	.data = &(struct clk_regmap_gate_data){
		.offset = DSPA_CLK_CTRL0,
		.bit_idx = 29,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "dspa_b",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_dspa_b_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_dspa_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = DSPA_CLK_CTRL0,
		.mask = 0x1,
		.shift = 15,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dspa_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_dspa_a.hw,
			&a1_dspa_b.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_dspa_en = {
	.data = &(struct clk_regmap_gate_data){
		.offset = DSPA_CLK_EN,
		.bit_idx = 1,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "dspa_en",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_dspa_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_dspa_en_nic = {
	.data = &(struct clk_regmap_gate_data){
		.offset = DSPA_CLK_EN,
		.bit_idx = 0,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "dspa_en_nic",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_dspa_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_dspb_a_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = DSPB_CLK_CTRL0,
		.mask = 0x7,
		.shift = 10,
		.table = a1_dsp_parents_val_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dspb_a_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = a1_dsp_parents,
		.num_parents = ARRAY_SIZE(a1_dsp_parents),
	},
};

static struct clk_regmap a1_dspb_a_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = DSPB_CLK_CTRL0,
		.shift = 0,
		.width = 10,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dspb_a_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_dspb_a_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_dspb_a = {
	.data = &(struct clk_regmap_gate_data){
		.offset = DSPB_CLK_CTRL0,
		.bit_idx = 13,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "dspb_a",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_dspb_a_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_dspb_b_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = DSPB_CLK_CTRL0,
		.mask = 0x7,
		.shift = 26,
		.table = a1_dsp_parents_val_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dspb_b_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = a1_dsp_parents,
		.num_parents = ARRAY_SIZE(a1_dsp_parents),
	},
};

static struct clk_regmap a1_dspb_b_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = DSPB_CLK_CTRL0,
		.shift = 16,
		.width = 10,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dspb_b_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_dspb_b_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_dspb_b = {
	.data = &(struct clk_regmap_gate_data){
		.offset = DSPB_CLK_CTRL0,
		.bit_idx = 29,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "dspb_b",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_dspb_b_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_dspb_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = DSPB_CLK_CTRL0,
		.mask = 0x1,
		.shift = 15,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dspb_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_dspb_a.hw,
			&a1_dspb_b.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_dspb_en = {
	.data = &(struct clk_regmap_gate_data){
		.offset = DSPB_CLK_EN,
		.bit_idx = 1,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "dspb_en",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_dspb_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_dspb_en_nic = {
	.data = &(struct clk_regmap_gate_data){
		.offset = DSPB_CLK_EN,
		.bit_idx = 0,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "dspb_en_nic",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_dspb_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_24m = {
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

static struct clk_fixed_factor a1_24m_div2 = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "24m_div2",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_24m.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a1_12m = {
	.data = &(struct clk_regmap_gate_data){
		.offset = CLK12_24_CTRL,
		.bit_idx = 10,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "12m",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_24m_div2.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a1_fclk_div2_divn_pre = {
	.data = &(struct clk_regmap_div_data){
		.offset = CLK12_24_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div2_divn_pre",
		.ops = &clk_regmap_divider_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "fclk_div2",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a1_fclk_div2_divn = {
	.data = &(struct clk_regmap_gate_data){
		.offset = CLK12_24_CTRL,
		.bit_idx = 12,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div2_divn",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_fclk_div2_divn_pre.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

/*
 * the index 2 is sys_pll_div16, it will be implemented in the CPU clock driver,
 * the index 4 is the clock measurement source, it's not supported yet
 */
static u32 a1_gen_parents_val_table[] = { 0, 1, 3, 5, 6, 7, 8 };
static const struct clk_parent_data a1_gen_parents[] = {
	{ .fw_name = "xtal", },
	{ .hw = &a1_rtc.hw },
	{ .fw_name = "hifi_pll", },
	{ .fw_name = "fclk_div2", },
	{ .fw_name = "fclk_div3", },
	{ .fw_name = "fclk_div5", },
	{ .fw_name = "fclk_div7", },
};

static struct clk_regmap a1_gen_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = GEN_CLK_CTRL,
		.mask = 0xf,
		.shift = 12,
		.table = a1_gen_parents_val_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "gen_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = a1_gen_parents,
		.num_parents = ARRAY_SIZE(a1_gen_parents),
		/*
		 * The GEN clock can be connected to an external pad, so it
		 * may be set up directly from the device tree. Additionally,
		 * the GEN clock can be inherited from a more accurate RTC
		 * clock, so in certain situations, it may be necessary
		 * to freeze its parent.
		 */
		.flags = CLK_SET_RATE_NO_REPARENT,
	},
};

static struct clk_regmap a1_gen_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = GEN_CLK_CTRL,
		.shift = 0,
		.width = 11,
	},
	.hw.init = &(struct clk_init_data){
		.name = "gen_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_gen_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_gen = {
	.data = &(struct clk_regmap_gate_data){
		.offset = GEN_CLK_CTRL,
		.bit_idx = 11,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "gen",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_gen_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_saradc_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = SAR_ADC_CLK_CTRL,
		.mask = 0x1,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "saradc_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .fw_name = "xtal", },
			{ .hw = &a1_sys.hw, },
		},
		.num_parents = 2,
	},
};

static struct clk_regmap a1_saradc_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = SAR_ADC_CLK_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "saradc_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_saradc_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_saradc = {
	.data = &(struct clk_regmap_gate_data){
		.offset = SAR_ADC_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "saradc",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_saradc_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data a1_pwm_abcd_parents[] = {
	{ .fw_name = "xtal", },
	{ .hw = &a1_sys.hw },
	{ .hw = &a1_rtc.hw },
};

static struct clk_regmap a1_pwm_a_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = PWM_CLK_AB_CTRL,
		.mask = 0x1,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "pwm_a_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = a1_pwm_abcd_parents,
		.num_parents = ARRAY_SIZE(a1_pwm_abcd_parents),
	},
};

static struct clk_regmap a1_pwm_a_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = PWM_CLK_AB_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "pwm_a_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_pwm_a_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_pwm_a = {
	.data = &(struct clk_regmap_gate_data){
		.offset = PWM_CLK_AB_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "pwm_a",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_pwm_a_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_pwm_b_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = PWM_CLK_AB_CTRL,
		.mask = 0x1,
		.shift = 25,
	},
	.hw.init = &(struct clk_init_data){
		.name = "pwm_b_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = a1_pwm_abcd_parents,
		.num_parents = ARRAY_SIZE(a1_pwm_abcd_parents),
	},
};

static struct clk_regmap a1_pwm_b_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = PWM_CLK_AB_CTRL,
		.shift = 16,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "pwm_b_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_pwm_b_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_pwm_b = {
	.data = &(struct clk_regmap_gate_data){
		.offset = PWM_CLK_AB_CTRL,
		.bit_idx = 24,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "pwm_b",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_pwm_b_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_pwm_c_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = PWM_CLK_CD_CTRL,
		.mask = 0x1,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "pwm_c_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = a1_pwm_abcd_parents,
		.num_parents = ARRAY_SIZE(a1_pwm_abcd_parents),
	},
};

static struct clk_regmap a1_pwm_c_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = PWM_CLK_CD_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "pwm_c_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_pwm_c_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_pwm_c = {
	.data = &(struct clk_regmap_gate_data){
		.offset = PWM_CLK_CD_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "pwm_c",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_pwm_c_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_pwm_d_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = PWM_CLK_CD_CTRL,
		.mask = 0x1,
		.shift = 25,
	},
	.hw.init = &(struct clk_init_data){
		.name = "pwm_d_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = a1_pwm_abcd_parents,
		.num_parents = ARRAY_SIZE(a1_pwm_abcd_parents),
	},
};

static struct clk_regmap a1_pwm_d_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = PWM_CLK_CD_CTRL,
		.shift = 16,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "pwm_d_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_pwm_d_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_pwm_d = {
	.data = &(struct clk_regmap_gate_data){
		.offset = PWM_CLK_CD_CTRL,
		.bit_idx = 24,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "pwm_d",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_pwm_d_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data a1_pwm_ef_parents[] = {
	{ .fw_name = "xtal", },
	{ .hw = &a1_sys.hw },
	{ .fw_name = "fclk_div5", },
	{ .hw = &a1_rtc.hw },
};

static struct clk_regmap a1_pwm_e_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = PWM_CLK_EF_CTRL,
		.mask = 0x3,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "pwm_e_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = a1_pwm_ef_parents,
		.num_parents = ARRAY_SIZE(a1_pwm_ef_parents),
	},
};

static struct clk_regmap a1_pwm_e_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = PWM_CLK_EF_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "pwm_e_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_pwm_e_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_pwm_e = {
	.data = &(struct clk_regmap_gate_data){
		.offset = PWM_CLK_EF_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "pwm_e",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_pwm_e_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_pwm_f_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = PWM_CLK_EF_CTRL,
		.mask = 0x3,
		.shift = 25,
	},
	.hw.init = &(struct clk_init_data){
		.name = "pwm_f_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = a1_pwm_ef_parents,
		.num_parents = ARRAY_SIZE(a1_pwm_ef_parents),
	},
};

static struct clk_regmap a1_pwm_f_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = PWM_CLK_EF_CTRL,
		.shift = 16,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "pwm_f_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_pwm_f_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_pwm_f = {
	.data = &(struct clk_regmap_gate_data){
		.offset = PWM_CLK_EF_CTRL,
		.bit_idx = 24,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "pwm_f",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_pwm_f_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

/*
 * spicc clk
 *   fdiv2   |\         |\       _____
 *  ---------| |---DIV--| |     |     |    spicc out
 *  ---------| |        | |-----|GATE |---------
 *     ..... |/         | /     |_____|
 *  --------------------|/
 *                 24M
 */
static const struct clk_parent_data a1_spi_parents[] = {
	{ .fw_name = "fclk_div2"},
	{ .fw_name = "fclk_div3"},
	{ .fw_name = "fclk_div5"},
	{ .fw_name = "hifi_pll" },
};

static struct clk_regmap a1_spicc_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = SPICC_CLK_CTRL,
		.mask = 0x3,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "spicc_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = a1_spi_parents,
		.num_parents = ARRAY_SIZE(a1_spi_parents),
	},
};

static struct clk_regmap a1_spicc_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = SPICC_CLK_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "spicc_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_spicc_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_spicc_sel2 = {
	.data = &(struct clk_regmap_mux_data){
		.offset = SPICC_CLK_CTRL,
		.mask = 0x1,
		.shift = 15,
	},
	.hw.init = &(struct clk_init_data){
		.name = "spicc_sel2",
		.ops = &clk_regmap_mux_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .hw = &a1_spicc_div.hw },
			{ .fw_name = "xtal", },
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_spicc = {
	.data = &(struct clk_regmap_gate_data){
		.offset = SPICC_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "spicc",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_spicc_sel2.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_ts_div = {
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

static struct clk_regmap a1_ts = {
	.data = &(struct clk_regmap_gate_data){
		.offset = TS_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "ts",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_ts_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_spifc_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = SPIFC_CLK_CTRL,
		.mask = 0x3,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "spifc_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = a1_spi_parents,
		.num_parents = ARRAY_SIZE(a1_spi_parents),
	},
};

static struct clk_regmap a1_spifc_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = SPIFC_CLK_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "spifc_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_spifc_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_spifc_sel2 = {
	.data = &(struct clk_regmap_mux_data){
		.offset = SPIFC_CLK_CTRL,
		.mask = 0x1,
		.shift = 15,
	},
	.hw.init = &(struct clk_init_data){
		.name = "spifc_sel2",
		.ops = &clk_regmap_mux_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .hw = &a1_spifc_div.hw },
			{ .fw_name = "xtal", },
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_spifc = {
	.data = &(struct clk_regmap_gate_data){
		.offset = SPIFC_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "spifc",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_spifc_sel2.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data a1_usb_bus_parents[] = {
	{ .fw_name = "xtal", },
	{ .hw = &a1_sys.hw },
	{ .fw_name = "fclk_div3", },
	{ .fw_name = "fclk_div5", },
};

static struct clk_regmap a1_usb_bus_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = USB_BUSCLK_CTRL,
		.mask = 0x3,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "usb_bus_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = a1_usb_bus_parents,
		.num_parents = ARRAY_SIZE(a1_usb_bus_parents),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_usb_bus_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = USB_BUSCLK_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "usb_bus_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_usb_bus_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_usb_bus = {
	.data = &(struct clk_regmap_gate_data){
		.offset = USB_BUSCLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "usb_bus",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_usb_bus_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data a1_sd_emmc_parents[] = {
	{ .fw_name = "fclk_div2", },
	{ .fw_name = "fclk_div3", },
	{ .fw_name = "fclk_div5", },
	{ .fw_name = "hifi_pll", },
};

static struct clk_regmap a1_sd_emmc_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = SD_EMMC_CLK_CTRL,
		.mask = 0x3,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sd_emmc_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = a1_sd_emmc_parents,
		.num_parents = ARRAY_SIZE(a1_sd_emmc_parents),
	},
};

static struct clk_regmap a1_sd_emmc_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = SD_EMMC_CLK_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sd_emmc_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_sd_emmc_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_sd_emmc_sel2 = {
	.data = &(struct clk_regmap_mux_data){
		.offset = SD_EMMC_CLK_CTRL,
		.mask = 0x1,
		.shift = 15,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sd_emmc_sel2",
		.ops = &clk_regmap_mux_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .hw = &a1_sd_emmc_div.hw },
			{ .fw_name = "xtal", },
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_sd_emmc = {
	.data = &(struct clk_regmap_gate_data){
		.offset = SD_EMMC_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "sd_emmc",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_sd_emmc_sel2.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_psram_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = PSRAM_CLK_CTRL,
		.mask = 0x3,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "psram_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = a1_sd_emmc_parents,
		.num_parents = ARRAY_SIZE(a1_sd_emmc_parents),
	},
};

static struct clk_regmap a1_psram_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = PSRAM_CLK_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "psram_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_psram_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_psram_sel2 = {
	.data = &(struct clk_regmap_mux_data){
		.offset = PSRAM_CLK_CTRL,
		.mask = 0x1,
		.shift = 15,
	},
	.hw.init = &(struct clk_init_data){
		.name = "psram_sel2",
		.ops = &clk_regmap_mux_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .hw = &a1_psram_div.hw },
			{ .fw_name = "xtal", },
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_psram = {
	.data = &(struct clk_regmap_gate_data){
		.offset = PSRAM_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "psram",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_psram_sel2.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_dmc_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = DMC_CLK_CTRL,
		.mask = 0x3,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dmc_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = a1_sd_emmc_parents,
		.num_parents = ARRAY_SIZE(a1_sd_emmc_parents),
	},
};

static struct clk_regmap a1_dmc_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = DMC_CLK_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dmc_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_dmc_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_dmc_sel2 = {
	.data = &(struct clk_regmap_mux_data){
		.offset = DMC_CLK_CTRL,
		.mask = 0x1,
		.shift = 15,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dmc_sel2",
		.ops = &clk_regmap_mux_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .hw = &a1_dmc_div.hw },
			{ .fw_name = "xtal", },
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_dmc = {
	.data = &(struct clk_regmap_gate_data){
		.offset = DMC_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "dmc",
		.ops = &clk_regmap_gate_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_dmc_sel2.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_ceca_32k_in = {
	.data = &(struct clk_regmap_gate_data){
		.offset = CECA_CLK_CTRL0,
		.bit_idx = 31,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "ceca_32k_in",
		.ops = &clk_regmap_gate_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a1_ceca_32k_div = {
	.data = &(struct meson_clk_dualdiv_data){
		.n1 = {
			.reg_off = CECA_CLK_CTRL0,
			.shift   = 0,
			.width   = 12,
		},
		.n2 = {
			.reg_off = CECA_CLK_CTRL0,
			.shift   = 12,
			.width   = 12,
		},
		.m1 = {
			.reg_off = CECA_CLK_CTRL1,
			.shift   = 0,
			.width   = 12,
		},
		.m2 = {
			.reg_off = CECA_CLK_CTRL1,
			.shift   = 12,
			.width   = 12,
		},
		.dual = {
			.reg_off = CECA_CLK_CTRL0,
			.shift   = 28,
			.width   = 1,
		},
		.table = a1_32k_div_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ceca_32k_div",
		.ops = &meson_clk_dualdiv_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_ceca_32k_in.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a1_ceca_32k_sel_pre = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = CECA_CLK_CTRL1,
		.mask = 0x1,
		.shift = 24,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ceca_32k_sel_pre",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_ceca_32k_div.hw,
			&a1_ceca_32k_in.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_ceca_32k_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = CECA_CLK_CTRL1,
		.mask = 0x1,
		.shift = 31,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ceca_32k_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_ceca_32k_sel_pre.hw,
			&a1_rtc.hw,
		},
		.num_parents = 2,
	},
};

static struct clk_regmap a1_ceca_32k_out = {
	.data = &(struct clk_regmap_gate_data){
		.offset = CECA_CLK_CTRL0,
		.bit_idx = 30,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ceca_32k_out",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_ceca_32k_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_cecb_32k_in = {
	.data = &(struct clk_regmap_gate_data){
		.offset = CECB_CLK_CTRL0,
		.bit_idx = 31,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "cecb_32k_in",
		.ops = &clk_regmap_gate_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a1_cecb_32k_div = {
	.data = &(struct meson_clk_dualdiv_data){
		.n1 = {
			.reg_off = CECB_CLK_CTRL0,
			.shift   = 0,
			.width   = 12,
		},
		.n2 = {
			.reg_off = CECB_CLK_CTRL0,
			.shift   = 12,
			.width   = 12,
		},
		.m1 = {
			.reg_off = CECB_CLK_CTRL1,
			.shift   = 0,
			.width   = 12,
		},
		.m2 = {
			.reg_off = CECB_CLK_CTRL1,
			.shift   = 12,
			.width   = 12,
		},
		.dual = {
			.reg_off = CECB_CLK_CTRL0,
			.shift   = 28,
			.width   = 1,
		},
		.table = a1_32k_div_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cecb_32k_div",
		.ops = &meson_clk_dualdiv_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_cecb_32k_in.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a1_cecb_32k_sel_pre = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = CECB_CLK_CTRL1,
		.mask = 0x1,
		.shift = 24,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cecb_32k_sel_pre",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_cecb_32k_div.hw,
			&a1_cecb_32k_in.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a1_cecb_32k_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = CECB_CLK_CTRL1,
		.mask = 0x1,
		.shift = 31,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cecb_32k_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_cecb_32k_sel_pre.hw,
			&a1_rtc.hw,
		},
		.num_parents = 2,
	},
};

static struct clk_regmap a1_cecb_32k_out = {
	.data = &(struct clk_regmap_gate_data){
		.offset = CECB_CLK_CTRL0,
		.bit_idx = 30,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cecb_32k_out",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_cecb_32k_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data a1_pclk_parents = { .hw = &a1_sys.hw };

#define A1_PCLK(_name, _reg, _bit, _flags) \
	MESON_PCLK(a1_##_name, _reg, _bit, &a1_pclk_parents, _flags)

/*
 * NOTE: The gates below are marked with CLK_IGNORE_UNUSED for historic reasons
 * Users are encouraged to test without it and submit changes to:
 *  - remove the flag if not necessary
 *  - replace the flag with something more adequate, such as CLK_IS_CRITICAL,
 *    if appropriate.
 *  - add a comment explaining why the use of CLK_IGNORE_UNUSED is desirable
 *    for a particular clock.
 */
static A1_PCLK(clktree,		SYS_CLK_EN0,	 0, CLK_IGNORE_UNUSED);
static A1_PCLK(reset_ctrl,	SYS_CLK_EN0,	 1, CLK_IGNORE_UNUSED);
static A1_PCLK(analog_ctrl,	SYS_CLK_EN0,	 2, CLK_IGNORE_UNUSED);
static A1_PCLK(pwr_ctrl,	SYS_CLK_EN0,	 3, CLK_IGNORE_UNUSED);
static A1_PCLK(pad_ctrl,	SYS_CLK_EN0,	 4, CLK_IGNORE_UNUSED);
static A1_PCLK(sys_ctrl,	SYS_CLK_EN0,	 5, CLK_IGNORE_UNUSED);
static A1_PCLK(temp_sensor,	SYS_CLK_EN0,	 6, CLK_IGNORE_UNUSED);
static A1_PCLK(am2axi_dev,	SYS_CLK_EN0,	 7, CLK_IGNORE_UNUSED);
static A1_PCLK(spicc_b,		SYS_CLK_EN0,	 8, CLK_IGNORE_UNUSED);
static A1_PCLK(spicc_a,		SYS_CLK_EN0,	 9, CLK_IGNORE_UNUSED);
static A1_PCLK(msr,		SYS_CLK_EN0,	10, CLK_IGNORE_UNUSED);
static A1_PCLK(audio,		SYS_CLK_EN0,	11, CLK_IGNORE_UNUSED);
static A1_PCLK(jtag_ctrl,	SYS_CLK_EN0,	12, CLK_IGNORE_UNUSED);
static A1_PCLK(saradc_en,	SYS_CLK_EN0,	13, CLK_IGNORE_UNUSED);
static A1_PCLK(pwm_ef,		SYS_CLK_EN0,	14, CLK_IGNORE_UNUSED);
static A1_PCLK(pwm_cd,		SYS_CLK_EN0,	15, CLK_IGNORE_UNUSED);
static A1_PCLK(pwm_ab,		SYS_CLK_EN0,	16, CLK_IGNORE_UNUSED);
static A1_PCLK(cec,		SYS_CLK_EN0,	17, CLK_IGNORE_UNUSED);
static A1_PCLK(i2c_s,		SYS_CLK_EN0,	18, CLK_IGNORE_UNUSED);
static A1_PCLK(ir_ctrl,		SYS_CLK_EN0,	19, CLK_IGNORE_UNUSED);
static A1_PCLK(i2c_m_d,		SYS_CLK_EN0,	20, CLK_IGNORE_UNUSED);
static A1_PCLK(i2c_m_c,		SYS_CLK_EN0,	21, CLK_IGNORE_UNUSED);
static A1_PCLK(i2c_m_b,		SYS_CLK_EN0,	22, CLK_IGNORE_UNUSED);
static A1_PCLK(i2c_m_a,		SYS_CLK_EN0,	23, CLK_IGNORE_UNUSED);
static A1_PCLK(acodec,		SYS_CLK_EN0,	24, CLK_IGNORE_UNUSED);
static A1_PCLK(otp,		SYS_CLK_EN0,	25, CLK_IGNORE_UNUSED);
static A1_PCLK(sd_emmc_a,	SYS_CLK_EN0,	26, CLK_IGNORE_UNUSED);
static A1_PCLK(usb_phy,		SYS_CLK_EN0,	27, CLK_IGNORE_UNUSED);
static A1_PCLK(usb_ctrl,	SYS_CLK_EN0,	28, CLK_IGNORE_UNUSED);
static A1_PCLK(sys_dspb,	SYS_CLK_EN0,	29, CLK_IGNORE_UNUSED);
static A1_PCLK(sys_dspa,	SYS_CLK_EN0,	30, CLK_IGNORE_UNUSED);
static A1_PCLK(dma,		SYS_CLK_EN0,	31, CLK_IGNORE_UNUSED);

static A1_PCLK(irq_ctrl,	SYS_CLK_EN1,	 0, CLK_IGNORE_UNUSED);
static A1_PCLK(nic,		SYS_CLK_EN1,	 1, CLK_IGNORE_UNUSED);
static A1_PCLK(gic,		SYS_CLK_EN1,	 2, CLK_IGNORE_UNUSED);
static A1_PCLK(uart_c,		SYS_CLK_EN1,	 3, CLK_IGNORE_UNUSED);
static A1_PCLK(uart_b,		SYS_CLK_EN1,	 4, CLK_IGNORE_UNUSED);
static A1_PCLK(uart_a,		SYS_CLK_EN1,	 5, CLK_IGNORE_UNUSED);
static A1_PCLK(sys_psram,	SYS_CLK_EN1,	 6, CLK_IGNORE_UNUSED);
static A1_PCLK(rsa,		SYS_CLK_EN1,	 8, CLK_IGNORE_UNUSED);
static A1_PCLK(coresight,	SYS_CLK_EN1,	 9, CLK_IGNORE_UNUSED);

static A1_PCLK(am2axi_vad,	AXI_CLK_EN,	 0, CLK_IGNORE_UNUSED);
static A1_PCLK(audio_vad,	AXI_CLK_EN,	 1, CLK_IGNORE_UNUSED);
static A1_PCLK(axi_dmc,		AXI_CLK_EN,	 3, CLK_IGNORE_UNUSED);
static A1_PCLK(axi_psram,	AXI_CLK_EN,	 4, CLK_IGNORE_UNUSED);
static A1_PCLK(ramb,		AXI_CLK_EN,	 5, CLK_IGNORE_UNUSED);
static A1_PCLK(rama,		AXI_CLK_EN,	 6, CLK_IGNORE_UNUSED);
static A1_PCLK(axi_spifc,	AXI_CLK_EN,	 7, CLK_IGNORE_UNUSED);
static A1_PCLK(axi_nic,		AXI_CLK_EN,	 8, CLK_IGNORE_UNUSED);
static A1_PCLK(axi_dma,		AXI_CLK_EN,	 9, CLK_IGNORE_UNUSED);
static A1_PCLK(cpu_ctrl,	AXI_CLK_EN,	10, CLK_IGNORE_UNUSED);
static A1_PCLK(rom,		AXI_CLK_EN,	11, CLK_IGNORE_UNUSED);
static A1_PCLK(prod_i2c,	AXI_CLK_EN,	12, CLK_IGNORE_UNUSED);

/* Array of all clocks registered by this provider */
static struct clk_hw *a1_peripherals_hw_clks[] = {
	[CLKID_XTAL_IN]			= &a1_xtal_in.hw,
	[CLKID_FIXPLL_IN]		= &a1_fixpll_in.hw,
	[CLKID_USB_PHY_IN]		= &a1_usb_phy_in.hw,
	[CLKID_USB_CTRL_IN]		= &a1_usb_ctrl_in.hw,
	[CLKID_HIFIPLL_IN]		= &a1_hifipll_in.hw,
	[CLKID_SYSPLL_IN]		= &a1_syspll_in.hw,
	[CLKID_DDS_IN]			= &a1_dds_in.hw,
	[CLKID_SYS]			= &a1_sys.hw,
	[CLKID_CLKTREE]			= &a1_clktree.hw,
	[CLKID_RESET_CTRL]		= &a1_reset_ctrl.hw,
	[CLKID_ANALOG_CTRL]		= &a1_analog_ctrl.hw,
	[CLKID_PWR_CTRL]		= &a1_pwr_ctrl.hw,
	[CLKID_PAD_CTRL]		= &a1_pad_ctrl.hw,
	[CLKID_SYS_CTRL]		= &a1_sys_ctrl.hw,
	[CLKID_TEMP_SENSOR]		= &a1_temp_sensor.hw,
	[CLKID_AM2AXI_DIV]		= &a1_am2axi_dev.hw,
	[CLKID_SPICC_B]			= &a1_spicc_b.hw,
	[CLKID_SPICC_A]			= &a1_spicc_a.hw,
	[CLKID_MSR]			= &a1_msr.hw,
	[CLKID_AUDIO]			= &a1_audio.hw,
	[CLKID_JTAG_CTRL]		= &a1_jtag_ctrl.hw,
	[CLKID_SARADC_EN]		= &a1_saradc_en.hw,
	[CLKID_PWM_EF]			= &a1_pwm_ef.hw,
	[CLKID_PWM_CD]			= &a1_pwm_cd.hw,
	[CLKID_PWM_AB]			= &a1_pwm_ab.hw,
	[CLKID_CEC]			= &a1_cec.hw,
	[CLKID_I2C_S]			= &a1_i2c_s.hw,
	[CLKID_IR_CTRL]			= &a1_ir_ctrl.hw,
	[CLKID_I2C_M_D]			= &a1_i2c_m_d.hw,
	[CLKID_I2C_M_C]			= &a1_i2c_m_c.hw,
	[CLKID_I2C_M_B]			= &a1_i2c_m_b.hw,
	[CLKID_I2C_M_A]			= &a1_i2c_m_a.hw,
	[CLKID_ACODEC]			= &a1_acodec.hw,
	[CLKID_OTP]			= &a1_otp.hw,
	[CLKID_SD_EMMC_A]		= &a1_sd_emmc_a.hw,
	[CLKID_USB_PHY]			= &a1_usb_phy.hw,
	[CLKID_USB_CTRL]		= &a1_usb_ctrl.hw,
	[CLKID_SYS_DSPB]		= &a1_sys_dspb.hw,
	[CLKID_SYS_DSPA]		= &a1_sys_dspa.hw,
	[CLKID_DMA]			= &a1_dma.hw,
	[CLKID_IRQ_CTRL]		= &a1_irq_ctrl.hw,
	[CLKID_NIC]			= &a1_nic.hw,
	[CLKID_GIC]			= &a1_gic.hw,
	[CLKID_UART_C]			= &a1_uart_c.hw,
	[CLKID_UART_B]			= &a1_uart_b.hw,
	[CLKID_UART_A]			= &a1_uart_a.hw,
	[CLKID_SYS_PSRAM]		= &a1_sys_psram.hw,
	[CLKID_RSA]			= &a1_rsa.hw,
	[CLKID_CORESIGHT]		= &a1_coresight.hw,
	[CLKID_AM2AXI_VAD]		= &a1_am2axi_vad.hw,
	[CLKID_AUDIO_VAD]		= &a1_audio_vad.hw,
	[CLKID_AXI_DMC]			= &a1_axi_dmc.hw,
	[CLKID_AXI_PSRAM]		= &a1_axi_psram.hw,
	[CLKID_RAMB]			= &a1_ramb.hw,
	[CLKID_RAMA]			= &a1_rama.hw,
	[CLKID_AXI_SPIFC]		= &a1_axi_spifc.hw,
	[CLKID_AXI_NIC]			= &a1_axi_nic.hw,
	[CLKID_AXI_DMA]			= &a1_axi_dma.hw,
	[CLKID_CPU_CTRL]		= &a1_cpu_ctrl.hw,
	[CLKID_ROM]			= &a1_rom.hw,
	[CLKID_PROC_I2C]		= &a1_prod_i2c.hw,
	[CLKID_DSPA_SEL]		= &a1_dspa_sel.hw,
	[CLKID_DSPB_SEL]		= &a1_dspb_sel.hw,
	[CLKID_DSPA_EN]			= &a1_dspa_en.hw,
	[CLKID_DSPA_EN_NIC]		= &a1_dspa_en_nic.hw,
	[CLKID_DSPB_EN]			= &a1_dspb_en.hw,
	[CLKID_DSPB_EN_NIC]		= &a1_dspb_en_nic.hw,
	[CLKID_RTC]			= &a1_rtc.hw,
	[CLKID_CECA_32K]		= &a1_ceca_32k_out.hw,
	[CLKID_CECB_32K]		= &a1_cecb_32k_out.hw,
	[CLKID_24M]			= &a1_24m.hw,
	[CLKID_12M]			= &a1_12m.hw,
	[CLKID_FCLK_DIV2_DIVN]		= &a1_fclk_div2_divn.hw,
	[CLKID_GEN]			= &a1_gen.hw,
	[CLKID_SARADC_SEL]		= &a1_saradc_sel.hw,
	[CLKID_SARADC]			= &a1_saradc.hw,
	[CLKID_PWM_A]			= &a1_pwm_a.hw,
	[CLKID_PWM_B]			= &a1_pwm_b.hw,
	[CLKID_PWM_C]			= &a1_pwm_c.hw,
	[CLKID_PWM_D]			= &a1_pwm_d.hw,
	[CLKID_PWM_E]			= &a1_pwm_e.hw,
	[CLKID_PWM_F]			= &a1_pwm_f.hw,
	[CLKID_SPICC]			= &a1_spicc.hw,
	[CLKID_TS]			= &a1_ts.hw,
	[CLKID_SPIFC]			= &a1_spifc.hw,
	[CLKID_USB_BUS]			= &a1_usb_bus.hw,
	[CLKID_SD_EMMC]			= &a1_sd_emmc.hw,
	[CLKID_PSRAM]			= &a1_psram.hw,
	[CLKID_DMC]			= &a1_dmc.hw,
	[CLKID_SYS_A_SEL]		= &a1_sys_a_sel.hw,
	[CLKID_SYS_A_DIV]		= &a1_sys_a_div.hw,
	[CLKID_SYS_A]			= &a1_sys_a.hw,
	[CLKID_SYS_B_SEL]		= &a1_sys_b_sel.hw,
	[CLKID_SYS_B_DIV]		= &a1_sys_b_div.hw,
	[CLKID_SYS_B]			= &a1_sys_b.hw,
	[CLKID_DSPA_A_SEL]		= &a1_dspa_a_sel.hw,
	[CLKID_DSPA_A_DIV]		= &a1_dspa_a_div.hw,
	[CLKID_DSPA_A]			= &a1_dspa_a.hw,
	[CLKID_DSPA_B_SEL]		= &a1_dspa_b_sel.hw,
	[CLKID_DSPA_B_DIV]		= &a1_dspa_b_div.hw,
	[CLKID_DSPA_B]			= &a1_dspa_b.hw,
	[CLKID_DSPB_A_SEL]		= &a1_dspb_a_sel.hw,
	[CLKID_DSPB_A_DIV]		= &a1_dspb_a_div.hw,
	[CLKID_DSPB_A]			= &a1_dspb_a.hw,
	[CLKID_DSPB_B_SEL]		= &a1_dspb_b_sel.hw,
	[CLKID_DSPB_B_DIV]		= &a1_dspb_b_div.hw,
	[CLKID_DSPB_B]			= &a1_dspb_b.hw,
	[CLKID_RTC_32K_IN]		= &a1_rtc_32k_in.hw,
	[CLKID_RTC_32K_DIV]		= &a1_rtc_32k_div.hw,
	[CLKID_RTC_32K_XTAL]		= &a1_rtc_32k_xtal.hw,
	[CLKID_RTC_32K_SEL]		= &a1_rtc_32k_sel.hw,
	[CLKID_CECB_32K_IN]		= &a1_cecb_32k_in.hw,
	[CLKID_CECB_32K_DIV]		= &a1_cecb_32k_div.hw,
	[CLKID_CECB_32K_SEL_PRE]	= &a1_cecb_32k_sel_pre.hw,
	[CLKID_CECB_32K_SEL]		= &a1_cecb_32k_sel.hw,
	[CLKID_CECA_32K_IN]		= &a1_ceca_32k_in.hw,
	[CLKID_CECA_32K_DIV]		= &a1_ceca_32k_div.hw,
	[CLKID_CECA_32K_SEL_PRE]	= &a1_ceca_32k_sel_pre.hw,
	[CLKID_CECA_32K_SEL]		= &a1_ceca_32k_sel.hw,
	[CLKID_DIV2_PRE]		= &a1_fclk_div2_divn_pre.hw,
	[CLKID_24M_DIV2]		= &a1_24m_div2.hw,
	[CLKID_GEN_SEL]			= &a1_gen_sel.hw,
	[CLKID_GEN_DIV]			= &a1_gen_div.hw,
	[CLKID_SARADC_DIV]		= &a1_saradc_div.hw,
	[CLKID_PWM_A_SEL]		= &a1_pwm_a_sel.hw,
	[CLKID_PWM_A_DIV]		= &a1_pwm_a_div.hw,
	[CLKID_PWM_B_SEL]		= &a1_pwm_b_sel.hw,
	[CLKID_PWM_B_DIV]		= &a1_pwm_b_div.hw,
	[CLKID_PWM_C_SEL]		= &a1_pwm_c_sel.hw,
	[CLKID_PWM_C_DIV]		= &a1_pwm_c_div.hw,
	[CLKID_PWM_D_SEL]		= &a1_pwm_d_sel.hw,
	[CLKID_PWM_D_DIV]		= &a1_pwm_d_div.hw,
	[CLKID_PWM_E_SEL]		= &a1_pwm_e_sel.hw,
	[CLKID_PWM_E_DIV]		= &a1_pwm_e_div.hw,
	[CLKID_PWM_F_SEL]		= &a1_pwm_f_sel.hw,
	[CLKID_PWM_F_DIV]		= &a1_pwm_f_div.hw,
	[CLKID_SPICC_SEL]		= &a1_spicc_sel.hw,
	[CLKID_SPICC_DIV]		= &a1_spicc_div.hw,
	[CLKID_SPICC_SEL2]		= &a1_spicc_sel2.hw,
	[CLKID_TS_DIV]			= &a1_ts_div.hw,
	[CLKID_SPIFC_SEL]		= &a1_spifc_sel.hw,
	[CLKID_SPIFC_DIV]		= &a1_spifc_div.hw,
	[CLKID_SPIFC_SEL2]		= &a1_spifc_sel2.hw,
	[CLKID_USB_BUS_SEL]		= &a1_usb_bus_sel.hw,
	[CLKID_USB_BUS_DIV]		= &a1_usb_bus_div.hw,
	[CLKID_SD_EMMC_SEL]		= &a1_sd_emmc_sel.hw,
	[CLKID_SD_EMMC_DIV]		= &a1_sd_emmc_div.hw,
	[CLKID_SD_EMMC_SEL2]		= &a1_sd_emmc_sel2.hw,
	[CLKID_PSRAM_SEL]		= &a1_psram_sel.hw,
	[CLKID_PSRAM_DIV]		= &a1_psram_div.hw,
	[CLKID_PSRAM_SEL2]		= &a1_psram_sel2.hw,
	[CLKID_DMC_SEL]			= &a1_dmc_sel.hw,
	[CLKID_DMC_DIV]			= &a1_dmc_div.hw,
	[CLKID_DMC_SEL2]		= &a1_dmc_sel2.hw,
};

static const struct meson_clkc_data a1_peripherals_clkc_data = {
	.hw_clks = {
		.hws = a1_peripherals_hw_clks,
		.num = ARRAY_SIZE(a1_peripherals_hw_clks),
	},
};

static const struct of_device_id a1_peripherals_clkc_match_table[] = {
	{
		.compatible = "amlogic,a1-peripherals-clkc",
		.data = &a1_peripherals_clkc_data,
	},
	{}
};
MODULE_DEVICE_TABLE(of, a1_peripherals_clkc_match_table);

static struct platform_driver a1_peripherals_clkc_driver = {
	.probe = meson_clkc_mmio_probe,
	.driver = {
		.name = "a1-peripherals-clkc",
		.of_match_table = a1_peripherals_clkc_match_table,
	},
};
module_platform_driver(a1_peripherals_clkc_driver);

MODULE_DESCRIPTION("Amlogic A1 Peripherals Clock Controller driver");
MODULE_AUTHOR("Jian Hu <jian.hu@amlogic.com>");
MODULE_AUTHOR("Dmitry Rokosov <ddrokosov@sberdevices.ru>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("CLK_MESON");
