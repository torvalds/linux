/*
 * Allwinner sun5i SoCs pinctrl driver.
 *
 * Copyright (C) 2014-2016 Maxime Ripard <maxime.ripard@free-electrons.com>
 * Copyright (C) 2016 Mylene Josserand <mylene.josserand@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-sunxi.h"

static const struct sunxi_desc_pin sun5i_pins[] = {
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(A, 0),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ERXD3 */
		  SUNXI_FUNCTION(0x3, "ts0"),		/* CLK */
		  SUNXI_FUNCTION(0x5, "keypad")),	/* IN0 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(A, 1),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ERXD2 */
		  SUNXI_FUNCTION(0x3, "ts0"),		/* ERR */
		  SUNXI_FUNCTION(0x5, "keypad")),	/* IN1 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(A, 2),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ERXD1 */
		  SUNXI_FUNCTION(0x3, "ts0"),		/* SYNC */
		  SUNXI_FUNCTION(0x5, "keypad")),	/* IN2 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(A, 3),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ERXD0 */
		  SUNXI_FUNCTION(0x3, "ts0"),		/* DLVD */
		  SUNXI_FUNCTION(0x5, "keypad")),	/* IN3 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(A, 4),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ETXD3 */
		  SUNXI_FUNCTION(0x3, "ts0"),		/* D0 */
		  SUNXI_FUNCTION(0x5, "keypad")),	/* IN4 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(A, 5),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ETXD2 */
		  SUNXI_FUNCTION(0x3, "ts0"),		/* D1 */
		  SUNXI_FUNCTION(0x5, "keypad")),	/* IN5 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(A, 6),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ETXD1 */
		  SUNXI_FUNCTION(0x3, "ts0"),		/* D2 */
		  SUNXI_FUNCTION(0x5, "keypad")),	/* IN6 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(A, 7),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ETXD0 */
		  SUNXI_FUNCTION(0x3, "ts0"),		/* D3 */
		  SUNXI_FUNCTION(0x5, "keypad")),	/* IN7 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(A, 8),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ERXCK */
		  SUNXI_FUNCTION(0x3, "ts0"),		/* D4 */
		  SUNXI_FUNCTION(0x4, "uart1"),		/* DTR */
		  SUNXI_FUNCTION(0x5, "keypad")),	/* OUT0 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(A, 9),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ERXERR */
		  SUNXI_FUNCTION(0x3, "ts0"),		/* D5 */
		  SUNXI_FUNCTION(0x4, "uart1"),		/* DSR */
		  SUNXI_FUNCTION(0x5, "keypad")),	/* OUT1 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(A, 10),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ERXDV */
		  SUNXI_FUNCTION(0x3, "ts0"),		/* D6 */
		  SUNXI_FUNCTION(0x4, "uart1"),		/* DCD */
		  SUNXI_FUNCTION(0x5, "keypad")),	/* OUT2 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(A, 11),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* EMDC */
		  SUNXI_FUNCTION(0x3, "ts0"),		/* D7 */
		  SUNXI_FUNCTION(0x4, "uart1"),		/* RING */
		  SUNXI_FUNCTION(0x5, "keypad")),	/* OUT3 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(A, 12),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* EMDIO */
		  SUNXI_FUNCTION(0x3, "uart1"),		/* TX */
		  SUNXI_FUNCTION(0x5, "keypad")),	/* OUT4 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(A, 13),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ETXEN */
		  SUNXI_FUNCTION(0x3, "uart1"),		/* RX */
		  SUNXI_FUNCTION(0x5, "keypad")),	/* OUT5 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(A, 14),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ETXCK */
		  SUNXI_FUNCTION(0x3, "uart1"),		/* CTS */
		  SUNXI_FUNCTION(0x4, "uart3"),		/* TX */
		  SUNXI_FUNCTION(0x5, "keypad")),	/* OUT6 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(A, 15),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ECRS */
		  SUNXI_FUNCTION(0x3, "uart1"),		/* RTS */
		  SUNXI_FUNCTION(0x4, "uart3"),		/* RX */
		  SUNXI_FUNCTION(0x5, "keypad")),	/* OUT7 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(A, 16),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ECOL */
		  SUNXI_FUNCTION(0x3, "uart2")),	/* TX */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(A, 17),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ETXERR */
		  SUNXI_FUNCTION(0x3, "uart2"),		/* RX */
		  SUNXI_FUNCTION_IRQ(0x6, 31)),		/* EINT31 */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c0")),		/* SCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c0")),		/* SDA */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "pwm"),		/* PWM0 */
		  SUNXI_FUNCTION_VARIANT(0x3,
					 "spdif",	/* DO */
					 PINCTRL_SUN5I_GR8),
		  SUNXI_FUNCTION_IRQ(0x6, 16)),		/* EINT16 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ir0"),		/* TX */
		  SUNXI_FUNCTION_IRQ(0x6, 17)),		/* EINT17 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ir0"),		/* RX */
		  SUNXI_FUNCTION_IRQ(0x6, 18)),		/* EINT18 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(B, 5),
		  PINCTRL_SUN5I_A10S | PINCTRL_SUN5I_GR8,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2s"),		/* MCLK */
		  SUNXI_FUNCTION_IRQ(0x6, 19)),		/* EINT19 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(B, 6),
		  PINCTRL_SUN5I_A10S | PINCTRL_SUN5I_GR8,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2s"),		/* BCLK */
		  SUNXI_FUNCTION_IRQ(0x6, 20)),		/* EINT20 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(B, 7),
		  PINCTRL_SUN5I_A10S | PINCTRL_SUN5I_GR8,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2s"),		/* LRCK */
		  SUNXI_FUNCTION_IRQ(0x6, 21)),		/* EINT21 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(B, 8),
		  PINCTRL_SUN5I_A10S | PINCTRL_SUN5I_GR8,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2s"),		/* DO */
		  SUNXI_FUNCTION_IRQ(0x6, 22)),		/* EINT22 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(B, 9),
		  PINCTRL_SUN5I_A10S | PINCTRL_SUN5I_GR8,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2s"),		/* DI */
		  SUNXI_FUNCTION_VARIANT(0x3,
					 "spdif",	/* DI */
					 PINCTRL_SUN5I_GR8),
		  SUNXI_FUNCTION_IRQ(0x6, 23)),		/* EINT23 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 10),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi2"),		/* CS1 */
		  SUNXI_FUNCTION_VARIANT(0x3,
					 "spdif",	/* DO */
					 PINCTRL_SUN5I_GR8),
		  SUNXI_FUNCTION_IRQ(0x6, 24)),		/* EINT24 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(B, 11),
		  PINCTRL_SUN5I_A10S | PINCTRL_SUN5I_GR8,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi2"),		/* CS0 */
		  SUNXI_FUNCTION(0x3, "jtag"),		/* MS0 */
		  SUNXI_FUNCTION_IRQ(0x6, 25)),		/* EINT25 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(B, 12),
		  PINCTRL_SUN5I_A10S | PINCTRL_SUN5I_GR8,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi2"),		/* CLK */
		  SUNXI_FUNCTION(0x3, "jtag"),		/* CK0 */
		  SUNXI_FUNCTION_IRQ(0x6, 26)),		/* EINT26 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(B, 13),
		  PINCTRL_SUN5I_A10S | PINCTRL_SUN5I_GR8,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi2"),		/* MOSI */
		  SUNXI_FUNCTION(0x3, "jtag"),		/* DO0 */
		  SUNXI_FUNCTION_IRQ(0x6, 27)),		/* EINT27 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(B, 14),
		  PINCTRL_SUN5I_A10S | PINCTRL_SUN5I_GR8,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi2"),		/* MISO */
		  SUNXI_FUNCTION(0x3, "jtag"),		/* DI0 */
		  SUNXI_FUNCTION_IRQ(0x6, 28)),		/* EINT28 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 15),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c1")),		/* SCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 16),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c1")),		/* SDA */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 17),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c2")),		/* SCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 18),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c2")),		/* SDA */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(B, 19),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart0"),		/* TX */
		  SUNXI_FUNCTION_IRQ(0x6, 29)),		/* EINT29 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(B, 20),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart0"),		/* RX */
		  SUNXI_FUNCTION_IRQ(0x6, 30)),		/* EINT30 */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NWE */
		  SUNXI_FUNCTION(0x3, "spi0")),		/* MOSI */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NALE */
		  SUNXI_FUNCTION(0x3, "spi0")),		/* MISO */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NCLE */
		  SUNXI_FUNCTION(0x3, "spi0")),		/* CLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NCE1 */
		  SUNXI_FUNCTION(0x3, "spi0")),		/* CS0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0")),	/* NCE0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0")),	/* NRE */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NRB0 */
		  SUNXI_FUNCTION(0x3, "mmc2")),		/* CMD */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NRB1 */
		  SUNXI_FUNCTION(0x3, "mmc2")),		/* CLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 8),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NDQ0 */
		  SUNXI_FUNCTION(0x3, "mmc2")),		/* D0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 9),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NDQ1 */
		  SUNXI_FUNCTION(0x3, "mmc2")),		/* D1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 10),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NDQ2 */
		  SUNXI_FUNCTION(0x3, "mmc2")),		/* D2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 11),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NDQ3 */
		  SUNXI_FUNCTION(0x3, "mmc2")),		/* D3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 12),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NDQ4 */
		  SUNXI_FUNCTION(0x3, "mmc2")),		/* D4 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 13),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NDQ5 */
		  SUNXI_FUNCTION(0x3, "mmc2")),		/* D5 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 14),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NDQ6 */
		  SUNXI_FUNCTION(0x3, "mmc2")),		/* D6 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 15),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NDQ7 */
		  SUNXI_FUNCTION(0x3, "mmc2")),		/* D7 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(C, 16),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NWP */
		  SUNXI_FUNCTION(0x4, "uart3")),	/* TX */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(C, 17),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NCE2 */
		  SUNXI_FUNCTION(0x4, "uart3")),	/* RX */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(C, 18),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NCE3 */
		  SUNXI_FUNCTION(0x3, "uart2"),		/* TX */
		  SUNXI_FUNCTION(0x4, "uart3")),	/* CTS */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 19),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NCE4 */
		  SUNXI_FUNCTION(0x3, "uart2"),		/* RX */
		  SUNXI_FUNCTION(0x4, "uart3")),	/* RTS */
	/* Hole */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 0),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* D0 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 1),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* D1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D2 */
		  SUNXI_FUNCTION(0x3, "uart2")),	/* TX */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D3 */
		  SUNXI_FUNCTION(0x3, "uart2")),	/* RX */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D4 */
		  SUNXI_FUNCTION(0x3, "uart2")),	/* CTS */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D5 */
		  SUNXI_FUNCTION(0x3, "uart2")),	/* RTS */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D6 */
		  SUNXI_FUNCTION(0x3, "emac")),		/* ECRS */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D7 */
		  SUNXI_FUNCTION(0x3, "emac")),		/* ECOL */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 8),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* D8 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 9),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* D9 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 10),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D10 */
		  SUNXI_FUNCTION(0x3, "emac")),		/* ERXD0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 11),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D11 */
		  SUNXI_FUNCTION(0x3, "emac")),		/* ERXD1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 12),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D12 */
		  SUNXI_FUNCTION(0x3, "emac")),		/* ERXD2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 13),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D13 */
		  SUNXI_FUNCTION(0x3, "emac")),		/* ERXD3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 14),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D14 */
		  SUNXI_FUNCTION(0x3, "emac")),		/* ERXCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 15),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D15 */
		  SUNXI_FUNCTION(0x3, "emac")),		/* ERXERR */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 16),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* D16 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 17),
		  PINCTRL_SUN5I_A10S,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* D17 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 18),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D18 */
		  SUNXI_FUNCTION(0x3, "emac")),		/* ERXDV */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 19),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D19 */
		  SUNXI_FUNCTION(0x3, "emac")),		/* ETXD0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 20),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D20 */
		  SUNXI_FUNCTION(0x3, "emac")),		/* ETXD1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 21),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D21 */
		  SUNXI_FUNCTION(0x3, "emac")),		/* ETXD2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 22),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D22 */
		  SUNXI_FUNCTION(0x3, "emac")),		/* ETXD3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 23),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D23 */
		  SUNXI_FUNCTION(0x3, "emac")),		/* ETXEN */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 24),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* CLK */
		  SUNXI_FUNCTION(0x3, "emac")),		/* ETXCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 25),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* DE */
		  SUNXI_FUNCTION(0x3, "emac")),		/* ETXERR */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 26),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* HSYNC */
		  SUNXI_FUNCTION(0x3, "emac")),		/* EMDC */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 27),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* VSYNC */
		  SUNXI_FUNCTION(0x3, "emac")),		/* EMDIO */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* CLK */
		  SUNXI_FUNCTION(0x3, "csi0"),		/* PCK */
		  SUNXI_FUNCTION(0x4, "spi2"),		/* CS0 */
		  SUNXI_FUNCTION_IRQ(0x6, 14)),		/* EINT14 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* ERR */
		  SUNXI_FUNCTION(0x3, "csi0"),		/* CK */
		  SUNXI_FUNCTION(0x4, "spi2"),		/* CLK */
		  SUNXI_FUNCTION_IRQ(0x6, 15)),		/* EINT15 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* SYNC */
		  SUNXI_FUNCTION(0x3, "csi0"),		/* HSYNC */
		  SUNXI_FUNCTION(0x4, "spi2")),		/* MOSI */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* DVLD */
		  SUNXI_FUNCTION(0x3, "csi0"),		/* VSYNC */
		  SUNXI_FUNCTION(0x4, "spi2")),		/* MISO */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* D0 */
		  SUNXI_FUNCTION(0x3, "csi0"),		/* D0 */
		  SUNXI_FUNCTION(0x4, "mmc2")),		/* D0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* D1 */
		  SUNXI_FUNCTION(0x3, "csi0"),		/* D1 */
		  SUNXI_FUNCTION(0x4, "mmc2")),		/* D1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* D2 */
		  SUNXI_FUNCTION(0x3, "csi0"),		/* D2 */
		  SUNXI_FUNCTION(0x4, "mmc2")),		/* D2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* D3 */
		  SUNXI_FUNCTION(0x3, "csi0"),		/* D3 */
		  SUNXI_FUNCTION(0x4, "mmc2")),		/* D3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 8),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* D4 */
		  SUNXI_FUNCTION(0x3, "csi0"),		/* D4 */
		  SUNXI_FUNCTION(0x4, "mmc2")),		/* CMD */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 9),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* D5 */
		  SUNXI_FUNCTION(0x3, "csi0"),		/* D5 */
		  SUNXI_FUNCTION(0x4, "mmc2")),		/* CLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 10),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* D6 */
		  SUNXI_FUNCTION(0x3, "csi0"),		/* D6 */
		  SUNXI_FUNCTION(0x4, "uart1")),	/* TX */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 11),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* D7 */
		  SUNXI_FUNCTION(0x3, "csi0"),		/* D7 */
		  SUNXI_FUNCTION(0x4, "uart1")),	/* RX */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc0"),		/* D1 */
		  SUNXI_FUNCTION(0x4, "jtag")),		/* MS1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc0"),		/* D0 */
		  SUNXI_FUNCTION(0x4, "jtag")),		/* DI1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc0"),		/* CLK */
		  SUNXI_FUNCTION(0x4, "uart0")),	/* TX */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc0"),		/* CMD */
		  SUNXI_FUNCTION(0x4, "jtag")),		/* DO1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc0"),		/* D3 */
		  SUNXI_FUNCTION(0x4, "uart0")),	/* RX */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc0"),		/* D2 */
		  SUNXI_FUNCTION(0x4, "jtag")),		/* CK1 */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x2, "gps"),		/* CLK */
		  SUNXI_FUNCTION_IRQ(0x6, 0)),		/* EINT0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x2, "gps"),		/* SIGN */
		  SUNXI_FUNCTION_IRQ(0x6, 1)),		/* EINT1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x2, "gps"),		/* MAG */
		  SUNXI_FUNCTION_IRQ(0x6, 2)),		/* EINT2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc1"),		/* CMD */
		  SUNXI_FUNCTION(0x4, "uart1"),		/* TX */
		  SUNXI_FUNCTION_IRQ(0x6, 3)),		/* EINT3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc1"),		/* CLK */
		  SUNXI_FUNCTION(0x4, "uart1"),		/* RX */
		  SUNXI_FUNCTION_IRQ(0x6, 4)),		/* EINT4 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(G, 5),
		  PINCTRL_SUN5I_A10S | PINCTRL_SUN5I_GR8,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc1"),		/* DO */
		  SUNXI_FUNCTION(0x4, "uart1"),		/* CTS */
		  SUNXI_FUNCTION_IRQ(0x6, 5)),		/* EINT5 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(G, 6),
		  PINCTRL_SUN5I_A10S | PINCTRL_SUN5I_GR8,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc1"),		/* D1 */
		  SUNXI_FUNCTION(0x4, "uart1"),		/* RTS */
		  SUNXI_FUNCTION(0x5, "uart2"),		/* RTS */
		  SUNXI_FUNCTION_IRQ(0x6, 6)),		/* EINT6 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(G, 7),
		  PINCTRL_SUN5I_A10S | PINCTRL_SUN5I_GR8,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc1"),		/* D2 */
		  SUNXI_FUNCTION(0x5, "uart2"),		/* TX */
		  SUNXI_FUNCTION_IRQ(0x6, 7)),		/* EINT7 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(G, 8),
		  PINCTRL_SUN5I_A10S | PINCTRL_SUN5I_GR8,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc1"),		/* D3 */
		  SUNXI_FUNCTION(0x5, "uart2"),		/* RX */
		  SUNXI_FUNCTION_IRQ(0x6, 8)),		/* EINT8 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 9),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi1"),		/* CS0 */
		  SUNXI_FUNCTION(0x3, "uart3"),		/* TX */
		  SUNXI_FUNCTION_IRQ(0x6, 9)),		/* EINT9 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 10),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi1"),		/* CLK */
		  SUNXI_FUNCTION(0x3, "uart3"),		/* RX */
		  SUNXI_FUNCTION_IRQ(0x6, 10)),		/* EINT10 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 11),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi1"),		/* MOSI */
		  SUNXI_FUNCTION(0x3, "uart3"),		/* CTS */
		  SUNXI_FUNCTION_IRQ(0x6, 11)),		/* EINT11 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 12),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi1"),		/* MISO */
		  SUNXI_FUNCTION(0x3, "uart3"),		/* RTS */
		  SUNXI_FUNCTION_IRQ(0x6, 12)),		/* EINT12 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(G, 13),
		  PINCTRL_SUN5I_A10S | PINCTRL_SUN5I_GR8,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi1"),		/* CS1 */
		  SUNXI_FUNCTION(0x3, "pwm"),		/* PWM1 */
		  SUNXI_FUNCTION(0x5, "uart2"),		/* CTS */
		  SUNXI_FUNCTION_IRQ(0x6, 13)),		/* EINT13 */
};

static const struct sunxi_pinctrl_desc sun5i_pinctrl_data = {
	.pins = sun5i_pins,
	.npins = ARRAY_SIZE(sun5i_pins),
	.irq_banks = 1,
	.disable_strict_mode = true,
};

static int sun5i_pinctrl_probe(struct platform_device *pdev)
{
	unsigned long variant = (unsigned long)of_device_get_match_data(&pdev->dev);

	return sunxi_pinctrl_init_with_variant(pdev, &sun5i_pinctrl_data,
					       variant);
}

static const struct of_device_id sun5i_pinctrl_match[] = {
	{
		.compatible = "allwinner,sun5i-a10s-pinctrl",
		.data = (void *)PINCTRL_SUN5I_A10S
	},
	{
		.compatible = "allwinner,sun5i-a13-pinctrl",
		.data = (void *)PINCTRL_SUN5I_A13
	},
	{
		.compatible = "nextthing,gr8-pinctrl",
		.data = (void *)PINCTRL_SUN5I_GR8
	},
	{ },
};

static struct platform_driver sun5i_pinctrl_driver = {
	.probe	= sun5i_pinctrl_probe,
	.driver	= {
		.name		= "sun5i-pinctrl",
		.of_match_table	= sun5i_pinctrl_match,
	},
};
builtin_platform_driver(sun5i_pinctrl_driver);
