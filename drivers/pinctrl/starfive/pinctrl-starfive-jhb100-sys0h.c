// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Pinctrl / GPIO driver for StarFive JHB100 SoC System-0 host domain
 *
 * Copyright (C) 2024 StarFive Technology Co., Ltd.
 * Author: Alex Soo <yuklin.soo@starfivetech.com>
 *
 */

#include <linux/platform_device.h>

#include "pinctrl-starfive-jhb100.h"

/* sys0h pad numbers */
#define PADNUM_SYS0H_GPIO_A10				6
#define PADNUM_SYS0H_GPIO_A11				7
#define PADNUM_SYS0H_GPIO_A15				11

static const struct jhb100_pin_layout_desc jhb100_sys0h_pl_desc[] = {
	{ .pin_start = 0, .pin_cnt = 7, .name = "gpio", .gpio_func_sel = 0 },
	{ .pin_start = 7, .pin_cnt = 1, .name = "espi0_reset", .gpio_func_sel = 1 },
	{ .pin_start = 8, .pin_cnt = 4, .name = "gpio", .gpio_func_sel = 0 },
	{ 0xff },
};

static const struct config_reg_layout_desc jhb100_sys0h_pinctrl_rl_desc[] = {
	{
		.pin_start					= 0,
		.pin_cnt					= 12,
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

static const struct starfive_pinctrl_regs jhb100_sys0h_pinctrl_regs = {
	.func_sel		= { .reg = 0x40, .width_per_pin = 2 },
	.config			= 0x04,
	.output			= 0x34,
	.output_en		= 0x38,
	.gpio_status		= 0x3c,
	.irq_en			= 0x44,
	.irq_status		= 0x48,
	.irq_clr		= 0x4c,
	.irq_trigger		= 0x50,
	.irq_level		= 0x54,
	.irq_both_edge		= 0x58,
	.irq_edge		= 0x5c,
};

static const struct jhb100_pinctrl_func_maps jhb100_func_maps_sys0h[] = {
	{ .func = "espi",		.val = 1 },
	{ .func = "espi_reset",		.val = 0 },
	{ .func = "espi0_vw",		.val = 1 },
	{ .func = "espi1_vw",		.val = 2 },
	{ .func = "gpio",		.val = 0,
	  .max_pin = JHB100_FUNC_MAPS_MAX_PIN(PADNUM_SYS0H_GPIO_A10) },
	{ .func = "gpio",		.val = 1,
	  .max_pin = JHB100_FUNC_MAPS_MAX_PIN(PADNUM_SYS0H_GPIO_A11) },
	{ .func = "gpio",		.val = 0,
	  .max_pin = JHB100_FUNC_MAPS_MAX_PIN(PADNUM_SYS0H_GPIO_A15) },
	{ .func = "scap_trigger",	.val = 3 },
};

static const struct jhb100_pinctrl_domain_info jhb100_sys0h_pinctrl_info = {
	.name			= "jhb100-sys0h",
	.pl_desc		= jhb100_sys0h_pl_desc,
	.crl_desc		= jhb100_sys0h_pinctrl_rl_desc,
	.regs			= &jhb100_sys0h_pinctrl_regs,
	.fmaps			= jhb100_func_maps_sys0h,
	.num_maps		= ARRAY_SIZE(jhb100_func_maps_sys0h),
};

static const struct of_device_id jhb100_sys0h_pinctrl_of_match[] = {
	{
		.compatible = "starfive,jhb100-sys0h-pinctrl",
		.data = &jhb100_sys0h_pinctrl_info,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jhb100_sys0h_pinctrl_of_match);

static struct platform_driver jhb100_sys0h_pinctrl_driver = {
	.probe = jhb100_pinctrl_probe,
	.driver = {
		.name = "starfive-jhb100-sys0h-pinctrl",
		.of_match_table = jhb100_sys0h_pinctrl_of_match,
	},
};
module_platform_driver(jhb100_sys0h_pinctrl_driver);

MODULE_DESCRIPTION("Pinctrl driver for StarFive JHB100 SoC System-0 host domain");
MODULE_AUTHOR("Alex Soo <yuklin.soo@starfivetech.com>");
MODULE_LICENSE("GPL");
