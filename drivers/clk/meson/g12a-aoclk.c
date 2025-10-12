// SPDX-License-Identifier: GPL-2.0+
/*
 * Amlogic Meson-AXG Clock Controller Driver
 *
 * Copyright (c) 2016 Baylibre SAS.
 * Author: Michael Turquette <mturquette@baylibre.com>
 *
 * Copyright (c) 2019 Baylibre SAS.
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */
#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include "meson-aoclk.h"

#include "clk-regmap.h"
#include "clk-dualdiv.h"

#include <dt-bindings/clock/g12a-aoclkc.h>
#include <dt-bindings/reset/g12a-aoclkc.h>

/*
 * AO Configuration Clock registers offsets
 * Register offsets from the data sheet must be multiplied by 4.
 */
#define AO_RTI_STATUS_REG3	0x0C
#define AO_RTI_PWR_CNTL_REG0	0x10
#define AO_RTI_GEN_CNTL_REG0	0x40
#define AO_CLK_GATE0		0x4c
#define AO_CLK_GATE0_SP		0x50
#define AO_OSCIN_CNTL		0x58
#define AO_CEC_CLK_CNTL_REG0	0x74
#define AO_CEC_CLK_CNTL_REG1	0x78
#define AO_SAR_CLK		0x90
#define AO_RTC_ALT_CLK_CNTL0	0x94
#define AO_RTC_ALT_CLK_CNTL1	0x98

static const struct clk_parent_data g12a_ao_pclk_parents = { .fw_name = "mpeg-clk" };

#define G12A_AO_PCLK(_name, _reg, _bit, _flags) \
	MESON_PCLK(g12a_ao_##_name, _reg, _bit, &g12a_ao_pclk_parents, _flags)

/*
 * NOTE: The gates below are marked with CLK_IGNORE_UNUSED for historic reasons
 * Users are encouraged to test without it and submit changes to:
 *  - remove the flag if not necessary
 *  - replace the flag with something more adequate, such as CLK_IS_CRITICAL,
 *    if appropriate.
 *  - add a comment explaining why the use of CLK_IGNORE_UNUSED is desirable
 *    for a particular clock.
 */
static G12A_AO_PCLK(ahb,	AO_CLK_GATE0,    0, CLK_IGNORE_UNUSED);
static G12A_AO_PCLK(ir_in,	AO_CLK_GATE0,    1, CLK_IGNORE_UNUSED);
static G12A_AO_PCLK(i2c_m0,	AO_CLK_GATE0,    2, CLK_IGNORE_UNUSED);
static G12A_AO_PCLK(i2c_s0,	AO_CLK_GATE0,    3, CLK_IGNORE_UNUSED);
static G12A_AO_PCLK(uart,	AO_CLK_GATE0,    4, CLK_IGNORE_UNUSED);
static G12A_AO_PCLK(prod_i2c,	AO_CLK_GATE0,    5, CLK_IGNORE_UNUSED);
static G12A_AO_PCLK(uart2,	AO_CLK_GATE0,    6, CLK_IGNORE_UNUSED);
static G12A_AO_PCLK(ir_out,	AO_CLK_GATE0,    7, CLK_IGNORE_UNUSED);
static G12A_AO_PCLK(saradc,	AO_CLK_GATE0,    8, CLK_IGNORE_UNUSED);

static G12A_AO_PCLK(mailbox,	AO_CLK_GATE0_SP, 0, CLK_IGNORE_UNUSED);
static G12A_AO_PCLK(m3,		AO_CLK_GATE0_SP, 1, CLK_IGNORE_UNUSED);
static G12A_AO_PCLK(ahb_sram,	AO_CLK_GATE0_SP, 2, CLK_IGNORE_UNUSED);
static G12A_AO_PCLK(rti,	AO_CLK_GATE0_SP, 3, CLK_IGNORE_UNUSED);
static G12A_AO_PCLK(m4_fclk,	AO_CLK_GATE0_SP, 4, CLK_IGNORE_UNUSED);
static G12A_AO_PCLK(m4_hclk,	AO_CLK_GATE0_SP, 5, CLK_IGNORE_UNUSED);

static struct clk_regmap g12a_ao_cts_oscin = {
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

static const struct meson_clk_dualdiv_param g12a_32k_div_table[] = {
	{
		.dual	= 1,
		.n1	= 733,
		.m1	= 8,
		.n2	= 732,
		.m2	= 11,
	}, {}
};

/* 32k_by_oscin clock */

static struct clk_regmap g12a_ao_32k_by_oscin_pre = {
	.data = &(struct clk_regmap_gate_data){
		.offset = AO_RTC_ALT_CLK_CNTL0,
		.bit_idx = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_32k_by_oscin_pre",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&g12a_ao_cts_oscin.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap g12a_ao_32k_by_oscin_div = {
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
		.table = g12a_32k_div_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_32k_by_oscin_div",
		.ops = &meson_clk_dualdiv_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&g12a_ao_32k_by_oscin_pre.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap g12a_ao_32k_by_oscin_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = AO_RTC_ALT_CLK_CNTL1,
		.mask = 0x1,
		.shift = 24,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_32k_by_oscin_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&g12a_ao_32k_by_oscin_div.hw,
			&g12a_ao_32k_by_oscin_pre.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap g12a_ao_32k_by_oscin = {
	.data = &(struct clk_regmap_gate_data){
		.offset = AO_RTC_ALT_CLK_CNTL0,
		.bit_idx = 30,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_32k_by_oscin",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&g12a_ao_32k_by_oscin_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

/* cec clock */

static struct clk_regmap g12a_ao_cec_pre = {
	.data = &(struct clk_regmap_gate_data){
		.offset = AO_CEC_CLK_CNTL_REG0,
		.bit_idx = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_cec_pre",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&g12a_ao_cts_oscin.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap g12a_ao_cec_div = {
	.data = &(struct meson_clk_dualdiv_data){
		.n1 = {
			.reg_off = AO_CEC_CLK_CNTL_REG0,
			.shift   = 0,
			.width   = 12,
		},
		.n2 = {
			.reg_off = AO_CEC_CLK_CNTL_REG0,
			.shift   = 12,
			.width   = 12,
		},
		.m1 = {
			.reg_off = AO_CEC_CLK_CNTL_REG1,
			.shift   = 0,
			.width   = 12,
		},
		.m2 = {
			.reg_off = AO_CEC_CLK_CNTL_REG1,
			.shift   = 12,
			.width   = 12,
		},
		.dual = {
			.reg_off = AO_CEC_CLK_CNTL_REG0,
			.shift   = 28,
			.width   = 1,
		},
		.table = g12a_32k_div_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_cec_div",
		.ops = &meson_clk_dualdiv_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&g12a_ao_cec_pre.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap g12a_ao_cec_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = AO_CEC_CLK_CNTL_REG1,
		.mask = 0x1,
		.shift = 24,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_cec_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&g12a_ao_cec_div.hw,
			&g12a_ao_cec_pre.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap g12a_ao_cec = {
	.data = &(struct clk_regmap_gate_data){
		.offset = AO_CEC_CLK_CNTL_REG0,
		.bit_idx = 30,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_cec",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&g12a_ao_cec_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap g12a_ao_cts_rtc_oscin = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = AO_RTI_PWR_CNTL_REG0,
		.mask = 0x1,
		.shift = 10,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_cts_rtc_oscin",
		.ops = &clk_regmap_mux_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .hw = &g12a_ao_32k_by_oscin.hw },
			{ .fw_name = "ext-32k-0", },
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap g12a_ao_clk81 = {
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
		.name = "g12a_ao_clk81",
		.ops = &clk_regmap_mux_ro_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .fw_name = "mpeg-clk", },
			{ .hw = &g12a_ao_cts_rtc_oscin.hw },
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap g12a_ao_saradc_mux = {
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
			{ .hw = &g12a_ao_clk81.hw },
		},
		.num_parents = 2,
	},
};

static struct clk_regmap g12a_ao_saradc_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = AO_SAR_CLK,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_saradc_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&g12a_ao_saradc_mux.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap g12a_ao_saradc_gate = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = AO_SAR_CLK,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_saradc_gate",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&g12a_ao_saradc_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const unsigned int g12a_ao_reset[] = {
	[RESET_AO_IR_IN]	= 16,
	[RESET_AO_UART]		= 17,
	[RESET_AO_I2C_M]	= 18,
	[RESET_AO_I2C_S]	= 19,
	[RESET_AO_SAR_ADC]	= 20,
	[RESET_AO_UART2]	= 22,
	[RESET_AO_IR_OUT]	= 23,
};

static struct clk_hw *g12a_ao_hw_clks[] = {
	[CLKID_AO_AHB]		= &g12a_ao_ahb.hw,
	[CLKID_AO_IR_IN]	= &g12a_ao_ir_in.hw,
	[CLKID_AO_I2C_M0]	= &g12a_ao_i2c_m0.hw,
	[CLKID_AO_I2C_S0]	= &g12a_ao_i2c_s0.hw,
	[CLKID_AO_UART]		= &g12a_ao_uart.hw,
	[CLKID_AO_PROD_I2C]	= &g12a_ao_prod_i2c.hw,
	[CLKID_AO_UART2]	= &g12a_ao_uart2.hw,
	[CLKID_AO_IR_OUT]	= &g12a_ao_ir_out.hw,
	[CLKID_AO_SAR_ADC]	= &g12a_ao_saradc.hw,
	[CLKID_AO_MAILBOX]	= &g12a_ao_mailbox.hw,
	[CLKID_AO_M3]		= &g12a_ao_m3.hw,
	[CLKID_AO_AHB_SRAM]	= &g12a_ao_ahb_sram.hw,
	[CLKID_AO_RTI]		= &g12a_ao_rti.hw,
	[CLKID_AO_M4_FCLK]	= &g12a_ao_m4_fclk.hw,
	[CLKID_AO_M4_HCLK]	= &g12a_ao_m4_hclk.hw,
	[CLKID_AO_CLK81]	= &g12a_ao_clk81.hw,
	[CLKID_AO_SAR_ADC_SEL]	= &g12a_ao_saradc_mux.hw,
	[CLKID_AO_SAR_ADC_DIV]	= &g12a_ao_saradc_div.hw,
	[CLKID_AO_SAR_ADC_CLK]	= &g12a_ao_saradc_gate.hw,
	[CLKID_AO_CTS_OSCIN]	= &g12a_ao_cts_oscin.hw,
	[CLKID_AO_32K_PRE]	= &g12a_ao_32k_by_oscin_pre.hw,
	[CLKID_AO_32K_DIV]	= &g12a_ao_32k_by_oscin_div.hw,
	[CLKID_AO_32K_SEL]	= &g12a_ao_32k_by_oscin_sel.hw,
	[CLKID_AO_32K]		= &g12a_ao_32k_by_oscin.hw,
	[CLKID_AO_CEC_PRE]	= &g12a_ao_cec_pre.hw,
	[CLKID_AO_CEC_DIV]	= &g12a_ao_cec_div.hw,
	[CLKID_AO_CEC_SEL]	= &g12a_ao_cec_sel.hw,
	[CLKID_AO_CEC]		= &g12a_ao_cec.hw,
	[CLKID_AO_CTS_RTC_OSCIN] = &g12a_ao_cts_rtc_oscin.hw,
};

static const struct meson_aoclk_data g12a_ao_clkc_data = {
	.reset_reg	= AO_RTI_GEN_CNTL_REG0,
	.num_reset	= ARRAY_SIZE(g12a_ao_reset),
	.reset		= g12a_ao_reset,
	.clkc_data = {
		.hw_clks = {
			.hws	= g12a_ao_hw_clks,
			.num	= ARRAY_SIZE(g12a_ao_hw_clks),
		},
	},
};

static const struct of_device_id g12a_ao_clkc_match_table[] = {
	{
		.compatible	= "amlogic,meson-g12a-aoclkc",
		.data		= &g12a_ao_clkc_data.clkc_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, g12a_ao_clkc_match_table);

static struct platform_driver g12a_ao_clkc_driver = {
	.probe		= meson_aoclkc_probe,
	.driver		= {
		.name	= "g12a-aoclkc",
		.of_match_table = g12a_ao_clkc_match_table,
	},
};
module_platform_driver(g12a_ao_clkc_driver);

MODULE_DESCRIPTION("Amlogic G12A Always-ON Clock Controller driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("CLK_MESON");
