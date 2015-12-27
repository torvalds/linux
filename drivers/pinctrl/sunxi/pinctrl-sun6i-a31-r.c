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
#include <linux/reset.h>

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
		  SUNXI_FUNCTION_IRQ_BANK(0x2, 0, 0),	/* PL_EINT0 */
		  SUNXI_FUNCTION(0x3, "s_jtag")),	/* MS */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x2, 0, 1),	/* PL_EINT1 */
		  SUNXI_FUNCTION(0x3, "s_jtag")),	/* CK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x2, 0, 2),	/* PL_EINT2 */
		  SUNXI_FUNCTION(0x3, "s_jtag")),	/* DO */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 8),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x2, 0, 3),	/* PL_EINT3 */
		  SUNXI_FUNCTION(0x3, "s_jtag")),	/* DI */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x2, 1, 0)),	/* PM_EINT0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x2, 1, 1)),	/* PM_EINT1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x2, 1, 2),	/* PM_EINT2 */
		  SUNXI_FUNCTION(0x3, "1wire")),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x2, 1, 3)),	/* PM_EINT3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x2, 1, 4)),	/* PM_EINT4 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x2, 1, 5)),	/* PM_EINT5 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x2, 1, 6)),	/* PM_EINT6 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x2, 1, 7),	/* PM_EINT7 */
		  SUNXI_FUNCTION(0x3, "rtc")),		/* CLKO */
};

static const struct sunxi_pinctrl_desc sun6i_a31_r_pinctrl_data = {
	.pins = sun6i_a31_r_pins,
	.npins = ARRAY_SIZE(sun6i_a31_r_pins),
	.pin_base = PL_BASE,
	.irq_banks = 2,
};

static int sun6i_a31_r_pinctrl_probe(struct platform_device *pdev)
{
	struct reset_control *rstc;
	int ret;

	rstc = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(rstc)) {
		dev_err(&pdev->dev, "Reset controller missing\n");
		return PTR_ERR(rstc);
	}

	ret = reset_control_deassert(rstc);
	if (ret)
		return ret;

	ret = sunxi_pinctrl_init(pdev,
				 &sun6i_a31_r_pinctrl_data);

	if (ret)
		reset_control_assert(rstc);

	return ret;
}

static const struct of_device_id sun6i_a31_r_pinctrl_match[] = {
	{ .compatible = "allwinner,sun6i-a31-r-pinctrl", },
	{}
};
MODULE_DEVICE_TABLE(of, sun6i_a31_r_pinctrl_match);

static struct platform_driver sun6i_a31_r_pinctrl_driver = {
	.probe	= sun6i_a31_r_pinctrl_probe,
	.driver	= {
		.name		= "sun6i-a31-r-pinctrl",
		.of_match_table	= sun6i_a31_r_pinctrl_match,
	},
};
module_platform_driver(sun6i_a31_r_pinctrl_driver);

MODULE_AUTHOR("Boris Brezillon <boris.brezillon@free-electrons.com");
MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com");
MODULE_DESCRIPTION("Allwinner A31 R_PIO pinctrl driver");
MODULE_LICENSE("GPL");
