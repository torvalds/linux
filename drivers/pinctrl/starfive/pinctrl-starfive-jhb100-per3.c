// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Pinctrl / GPIO driver for StarFive JHB100 SoC Peripheral-3 domain
 *
 * Copyright (C) 2024 StarFive Technology Co., Ltd.
 * Author: Alex Soo <yuklin.soo@starfivetech.com>
 *
 */

#include <linux/platform_device.h>

#include "pinctrl-starfive-jhb100.h"

/* per3 pad numbers */
#define PADNUM_PER3_GPIO_E0				0
#define PADNUM_PER3_GPIO_E1				1
#define PADNUM_PER3_GPIO_E2				2
#define PADNUM_PER3_GPIO_E3				3
#define PADNUM_PER3_GPIO_E4				4
#define PADNUM_PER3_GPIO_E5				5
#define PADNUM_PER3_GPIO_E6				6
#define PADNUM_PER3_GPIO_E7				7
#define PADNUM_PER3_GPIO_E8				8
#define PADNUM_PER3_GPIO_E9				9
#define PADNUM_PER3_GPIO_E10				10

static const struct jhb100_pin_layout_desc jhb100_per3_pl_desc[] = {
	{ .pin_start = 0, .pin_cnt = 11, .name = "gpio", .gpio_func_sel = 0 },
	{ 0xff },
};

static const struct config_reg_layout_desc jhb100_per3_pinctrl_rl_desc[] = {
	{
		.pin_start					= 0,
		.pin_cnt					= 2,
		.fields[PAD_CFG_DRIVE_STRENGTH_2BIT]		= { .shift = 0, .width = 2 },
		.fields[PAD_CFG_INPUT_ENABLE]			= { .shift = 2, .width = 1 },
		.fields[PAD_CFG_PULL_DOWN]			= { .shift = 3, .width = 1 },
		.fields[PAD_CFG_PULL_UP]			= { .shift = 4, .width = 1 },
		.fields[PAD_CFG_SLEW_RATE]			= { .shift = 5, .width = 1 },
		.fields[PAD_CFG_SCHMITT_TRIGGER_SELECT]		= { .shift = 6, .width = 1 },
		.fields[PAD_CFG_DEBOUNCE_WIDTH]			= { .shift = 15, .width = 17 },
	},
	{
		.pin_start					= 2,
		.pin_cnt					= 9,
		.fields[PAD_CFG_INPUT_ENABLE]			= { .shift = 0, .width = 1 },
		.fields[PAD_CFG_SLEW_RATE]			= { .shift = 1, .width = 1 },
		.fields[PAD_CFG_VSEL]				= { .shift = 2, .width = 2 },
		.fields[PAD_CFG_DEBOUNCE_WIDTH]			= { .shift = 15, .width = 17 },
	},
	{ 0xff },
};

static const struct pinvref_desc pinvref_desc_per3[] = {
	{
		/* gpios */
		.pin_grp = {
			PADNUM_PER3_GPIO_E0,
			PADNUM_PER3_GPIO_E1,
			PADNUM_PER3_GPIO_E2,
			PADNUM_PER3_GPIO_E3,
			PADNUM_PER3_GPIO_E4,
			PADNUM_PER3_GPIO_E5,
			PADNUM_PER3_GPIO_E6,
			PADNUM_PER3_GPIO_E7,
			PADNUM_PER3_GPIO_E8,
			PADNUM_PER3_GPIO_E9,
			PADNUM_PER3_GPIO_E10
		},
		.num_pins = 11,
		.range = BIT(JHB100_PINVREF_1_8V) | BIT(JHB100_PINVREF_2_5V) |
			 BIT(JHB100_PINVREF_3_3V)
	},
};

static const struct starfive_pinctrl_regs jhb100_per3_pinctrl_regs = {
	.vref			= { .reg = 0x00, .pv_desc = pinvref_desc_per3,
				    .num_pv = ARRAY_SIZE(pinvref_desc_per3) },
	.func_sel		= { .reg = 0x3c, .width_per_pin = 2 },
	.config			= 0x04,
	.output			= 0x30,
	.output_en		= 0x34,
	.gpio_status		= 0x38,
	.irq_en			= 0x40,
	.irq_status		= 0x44,
	.irq_clr		= 0x48,
	.irq_trigger		= 0x4c,
	.irq_level		= 0x50,
	.irq_both_edge		= 0x54,
	.irq_edge		= 0x58,
};

static const struct jhb100_pinctrl_func_maps jhb100_func_maps_per3[] = {
	{ .func = "gmac_mdio",		.val = 1 },
	{ .func = "gmac_rmii",		.val = 1 },
	{ .func = "gpio",		.val = 0 },
};

static const struct jhb100_pinctrl_domain_info jhb100_per3_pinctrl_info = {
	.name			= "jhb100-per3",
	.pl_desc		= jhb100_per3_pl_desc,
	.crl_desc		= jhb100_per3_pinctrl_rl_desc,
	.regs			= &jhb100_per3_pinctrl_regs,
	.fmaps			= jhb100_func_maps_per3,
	.num_maps		= ARRAY_SIZE(jhb100_func_maps_per3),
};

static const struct of_device_id jhb100_per3_pinctrl_of_match[] = {
	{
		.compatible = "starfive,jhb100-per3-pinctrl",
		.data = &jhb100_per3_pinctrl_info,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jhb100_per3_pinctrl_of_match);

static struct platform_driver jhb100_per3_pinctrl_driver = {
	.probe = jhb100_pinctrl_probe,
	.driver = {
		.name = "starfive-jhb100-per3-pinctrl",
		.of_match_table = jhb100_per3_pinctrl_of_match,
	},
};
module_platform_driver(jhb100_per3_pinctrl_driver);

MODULE_DESCRIPTION("Pinctrl driver for StarFive JHB100 SoC Peripheral-3 domain");
MODULE_AUTHOR("Alex Soo <yuklin.soo@starfivetech.com>");
MODULE_LICENSE("GPL");
