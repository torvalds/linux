/*
 * Allwinner A20 SoCs pinctrl driver.
 *
 * Copyright (C) 2014 Maxime Ripard
 *
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

static const struct sunxi_desc_pin sun7i_a20_pins[] = {
	SUNXI_PIN(SUNXI_PINCTRL_PIN(A, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ERXD3 */
		  SUNXI_FUNCTION(0x3, "spi1"),		/* CS0 */
		  SUNXI_FUNCTION(0x4, "uart2"),		/* RTS */
		  SUNXI_FUNCTION(0x5, "gmac")),		/* GRXD3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(A, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ERXD2 */
		  SUNXI_FUNCTION(0x3, "spi1"),		/* CLK */
		  SUNXI_FUNCTION(0x4, "uart2"),		/* CTS */
		  SUNXI_FUNCTION(0x5, "gmac")),		/* GRXD2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(A, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ERXD1 */
		  SUNXI_FUNCTION(0x3, "spi1"),		/* MOSI */
		  SUNXI_FUNCTION(0x4, "uart2"),		/* TX */
		  SUNXI_FUNCTION(0x5, "gmac")),		/* GRXD1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(A, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ERXD0 */
		  SUNXI_FUNCTION(0x3, "spi1"),		/* MISO */
		  SUNXI_FUNCTION(0x4, "uart2"),		/* RX */
		  SUNXI_FUNCTION(0x5, "gmac")),		/* GRXD0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(A, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ETXD3 */
		  SUNXI_FUNCTION(0x3, "spi1"),		/* CS1 */
		  SUNXI_FUNCTION(0x5, "gmac")),		/* GTXD3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(A, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ETXD2 */
		  SUNXI_FUNCTION(0x3, "spi3"),		/* CS0 */
		  SUNXI_FUNCTION(0x5, "gmac")),		/* GTXD2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(A, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ETXD1 */
		  SUNXI_FUNCTION(0x3, "spi3"),		/* CLK */
		  SUNXI_FUNCTION(0x5, "gmac")),		/* GTXD1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(A, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ETXD0 */
		  SUNXI_FUNCTION(0x3, "spi3"),		/* MOSI */
		  SUNXI_FUNCTION(0x5, "gmac")),		/* GTXD0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(A, 8),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ERXCK */
		  SUNXI_FUNCTION(0x3, "spi3"),		/* MISO */
		  SUNXI_FUNCTION(0x5, "gmac")),		/* GRXCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(A, 9),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ERXERR */
		  SUNXI_FUNCTION(0x3, "spi3"),		/* CS1 */
		  SUNXI_FUNCTION(0x5, "gmac"),		/* GNULL / ERXERR */
		  SUNXI_FUNCTION(0x6, "i2s1")),		/* MCLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(A, 10),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ERXDV */
		  SUNXI_FUNCTION(0x4, "uart1"),		/* TX */
		  SUNXI_FUNCTION(0x5, "gmac")),		/* GRXCTL / ERXDV */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(A, 11),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* EMDC */
		  SUNXI_FUNCTION(0x4, "uart1"),		/* RX */
		  SUNXI_FUNCTION(0x5, "gmac")),		/* EMDC */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(A, 12),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* EMDIO */
		  SUNXI_FUNCTION(0x3, "uart6"),		/* TX */
		  SUNXI_FUNCTION(0x4, "uart1"),		/* RTS */
		  SUNXI_FUNCTION(0x5, "gmac")),		/* EMDIO */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(A, 13),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ETXEN */
		  SUNXI_FUNCTION(0x3, "uart6"),		/* RX */
		  SUNXI_FUNCTION(0x4, "uart1"),		/* CTS */
		  SUNXI_FUNCTION(0x5, "gmac")),		/* GTXCTL / ETXEN */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(A, 14),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ETXCK */
		  SUNXI_FUNCTION(0x3, "uart7"),		/* TX */
		  SUNXI_FUNCTION(0x4, "uart1"),		/* DTR */
		  SUNXI_FUNCTION(0x5, "gmac"),		/* GNULL / ETXCK */
		  SUNXI_FUNCTION(0x6, "i2s1")),		/* BCLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(A, 15),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ECRS */
		  SUNXI_FUNCTION(0x3, "uart7"),		/* RX */
		  SUNXI_FUNCTION(0x4, "uart1"),		/* DSR */
		  SUNXI_FUNCTION(0x5, "gmac"),		/* GTXCK / ECRS */
		  SUNXI_FUNCTION(0x6, "i2s1")),		/* LRCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(A, 16),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ECOL */
		  SUNXI_FUNCTION(0x3, "can"),		/* TX */
		  SUNXI_FUNCTION(0x4, "uart1"),		/* DCD */
		  SUNXI_FUNCTION(0x5, "gmac"),		/* GCLKIN / ECOL */
		  SUNXI_FUNCTION(0x6, "i2s1")),		/* DO */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(A, 17),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ETXERR */
		  SUNXI_FUNCTION(0x3, "can"),		/* RX */
		  SUNXI_FUNCTION(0x4, "uart1"),		/* RING */
		  SUNXI_FUNCTION(0x5, "gmac"),		/* GNULL / ETXERR */
		  SUNXI_FUNCTION(0x6, "i2s1")),		/* LRCK */
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
		  SUNXI_FUNCTION(0x2, "pwm")),		/* PWM0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ir0"),		/* TX */
		  SUNXI_FUNCTION(0x4, "spdif")),	/* MCLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ir0")),		/* RX */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2s0"),		/* MCLK */
		  SUNXI_FUNCTION(0x3, "ac97")),		/* MCLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2s0"),		/* BCLK */
		  SUNXI_FUNCTION(0x3, "ac97")),		/* BCLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2s0"),		/* LRCK */
		  SUNXI_FUNCTION(0x3, "ac97")),		/* SYNC */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 8),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2s0"),		/* DO0 */
		  SUNXI_FUNCTION(0x3, "ac97")),		/* DO */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 9),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2s0")),		/* DO1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 10),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2s0")),		/* DO2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 11),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2s0")),		/* DO3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 12),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2s0"),		/* DI */
		  SUNXI_FUNCTION(0x3, "ac97"),		/* DI */
		  SUNXI_FUNCTION(0x4, "spdif")),	/* DI */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 13),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi2"),		/* CS1 */
		  SUNXI_FUNCTION(0x4, "spdif")),	/* DO */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 14),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi2"),		/* CS0 */
		  SUNXI_FUNCTION(0x3, "jtag")),		/* MS0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 15),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi2"),		/* CLK */
		  SUNXI_FUNCTION(0x3, "jtag")),		/* CK0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 16),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi2"),		/* MOSI */
		  SUNXI_FUNCTION(0x3, "jtag")),		/* DO0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 17),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi2"),		/* MISO */
		  SUNXI_FUNCTION(0x3, "jtag")),		/* DI0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 18),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c1")),		/* SCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 19),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c1")),		/* SDA */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 20),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c2")),		/* SCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 21),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c2")),		/* SDA */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 22),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart0"),		/* TX */
		  SUNXI_FUNCTION(0x3, "ir1")),		/* TX */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 23),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart0"),		/* RX */
		  SUNXI_FUNCTION(0x3, "ir1")),		/* RX */
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
		  SUNXI_FUNCTION(0x3, "spi0")),		/* SCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0")),	/* NCE1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0")),	/* NCE0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0")),	/* NRE# */
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
		  SUNXI_FUNCTION(0x2, "nand0")),	/* NDQ4 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 13),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0")),	/* NDQ5 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 14),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0")),	/* NDQ6 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 15),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0")),	/* NDQ7 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 16),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0")),	/* NWP */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 17),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0")),	/* NCE2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 18),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0")),	/* NCE3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 19),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NCE4 */
		  SUNXI_FUNCTION(0x3, "spi2"),		/* CS0 */
		  SUNXI_FUNCTION_IRQ(0x6, 12)),		/* EINT12 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 20),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NCE5 */
		  SUNXI_FUNCTION(0x3, "spi2"),		/* CLK */
		  SUNXI_FUNCTION_IRQ(0x6, 13)),		/* EINT13 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 21),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NCE6 */
		  SUNXI_FUNCTION(0x3, "spi2"),		/* MOSI */
		  SUNXI_FUNCTION_IRQ(0x6, 14)),		/* EINT14 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 22),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NCE7 */
		  SUNXI_FUNCTION(0x3, "spi2"),		/* MISO */
		  SUNXI_FUNCTION_IRQ(0x6, 15)),		/* EINT15 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 23),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "spi0")),		/* CS0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 24),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0")),	/* NDQS */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D0 */
		  SUNXI_FUNCTION(0x3, "lvds0")),	/* VP0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D1 */
		  SUNXI_FUNCTION(0x3, "lvds0")),	/* VN0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D2 */
		  SUNXI_FUNCTION(0x3, "lvds0")),	/* VP1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D3 */
		  SUNXI_FUNCTION(0x3, "lvds0")),	/* VN1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D4 */
		  SUNXI_FUNCTION(0x3, "lvds0")),	/* VP2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D5 */
		  SUNXI_FUNCTION(0x3, "lvds0")),	/* VN2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D6 */
		  SUNXI_FUNCTION(0x3, "lvds0")),	/* VPC */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D7 */
		  SUNXI_FUNCTION(0x3, "lvds0")),	/* VNC */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 8),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D8 */
		  SUNXI_FUNCTION(0x3, "lvds0")),	/* VP3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 9),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D9 */
		  SUNXI_FUNCTION(0x3, "lvds0")),	/* VM3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 10),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D10 */
		  SUNXI_FUNCTION(0x3, "lvds1")),	/* VP0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 11),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D11 */
		  SUNXI_FUNCTION(0x3, "lvds1")),	/* VN0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 12),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D12 */
		  SUNXI_FUNCTION(0x3, "lvds1")),	/* VP1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 13),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D13 */
		  SUNXI_FUNCTION(0x3, "lvds1")),	/* VN1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 14),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D14 */
		  SUNXI_FUNCTION(0x3, "lvds1")),	/* VP2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 15),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D15 */
		  SUNXI_FUNCTION(0x3, "lvds1")),	/* VN2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 16),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D16 */
		  SUNXI_FUNCTION(0x3, "lvds1")),	/* VPC */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 17),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D17 */
		  SUNXI_FUNCTION(0x3, "lvds1")),	/* VNC */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 18),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D18 */
		  SUNXI_FUNCTION(0x3, "lvds1")),	/* VP3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 19),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D19 */
		  SUNXI_FUNCTION(0x3, "lvds1")),	/* VN3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 20),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D20 */
		  SUNXI_FUNCTION(0x3, "csi1")),		/* MCLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 21),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D21 */
		  SUNXI_FUNCTION(0x3, "sim")),		/* VPPEN */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 22),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D22 */
		  SUNXI_FUNCTION(0x3, "sim")),		/* VPPPP */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 23),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D23 */
		  SUNXI_FUNCTION(0x3, "sim")),		/* DET */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 24),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* CLK */
		  SUNXI_FUNCTION(0x3, "sim")),		/* VCCEN */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 25),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* DE */
		  SUNXI_FUNCTION(0x3, "sim")),		/* RST */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 26),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* HSYNC */
		  SUNXI_FUNCTION(0x3, "sim")),		/* SCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 27),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* VSYNC */
		  SUNXI_FUNCTION(0x3, "sim")),		/* SDA */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* CLK */
		  SUNXI_FUNCTION(0x3, "csi0")),		/* PCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* ERR */
		  SUNXI_FUNCTION(0x3, "csi0")),		/* CK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* SYNC */
		  SUNXI_FUNCTION(0x3, "csi0")),		/* HSYNC */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* DVLD */
		  SUNXI_FUNCTION(0x3, "csi0")),		/* VSYNC */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* D0 */
		  SUNXI_FUNCTION(0x3, "csi0")),		/* D0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* D1 */
		  SUNXI_FUNCTION(0x3, "csi0"),		/* D1 */
		  SUNXI_FUNCTION(0x4, "sim")),		/* VPPEN */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* D2 */
		  SUNXI_FUNCTION(0x3, "csi0")),		/* D2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* D3 */
		  SUNXI_FUNCTION(0x3, "csi0")),		/* D3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 8),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* D4 */
		  SUNXI_FUNCTION(0x3, "csi0")),		/* D4 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 9),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* D5 */
		  SUNXI_FUNCTION(0x3, "csi0")),		/* D5 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 10),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* D6 */
		  SUNXI_FUNCTION(0x3, "csi0")),		/* D6 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 11),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* D7 */
		  SUNXI_FUNCTION(0x3, "csi0")),		/* D7 */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc0"),		/* D1 */
		  SUNXI_FUNCTION(0x4, "jtag")),		/* MSI */
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
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts1"),		/* CLK */
		  SUNXI_FUNCTION(0x3, "csi1"),		/* PCK */
		  SUNXI_FUNCTION(0x4, "mmc1")),		/* CMD */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts1"),		/* ERR */
		  SUNXI_FUNCTION(0x3, "csi1"),		/* CK */
		  SUNXI_FUNCTION(0x4, "mmc1")),		/* CLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts1"),		/* SYNC */
		  SUNXI_FUNCTION(0x3, "csi1"),		/* HSYNC */
		  SUNXI_FUNCTION(0x4, "mmc1")),		/* D0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts1"),		/* DVLD */
		  SUNXI_FUNCTION(0x3, "csi1"),		/* VSYNC */
		  SUNXI_FUNCTION(0x4, "mmc1")),		/* D1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts1"),		/* D0 */
		  SUNXI_FUNCTION(0x3, "csi1"),		/* D0 */
		  SUNXI_FUNCTION(0x4, "mmc1"),		/* D2 */
		  SUNXI_FUNCTION(0x5, "csi0")),		/* D8 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts1"),		/* D1 */
		  SUNXI_FUNCTION(0x3, "csi1"),		/* D1 */
		  SUNXI_FUNCTION(0x4, "mmc1"),		/* D3 */
		  SUNXI_FUNCTION(0x5, "csi0")),		/* D9 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts1"),		/* D2 */
		  SUNXI_FUNCTION(0x3, "csi1"),		/* D2 */
		  SUNXI_FUNCTION(0x4, "uart3"),		/* TX */
		  SUNXI_FUNCTION(0x5, "csi0")),		/* D10 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts1"),		/* D3 */
		  SUNXI_FUNCTION(0x3, "csi1"),		/* D3 */
		  SUNXI_FUNCTION(0x4, "uart3"),		/* RX */
		  SUNXI_FUNCTION(0x5, "csi0")),		/* D11 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 8),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts1"),		/* D4 */
		  SUNXI_FUNCTION(0x3, "csi1"),		/* D4 */
		  SUNXI_FUNCTION(0x4, "uart3"),		/* RTS */
		  SUNXI_FUNCTION(0x5, "csi0")),		/* D12 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 9),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts1"),		/* D5 */
		  SUNXI_FUNCTION(0x3, "csi1"),		/* D5 */
		  SUNXI_FUNCTION(0x4, "uart3"),		/* CTS */
		  SUNXI_FUNCTION(0x5, "csi0")),		/* D13 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 10),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts1"),		/* D6 */
		  SUNXI_FUNCTION(0x3, "csi1"),		/* D6 */
		  SUNXI_FUNCTION(0x4, "uart4"),		/* TX */
		  SUNXI_FUNCTION(0x5, "csi0")),		/* D14 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 11),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts1"),		/* D7 */
		  SUNXI_FUNCTION(0x3, "csi1"),		/* D7 */
		  SUNXI_FUNCTION(0x4, "uart4"),		/* RX */
		  SUNXI_FUNCTION(0x5, "csi0")),		/* D15 */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D0 */
		  SUNXI_FUNCTION(0x4, "uart3"),		/* TX */
		  SUNXI_FUNCTION_IRQ(0x6, 0),		/* EINT0 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D1 */
		  SUNXI_FUNCTION(0x4, "uart3"),		/* RX */
		  SUNXI_FUNCTION_IRQ(0x6, 1),		/* EINT1 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D2 */
		  SUNXI_FUNCTION(0x4, "uart3"),		/* RTS */
		  SUNXI_FUNCTION_IRQ(0x6, 2),		/* EINT2 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D3 */
		  SUNXI_FUNCTION(0x4, "uart3"),		/* CTS */
		  SUNXI_FUNCTION_IRQ(0x6, 3),		/* EINT3 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D4 */
		  SUNXI_FUNCTION(0x4, "uart4"),		/* TX */
		  SUNXI_FUNCTION_IRQ(0x6, 4),		/* EINT4 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D4 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D5 */
		  SUNXI_FUNCTION(0x4, "uart4"),		/* RX */
		  SUNXI_FUNCTION_IRQ(0x6, 5),		/* EINT5 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D5 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D6 */
		  SUNXI_FUNCTION(0x4, "uart5"),		/* TX */
		  SUNXI_FUNCTION(0x5, "ms"),		/* BS */
		  SUNXI_FUNCTION_IRQ(0x6, 6),		/* EINT6 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D6 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D7 */
		  SUNXI_FUNCTION(0x4, "uart5"),		/* RX */
		  SUNXI_FUNCTION(0x5, "ms"),		/* CLK */
		  SUNXI_FUNCTION_IRQ(0x6, 7),		/* EINT7 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D7 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 8),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D8 */
		  SUNXI_FUNCTION(0x3, "emac"),		/* ERXD3 */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* IN0 */
		  SUNXI_FUNCTION(0x5, "ms"),		/* D0 */
		  SUNXI_FUNCTION_IRQ(0x6, 8),		/* EINT8 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D8 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 9),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D9 */
		  SUNXI_FUNCTION(0x3, "emac"),		/* ERXD2 */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* IN1 */
		  SUNXI_FUNCTION(0x5, "ms"),		/* D1 */
		  SUNXI_FUNCTION_IRQ(0x6, 9),		/* EINT9 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D9 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 10),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D10 */
		  SUNXI_FUNCTION(0x3, "emac"),		/* ERXD1 */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* IN2 */
		  SUNXI_FUNCTION(0x5, "ms"),		/* D2 */
		  SUNXI_FUNCTION_IRQ(0x6, 10),		/* EINT10 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D10 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 11),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D11 */
		  SUNXI_FUNCTION(0x3, "emac"),		/* ERXD0 */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* IN3 */
		  SUNXI_FUNCTION(0x5, "ms"),		/* D3 */
		  SUNXI_FUNCTION_IRQ(0x6, 11),		/* EINT11 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D11 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 12),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D12 */
		  SUNXI_FUNCTION(0x4, "ps2"),		/* SCK1 */
		  SUNXI_FUNCTION_IRQ(0x6, 12),		/* EINT12 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D12 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 13),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D13 */
		  SUNXI_FUNCTION(0x4, "ps2"),		/* SDA1 */
		  SUNXI_FUNCTION(0x5, "sim"),		/* RST */
		  SUNXI_FUNCTION_IRQ(0x6, 13),		/* EINT13 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D13 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 14),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D14 */
		  SUNXI_FUNCTION(0x3, "emac"),		/* ETXD3 */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* IN4 */
		  SUNXI_FUNCTION(0x5, "sim"),		/* VPPEN */
		  SUNXI_FUNCTION_IRQ(0x6, 14),		/* EINT14 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D14 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 15),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D15 */
		  SUNXI_FUNCTION(0x3, "emac"),		/* ETXD3 */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* IN5 */
		  SUNXI_FUNCTION(0x5, "sim"),		/* VPPPP */
		  SUNXI_FUNCTION_IRQ(0x6, 15),		/* EINT15 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D15 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 16),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D16 */
		  SUNXI_FUNCTION(0x3, "emac"),		/* ETXD2 */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* IN6 */
		  SUNXI_FUNCTION_IRQ(0x6, 16),		/* EINT16 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D16 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 17),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D17 */
		  SUNXI_FUNCTION(0x3, "emac"),		/* ETXD1 */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* IN7 */
		  SUNXI_FUNCTION(0x5, "sim"),		/* VCCEN */
		  SUNXI_FUNCTION_IRQ(0x6, 17),		/* EINT17 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D17 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 18),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D18 */
		  SUNXI_FUNCTION(0x3, "emac"),		/* ETXD0 */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* OUT0 */
		  SUNXI_FUNCTION(0x5, "sim"),		/* SCK */
		  SUNXI_FUNCTION_IRQ(0x6, 18),		/* EINT18 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D18 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 19),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D19 */
		  SUNXI_FUNCTION(0x3, "emac"),		/* ERXERR */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* OUT1 */
		  SUNXI_FUNCTION(0x5, "sim"),		/* SDA */
		  SUNXI_FUNCTION_IRQ(0x6, 19),		/* EINT19 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D19 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 20),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D20 */
		  SUNXI_FUNCTION(0x3, "emac"),		/* ERXDV */
		  SUNXI_FUNCTION(0x4, "can"),		/* TX */
		  SUNXI_FUNCTION_IRQ(0x6, 20),		/* EINT20 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D20 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 21),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D21 */
		  SUNXI_FUNCTION(0x3, "emac"),		/* EMDC */
		  SUNXI_FUNCTION(0x4, "can"),		/* RX */
		  SUNXI_FUNCTION_IRQ(0x6, 21),		/* EINT21 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D21 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 22),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D22 */
		  SUNXI_FUNCTION(0x3, "emac"),		/* EMDIO */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* OUT2 */
		  SUNXI_FUNCTION(0x5, "mmc1"),		/* CMD */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D22 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 23),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D23 */
		  SUNXI_FUNCTION(0x3, "emac"),		/* ETXEN */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* OUT3 */
		  SUNXI_FUNCTION(0x5, "mmc1"),		/* CLK */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D23 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 24),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* CLK */
		  SUNXI_FUNCTION(0x3, "emac"),		/* ETXCK */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* OUT4 */
		  SUNXI_FUNCTION(0x5, "mmc1"),		/* D0 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* PCLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 25),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* DE */
		  SUNXI_FUNCTION(0x3, "emac"),		/* ECRS */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* OUT5 */
		  SUNXI_FUNCTION(0x5, "mmc1"),		/* D1 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* FIELD */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 26),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* HSYNC */
		  SUNXI_FUNCTION(0x3, "emac"),		/* ECOL */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* OUT6 */
		  SUNXI_FUNCTION(0x5, "mmc1"),		/* D2 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* HSYNC */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 27),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* VSYNC */
		  SUNXI_FUNCTION(0x3, "emac"),		/* ETXERR */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* OUT7 */
		  SUNXI_FUNCTION(0x5, "mmc1"),		/* D3 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* VSYNC */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(I, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "i2c3")),		/* SCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(I, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "i2c3")),		/* SDA */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(I, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "i2c4")),		/* SCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(I, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "pwm"),		/* PWM1 */
		  SUNXI_FUNCTION(0x3, "i2c4")),		/* SDA */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(I, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc3")),		/* CMD */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(I, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc3")),		/* CLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(I, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc3")),		/* D0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(I, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc3")),		/* D1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(I, 8),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc3")),		/* D2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(I, 9),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc3")),		/* D3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(I, 10),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi0"),		/* CS0 */
		  SUNXI_FUNCTION(0x3, "uart5"),		/* TX */
		  SUNXI_FUNCTION_IRQ(0x5, 22)),		/* EINT22 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(I, 11),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi0"),		/* CLK */
		  SUNXI_FUNCTION(0x3, "uart5"),		/* RX */
		  SUNXI_FUNCTION_IRQ(0x5, 23)),		/* EINT23 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(I, 12),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi0"),		/* MOSI */
		  SUNXI_FUNCTION(0x3, "uart6"),		/* TX */
		  SUNXI_FUNCTION(0x4, "clk_out_a"),	/* CLK_OUT_A */
		  SUNXI_FUNCTION_IRQ(0x5, 24)),		/* EINT24 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(I, 13),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi0"),		/* MISO */
		  SUNXI_FUNCTION(0x3, "uart6"),		/* RX */
		  SUNXI_FUNCTION(0x4, "clk_out_b"),	/* CLK_OUT_B */
		  SUNXI_FUNCTION_IRQ(0x5, 25)),		/* EINT25 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(I, 14),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi0"),		/* CS1 */
		  SUNXI_FUNCTION(0x3, "ps2"),		/* SCK1 */
		  SUNXI_FUNCTION(0x4, "timer4"),	/* TCLKIN0 */
		  SUNXI_FUNCTION_IRQ(0x5, 26)),		/* EINT26 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(I, 15),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi1"),		/* CS1 */
		  SUNXI_FUNCTION(0x3, "ps2"),		/* SDA1 */
		  SUNXI_FUNCTION(0x4, "timer5"),	/* TCLKIN1 */
		  SUNXI_FUNCTION_IRQ(0x5, 27)),		/* EINT27 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(I, 16),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi1"),		/* CS0 */
		  SUNXI_FUNCTION(0x3, "uart2"),		/* RTS */
		  SUNXI_FUNCTION_IRQ(0x5, 28)),		/* EINT28 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(I, 17),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi1"),		/* CLK */
		  SUNXI_FUNCTION(0x3, "uart2"),		/* CTS */
		  SUNXI_FUNCTION_IRQ(0x5, 29)),		/* EINT29 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(I, 18),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi1"),		/* MOSI */
		  SUNXI_FUNCTION(0x3, "uart2"),		/* TX */
		  SUNXI_FUNCTION_IRQ(0x5, 30)),		/* EINT30 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(I, 19),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi1"),		/* MISO */
		  SUNXI_FUNCTION(0x3, "uart2"),		/* RX */
		  SUNXI_FUNCTION_IRQ(0x5, 31)),		/* EINT31 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(I, 20),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ps2"),		/* SCK0 */
		  SUNXI_FUNCTION(0x3, "uart7"),		/* TX */
		  SUNXI_FUNCTION(0x4, "hdmi")),		/* HSCL */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(I, 21),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ps2"),		/* SDA0 */
		  SUNXI_FUNCTION(0x3, "uart7"),		/* RX */
		  SUNXI_FUNCTION(0x4, "hdmi")),		/* HSDA */
};

static const struct sunxi_pinctrl_desc sun7i_a20_pinctrl_data = {
	.pins = sun7i_a20_pins,
	.npins = ARRAY_SIZE(sun7i_a20_pins),
};

static int sun7i_a20_pinctrl_probe(struct platform_device *pdev)
{
	return sunxi_pinctrl_init(pdev,
				  &sun7i_a20_pinctrl_data);
}

static struct of_device_id sun7i_a20_pinctrl_match[] = {
	{ .compatible = "allwinner,sun7i-a20-pinctrl", },
	{}
};
MODULE_DEVICE_TABLE(of, sun7i_a20_pinctrl_match);

static struct platform_driver sun7i_a20_pinctrl_driver = {
	.probe	= sun7i_a20_pinctrl_probe,
	.driver	= {
		.name		= "sun7i-a20-pinctrl",
		.owner		= THIS_MODULE,
		.of_match_table	= sun7i_a20_pinctrl_match,
	},
};
module_platform_driver(sun7i_a20_pinctrl_driver);

MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com");
MODULE_DESCRIPTION("Allwinner A20 pinctrl driver");
MODULE_LICENSE("GPL");
