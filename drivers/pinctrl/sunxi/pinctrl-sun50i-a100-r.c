// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Yangtao Li <frank@allwinnertech.com>
 *
 * Based on:
 * huangshuosheng <huangshuosheng@allwinnertech.com>
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/platform_device.h>

#include "pinctrl-sunxi.h"

static const struct sunxi_desc_pin a100_r_pins[] = {
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_i2c0"),	/* SCK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 0)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_i2c0"),	/* SDA */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 1)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_uart0"),	/* TX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 2)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_uart0"),	/* RX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 3)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_jtag"),	/* MS */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 4)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_jtag"),	/* CK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 5)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_jtag"),	/* DO */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 6)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_jtag"),	/* DI */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 7)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 8),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_i2c1"),	/* SCK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 8)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 9),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_i2c1"),	/* SDA */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 9)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 10),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_pwm"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 10)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 11),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "s_cir"),		/* IN */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 11)),
};

static const struct sunxi_pinctrl_desc a100_r_pinctrl_data = {
	.pins = a100_r_pins,
	.npins = ARRAY_SIZE(a100_r_pins),
	.pin_base = PL_BASE,
	.irq_banks = 1,
	.io_bias_cfg_variant = BIAS_VOLTAGE_PIO_POW_MODE_CTL,
};

static int a100_r_pinctrl_probe(struct platform_device *pdev)
{
	return sunxi_pinctrl_init(pdev, &a100_r_pinctrl_data);
}

static const struct of_device_id a100_r_pinctrl_match[] = {
	{ .compatible = "allwinner,sun50i-a100-r-pinctrl", },
	{}
};
MODULE_DEVICE_TABLE(of, a100_r_pinctrl_match);

static struct platform_driver a100_r_pinctrl_driver = {
	.probe	= a100_r_pinctrl_probe,
	.driver	= {
		.name		= "sun50i-a100-r-pinctrl",
		.of_match_table	= a100_r_pinctrl_match,
	},
};
module_platform_driver(a100_r_pinctrl_driver);
