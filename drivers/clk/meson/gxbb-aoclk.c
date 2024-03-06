// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Copyright (c) 2016 BayLibre, SAS.
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include "meson-aoclk.h"

#include "clk-regmap.h"
#include "clk-dualdiv.h"

#include <dt-bindings/clock/gxbb-aoclkc.h>
#include <dt-bindings/reset/gxbb-aoclkc.h>

/* AO Configuration Clock registers offsets */
#define AO_RTI_PWR_CNTL_REG1	0x0c
#define AO_RTI_PWR_CNTL_REG0	0x10
#define AO_RTI_GEN_CNTL_REG0	0x40
#define AO_OSCIN_CNTL		0x58
#define AO_CRT_CLK_CNTL1	0x68
#define AO_RTC_ALT_CLK_CNTL0	0x94
#define AO_RTC_ALT_CLK_CNTL1	0x98

#define GXBB_AO_GATE(_name, _bit)					\
static struct clk_regmap _name##_ao = {					\
	.data = &(struct clk_regmap_gate_data) {			\
		.offset = AO_RTI_GEN_CNTL_REG0,				\
		.bit_idx = (_bit),					\
	},								\
	.hw.init = &(struct clk_init_data) {				\
		.name = #_name "_ao",					\
		.ops = &clk_regmap_gate_ops,				\
		.parent_data = &(const struct clk_parent_data) {	\
			.fw_name = "mpeg-clk",				\
		},							\
		.num_parents = 1,					\
		.flags = CLK_IGNORE_UNUSED,				\
	},								\
}

GXBB_AO_GATE(remote, 0);
GXBB_AO_GATE(i2c_master, 1);
GXBB_AO_GATE(i2c_slave, 2);
GXBB_AO_GATE(uart1, 3);
GXBB_AO_GATE(uart2, 5);
GXBB_AO_GATE(ir_blaster, 6);

static struct clk_regmap ao_cts_oscin = {
	.data = &(struct clk_regmap_gate_data){
		.offset = AO_RTI_PWR_CNTL_REG0,
		.bit_idx = 6,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_cts_oscin",
		.ops = &clk_regmap_gate_ro_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap ao_32k_pre = {
	.data = &(struct clk_regmap_gate_data){
		.offset = AO_RTC_ALT_CLK_CNTL0,
		.bit_idx = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_32k_pre",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &ao_cts_oscin.hw },
		.num_parents = 1,
	},
};

static const struct meson_clk_dualdiv_param gxbb_32k_div_table[] = {
	{
		.dual	= 1,
		.n1	= 733,
		.m1	= 8,
		.n2	= 732,
		.m2	= 11,
	}, {}
};

static struct clk_regmap ao_32k_div = {
	.data = &(struct meson_clk_dualdiv_data){
		.n1 = {
			.reg_off = AO_RTC_ALT_CLK_CNTL0,
			.shift   = 0,
			.width   = 12,
		},
		.n2 = {
			.reg_off = AO_RTC_ALT_CLK_CNTL0,
			.shift   = 12,
			.width   = 12,
		},
		.m1 = {
			.reg_off = AO_RTC_ALT_CLK_CNTL1,
			.shift   = 0,
			.width   = 12,
		},
		.m2 = {
			.reg_off = AO_RTC_ALT_CLK_CNTL1,
			.shift   = 12,
			.width   = 12,
		},
		.dual = {
			.reg_off = AO_RTC_ALT_CLK_CNTL0,
			.shift   = 28,
			.width   = 1,
		},
		.table = gxbb_32k_div_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_32k_div",
		.ops = &meson_clk_dualdiv_ops,
		.parent_hws = (const struct clk_hw *[]) { &ao_32k_pre.hw },
		.num_parents = 1,
	},
};

static struct clk_regmap ao_32k_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = AO_RTC_ALT_CLK_CNTL1,
		.mask = 0x1,
		.shift = 24,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_32k_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&ao_32k_div.hw,
			&ao_32k_pre.hw
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap ao_32k = {
	.data = &(struct clk_regmap_gate_data){
		.offset = AO_RTC_ALT_CLK_CNTL0,
		.bit_idx = 30,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_32k",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &ao_32k_sel.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap ao_cts_rtc_oscin = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = AO_RTI_PWR_CNTL_REG0,
		.mask = 0x7,
		.shift = 10,
		.table = (u32[]){ 1, 2, 3, 4 },
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_cts_rtc_oscin",
		.ops = &clk_regmap_mux_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .fw_name = "ext-32k-0", },
			{ .fw_name = "ext-32k-1", },
			{ .fw_name = "ext-32k-2", },
			{ .hw = &ao_32k.hw },
		},
		.num_parents = 4,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap ao_clk81 = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = AO_RTI_PWR_CNTL_REG0,
		.mask = 0x1,
		.shift = 0,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_clk81",
		.ops = &clk_regmap_mux_ro_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .fw_name = "mpeg-clk", },
			{ .hw = &ao_cts_rtc_oscin.hw },
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap ao_cts_cec = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = AO_CRT_CLK_CNTL1,
		.mask = 0x1,
		.shift = 27,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_cts_cec",
		.ops = &clk_regmap_mux_ops,
		/*
		 * FIXME: The 'fixme' parent obviously does not exist.
		 *
		 * ATM, CCF won't call get_parent() if num_parents is 1. It
		 * does not allow NULL as a parent name either.
		 *
		 * On this particular mux, we only know the input #1 parent
		 * but, on boot, unknown input #0 is set, so it is critical
		 * to call .get_parent() on it
		 *
		 * Until CCF gets fixed, adding this fake parent that won't
		 * ever be registered should work around the problem
		 */
		.parent_data = (const struct clk_parent_data []) {
			{ .name = "fixme", .index = -1, },
			{ .hw = &ao_cts_rtc_oscin.hw },
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const unsigned int gxbb_aoclk_reset[] = {
	[RESET_AO_REMOTE] = 16,
	[RESET_AO_I2C_MASTER] = 18,
	[RESET_AO_I2C_SLAVE] = 19,
	[RESET_AO_UART1] = 17,
	[RESET_AO_UART2] = 22,
	[RESET_AO_IR_BLASTER] = 23,
};

static struct clk_regmap *gxbb_aoclk[] = {
	&remote_ao,
	&i2c_master_ao,
	&i2c_slave_ao,
	&uart1_ao,
	&uart2_ao,
	&ir_blaster_ao,
	&ao_cts_oscin,
	&ao_32k_pre,
	&ao_32k_div,
	&ao_32k_sel,
	&ao_32k,
	&ao_cts_rtc_oscin,
	&ao_clk81,
	&ao_cts_cec,
};

static struct clk_hw *gxbb_aoclk_hw_clks[] = {
		[CLKID_AO_REMOTE] = &remote_ao.hw,
		[CLKID_AO_I2C_MASTER] = &i2c_master_ao.hw,
		[CLKID_AO_I2C_SLAVE] = &i2c_slave_ao.hw,
		[CLKID_AO_UART1] = &uart1_ao.hw,
		[CLKID_AO_UART2] = &uart2_ao.hw,
		[CLKID_AO_IR_BLASTER] = &ir_blaster_ao.hw,
		[CLKID_AO_CEC_32K] = &ao_cts_cec.hw,
		[CLKID_AO_CTS_OSCIN] = &ao_cts_oscin.hw,
		[CLKID_AO_32K_PRE] = &ao_32k_pre.hw,
		[CLKID_AO_32K_DIV] = &ao_32k_div.hw,
		[CLKID_AO_32K_SEL] = &ao_32k_sel.hw,
		[CLKID_AO_32K] = &ao_32k.hw,
		[CLKID_AO_CTS_RTC_OSCIN] = &ao_cts_rtc_oscin.hw,
		[CLKID_AO_CLK81] = &ao_clk81.hw,
};

static const struct meson_aoclk_data gxbb_aoclkc_data = {
	.reset_reg	= AO_RTI_GEN_CNTL_REG0,
	.num_reset	= ARRAY_SIZE(gxbb_aoclk_reset),
	.reset		= gxbb_aoclk_reset,
	.num_clks	= ARRAY_SIZE(gxbb_aoclk),
	.clks		= gxbb_aoclk,
	.hw_clks	= {
		.hws	= gxbb_aoclk_hw_clks,
		.num	= ARRAY_SIZE(gxbb_aoclk_hw_clks),
	},
};

static const struct of_device_id gxbb_aoclkc_match_table[] = {
	{
		.compatible	= "amlogic,meson-gx-aoclkc",
		.data		= &gxbb_aoclkc_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, gxbb_aoclkc_match_table);

static struct platform_driver gxbb_aoclkc_driver = {
	.probe		= meson_aoclkc_probe,
	.driver		= {
		.name	= "gxbb-aoclkc",
		.of_match_table = gxbb_aoclkc_match_table,
	},
};
module_platform_driver(gxbb_aoclkc_driver);
MODULE_LICENSE("GPL v2");
