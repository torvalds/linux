// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Pinctrl / GPIO driver for StarFive JHB100 SoC Peripheral-2 domain
 *
 * Copyright (C) 2024 StarFive Technology Co., Ltd.
 * Author: Alex Soo <yuklin.soo@starfivetech.com>
 *
 */

#include <linux/platform_device.h>

#include "pinctrl-starfive-jhb100.h"

/* per2 pad numbers */
#define PADNUM_PER2_GPIO_D19				19
#define PADNUM_PER2_GPIO_D20				20
#define PADNUM_PER2_GPIO_D21				21
#define PADNUM_PER2_GPIO_D22				22
#define PADNUM_PER2_GPIO_D23				23
#define PADNUM_PER2_GPIO_D24				24
#define PADNUM_PER2_GPIO_D25				25
#define PADNUM_PER2_GPIO_D26				26
#define PADNUM_PER2_GPIO_D27				27
#define PADNUM_PER2_GPIO_D28				28
#define PADNUM_PER2_GPIO_D29				29
#define PADNUM_PER2_GPIO_D30				30

static const struct jhb100_pin_layout_desc jhb100_per2_pl_desc[] = {
	{ .pin_start = 0, .pin_cnt = 15, .name = "gpio", .gpio_func_sel = 0 },
	{ .pin_start = 15, .pin_cnt = 1, .name = "gpio", .gpio_func_sel = -1 },
	{ .pin_start = 16, .pin_cnt = 15, .name = "gpio", .gpio_func_sel = 0 },
	{ 0xff },
};

static const struct config_reg_layout_desc jhb100_per2_pinctrl_rl_desc[] = {
	{
		.pin_start					= 0,
		.pin_cnt					= 19,
		.fields[PAD_CFG_DRIVE_STRENGTH_2BIT]		= { .shift = 0, .width = 2 },
		.fields[PAD_CFG_INPUT_ENABLE]			= { .shift = 2, .width = 1 },
		.fields[PAD_CFG_PULL_DOWN]			= { .shift = 3, .width = 1 },
		.fields[PAD_CFG_PULL_UP]			= { .shift = 4, .width = 1 },
		.fields[PAD_CFG_SLEW_RATE]			= { .shift = 5, .width = 1 },
		.fields[PAD_CFG_SCHMITT_TRIGGER_SELECT]		= { .shift = 6, .width = 1 },
		.fields[PAD_CFG_DEBOUNCE_WIDTH]			= { .shift = 15, .width = 17 },
	},
	{
		.pin_start					= 19,
		.pin_cnt					= 12,
		.fields[PAD_CFG_INPUT_ENABLE]			= { .shift = 0, .width = 1 },
		.fields[PAD_CFG_SLEW_RATE]			= { .shift = 1, .width = 1 },
		.fields[PAD_CFG_VSEL]				= { .shift = 2, .width = 2 },
		.fields[PAD_CFG_DEBOUNCE_WIDTH]			= { .shift = 15, .width = 17 },
	},
	{ 0xff },
};

static const struct pinvref_desc pinvref_desc_per2[] = {
	{
		/* gpionw */
		.pin_grp = {
			PADNUM_PER2_GPIO_D19,
			PADNUM_PER2_GPIO_D20,
			PADNUM_PER2_GPIO_D21,
			PADNUM_PER2_GPIO_D22,
			PADNUM_PER2_GPIO_D23,
			PADNUM_PER2_GPIO_D24,
			PADNUM_PER2_GPIO_D25,
			PADNUM_PER2_GPIO_D26,
			PADNUM_PER2_GPIO_D27,
			PADNUM_PER2_GPIO_D28,
			PADNUM_PER2_GPIO_D29,
			PADNUM_PER2_GPIO_D30
		},
		.num_pins = 12,
		.range = BIT(JHB100_PINVREF_1_8V) | BIT(JHB100_PINVREF_2_5V) |
			 BIT(JHB100_PINVREF_3_3V)
	},
};

static const struct starfive_pinctrl_regs jhb100_per2_pinctrl_regs = {
	.vref			= { .reg = 0x00, .pv_desc = pinvref_desc_per2,
				    .num_pv = ARRAY_SIZE(pinvref_desc_per2) },
	.func_sel		= { .reg = 0x8c, .width_per_pin = 2 },
	.config			= 0x04,
	.output			= 0x80,
	.output_en		= 0x84,
	.gpio_status		= 0x88,
	.irq_en			= 0x94,
	.irq_status		= 0x98,
	.irq_clr		= 0x9c,
	.irq_trigger		= 0xa0,
	.irq_level		= 0xa4,
	.irq_both_edge		= 0xa8,
	.irq_edge		= 0xac,
};

static const struct jhb100_pinctrl_func_maps jhb100_func_maps_per2[] = {
	{ .func = "fan_tach",		.val = 1 },
	{ .func = "gmac_rgmii",		.val = 1 },
	{ .func = "gmac_rmii",		.val = 2 },
	{ .func = "gpio",		.val = 0 },
	{ .func = "host0_port80",	.val = 2 },
	{ .func = "host1_port80",	.val = 3 },
};

static const struct jhb100_pinctrl_domain_info jhb100_per2_pinctrl_info = {
	.name			= "jhb100-per2",
	.pl_desc		= jhb100_per2_pl_desc,
	.crl_desc		= jhb100_per2_pinctrl_rl_desc,
	.regs			= &jhb100_per2_pinctrl_regs,
	.fmaps			= jhb100_func_maps_per2,
	.num_maps		= ARRAY_SIZE(jhb100_func_maps_per2),
};

static const struct of_device_id jhb100_per2_pinctrl_of_match[] = {
	{
		.compatible = "starfive,jhb100-per2-pinctrl",
		.data = &jhb100_per2_pinctrl_info,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jhb100_per2_pinctrl_of_match);

static struct platform_driver jhb100_per2_pinctrl_driver = {
	.probe = jhb100_pinctrl_probe,
	.driver = {
		.name = "starfive-jhb100-per2-pinctrl",
		.of_match_table = jhb100_per2_pinctrl_of_match,
	},
};
module_platform_driver(jhb100_per2_pinctrl_driver);

MODULE_DESCRIPTION("Pinctrl driver for StarFive JHB100 SoC Peripheral-2 domain");
MODULE_AUTHOR("Alex Soo <yuklin.soo@starfivetech.com>");
MODULE_LICENSE("GPL");
