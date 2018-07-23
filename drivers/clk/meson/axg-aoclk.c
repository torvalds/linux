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
#include "clk-regmap.h"
#include "meson-aoclk.h"
#include "axg-aoclk.h"

#define AXG_AO_GATE(_name, _bit)					\
static struct clk_regmap axg_aoclk_##_name = {				\
	.data = &(struct clk_regmap_gate_data) {			\
		.offset = (AO_RTI_GEN_CNTL_REG0),			\
		.bit_idx = (_bit),					\
	},								\
	.hw.init = &(struct clk_init_data) {				\
		.name =  "axg_ao_" #_name,				\
		.ops = &clk_regmap_gate_ops,				\
		.parent_names = (const char *[]){ "clk81" },		\
		.num_parents = 1,					\
		.flags = CLK_IGNORE_UNUSED,				\
	},								\
}

AXG_AO_GATE(remote, 0);
AXG_AO_GATE(i2c_master, 1);
AXG_AO_GATE(i2c_slave, 2);
AXG_AO_GATE(uart1, 3);
AXG_AO_GATE(uart2, 5);
AXG_AO_GATE(ir_blaster, 6);
AXG_AO_GATE(saradc, 7);

static struct clk_regmap axg_aoclk_clk81 = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = AO_RTI_PWR_CNTL_REG0,
		.mask = 0x1,
		.shift = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "axg_ao_clk81",
		.ops = &clk_regmap_mux_ro_ops,
		.parent_names = (const char *[]){ "clk81", "ao_alt_xtal"},
		.num_parents = 2,
	},
};

static struct clk_regmap axg_aoclk_saradc_mux = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = AO_SAR_CLK,
		.mask = 0x3,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "axg_ao_saradc_mux",
		.ops = &clk_regmap_mux_ops,
		.parent_names = (const char *[]){ "xtal", "axg_ao_clk81" },
		.num_parents = 2,
	},
};

static struct clk_regmap axg_aoclk_saradc_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = AO_SAR_CLK,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "axg_ao_saradc_div",
		.ops = &clk_regmap_divider_ops,
		.parent_names = (const char *[]){ "axg_ao_saradc_mux" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap axg_aoclk_saradc_gate = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = AO_SAR_CLK,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "axg_ao_saradc_gate",
		.ops = &clk_regmap_gate_ops,
		.parent_names = (const char *[]){ "axg_ao_saradc_div" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const unsigned int axg_aoclk_reset[] = {
	[RESET_AO_REMOTE]	= 16,
	[RESET_AO_I2C_MASTER]	= 18,
	[RESET_AO_I2C_SLAVE]	= 19,
	[RESET_AO_UART1]	= 17,
	[RESET_AO_UART2]	= 22,
	[RESET_AO_IR_BLASTER]	= 23,
};

static struct clk_regmap *axg_aoclk_regmap[] = {
	[CLKID_AO_REMOTE]	= &axg_aoclk_remote,
	[CLKID_AO_I2C_MASTER]	= &axg_aoclk_i2c_master,
	[CLKID_AO_I2C_SLAVE]	= &axg_aoclk_i2c_slave,
	[CLKID_AO_UART1]	= &axg_aoclk_uart1,
	[CLKID_AO_UART2]	= &axg_aoclk_uart2,
	[CLKID_AO_IR_BLASTER]	= &axg_aoclk_ir_blaster,
	[CLKID_AO_SAR_ADC]	= &axg_aoclk_saradc,
	[CLKID_AO_CLK81]	= &axg_aoclk_clk81,
	[CLKID_AO_SAR_ADC_SEL]	= &axg_aoclk_saradc_mux,
	[CLKID_AO_SAR_ADC_DIV]	= &axg_aoclk_saradc_div,
	[CLKID_AO_SAR_ADC_CLK]	= &axg_aoclk_saradc_gate,
};

static const struct clk_hw_onecell_data axg_aoclk_onecell_data = {
	.hws = {
		[CLKID_AO_REMOTE]	= &axg_aoclk_remote.hw,
		[CLKID_AO_I2C_MASTER]	= &axg_aoclk_i2c_master.hw,
		[CLKID_AO_I2C_SLAVE]	= &axg_aoclk_i2c_slave.hw,
		[CLKID_AO_UART1]	= &axg_aoclk_uart1.hw,
		[CLKID_AO_UART2]	= &axg_aoclk_uart2.hw,
		[CLKID_AO_IR_BLASTER]	= &axg_aoclk_ir_blaster.hw,
		[CLKID_AO_SAR_ADC]	= &axg_aoclk_saradc.hw,
		[CLKID_AO_CLK81]	= &axg_aoclk_clk81.hw,
		[CLKID_AO_SAR_ADC_SEL]	= &axg_aoclk_saradc_mux.hw,
		[CLKID_AO_SAR_ADC_DIV]	= &axg_aoclk_saradc_div.hw,
		[CLKID_AO_SAR_ADC_CLK]	= &axg_aoclk_saradc_gate.hw,
	},
	.num = NR_CLKS,
};

static const struct meson_aoclk_data axg_aoclkc_data = {
	.reset_reg	= AO_RTI_GEN_CNTL_REG0,
	.num_reset	= ARRAY_SIZE(axg_aoclk_reset),
	.reset		= axg_aoclk_reset,
	.num_clks	= ARRAY_SIZE(axg_aoclk_regmap),
	.clks		= axg_aoclk_regmap,
	.hw_data	= &axg_aoclk_onecell_data,
};

static const struct of_device_id axg_aoclkc_match_table[] = {
	{
		.compatible	= "amlogic,meson-axg-aoclkc",
		.data		= &axg_aoclkc_data,
	},
	{ }
};

static struct platform_driver axg_aoclkc_driver = {
	.probe		= meson_aoclkc_probe,
	.driver		= {
		.name	= "axg-aoclkc",
		.of_match_table = axg_aoclkc_match_table,
	},
};

builtin_platform_driver(axg_aoclkc_driver);
