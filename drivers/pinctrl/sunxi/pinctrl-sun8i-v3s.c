/*
 * Allwinner V3/V3s SoCs pinctrl driver.
 *
 * Copyright (C) 2016 Icenowy Zheng <icenowy@aosc.xyz>
 *
 * Based on pinctrl-sun8i-h3.c, which is:
 * Copyright (C) 2015 Jens Kuske <jenskuske@gmail.com>
 *
 * Based on pinctrl-sun8i-a23.c, which is:
 * Copyright (C) 2014 Chen-Yu Tsai <wens@csie.org>
 * Copyright (C) 2014 Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-sunxi.h"

#define PINCTRL_SUN8I_V3	BIT(0)
#define PINCTRL_SUN8I_V3S	BIT(1)

static const struct sunxi_desc_pin sun8i_v3s_pins[] = {
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart2"),		/* TX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 0)),	/* PB_EINT0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart2"),		/* RX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 1)),	/* PB_EINT1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart2"),		/* RTS */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 2)),	/* PB_EINT2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart2"),		/* CTS */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 3)),	/* PB_EINT3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "pwm0"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 4)),	/* PB_EINT4 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "pwm1"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 5)),	/* PB_EINT5 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c0"),		/* SCK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 6)),	/* PB_EINT6 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c0"),		/* SDA */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 7)),	/* PB_EINT7 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 8),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c1"),		/* SDA */
		  SUNXI_FUNCTION(0x3, "uart0"),		/* TX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 8)),	/* PB_EINT8 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 9),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c1"),		/* SCK */
		  SUNXI_FUNCTION(0x3, "uart0"),		/* RX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 9)),	/* PB_EINT9 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(B, 10),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "jtag"),		/* MS */
			  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 10)),	/* PB_EINT10 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(B, 11),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "jtag"),		/* CK */
			  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 11)),	/* PB_EINT11 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(B, 12),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "jtag"),		/* DO */
			  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 12)),	/* PB_EINT12 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(B, 13),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "jtag"),		/* DI */
			  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 13)),	/* PB_EINT13 */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc2"),		/* CLK */
		  SUNXI_FUNCTION(0x3, "spi0")),		/* MISO */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc2"),		/* CMD */
		  SUNXI_FUNCTION(0x3, "spi0")),		/* CLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc2"),		/* RST */
		  SUNXI_FUNCTION(0x3, "spi0")),		/* CS */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc2"),		/* D0 */
		  SUNXI_FUNCTION(0x3, "spi0")),		/* MOSI */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(C, 4),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "mmc2")),		/* D1 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(C, 5),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "mmc2")),		/* D2 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(C, 6),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "mmc2")),		/* D3 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(C, 7),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "mmc2")),		/* D4 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(C, 8),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "mmc2")),		/* D5 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(C, 9),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "mmc2")),		/* D6 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(C, 10),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "mmc2")),		/* D7 */
	/* Hole */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 0),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "lcd"),		/* D2 */
			  SUNXI_FUNCTION(0x4, "emac")),		/* RXD3 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 1),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "lcd"),		/* D3 */
			  SUNXI_FUNCTION(0x4, "emac")),		/* RXD2 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 2),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "lcd"),		/* D4 */
			  SUNXI_FUNCTION(0x4, "emac")),		/* RXD1 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 3),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "lcd"),		/* D5 */
			  SUNXI_FUNCTION(0x4, "emac")),		/* RXD0 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 4),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "lcd"),		/* D6 */
			  SUNXI_FUNCTION(0x4, "emac")),		/* RXCK */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 5),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "lcd"),		/* D7 */
			  SUNXI_FUNCTION(0x4, "emac")),		/* RXCTL/RXDV */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 6),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "lcd"),		/* D10 */
			  SUNXI_FUNCTION(0x4, "emac")),		/* RXERR */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 7),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "lcd"),		/* D11 */
			  SUNXI_FUNCTION(0x4, "emac")),		/* TXD3 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 8),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "lcd"),		/* D12 */
			  SUNXI_FUNCTION(0x4, "emac")),		/* TXD2 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 9),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "lcd"),		/* D13 */
			  SUNXI_FUNCTION(0x4, "emac")),		/* TXD1 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 10),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "lcd"),		/* D14 */
			  SUNXI_FUNCTION(0x4, "emac")),		/* TXD0 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 11),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "lcd"),		/* D15 */
			  SUNXI_FUNCTION(0x4, "emac")),		/* CRS */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 12),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "lcd"),		/* D18 */
			  SUNXI_FUNCTION(0x3, "lvds"),		/* VP0 */
			  SUNXI_FUNCTION(0x4, "emac")),		/* TXCK */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 13),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "lcd"),		/* D19 */
			  SUNXI_FUNCTION(0x3, "lvds"),		/* VN0 */
			  SUNXI_FUNCTION(0x4, "emac")),		/* TXCTL/TXEN */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 14),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "lcd"),		/* D20 */
			  SUNXI_FUNCTION(0x3, "lvds"),		/* VP1 */
			  SUNXI_FUNCTION(0x4, "emac")),		/* TXERR */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 15),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "lcd"),		/* D21 */
			  SUNXI_FUNCTION(0x3, "lvds"),		/* VN1 */
			  SUNXI_FUNCTION(0x4, "emac")),		/* CLKIN/COL */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 16),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "lcd"),		/* D22 */
			  SUNXI_FUNCTION(0x3, "lvds"),		/* VP2 */
			  SUNXI_FUNCTION(0x4, "emac")),		/* MDC */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 17),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "lcd"),		/* D23 */
			  SUNXI_FUNCTION(0x3, "lvds"),		/* VN2 */
			  SUNXI_FUNCTION(0x4, "emac")),		/* MDIO */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 18),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "lcd"),		/* CLK */
			  SUNXI_FUNCTION(0x3, "lvds")),		/* VPC */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 19),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "lcd"),		/* DE */
			  SUNXI_FUNCTION(0x3, "lvds")),		/* VNC */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 20),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "lcd"),		/* HSYNC */
			  SUNXI_FUNCTION(0x3, "lvds")),		/* VP3 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(D, 21),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "lcd"),		/* VSYNC */
			  SUNXI_FUNCTION(0x3, "lvds")),		/* VN3 */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* PCLK */
		  SUNXI_FUNCTION(0x3, "lcd")),		/* CLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* MCLK */
		  SUNXI_FUNCTION(0x3, "lcd")),		/* DE */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* HSYNC */
		  SUNXI_FUNCTION(0x3, "lcd")),		/* HSYNC */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* VSYNC */
		  SUNXI_FUNCTION(0x3, "lcd")),		/* VSYNC */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* D0 */
		  SUNXI_FUNCTION(0x3, "lcd")),		/* D2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* D1 */
		  SUNXI_FUNCTION(0x3, "lcd")),		/* D3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* D2 */
		  SUNXI_FUNCTION(0x3, "lcd")),		/* D4 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* D3 */
		  SUNXI_FUNCTION(0x3, "lcd")),		/* D5 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 8),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* D4 */
		  SUNXI_FUNCTION(0x3, "lcd")),		/* D6 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 9),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* D5 */
		  SUNXI_FUNCTION(0x3, "lcd")),		/* D7 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 10),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* D6 */
		  SUNXI_FUNCTION(0x3, "lcd")),		/* D10 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 11),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* D7 */
		  SUNXI_FUNCTION(0x3, "lcd")),		/* D11 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 12),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* D8 */
		  SUNXI_FUNCTION(0x3, "lcd")),		/* D12 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 13),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* D9 */
		  SUNXI_FUNCTION(0x3, "lcd")),		/* D13 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 14),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* D10 */
		  SUNXI_FUNCTION(0x3, "lcd")),		/* D14 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 15),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* D11 */
		  SUNXI_FUNCTION(0x3, "lcd")),		/* D15 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 16),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* D12 */
		  SUNXI_FUNCTION(0x3, "lcd")),		/* D18 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 17),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* D13 */
		  SUNXI_FUNCTION(0x3, "lcd")),		/* D19 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 18),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* D14 */
		  SUNXI_FUNCTION(0x3, "lcd")),		/* D20 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 19),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* D15 */
		  SUNXI_FUNCTION(0x3, "lcd")),		/* D21 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 20),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* FIELD */
		  SUNXI_FUNCTION(0x3, "csi_mipi")),	/* MCLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 21),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* SCK */
		  SUNXI_FUNCTION(0x3, "i2c1"),		/* SCK */
		  SUNXI_FUNCTION(0x4, "uart1")),	/* TX */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 22),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "csi"),		/* SDA */
		  SUNXI_FUNCTION(0x3, "i2c1"),		/* SDA */
		  SUNXI_FUNCTION(0x4, "uart1")),	/* RX */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 23),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "lcd"),		/* D22 */
		  SUNXI_FUNCTION(0x4, "uart1")),	/* RTS */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 24),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "lcd"),		/* D23 */
		  SUNXI_FUNCTION(0x4, "uart1")),	/* CTS */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc0"),		/* D1 */
		  SUNXI_FUNCTION(0x3, "jtag")),		/* MS */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc0"),		/* D0 */
		  SUNXI_FUNCTION(0x3, "jtag")),		/* DI */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc0"),		/* CLK */
		  SUNXI_FUNCTION(0x3, "uart0")),	/* TX */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc0"),		/* CMD */
		  SUNXI_FUNCTION(0x3, "jtag")),		/* DO */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc0"),		/* D3 */
		  SUNXI_FUNCTION(0x3, "uart0")),	/* RX */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc0"),		/* D2 */
		  SUNXI_FUNCTION(0x3, "jtag")),		/* CK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out")),
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc1"),		/* CLK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 0)),	/* PG_EINT0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc1"),		/* CMD */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 1)),	/* PG_EINT1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc1"),		/* D0 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 2)),	/* PG_EINT2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc1"),		/* D1 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 3)),	/* PG_EINT3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc1"),		/* D2 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 4)),	/* PG_EINT4 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc1"),		/* D3 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 5)),	/* PG_EINT5 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(G, 6),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "uart1"),		/* TX */
			  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 6)),	/* PG_EINT6 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(G, 7),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "uart1"),		/* RX */
			  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 7)),	/* PG_EINT7 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(G, 8),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "uart1"),		/* RTS */
			  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 8)),	/* PG_EINT8 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(G, 9),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "uart1"),		/* CTS */
			  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 9)),	/* PG_EINT9 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(G, 10),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "i2s"),		/* SYNC */
			  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 10)),	/* PG_EINT10 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(G, 11),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "i2s"),		/* BCLK */
			  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 11)),	/* PG_EINT11 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(G, 12),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "i2s"),		/* DOUT */
			  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 12)),	/* PG_EINT12 */
	SUNXI_PIN_VARIANT(SUNXI_PINCTRL_PIN(G, 13),
			  PINCTRL_SUN8I_V3,
			  SUNXI_FUNCTION(0x0, "gpio_in"),
			  SUNXI_FUNCTION(0x1, "gpio_out"),
			  SUNXI_FUNCTION(0x2, "i2s"),		/* DIN */
			  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 13)),	/* PG_EINT13 */
};

static const unsigned int sun8i_v3s_pinctrl_irq_bank_map[] = { 1, 2 };

static const struct sunxi_pinctrl_desc sun8i_v3s_pinctrl_data = {
	.pins = sun8i_v3s_pins,
	.npins = ARRAY_SIZE(sun8i_v3s_pins),
	.irq_banks = 2,
	.irq_bank_map = sun8i_v3s_pinctrl_irq_bank_map,
	.irq_read_needs_mux = true
};

static int sun8i_v3s_pinctrl_probe(struct platform_device *pdev)
{
	unsigned long variant = (unsigned long)of_device_get_match_data(&pdev->dev);

	return sunxi_pinctrl_init_with_flags(pdev, &sun8i_v3s_pinctrl_data,
					     variant);
}

static const struct of_device_id sun8i_v3s_pinctrl_match[] = {
	{
		.compatible = "allwinner,sun8i-v3-pinctrl",
		.data = (void *)PINCTRL_SUN8I_V3
	},
	{
		.compatible = "allwinner,sun8i-v3s-pinctrl",
		.data = (void *)PINCTRL_SUN8I_V3S
	},
	{ },
};

static struct platform_driver sun8i_v3s_pinctrl_driver = {
	.probe	= sun8i_v3s_pinctrl_probe,
	.driver	= {
		.name		= "sun8i-v3s-pinctrl",
		.of_match_table	= sun8i_v3s_pinctrl_match,
	},
};
builtin_platform_driver(sun8i_v3s_pinctrl_driver);
