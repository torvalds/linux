// SPDX-License-Identifier: GPL-2.0
/*
 * Allwinner H616 R_PIO pin controller driver
 *
 * Copyright (C) 2020 Arm Ltd.
 * Based on former work, which is:
 *   Copyright (C) 2017 Icenowy Zheng <icenowy@aosc.io>
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-sunxi.h"

static const struct sunxi_desc_pin sun50i_h616_r_pins[] = {
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_rsb"),		/* SCK */
		  SUNXI_FUNCTION(0x3, "s_i2c")),	/* SCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_rsb"),		/* SDA */
		  SUNXI_FUNCTION(0x3, "s_i2c")),	/* SDA */
};

static const struct sunxi_pinctrl_desc sun50i_h616_r_pinctrl_data = {
	.pins = sun50i_h616_r_pins,
	.npins = ARRAY_SIZE(sun50i_h616_r_pins),
	.pin_base = PL_BASE,
};

static int sun50i_h616_r_pinctrl_probe(struct platform_device *pdev)
{
	return sunxi_pinctrl_init(pdev,
				  &sun50i_h616_r_pinctrl_data);
}

static const struct of_device_id sun50i_h616_r_pinctrl_match[] = {
	{ .compatible = "allwinner,sun50i-h616-r-pinctrl", },
	{}
};

static struct platform_driver sun50i_h616_r_pinctrl_driver = {
	.probe	= sun50i_h616_r_pinctrl_probe,
	.driver	= {
		.name		= "sun50i-h616-r-pinctrl",
		.of_match_table	= sun50i_h616_r_pinctrl_match,
	},
};
builtin_platform_driver(sun50i_h616_r_pinctrl_driver);
