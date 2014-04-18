/*
 * Allwinner A31 SoCs special pins pinctrl driver.
 *
 * Copyright (C) 2014 Boris Brezillon
 * Boris Brezillon <boris.brezillon@free-electrons.com>
 *
 * Copyright (C) 2014 Maxime Ripard
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-sunxi.h"

static const struct sunxi_desc_pin sun6i_a31_r_pins[] = {
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_twi"),		/* SCK */
		  SUNXI_FUNCTION(0x3, "s_p2wi")),	/* SCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_twi"),		/* SDA */
		  SUNXI_FUNCTION(0x3, "s_p2wi")),	/* SDA */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_uart")),	/* TX */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_uart")),	/* RX */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_ir")),		/* RX */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "s_jtag")),	/* MS */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "s_jtag")),	/* CK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "s_jtag")),	/* DO */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 8),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "s_jtag")),	/* DI */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out")),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out")),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "1wire")),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out")),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out")),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out")),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out")),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "rtc")),		/* CLKO */
};

static const struct sunxi_pinctrl_desc sun6i_a31_r_pinctrl_data = {
	.pins = sun6i_a31_r_pins,
	.npins = ARRAY_SIZE(sun6i_a31_r_pins),
	.pin_base = PL_BASE,
};

static int sun6i_a31_r_pinctrl_probe(struct platform_device *pdev)
{
	return sunxi_pinctrl_init(pdev,
				  &sun6i_a31_r_pinctrl_data);
}

static struct of_device_id sun6i_a31_r_pinctrl_match[] = {
	{ .compatible = "allwinner,sun6i-a31-r-pinctrl", },
	{}
};
MODULE_DEVICE_TABLE(of, sun6i_a31_r_pinctrl_match);

static struct platform_driver sun6i_a31_r_pinctrl_driver = {
	.probe	= sun6i_a31_r_pinctrl_probe,
	.driver	= {
		.name		= "sun6i-a31-r-pinctrl",
		.owner		= THIS_MODULE,
		.of_match_table	= sun6i_a31_r_pinctrl_match,
	},
};
module_platform_driver(sun6i_a31_r_pinctrl_driver);

MODULE_AUTHOR("Boris Brezillon <boris.brezillon@free-electrons.com");
MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com");
MODULE_DESCRIPTION("Allwinner A31 R_PIO pinctrl driver");
MODULE_LICENSE("GPL");
