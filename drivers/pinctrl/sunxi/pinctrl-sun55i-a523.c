// SPDX-License-Identifier: GPL-2.0
/*
 * Allwinner A523 SoC pinctrl driver.
 *
 * Copyright (C) 2023 Arm Ltd.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-sunxi.h"

static const u8 a523_nr_bank_pins[SUNXI_PINCTRL_MAX_BANKS] =
/*	  PA  PB  PC  PD  PE  PF  PG  PH  PI  PJ  PK */
	{  0, 15, 17, 24, 16,  7, 15, 20, 17, 28, 24 };

static const unsigned int a523_irq_bank_map[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };

static const u8 a523_irq_bank_muxes[SUNXI_PINCTRL_MAX_BANKS] =
/*	  PA  PB  PC  PD  PE  PF  PG  PH  PI  PJ  PK */
	{  0, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14};

static struct sunxi_pinctrl_desc a523_pinctrl_data = {
	.irq_banks = ARRAY_SIZE(a523_irq_bank_map),
	.irq_bank_map = a523_irq_bank_map,
	.irq_read_needs_mux = true,
	.io_bias_cfg_variant = BIAS_VOLTAGE_PIO_POW_MODE_SEL,
};

static int a523_pinctrl_probe(struct platform_device *pdev)
{
	return sunxi_pinctrl_dt_table_init(pdev, a523_nr_bank_pins,
					   a523_irq_bank_muxes,
					   &a523_pinctrl_data,
					   SUNXI_PINCTRL_NEW_REG_LAYOUT |
					   SUNXI_PINCTRL_ELEVEN_BANKS);
}

static const struct of_device_id a523_pinctrl_match[] = {
	{ .compatible = "allwinner,sun55i-a523-pinctrl", },
	{}
};

static struct platform_driver a523_pinctrl_driver = {
	.probe	= a523_pinctrl_probe,
	.driver	= {
		.name		= "sun55i-a523-pinctrl",
		.of_match_table	= a523_pinctrl_match,
	},
};
builtin_platform_driver(a523_pinctrl_driver);
