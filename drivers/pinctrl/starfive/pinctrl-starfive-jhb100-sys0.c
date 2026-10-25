// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Pinctrl / GPIO driver for StarFive JHB100 SoC System-0 domain
 *
 * Copyright (C) 2024 StarFive Technology Co., Ltd.
 * Author: Alex Soo <yuklin.soo@starfivetech.com>
 *
 */

#include <linux/platform_device.h>

#include "pinctrl-starfive-jhb100.h"

/* sys0 pad numbers */
#define PADNUM_SYS0_GPIO_A2				2
#define PADNUM_SYS0_GPIO_A3				3

static const struct jhb100_pin_layout_desc jhb100_sys0_pl_desc[] = {
	{ .pin_start = 0, .pin_cnt = 3, .name = "gpio", .gpio_func_sel = 0, },
	{ .pin_start = 3, .pin_cnt = 1, .name = "bmcpcierp_pe2rst_out", .gpio_func_sel = 1, },
	{ .pin_start = 4, .pin_cnt = 1, .name = "testen", .gpio_func_sel = -1, },
	{ .pin_start = 5, .pin_cnt = 1, .name = "syspok_in", .gpio_func_sel = -1, },
	{ .pin_start = 6, .pin_cnt = 1, .name = "sysrstn_in", .gpio_func_sel = -1, },
	{ .pin_start = 7, .pin_cnt = 1, .name = "perstn0_in", .gpio_func_sel = -1, },
	{ .pin_start = 8, .pin_cnt = 1, .name = "perstn1_in", .gpio_func_sel = -1, },
	{ .pin_start = 9, .pin_cnt = 1, .name = "aprstn_out", .gpio_func_sel = -1, },
	{ .pin_start = 10, .pin_cnt = 1, .name = "pcierp_wake", .gpio_func_sel = -1, },
	{ 0xff },
};

static const struct config_reg_layout_desc jhb100_sys0_pinctrl_crl_desc[] = {
	{
		.pin_start					= 0,
		.pin_cnt					= 4,
		.fields[PAD_CFG_DRIVE_STRENGTH_2BIT]		= { .shift = 0, .width = 2 },
		.fields[PAD_CFG_INPUT_ENABLE]			= { .shift = 2, .width = 1 },
		.fields[PAD_CFG_PULL_DOWN]			= { .shift = 3, .width = 1 },
		.fields[PAD_CFG_PULL_UP]			= { .shift = 4, .width = 1 },
		.fields[PAD_CFG_SLEW_RATE]			= { .shift = 5, .width = 1 },
		.fields[PAD_CFG_SCHMITT_TRIGGER_SELECT]		= { .shift = 6, .width = 1 },
		.fields[PAD_CFG_DEBOUNCE_WIDTH]			= { .shift = 15, .width = 17 },
	},
	{
		.pin_start					= 4,
		.pin_cnt					= 5,
		.fields[PAD_CFG_SCHMITT_TRIGGER_SELECT]		= { .shift = 0, .width = 1 },
	},
	{
		.pin_start					= 9,
		.pin_cnt					= 1,
		.fields[PAD_CFG_DRIVE_STRENGTH_2BIT]		= { .shift = 0, .width = 2 },
		.fields[PAD_CFG_SLEW_RATE]			= { .shift = 2, .width = 1 },
	},
	{
		.pin_start					= 10,
		.pin_cnt					= 1,
		.fields[PAD_CFG_DRIVE_STRENGTH_2BIT]		= { .shift = 0, .width = 2 },
		.fields[PAD_CFG_INPUT_ENABLE]			= { .shift = 2, .width = 1 },
		.fields[PAD_CFG_PULL_DOWN]			= { .shift = 3, .width = 1 },
		.fields[PAD_CFG_PULL_UP]			= { .shift = 4, .width = 1 },
		.fields[PAD_CFG_SLEW_RATE]			= { .shift = 5, .width = 1 },
		.fields[PAD_CFG_SCHMITT_TRIGGER_SELECT]		= { .shift = 6, .width = 1 },
	},
	{ 0xff },
};

static const struct starfive_pinctrl_regs jhb100_sys0_pinctrl_regs = {
	.func_sel		= { .reg = 0x44, .width_per_pin = 2 },
	.config			= 0x0c,
	.output			= 0x38,
	.output_en		= 0x3c,
	.gpio_status		= 0x40,
	.irq_en			= 0x48,
	.irq_status		= 0x4c,
	.irq_clr		= 0x50,
	.irq_trigger		= 0x54,
	.irq_level		= 0x58,
	.irq_both_edge		= 0x5c,
	.irq_edge		= 0x60,
};

static const struct jhb100_pinctrl_func_maps jhb100_func_maps_sys0[] = {
	{ .func = "auxpwrgood",		.val = 1 },
	{ .func = "gpio",		.val = 0,
	  .max_pin = JHB100_FUNC_MAPS_MAX_PIN(PADNUM_SYS0_GPIO_A2) },
	{ .func = "gpio",		.val = 1,
	  .max_pin = JHB100_FUNC_MAPS_MAX_PIN(PADNUM_SYS0_GPIO_A3) },
	{ .func = "hbled",		.val = 1 },
	{ .func = "pe2rst_out",		.val = 0 },
};

static const struct jhb100_pinctrl_domain_info jhb100_sys0_pinctrl_info = {
	.name			= "jhb100-sys0",
	.pl_desc		= jhb100_sys0_pl_desc,
	.crl_desc		= jhb100_sys0_pinctrl_crl_desc,
	.regs			= &jhb100_sys0_pinctrl_regs,
	.fmaps			= jhb100_func_maps_sys0,
	.num_maps		= ARRAY_SIZE(jhb100_func_maps_sys0),
};

static const struct of_device_id jhb100_sys0_pinctrl_of_match[] = {
	{
		.compatible = "starfive,jhb100-sys0-pinctrl",
		.data = &jhb100_sys0_pinctrl_info,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jhb100_sys0_pinctrl_of_match);

static struct platform_driver jhb100_sys0_pinctrl_driver = {
	.probe = jhb100_pinctrl_probe,
	.driver = {
		.name = "starfive-jhb100-sys0-pinctrl",
		.of_match_table = jhb100_sys0_pinctrl_of_match,
	},
};
module_platform_driver(jhb100_sys0_pinctrl_driver);

MODULE_DESCRIPTION("Pinctrl driver for StarFive JHB100 SoC System-0 domain");
MODULE_AUTHOR("Alex Soo <yuklin.soo@starfivetech.com>");
MODULE_LICENSE("GPL");
