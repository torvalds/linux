// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Pinctrl / GPIO driver for StarFive JHB100 SoC Peripheral-1 domain
 *
 * Copyright (C) 2024 StarFive Technology Co., Ltd.
 * Author: Alex Soo <yuklin.soo@starfivetech.com>
 *
 */

#include <linux/platform_device.h>

#include "pinctrl-starfive-jhb100.h"

/* per1 pad numbers */
#define PADNUM_PER1_GPIO_C0				0
#define PADNUM_PER1_GPIO_C1				1
#define PADNUM_PER1_GPIO_C2				2
#define PADNUM_PER1_GPIO_C3				3
#define PADNUM_PER1_GPIO_C4				4
#define PADNUM_PER1_GPIO_C5				5
#define PADNUM_PER1_GPIO_C6				6
#define PADNUM_PER1_GPIO_C7				7
#define PADNUM_PER1_GPIO_C8				8
#define PADNUM_PER1_GPIO_C9				9
#define PADNUM_PER1_GPIO_C10				10
#define PADNUM_PER1_GPIO_C11				11
#define PADNUM_PER1_GPIO_C12				12
#define PADNUM_PER1_GPIO_C13				13
#define PADNUM_PER1_GPIO_C14				14
#define PADNUM_PER1_GPIO_C15				15
#define PADNUM_PER1_GPIO_C16				16
#define PADNUM_PER1_GPIO_C17				17
#define PADNUM_PER1_GPIO_C18				18
#define PADNUM_PER1_GPIO_C19				19
#define PADNUM_PER1_GPIO_C20				20
#define PADNUM_PER1_GPIO_C21				21
#define PADNUM_PER1_GPIO_C22				22
#define PADNUM_PER1_GPIO_C23				23
#define PADNUM_PER1_GPIO_C24				24
#define PADNUM_PER1_GPIO_C25				25
#define PADNUM_PER1_GPIO_C26				26
#define PADNUM_PER1_GPIO_C27				27
#define PADNUM_PER1_GPIO_C31				31
#define PADNUM_PER1_GPIO_C35				35

static const struct jhb100_pin_layout_desc jhb100_per1_pl_desc[] = {
	{ .pin_start = 0, .pin_cnt = 36, .name = "gpio", .gpio_func_sel = 0 },
	{ 0xff },
};

static const struct config_reg_layout_desc jhb100_per1_pinctr_rldesc[] = {
	{
		.pin_start					= 0,
		.pin_cnt					= 32,
		.fields[PAD_CFG_DRIVE_STRENGTH_2BIT]		= { .shift = 0, .width = 2 },
		.fields[PAD_CFG_INPUT_ENABLE]			= { .shift = 2, .width = 1 },
		.fields[PAD_CFG_PULL_DOWN]			= { .shift = 3, .width = 1 },
		.fields[PAD_CFG_PULL_UP]			= { .shift = 4, .width = 1 },
		.fields[PAD_CFG_SLEW_RATE]			= { .shift = 5, .width = 1 },
		.fields[PAD_CFG_SCHMITT_TRIGGER_SELECT]		= { .shift = 6, .width = 1 },
		.fields[PAD_CFG_DEBOUNCE_WIDTH]			= { .shift = 15, .width = 17 },
	},
	{
		.pin_start					= 32,
		.pin_cnt					= 4,
		.fields[PAD_CFG_INPUT_ENABLE]			= { .shift = 0, .width = 1 },
		.fields[PAD_CFG_MODE_SELECT]			= { .shift = 1, .width = 2 },
		.fields[PAD_CFG_PULL_DOWN]			= { .shift = 3, .width = 1 },
		.fields[PAD_CFG_PULL_UP]			= { .shift = 4, .width = 1 },
		.fields[PAD_CFG_OPEN_DRAIN_PULL_UP_SEL]		= { .shift = 5, .width = 2 },
		.fields[PAD_CFG_SCHMITT_TRIGGER_SELECT]		= { .shift = 7, .width = 1 },
		.fields[PAD_CFG_DEBOUNCE_WIDTH]			= { .shift = 15, .width = 17 },
	},
	{ 0xff },
};

static const struct pinvref_desc pinvref_desc_per1[] = {
	{
		/* gpioe-spi */
		.pin_grp = {
			PADNUM_PER1_GPIO_C0,
			PADNUM_PER1_GPIO_C1,
			PADNUM_PER1_GPIO_C2,
			PADNUM_PER1_GPIO_C3,
			PADNUM_PER1_GPIO_C4
		},
		.num_pins = 5,
		.range = BIT(JHB100_PINVREF_1_8V) | BIT(JHB100_PINVREF_3_3V)
	},
	{
		/* gpioe-qspi0 */
		.pin_grp = {
			PADNUM_PER1_GPIO_C5,
			PADNUM_PER1_GPIO_C6,
			PADNUM_PER1_GPIO_C7,
			PADNUM_PER1_GPIO_C8,
			PADNUM_PER1_GPIO_C9,
			PADNUM_PER1_GPIO_C10,
			PADNUM_PER1_GPIO_C11
		},
		.num_pins = 7,
		.range = BIT(JHB100_PINVREF_1_8V) | BIT(JHB100_PINVREF_3_3V)
	},
	{
		/* gpioe-qspi1 */
		.pin_grp = {
			PADNUM_PER1_GPIO_C12,
			PADNUM_PER1_GPIO_C13,
			PADNUM_PER1_GPIO_C14,
			PADNUM_PER1_GPIO_C15,
			PADNUM_PER1_GPIO_C16,
			PADNUM_PER1_GPIO_C17,
			PADNUM_PER1_GPIO_C18,
			PADNUM_PER1_GPIO_C19
		},
		.num_pins = 8,
		.range = BIT(JHB100_PINVREF_1_8V) | BIT(JHB100_PINVREF_3_3V)
	},
	{
		/* gpioe-qspi2 */
		.pin_grp = {
			PADNUM_PER1_GPIO_C20,
			PADNUM_PER1_GPIO_C21,
			PADNUM_PER1_GPIO_C22,
			PADNUM_PER1_GPIO_C23,
			PADNUM_PER1_GPIO_C24,
			PADNUM_PER1_GPIO_C25,
			PADNUM_PER1_GPIO_C26,
			PADNUM_PER1_GPIO_C27
		},
		.num_pins = 8,
		.range = BIT(JHB100_PINVREF_1_8V) | BIT(JHB100_PINVREF_3_3V)
	},
};

static const struct starfive_pinctrl_regs jhb100_per1_pinctrl_regs = {
	.vref			= { .reg = 0x00, .pv_desc = pinvref_desc_per1,
				    .num_pv = ARRAY_SIZE(pinvref_desc_per1) },
	.func_sel		= { .reg = 0xbc, .width_per_pin = 2 },
	.config			= 0x14,
	.output			= 0xa4,
	.output_en		= 0xac,
	.gpio_status		= 0xb4,
	.irq_en			= 0xc8,
	.irq_status		= 0xd0,
	.irq_clr		= 0xd8,
	.irq_trigger		= 0xe0,
	.irq_level		= 0xe8,
	.irq_both_edge		= 0xf0,
	.irq_edge		= 0xf8,
};

static const struct jhb100_pinctrl_func_maps jhb100_func_maps_per1[] = {
	{ .func = "gpio",		.val = 0 },
	{ .func = "i2c",		.val = 1 },
	{ .func = "sfc",		.val = 1 },
	{ .func = "sgpio",		.val = 1,
	  .max_pin = JHB100_FUNC_MAPS_MAX_PIN(PADNUM_PER1_GPIO_C31) },
	{ .func = "sgpio",		.val = 2,
	  .max_pin = JHB100_FUNC_MAPS_MAX_PIN(PADNUM_PER1_GPIO_C35) },
	{ .func = "spi",		.val = 1 },
};

static const struct jhb100_pinctrl_domain_info jhb100_per1_pinctrl_info = {
	.name			= "jhb100-per1",
	.pl_desc		= jhb100_per1_pl_desc,
	.crl_desc		= jhb100_per1_pinctr_rldesc,
	.regs			= &jhb100_per1_pinctrl_regs,
	.fmaps			= jhb100_func_maps_per1,
	.num_maps		= ARRAY_SIZE(jhb100_func_maps_per1),
};

static const struct of_device_id jhb100_per1_pinctrl_of_match[] = {
	{
		.compatible = "starfive,jhb100-per1-pinctrl",
		.data = &jhb100_per1_pinctrl_info,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jhb100_per1_pinctrl_of_match);

static struct platform_driver jhb100_per1_pinctrl_driver = {
	.probe = jhb100_pinctrl_probe,
	.driver = {
		.name = "starfive-jhb100-per1-pinctrl",
		.of_match_table = jhb100_per1_pinctrl_of_match,
	},
};
module_platform_driver(jhb100_per1_pinctrl_driver);

MODULE_DESCRIPTION("Pinctrl driver for StarFive JHB100 SoC Peripheral-1 domain");
MODULE_AUTHOR("Alex Soo <yuklin.soo@starfivetech.com>");
MODULE_LICENSE("GPL");
