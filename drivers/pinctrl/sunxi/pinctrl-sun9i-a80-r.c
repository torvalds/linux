/*
 * Allwinner A80 SoCs special pins pinctrl driver.
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
#include <linux/reset.h>

#include "pinctrl-sunxi.h"

static const struct sunxi_desc_pin sun9i_a80_r_pins[] = {
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "s_uart"),	/* TX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 0)),	/* PL_EINT0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "s_uart"),	/* RX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 1)),	/* PL_EINT1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "s_jtag"),	/* TMS */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 2)),	/* PL_EINT2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "s_jtag"),	/* TCK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 3)),	/* PL_EINT3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "s_jtag"),	/* TDO */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 4)),	/* PL_EINT4 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "s_jtag"),	/* TDI */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 5)),	/* PL_EINT5 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "s_cir_rx"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 6)),	/* PL_EINT6 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "1wire"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 7)),	/* PL_EINT7 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 8),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_ps2"),		/* SCK1 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 8)),	/* PL_EINT8 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 9),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_ps2"),		/* SDA1 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 9)),	/* PL_EINT9 */

	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 0)),	/* PM_EINT0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 1)),	/* PM_EINT1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 2)),	/* PM_EINT2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 3)),	/* PM_EINT3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "s_i2s1"),	/* LRCKR */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 4)),	/* PM_EINT4 */

	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 8),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "s_i2c1"),	/* SCK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 8)),	/* PM_EINT8 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 9),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "s_i2c1"),	/* SDA */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 9)),	/* PM_EINT9 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 10),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_i2s0"),	/* MCLK */
		  SUNXI_FUNCTION(0x3, "s_i2s1")),	/* MCLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 11),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_i2s0"),	/* BCLK */
		  SUNXI_FUNCTION(0x3, "s_i2s1")),	/* BCLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 12),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_i2s0"),	/* LRCK */
		  SUNXI_FUNCTION(0x3, "s_i2s1")),	/* LRCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 13),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_i2s0"),	/* DIN */
		  SUNXI_FUNCTION(0x3, "s_i2s1")),	/* DIN */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 14),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_i2s0"),	/* DOUT */
		  SUNXI_FUNCTION(0x3, "s_i2s1")),	/* DOUT */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 15),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 15)),	/* PM_EINT15 */

	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(N, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_i2c0"),	/* SCK */
		  SUNXI_FUNCTION(0x3, "s_rsb")),	/* SCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(N, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_i2c0"),	/* SDA */
		  SUNXI_FUNCTION(0x3, "s_rsb")),	/* SDA */
};

static const struct sunxi_pinctrl_desc sun9i_a80_r_pinctrl_data = {
	.pins = sun9i_a80_r_pins,
	.npins = ARRAY_SIZE(sun9i_a80_r_pins),
	.pin_base = PL_BASE,
	.irq_banks = 2,
};

static int sun9i_a80_r_pinctrl_probe(struct platform_device *pdev)
{
	return sunxi_pinctrl_init(pdev,
				  &sun9i_a80_r_pinctrl_data);
}

static const struct of_device_id sun9i_a80_r_pinctrl_match[] = {
	{ .compatible = "allwinner,sun9i-a80-r-pinctrl", },
	{}
};
MODULE_DEVICE_TABLE(of, sun9i_a80_r_pinctrl_match);

static struct platform_driver sun9i_a80_r_pinctrl_driver = {
	.probe	= sun9i_a80_r_pinctrl_probe,
	.driver	= {
		.name		= "sun9i-a80-r-pinctrl",
		.owner		= THIS_MODULE,
		.of_match_table	= sun9i_a80_r_pinctrl_match,
	},
};
module_platform_driver(sun9i_a80_r_pinctrl_driver);

MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com");
MODULE_DESCRIPTION("Allwinner A80 R_PIO pinctrl driver");
MODULE_LICENSE("GPL");
