/*
 * Allwinner H3 SoCs pinctrl driver.
 *
 * Copyright (C) 2016 Krzysztof Adamski <k@japko.eu>
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

static const struct sunxi_desc_pin sun8i_h3_r_pins[] = {
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_twi"),         /* SCK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 0)),	/* PL_EINT0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_twi"),         /* SDA */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 1)),	/* PL_EINT1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_uart"),        /* TX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 2)),	/* PL_EINT2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_uart"),        /* RX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 3)),	/* PL_EINT3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_jtag"),        /* MS */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 4)),	/* PL_EINT4 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_jtag"),        /* CK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 5)),	/* PL_EINT5 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_jtag"),        /* DO */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 6)),	/* PL_EINT6 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_jtag"),        /* DI */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 7)),	/* PL_EINT7 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 8),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 8)),	/* PL_EINT8 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 9),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 9)),	/* PL_EINT9 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 10),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_pwm"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 10)),	/* PL_EINT10 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 11),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_cir_rx"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 11)),	/* PL_EINT11 */
};

static const struct sunxi_pinctrl_desc sun8i_h3_r_pinctrl_data = {
	.pins = sun8i_h3_r_pins,
	.npins = ARRAY_SIZE(sun8i_h3_r_pins),
	.irq_banks = 1,
	.pin_base = PL_BASE,
	.irq_read_needs_mux = true
};

static int sun8i_h3_r_pinctrl_probe(struct platform_device *pdev)
{
	return sunxi_pinctrl_init(pdev,
				  &sun8i_h3_r_pinctrl_data);
}

static const struct of_device_id sun8i_h3_r_pinctrl_match[] = {
	{ .compatible = "allwinner,sun8i-h3-r-pinctrl", },
	{}
};

static struct platform_driver sun8i_h3_r_pinctrl_driver = {
	.probe	= sun8i_h3_r_pinctrl_probe,
	.driver	= {
		.name		= "sun8i-h3-r-pinctrl",
		.of_match_table	= sun8i_h3_r_pinctrl_match,
	},
};
builtin_platform_driver(sun8i_h3_r_pinctrl_driver);
