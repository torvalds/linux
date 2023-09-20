// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 * Author: Jian Hu <jian.hu@amlogic.com>
 *
 * Copyright (c) 2023, SberDevices. All Rights Reserved.
 * Author: Dmitry Rokosov <ddrokosov@sberdevices.ru>
 */

#include <linux/clk-provider.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include "a1-peripherals.h"
#include "clk-dualdiv.h"
#include "clk-regmap.h"

static struct clk_regmap xtal_in = {
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

static struct clk_regmap fixpll_in = {
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

static struct clk_regmap usb_phy_in = {
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

static struct clk_regmap usb_ctrl_in = {
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

static struct clk_regmap hifipll_in = {
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

static struct clk_regmap syspll_in = {
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

static struct clk_regmap dds_in = {
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

static struct clk_regmap rtc_32k_in = {
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

static const struct meson_clk_dualdiv_param clk_32k_div_table[] = {
	{
		.dual		= 1,
		.n1		= 733,
		.m1		= 8,
		.n2		= 732,
		.m2		= 11,
	},
	{}
};

static struct clk_regmap rtc_32k_div = {
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
		.table = clk_32k_div_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "rtc_32k_div",
		.ops = &meson_clk_dualdiv_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&rtc_32k_in.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap rtc_32k_xtal = {
	.data = &(struct clk_regmap_gate_data){
		.offset = RTC_BY_OSCIN_CTRL1,
		.bit_idx = 24,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "rtc_32k_xtal",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&rtc_32k_in.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap rtc_32k_sel = {
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
			&rtc_32k_xtal.hw,
			&rtc_32k_div.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap rtc = {
	.data = &(struct clk_regmap_gate_data){
		.offset = RTC_BY_OSCIN_CTRL0,
		.bit_idx = 30,
	},
	.hw.init = &(struct clk_init_data){
		.name = "rtc",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&rtc_32k_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static u32 mux_table_sys[] = { 0, 1, 2, 3, 7 };
static const struct clk_parent_data sys_parents[] = {
	{ .fw_name = "xtal" },
	{ .fw_name = "fclk_div2" },
	{ .fw_name = "fclk_div3" },
	{ .fw_name = "fclk_div5" },
	{ .hw = &rtc.hw },
};

static struct clk_regmap sys_b_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = SYS_CLK_CTRL0,
		.mask = 0x7,
		.shift = 26,
		.table = mux_table_sys,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sys_b_sel",
		.ops = &clk_regmap_mux_ro_ops,
		.parent_data = sys_parents,
		.num_parents = ARRAY_SIZE(sys_parents),
	},
};

static struct clk_regmap sys_b_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = SYS_CLK_CTRL0,
		.shift = 16,
		.width = 10,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sys_b_div",
		.ops = &clk_regmap_divider_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&sys_b_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap sys_b = {
	.data = &(struct clk_regmap_gate_data){
		.offset = SYS_CLK_CTRL0,
		.bit_idx = 29,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "sys_b",
		.ops = &clk_regmap_gate_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&sys_b_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap sys_a_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = SYS_CLK_CTRL0,
		.mask = 0x7,
		.shift = 10,
		.table = mux_table_sys,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sys_a_sel",
		.ops = &clk_regmap_mux_ro_ops,
		.parent_data = sys_parents,
		.num_parents = ARRAY_SIZE(sys_parents),
	},
};

static struct clk_regmap sys_a_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = SYS_CLK_CTRL0,
		.shift = 0,
		.width = 10,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sys_a_div",
		.ops = &clk_regmap_divider_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&sys_a_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap sys_a = {
	.data = &(struct clk_regmap_gate_data){
		.offset = SYS_CLK_CTRL0,
		.bit_idx = 13,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "sys_a",
		.ops = &clk_regmap_gate_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&sys_a_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap sys = {
	.data = &(struct clk_regmap_mux_data){
		.offset = SYS_CLK_CTRL0,
		.mask = 0x1,
		.shift = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sys",
		.ops = &clk_regmap_mux_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&sys_a.hw,
			&sys_b.hw,
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

static u32 mux_table_dsp_ab[] = { 0, 1, 2, 3, 4, 7 };
static const struct clk_parent_data dsp_ab_parent_data[] = {
	{ .fw_name = "xtal", },
	{ .fw_name = "fclk_div2", },
	{ .fw_name = "fclk_div3", },
	{ .fw_name = "fclk_div5", },
	{ .fw_name = "hifi_pll", },
	{ .hw = &rtc.hw },
};

static struct clk_regmap dspa_a_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = DSPA_CLK_CTRL0,
		.mask = 0x7,
		.shift = 10,
		.table = mux_table_dsp_ab,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dspa_a_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = dsp_ab_parent_data,
		.num_parents = ARRAY_SIZE(dsp_ab_parent_data),
	},
};

static struct clk_regmap dspa_a_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = DSPA_CLK_CTRL0,
		.shift = 0,
		.width = 10,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dspa_a_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&dspa_a_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap dspa_a = {
	.data = &(struct clk_regmap_gate_data){
		.offset = DSPA_CLK_CTRL0,
		.bit_idx = 13,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "dspa_a",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&dspa_a_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap dspa_b_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = DSPA_CLK_CTRL0,
		.mask = 0x7,
		.shift = 26,
		.table = mux_table_dsp_ab,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dspa_b_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = dsp_ab_parent_data,
		.num_parents = ARRAY_SIZE(dsp_ab_parent_data),
	},
};

static struct clk_regmap dspa_b_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = DSPA_CLK_CTRL0,
		.shift = 16,
		.width = 10,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dspa_b_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&dspa_b_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap dspa_b = {
	.data = &(struct clk_regmap_gate_data){
		.offset = DSPA_CLK_CTRL0,
		.bit_idx = 29,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "dspa_b",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&dspa_b_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap dspa_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = DSPA_CLK_CTRL0,
		.mask = 0x1,
		.shift = 15,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dspa_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&dspa_a.hw,
			&dspa_b.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap dspa_en = {
	.data = &(struct clk_regmap_gate_data){
		.offset = DSPA_CLK_EN,
		.bit_idx = 1,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "dspa_en",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&dspa_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap dspa_en_nic = {
	.data = &(struct clk_regmap_gate_data){
		.offset = DSPA_CLK_EN,
		.bit_idx = 0,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "dspa_en_nic",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&dspa_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap dspb_a_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = DSPB_CLK_CTRL0,
		.mask = 0x7,
		.shift = 10,
		.table = mux_table_dsp_ab,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dspb_a_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = dsp_ab_parent_data,
		.num_parents = ARRAY_SIZE(dsp_ab_parent_data),
	},
};

static struct clk_regmap dspb_a_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = DSPB_CLK_CTRL0,
		.shift = 0,
		.width = 10,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dspb_a_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&dspb_a_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap dspb_a = {
	.data = &(struct clk_regmap_gate_data){
		.offset = DSPB_CLK_CTRL0,
		.bit_idx = 13,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "dspb_a",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&dspb_a_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap dspb_b_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = DSPB_CLK_CTRL0,
		.mask = 0x7,
		.shift = 26,
		.table = mux_table_dsp_ab,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dspb_b_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = dsp_ab_parent_data,
		.num_parents = ARRAY_SIZE(dsp_ab_parent_data),
	},
};

static struct clk_regmap dspb_b_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = DSPB_CLK_CTRL0,
		.shift = 16,
		.width = 10,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dspb_b_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&dspb_b_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap dspb_b = {
	.data = &(struct clk_regmap_gate_data){
		.offset = DSPB_CLK_CTRL0,
		.bit_idx = 29,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "dspb_b",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&dspb_b_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap dspb_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = DSPB_CLK_CTRL0,
		.mask = 0x1,
		.shift = 15,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dspb_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&dspb_a.hw,
			&dspb_b.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap dspb_en = {
	.data = &(struct clk_regmap_gate_data){
		.offset = DSPB_CLK_EN,
		.bit_idx = 1,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "dspb_en",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&dspb_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap dspb_en_nic = {
	.data = &(struct clk_regmap_gate_data){
		.offset = DSPB_CLK_EN,
		.bit_idx = 0,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "dspb_en_nic",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&dspb_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap clk_24m = {
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

static struct clk_fixed_factor clk_24m_div2 = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "24m_div2",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&clk_24m.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap clk_12m = {
	.data = &(struct clk_regmap_gate_data){
		.offset = CLK12_24_CTRL,
		.bit_idx = 10,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "12m",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&clk_24m_div2.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap fclk_div2_divn_pre = {
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

static struct clk_regmap fclk_div2_divn = {
	.data = &(struct clk_regmap_gate_data){
		.offset = CLK12_24_CTRL,
		.bit_idx = 12,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div2_divn",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&fclk_div2_divn_pre.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

/*
 * the index 2 is sys_pll_div16, it will be implemented in the CPU clock driver,
 * the index 4 is the clock measurement source, it's not supported yet
 */
static u32 gen_table[] = { 0, 1, 3, 5, 6, 7, 8 };
static const struct clk_parent_data gen_parent_data[] = {
	{ .fw_name = "xtal", },
	{ .hw = &rtc.hw },
	{ .fw_name = "hifi_pll", },
	{ .fw_name = "fclk_div2", },
	{ .fw_name = "fclk_div3", },
	{ .fw_name = "fclk_div5", },
	{ .fw_name = "fclk_div7", },
};

static struct clk_regmap gen_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = GEN_CLK_CTRL,
		.mask = 0xf,
		.shift = 12,
		.table = gen_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "gen_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = gen_parent_data,
		.num_parents = ARRAY_SIZE(gen_parent_data),
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

static struct clk_regmap gen_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = GEN_CLK_CTRL,
		.shift = 0,
		.width = 11,
	},
	.hw.init = &(struct clk_init_data){
		.name = "gen_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gen_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gen = {
	.data = &(struct clk_regmap_gate_data){
		.offset = GEN_CLK_CTRL,
		.bit_idx = 11,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "gen",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gen_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap saradc_sel = {
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
			{ .hw = &sys.hw, },
		},
		.num_parents = 2,
	},
};

static struct clk_regmap saradc_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = SAR_ADC_CLK_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "saradc_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&saradc_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap saradc = {
	.data = &(struct clk_regmap_gate_data){
		.offset = SAR_ADC_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "saradc",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&saradc_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data pwm_abcd_parents[] = {
	{ .fw_name = "xtal", },
	{ .hw = &sys.hw },
	{ .hw = &rtc.hw },
};

static struct clk_regmap pwm_a_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = PWM_CLK_AB_CTRL,
		.mask = 0x1,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "pwm_a_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = pwm_abcd_parents,
		.num_parents = ARRAY_SIZE(pwm_abcd_parents),
	},
};

static struct clk_regmap pwm_a_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = PWM_CLK_AB_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "pwm_a_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&pwm_a_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap pwm_a = {
	.data = &(struct clk_regmap_gate_data){
		.offset = PWM_CLK_AB_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "pwm_a",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&pwm_a_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap pwm_b_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = PWM_CLK_AB_CTRL,
		.mask = 0x1,
		.shift = 25,
	},
	.hw.init = &(struct clk_init_data){
		.name = "pwm_b_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = pwm_abcd_parents,
		.num_parents = ARRAY_SIZE(pwm_abcd_parents),
	},
};

static struct clk_regmap pwm_b_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = PWM_CLK_AB_CTRL,
		.shift = 16,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "pwm_b_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&pwm_b_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap pwm_b = {
	.data = &(struct clk_regmap_gate_data){
		.offset = PWM_CLK_AB_CTRL,
		.bit_idx = 24,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "pwm_b",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&pwm_b_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap pwm_c_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = PWM_CLK_CD_CTRL,
		.mask = 0x1,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "pwm_c_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = pwm_abcd_parents,
		.num_parents = ARRAY_SIZE(pwm_abcd_parents),
	},
};

static struct clk_regmap pwm_c_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = PWM_CLK_CD_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "pwm_c_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&pwm_c_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap pwm_c = {
	.data = &(struct clk_regmap_gate_data){
		.offset = PWM_CLK_CD_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "pwm_c",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&pwm_c_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap pwm_d_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = PWM_CLK_CD_CTRL,
		.mask = 0x1,
		.shift = 25,
	},
	.hw.init = &(struct clk_init_data){
		.name = "pwm_d_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = pwm_abcd_parents,
		.num_parents = ARRAY_SIZE(pwm_abcd_parents),
	},
};

static struct clk_regmap pwm_d_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = PWM_CLK_CD_CTRL,
		.shift = 16,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "pwm_d_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&pwm_d_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap pwm_d = {
	.data = &(struct clk_regmap_gate_data){
		.offset = PWM_CLK_CD_CTRL,
		.bit_idx = 24,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "pwm_d",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&pwm_d_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data pwm_ef_parents[] = {
	{ .fw_name = "xtal", },
	{ .hw = &sys.hw },
	{ .fw_name = "fclk_div5", },
	{ .hw = &rtc.hw },
};

static struct clk_regmap pwm_e_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = PWM_CLK_EF_CTRL,
		.mask = 0x3,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "pwm_e_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = pwm_ef_parents,
		.num_parents = ARRAY_SIZE(pwm_ef_parents),
	},
};

static struct clk_regmap pwm_e_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = PWM_CLK_EF_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "pwm_e_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&pwm_e_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap pwm_e = {
	.data = &(struct clk_regmap_gate_data){
		.offset = PWM_CLK_EF_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "pwm_e",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&pwm_e_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap pwm_f_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = PWM_CLK_EF_CTRL,
		.mask = 0x3,
		.shift = 25,
	},
	.hw.init = &(struct clk_init_data){
		.name = "pwm_f_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = pwm_ef_parents,
		.num_parents = ARRAY_SIZE(pwm_ef_parents),
	},
};

static struct clk_regmap pwm_f_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = PWM_CLK_EF_CTRL,
		.shift = 16,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "pwm_f_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&pwm_f_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap pwm_f = {
	.data = &(struct clk_regmap_gate_data){
		.offset = PWM_CLK_EF_CTRL,
		.bit_idx = 24,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "pwm_f",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&pwm_f_div.hw
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
static const struct clk_parent_data spicc_spifc_parents[] = {
	{ .fw_name = "fclk_div2"},
	{ .fw_name = "fclk_div3"},
	{ .fw_name = "fclk_div5"},
	{ .fw_name = "hifi_pll" },
};

static struct clk_regmap spicc_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = SPICC_CLK_CTRL,
		.mask = 0x3,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "spicc_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = spicc_spifc_parents,
		.num_parents = ARRAY_SIZE(spicc_spifc_parents),
	},
};

static struct clk_regmap spicc_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = SPICC_CLK_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "spicc_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&spicc_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap spicc_sel2 = {
	.data = &(struct clk_regmap_mux_data){
		.offset = SPICC_CLK_CTRL,
		.mask = 0x1,
		.shift = 15,
	},
	.hw.init = &(struct clk_init_data){
		.name = "spicc_sel2",
		.ops = &clk_regmap_mux_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .hw = &spicc_div.hw },
			{ .fw_name = "xtal", },
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap spicc = {
	.data = &(struct clk_regmap_gate_data){
		.offset = SPICC_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "spicc",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&spicc_sel2.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap ts_div = {
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

static struct clk_regmap ts = {
	.data = &(struct clk_regmap_gate_data){
		.offset = TS_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "ts",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&ts_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap spifc_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = SPIFC_CLK_CTRL,
		.mask = 0x3,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "spifc_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = spicc_spifc_parents,
		.num_parents = ARRAY_SIZE(spicc_spifc_parents),
	},
};

static struct clk_regmap spifc_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = SPIFC_CLK_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "spifc_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&spifc_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap spifc_sel2 = {
	.data = &(struct clk_regmap_mux_data){
		.offset = SPIFC_CLK_CTRL,
		.mask = 0x1,
		.shift = 15,
	},
	.hw.init = &(struct clk_init_data){
		.name = "spifc_sel2",
		.ops = &clk_regmap_mux_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .hw = &spifc_div.hw },
			{ .fw_name = "xtal", },
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap spifc = {
	.data = &(struct clk_regmap_gate_data){
		.offset = SPIFC_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "spifc",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&spifc_sel2.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data usb_bus_parents[] = {
	{ .fw_name = "xtal", },
	{ .hw = &sys.hw },
	{ .fw_name = "fclk_div3", },
	{ .fw_name = "fclk_div5", },
};

static struct clk_regmap usb_bus_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = USB_BUSCLK_CTRL,
		.mask = 0x3,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "usb_bus_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = usb_bus_parents,
		.num_parents = ARRAY_SIZE(usb_bus_parents),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap usb_bus_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = USB_BUSCLK_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "usb_bus_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&usb_bus_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap usb_bus = {
	.data = &(struct clk_regmap_gate_data){
		.offset = USB_BUSCLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "usb_bus",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&usb_bus_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data sd_emmc_psram_dmc_parents[] = {
	{ .fw_name = "fclk_div2", },
	{ .fw_name = "fclk_div3", },
	{ .fw_name = "fclk_div5", },
	{ .fw_name = "hifi_pll", },
};

static struct clk_regmap sd_emmc_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = SD_EMMC_CLK_CTRL,
		.mask = 0x3,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sd_emmc_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = sd_emmc_psram_dmc_parents,
		.num_parents = ARRAY_SIZE(sd_emmc_psram_dmc_parents),
	},
};

static struct clk_regmap sd_emmc_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = SD_EMMC_CLK_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sd_emmc_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&sd_emmc_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap sd_emmc_sel2 = {
	.data = &(struct clk_regmap_mux_data){
		.offset = SD_EMMC_CLK_CTRL,
		.mask = 0x1,
		.shift = 15,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sd_emmc_sel2",
		.ops = &clk_regmap_mux_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .hw = &sd_emmc_div.hw },
			{ .fw_name = "xtal", },
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap sd_emmc = {
	.data = &(struct clk_regmap_gate_data){
		.offset = SD_EMMC_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "sd_emmc",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&sd_emmc_sel2.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap psram_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = PSRAM_CLK_CTRL,
		.mask = 0x3,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "psram_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = sd_emmc_psram_dmc_parents,
		.num_parents = ARRAY_SIZE(sd_emmc_psram_dmc_parents),
	},
};

static struct clk_regmap psram_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = PSRAM_CLK_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "psram_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&psram_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap psram_sel2 = {
	.data = &(struct clk_regmap_mux_data){
		.offset = PSRAM_CLK_CTRL,
		.mask = 0x1,
		.shift = 15,
	},
	.hw.init = &(struct clk_init_data){
		.name = "psram_sel2",
		.ops = &clk_regmap_mux_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .hw = &psram_div.hw },
			{ .fw_name = "xtal", },
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap psram = {
	.data = &(struct clk_regmap_gate_data){
		.offset = PSRAM_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "psram",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&psram_sel2.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap dmc_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = DMC_CLK_CTRL,
		.mask = 0x3,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dmc_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = sd_emmc_psram_dmc_parents,
		.num_parents = ARRAY_SIZE(sd_emmc_psram_dmc_parents),
	},
};

static struct clk_regmap dmc_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = DMC_CLK_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dmc_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&dmc_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap dmc_sel2 = {
	.data = &(struct clk_regmap_mux_data){
		.offset = DMC_CLK_CTRL,
		.mask = 0x1,
		.shift = 15,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dmc_sel2",
		.ops = &clk_regmap_mux_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .hw = &dmc_div.hw },
			{ .fw_name = "xtal", },
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap dmc = {
	.data = &(struct clk_regmap_gate_data){
		.offset = DMC_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "dmc",
		.ops = &clk_regmap_gate_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&dmc_sel2.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap ceca_32k_in = {
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

static struct clk_regmap ceca_32k_div = {
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
		.table = clk_32k_div_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ceca_32k_div",
		.ops = &meson_clk_dualdiv_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&ceca_32k_in.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap ceca_32k_sel_pre = {
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
			&ceca_32k_div.hw,
			&ceca_32k_in.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap ceca_32k_sel = {
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
			&ceca_32k_sel_pre.hw,
			&rtc.hw,
		},
		.num_parents = 2,
	},
};

static struct clk_regmap ceca_32k_out = {
	.data = &(struct clk_regmap_gate_data){
		.offset = CECA_CLK_CTRL0,
		.bit_idx = 30,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ceca_32k_out",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&ceca_32k_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap cecb_32k_in = {
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

static struct clk_regmap cecb_32k_div = {
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
		.table = clk_32k_div_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cecb_32k_div",
		.ops = &meson_clk_dualdiv_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&cecb_32k_in.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap cecb_32k_sel_pre = {
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
			&cecb_32k_div.hw,
			&cecb_32k_in.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap cecb_32k_sel = {
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
			&cecb_32k_sel_pre.hw,
			&rtc.hw,
		},
		.num_parents = 2,
	},
};

static struct clk_regmap cecb_32k_out = {
	.data = &(struct clk_regmap_gate_data){
		.offset = CECB_CLK_CTRL0,
		.bit_idx = 30,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cecb_32k_out",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&cecb_32k_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

#define MESON_GATE(_name, _reg, _bit) \
	MESON_PCLK(_name, _reg, _bit, &sys.hw)

static MESON_GATE(clktree,	SYS_CLK_EN0,	0);
static MESON_GATE(reset_ctrl,	SYS_CLK_EN0,	1);
static MESON_GATE(analog_ctrl,	SYS_CLK_EN0,	2);
static MESON_GATE(pwr_ctrl,	SYS_CLK_EN0,	3);
static MESON_GATE(pad_ctrl,	SYS_CLK_EN0,	4);
static MESON_GATE(sys_ctrl,	SYS_CLK_EN0,	5);
static MESON_GATE(temp_sensor,	SYS_CLK_EN0,	6);
static MESON_GATE(am2axi_dev,	SYS_CLK_EN0,	7);
static MESON_GATE(spicc_b,	SYS_CLK_EN0,	8);
static MESON_GATE(spicc_a,	SYS_CLK_EN0,	9);
static MESON_GATE(msr,		SYS_CLK_EN0,	10);
static MESON_GATE(audio,	SYS_CLK_EN0,	11);
static MESON_GATE(jtag_ctrl,	SYS_CLK_EN0,	12);
static MESON_GATE(saradc_en,	SYS_CLK_EN0,	13);
static MESON_GATE(pwm_ef,	SYS_CLK_EN0,	14);
static MESON_GATE(pwm_cd,	SYS_CLK_EN0,	15);
static MESON_GATE(pwm_ab,	SYS_CLK_EN0,	16);
static MESON_GATE(cec,		SYS_CLK_EN0,	17);
static MESON_GATE(i2c_s,	SYS_CLK_EN0,	18);
static MESON_GATE(ir_ctrl,	SYS_CLK_EN0,	19);
static MESON_GATE(i2c_m_d,	SYS_CLK_EN0,	20);
static MESON_GATE(i2c_m_c,	SYS_CLK_EN0,	21);
static MESON_GATE(i2c_m_b,	SYS_CLK_EN0,	22);
static MESON_GATE(i2c_m_a,	SYS_CLK_EN0,	23);
static MESON_GATE(acodec,	SYS_CLK_EN0,	24);
static MESON_GATE(otp,		SYS_CLK_EN0,	25);
static MESON_GATE(sd_emmc_a,	SYS_CLK_EN0,	26);
static MESON_GATE(usb_phy,	SYS_CLK_EN0,	27);
static MESON_GATE(usb_ctrl,	SYS_CLK_EN0,	28);
static MESON_GATE(sys_dspb,	SYS_CLK_EN0,	29);
static MESON_GATE(sys_dspa,	SYS_CLK_EN0,	30);
static MESON_GATE(dma,		SYS_CLK_EN0,	31);
static MESON_GATE(irq_ctrl,	SYS_CLK_EN1,	0);
static MESON_GATE(nic,		SYS_CLK_EN1,	1);
static MESON_GATE(gic,		SYS_CLK_EN1,	2);
static MESON_GATE(uart_c,	SYS_CLK_EN1,	3);
static MESON_GATE(uart_b,	SYS_CLK_EN1,	4);
static MESON_GATE(uart_a,	SYS_CLK_EN1,	5);
static MESON_GATE(sys_psram,	SYS_CLK_EN1,	6);
static MESON_GATE(rsa,		SYS_CLK_EN1,	8);
static MESON_GATE(coresight,	SYS_CLK_EN1,	9);
static MESON_GATE(am2axi_vad,	AXI_CLK_EN,	0);
static MESON_GATE(audio_vad,	AXI_CLK_EN,	1);
static MESON_GATE(axi_dmc,	AXI_CLK_EN,	3);
static MESON_GATE(axi_psram,	AXI_CLK_EN,	4);
static MESON_GATE(ramb,		AXI_CLK_EN,	5);
static MESON_GATE(rama,		AXI_CLK_EN,	6);
static MESON_GATE(axi_spifc,	AXI_CLK_EN,	7);
static MESON_GATE(axi_nic,	AXI_CLK_EN,	8);
static MESON_GATE(axi_dma,	AXI_CLK_EN,	9);
static MESON_GATE(cpu_ctrl,	AXI_CLK_EN,	10);
static MESON_GATE(rom,		AXI_CLK_EN,	11);
static MESON_GATE(prod_i2c,	AXI_CLK_EN,	12);

/* Array of all clocks registered by this provider */
static struct clk_hw_onecell_data a1_periphs_clks = {
	.hws = {
		[CLKID_XTAL_IN]			= &xtal_in.hw,
		[CLKID_FIXPLL_IN]		= &fixpll_in.hw,
		[CLKID_USB_PHY_IN]		= &usb_phy_in.hw,
		[CLKID_USB_CTRL_IN]		= &usb_ctrl_in.hw,
		[CLKID_HIFIPLL_IN]		= &hifipll_in.hw,
		[CLKID_SYSPLL_IN]		= &syspll_in.hw,
		[CLKID_DDS_IN]			= &dds_in.hw,
		[CLKID_SYS]			= &sys.hw,
		[CLKID_CLKTREE]			= &clktree.hw,
		[CLKID_RESET_CTRL]		= &reset_ctrl.hw,
		[CLKID_ANALOG_CTRL]		= &analog_ctrl.hw,
		[CLKID_PWR_CTRL]		= &pwr_ctrl.hw,
		[CLKID_PAD_CTRL]		= &pad_ctrl.hw,
		[CLKID_SYS_CTRL]		= &sys_ctrl.hw,
		[CLKID_TEMP_SENSOR]		= &temp_sensor.hw,
		[CLKID_AM2AXI_DIV]		= &am2axi_dev.hw,
		[CLKID_SPICC_B]			= &spicc_b.hw,
		[CLKID_SPICC_A]			= &spicc_a.hw,
		[CLKID_MSR]			= &msr.hw,
		[CLKID_AUDIO]			= &audio.hw,
		[CLKID_JTAG_CTRL]		= &jtag_ctrl.hw,
		[CLKID_SARADC_EN]		= &saradc_en.hw,
		[CLKID_PWM_EF]			= &pwm_ef.hw,
		[CLKID_PWM_CD]			= &pwm_cd.hw,
		[CLKID_PWM_AB]			= &pwm_ab.hw,
		[CLKID_CEC]			= &cec.hw,
		[CLKID_I2C_S]			= &i2c_s.hw,
		[CLKID_IR_CTRL]			= &ir_ctrl.hw,
		[CLKID_I2C_M_D]			= &i2c_m_d.hw,
		[CLKID_I2C_M_C]			= &i2c_m_c.hw,
		[CLKID_I2C_M_B]			= &i2c_m_b.hw,
		[CLKID_I2C_M_A]			= &i2c_m_a.hw,
		[CLKID_ACODEC]			= &acodec.hw,
		[CLKID_OTP]			= &otp.hw,
		[CLKID_SD_EMMC_A]		= &sd_emmc_a.hw,
		[CLKID_USB_PHY]			= &usb_phy.hw,
		[CLKID_USB_CTRL]		= &usb_ctrl.hw,
		[CLKID_SYS_DSPB]		= &sys_dspb.hw,
		[CLKID_SYS_DSPA]		= &sys_dspa.hw,
		[CLKID_DMA]			= &dma.hw,
		[CLKID_IRQ_CTRL]		= &irq_ctrl.hw,
		[CLKID_NIC]			= &nic.hw,
		[CLKID_GIC]			= &gic.hw,
		[CLKID_UART_C]			= &uart_c.hw,
		[CLKID_UART_B]			= &uart_b.hw,
		[CLKID_UART_A]			= &uart_a.hw,
		[CLKID_SYS_PSRAM]		= &sys_psram.hw,
		[CLKID_RSA]			= &rsa.hw,
		[CLKID_CORESIGHT]		= &coresight.hw,
		[CLKID_AM2AXI_VAD]		= &am2axi_vad.hw,
		[CLKID_AUDIO_VAD]		= &audio_vad.hw,
		[CLKID_AXI_DMC]			= &axi_dmc.hw,
		[CLKID_AXI_PSRAM]		= &axi_psram.hw,
		[CLKID_RAMB]			= &ramb.hw,
		[CLKID_RAMA]			= &rama.hw,
		[CLKID_AXI_SPIFC]		= &axi_spifc.hw,
		[CLKID_AXI_NIC]			= &axi_nic.hw,
		[CLKID_AXI_DMA]			= &axi_dma.hw,
		[CLKID_CPU_CTRL]		= &cpu_ctrl.hw,
		[CLKID_ROM]			= &rom.hw,
		[CLKID_PROC_I2C]		= &prod_i2c.hw,
		[CLKID_DSPA_SEL]		= &dspa_sel.hw,
		[CLKID_DSPB_SEL]		= &dspb_sel.hw,
		[CLKID_DSPA_EN]			= &dspa_en.hw,
		[CLKID_DSPA_EN_NIC]		= &dspa_en_nic.hw,
		[CLKID_DSPB_EN]			= &dspb_en.hw,
		[CLKID_DSPB_EN_NIC]		= &dspb_en_nic.hw,
		[CLKID_RTC]			= &rtc.hw,
		[CLKID_CECA_32K]		= &ceca_32k_out.hw,
		[CLKID_CECB_32K]		= &cecb_32k_out.hw,
		[CLKID_24M]			= &clk_24m.hw,
		[CLKID_12M]			= &clk_12m.hw,
		[CLKID_FCLK_DIV2_DIVN]		= &fclk_div2_divn.hw,
		[CLKID_GEN]			= &gen.hw,
		[CLKID_SARADC_SEL]		= &saradc_sel.hw,
		[CLKID_SARADC]			= &saradc.hw,
		[CLKID_PWM_A]			= &pwm_a.hw,
		[CLKID_PWM_B]			= &pwm_b.hw,
		[CLKID_PWM_C]			= &pwm_c.hw,
		[CLKID_PWM_D]			= &pwm_d.hw,
		[CLKID_PWM_E]			= &pwm_e.hw,
		[CLKID_PWM_F]			= &pwm_f.hw,
		[CLKID_SPICC]			= &spicc.hw,
		[CLKID_TS]			= &ts.hw,
		[CLKID_SPIFC]			= &spifc.hw,
		[CLKID_USB_BUS]			= &usb_bus.hw,
		[CLKID_SD_EMMC]			= &sd_emmc.hw,
		[CLKID_PSRAM]			= &psram.hw,
		[CLKID_DMC]			= &dmc.hw,
		[CLKID_SYS_A_SEL]		= &sys_a_sel.hw,
		[CLKID_SYS_A_DIV]		= &sys_a_div.hw,
		[CLKID_SYS_A]			= &sys_a.hw,
		[CLKID_SYS_B_SEL]		= &sys_b_sel.hw,
		[CLKID_SYS_B_DIV]		= &sys_b_div.hw,
		[CLKID_SYS_B]			= &sys_b.hw,
		[CLKID_DSPA_A_SEL]		= &dspa_a_sel.hw,
		[CLKID_DSPA_A_DIV]		= &dspa_a_div.hw,
		[CLKID_DSPA_A]			= &dspa_a.hw,
		[CLKID_DSPA_B_SEL]		= &dspa_b_sel.hw,
		[CLKID_DSPA_B_DIV]		= &dspa_b_div.hw,
		[CLKID_DSPA_B]			= &dspa_b.hw,
		[CLKID_DSPB_A_SEL]		= &dspb_a_sel.hw,
		[CLKID_DSPB_A_DIV]		= &dspb_a_div.hw,
		[CLKID_DSPB_A]			= &dspb_a.hw,
		[CLKID_DSPB_B_SEL]		= &dspb_b_sel.hw,
		[CLKID_DSPB_B_DIV]		= &dspb_b_div.hw,
		[CLKID_DSPB_B]			= &dspb_b.hw,
		[CLKID_RTC_32K_IN]		= &rtc_32k_in.hw,
		[CLKID_RTC_32K_DIV]		= &rtc_32k_div.hw,
		[CLKID_RTC_32K_XTAL]		= &rtc_32k_xtal.hw,
		[CLKID_RTC_32K_SEL]		= &rtc_32k_sel.hw,
		[CLKID_CECB_32K_IN]		= &cecb_32k_in.hw,
		[CLKID_CECB_32K_DIV]		= &cecb_32k_div.hw,
		[CLKID_CECB_32K_SEL_PRE]	= &cecb_32k_sel_pre.hw,
		[CLKID_CECB_32K_SEL]		= &cecb_32k_sel.hw,
		[CLKID_CECA_32K_IN]		= &ceca_32k_in.hw,
		[CLKID_CECA_32K_DIV]		= &ceca_32k_div.hw,
		[CLKID_CECA_32K_SEL_PRE]	= &ceca_32k_sel_pre.hw,
		[CLKID_CECA_32K_SEL]		= &ceca_32k_sel.hw,
		[CLKID_DIV2_PRE]		= &fclk_div2_divn_pre.hw,
		[CLKID_24M_DIV2]		= &clk_24m_div2.hw,
		[CLKID_GEN_SEL]			= &gen_sel.hw,
		[CLKID_GEN_DIV]			= &gen_div.hw,
		[CLKID_SARADC_DIV]		= &saradc_div.hw,
		[CLKID_PWM_A_SEL]		= &pwm_a_sel.hw,
		[CLKID_PWM_A_DIV]		= &pwm_a_div.hw,
		[CLKID_PWM_B_SEL]		= &pwm_b_sel.hw,
		[CLKID_PWM_B_DIV]		= &pwm_b_div.hw,
		[CLKID_PWM_C_SEL]		= &pwm_c_sel.hw,
		[CLKID_PWM_C_DIV]		= &pwm_c_div.hw,
		[CLKID_PWM_D_SEL]		= &pwm_d_sel.hw,
		[CLKID_PWM_D_DIV]		= &pwm_d_div.hw,
		[CLKID_PWM_E_SEL]		= &pwm_e_sel.hw,
		[CLKID_PWM_E_DIV]		= &pwm_e_div.hw,
		[CLKID_PWM_F_SEL]		= &pwm_f_sel.hw,
		[CLKID_PWM_F_DIV]		= &pwm_f_div.hw,
		[CLKID_SPICC_SEL]		= &spicc_sel.hw,
		[CLKID_SPICC_DIV]		= &spicc_div.hw,
		[CLKID_SPICC_SEL2]		= &spicc_sel2.hw,
		[CLKID_TS_DIV]			= &ts_div.hw,
		[CLKID_SPIFC_SEL]		= &spifc_sel.hw,
		[CLKID_SPIFC_DIV]		= &spifc_div.hw,
		[CLKID_SPIFC_SEL2]		= &spifc_sel2.hw,
		[CLKID_USB_BUS_SEL]		= &usb_bus_sel.hw,
		[CLKID_USB_BUS_DIV]		= &usb_bus_div.hw,
		[CLKID_SD_EMMC_SEL]		= &sd_emmc_sel.hw,
		[CLKID_SD_EMMC_DIV]		= &sd_emmc_div.hw,
		[CLKID_SD_EMMC_SEL2]		= &sd_emmc_sel2.hw,
		[CLKID_PSRAM_SEL]		= &psram_sel.hw,
		[CLKID_PSRAM_DIV]		= &psram_div.hw,
		[CLKID_PSRAM_SEL2]		= &psram_sel2.hw,
		[CLKID_DMC_SEL]			= &dmc_sel.hw,
		[CLKID_DMC_DIV]			= &dmc_div.hw,
		[CLKID_DMC_SEL2]		= &dmc_sel2.hw,
		[NR_CLKS]			= NULL,
	},
	.num = NR_CLKS,
};

/* Convenience table to populate regmap in .probe */
static struct clk_regmap *const a1_periphs_regmaps[] = {
	&xtal_in,
	&fixpll_in,
	&usb_phy_in,
	&usb_ctrl_in,
	&hifipll_in,
	&syspll_in,
	&dds_in,
	&sys,
	&clktree,
	&reset_ctrl,
	&analog_ctrl,
	&pwr_ctrl,
	&pad_ctrl,
	&sys_ctrl,
	&temp_sensor,
	&am2axi_dev,
	&spicc_b,
	&spicc_a,
	&msr,
	&audio,
	&jtag_ctrl,
	&saradc_en,
	&pwm_ef,
	&pwm_cd,
	&pwm_ab,
	&cec,
	&i2c_s,
	&ir_ctrl,
	&i2c_m_d,
	&i2c_m_c,
	&i2c_m_b,
	&i2c_m_a,
	&acodec,
	&otp,
	&sd_emmc_a,
	&usb_phy,
	&usb_ctrl,
	&sys_dspb,
	&sys_dspa,
	&dma,
	&irq_ctrl,
	&nic,
	&gic,
	&uart_c,
	&uart_b,
	&uart_a,
	&sys_psram,
	&rsa,
	&coresight,
	&am2axi_vad,
	&audio_vad,
	&axi_dmc,
	&axi_psram,
	&ramb,
	&rama,
	&axi_spifc,
	&axi_nic,
	&axi_dma,
	&cpu_ctrl,
	&rom,
	&prod_i2c,
	&dspa_sel,
	&dspb_sel,
	&dspa_en,
	&dspa_en_nic,
	&dspb_en,
	&dspb_en_nic,
	&rtc,
	&ceca_32k_out,
	&cecb_32k_out,
	&clk_24m,
	&clk_12m,
	&fclk_div2_divn,
	&gen,
	&saradc_sel,
	&saradc,
	&pwm_a,
	&pwm_b,
	&pwm_c,
	&pwm_d,
	&pwm_e,
	&pwm_f,
	&spicc,
	&ts,
	&spifc,
	&usb_bus,
	&sd_emmc,
	&psram,
	&dmc,
	&sys_a_sel,
	&sys_a_div,
	&sys_a,
	&sys_b_sel,
	&sys_b_div,
	&sys_b,
	&dspa_a_sel,
	&dspa_a_div,
	&dspa_a,
	&dspa_b_sel,
	&dspa_b_div,
	&dspa_b,
	&dspb_a_sel,
	&dspb_a_div,
	&dspb_a,
	&dspb_b_sel,
	&dspb_b_div,
	&dspb_b,
	&rtc_32k_in,
	&rtc_32k_div,
	&rtc_32k_xtal,
	&rtc_32k_sel,
	&cecb_32k_in,
	&cecb_32k_div,
	&cecb_32k_sel_pre,
	&cecb_32k_sel,
	&ceca_32k_in,
	&ceca_32k_div,
	&ceca_32k_sel_pre,
	&ceca_32k_sel,
	&fclk_div2_divn_pre,
	&gen_sel,
	&gen_div,
	&saradc_div,
	&pwm_a_sel,
	&pwm_a_div,
	&pwm_b_sel,
	&pwm_b_div,
	&pwm_c_sel,
	&pwm_c_div,
	&pwm_d_sel,
	&pwm_d_div,
	&pwm_e_sel,
	&pwm_e_div,
	&pwm_f_sel,
	&pwm_f_div,
	&spicc_sel,
	&spicc_div,
	&spicc_sel2,
	&ts_div,
	&spifc_sel,
	&spifc_div,
	&spifc_sel2,
	&usb_bus_sel,
	&usb_bus_div,
	&sd_emmc_sel,
	&sd_emmc_div,
	&sd_emmc_sel2,
	&psram_sel,
	&psram_div,
	&psram_sel2,
	&dmc_sel,
	&dmc_div,
	&dmc_sel2,
};

static struct regmap_config a1_periphs_regmap_cfg = {
	.reg_bits   = 32,
	.val_bits   = 32,
	.reg_stride = 4,
};

static int meson_a1_periphs_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	void __iomem *base;
	struct regmap *map;
	int clkid, i, err;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return dev_err_probe(dev, PTR_ERR(base),
				     "can't ioremap resource\n");

	map = devm_regmap_init_mmio(dev, base, &a1_periphs_regmap_cfg);
	if (IS_ERR(map))
		return dev_err_probe(dev, PTR_ERR(map),
				     "can't init regmap mmio region\n");

	/* Populate regmap for the regmap backed clocks */
	for (i = 0; i < ARRAY_SIZE(a1_periphs_regmaps); i++)
		a1_periphs_regmaps[i]->map = map;

	for (clkid = 0; clkid < a1_periphs_clks.num; clkid++) {
		err = devm_clk_hw_register(dev, a1_periphs_clks.hws[clkid]);
		if (err)
			return dev_err_probe(dev, err,
					     "clock[%d] registration failed\n",
					     clkid);
	}

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
					   &a1_periphs_clks);
}

static const struct of_device_id a1_periphs_clkc_match_table[] = {
	{ .compatible = "amlogic,a1-peripherals-clkc", },
	{}
};
MODULE_DEVICE_TABLE(of, a1_periphs_clkc_match_table);

static struct platform_driver a1_periphs_clkc_driver = {
	.probe = meson_a1_periphs_probe,
	.driver = {
		.name = "a1-peripherals-clkc",
		.of_match_table = a1_periphs_clkc_match_table,
	},
};

module_platform_driver(a1_periphs_clkc_driver);
MODULE_AUTHOR("Jian Hu <jian.hu@amlogic.com>");
MODULE_AUTHOR("Dmitry Rokosov <ddrokosov@sberdevices.ru>");
MODULE_LICENSE("GPL");
