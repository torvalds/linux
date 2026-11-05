// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Pinctrl / GPIO driver for StarFive JHB100 SoC Peripheral-2 Power OK domain
 *
 * Copyright (C) 2024 StarFive Technology Co., Ltd.
 * Author: Alex Soo <yuklin.soo@starfivetech.com>
 *
 */

#include <linux/platform_device.h>

#include "pinctrl-starfive-jhb100.h"

/* per2pok pad numbers */
#define PADNUM_PER2POK_GPIO_D36				5
#define PADNUM_PER2POK_GPIO_D40				9
#define PADNUM_PER2POK_GPIO_D48				17

static const struct jhb100_pin_layout_desc jhb100_per2pok_pl_desc[] = {
	{ .pin_start = 0, .pin_cnt = 10, .name = "gpio", .gpio_func_sel = 0 },
	{ .pin_start = 10, .pin_cnt = 8, .name = "pwm_channel", .gpio_func_sel = 1 },
	{ 0xff },
};

static const struct config_reg_layout_desc jhb100_per2pok_pinctrl_rl_desc[] = {
	{
		.pin_start					= 0,
		.pin_cnt					= 18,
		.fields[PAD_CFG_DRIVE_STRENGTH_2BIT]		= { .shift = 0, .width = 2 },
		.fields[PAD_CFG_INPUT_ENABLE]			= { .shift = 2, .width = 1 },
		.fields[PAD_CFG_PULL_DOWN]			= { .shift = 3, .width = 1 },
		.fields[PAD_CFG_PULL_UP]			= { .shift = 4, .width = 1 },
		.fields[PAD_CFG_SLEW_RATE]			= { .shift = 5, .width = 1 },
		.fields[PAD_CFG_SCHMITT_TRIGGER_SELECT]		= { .shift = 6, .width = 1 },
		.fields[PAD_CFG_DEBOUNCE_WIDTH]			= { .shift = 15, .width = 17 },
	},
	{ 0xff },
};

static const struct starfive_pinctrl_regs jhb100_per2pok_pinctrl_regs = {
	.func_sel		= { .reg = 0x58, .width_per_pin = 2 },
	.config			= 0x04,
	.output			= 0x4c,
	.output_en		= 0x50,
	.gpio_status		= 0x54,
	.irq_en			= 0x60,
	.irq_status		= 0x64,
	.irq_clr		= 0x68,
	.irq_trigger		= 0x6c,
	.irq_level		= 0x70,
	.irq_both_edge		= 0x74,
	.irq_edge		= 0x78,
};

static const struct jhb100_pinctrl_func_maps jhb100_func_maps_per2pok[] = {
	{ .func = "can",		.val = 1 },
	{ .func = "gpio",		.val = 0,
	  .max_pin = JHB100_FUNC_MAPS_MAX_PIN(PADNUM_PER2POK_GPIO_D40) },
	{ .func = "gpio",		.val = 1,
	  .max_pin = JHB100_FUNC_MAPS_MAX_PIN(PADNUM_PER2POK_GPIO_D48) },
	{ .func = "host0_port80",	.val = 2 },
	{ .func = "host1_port80",	.val = 3 },
	{ .func = "passthru",		.val = 2,
	  .max_pin = JHB100_FUNC_MAPS_MAX_PIN(PADNUM_PER2POK_GPIO_D36) },
	{ .func = "passthru",		.val = 1,
	  .max_pin = JHB100_FUNC_MAPS_MAX_PIN(PADNUM_PER2POK_GPIO_D40) },
	{ .func = "pwm",		.val = 0 },
};

static const struct jhb100_pinctrl_domain_info jhb100_per2pok_pinctrl_info = {
	.name			= "jhb100-per2pok",
	.pl_desc		= jhb100_per2pok_pl_desc,
	.crl_desc		= jhb100_per2pok_pinctrl_rl_desc,
	.regs			= &jhb100_per2pok_pinctrl_regs,
	.fmaps			= jhb100_func_maps_per2pok,
	.num_maps		= ARRAY_SIZE(jhb100_func_maps_per2pok),
};

static const struct of_device_id jhb100_per2pok_pinctrl_of_match[] = {
	{
		.compatible = "starfive,jhb100-per2pok-pinctrl",
		.data = &jhb100_per2pok_pinctrl_info,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jhb100_per2pok_pinctrl_of_match);

static struct platform_driver jhb100_per2pok_pinctrl_driver = {
	.probe = jhb100_pinctrl_probe,
	.driver = {
		.name = "starfive-jhb100-per2pok-pinctrl",
		.of_match_table = jhb100_per2pok_pinctrl_of_match,
	},
};
module_platform_driver(jhb100_per2pok_pinctrl_driver);

MODULE_DESCRIPTION("Pinctrl driver for StarFive JHB100 SoC Peripheral-2 Power OK domain");
MODULE_AUTHOR("Alex Soo <yuklin.soo@starfivetech.com>");
MODULE_LICENSE("GPL");
