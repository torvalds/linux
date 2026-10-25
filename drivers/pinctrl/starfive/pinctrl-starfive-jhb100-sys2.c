// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Pinctrl / GPIO driver for StarFive JHB100 SoC System-2 domain
 *
 * Copyright (C) 2024 StarFive Technology Co., Ltd.
 * Author: Alex Soo <yuklin.soo@starfivetech.com>
 *
 */

#include <linux/platform_device.h>

#include "pinctrl-starfive-jhb100.h"

/* sys2 pad numbers */
#define PADNUM_SYS2_GPIO_A36				12
#define PADNUM_SYS2_GPIO_A37				13
#define PADNUM_SYS2_GPIO_A38				14
#define PADNUM_SYS2_GPIO_A39				15
#define PADNUM_SYS2_GPIO_A40				16
#define PADNUM_SYS2_GPIO_A41				17
#define PADNUM_SYS2_GPIO_A42				18
#define PADNUM_SYS2_GPIO_A43				19

static const struct jhb100_pin_layout_desc jhb100_sys2_pl_desc[] = {
	{ .pin_start = 0, .pin_cnt = 37, .name = "gpio", .gpio_func_sel = 0 },
	{ .pin_start = 37, .pin_cnt = 1, .name = "jtag_tck", .gpio_func_sel = -1 },
	{ .pin_start = 38, .pin_cnt = 1, .name = "jtag_tresetn", .gpio_func_sel = -1 },
	{ .pin_start = 39, .pin_cnt = 1, .name = "jtag_tmc", .gpio_func_sel = -1 },
	{ .pin_start = 40, .pin_cnt = 1, .name = "jtag_tdi", .gpio_func_sel = -1 },
	{ .pin_start = 41, .pin_cnt = 1, .name = "jtag_tdo", .gpio_func_sel = -1 },
	{ .pin_start = 42, .pin_cnt = 1, .name = "jtag_hpd", .gpio_func_sel = -1 },
	{ 0xff },
};

static const struct config_reg_layout_desc jhb100_sys2_pinctrl_rl_desc[] = {
	{
		.pin_start					= 0,
		.pin_cnt					= 37,
		.fields[PAD_CFG_DRIVE_STRENGTH_2BIT]		= { .shift = 0, .width = 2 },
		.fields[PAD_CFG_INPUT_ENABLE]			= { .shift = 2, .width = 1 },
		.fields[PAD_CFG_PULL_DOWN]			= { .shift = 3, .width = 1 },
		.fields[PAD_CFG_PULL_UP]			= { .shift = 4, .width = 1 },
		.fields[PAD_CFG_SLEW_RATE]			= { .shift = 5, .width = 1 },
		.fields[PAD_CFG_SCHMITT_TRIGGER_SELECT]		= { .shift = 6, .width = 1 },
		.fields[PAD_CFG_DEBOUNCE_WIDTH]			= { .shift = 15, .width = 17 },
	},
	{
		.pin_start					= 37,
		.pin_cnt					= 6,
		.fields[PAD_CFG_DRIVE_STRENGTH_2BIT]		= { .shift = 0, .width = 2 },
		.fields[PAD_CFG_INPUT_ENABLE]			= { .shift = 2, .width = 1 },
		.fields[PAD_CFG_PULL_DOWN]			= { .shift = 3, .width = 1 },
		.fields[PAD_CFG_PULL_UP]			= { .shift = 4, .width = 1 },
		.fields[PAD_CFG_SLEW_RATE]			= { .shift = 5, .width = 1 },
		.fields[PAD_CFG_SCHMITT_TRIGGER_SELECT]		= { .shift = 6, .width = 1 },
	},
	{ 0xff },
};

static const struct pinvref_desc pinvref_desc_sys2[] = {
	{
		/* gpiow */
		.pin_grp = {
			PADNUM_SYS2_GPIO_A36,
			PADNUM_SYS2_GPIO_A37,
			PADNUM_SYS2_GPIO_A38,
			PADNUM_SYS2_GPIO_A39
		},
		.num_pins = 4,
		.range = BIT(JHB100_PINVREF_1_8V) | BIT(JHB100_PINVREF_3_3V)
	},
	{
		/* gpiow-inner */
		.pin_grp = {
			PADNUM_SYS2_GPIO_A40,
			PADNUM_SYS2_GPIO_A41,
			PADNUM_SYS2_GPIO_A42,
			PADNUM_SYS2_GPIO_A43
		},
		.num_pins = 4,
		.range = BIT(JHB100_PINVREF_1_8V) | BIT(JHB100_PINVREF_3_3V)
	},
};

static const struct starfive_pinctrl_regs jhb100_sys2_pinctrl_regs = {
	.vref			= { .reg = 0x000, .pv_desc = pinvref_desc_sys2,
				    .num_pv = ARRAY_SIZE(pinvref_desc_sys2) },
	.func_sel		= { .reg = 0x0d4, .width_per_pin = 2 },
	.config			= 0x010,
	.output			= 0x0bc,
	.output_en		= 0x0c4,
	.gpio_status		= 0x0cc,
	.irq_en			= 0x0e0,
	.irq_status		= 0x0e8,
	.irq_clr		= 0x0f0,
	.irq_trigger		= 0x0f8,
	.irq_level		= 0x100,
	.irq_both_edge		= 0x108,
	.irq_edge		= 0x110,
};

static const struct jhb100_pinctrl_func_maps jhb100_func_maps_sys2[] = {
	{ .func = "gpio",		.val = 0 },
	{ .func = "host0_port80",	.val = 2 },
	{ .func = "host1_port80",	.val = 3 },
	{ .func = "jtag",		.val = 2 },
	{ .func = "smbalert",		.val = 2 },
	{ .func = "uart",		.val = 1 },
};

static const struct jhb100_pinctrl_domain_info jhb100_sys2_pinctrl_info = {
	.name			= "jhb100-sys2",
	.pl_desc		= jhb100_sys2_pl_desc,
	.crl_desc		= jhb100_sys2_pinctrl_rl_desc,
	.regs			= &jhb100_sys2_pinctrl_regs,
	.fmaps			= jhb100_func_maps_sys2,
	.num_maps		= ARRAY_SIZE(jhb100_func_maps_sys2),
};

static const struct of_device_id jhb100_sys2_pinctrl_of_match[] = {
	{
		.compatible = "starfive,jhb100-sys2-pinctrl",
		.data = &jhb100_sys2_pinctrl_info,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jhb100_sys2_pinctrl_of_match);

static struct platform_driver jhb100_sys2_pinctrl_driver = {
	.probe = jhb100_pinctrl_probe,
	.driver = {
		.name = "starfive-jhb100-sys2-pinctrl",
		.of_match_table = jhb100_sys2_pinctrl_of_match,
	},
};
module_platform_driver(jhb100_sys2_pinctrl_driver);

MODULE_DESCRIPTION("Pinctrl driver for StarFive JHB100 SoC System-2 domain");
MODULE_AUTHOR("Alex Soo <yuklin.soo@starfivetech.com>");
MODULE_LICENSE("GPL");
