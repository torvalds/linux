// SPDX-License-Identifier: (GPL-2.0-only OR MIT)
/*
 * Copyright (C) 2026 Amlogic, Inc. All rights reserved
 */

#include <dt-bindings/clock/amlogic,a9-aoclkc.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include "clk-regmap.h"
#include "clk-dualdiv.h"
#include "meson-clkc-utils.h"

#define AO_OSCIN_CTRL			0x00
#define AO_SYS_CLK0			0x04
#define AO_PWM_CLK_A_CTRL		0x1c
#define AO_PWM_CLK_B_CTRL		0x20
#define AO_PWM_CLK_C_CTRL		0x24
#define AO_PWM_CLK_D_CTRL		0x28
#define AO_PWM_CLK_E_CTRL		0x2c
#define AO_PWM_CLK_F_CTRL		0x30
#define AO_PWM_CLK_G_CTRL		0x34
#define AO_CEC_CTRL0			0x38
#define AO_CEC_CTRL1			0x3c
#define AO_RTC_BY_OSCIN_CTRL0		0x50
#define AO_RTC_BY_OSCIN_CTRL1		0x54

#define A9_COMP_SEL(_name, _reg, _shift, _mask, _pdata) \
	MESON_COMP_SEL(a9_ao_, _name, _reg, _shift, _mask, _pdata, NULL, 0, 0)

#define A9_COMP_DIV(_name, _reg, _shift, _width) \
	MESON_COMP_DIV(a9_ao_, _name, _reg, _shift, _width, 0, CLK_SET_RATE_PARENT)

#define A9_COMP_GATE(_name, _reg, _bit) \
	MESON_COMP_GATE(a9_ao_, _name, _reg, _bit, CLK_SET_RATE_PARENT)

static struct clk_regmap a9_ao_xtal_in = {
	.data = &(struct clk_regmap_gate_data){
		.offset = AO_OSCIN_CTRL,
		.bit_idx = 3,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "ao_xtal_in",
		.ops = &clk_regmap_gate_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a9_ao_xtal = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = AO_OSCIN_CTRL,
		.mask = 0x1,
		.shift = 0,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_xtal",
		.ops = &clk_regmap_mux_ops,
		/* ext_32k is from external PAD, do not automatically reparent */
		.parent_data = (const struct clk_parent_data []) {
			{ .hw = &a9_ao_xtal_in.hw },
			{ .fw_name = "ext_32k", },
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_NO_REPARENT,
	},
};

static struct clk_regmap a9_ao_sys = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = AO_OSCIN_CTRL,
		.mask = 0x1,
		.shift = 1,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_sys",
		.ops = &clk_regmap_mux_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .hw = &a9_ao_xtal.hw },
			{ .fw_name = "sys", },
		},
		.num_parents = 2,
	},
};

static const struct clk_parent_data a9_ao_pclk_parents = { .hw = &a9_ao_sys.hw };

#define A9_AO_PCLK(_name, _bit, _flags)		       \
	MESON_PCLK(a9_ao_sys_##_name, AO_SYS_CLK0, _bit, \
		   &a9_ao_pclk_parents, _flags)

/*
 * A9 integrates a low-power microprocessor (Always-on CPU: AOCPU). Some AO sys
 * clocks control the AOCPU modules. Mark the AOCPU-related clocks with
 * CLK_IS_CRITICAL to avoid them being disabled and impacting AOCPU functionality.
 * AOCPU-related clocks list:
 * - clktree
 * - rst_ctrl
 * - pad
 * - irq
 * - pwrctrl
 * - aocpu
 * - sram
 */
static A9_AO_PCLK(i3c,		0,	0);
static A9_AO_PCLK(rtc_reg,	1,	0);
static A9_AO_PCLK(clktree,	2,	CLK_IS_CRITICAL);
static A9_AO_PCLK(rst_ctrl,	3,	CLK_IS_CRITICAL);
static A9_AO_PCLK(pad,		4,	CLK_IS_CRITICAL);
static A9_AO_PCLK(rtc_dig,	5,	0);
static A9_AO_PCLK(irq,		6,	CLK_IS_CRITICAL);
static A9_AO_PCLK(pwrctrl,	7,	CLK_IS_CRITICAL);
static A9_AO_PCLK(pwm_a,	8,	0);
static A9_AO_PCLK(pwm_b,	9,	0);
static A9_AO_PCLK(pwm_c,	10,	0);
static A9_AO_PCLK(pwm_d,	11,	0);
static A9_AO_PCLK(pwm_e,	12,	0);
static A9_AO_PCLK(pwm_f,	13,	0);
static A9_AO_PCLK(pwm_g,	14,	0);
static A9_AO_PCLK(i2c_a,	15,	0);
static A9_AO_PCLK(i2c_b,	16,	0);
static A9_AO_PCLK(i2c_c,	17,	0);
static A9_AO_PCLK(i2c_d,	18,	0);
static A9_AO_PCLK(sed,		19,	0);
static A9_AO_PCLK(ir_ctrl,	20,	0);
static A9_AO_PCLK(uart_b,	21,	0);
static A9_AO_PCLK(uart_c,	22,	0);
static A9_AO_PCLK(uart_d,	23,	0);
static A9_AO_PCLK(uart_e,	24,	0);
static A9_AO_PCLK(spisg_0,	25,	0);
static A9_AO_PCLK(rtc_secure,	26,	0);
static A9_AO_PCLK(cec,		27,	0);
static A9_AO_PCLK(aocpu,	28,	CLK_IS_CRITICAL);
static A9_AO_PCLK(sram,		29,	CLK_IS_CRITICAL);
static A9_AO_PCLK(spisg_1,	30,	0);
static A9_AO_PCLK(spisg_2,	31,	0);

static const struct clk_parent_data a9_ao_pwm_parents[] = {
	{ .hw = &a9_ao_xtal.hw },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv3", }
};

static A9_COMP_SEL(pwm_a, AO_PWM_CLK_A_CTRL, 9, 0x7, a9_ao_pwm_parents);
static A9_COMP_DIV(pwm_a, AO_PWM_CLK_A_CTRL, 0, 8);
static A9_COMP_GATE(pwm_a, AO_PWM_CLK_A_CTRL, 8);

static A9_COMP_SEL(pwm_b, AO_PWM_CLK_B_CTRL, 9, 0x7, a9_ao_pwm_parents);
static A9_COMP_DIV(pwm_b, AO_PWM_CLK_B_CTRL, 0, 8);
static A9_COMP_GATE(pwm_b, AO_PWM_CLK_B_CTRL, 8);

static A9_COMP_SEL(pwm_c, AO_PWM_CLK_C_CTRL, 9, 0x7, a9_ao_pwm_parents);
static A9_COMP_DIV(pwm_c, AO_PWM_CLK_C_CTRL, 0, 8);
static A9_COMP_GATE(pwm_c, AO_PWM_CLK_C_CTRL, 8);

static A9_COMP_SEL(pwm_d, AO_PWM_CLK_D_CTRL, 9, 0x7, a9_ao_pwm_parents);
static A9_COMP_DIV(pwm_d, AO_PWM_CLK_D_CTRL, 0, 8);
static A9_COMP_GATE(pwm_d, AO_PWM_CLK_D_CTRL, 8);

static A9_COMP_SEL(pwm_e, AO_PWM_CLK_E_CTRL, 9, 0x7, a9_ao_pwm_parents);
static A9_COMP_DIV(pwm_e, AO_PWM_CLK_E_CTRL, 0, 8);
static A9_COMP_GATE(pwm_e, AO_PWM_CLK_E_CTRL, 8);

static A9_COMP_SEL(pwm_f, AO_PWM_CLK_F_CTRL, 9, 0x7, a9_ao_pwm_parents);
static A9_COMP_DIV(pwm_f, AO_PWM_CLK_F_CTRL, 0, 8);
static A9_COMP_GATE(pwm_f, AO_PWM_CLK_F_CTRL, 8);

static A9_COMP_SEL(pwm_g, AO_PWM_CLK_G_CTRL, 9, 0x7, a9_ao_pwm_parents);
static A9_COMP_DIV(pwm_g, AO_PWM_CLK_G_CTRL, 0, 8);
static A9_COMP_GATE(pwm_g, AO_PWM_CLK_G_CTRL, 8);

static struct clk_regmap a9_ao_rtc_dualdiv_in = {
	.data = &(struct clk_regmap_gate_data){
		.offset = AO_RTC_BY_OSCIN_CTRL0,
		.bit_idx = 31,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "ao_rtc_dualdiv_in",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_ao_xtal.hw
		},
		.num_parents = 1,
	},
};

static const struct meson_clk_dualdiv_param a9_ao_dualdiv_table[] = {
	{ 733, 732, 8, 11, 1 },
	{ /* sentinel */ }
};

static struct clk_regmap a9_ao_rtc_dualdiv_div = {
	.data = &(struct meson_clk_dualdiv_data){
		.n1 = {
			.reg_off = AO_RTC_BY_OSCIN_CTRL0,
			.shift   = 0,
			.width   = 12,
		},
		.n2 = {
			.reg_off = AO_RTC_BY_OSCIN_CTRL0,
			.shift   = 12,
			.width   = 12,
		},
		.m1 = {
			.reg_off = AO_RTC_BY_OSCIN_CTRL1,
			.shift   = 0,
			.width   = 12,
		},
		.m2 = {
			.reg_off = AO_RTC_BY_OSCIN_CTRL1,
			.shift   = 12,
			.width   = 12,
		},
		.dual = {
			.reg_off = AO_RTC_BY_OSCIN_CTRL0,
			.shift   = 28,
			.width   = 1,
		},
		.table = a9_ao_dualdiv_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "a9_ao_rtc_dualdiv_div",
		.ops = &meson_clk_dualdiv_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_ao_rtc_dualdiv_in.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a9_ao_rtc_dualdiv_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = AO_RTC_BY_OSCIN_CTRL1,
		.mask = 0x1,
		.shift = 24,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_rtc_dualdiv_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_ao_rtc_dualdiv_div.hw,
			&a9_ao_rtc_dualdiv_in.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a9_ao_rtc_dualdiv = {
	.data = &(struct clk_regmap_gate_data){
		.offset = AO_RTC_BY_OSCIN_CTRL0,
		.bit_idx = 30,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "ao_rtc_dualdiv",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_ao_rtc_dualdiv_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a9_ao_rtc = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = AO_RTC_BY_OSCIN_CTRL1,
		.mask = 0x1,
		.shift = 30,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_rtc",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_ao_xtal.hw,
			&a9_ao_rtc_dualdiv.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a9_ao_cec_dualdiv_in = {
	.data = &(struct clk_regmap_gate_data){
		.offset = AO_CEC_CTRL0,
		.bit_idx = 31,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "ao_cec_dualdiv_in",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_ao_xtal.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a9_ao_cec_dualdiv_div = {
	.data = &(struct meson_clk_dualdiv_data){
		.n1 = {
			.reg_off = AO_CEC_CTRL0,
			.shift   = 0,
			.width   = 12,
		},
		.n2 = {
			.reg_off = AO_CEC_CTRL0,
			.shift   = 12,
			.width   = 12,
		},
		.m1 = {
			.reg_off = AO_CEC_CTRL1,
			.shift   = 0,
			.width   = 12,
		},
		.m2 = {
			.reg_off = AO_CEC_CTRL1,
			.shift   = 12,
			.width   = 12,
		},
		.dual = {
			.reg_off = AO_CEC_CTRL0,
			.shift   = 28,
			.width   = 1,
		},
		.table = a9_ao_dualdiv_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_cec_dualdiv_div",
		.ops = &meson_clk_dualdiv_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_ao_cec_dualdiv_in.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a9_ao_cec_dualdiv_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = AO_CEC_CTRL1,
		.mask = 0x1,
		.shift = 24,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_cec_dualdiv_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_ao_cec_dualdiv_div.hw,
			&a9_ao_cec_dualdiv_in.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a9_ao_cec_dualdiv = {
	.data = &(struct clk_regmap_gate_data){
		.offset = AO_CEC_CTRL0,
		.bit_idx = 30,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_cec_dualdiv",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_ao_cec_dualdiv_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a9_ao_cec = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = AO_CEC_CTRL1,
		.mask = 0x1,
		.shift = 30,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ao_cec",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_ao_cec_dualdiv.hw,
			&a9_ao_rtc.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_hw *a9_ao_hw_clks[] = {
	[CLKID_AO_XTAL_IN]		= &a9_ao_xtal_in.hw,
	[CLKID_AO_XTAL]			= &a9_ao_xtal.hw,
	[CLKID_AO_SYS]			= &a9_ao_sys.hw,
	[CLKID_AO_SYS_I3C]		= &a9_ao_sys_i3c.hw,
	[CLKID_AO_SYS_RTC_REG]		= &a9_ao_sys_rtc_reg.hw,
	[CLKID_AO_SYS_CLKTREE]		= &a9_ao_sys_clktree.hw,
	[CLKID_AO_SYS_RST_CTRL]		= &a9_ao_sys_rst_ctrl.hw,
	[CLKID_AO_SYS_PAD]		= &a9_ao_sys_pad.hw,
	[CLKID_AO_SYS_RTC_DIG]		= &a9_ao_sys_rtc_dig.hw,
	[CLKID_AO_SYS_IRQ]		= &a9_ao_sys_irq.hw,
	[CLKID_AO_SYS_PWRCTRL]		= &a9_ao_sys_pwrctrl.hw,
	[CLKID_AO_SYS_PWM_A]		= &a9_ao_sys_pwm_a.hw,
	[CLKID_AO_SYS_PWM_B]		= &a9_ao_sys_pwm_b.hw,
	[CLKID_AO_SYS_PWM_C]		= &a9_ao_sys_pwm_c.hw,
	[CLKID_AO_SYS_PWM_D]		= &a9_ao_sys_pwm_d.hw,
	[CLKID_AO_SYS_PWM_E]		= &a9_ao_sys_pwm_e.hw,
	[CLKID_AO_SYS_PWM_F]		= &a9_ao_sys_pwm_f.hw,
	[CLKID_AO_SYS_PWM_G]		= &a9_ao_sys_pwm_g.hw,
	[CLKID_AO_SYS_I2C_A]		= &a9_ao_sys_i2c_a.hw,
	[CLKID_AO_SYS_I2C_B]		= &a9_ao_sys_i2c_b.hw,
	[CLKID_AO_SYS_I2C_C]		= &a9_ao_sys_i2c_c.hw,
	[CLKID_AO_SYS_I2C_D]		= &a9_ao_sys_i2c_d.hw,
	[CLKID_AO_SYS_SED]		= &a9_ao_sys_sed.hw,
	[CLKID_AO_SYS_IR_CTRL]		= &a9_ao_sys_ir_ctrl.hw,
	[CLKID_AO_SYS_UART_B]		= &a9_ao_sys_uart_b.hw,
	[CLKID_AO_SYS_UART_C]		= &a9_ao_sys_uart_c.hw,
	[CLKID_AO_SYS_UART_D]		= &a9_ao_sys_uart_d.hw,
	[CLKID_AO_SYS_UART_E]		= &a9_ao_sys_uart_e.hw,
	[CLKID_AO_SYS_SPISG_0]		= &a9_ao_sys_spisg_0.hw,
	[CLKID_AO_SYS_RTC_SECURE]	= &a9_ao_sys_rtc_secure.hw,
	[CLKID_AO_SYS_CEC]		= &a9_ao_sys_cec.hw,
	[CLKID_AO_SYS_AOCPU]		= &a9_ao_sys_aocpu.hw,
	[CLKID_AO_SYS_SRAM]		= &a9_ao_sys_sram.hw,
	[CLKID_AO_SYS_SPISG_1]		= &a9_ao_sys_spisg_1.hw,
	[CLKID_AO_SYS_SPISG_2]		= &a9_ao_sys_spisg_2.hw,
	[CLKID_AO_PWM_A_SEL]		= &a9_ao_pwm_a_sel.hw,
	[CLKID_AO_PWM_A_DIV]		= &a9_ao_pwm_a_div.hw,
	[CLKID_AO_PWM_A]		= &a9_ao_pwm_a.hw,
	[CLKID_AO_PWM_B_SEL]		= &a9_ao_pwm_b_sel.hw,
	[CLKID_AO_PWM_B_DIV]		= &a9_ao_pwm_b_div.hw,
	[CLKID_AO_PWM_B]		= &a9_ao_pwm_b.hw,
	[CLKID_AO_PWM_C_SEL]		= &a9_ao_pwm_c_sel.hw,
	[CLKID_AO_PWM_C_DIV]		= &a9_ao_pwm_c_div.hw,
	[CLKID_AO_PWM_C]		= &a9_ao_pwm_c.hw,
	[CLKID_AO_PWM_D_SEL]		= &a9_ao_pwm_d_sel.hw,
	[CLKID_AO_PWM_D_DIV]		= &a9_ao_pwm_d_div.hw,
	[CLKID_AO_PWM_D]		= &a9_ao_pwm_d.hw,
	[CLKID_AO_PWM_E_SEL]		= &a9_ao_pwm_e_sel.hw,
	[CLKID_AO_PWM_E_DIV]		= &a9_ao_pwm_e_div.hw,
	[CLKID_AO_PWM_E]		= &a9_ao_pwm_e.hw,
	[CLKID_AO_PWM_F_SEL]		= &a9_ao_pwm_f_sel.hw,
	[CLKID_AO_PWM_F_DIV]		= &a9_ao_pwm_f_div.hw,
	[CLKID_AO_PWM_F]		= &a9_ao_pwm_f.hw,
	[CLKID_AO_PWM_G_SEL]		= &a9_ao_pwm_g_sel.hw,
	[CLKID_AO_PWM_G_DIV]		= &a9_ao_pwm_g_div.hw,
	[CLKID_AO_PWM_G]		= &a9_ao_pwm_g.hw,
	[CLKID_AO_RTC_DUALDIV_IN]	= &a9_ao_rtc_dualdiv_in.hw,
	[CLKID_AO_RTC_DUALDIV_DIV]	= &a9_ao_rtc_dualdiv_div.hw,
	[CLKID_AO_RTC_DUALDIV_SEL]	= &a9_ao_rtc_dualdiv_sel.hw,
	[CLKID_AO_RTC_DUALDIV]		= &a9_ao_rtc_dualdiv.hw,
	[CLKID_AO_RTC]			= &a9_ao_rtc.hw,
	[CLKID_AO_CEC_DUALDIV_IN]	= &a9_ao_cec_dualdiv_in.hw,
	[CLKID_AO_CEC_DUALDIV_DIV]	= &a9_ao_cec_dualdiv_div.hw,
	[CLKID_AO_CEC_DUALDIV_SEL]	= &a9_ao_cec_dualdiv_sel.hw,
	[CLKID_AO_CEC_DUALDIV]		= &a9_ao_cec_dualdiv.hw,
	[CLKID_AO_CEC]			= &a9_ao_cec.hw,
};

static const struct meson_clkc_data a9_ao_clkc_data = {
	.hw_clks = {
		.hws = a9_ao_hw_clks,
		.num = ARRAY_SIZE(a9_ao_hw_clks),
	},
};

static const struct of_device_id a9_ao_clkc_match_table[] = {
	{
		.compatible	= "amlogic,a9-aoclkc",
		.data		= &a9_ao_clkc_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, a9_ao_clkc_match_table);

static struct platform_driver a9_ao_clkc_driver = {
	.probe		= meson_clkc_mmio_probe,
	.driver		= {
		.name	= "a9-aoclkc",
		.of_match_table = a9_ao_clkc_match_table,
	},
};
module_platform_driver(a9_ao_clkc_driver);

MODULE_DESCRIPTION("Amlogic A9 Always-ON Clock Controller driver");
MODULE_AUTHOR("Jian Hu <jian.hu@amlogic.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("CLK_MESON");
