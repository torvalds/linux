// SPDX-License-Identifier: GPL-2.0
/*
 * Allwinner A733 SoC pinctrl driver.
 *
 * Copyright (C) 2025 Arm Ltd.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-sunxi.h"

static const u8 a733_nr_bank_pins[SUNXI_PINCTRL_MAX_BANKS] =
/*	  PA  PB  PC  PD  PE  PF  PG  PH  PI  PJ  PK */
	{  0, 11, 17, 24, 16,  7, 15, 20, 17, 28, 26 };

static const u8 a733_irq_bank_muxes[SUNXI_PINCTRL_MAX_BANKS] =
/*	  PA  PB  PC  PD  PE  PF  PG  PH  PI  PJ  PK */
	{  0, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14};

static struct sunxi_pinctrl_desc a733_pinctrl_data = {
	.irq_banks = 10,
	.irq_read_needs_mux = true,
	.io_bias_cfg_variant = BIAS_VOLTAGE_PIO_POW_MODE_SEL,
};

static int a733_pinctrl_probe(struct platform_device *pdev)
{
	return sunxi_pinctrl_dt_table_init(pdev, a733_nr_bank_pins,
					   a733_irq_bank_muxes,
					   &a733_pinctrl_data,
					   SUNXI_PINCTRL_NCAT3_REG_LAYOUT);
}

static const struct of_device_id a733_pinctrl_match[] = {
	{ .compatible = "allwinner,sun60i-a733-pinctrl", },
	{}
};

static struct platform_driver a733_pinctrl_driver = {
	.probe	= a733_pinctrl_probe,
	.driver	= {
		.name		= "sun60i-a733-pinctrl",
		.of_match_table	= a733_pinctrl_match,
	},
};
builtin_platform_driver(a733_pinctrl_driver);
