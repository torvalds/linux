/*
 * Allwinner new F-series F1C100s SoC (suniv) pinctrl driver.
 *
 * Copyright (C) 2018 Icenowy Zheng
 *
 * Icenowy Zheng <icenowy@aosc.io>
 *
 * Copyright (C) 2014 Jackie Hwang
 *
 * Jackie Hwang <huangshr@allwinnertech.com>
 *
 * Copyright (C) 2014 Chen-Yu Tsai
 *
 * Chen-Yu Tsai <wens@csie.org>
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
static const struct sunxi_desc_pin suniv_f1c100s_pins[] = {
	SUNXI_PIN(SUNXI_PINCTRL_PIN(A, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "rtp"),		/* X1 */
		  SUNXI_FUNCTION(0x4, "i2s"),		/* BCLK */
		  SUNXI_FUNCTION(0x5, "uart1"),		/* RTS */
		  SUNXI_FUNCTION(0x6, "spi1")),		/* CS */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(A, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "rtp"),		/* X2 */
		  SUNXI_FUNCTION(0x4, "i2s"),		/* LRCK */
		  SUNXI_FUNCTION(0x5, "uart1"),		/* CTS */
		  SUNXI_FUNCTION(0x6, "spi1")),		/* MOSI */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(A, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "rtp"),		/* Y1 */
		  SUNXI_FUNCTION(0x3, "pwm0"),		/* PWM0 */
		  SUNXI_FUNCTION(0x4, "i2s"),		/* IN */
		  SUNXI_FUNCTION(0x5, "uart1"),		/* RX */
		  SUNXI_FUNCTION(0x6, "spi1")),		/* MOSI */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(A, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "rtp"),		/* Y2 */
		  SUNXI_FUNCTION(0x3, "ir0"),		/* RX */
		  SUNXI_FUNCTION(0x4, "i2s"),		/* OUT */
		  SUNXI_FUNCTION(0x5, "uart1"),		/* TX */
		  SUNXI_FUNCTION(0x6, "spi1")),		/* MISO */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "dram"),		/* DQS0 */
		  SUNXI_FUNCTION(0x3, "i2c1"),		/* SCK */
		  SUNXI_FUNCTION(0x4, "i2s"),		/* BCLK */
		  SUNXI_FUNCTION(0x5, "uart1"),		/* RTS */
		  SUNXI_FUNCTION(0x6, "spi1")),		/* CS */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "dram"),		/* DQS1 */
		  SUNXI_FUNCTION(0x3, "i2c1"),		/* SDA */
		  SUNXI_FUNCTION(0x4, "i2s"),		/* LRCK */
		  SUNXI_FUNCTION(0x5, "uart1"),		/* CTS */
		  SUNXI_FUNCTION(0x6, "spi1")),		/* MOSI */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "dram"),		/* CKE */
		  SUNXI_FUNCTION(0x3, "pwm0"),		/* PWM0 */
		  SUNXI_FUNCTION(0x4, "i2s"),		/* IN */
		  SUNXI_FUNCTION(0x5, "uart1"),		/* RX */
		  SUNXI_FUNCTION(0x6, "spi1")),		/* CLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "dram"),		/* DDR_REF_D */
		  SUNXI_FUNCTION(0x3, "ir0"),		/* RX */
		  SUNXI_FUNCTION(0x4, "i2s"),		/* OUT */
		  SUNXI_FUNCTION(0x5, "uart1"),		/* TX */
		  SUNXI_FUNCTION(0x6, "spi1")),		/* MISO */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 0),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi0"),		/* CLK */
		  SUNXI_FUNCTION(0x3, "mmc1")),		/* CLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi0"),		/* CS */
		  SUNXI_FUNCTION(0x3, "mmc1")),		/* CMD */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi0"),		/* MISO */
		  SUNXI_FUNCTION(0x3, "mmc1")),		/* D0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi0"),		/* MOSI */
		  SUNXI_FUNCTION(0x3, "uart0")),	/* TX */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd"),		/* D2 */
		  SUNXI_FUNCTION(0x3, "i2c0"),		/* SDA */
		  SUNXI_FUNCTION(0x4, "rsb"),		/* SDA */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 0)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd"),		/* D3 */
		  SUNXI_FUNCTION(0x3, "uart1"),		/* RTS */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 1)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd"),		/* D4*/
		  SUNXI_FUNCTION(0x3, "uart1"),		/* CTS */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 2)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd"),		/* D5 */
		  SUNXI_FUNCTION(0x3, "uart1"),		/* RX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 3)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd"),		/* D6 */
		  SUNXI_FUNCTION(0x3, "uart1"),		/* TX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 4)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd"),		/* D7 */
		  SUNXI_FUNCTION(0x3, "i2c1"),		/* SCK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 5)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd"),		/* D10 */
		  SUNXI_FUNCTION(0x3, "i2c1"),		/* SDA */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 6)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd"),		/* D11 */
		  SUNXI_FUNCTION(0x3, "i2s"),		/* MCLK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 7)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 8),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd"),		/* D12 */
		  SUNXI_FUNCTION(0x3, "i2s"),		/* BCLK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 8)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 9),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd"),		/* D13 */
		  SUNXI_FUNCTION(0x3, "i2s"),		/* LRCK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 9)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 10),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd"),		/* D14 */
		  SUNXI_FUNCTION(0x3, "i2s"),		/* IN */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 10)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 11),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd"),		/* D15 */
		  SUNXI_FUNCTION(0x3, "i2s"),		/* OUT */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 11)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 12),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd"),		/* D18 */
		  SUNXI_FUNCTION(0x3, "i2c0"),		/* SCK */
		  SUNXI_FUNCTION(0x4, "rsb"),		/* SCK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 12)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 13),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd"),		/* D19 */
		  SUNXI_FUNCTION(0x3, "uart2"),		/* TX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 13)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 14),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd"),		/* D20 */
		  SUNXI_FUNCTION(0x3, "lvds1"),		/* RX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 14)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 15),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd"),		/* D21 */
		  SUNXI_FUNCTION(0x3, "uart2"),		/* RTS */
		  SUNXI_FUNCTION(0x4, "i2c2"),		/* SCK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 15)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 16),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd"),		/* D22 */
		  SUNXI_FUNCTION(0x3, "uart2"),		/* CTS */
		  SUNXI_FUNCTION(0x4, "i2c2"),		/* SDA */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 16)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 17),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd"),		/* D23 */
		  SUNXI_FUNCTION(0x3, "spdif"),		/* OUT */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 17)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 18),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd"),		/* CLK */
		  SUNXI_FUNCTION(0x3, "spi0"),		/* CS */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 18)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 19),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd"),		/* DE */
		  SUNXI_FUNCTION(0x3, "spi0"),		/* MOSI */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 19)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 20),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd"),		/* HYSNC */
		  SUNXI_FUNCTION(0x3, "spi0"),		/* CLK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 20)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 21),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd"),		/* VSYNC */
		  SUNXI_FUNCTION(0x3, "spi0"),		/* MISO */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 21)),
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* HSYNC */
		  SUNXI_FUNCTION(0x3, "lcd"),		/* D0 */
		  SUNXI_FUNCTION(0x4, "i2c2"),		/* SCK */
		  SUNXI_FUNCTION(0x5, "uart0"),		/* RX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 0)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* VSYNC */
		  SUNXI_FUNCTION(0x3, "lcd"),		/* D1 */
		  SUNXI_FUNCTION(0x4, "i2c2"),		/* SDA */
		  SUNXI_FUNCTION(0x5, "uart0"),		/* TX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 1)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* PCLK */
		  SUNXI_FUNCTION(0x3, "lcd"),		/* D8 */
		  SUNXI_FUNCTION(0x4, "clk"),		/* OUT */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 2)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* D0 */
		  SUNXI_FUNCTION(0x3, "lcd"),		/* D9 */
		  SUNXI_FUNCTION(0x4, "i2s"),		/* BCLK */
		  SUNXI_FUNCTION(0x5, "rsb"),		/* SCK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 3)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* D1 */
		  SUNXI_FUNCTION(0x3, "lcd"),		/* D16 */
		  SUNXI_FUNCTION(0x4, "i2s"),		/* LRCK */
		  SUNXI_FUNCTION(0x5, "rsb"),		/* SDA */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 4)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* D2 */
		  SUNXI_FUNCTION(0x3, "lcd"),		/* D17 */
		  SUNXI_FUNCTION(0x4, "i2s"),		/* IN */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 5)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* D3 */
		  SUNXI_FUNCTION(0x3, "pwm1"),		/* PWM1 */
		  SUNXI_FUNCTION(0x4, "i2s"),		/* OUT */
		  SUNXI_FUNCTION(0x5, "spdif"),		/* OUT */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 6)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* D4 */
		  SUNXI_FUNCTION(0x3, "uart2"),		/* TX */
		  SUNXI_FUNCTION(0x4, "spi1"),		/* CS */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 7)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 8),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* D5 */
		  SUNXI_FUNCTION(0x3, "uart2"),		/* RX */
		  SUNXI_FUNCTION(0x4, "spi1"),		/* MOSI */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 8)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 9),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* D6 */
		  SUNXI_FUNCTION(0x3, "uart2"),		/* RTS */
		  SUNXI_FUNCTION(0x4, "spi1"),		/* CLK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 9)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 10),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* D7 */
		  SUNXI_FUNCTION(0x3, "uart2"),		/* CTS */
		  SUNXI_FUNCTION(0x4, "spi1"),		/* MISO */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 10)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 11),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "clk0"),		/* OUT */
		  SUNXI_FUNCTION(0x3, "i2c0"),		/* SCK */
		  SUNXI_FUNCTION(0x4, "ir"),		/* RX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 11)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 12),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2s"),		/* MCLK */
		  SUNXI_FUNCTION(0x3, "i2c0"),		/* SDA */
		  SUNXI_FUNCTION(0x4, "pwm0"),		/* PWM0 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 12)),

	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc0"),		/* D1 */
		  SUNXI_FUNCTION(0x3, "jtag"),		/* MS */
		  SUNXI_FUNCTION(0x4, "ir0"),		/* MS */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 0)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc0"),		/* D0 */
		  SUNXI_FUNCTION(0x3, "dgb0"),		/* DI */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 1)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc0"),		/* CLK */
		  SUNXI_FUNCTION(0x3, "uart0"),		/* TX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 2)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc0"),		/* CMD */
		  SUNXI_FUNCTION(0x3, "jtag"),		/* DO */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 3)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc0"),		/* D3 */
		  SUNXI_FUNCTION(0x3, "uart0"),		/* TX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 4)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc0"),		/* D2 */
		  SUNXI_FUNCTION(0x3, "jtag"),		/* CK */
		  SUNXI_FUNCTION(0x4, "pwm1"),		/* PWM1 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 5)),
};

static const struct sunxi_pinctrl_desc suniv_f1c100s_pinctrl_data = {
	.pins = suniv_f1c100s_pins,
	.npins = ARRAY_SIZE(suniv_f1c100s_pins),
	.irq_banks = 3,
};

static int suniv_pinctrl_probe(struct platform_device *pdev)
{
	return sunxi_pinctrl_init(pdev,
				  &suniv_f1c100s_pinctrl_data);
}

static const struct of_device_id suniv_f1c100s_pinctrl_match[] = {
	{ .compatible = "allwinner,suniv-f1c100s-pinctrl", },
	{}
};

static struct platform_driver suniv_f1c100s_pinctrl_driver = {
	.probe	= suniv_pinctrl_probe,
	.driver	= {
		.name		= "suniv-f1c100s-pinctrl",
		.of_match_table	= suniv_f1c100s_pinctrl_match,
	},
};
builtin_platform_driver(suniv_f1c100s_pinctrl_driver);
