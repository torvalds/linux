// SPDX-License-Identifier: GPL-2.0
/*
 * Allwinner A523 SoC r-pinctrl driver.
 *
 * Copyright (C) 2024 Arm Ltd.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-sunxi.h"

static const u8 a523_r_nr_bank_pins[SUNXI_PINCTRL_MAX_BANKS] =
/*	  PL  PM */
	{ 14,  6 };

static const unsigned int a523_r_irq_bank_map[] = { 0, 1 };

static const u8 a523_r_irq_bank_muxes[SUNXI_PINCTRL_MAX_BANKS] =
/*	  PL  PM */
	{ 14, 14 };

static struct sunxi_pinctrl_desc a523_r_pinctrl_data = {
	.irq_banks = ARRAY_SIZE(a523_r_irq_bank_map),
	.irq_bank_map = a523_r_irq_bank_map,
	.irq_read_needs_mux = true,
	.io_bias_cfg_variant = BIAS_VOLTAGE_PIO_POW_MODE_SEL,
	.pin_base = PL_BASE,
};

static int a523_r_pinctrl_probe(struct platform_device *pdev)
{
	return sunxi_pinctrl_dt_table_init(pdev, a523_r_nr_bank_pins,
					   a523_r_irq_bank_muxes,
					   &a523_r_pinctrl_data,
					   SUNXI_PINCTRL_NEW_REG_LAYOUT);
}

static const struct of_device_id a523_r_pinctrl_match[] = {
	{ .compatible = "allwinner,sun55i-a523-r-pinctrl", },
	{}
};

static struct platform_driver a523_r_pinctrl_driver = {
	.probe	= a523_r_pinctrl_probe,
	.driver	= {
		.name		= "sun55i-a523-r-pinctrl",
		.of_match_table	= a523_r_pinctrl_match,
	},
};
builtin_platform_driver(a523_r_pinctrl_driver);
