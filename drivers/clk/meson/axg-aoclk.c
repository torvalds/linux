// SPDX-License-Identifier: GPL-2.0+
/*
 * Amlogic Meson-AXG Clock Controller Driver
 *
 * Copyright (c) 2016 Baylibre SAS.
 * Author: Michael Turquette <mturquette@baylibre.com>
 *
 * Copyright (c) 2018 Amlogic, inc.
 * Author: Qiufang Dai <qiufang.dai@amlogic.com>
 */
#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include "meson-aoclk.h"

#include "clk-regmap.h"
#include "clk-dualdiv.h"

#include <dt-bindings/clock/axg-aoclkc.h>
#include <dt-bindings/reset/axg-aoclkc.h>

/*
 * AO Configuration Clock registers offsets
 * Register offsets from the data sheet must be multiplied by 4.
 */
#define AO_RTI_PWR_CNTL_REG1	0x0C
#define AO_RTI_PWR_CNTL_REG0	0x10
#define AO_RTI_GEN_CNTL_REG0	0x40
#define AO_OSCIN_CNTL		0x58
#define AO_CRT_CLK_CNTL1	0x68
#define AO_SAR_CLK		0x90
#define AO_RTC_ALT_CLK_CNTL0	0x94
#define AO_RTC_ALT_CLK_CNTL1	0x98

static const struct clk_parent_data axg_ao_pclk_parents = { .fw_name = "mpeg-clk" };

#define AXG_AO_GATE(_name, _bit, _flags)		       \
	MESON_PCLK(axg_ao_##_name, AO_RTI_GEN_CNTL_REG0, _bit, \
		   &axg_ao_pclk_parents, _flags)

static AXG_AO_GATE(remote,	0, CLK_IGNORE_UNUSED);
static AXG_AO_GATE(i2c_master,	1, CLK_IGNORE_UNUSED);
static AXG_AO_GATE(i2c_slave,	2, CLK_IGNORE_UNUSED);
static AXG_AO_GATE(uart1,	3, CLK_IGNORE_UNUSED);
static AXG_AO_GATE(uart2,	5, CLK_IGNORE_UNUSED);
static AXG_AO_GATE(ir_blaster,	6, CLK_IGNORE_UNUSED);
static AXG_AO_GATE(saradc,	7, CLK_IGNORE_UNUSED);

static struct clk_regmap axg_ao_cts_oscin = {
	.data = &(struct clk_regmap_gate_data){
		.offset = AO_RTI_PWR_CNTL_REG0,
		.bit_idx = 14,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cts_oscin",
		.ops = &clk_regmap_gate_ro_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap axg_ao_32k_pre = {
	.data = &(struct clk_regmap_gate_data){
		.offset = AO_RTC_ALT_CLK_CNTL0,
		.bit_idx = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "axg_ao_32k_pre",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&axg_ao_cts_oscin.hw
		},
		.num_parents = 1,
	},
};

static const struct meson_clk_dualdiv_param axg_32k_div_table[] = {
	{
		.dual	= 1,
		.n1	= 733,
		.m1	= 8,
		.n2	= 732,
		.m2	= 11,
	}, {}
};

static struct clk_regmap axg_ao_32k_div = {
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
		.table = axg_32k_div_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "axg_ao_32k_div",
		.ops = &meson_clk_dualdiv_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&axg_ao_32k_pre.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap axg_ao_32k_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = AO_RTC_ALT_CLK_CNTL1,
		.mask = 0x1,
		.shift = 24,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "axg_ao_32k_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&axg_ao_32k_div.hw,
			&axg_ao_32k_pre.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap axg_ao_32k = {
	.data = &(struct clk_regmap_gate_data){
		.offset = AO_RTC_ALT_CLK_CNTL0,
		.bit_idx = 30,
	},
	.hw.init = &(struct clk_init_data){
		.name = "axg_ao_32k",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&axg_ao_32k_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap axg_ao_cts_rtc_oscin = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = AO_RTI_PWR_CNTL_REG0,
		.mask = 0x1,
		.shift = 10,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "axg_ao_cts_rtc_oscin",
		.ops = &clk_regmap_mux_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .hw = &axg_ao_32k.hw },
			{ .fw_name = "ext_32k-0", },
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap axg_ao_clk81 = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = AO_RTI_PWR_CNTL_REG0,
		.mask = 0x1,
		.shift = 8,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		/*
		 * NOTE: this is one of the infamous clock the pwm driver
		 * can request directly by its global name. It's wrong but
		 * there is not much we can do about it until the support
		 * for the old pwm bindings is dropped
		 */
		.name = "axg_ao_clk81",
		.ops = &clk_regmap_mux_ro_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .fw_name = "mpeg-clk", },
			{ .hw = &axg_ao_cts_rtc_oscin.hw },
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap axg_ao_saradc_mux = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = AO_SAR_CLK,
		.mask = 0x3,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_saradc_mux",
		.ops = &clk_regmap_mux_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .fw_name = "xtal", },
			{ .hw = &axg_ao_clk81.hw },
		},
		.num_parents = 2,
	},
};

static struct clk_regmap axg_ao_saradc_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = AO_SAR_CLK,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_saradc_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&axg_ao_saradc_mux.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap axg_ao_saradc_gate = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = AO_SAR_CLK,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_saradc_gate",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&axg_ao_saradc_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const unsigned int axg_ao_reset[] = {
	[RESET_AO_REMOTE]	= 16,
	[RESET_AO_I2C_MASTER]	= 18,
	[RESET_AO_I2C_SLAVE]	= 19,
	[RESET_AO_UART1]	= 17,
	[RESET_AO_UART2]	= 22,
	[RESET_AO_IR_BLASTER]	= 23,
};

static struct clk_hw *axg_ao_hw_clks[] = {
	[CLKID_AO_REMOTE]	= &axg_ao_remote.hw,
	[CLKID_AO_I2C_MASTER]	= &axg_ao_i2c_master.hw,
	[CLKID_AO_I2C_SLAVE]	= &axg_ao_i2c_slave.hw,
	[CLKID_AO_UART1]	= &axg_ao_uart1.hw,
	[CLKID_AO_UART2]	= &axg_ao_uart2.hw,
	[CLKID_AO_IR_BLASTER]	= &axg_ao_ir_blaster.hw,
	[CLKID_AO_SAR_ADC]	= &axg_ao_saradc.hw,
	[CLKID_AO_CLK81]	= &axg_ao_clk81.hw,
	[CLKID_AO_SAR_ADC_SEL]	= &axg_ao_saradc_mux.hw,
	[CLKID_AO_SAR_ADC_DIV]	= &axg_ao_saradc_div.hw,
	[CLKID_AO_SAR_ADC_CLK]	= &axg_ao_saradc_gate.hw,
	[CLKID_AO_CTS_OSCIN]	= &axg_ao_cts_oscin.hw,
	[CLKID_AO_32K_PRE]	= &axg_ao_32k_pre.hw,
	[CLKID_AO_32K_DIV]	= &axg_ao_32k_div.hw,
	[CLKID_AO_32K_SEL]	= &axg_ao_32k_sel.hw,
	[CLKID_AO_32K]		= &axg_ao_32k.hw,
	[CLKID_AO_CTS_RTC_OSCIN] = &axg_ao_cts_rtc_oscin.hw,
};

static const struct meson_aoclk_data axg_ao_clkc_data = {
	.reset_reg	= AO_RTI_GEN_CNTL_REG0,
	.num_reset	= ARRAY_SIZE(axg_ao_reset),
	.reset		= axg_ao_reset,
	.clkc_data	= {
		.hw_clks = {
			.hws	= axg_ao_hw_clks,
			.num	= ARRAY_SIZE(axg_ao_hw_clks),
		},
	},
};

static const struct of_device_id axg_ao_clkc_match_table[] = {
	{
		.compatible	= "amlogic,meson-axg-aoclkc",
		.data		= &axg_ao_clkc_data.clkc_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, axg_ao_clkc_match_table);

static struct platform_driver axg_ao_clkc_driver = {
	.probe		= meson_aoclkc_probe,
	.driver		= {
		.name	= "axg-ao-clkc",
		.of_match_table = axg_ao_clkc_match_table,
	},
};
module_platform_driver(axg_ao_clkc_driver);

MODULE_DESCRIPTION("Amlogic AXG Always-ON Clock Controller driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("CLK_MESON");
