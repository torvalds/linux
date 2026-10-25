// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Pinctrl / GPIO driver for StarFive JHB100 SoC Peripheral-0 domain
 *
 * Copyright (C) 2024 StarFive Technology Co., Ltd.
 * Author: Alex Soo <yuklin.soo@starfivetech.com>
 *
 */

#include <linux/platform_device.h>

#include "pinctrl-starfive-jhb100.h"

/* per0 pad numbers */
#define PADNUM_PER0_GPIO_B8				8
#define PADNUM_PER0_GPIO_B9				9
#define PADNUM_PER0_GPIO_B10				10
#define PADNUM_PER0_GPIO_B11				11
#define PADNUM_PER0_GPIO_B12				12
#define PADNUM_PER0_GPIO_B13				13
#define PADNUM_PER0_GPIO_B14				14
#define PADNUM_PER0_GPIO_B15				15
#define PADNUM_PER0_GPIO_B16				16
#define PADNUM_PER0_GPIO_B17				17
#define PADNUM_PER0_GPIO_B18				18
#define PADNUM_PER0_GPIO_B19				19
#define PADNUM_PER0_GPIO_B20				20
#define PADNUM_PER0_GPIO_B21				21
#define PADNUM_PER0_GPIO_B22				22
#define PADNUM_PER0_GPIO_B23				23
#define PADNUM_PER0_GPIO_B32				32
#define PADNUM_PER0_GPIO_B33				33
#define PADNUM_PER0_GPIO_B34				34
#define PADNUM_PER0_GPIO_B35				35
#define PADNUM_PER0_GPIO_B36				36
#define PADNUM_PER0_GPIO_B37				37
#define PADNUM_PER0_GPIO_B38				38
#define PADNUM_PER0_GPIO_B39				39
#define PADNUM_PER0_GPIO_B40				40
#define PADNUM_PER0_GPIO_B41				41
#define PADNUM_PER0_GPIO_B42				42
#define PADNUM_PER0_GPIO_B43				43
#define PADNUM_PER0_GPIO_B59				59

static const struct jhb100_pin_layout_desc jhb100_per0_pl_desc[] = {
	{ .pin_start = 0, .pin_cnt = 60, .name = "gpio", .gpio_func_sel = 0 },
	{ 0xff },
};

static const struct config_reg_layout_desc jhb100_per0_pinctrl_rl_desc[] = {
	{
		.pin_start					= 0,
		.pin_cnt					= 60,
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

static const struct pinvref_desc pinvref_desc_per0[] = {
	{
		/* gpioe-i3c0 */
		.pin_grp = {
			PADNUM_PER0_GPIO_B8,
			PADNUM_PER0_GPIO_B9,
			PADNUM_PER0_GPIO_B10,
			PADNUM_PER0_GPIO_B11,
			PADNUM_PER0_GPIO_B32,
			PADNUM_PER0_GPIO_B33
		},
		.num_pins = 6,
		.range = BIT(JHB100_PINVREF_1_8V) | BIT(JHB100_PINVREF_3_3V)
	},
	{
		/* gpioe-i3c1 */
		.pin_grp = {
			PADNUM_PER0_GPIO_B12,
			PADNUM_PER0_GPIO_B13,
			PADNUM_PER0_GPIO_B14,
			PADNUM_PER0_GPIO_B15,
			PADNUM_PER0_GPIO_B34,
			PADNUM_PER0_GPIO_B35
		},
		.num_pins = 6,
		.range = BIT(JHB100_PINVREF_1_8V) | BIT(JHB100_PINVREF_3_3V)
	},
	{
		/* gpioe-i3c2 */
		.pin_grp = {
			PADNUM_PER0_GPIO_B16,
			PADNUM_PER0_GPIO_B17,
			PADNUM_PER0_GPIO_B18,
			PADNUM_PER0_GPIO_B19,
			PADNUM_PER0_GPIO_B20,
			PADNUM_PER0_GPIO_B21,
			PADNUM_PER0_GPIO_B22,
			PADNUM_PER0_GPIO_B23
		},
		.num_pins = 8,
		.range = BIT(JHB100_PINVREF_1_8V) | BIT(JHB100_PINVREF_3_3V)
	},
	{
		/* gpioe-i3c4 */
		.pin_grp = {
			PADNUM_PER0_GPIO_B36,
			PADNUM_PER0_GPIO_B37,
			PADNUM_PER0_GPIO_B38,
			PADNUM_PER0_GPIO_B39,
			PADNUM_PER0_GPIO_B40,
			PADNUM_PER0_GPIO_B41,
			PADNUM_PER0_GPIO_B42,
			PADNUM_PER0_GPIO_B43
		},
		.num_pins = 8,
		.range = BIT(JHB100_PINVREF_1_8V) | BIT(JHB100_PINVREF_3_3V)
	},
};

static const struct starfive_pinctrl_regs jhb100_per0_pinctrl_regs = {
	.vref			= { .reg = 0x004, .pv_desc = pinvref_desc_per0,
				    .num_pv = ARRAY_SIZE(pinvref_desc_per0) },
	.func_sel		= { .reg = 0x11c, .width_per_pin = 2 },
	.config			= 0x014,
	.output			= 0x104,
	.output_en		= 0x10c,
	.gpio_status		= 0x114,
	.irq_en			= 0x12c,
	.irq_status		= 0x134,
	.irq_clr		= 0x13c,
	.irq_trigger		= 0x144,
	.irq_level		= 0x14c,
	.irq_both_edge		= 0x154,
	.irq_edge		= 0x15c,
};

static const struct jhb100_pinctrl_func_maps jhb100_func_maps_per0[] = {
	{ .func = "gmac_mdio",		.val = 2 },
	{ .func = "gpio",		.val = 0 },
	{ .func = "i2c",		.val = 1 },
	{ .func = "i3c",		.val = 2,
	  .max_pin = JHB100_FUNC_MAPS_MAX_PIN(PADNUM_PER0_GPIO_B23) },
	{ .func = "i3c",		.val = 1,
	  .max_pin = JHB100_FUNC_MAPS_MAX_PIN(PADNUM_PER0_GPIO_B59) },
	{ .func = "smbalert",		.val = 1 },
	{ .func = "wdt",		.val = 2 },
};

static const struct jhb100_pinctrl_domain_info jhb100_per0_pinctrl_info = {
	.name			= "jhb100-per0",
	.pl_desc		= jhb100_per0_pl_desc,
	.crl_desc		= jhb100_per0_pinctrl_rl_desc,
	.regs			= &jhb100_per0_pinctrl_regs,
	.fmaps			= jhb100_func_maps_per0,
	.num_maps		= ARRAY_SIZE(jhb100_func_maps_per0),
};

static const struct of_device_id jhb100_per0_pinctrl_of_match[] = {
	{
		.compatible = "starfive,jhb100-per0-pinctrl",
		.data = &jhb100_per0_pinctrl_info,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jhb100_per0_pinctrl_of_match);

static struct platform_driver jhb100_per0_pinctrl_driver = {
	.probe = jhb100_pinctrl_probe,
	.driver = {
		.name = "starfive-jhb100-per0-pinctrl",
		.of_match_table = jhb100_per0_pinctrl_of_match,
	},
};
module_platform_driver(jhb100_per0_pinctrl_driver);

MODULE_DESCRIPTION("Pinctrl driver for StarFive JHB100 SoC Peripheral-0 domain");
MODULE_AUTHOR("Alex Soo <yuklin.soo@starfivetech.com>");
MODULE_LICENSE("GPL");
