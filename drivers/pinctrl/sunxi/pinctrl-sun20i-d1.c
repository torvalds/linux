// SPDX-License-Identifier: GPL-2.0
/*
 * Allwinner D1 SoC pinctrl driver.
 *
 * Copyright (c) 2020 wuyan@allwinnertech.com
 * Copyright (c) 2021-2022 Samuel Holland <samuel@sholland.org>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-sunxi.h"

static const struct sunxi_desc_pin d1_pins[] = {
	/* PB */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 0),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "pwm3"),
		SUNXI_FUNCTION(0x3, "ir"),		/* TX */
		SUNXI_FUNCTION(0x4, "i2c2"),		/* SCK */
		SUNXI_FUNCTION(0x5, "spi1"),		/* WP */
		SUNXI_FUNCTION(0x6, "uart0"),		/* TX */
		SUNXI_FUNCTION(0x7, "uart2"),		/* TX */
		SUNXI_FUNCTION(0x8, "spdif"),		/* OUT */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 0, 0)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 1),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "pwm4"),
		SUNXI_FUNCTION(0x3, "i2s2_dout"),	/* DOUT3 */
		SUNXI_FUNCTION(0x4, "i2c2"),		/* SDA */
		SUNXI_FUNCTION(0x5, "i2s2_din"),	/* DIN3 */
		SUNXI_FUNCTION(0x6, "uart0"),		/* RX */
		SUNXI_FUNCTION(0x7, "uart2"),		/* RX */
		SUNXI_FUNCTION(0x8, "ir"),		/* RX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 0, 1)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 2),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* D0 */
		SUNXI_FUNCTION(0x3, "i2s2_dout"),	/* DOUT2 */
		SUNXI_FUNCTION(0x4, "i2c0"),		/* SDA */
		SUNXI_FUNCTION(0x5, "i2s2_din"),	/* DIN2 */
		SUNXI_FUNCTION(0x6, "lcd0"),		/* D18 */
		SUNXI_FUNCTION(0x7, "uart4"),		/* TX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 0, 2)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 3),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* D1 */
		SUNXI_FUNCTION(0x3, "i2s2_dout"),	/* DOUT1 */
		SUNXI_FUNCTION(0x4, "i2c0"),		/* SCK */
		SUNXI_FUNCTION(0x5, "i2s2_din"),	/* DIN0 */
		SUNXI_FUNCTION(0x6, "lcd0"),		/* D19 */
		SUNXI_FUNCTION(0x7, "uart4"),		/* RX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 0, 3)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 4),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* D8 */
		SUNXI_FUNCTION(0x3, "i2s2_dout"),	/* DOUT0 */
		SUNXI_FUNCTION(0x4, "i2c1"),		/* SCK */
		SUNXI_FUNCTION(0x5, "i2s2_din"),	/* DIN1 */
		SUNXI_FUNCTION(0x6, "lcd0"),		/* D20 */
		SUNXI_FUNCTION(0x7, "uart5"),		/* TX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 0, 4)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 5),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* D9 */
		SUNXI_FUNCTION(0x3, "i2s2"),		/* BCLK */
		SUNXI_FUNCTION(0x4, "i2c1"),		/* SDA */
		SUNXI_FUNCTION(0x5, "pwm0"),
		SUNXI_FUNCTION(0x6, "lcd0"),		/* D21 */
		SUNXI_FUNCTION(0x7, "uart5"),		/* RX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 0, 5)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 6),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* D16 */
		SUNXI_FUNCTION(0x3, "i2s2"),		/* LRCK */
		SUNXI_FUNCTION(0x4, "i2c3"),		/* SCK */
		SUNXI_FUNCTION(0x5, "pwm1"),
		SUNXI_FUNCTION(0x6, "lcd0"),		/* D22 */
		SUNXI_FUNCTION(0x7, "uart3"),		/* TX */
		SUNXI_FUNCTION(0x8, "bist0"),		/* BIST_RESULT0 */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 0, 6)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 7),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* D17 */
		SUNXI_FUNCTION(0x3, "i2s2"),		/* MCLK */
		SUNXI_FUNCTION(0x4, "i2c3"),		/* SDA */
		SUNXI_FUNCTION(0x5, "ir"),		/* RX */
		SUNXI_FUNCTION(0x6, "lcd0"),		/* D23 */
		SUNXI_FUNCTION(0x7, "uart3"),		/* RX */
		SUNXI_FUNCTION(0x8, "bist1"),		/* BIST_RESULT1 */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 0, 7)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 8),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "dmic"),		/* DATA3 */
		SUNXI_FUNCTION(0x3, "pwm5"),
		SUNXI_FUNCTION(0x4, "i2c2"),		/* SCK */
		SUNXI_FUNCTION(0x5, "spi1"),		/* HOLD */
		SUNXI_FUNCTION(0x6, "uart0"),		/* TX */
		SUNXI_FUNCTION(0x7, "uart1"),		/* TX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 0, 8)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 9),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "dmic"),		/* DATA2 */
		SUNXI_FUNCTION(0x3, "pwm6"),
		SUNXI_FUNCTION(0x4, "i2c2"),		/* SDA */
		SUNXI_FUNCTION(0x5, "spi1"),		/* MISO */
		SUNXI_FUNCTION(0x6, "uart0"),		/* RX */
		SUNXI_FUNCTION(0x7, "uart1"),		/* RX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 0, 9)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 10),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "dmic"),		/* DATA1 */
		SUNXI_FUNCTION(0x3, "pwm7"),
		SUNXI_FUNCTION(0x4, "i2c0"),		/* SCK */
		SUNXI_FUNCTION(0x5, "spi1"),		/* MOSI */
		SUNXI_FUNCTION(0x6, "clk"),		/* FANOUT0 */
		SUNXI_FUNCTION(0x7, "uart1"),		/* RTS */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 0, 10)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 11),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "dmic"),		/* DATA0 */
		SUNXI_FUNCTION(0x3, "pwm2"),
		SUNXI_FUNCTION(0x4, "i2c0"),		/* SDA */
		SUNXI_FUNCTION(0x5, "spi1"),		/* CLK */
		SUNXI_FUNCTION(0x6, "clk"),		/* FANOUT1 */
		SUNXI_FUNCTION(0x7, "uart1"),		/* CTS */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 0, 11)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 12),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "dmic"),		/* CLK */
		SUNXI_FUNCTION(0x3, "pwm0"),
		SUNXI_FUNCTION(0x4, "spdif"),		/* IN */
		SUNXI_FUNCTION(0x5, "spi1"),		/* CS0 */
		SUNXI_FUNCTION(0x6, "clk"),		/* FANOUT2 */
		SUNXI_FUNCTION(0x7, "ir"),		/* RX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 0, 12)),
	/* PC */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 0),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "uart2"),		/* TX */
		SUNXI_FUNCTION(0x3, "i2c2"),		/* SCK */
		SUNXI_FUNCTION(0x4, "ledc"),
		SUNXI_FUNCTION_IRQ_BANK(0xe, 1, 0)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 1),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "uart2"),		/* RX */
		SUNXI_FUNCTION(0x3, "i2c2"),		/* SDA */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 1, 1)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 2),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "spi0"),		/* CLK */
		SUNXI_FUNCTION(0x3, "mmc2"),		/* CLK */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 1, 2)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 3),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "spi0"),		/* CS0 */
		SUNXI_FUNCTION(0x3, "mmc2"),		/* CMD */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 1, 3)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 4),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "spi0"),		/* MOSI */
		SUNXI_FUNCTION(0x3, "mmc2"),		/* D2 */
		SUNXI_FUNCTION(0x4, "boot"),		/* SEL0 */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 1, 4)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 5),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "spi0"),		/* MISO */
		SUNXI_FUNCTION(0x3, "mmc2"),		/* D1 */
		SUNXI_FUNCTION(0x4, "boot"),		/* SEL1 */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 1, 5)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 6),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "spi0"),		/* WP */
		SUNXI_FUNCTION(0x3, "mmc2"),		/* D0 */
		SUNXI_FUNCTION(0x4, "uart3"),		/* TX */
		SUNXI_FUNCTION(0x5, "i2c3"),		/* SCK */
		SUNXI_FUNCTION(0x6, "pll"),		/* DBG-CLK */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 1, 6)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 7),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "spi0"),		/* HOLD */
		SUNXI_FUNCTION(0x3, "mmc2"),		/* D3 */
		SUNXI_FUNCTION(0x4, "uart3"),		/* RX */
		SUNXI_FUNCTION(0x5, "i2c3"),		/* SDA */
		SUNXI_FUNCTION(0x6, "tcon"),		/* TRIG0 */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 1, 7)),
	/* PD */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 0),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* D2 */
		SUNXI_FUNCTION(0x3, "lvds0"),		/* V0P */
		SUNXI_FUNCTION(0x4, "dsi"),		/* D0P */
		SUNXI_FUNCTION(0x5, "i2c0"),		/* SCK */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 2, 0)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 1),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* D3 */
		SUNXI_FUNCTION(0x3, "lvds0"),		/* V0N */
		SUNXI_FUNCTION(0x4, "dsi"),		/* D0N */
		SUNXI_FUNCTION(0x5, "uart2"),		/* TX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 2, 1)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 2),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* D4 */
		SUNXI_FUNCTION(0x3, "lvds0"),		/* V1P */
		SUNXI_FUNCTION(0x4, "dsi"),		/* D1P */
		SUNXI_FUNCTION(0x5, "uart2"),		/* RX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 2, 2)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 3),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* D5 */
		SUNXI_FUNCTION(0x3, "lvds0"),		/* V1N */
		SUNXI_FUNCTION(0x4, "dsi"),		/* D1N */
		SUNXI_FUNCTION(0x5, "uart2"),		/* RTS */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 2, 3)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 4),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* D6 */
		SUNXI_FUNCTION(0x3, "lvds0"),		/* V2P */
		SUNXI_FUNCTION(0x4, "dsi"),		/* CKP */
		SUNXI_FUNCTION(0x5, "uart2"),		/* CTS */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 2, 4)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 5),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* D7 */
		SUNXI_FUNCTION(0x3, "lvds0"),		/* V2N */
		SUNXI_FUNCTION(0x4, "dsi"),		/* CKN */
		SUNXI_FUNCTION(0x5, "uart5"),		/* TX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 2, 5)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 6),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* D10 */
		SUNXI_FUNCTION(0x3, "lvds0"),		/* CKP */
		SUNXI_FUNCTION(0x4, "dsi"),		/* D2P */
		SUNXI_FUNCTION(0x5, "uart5"),		/* RX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 2, 6)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 7),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* D11 */
		SUNXI_FUNCTION(0x3, "lvds0"),		/* CKN */
		SUNXI_FUNCTION(0x4, "dsi"),		/* D2N */
		SUNXI_FUNCTION(0x5, "uart4"),		/* TX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 2, 7)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 8),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* D12 */
		SUNXI_FUNCTION(0x3, "lvds0"),		/* V3P */
		SUNXI_FUNCTION(0x4, "dsi"),		/* D3P */
		SUNXI_FUNCTION(0x5, "uart4"),		/* RX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 2, 8)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 9),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* D13 */
		SUNXI_FUNCTION(0x3, "lvds0"),		/* V3N */
		SUNXI_FUNCTION(0x4, "dsi"),		/* D3N */
		SUNXI_FUNCTION(0x5, "pwm6"),
		SUNXI_FUNCTION_IRQ_BANK(0xe, 2, 9)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 10),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* D14 */
		SUNXI_FUNCTION(0x3, "lvds1"),		/* V0P */
		SUNXI_FUNCTION(0x4, "spi1"),		/* CS0 */
		SUNXI_FUNCTION(0x5, "uart3"),		/* TX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 2, 10)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 11),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* D15 */
		SUNXI_FUNCTION(0x3, "lvds1"),		/* V0N */
		SUNXI_FUNCTION(0x4, "spi1"),		/* CLK */
		SUNXI_FUNCTION(0x5, "uart3"),		/* RX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 2, 11)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 12),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* D18 */
		SUNXI_FUNCTION(0x3, "lvds1"),		/* V1P */
		SUNXI_FUNCTION(0x4, "spi1"),		/* MOSI */
		SUNXI_FUNCTION(0x5, "i2c0"),		/* SDA */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 2, 12)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 13),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* D19 */
		SUNXI_FUNCTION(0x3, "lvds1"),		/* V1N */
		SUNXI_FUNCTION(0x4, "spi1"),		/* MISO */
		SUNXI_FUNCTION(0x5, "uart3"),		/* RTS */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 2, 13)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 14),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* D20 */
		SUNXI_FUNCTION(0x3, "lvds1"),		/* V2P */
		SUNXI_FUNCTION(0x4, "spi1"),		/* HOLD */
		SUNXI_FUNCTION(0x5, "uart3"),		/* CTS */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 2, 14)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 15),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* D21 */
		SUNXI_FUNCTION(0x3, "lvds1"),		/* V2N */
		SUNXI_FUNCTION(0x4, "spi1"),		/* WP */
		SUNXI_FUNCTION(0x5, "ir"),		/* RX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 2, 15)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 16),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* D22 */
		SUNXI_FUNCTION(0x3, "lvds1"),		/* CKP */
		SUNXI_FUNCTION(0x4, "dmic"),		/* DATA3 */
		SUNXI_FUNCTION(0x5, "pwm0"),
		SUNXI_FUNCTION_IRQ_BANK(0xe, 2, 16)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 17),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* D23 */
		SUNXI_FUNCTION(0x3, "lvds1"),		/* CKN */
		SUNXI_FUNCTION(0x4, "dmic"),		/* DATA2 */
		SUNXI_FUNCTION(0x5, "pwm1"),
		SUNXI_FUNCTION_IRQ_BANK(0xe, 2, 17)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 18),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* CLK */
		SUNXI_FUNCTION(0x3, "lvds1"),		/* V3P */
		SUNXI_FUNCTION(0x4, "dmic"),		/* DATA1 */
		SUNXI_FUNCTION(0x5, "pwm2"),
		SUNXI_FUNCTION_IRQ_BANK(0xe, 2, 18)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 19),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* DE */
		SUNXI_FUNCTION(0x3, "lvds1"),		/* V3N */
		SUNXI_FUNCTION(0x4, "dmic"),		/* DATA0 */
		SUNXI_FUNCTION(0x5, "pwm3"),
		SUNXI_FUNCTION_IRQ_BANK(0xe, 2, 19)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 20),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* HSYNC */
		SUNXI_FUNCTION(0x3, "i2c2"),		/* SCK */
		SUNXI_FUNCTION(0x4, "dmic"),		/* CLK */
		SUNXI_FUNCTION(0x5, "pwm4"),
		SUNXI_FUNCTION_IRQ_BANK(0xe, 2, 20)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 21),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "lcd0"),		/* VSYNC */
		SUNXI_FUNCTION(0x3, "i2c2"),		/* SDA */
		SUNXI_FUNCTION(0x4, "uart1"),		/* TX */
		SUNXI_FUNCTION(0x5, "pwm5"),
		SUNXI_FUNCTION_IRQ_BANK(0xe, 2, 21)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(D, 22),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "spdif"),		/* OUT */
		SUNXI_FUNCTION(0x3, "ir"),		/* RX */
		SUNXI_FUNCTION(0x4, "uart1"),		/* RX */
		SUNXI_FUNCTION(0x5, "pwm7"),
		SUNXI_FUNCTION_IRQ_BANK(0xe, 2, 22)),
	/* PE */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 0),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "ncsi0"),		/* HSYNC */
		SUNXI_FUNCTION(0x3, "uart2"),		/* RTS */
		SUNXI_FUNCTION(0x4, "i2c1"),		/* SCK */
		SUNXI_FUNCTION(0x5, "lcd0"),		/* HSYNC */
		SUNXI_FUNCTION(0x8, "emac"),		/* RXCTL/CRS_DV */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 3, 0)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 1),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "ncsi0"),		/* VSYNC */
		SUNXI_FUNCTION(0x3, "uart2"),		/* CTS */
		SUNXI_FUNCTION(0x4, "i2c1"),		/* SDA */
		SUNXI_FUNCTION(0x5, "lcd0"),		/* VSYNC */
		SUNXI_FUNCTION(0x8, "emac"),		/* RXD0 */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 3, 1)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 2),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "ncsi0"),		/* PCLK */
		SUNXI_FUNCTION(0x3, "uart2"),		/* TX */
		SUNXI_FUNCTION(0x4, "i2c0"),		/* SCK */
		SUNXI_FUNCTION(0x5, "clk"),		/* FANOUT0 */
		SUNXI_FUNCTION(0x6, "uart0"),		/* TX */
		SUNXI_FUNCTION(0x8, "emac"),		/* RXD1 */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 3, 2)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 3),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "ncsi0"),		/* MCLK */
		SUNXI_FUNCTION(0x3, "uart2"),		/* RX */
		SUNXI_FUNCTION(0x4, "i2c0"),		/* SDA */
		SUNXI_FUNCTION(0x5, "clk"),		/* FANOUT1 */
		SUNXI_FUNCTION(0x6, "uart0"),		/* RX */
		SUNXI_FUNCTION(0x8, "emac"),		/* TXCK */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 3, 3)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 4),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "ncsi0"),		/* D0 */
		SUNXI_FUNCTION(0x3, "uart4"),		/* TX */
		SUNXI_FUNCTION(0x4, "i2c2"),		/* SCK */
		SUNXI_FUNCTION(0x5, "clk"),		/* FANOUT2 */
		SUNXI_FUNCTION(0x6, "d_jtag"),		/* MS */
		SUNXI_FUNCTION(0x7, "r_jtag"),		/* MS */
		SUNXI_FUNCTION(0x8, "emac"),		/* TXD0 */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 3, 4)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 5),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "ncsi0"),		/* D1 */
		SUNXI_FUNCTION(0x3, "uart4"),		/* RX */
		SUNXI_FUNCTION(0x4, "i2c2"),		/* SDA */
		SUNXI_FUNCTION(0x5, "ledc"),
		SUNXI_FUNCTION(0x6, "d_jtag"),		/* DI */
		SUNXI_FUNCTION(0x7, "r_jtag"),		/* DI */
		SUNXI_FUNCTION(0x8, "emac"),		/* TXD1 */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 3, 5)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 6),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "ncsi0"),		/* D2 */
		SUNXI_FUNCTION(0x3, "uart5"),		/* TX */
		SUNXI_FUNCTION(0x4, "i2c3"),		/* SCK */
		SUNXI_FUNCTION(0x5, "spdif"),		/* IN */
		SUNXI_FUNCTION(0x6, "d_jtag"),		/* DO */
		SUNXI_FUNCTION(0x7, "r_jtag"),		/* DO */
		SUNXI_FUNCTION(0x8, "emac"),		/* TXCTL/TXEN */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 3, 6)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 7),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "ncsi0"),		/* D3 */
		SUNXI_FUNCTION(0x3, "uart5"),		/* RX */
		SUNXI_FUNCTION(0x4, "i2c3"),		/* SDA */
		SUNXI_FUNCTION(0x5, "spdif"),		/* OUT */
		SUNXI_FUNCTION(0x6, "d_jtag"),		/* CK */
		SUNXI_FUNCTION(0x7, "r_jtag"),		/* CK */
		SUNXI_FUNCTION(0x8, "emac"),		/* CK */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 3, 7)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 8),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "ncsi0"),		/* D4 */
		SUNXI_FUNCTION(0x3, "uart1"),		/* RTS */
		SUNXI_FUNCTION(0x4, "pwm2"),
		SUNXI_FUNCTION(0x5, "uart3"),		/* TX */
		SUNXI_FUNCTION(0x6, "jtag"),		/* MS */
		SUNXI_FUNCTION(0x8, "emac"),		/* MDC */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 3, 8)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 9),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "ncsi0"),		/* D5 */
		SUNXI_FUNCTION(0x3, "uart1"),		/* CTS */
		SUNXI_FUNCTION(0x4, "pwm3"),
		SUNXI_FUNCTION(0x5, "uart3"),		/* RX */
		SUNXI_FUNCTION(0x6, "jtag"),		/* DI */
		SUNXI_FUNCTION(0x8, "emac"),		/* MDIO */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 3, 9)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 10),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "ncsi0"),		/* D6 */
		SUNXI_FUNCTION(0x3, "uart1"),		/* TX */
		SUNXI_FUNCTION(0x4, "pwm4"),
		SUNXI_FUNCTION(0x5, "ir"),		/* RX */
		SUNXI_FUNCTION(0x6, "jtag"),		/* DO */
		SUNXI_FUNCTION(0x8, "emac"),		/* EPHY-25M */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 3, 10)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 11),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "ncsi0"),		/* D7 */
		SUNXI_FUNCTION(0x3, "uart1"),		/* RX */
		SUNXI_FUNCTION(0x4, "i2s0_dout"),	/* DOUT3 */
		SUNXI_FUNCTION(0x5, "i2s0_din"),	/* DIN3 */
		SUNXI_FUNCTION(0x6, "jtag"),		/* CK */
		SUNXI_FUNCTION(0x8, "emac"),		/* TXD2 */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 3, 11)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 12),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "i2c2"),		/* SCK */
		SUNXI_FUNCTION(0x3, "ncsi0"),		/* FIELD */
		SUNXI_FUNCTION(0x4, "i2s0_dout"),	/* DOUT2 */
		SUNXI_FUNCTION(0x5, "i2s0_din"),	/* DIN2 */
		SUNXI_FUNCTION(0x8, "emac"),		/* TXD3 */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 3, 12)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 13),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "i2c2"),		/* SDA */
		SUNXI_FUNCTION(0x3, "pwm5"),
		SUNXI_FUNCTION(0x4, "i2s0_dout"),	/* DOUT0 */
		SUNXI_FUNCTION(0x5, "i2s0_din"),	/* DIN1 */
		SUNXI_FUNCTION(0x6, "dmic"),		/* DATA3 */
		SUNXI_FUNCTION(0x8, "emac"),		/* RXD2 */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 3, 13)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 14),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "i2c1"),		/* SCK */
		SUNXI_FUNCTION(0x3, "d_jtag"),		/* MS */
		SUNXI_FUNCTION(0x4, "i2s0_dout"),	/* DOUT1 */
		SUNXI_FUNCTION(0x5, "i2s0_din"),	/* DIN0 */
		SUNXI_FUNCTION(0x6, "dmic"),		/* DATA2 */
		SUNXI_FUNCTION(0x8, "emac"),		/* RXD3 */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 3, 14)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 15),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "i2c1"),		/* SDA */
		SUNXI_FUNCTION(0x3, "d_jtag"),		/* DI */
		SUNXI_FUNCTION(0x4, "pwm6"),
		SUNXI_FUNCTION(0x5, "i2s0"),		/* LRCK */
		SUNXI_FUNCTION(0x6, "dmic"),		/* DATA1 */
		SUNXI_FUNCTION(0x8, "emac"),		/* RXCK */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 3, 15)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 16),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "i2c3"),		/* SCK */
		SUNXI_FUNCTION(0x3, "d_jtag"),		/* DO */
		SUNXI_FUNCTION(0x4, "pwm7"),
		SUNXI_FUNCTION(0x5, "i2s0"),		/* BCLK */
		SUNXI_FUNCTION(0x6, "dmic"),		/* DATA0 */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 3, 16)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(E, 17),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "i2c3"),		/* SDA */
		SUNXI_FUNCTION(0x3, "d_jtag"),		/* CK */
		SUNXI_FUNCTION(0x4, "ir"),		/* TX */
		SUNXI_FUNCTION(0x5, "i2s0"),		/* MCLK */
		SUNXI_FUNCTION(0x6, "dmic"),		/* CLK */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 3, 17)),
	/* PF */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 0),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "mmc0"),		/* D1 */
		SUNXI_FUNCTION(0x3, "jtag"),		/* MS */
		SUNXI_FUNCTION(0x4, "r_jtag"),		/* MS */
		SUNXI_FUNCTION(0x5, "i2s2_dout"),	/* DOUT1 */
		SUNXI_FUNCTION(0x6, "i2s2_din"),	/* DIN0 */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 4, 0)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 1),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "mmc0"),		/* D0 */
		SUNXI_FUNCTION(0x3, "jtag"),		/* DI */
		SUNXI_FUNCTION(0x4, "r_jtag"),		/* DI */
		SUNXI_FUNCTION(0x5, "i2s2_dout"),	/* DOUT0 */
		SUNXI_FUNCTION(0x6, "i2s2_din"),	/* DIN1 */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 4, 1)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 2),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "mmc0"),		/* CLK */
		SUNXI_FUNCTION(0x3, "uart0"),		/* TX */
		SUNXI_FUNCTION(0x4, "i2c0"),		/* SCK */
		SUNXI_FUNCTION(0x5, "ledc"),
		SUNXI_FUNCTION(0x6, "spdif"),		/* IN */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 4, 2)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 3),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "mmc0"),		/* CMD */
		SUNXI_FUNCTION(0x3, "jtag"),		/* DO */
		SUNXI_FUNCTION(0x4, "r_jtag"),		/* DO */
		SUNXI_FUNCTION(0x5, "i2s2"),		/* BCLK */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 4, 3)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 4),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "mmc0"),		/* D3 */
		SUNXI_FUNCTION(0x3, "uart0"),		/* RX */
		SUNXI_FUNCTION(0x4, "i2c0"),		/* SDA */
		SUNXI_FUNCTION(0x5, "pwm6"),
		SUNXI_FUNCTION(0x6, "ir"),		/* TX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 4, 4)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 5),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "mmc0"),		/* D2 */
		SUNXI_FUNCTION(0x3, "jtag"),		/* CK */
		SUNXI_FUNCTION(0x4, "r_jtag"),		/* CK */
		SUNXI_FUNCTION(0x5, "i2s2"),		/* LRCK */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 4, 5)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 6),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x3, "spdif"),		/* OUT */
		SUNXI_FUNCTION(0x4, "ir"),		/* RX */
		SUNXI_FUNCTION(0x5, "i2s2"),		/* MCLK */
		SUNXI_FUNCTION(0x6, "pwm5"),
		SUNXI_FUNCTION_IRQ_BANK(0xe, 4, 6)),
	/* PG */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 0),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "mmc1"),		/* CLK */
		SUNXI_FUNCTION(0x3, "uart3"),		/* TX */
		SUNXI_FUNCTION(0x4, "emac"),		/* RXCTRL/CRS_DV */
		SUNXI_FUNCTION(0x5, "pwm7"),
		SUNXI_FUNCTION_IRQ_BANK(0xe, 5, 0)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 1),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "mmc1"),		/* CMD */
		SUNXI_FUNCTION(0x3, "uart3"),		/* RX */
		SUNXI_FUNCTION(0x4, "emac"),		/* RXD0 */
		SUNXI_FUNCTION(0x5, "pwm6"),
		SUNXI_FUNCTION_IRQ_BANK(0xe, 5, 1)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 2),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "mmc1"),		/* D0 */
		SUNXI_FUNCTION(0x3, "uart3"),		/* RTS */
		SUNXI_FUNCTION(0x4, "emac"),		/* RXD1 */
		SUNXI_FUNCTION(0x5, "uart4"),		/* TX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 5, 2)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 3),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "mmc1"),		/* D1 */
		SUNXI_FUNCTION(0x3, "uart3"),		/* CTS */
		SUNXI_FUNCTION(0x4, "emac"),		/* TXCK */
		SUNXI_FUNCTION(0x5, "uart4"),		/* RX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 5, 3)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 4),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "mmc1"),		/* D2 */
		SUNXI_FUNCTION(0x3, "uart5"),		/* TX */
		SUNXI_FUNCTION(0x4, "emac"),		/* TXD0 */
		SUNXI_FUNCTION(0x5, "pwm5"),
		SUNXI_FUNCTION_IRQ_BANK(0xe, 5, 4)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 5),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "mmc1"),		/* D3 */
		SUNXI_FUNCTION(0x3, "uart5"),		/* RX */
		SUNXI_FUNCTION(0x4, "emac"),		/* TXD1 */
		SUNXI_FUNCTION(0x5, "pwm4"),
		SUNXI_FUNCTION_IRQ_BANK(0xe, 5, 5)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 6),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "uart1"),		/* TX */
		SUNXI_FUNCTION(0x3, "i2c2"),		/* SCK */
		SUNXI_FUNCTION(0x4, "emac"),		/* TXD2 */
		SUNXI_FUNCTION(0x5, "pwm1"),
		SUNXI_FUNCTION_IRQ_BANK(0xe, 5, 6)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 7),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "uart1"),		/* RX */
		SUNXI_FUNCTION(0x3, "i2c2"),		/* SDA */
		SUNXI_FUNCTION(0x4, "emac"),		/* TXD3 */
		SUNXI_FUNCTION(0x5, "spdif"),		/* IN */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 5, 7)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 8),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "uart1"),		/* RTS */
		SUNXI_FUNCTION(0x3, "i2c1"),		/* SCK */
		SUNXI_FUNCTION(0x4, "emac"),		/* RXD2 */
		SUNXI_FUNCTION(0x5, "uart3"),		/* TX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 5, 8)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 9),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "uart1"),		/* CTS */
		SUNXI_FUNCTION(0x3, "i2c1"),		/* SDA */
		SUNXI_FUNCTION(0x4, "emac"),		/* RXD3 */
		SUNXI_FUNCTION(0x5, "uart3"),		/* RX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 5, 9)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 10),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "pwm3"),
		SUNXI_FUNCTION(0x3, "i2c3"),		/* SCK */
		SUNXI_FUNCTION(0x4, "emac"),		/* RXCK */
		SUNXI_FUNCTION(0x5, "clk"),		/* FANOUT0 */
		SUNXI_FUNCTION(0x6, "ir"),		/* RX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 5, 10)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 11),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "i2s1"),		/* MCLK */
		SUNXI_FUNCTION(0x3, "i2c3"),		/* SDA */
		SUNXI_FUNCTION(0x4, "emac"),		/* EPHY-25M */
		SUNXI_FUNCTION(0x5, "clk"),		/* FANOUT1 */
		SUNXI_FUNCTION(0x6, "tcon"),		/* TRIG0 */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 5, 11)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 12),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "i2s1"),		/* LRCK */
		SUNXI_FUNCTION(0x3, "i2c0"),		/* SCK */
		SUNXI_FUNCTION(0x4, "emac"),		/* TXCTL/TXEN */
		SUNXI_FUNCTION(0x5, "clk"),		/* FANOUT2 */
		SUNXI_FUNCTION(0x6, "pwm0"),
		SUNXI_FUNCTION(0x7, "uart1"),		/* TX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 5, 12)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 13),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "i2s1"),		/* BCLK */
		SUNXI_FUNCTION(0x3, "i2c0"),		/* SDA */
		SUNXI_FUNCTION(0x4, "emac"),		/* CLKIN/RXER */
		SUNXI_FUNCTION(0x5, "pwm2"),
		SUNXI_FUNCTION(0x6, "ledc"),
		SUNXI_FUNCTION(0x7, "uart1"),		/* RX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 5, 13)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 14),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "i2s1_din"),	/* DIN0 */
		SUNXI_FUNCTION(0x3, "i2c2"),		/* SCK */
		SUNXI_FUNCTION(0x4, "emac"),		/* MDC */
		SUNXI_FUNCTION(0x5, "i2s1_dout"),	/* DOUT1 */
		SUNXI_FUNCTION(0x6, "spi0"),		/* WP */
		SUNXI_FUNCTION(0x7, "uart1"),		/* RTS */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 5, 14)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 15),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "i2s1_dout"),	/* DOUT0 */
		SUNXI_FUNCTION(0x3, "i2c2"),		/* SDA */
		SUNXI_FUNCTION(0x4, "emac"),		/* MDIO */
		SUNXI_FUNCTION(0x5, "i2s1_din"),	/* DIN1 */
		SUNXI_FUNCTION(0x6, "spi0"),		/* HOLD */
		SUNXI_FUNCTION(0x7, "uart1"),		/* CTS */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 5, 15)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 16),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "ir"),		/* RX */
		SUNXI_FUNCTION(0x3, "tcon"),		/* TRIG0 */
		SUNXI_FUNCTION(0x4, "pwm5"),
		SUNXI_FUNCTION(0x5, "clk"),		/* FANOUT2 */
		SUNXI_FUNCTION(0x6, "spdif"),		/* IN */
		SUNXI_FUNCTION(0x7, "ledc"),
		SUNXI_FUNCTION_IRQ_BANK(0xe, 5, 16)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 17),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "uart2"),		/* TX */
		SUNXI_FUNCTION(0x3, "i2c3"),		/* SCK */
		SUNXI_FUNCTION(0x4, "pwm7"),
		SUNXI_FUNCTION(0x5, "clk"),		/* FANOUT0 */
		SUNXI_FUNCTION(0x6, "ir"),		/* TX */
		SUNXI_FUNCTION(0x7, "uart0"),		/* TX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 5, 17)),
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 18),
		SUNXI_FUNCTION(0x0, "gpio_in"),
		SUNXI_FUNCTION(0x1, "gpio_out"),
		SUNXI_FUNCTION(0x2, "uart2"),		/* RX */
		SUNXI_FUNCTION(0x3, "i2c3"),		/* SDA */
		SUNXI_FUNCTION(0x4, "pwm6"),
		SUNXI_FUNCTION(0x5, "clk"),		/* FANOUT1 */
		SUNXI_FUNCTION(0x6, "spdif"),		/* OUT */
		SUNXI_FUNCTION(0x7, "uart0"),		/* RX */
		SUNXI_FUNCTION_IRQ_BANK(0xe, 5, 18)),
};

static const unsigned int d1_irq_bank_map[] = { 1, 2, 3, 4, 5, 6 };

static const struct sunxi_pinctrl_desc d1_pinctrl_data = {
	.pins			= d1_pins,
	.npins			= ARRAY_SIZE(d1_pins),
	.irq_banks		= ARRAY_SIZE(d1_irq_bank_map),
	.irq_bank_map		= d1_irq_bank_map,
	.io_bias_cfg_variant	= BIAS_VOLTAGE_PIO_POW_MODE_CTL,
};

static int d1_pinctrl_probe(struct platform_device *pdev)
{
	unsigned long variant = (unsigned long)of_device_get_match_data(&pdev->dev);

	return sunxi_pinctrl_init_with_variant(pdev, &d1_pinctrl_data, variant);
}

static const struct of_device_id d1_pinctrl_match[] = {
	{
		.compatible = "allwinner,sun20i-d1-pinctrl",
		.data = (void *)PINCTRL_SUN20I_D1
	},
	{}
};

static struct platform_driver d1_pinctrl_driver = {
	.probe	= d1_pinctrl_probe,
	.driver	= {
		.name		= "sun20i-d1-pinctrl",
		.of_match_table	= d1_pinctrl_match,
	},
};
builtin_platform_driver(d1_pinctrl_driver);
