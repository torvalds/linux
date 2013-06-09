/*
 * Allwinner A1X SoCs pinctrl driver.
 *
 * Copyright (C) 2012 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __PINCTRL_SUNXI_PINS_H
#define __PINCTRL_SUNXI_PINS_H

#include "pinctrl-sunxi.h"

static const struct sunxi_desc_pin sun4i_a10_pins[] = {
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PA0,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ERXD3 */
		  SUNXI_FUNCTION(0x3, "spi1"),		/* CS0 */
		  SUNXI_FUNCTION(0x4, "uart2")),	/* RTS */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PA1,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ERXD2 */
		  SUNXI_FUNCTION(0x3, "spi1"),		/* CLK */
		  SUNXI_FUNCTION(0x4, "uart2")),	/* CTS */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PA2,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ERXD1 */
		  SUNXI_FUNCTION(0x3, "spi1"),		/* MOSI */
		  SUNXI_FUNCTION(0x4, "uart2")),	/* TX */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PA3,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ERXD0 */
		  SUNXI_FUNCTION(0x3, "spi1"),		/* MISO */
		  SUNXI_FUNCTION(0x4, "uart2")),	/* RX */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PA4,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ETXD3 */
		  SUNXI_FUNCTION(0x3, "spi1")),		/* CS1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PA5,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ETXD2 */
		  SUNXI_FUNCTION(0x3, "spi3")),		/* CS0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PA6,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ETXD1 */
		  SUNXI_FUNCTION(0x3, "spi3")),		/* CLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PA7,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ETXD0 */
		  SUNXI_FUNCTION(0x3, "spi3")),		/* MOSI */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PA8,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ERXCK */
		  SUNXI_FUNCTION(0x3, "spi3")),		/* MISO */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PA9,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ERXERR */
		  SUNXI_FUNCTION(0x3, "spi3")),		/* CS1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PA10,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ERXDV */
		  SUNXI_FUNCTION(0x4, "uart1")),	/* TX */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PA11,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* EMDC */
		  SUNXI_FUNCTION(0x4, "uart1")),	/* RX */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PA12,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* EMDIO */
		  SUNXI_FUNCTION(0x3, "uart6"),		/* TX */
		  SUNXI_FUNCTION(0x4, "uart1")),	/* RTS */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PA13,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ETXEN */
		  SUNXI_FUNCTION(0x3, "uart6"),		/* RX */
		  SUNXI_FUNCTION(0x4, "uart1")),	/* CTS */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PA14,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ETXCK */
		  SUNXI_FUNCTION(0x3, "uart7"),		/* TX */
		  SUNXI_FUNCTION(0x4, "uart1")),	/* DTR */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PA15,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ECRS */
		  SUNXI_FUNCTION(0x3, "uart7"),		/* RX */
		  SUNXI_FUNCTION(0x4, "uart1")),	/* DSR */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PA16,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ECOL */
		  SUNXI_FUNCTION(0x3, "can"),		/* TX */
		  SUNXI_FUNCTION(0x4, "uart1")),	/* DCD */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PA17,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "emac"),		/* ETXERR */
		  SUNXI_FUNCTION(0x3, "can"),		/* RX */
		  SUNXI_FUNCTION(0x4, "uart1")),	/* RING */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB0,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c0")),		/* SCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB1,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c0")),		/* SDA */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB2,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "pwm")),		/* PWM0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB3,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ir0")),		/* TX */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB4,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ir0")),		/* RX */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB5,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2s"),		/* MCLK */
		  SUNXI_FUNCTION(0x3, "ac97")),		/* MCLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB6,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2s"),		/* BCLK */
		  SUNXI_FUNCTION(0x3, "ac97")),		/* BCLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB7,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2s"),		/* LRCK */
		  SUNXI_FUNCTION(0x3, "ac97")),		/* SYNC */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB8,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2s"),		/* DO0 */
		  SUNXI_FUNCTION(0x3, "ac97")),		/* DO */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB9,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2s")),		/* DO1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB10,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2s")),		/* DO2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB11,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2s")),		/* DO3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB12,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2s"),		/* DI */
		  SUNXI_FUNCTION(0x3, "ac97")),		/* DI */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB13,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi2")),		/* CS1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB14,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi2"),		/* CS0 */
		  SUNXI_FUNCTION(0x3, "jtag")),		/* MS0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB15,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi2"),		/* CLK */
		  SUNXI_FUNCTION(0x3, "jtag")),		/* CK0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB16,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi2"),		/* MOSI */
		  SUNXI_FUNCTION(0x3, "jtag")),		/* DO0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB17,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi2"),		/* MISO */
		  SUNXI_FUNCTION(0x3, "jtag")),		/* DI0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB18,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c1")),		/* SCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB19,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c1")),		/* SDA */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB20,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c2")),		/* SCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB21,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c2")),		/* SDA */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB22,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart0"),		/* TX */
		  SUNXI_FUNCTION(0x3, "ir1")),		/* TX */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB23,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart0"),		/* RX */
		  SUNXI_FUNCTION(0x3, "ir1")),		/* RX */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC0,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NWE */
		  SUNXI_FUNCTION(0x3, "spi0")),		/* MOSI */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC1,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NALE */
		  SUNXI_FUNCTION(0x3, "spi0")),		/* MISO */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC2,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NCLE */
		  SUNXI_FUNCTION(0x3, "spi0")),		/* SCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC3,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0")),	/* NCE1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC4,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0")),	/* NCE0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC5,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0")),	/* NRE# */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC6,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NRB0 */
		  SUNXI_FUNCTION(0x3, "mmc2")),		/* CMD */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC7,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NRB1 */
		  SUNXI_FUNCTION(0x3, "mmc2")),		/* CLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC8,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NDQ0 */
		  SUNXI_FUNCTION(0x3, "mmc2")),		/* D0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC9,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NDQ1 */
		  SUNXI_FUNCTION(0x3, "mmc2")),		/* D1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC10,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NDQ2 */
		  SUNXI_FUNCTION(0x3, "mmc2")),		/* D2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC11,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NDQ3 */
		  SUNXI_FUNCTION(0x3, "mmc2")),		/* D3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC12,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0")),	/* NDQ4 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC13,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0")),	/* NDQ5 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC14,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0")),	/* NDQ6 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC15,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0")),	/* NDQ7 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC16,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0")),	/* NWP */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC17,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0")),	/* NCE2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC18,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0")),	/* NCE3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC19,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NCE4 */
		  SUNXI_FUNCTION(0x3, "spi2")),		/* CS0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC20,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NCE5 */
		  SUNXI_FUNCTION(0x3, "spi2")),		/* CLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC21,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NCE6 */
		  SUNXI_FUNCTION(0x3, "spi2")),		/* MOSI */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC22,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NCE7 */
		  SUNXI_FUNCTION(0x3, "spi2")),		/* MISO */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC23,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "spi0")),		/* CS0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC24,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0")),	/* NDQS */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD0,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D0 */
		  SUNXI_FUNCTION(0x3, "lvds0")),	/* VP0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD1,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D1 */
		  SUNXI_FUNCTION(0x3, "lvds0")),	/* VN0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD2,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D2 */
		  SUNXI_FUNCTION(0x3, "lvds0")),	/* VP1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD3,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D3 */
		  SUNXI_FUNCTION(0x3, "lvds0")),	/* VN1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD4,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D4 */
		  SUNXI_FUNCTION(0x3, "lvds0")),	/* VP2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD5,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D5 */
		  SUNXI_FUNCTION(0x3, "lvds0")),	/* VN2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD6,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D6 */
		  SUNXI_FUNCTION(0x3, "lvds0")),	/* VPC */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD7,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D7 */
		  SUNXI_FUNCTION(0x3, "lvds0")),	/* VNC */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD8,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D8 */
		  SUNXI_FUNCTION(0x3, "lvds0")),	/* VP3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD9,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D9 */
		  SUNXI_FUNCTION(0x3, "lvds0")),	/* VM3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD10,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D10 */
		  SUNXI_FUNCTION(0x3, "lvds1")),	/* VP0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD11,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D11 */
		  SUNXI_FUNCTION(0x3, "lvds1")),	/* VN0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD12,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D12 */
		  SUNXI_FUNCTION(0x3, "lvds1")),	/* VP1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD13,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D13 */
		  SUNXI_FUNCTION(0x3, "lvds1")),	/* VN1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD14,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D14 */
		  SUNXI_FUNCTION(0x3, "lvds1")),	/* VP2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD15,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D15 */
		  SUNXI_FUNCTION(0x3, "lvds1")),	/* VN2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD16,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D16 */
		  SUNXI_FUNCTION(0x3, "lvds1")),	/* VPC */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD17,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D17 */
		  SUNXI_FUNCTION(0x3, "lvds1")),	/* VNC */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD18,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D18 */
		  SUNXI_FUNCTION(0x3, "lvds1")),	/* VP3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD19,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D19 */
		  SUNXI_FUNCTION(0x3, "lvds1")),	/* VN3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD20,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D20 */
		  SUNXI_FUNCTION(0x3, "csi1")),		/* MCLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD21,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D21 */
		  SUNXI_FUNCTION(0x3, "sim")),		/* VPPEN */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD22,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D22 */
		  SUNXI_FUNCTION(0x3, "sim")),		/* VPPPP */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD23,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* D23 */
		  SUNXI_FUNCTION(0x3, "sim")),		/* DET */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD24,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* CLK */
		  SUNXI_FUNCTION(0x3, "sim")),		/* VCCEN */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD25,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* DE */
		  SUNXI_FUNCTION(0x3, "sim")),		/* RST */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD26,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* HSYNC */
		  SUNXI_FUNCTION(0x3, "sim")),		/* SCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD27,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0"),		/* VSYNC */
		  SUNXI_FUNCTION(0x3, "sim")),		/* SDA */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PE0,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* CLK */
		  SUNXI_FUNCTION(0x3, "csi0")),		/* PCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PE1,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* ERR */
		  SUNXI_FUNCTION(0x3, "csi0")),		/* CK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PE2,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* SYNC */
		  SUNXI_FUNCTION(0x3, "csi0")),		/* HSYNC */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PE3,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* DVLD */
		  SUNXI_FUNCTION(0x3, "csi0")),		/* VSYNC */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PE4,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* D0 */
		  SUNXI_FUNCTION(0x3, "csi0")),		/* D0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PE5,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* D1 */
		  SUNXI_FUNCTION(0x3, "csi0"),		/* D1 */
		  SUNXI_FUNCTION(0x4, "sim")),		/* VPPEN */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PE6,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* D2 */
		  SUNXI_FUNCTION(0x3, "csi0")),		/* D2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PE7,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* D3 */
		  SUNXI_FUNCTION(0x3, "csi0")),		/* D3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PE8,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* D4 */
		  SUNXI_FUNCTION(0x3, "csi0")),		/* D4 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PE9,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* D5 */
		  SUNXI_FUNCTION(0x3, "csi0")),		/* D5 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PE10,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* D6 */
		  SUNXI_FUNCTION(0x3, "csi0")),		/* D6 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PE11,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts0"),		/* D7 */
		  SUNXI_FUNCTION(0x3, "csi0")),		/* D7 */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PF0,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc0"),		/* D1 */
		  SUNXI_FUNCTION(0x4, "jtag")),		/* MSI */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PF1,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc0"),		/* D0 */
		  SUNXI_FUNCTION(0x4, "jtag")),		/* DI1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PF2,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc0"),		/* CLK */
		  SUNXI_FUNCTION(0x4, "uart0")),	/* TX */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PF3,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc0"),		/* CMD */
		  SUNXI_FUNCTION(0x4, "jtag")),		/* DO1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PF4,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc0"),		/* D3 */
		  SUNXI_FUNCTION(0x4, "uart0")),	/* RX */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PF5,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc0"),		/* D2 */
		  SUNXI_FUNCTION(0x4, "jtag")),		/* CK1 */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PG0,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts1"),		/* CLK */
		  SUNXI_FUNCTION(0x3, "csi1"),		/* PCK */
		  SUNXI_FUNCTION(0x4, "mmc1")),		/* CMD */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PG1,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts1"),		/* ERR */
		  SUNXI_FUNCTION(0x3, "csi1"),		/* CK */
		  SUNXI_FUNCTION(0x4, "mmc1")),		/* CLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PG2,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts1"),		/* SYNC */
		  SUNXI_FUNCTION(0x3, "csi1"),		/* HSYNC */
		  SUNXI_FUNCTION(0x4, "mmc1")),		/* D0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PG3,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts1"),		/* DVLD */
		  SUNXI_FUNCTION(0x3, "csi1"),		/* VSYNC */
		  SUNXI_FUNCTION(0x4, "mmc1")),		/* D1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PG4,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts1"),		/* D0 */
		  SUNXI_FUNCTION(0x3, "csi1"),		/* D0 */
		  SUNXI_FUNCTION(0x4, "mmc1"),		/* D2 */
		  SUNXI_FUNCTION(0x5, "csi0")),		/* D8 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PG5,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts1"),		/* D1 */
		  SUNXI_FUNCTION(0x3, "csi1"),		/* D1 */
		  SUNXI_FUNCTION(0x4, "mmc1"),		/* D3 */
		  SUNXI_FUNCTION(0x5, "csi0")),		/* D9 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PG6,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts1"),		/* D2 */
		  SUNXI_FUNCTION(0x3, "csi1"),		/* D2 */
		  SUNXI_FUNCTION(0x4, "uart3"),		/* TX */
		  SUNXI_FUNCTION(0x5, "csi0")),		/* D10 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PG7,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts1"),		/* D3 */
		  SUNXI_FUNCTION(0x3, "csi1"),		/* D3 */
		  SUNXI_FUNCTION(0x4, "uart3"),		/* RX */
		  SUNXI_FUNCTION(0x5, "csi0")),		/* D11 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PG8,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts1"),		/* D4 */
		  SUNXI_FUNCTION(0x3, "csi1"),		/* D4 */
		  SUNXI_FUNCTION(0x4, "uart3"),		/* RTS */
		  SUNXI_FUNCTION(0x5, "csi0")),		/* D12 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PG9,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts1"),		/* D5 */
		  SUNXI_FUNCTION(0x3, "csi1"),		/* D5 */
		  SUNXI_FUNCTION(0x4, "uart3"),		/* CTS */
		  SUNXI_FUNCTION(0x5, "csi0")),		/* D13 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PG10,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts1"),		/* D6 */
		  SUNXI_FUNCTION(0x3, "csi1"),		/* D6 */
		  SUNXI_FUNCTION(0x4, "uart4"),		/* TX */
		  SUNXI_FUNCTION(0x5, "csi0")),		/* D14 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PG11,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ts1"),		/* D7 */
		  SUNXI_FUNCTION(0x3, "csi1"),		/* D7 */
		  SUNXI_FUNCTION(0x4, "uart4"),		/* RX */
		  SUNXI_FUNCTION(0x5, "csi0")),		/* D15 */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH0,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D0 */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATAA0 */
		  SUNXI_FUNCTION(0x4, "uart3"),		/* TX */
		  SUNXI_FUNCTION_IRQ(0x6, 0),		/* EINT0 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH1,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D1 */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATAA1 */
		  SUNXI_FUNCTION(0x4, "uart3"),		/* RX */
		  SUNXI_FUNCTION_IRQ(0x6, 1),		/* EINT1 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH2,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D2 */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATAA2 */
		  SUNXI_FUNCTION(0x4, "uart3"),		/* RTS */
		  SUNXI_FUNCTION_IRQ(0x6, 2),		/* EINT2 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH3,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D3 */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATAIRQ */
		  SUNXI_FUNCTION(0x4, "uart3"),		/* CTS */
		  SUNXI_FUNCTION_IRQ(0x6, 3),		/* EINT3 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH4,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D4 */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATAD0 */
		  SUNXI_FUNCTION(0x4, "uart4"),		/* TX */
		  SUNXI_FUNCTION_IRQ(0x6, 4),		/* EINT4 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D4 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH5,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D5 */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATAD1 */
		  SUNXI_FUNCTION(0x4, "uart4"),		/* RX */
		  SUNXI_FUNCTION_IRQ(0x6, 5),		/* EINT5 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D5 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH6,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D6 */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATAD2 */
		  SUNXI_FUNCTION(0x4, "uart5"),		/* TX */
		  SUNXI_FUNCTION(0x5, "ms"),		/* BS */
		  SUNXI_FUNCTION_IRQ(0x6, 6),		/* EINT6 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D6 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH7,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D7 */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATAD3 */
		  SUNXI_FUNCTION(0x4, "uart5"),		/* RX */
		  SUNXI_FUNCTION(0x5, "ms"),		/* CLK */
		  SUNXI_FUNCTION_IRQ(0x6, 7),		/* EINT7 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D7 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH8,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D8 */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATAD4 */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* IN0 */
		  SUNXI_FUNCTION(0x5, "ms"),		/* D0 */
		  SUNXI_FUNCTION_IRQ(0x6, 8),		/* EINT8 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D8 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH9,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D9 */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATAD5 */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* IN1 */
		  SUNXI_FUNCTION(0x5, "ms"),		/* D1 */
		  SUNXI_FUNCTION_IRQ(0x6, 9),		/* EINT9 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D9 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH10,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D10 */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATAD6 */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* IN2 */
		  SUNXI_FUNCTION(0x5, "ms"),		/* D2 */
		  SUNXI_FUNCTION_IRQ(0x6, 10),		/* EINT10 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D10 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH11,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D11 */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATAD7 */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* IN3 */
		  SUNXI_FUNCTION(0x5, "ms"),		/* D3 */
		  SUNXI_FUNCTION_IRQ(0x6, 11),		/* EINT11 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D11 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH12,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D12 */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATAD8 */
		  SUNXI_FUNCTION(0x4, "ps2"),		/* SCK1 */
		  SUNXI_FUNCTION_IRQ(0x6, 12),		/* EINT12 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D12 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH13,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D13 */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATAD9 */
		  SUNXI_FUNCTION(0x4, "ps2"),		/* SDA1 */
		  SUNXI_FUNCTION(0x5, "sim"),		/* RST */
		  SUNXI_FUNCTION_IRQ(0x6, 13),		/* EINT13 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D13 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH14,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D14 */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATAD10 */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* IN4 */
		  SUNXI_FUNCTION(0x5, "sim"),		/* VPPEN */
		  SUNXI_FUNCTION_IRQ(0x6, 14),		/* EINT14 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D14 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH15,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D15 */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATAD11 */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* IN5 */
		  SUNXI_FUNCTION(0x5, "sim"),		/* VPPPP */
		  SUNXI_FUNCTION_IRQ(0x6, 15),		/* EINT15 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D15 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH16,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D16 */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATAD12 */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* IN6 */
		  SUNXI_FUNCTION_IRQ(0x6, 16),		/* EINT16 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D16 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH17,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D17 */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATAD13 */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* IN7 */
		  SUNXI_FUNCTION(0x5, "sim"),		/* VCCEN */
		SUNXI_FUNCTION_IRQ(0x6, 17),		/* EINT17 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D17 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH18,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D18 */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATAD14 */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* OUT0 */
		  SUNXI_FUNCTION(0x5, "sim"),		/* SCK */
		SUNXI_FUNCTION_IRQ(0x6, 18),		/* EINT18 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D18 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH19,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D19 */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATAD15 */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* OUT1 */
		  SUNXI_FUNCTION(0x5, "sim"),		/* SDA */
		SUNXI_FUNCTION_IRQ(0x6, 19),		/* EINT19 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D19 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH20,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D20 */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATAOE */
		  SUNXI_FUNCTION(0x4, "can"),		/* TX */
		SUNXI_FUNCTION_IRQ(0x6, 20),		/* EINT20 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D20 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH21,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D21 */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATADREQ */
		  SUNXI_FUNCTION(0x4, "can"),		/* RX */
		SUNXI_FUNCTION_IRQ(0x6, 21),		/* EINT21 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D21 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH22,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D22 */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATADACK */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* OUT2 */
		  SUNXI_FUNCTION(0x5, "mmc1"),		/* CMD */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D22 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH23,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* D23 */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATACS0 */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* OUT3 */
		  SUNXI_FUNCTION(0x5, "mmc1"),		/* CLK */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* D23 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH24,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* CLK */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATACS1 */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* OUT4 */
		  SUNXI_FUNCTION(0x5, "mmc1"),		/* D0 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* PCLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH25,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* DE */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATAIORDY */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* OUT5 */
		  SUNXI_FUNCTION(0x5, "mmc1"),		/* D1 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* FIELD */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH26,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* HSYNC */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATAIOR */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* OUT6 */
		  SUNXI_FUNCTION(0x5, "mmc1"),		/* D2 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* HSYNC */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PH27,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd1"),		/* VSYNC */
		  SUNXI_FUNCTION(0x3, "pata"),		/* ATAIOW */
		  SUNXI_FUNCTION(0x4, "keypad"),	/* OUT7 */
		  SUNXI_FUNCTION(0x5, "mmc1"),		/* D3 */
		  SUNXI_FUNCTION(0x7, "csi1")),		/* VSYNC */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PI0,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out")),
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PI1,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out")),
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PI2,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out")),
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PI3,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "pwm")),		/* PWM1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PI4,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc3")),		/* CMD */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PI5,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc3")),		/* CLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PI6,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc3")),		/* D0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PI7,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc3")),		/* D1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PI8,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc3")),		/* D2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PI9,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc3")),		/* D3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PI10,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi0"),		/* CS0 */
		  SUNXI_FUNCTION(0x3, "uart5"),		/* TX */
		  SUNXI_FUNCTION_IRQ(0x6, 22)),		/* EINT22 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PI11,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi0"),		/* CLK */
		  SUNXI_FUNCTION(0x3, "uart5"),		/* RX */
		  SUNXI_FUNCTION_IRQ(0x6, 23)),		/* EINT23 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PI12,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi0"),		/* MOSI */
		  SUNXI_FUNCTION(0x3, "uart6"),		/* TX */
		  SUNXI_FUNCTION_IRQ(0x6, 24)),		/* EINT24 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PI13,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi0"),		/* MISO */
		  SUNXI_FUNCTION(0x3, "uart6"),		/* RX */
		  SUNXI_FUNCTION_IRQ(0x6, 25)),		/* EINT25 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PI14,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi0"),		/* CS1 */
		  SUNXI_FUNCTION(0x3, "ps2"),		/* SCK1 */
		  SUNXI_FUNCTION(0x4, "timer4"),	/* TCLKIN0 */
		  SUNXI_FUNCTION_IRQ(0x6, 26)),		/* EINT26 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PI15,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi1"),		/* CS1 */
		  SUNXI_FUNCTION(0x3, "ps2"),		/* SDA1 */
		  SUNXI_FUNCTION(0x4, "timer5"),	/* TCLKIN1 */
		  SUNXI_FUNCTION_IRQ(0x6, 27)),		/* EINT27 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PI16,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi1"),		/* CS0 */
		  SUNXI_FUNCTION(0x3, "uart2"),		/* RTS */
		  SUNXI_FUNCTION_IRQ(0x6, 28)),		/* EINT28 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PI17,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi1"),		/* CLK */
		  SUNXI_FUNCTION(0x3, "uart2"),		/* CTS */
		  SUNXI_FUNCTION_IRQ(0x6, 29)),		/* EINT29 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PI18,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi1"),		/* MOSI */
		  SUNXI_FUNCTION(0x3, "uart2"),		/* TX */
		  SUNXI_FUNCTION_IRQ(0x6, 30)),		/* EINT30 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PI19,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi1"),		/* MISO */
		  SUNXI_FUNCTION(0x3, "uart2"),		/* RX */
		  SUNXI_FUNCTION_IRQ(0x6, 31)),		/* EINT31 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PI20,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ps2"),		/* SCK0 */
		  SUNXI_FUNCTION(0x3, "uart7"),		/* TX */
		  SUNXI_FUNCTION(0x4, "hdmi")),		/* HSCL */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PI21,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ps2"),		/* SDA0 */
		  SUNXI_FUNCTION(0x3, "uart7"),		/* RX */
		  SUNXI_FUNCTION(0x4, "hdmi")),		/* HSDA */
};

static const struct sunxi_desc_pin sun5i_a13_pins[] = {
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB0,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c0")),		/* SCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB1,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c0")),		/* SDA */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB2,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "pwm"),
		  SUNXI_FUNCTION_IRQ(0x6, 16)),		/* EINT16 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB3,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ir0"),		/* TX */
		  SUNXI_FUNCTION_IRQ(0x6, 17)),		/* EINT17 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB4,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ir0"),		/* RX */
		  SUNXI_FUNCTION_IRQ(0x6, 18)),		/* EINT18 */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB10,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi2"),		/* CS1 */
		  SUNXI_FUNCTION_IRQ(0x6, 24)),		/* EINT24 */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB15,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c1")),		/* SCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB16,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c1")),		/* SDA */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB17,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c2")),		/* SCK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PB18,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c2")),		/* SDA */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC0,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NWE */
		  SUNXI_FUNCTION(0x3, "spi0")),		/* MOSI */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC1,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NALE */
		  SUNXI_FUNCTION(0x3, "spi0")),		/* MISO */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC2,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NCLE */
		  SUNXI_FUNCTION(0x3, "spi0")),		/* CLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC3,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NCE1 */
		  SUNXI_FUNCTION(0x3, "spi0")),		/* CS0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC4,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0")),	/* NCE0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC5,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0")),	/* NRE */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC6,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NRB0 */
		  SUNXI_FUNCTION(0x3, "mmc2")),		/* CMD */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC7,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NRB1 */
		  SUNXI_FUNCTION(0x3, "mmc2")),		/* CLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC8,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NDQ0 */
		  SUNXI_FUNCTION(0x3, "mmc2")),		/* D0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC9,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NDQ1 */
		  SUNXI_FUNCTION(0x3, "mmc2")),		/* D1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC10,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NDQ2 */
		  SUNXI_FUNCTION(0x3, "mmc2")),		/* D2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC11,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NDQ3 */
		  SUNXI_FUNCTION(0x3, "mmc2")),		/* D3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC12,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NDQ4 */
		  SUNXI_FUNCTION(0x3, "mmc2")),		/* D4 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC13,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NDQ5 */
		  SUNXI_FUNCTION(0x3, "mmc2")),		/* D5 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC14,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NDQ6 */
		  SUNXI_FUNCTION(0x3, "mmc2")),		/* D6 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC15,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NDQ7 */
		  SUNXI_FUNCTION(0x3, "mmc2")),		/* D7 */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PC19,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* NDQS */
		  SUNXI_FUNCTION(0x4, "uart3")),	/* RTS */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD2,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* D2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD3,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* D3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD4,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* D4 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD5,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* D5 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD6,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* D6 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD7,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* D7 */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD10,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* D10 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD11,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* D11 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD12,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* D12 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD13,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* D13 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD14,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* D14 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD15,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* D15 */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD18,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* D18 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD19,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* D19 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD20,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* D20 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD21,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* D21 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD22,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* D22 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD23,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* D23 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD24,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* CLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD25,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* DE */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD26,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* HSYNC */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PD27,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "lcd0")),		/* VSYNC */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PE0,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x3, "csi0"),		/* PCLK */
		  SUNXI_FUNCTION(0x4, "spi2"),		/* CS0 */
		  SUNXI_FUNCTION_IRQ(0x6, 14)),		/* EINT14 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PE1,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x3, "csi0"),		/* MCLK */
		  SUNXI_FUNCTION(0x4, "spi2"),		/* CLK */
		  SUNXI_FUNCTION_IRQ(0x6, 15)),		/* EINT15 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PE2,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x3, "csi0"),		/* HSYNC */
		  SUNXI_FUNCTION(0x4, "spi2")),		/* MOSI */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PE3,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "csi0"),		/* VSYNC */
		  SUNXI_FUNCTION(0x4, "spi2")),		/* MISO */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PE4,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "csi0"),		/* D0 */
		  SUNXI_FUNCTION(0x4, "mmc2")),		/* D0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PE5,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "csi0"),		/* D1 */
		  SUNXI_FUNCTION(0x4, "mmc2")),		/* D1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PE6,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "csi0"),		/* D2 */
		  SUNXI_FUNCTION(0x4, "mmc2")),		/* D2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PE7,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "csi0"),		/* D3 */
		  SUNXI_FUNCTION(0x4, "mmc2")),		/* D3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PE8,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "csi0"),		/* D4 */
		  SUNXI_FUNCTION(0x4, "mmc2")),		/* CMD */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PE9,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "csi0"),		/* D5 */
		  SUNXI_FUNCTION(0x4, "mmc2")),		/* CLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PE10,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "csi0"),		/* D6 */
		  SUNXI_FUNCTION(0x4, "uart1")),	/* TX */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PE11,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "csi0"),		/* D7 */
		  SUNXI_FUNCTION(0x4, "uart1")),	/* RX */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PF0,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x4, "mmc0")),		/* D1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PF1,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x4, "mmc0")),		/* D0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PF2,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x4, "mmc0")),		/* CLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PF3,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x4, "mmc0")),		/* CMD */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PF4,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x4, "mmc0")),		/* D3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PF5,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x4, "mmc0")),		/* D2 */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PG0,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ(0x6, 0)),		/* EINT0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PG1,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ(0x6, 1)),		/* EINT1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PG2,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ(0x6, 2)),		/* EINT2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PG3,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc1"),		/* CMD */
		  SUNXI_FUNCTION(0x4, "uart1"),		/* TX */
		  SUNXI_FUNCTION_IRQ(0x6, 3)),		/* EINT3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PG4,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc1"),		/* CLK */
		  SUNXI_FUNCTION(0x4, "uart1"),		/* RX */
		  SUNXI_FUNCTION_IRQ(0x6, 4)),		/* EINT4 */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PG9,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi1"),		/* CS0 */
		  SUNXI_FUNCTION(0x3, "uart3"),		/* TX */
		  SUNXI_FUNCTION_IRQ(0x6, 9)),		/* EINT9 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PG10,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi1"),		/* CLK */
		  SUNXI_FUNCTION(0x3, "uart3"),		/* RX */
		  SUNXI_FUNCTION_IRQ(0x6, 10)),		/* EINT10 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PG11,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi1"),		/* MOSI */
		  SUNXI_FUNCTION(0x3, "uart3"),		/* CTS */
		  SUNXI_FUNCTION_IRQ(0x6, 11)),		/* EINT11 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN_PG12,
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "spi1"),		/* MISO */
		  SUNXI_FUNCTION(0x3, "uart3"),		/* RTS */
		  SUNXI_FUNCTION_IRQ(0x6, 12)),		/* EINT12 */
};

static const struct sunxi_pinctrl_desc sun4i_a10_pinctrl_data = {
	.pins = sun4i_a10_pins,
	.npins = ARRAY_SIZE(sun4i_a10_pins),
};

static const struct sunxi_pinctrl_desc sun5i_a13_pinctrl_data = {
	.pins = sun5i_a13_pins,
	.npins = ARRAY_SIZE(sun5i_a13_pins),
};

#endif /* __PINCTRL_SUNXI_PINS_H */
