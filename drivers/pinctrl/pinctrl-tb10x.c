// SPDX-License-Identifier: GPL-2.0-only
/*
 * Abilis Systems TB10x pin control driver
 *
 * Copyright (C) Abilis Systems 2012
 *
 * Author: Christian Ruppert <christian.ruppert@abilis.com>
 */

#include <linux/stringify.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/machine.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/slab.h>

#include "pinctrl-utils.h"

#define TB10X_PORT1 (0)
#define TB10X_PORT2 (16)
#define TB10X_PORT3 (32)
#define TB10X_PORT4 (48)
#define TB10X_PORT5 (128)
#define TB10X_PORT6 (64)
#define TB10X_PORT7 (80)
#define TB10X_PORT8 (96)
#define TB10X_PORT9 (112)
#define TB10X_GPIOS (256)

#define PCFG_PORT_BITWIDTH (2)
#define PCFG_PORT_MASK(PORT) \
	(((1 << PCFG_PORT_BITWIDTH) - 1) << (PCFG_PORT_BITWIDTH * (PORT)))

static const struct pinctrl_pin_desc tb10x_pins[] = {
	/* Port 1 */
	PINCTRL_PIN(TB10X_PORT1 +  0, "MICLK_S0"),
	PINCTRL_PIN(TB10X_PORT1 +  1, "MISTRT_S0"),
	PINCTRL_PIN(TB10X_PORT1 +  2, "MIVAL_S0"),
	PINCTRL_PIN(TB10X_PORT1 +  3, "MDI_S0"),
	PINCTRL_PIN(TB10X_PORT1 +  4, "GPIOA0"),
	PINCTRL_PIN(TB10X_PORT1 +  5, "GPIOA1"),
	PINCTRL_PIN(TB10X_PORT1 +  6, "GPIOA2"),
	PINCTRL_PIN(TB10X_PORT1 +  7, "MDI_S1"),
	PINCTRL_PIN(TB10X_PORT1 +  8, "MIVAL_S1"),
	PINCTRL_PIN(TB10X_PORT1 +  9, "MISTRT_S1"),
	PINCTRL_PIN(TB10X_PORT1 + 10, "MICLK_S1"),
	/* Port 2 */
	PINCTRL_PIN(TB10X_PORT2 +  0, "MICLK_S2"),
	PINCTRL_PIN(TB10X_PORT2 +  1, "MISTRT_S2"),
	PINCTRL_PIN(TB10X_PORT2 +  2, "MIVAL_S2"),
	PINCTRL_PIN(TB10X_PORT2 +  3, "MDI_S2"),
	PINCTRL_PIN(TB10X_PORT2 +  4, "GPIOC0"),
	PINCTRL_PIN(TB10X_PORT2 +  5, "GPIOC1"),
	PINCTRL_PIN(TB10X_PORT2 +  6, "GPIOC2"),
	PINCTRL_PIN(TB10X_PORT2 +  7, "MDI_S3"),
	PINCTRL_PIN(TB10X_PORT2 +  8, "MIVAL_S3"),
	PINCTRL_PIN(TB10X_PORT2 +  9, "MISTRT_S3"),
	PINCTRL_PIN(TB10X_PORT2 + 10, "MICLK_S3"),
	/* Port 3 */
	PINCTRL_PIN(TB10X_PORT3 +  0, "MICLK_S4"),
	PINCTRL_PIN(TB10X_PORT3 +  1, "MISTRT_S4"),
	PINCTRL_PIN(TB10X_PORT3 +  2, "MIVAL_S4"),
	PINCTRL_PIN(TB10X_PORT3 +  3, "MDI_S4"),
	PINCTRL_PIN(TB10X_PORT3 +  4, "GPIOE0"),
	PINCTRL_PIN(TB10X_PORT3 +  5, "GPIOE1"),
	PINCTRL_PIN(TB10X_PORT3 +  6, "GPIOE2"),
	PINCTRL_PIN(TB10X_PORT3 +  7, "MDI_S5"),
	PINCTRL_PIN(TB10X_PORT3 +  8, "MIVAL_S5"),
	PINCTRL_PIN(TB10X_PORT3 +  9, "MISTRT_S5"),
	PINCTRL_PIN(TB10X_PORT3 + 10, "MICLK_S5"),
	/* Port 4 */
	PINCTRL_PIN(TB10X_PORT4 +  0, "MICLK_S6"),
	PINCTRL_PIN(TB10X_PORT4 +  1, "MISTRT_S6"),
	PINCTRL_PIN(TB10X_PORT4 +  2, "MIVAL_S6"),
	PINCTRL_PIN(TB10X_PORT4 +  3, "MDI_S6"),
	PINCTRL_PIN(TB10X_PORT4 +  4, "GPIOG0"),
	PINCTRL_PIN(TB10X_PORT4 +  5, "GPIOG1"),
	PINCTRL_PIN(TB10X_PORT4 +  6, "GPIOG2"),
	PINCTRL_PIN(TB10X_PORT4 +  7, "MDI_S7"),
	PINCTRL_PIN(TB10X_PORT4 +  8, "MIVAL_S7"),
	PINCTRL_PIN(TB10X_PORT4 +  9, "MISTRT_S7"),
	PINCTRL_PIN(TB10X_PORT4 + 10, "MICLK_S7"),
	/* Port 5 */
	PINCTRL_PIN(TB10X_PORT5 +  0, "PC_CE1N"),
	PINCTRL_PIN(TB10X_PORT5 +  1, "PC_CE2N"),
	PINCTRL_PIN(TB10X_PORT5 +  2, "PC_REGN"),
	PINCTRL_PIN(TB10X_PORT5 +  3, "PC_INPACKN"),
	PINCTRL_PIN(TB10X_PORT5 +  4, "PC_OEN"),
	PINCTRL_PIN(TB10X_PORT5 +  5, "PC_WEN"),
	PINCTRL_PIN(TB10X_PORT5 +  6, "PC_IORDN"),
	PINCTRL_PIN(TB10X_PORT5 +  7, "PC_IOWRN"),
	PINCTRL_PIN(TB10X_PORT5 +  8, "PC_RDYIRQN"),
	PINCTRL_PIN(TB10X_PORT5 +  9, "PC_WAITN"),
	PINCTRL_PIN(TB10X_PORT5 + 10, "PC_A0"),
	PINCTRL_PIN(TB10X_PORT5 + 11, "PC_A1"),
	PINCTRL_PIN(TB10X_PORT5 + 12, "PC_A2"),
	PINCTRL_PIN(TB10X_PORT5 + 13, "PC_A3"),
	PINCTRL_PIN(TB10X_PORT5 + 14, "PC_A4"),
	PINCTRL_PIN(TB10X_PORT5 + 15, "PC_A5"),
	PINCTRL_PIN(TB10X_PORT5 + 16, "PC_A6"),
	PINCTRL_PIN(TB10X_PORT5 + 17, "PC_A7"),
	PINCTRL_PIN(TB10X_PORT5 + 18, "PC_A8"),
	PINCTRL_PIN(TB10X_PORT5 + 19, "PC_A9"),
	PINCTRL_PIN(TB10X_PORT5 + 20, "PC_A10"),
	PINCTRL_PIN(TB10X_PORT5 + 21, "PC_A11"),
	PINCTRL_PIN(TB10X_PORT5 + 22, "PC_A12"),
	PINCTRL_PIN(TB10X_PORT5 + 23, "PC_A13"),
	PINCTRL_PIN(TB10X_PORT5 + 24, "PC_A14"),
	PINCTRL_PIN(TB10X_PORT5 + 25, "PC_D0"),
	PINCTRL_PIN(TB10X_PORT5 + 26, "PC_D1"),
	PINCTRL_PIN(TB10X_PORT5 + 27, "PC_D2"),
	PINCTRL_PIN(TB10X_PORT5 + 28, "PC_D3"),
	PINCTRL_PIN(TB10X_PORT5 + 29, "PC_D4"),
	PINCTRL_PIN(TB10X_PORT5 + 30, "PC_D5"),
	PINCTRL_PIN(TB10X_PORT5 + 31, "PC_D6"),
	PINCTRL_PIN(TB10X_PORT5 + 32, "PC_D7"),
	PINCTRL_PIN(TB10X_PORT5 + 33, "PC_MOSTRT"),
	PINCTRL_PIN(TB10X_PORT5 + 34, "PC_MOVAL"),
	PINCTRL_PIN(TB10X_PORT5 + 35, "PC_MDO0"),
	PINCTRL_PIN(TB10X_PORT5 + 36, "PC_MDO1"),
	PINCTRL_PIN(TB10X_PORT5 + 37, "PC_MDO2"),
	PINCTRL_PIN(TB10X_PORT5 + 38, "PC_MDO3"),
	PINCTRL_PIN(TB10X_PORT5 + 39, "PC_MDO4"),
	PINCTRL_PIN(TB10X_PORT5 + 40, "PC_MDO5"),
	PINCTRL_PIN(TB10X_PORT5 + 41, "PC_MDO6"),
	PINCTRL_PIN(TB10X_PORT5 + 42, "PC_MDO7"),
	PINCTRL_PIN(TB10X_PORT5 + 43, "PC_MISTRT"),
	PINCTRL_PIN(TB10X_PORT5 + 44, "PC_MIVAL"),
	PINCTRL_PIN(TB10X_PORT5 + 45, "PC_MDI0"),
	PINCTRL_PIN(TB10X_PORT5 + 46, "PC_MDI1"),
	PINCTRL_PIN(TB10X_PORT5 + 47, "PC_MDI2"),
	PINCTRL_PIN(TB10X_PORT5 + 48, "PC_MDI3"),
	PINCTRL_PIN(TB10X_PORT5 + 49, "PC_MDI4"),
	PINCTRL_PIN(TB10X_PORT5 + 50, "PC_MDI5"),
	PINCTRL_PIN(TB10X_PORT5 + 51, "PC_MDI6"),
	PINCTRL_PIN(TB10X_PORT5 + 52, "PC_MDI7"),
	PINCTRL_PIN(TB10X_PORT5 + 53, "PC_MICLK"),
	/* Port 6 */
	PINCTRL_PIN(TB10X_PORT6 + 0, "T_MOSTRT_S0"),
	PINCTRL_PIN(TB10X_PORT6 + 1, "T_MOVAL_S0"),
	PINCTRL_PIN(TB10X_PORT6 + 2, "T_MDO_S0"),
	PINCTRL_PIN(TB10X_PORT6 + 3, "T_MOSTRT_S1"),
	PINCTRL_PIN(TB10X_PORT6 + 4, "T_MOVAL_S1"),
	PINCTRL_PIN(TB10X_PORT6 + 5, "T_MDO_S1"),
	PINCTRL_PIN(TB10X_PORT6 + 6, "T_MOSTRT_S2"),
	PINCTRL_PIN(TB10X_PORT6 + 7, "T_MOVAL_S2"),
	PINCTRL_PIN(TB10X_PORT6 + 8, "T_MDO_S2"),
	PINCTRL_PIN(TB10X_PORT6 + 9, "T_MOSTRT_S3"),
	/* Port 7 */
	PINCTRL_PIN(TB10X_PORT7 + 0, "UART0_TXD"),
	PINCTRL_PIN(TB10X_PORT7 + 1, "UART0_RXD"),
	PINCTRL_PIN(TB10X_PORT7 + 2, "UART0_CTS"),
	PINCTRL_PIN(TB10X_PORT7 + 3, "UART0_RTS"),
	PINCTRL_PIN(TB10X_PORT7 + 4, "UART1_TXD"),
	PINCTRL_PIN(TB10X_PORT7 + 5, "UART1_RXD"),
	PINCTRL_PIN(TB10X_PORT7 + 6, "UART1_CTS"),
	PINCTRL_PIN(TB10X_PORT7 + 7, "UART1_RTS"),
	/* Port 8 */
	PINCTRL_PIN(TB10X_PORT8 + 0, "SPI3_CLK"),
	PINCTRL_PIN(TB10X_PORT8 + 1, "SPI3_MISO"),
	PINCTRL_PIN(TB10X_PORT8 + 2, "SPI3_MOSI"),
	PINCTRL_PIN(TB10X_PORT8 + 3, "SPI3_SSN"),
	/* Port 9 */
	PINCTRL_PIN(TB10X_PORT9 + 0, "SPI1_CLK"),
	PINCTRL_PIN(TB10X_PORT9 + 1, "SPI1_MISO"),
	PINCTRL_PIN(TB10X_PORT9 + 2, "SPI1_MOSI"),
	PINCTRL_PIN(TB10X_PORT9 + 3, "SPI1_SSN0"),
	PINCTRL_PIN(TB10X_PORT9 + 4, "SPI1_SSN1"),
	/* Unmuxed GPIOs */
	PINCTRL_PIN(TB10X_GPIOS +  0, "GPIOB0"),
	PINCTRL_PIN(TB10X_GPIOS +  1, "GPIOB1"),

	PINCTRL_PIN(TB10X_GPIOS +  2, "GPIOD0"),
	PINCTRL_PIN(TB10X_GPIOS +  3, "GPIOD1"),

	PINCTRL_PIN(TB10X_GPIOS +  4, "GPIOF0"),
	PINCTRL_PIN(TB10X_GPIOS +  5, "GPIOF1"),

	PINCTRL_PIN(TB10X_GPIOS +  6, "GPIOH0"),
	PINCTRL_PIN(TB10X_GPIOS +  7, "GPIOH1"),

	PINCTRL_PIN(TB10X_GPIOS +  8, "GPIOI0"),
	PINCTRL_PIN(TB10X_GPIOS +  9, "GPIOI1"),
	PINCTRL_PIN(TB10X_GPIOS + 10, "GPIOI2"),
	PINCTRL_PIN(TB10X_GPIOS + 11, "GPIOI3"),
	PINCTRL_PIN(TB10X_GPIOS + 12, "GPIOI4"),
	PINCTRL_PIN(TB10X_GPIOS + 13, "GPIOI5"),
	PINCTRL_PIN(TB10X_GPIOS + 14, "GPIOI6"),
	PINCTRL_PIN(TB10X_GPIOS + 15, "GPIOI7"),
	PINCTRL_PIN(TB10X_GPIOS + 16, "GPIOI8"),
	PINCTRL_PIN(TB10X_GPIOS + 17, "GPIOI9"),
	PINCTRL_PIN(TB10X_GPIOS + 18, "GPIOI10"),
	PINCTRL_PIN(TB10X_GPIOS + 19, "GPIOI11"),

	PINCTRL_PIN(TB10X_GPIOS + 20, "GPION0"),
	PINCTRL_PIN(TB10X_GPIOS + 21, "GPION1"),
	PINCTRL_PIN(TB10X_GPIOS + 22, "GPION2"),
	PINCTRL_PIN(TB10X_GPIOS + 23, "GPION3"),
#define MAX_PIN (TB10X_GPIOS + 24)
	PINCTRL_PIN(MAX_PIN,  "GPION4"),
};


/* Port 1 */
static const unsigned mis0_pins[]  = {	TB10X_PORT1 + 0, TB10X_PORT1 + 1,
					TB10X_PORT1 + 2, TB10X_PORT1 + 3};
static const unsigned gpioa_pins[] = {	TB10X_PORT1 + 4, TB10X_PORT1 + 5,
					TB10X_PORT1 + 6};
static const unsigned mis1_pins[]  = {	TB10X_PORT1 + 7, TB10X_PORT1 + 8,
					TB10X_PORT1 + 9, TB10X_PORT1 + 10};
static const unsigned mip1_pins[]  = {	TB10X_PORT1 + 0, TB10X_PORT1 + 1,
					TB10X_PORT1 + 2, TB10X_PORT1 + 3,
					TB10X_PORT1 + 4, TB10X_PORT1 + 5,
					TB10X_PORT1 + 6, TB10X_PORT1 + 7,
					TB10X_PORT1 + 8, TB10X_PORT1 + 9,
					TB10X_PORT1 + 10};

/* Port 2 */
static const unsigned mis2_pins[]  = {	TB10X_PORT2 + 0, TB10X_PORT2 + 1,
					TB10X_PORT2 + 2, TB10X_PORT2 + 3};
static const unsigned gpioc_pins[] = {	TB10X_PORT2 + 4, TB10X_PORT2 + 5,
					TB10X_PORT2 + 6};
static const unsigned mis3_pins[]  = {	TB10X_PORT2 + 7, TB10X_PORT2 + 8,
					TB10X_PORT2 + 9, TB10X_PORT2 + 10};
static const unsigned mip3_pins[]  = {	TB10X_PORT2 + 0, TB10X_PORT2 + 1,
					TB10X_PORT2 + 2, TB10X_PORT2 + 3,
					TB10X_PORT2 + 4, TB10X_PORT2 + 5,
					TB10X_PORT2 + 6, TB10X_PORT2 + 7,
					TB10X_PORT2 + 8, TB10X_PORT2 + 9,
					TB10X_PORT2 + 10};

/* Port 3 */
static const unsigned mis4_pins[]  = {	TB10X_PORT3 + 0, TB10X_PORT3 + 1,
					TB10X_PORT3 + 2, TB10X_PORT3 + 3};
static const unsigned gpioe_pins[] = {	TB10X_PORT3 + 4, TB10X_PORT3 + 5,
					TB10X_PORT3 + 6};
static const unsigned mis5_pins[]  = {	TB10X_PORT3 + 7, TB10X_PORT3 + 8,
					TB10X_PORT3 + 9, TB10X_PORT3 + 10};
static const unsigned mip5_pins[]  = {	TB10X_PORT3 + 0, TB10X_PORT3 + 1,
					TB10X_PORT3 + 2, TB10X_PORT3 + 3,
					TB10X_PORT3 + 4, TB10X_PORT3 + 5,
					TB10X_PORT3 + 6, TB10X_PORT3 + 7,
					TB10X_PORT3 + 8, TB10X_PORT3 + 9,
					TB10X_PORT3 + 10};

/* Port 4 */
static const unsigned mis6_pins[]  = {	TB10X_PORT4 + 0, TB10X_PORT4 + 1,
					TB10X_PORT4 + 2, TB10X_PORT4 + 3};
static const unsigned gpiog_pins[] = {	TB10X_PORT4 + 4, TB10X_PORT4 + 5,
					TB10X_PORT4 + 6};
static const unsigned mis7_pins[]  = {	TB10X_PORT4 + 7, TB10X_PORT4 + 8,
					TB10X_PORT4 + 9, TB10X_PORT4 + 10};
static const unsigned mip7_pins[]  = {	TB10X_PORT4 + 0, TB10X_PORT4 + 1,
					TB10X_PORT4 + 2, TB10X_PORT4 + 3,
					TB10X_PORT4 + 4, TB10X_PORT4 + 5,
					TB10X_PORT4 + 6, TB10X_PORT4 + 7,
					TB10X_PORT4 + 8, TB10X_PORT4 + 9,
					TB10X_PORT4 + 10};

/* Port 6 */
static const unsigned mop_pins[] = {	TB10X_PORT6 + 0, TB10X_PORT6 + 1,
					TB10X_PORT6 + 2, TB10X_PORT6 + 3,
					TB10X_PORT6 + 4, TB10X_PORT6 + 5,
					TB10X_PORT6 + 6, TB10X_PORT6 + 7,
					TB10X_PORT6 + 8, TB10X_PORT6 + 9};
static const unsigned mos0_pins[] = {	TB10X_PORT6 + 0, TB10X_PORT6 + 1,
					TB10X_PORT6 + 2};
static const unsigned mos1_pins[] = {	TB10X_PORT6 + 3, TB10X_PORT6 + 4,
					TB10X_PORT6 + 5};
static const unsigned mos2_pins[] = {	TB10X_PORT6 + 6, TB10X_PORT6 + 7,
					TB10X_PORT6 + 8};
static const unsigned mos3_pins[] = {	TB10X_PORT6 + 9};

/* Port 7 */
static const unsigned uart0_pins[] = {	TB10X_PORT7 + 0, TB10X_PORT7 + 1,
					TB10X_PORT7 + 2, TB10X_PORT7 + 3};
static const unsigned uart1_pins[] = {	TB10X_PORT7 + 4, TB10X_PORT7 + 5,
					TB10X_PORT7 + 6, TB10X_PORT7 + 7};
static const unsigned gpiol_pins[] = {	TB10X_PORT7 + 0, TB10X_PORT7 + 1,
					TB10X_PORT7 + 2, TB10X_PORT7 + 3};
static const unsigned gpiom_pins[] = {	TB10X_PORT7 + 4, TB10X_PORT7 + 5,
					TB10X_PORT7 + 6, TB10X_PORT7 + 7};

/* Port 8 */
static const unsigned spi3_pins[] = {	TB10X_PORT8 + 0, TB10X_PORT8 + 1,
					TB10X_PORT8 + 2, TB10X_PORT8 + 3};
static const unsigned jtag_pins[] = {	TB10X_PORT8 + 0, TB10X_PORT8 + 1,
					TB10X_PORT8 + 2, TB10X_PORT8 + 3};

/* Port 9 */
static const unsigned spi1_pins[] = {	TB10X_PORT9 + 0, TB10X_PORT9 + 1,
					TB10X_PORT9 + 2, TB10X_PORT9 + 3,
					TB10X_PORT9 + 4};
static const unsigned gpion_pins[] = {	TB10X_PORT9 + 0, TB10X_PORT9 + 1,
					TB10X_PORT9 + 2, TB10X_PORT9 + 3,
					TB10X_PORT9 + 4};

/* Port 5 */
static const unsigned gpioj_pins[] = {	TB10X_PORT5 + 0, TB10X_PORT5 + 1,
					TB10X_PORT5 + 2, TB10X_PORT5 + 3,
					TB10X_PORT5 + 4, TB10X_PORT5 + 5,
					TB10X_PORT5 + 6, TB10X_PORT5 + 7,
					TB10X_PORT5 + 8, TB10X_PORT5 + 9,
					TB10X_PORT5 + 10, TB10X_PORT5 + 11,
					TB10X_PORT5 + 12, TB10X_PORT5 + 13,
					TB10X_PORT5 + 14, TB10X_PORT5 + 15,
					TB10X_PORT5 + 16, TB10X_PORT5 + 17,
					TB10X_PORT5 + 18, TB10X_PORT5 + 19,
					TB10X_PORT5 + 20, TB10X_PORT5 + 21,
					TB10X_PORT5 + 22, TB10X_PORT5 + 23,
					TB10X_PORT5 + 24, TB10X_PORT5 + 25,
					TB10X_PORT5 + 26, TB10X_PORT5 + 27,
					TB10X_PORT5 + 28, TB10X_PORT5 + 29,
					TB10X_PORT5 + 30, TB10X_PORT5 + 31};
static const unsigned gpiok_pins[] = {	TB10X_PORT5 + 32, TB10X_PORT5 + 33,
					TB10X_PORT5 + 34, TB10X_PORT5 + 35,
					TB10X_PORT5 + 36, TB10X_PORT5 + 37,
					TB10X_PORT5 + 38, TB10X_PORT5 + 39,
					TB10X_PORT5 + 40, TB10X_PORT5 + 41,
					TB10X_PORT5 + 42, TB10X_PORT5 + 43,
					TB10X_PORT5 + 44, TB10X_PORT5 + 45,
					TB10X_PORT5 + 46, TB10X_PORT5 + 47,
					TB10X_PORT5 + 48, TB10X_PORT5 + 49,
					TB10X_PORT5 + 50, TB10X_PORT5 + 51,
					TB10X_PORT5 + 52, TB10X_PORT5 + 53};
static const unsigned ciplus_pins[] = {	TB10X_PORT5 + 0, TB10X_PORT5 + 1,
					TB10X_PORT5 + 2, TB10X_PORT5 + 3,
					TB10X_PORT5 + 4, TB10X_PORT5 + 5,
					TB10X_PORT5 + 6, TB10X_PORT5 + 7,
					TB10X_PORT5 + 8, TB10X_PORT5 + 9,
					TB10X_PORT5 + 10, TB10X_PORT5 + 11,
					TB10X_PORT5 + 12, TB10X_PORT5 + 13,
					TB10X_PORT5 + 14, TB10X_PORT5 + 15,
					TB10X_PORT5 + 16, TB10X_PORT5 + 17,
					TB10X_PORT5 + 18, TB10X_PORT5 + 19,
					TB10X_PORT5 + 20, TB10X_PORT5 + 21,
					TB10X_PORT5 + 22, TB10X_PORT5 + 23,
					TB10X_PORT5 + 24, TB10X_PORT5 + 25,
					TB10X_PORT5 + 26, TB10X_PORT5 + 27,
					TB10X_PORT5 + 28, TB10X_PORT5 + 29,
					TB10X_PORT5 + 30, TB10X_PORT5 + 31,
					TB10X_PORT5 + 32, TB10X_PORT5 + 33,
					TB10X_PORT5 + 34, TB10X_PORT5 + 35,
					TB10X_PORT5 + 36, TB10X_PORT5 + 37,
					TB10X_PORT5 + 38, TB10X_PORT5 + 39,
					TB10X_PORT5 + 40, TB10X_PORT5 + 41,
					TB10X_PORT5 + 42, TB10X_PORT5 + 43,
					TB10X_PORT5 + 44, TB10X_PORT5 + 45,
					TB10X_PORT5 + 46, TB10X_PORT5 + 47,
					TB10X_PORT5 + 48, TB10X_PORT5 + 49,
					TB10X_PORT5 + 50, TB10X_PORT5 + 51,
					TB10X_PORT5 + 52, TB10X_PORT5 + 53};
static const unsigned mcard_pins[] = {	TB10X_PORT5 + 3, TB10X_PORT5 + 10,
					TB10X_PORT5 + 11, TB10X_PORT5 + 12,
					TB10X_PORT5 + 22, TB10X_PORT5 + 23,
					TB10X_PORT5 + 33, TB10X_PORT5 + 35,
					TB10X_PORT5 + 36, TB10X_PORT5 + 37,
					TB10X_PORT5 + 38, TB10X_PORT5 + 39,
					TB10X_PORT5 + 40, TB10X_PORT5 + 41,
					TB10X_PORT5 + 42, TB10X_PORT5 + 43,
					TB10X_PORT5 + 45, TB10X_PORT5 + 46,
					TB10X_PORT5 + 47, TB10X_PORT5 + 48,
					TB10X_PORT5 + 49, TB10X_PORT5 + 50,
					TB10X_PORT5 + 51, TB10X_PORT5 + 52,
					TB10X_PORT5 + 53};
static const unsigned stc0_pins[] = {	TB10X_PORT5 + 34, TB10X_PORT5 + 35,
					TB10X_PORT5 + 36, TB10X_PORT5 + 37,
					TB10X_PORT5 + 38, TB10X_PORT5 + 39,
					TB10X_PORT5 + 40};
static const unsigned stc1_pins[] = {	TB10X_PORT5 + 25, TB10X_PORT5 + 26,
					TB10X_PORT5 + 27, TB10X_PORT5 + 28,
					TB10X_PORT5 + 29, TB10X_PORT5 + 30,
					TB10X_PORT5 + 44};

/* Unmuxed GPIOs */
static const unsigned gpiob_pins[] = {	TB10X_GPIOS + 0, TB10X_GPIOS + 1};
static const unsigned gpiod_pins[] = {	TB10X_GPIOS + 2, TB10X_GPIOS + 3};
static const unsigned gpiof_pins[] = {	TB10X_GPIOS + 4, TB10X_GPIOS + 5};
static const unsigned gpioh_pins[] = {	TB10X_GPIOS + 6, TB10X_GPIOS + 7};
static const unsigned gpioi_pins[] = {	TB10X_GPIOS + 8, TB10X_GPIOS + 9,
					TB10X_GPIOS + 10, TB10X_GPIOS + 11,
					TB10X_GPIOS + 12, TB10X_GPIOS + 13,
					TB10X_GPIOS + 14, TB10X_GPIOS + 15,
					TB10X_GPIOS + 16, TB10X_GPIOS + 17,
					TB10X_GPIOS + 18, TB10X_GPIOS + 19};

struct tb10x_pinfuncgrp {
	const char *name;
	const unsigned int *pins;
	const unsigned int pincnt;
	const int port;
	const unsigned int mode;
	const int isgpio;
};
#define DEFPINFUNCGRP(NAME, PORT, MODE, ISGPIO) { \
		.name = __stringify(NAME), \
		.pins = NAME##_pins, .pincnt = ARRAY_SIZE(NAME##_pins), \
		.port = (PORT), .mode = (MODE), \
		.isgpio = (ISGPIO), \
	}
static const struct tb10x_pinfuncgrp tb10x_pingroups[] = {
	DEFPINFUNCGRP(mis0,   0, 0, 0),
	DEFPINFUNCGRP(gpioa,  0, 0, 1),
	DEFPINFUNCGRP(mis1,   0, 0, 0),
	DEFPINFUNCGRP(mip1,   0, 1, 0),
	DEFPINFUNCGRP(mis2,   1, 0, 0),
	DEFPINFUNCGRP(gpioc,  1, 0, 1),
	DEFPINFUNCGRP(mis3,   1, 0, 0),
	DEFPINFUNCGRP(mip3,   1, 1, 0),
	DEFPINFUNCGRP(mis4,   2, 0, 0),
	DEFPINFUNCGRP(gpioe,  2, 0, 1),
	DEFPINFUNCGRP(mis5,   2, 0, 0),
	DEFPINFUNCGRP(mip5,   2, 1, 0),
	DEFPINFUNCGRP(mis6,   3, 0, 0),
	DEFPINFUNCGRP(gpiog,  3, 0, 1),
	DEFPINFUNCGRP(mis7,   3, 0, 0),
	DEFPINFUNCGRP(mip7,   3, 1, 0),
	DEFPINFUNCGRP(gpioj,  4, 0, 1),
	DEFPINFUNCGRP(gpiok,  4, 0, 1),
	DEFPINFUNCGRP(ciplus, 4, 1, 0),
	DEFPINFUNCGRP(mcard,  4, 2, 0),
	DEFPINFUNCGRP(stc0,   4, 3, 0),
	DEFPINFUNCGRP(stc1,   4, 3, 0),
	DEFPINFUNCGRP(mop,    5, 0, 0),
	DEFPINFUNCGRP(mos0,   5, 1, 0),
	DEFPINFUNCGRP(mos1,   5, 1, 0),
	DEFPINFUNCGRP(mos2,   5, 1, 0),
	DEFPINFUNCGRP(mos3,   5, 1, 0),
	DEFPINFUNCGRP(uart0,  6, 0, 0),
	DEFPINFUNCGRP(uart1,  6, 0, 0),
	DEFPINFUNCGRP(gpiol,  6, 1, 1),
	DEFPINFUNCGRP(gpiom,  6, 1, 1),
	DEFPINFUNCGRP(spi3,   7, 0, 0),
	DEFPINFUNCGRP(jtag,   7, 1, 0),
	DEFPINFUNCGRP(spi1,   8, 0, 0),
	DEFPINFUNCGRP(gpion,  8, 1, 1),
	DEFPINFUNCGRP(gpiob, -1, 0, 1),
	DEFPINFUNCGRP(gpiod, -1, 0, 1),
	DEFPINFUNCGRP(gpiof, -1, 0, 1),
	DEFPINFUNCGRP(gpioh, -1, 0, 1),
	DEFPINFUNCGRP(gpioi, -1, 0, 1),
};
#undef DEFPINFUNCGRP

struct tb10x_of_pinfunc {
	const char *name;
	const char *group;
};

#define TB10X_PORTS (9)

/**
 * struct tb10x_port - state of an I/O port
 * @mode: Node this port is currently in.
 * @count: Number of enabled functions which require this port to be
 *         configured in @mode.
 */
struct tb10x_port {
	unsigned int mode;
	unsigned int count;
};

/**
 * struct tb10x_pinctrl - TB10x pin controller internal state
 * @pctl: pointer to the pinctrl_dev structure of this pin controller.
 * @base: register set base address.
 * @pingroups: pointer to an array of the pin groups this driver manages.
 * @pinfuncgrpcnt: number of pingroups in @pingroups.
 * @pinfuncnt: number of pin functions in @pinfuncs.
 * @mutex: mutex for exclusive access to a pin controller's state.
 * @ports: current state of each port.
 * @gpios: Indicates if a given pin is currently used as GPIO (1) or not (0).
 * @pinfuncs: flexible array of pin functions this driver manages.
 */
struct tb10x_pinctrl {
	struct pinctrl_dev *pctl;
	void *base;
	const struct tb10x_pinfuncgrp *pingroups;
	unsigned int pinfuncgrpcnt;
	unsigned int pinfuncnt;
	struct mutex mutex;
	struct tb10x_port ports[TB10X_PORTS];
	DECLARE_BITMAP(gpios, MAX_PIN + 1);
	struct tb10x_of_pinfunc pinfuncs[];
};

static inline void tb10x_pinctrl_set_config(struct tb10x_pinctrl *state,
				unsigned int port, unsigned int mode)
{
	u32 pcfg;

	if (state->ports[port].count)
		return;

	state->ports[port].mode = mode;

	pcfg = ioread32(state->base) & ~(PCFG_PORT_MASK(port));
	pcfg |= (mode << (PCFG_PORT_BITWIDTH * port)) & PCFG_PORT_MASK(port);
	iowrite32(pcfg, state->base);
}

static inline unsigned int tb10x_pinctrl_get_config(
				struct tb10x_pinctrl *state,
				unsigned int port)
{
	return (ioread32(state->base) & PCFG_PORT_MASK(port))
		>> (PCFG_PORT_BITWIDTH * port);
}

static int tb10x_get_groups_count(struct pinctrl_dev *pctl)
{
	struct tb10x_pinctrl *state = pinctrl_dev_get_drvdata(pctl);
	return state->pinfuncgrpcnt;
}

static const char *tb10x_get_group_name(struct pinctrl_dev *pctl, unsigned n)
{
	struct tb10x_pinctrl *state = pinctrl_dev_get_drvdata(pctl);
	return state->pingroups[n].name;
}

static int tb10x_get_group_pins(struct pinctrl_dev *pctl, unsigned n,
				unsigned const **pins,
				unsigned * const num_pins)
{
	struct tb10x_pinctrl *state = pinctrl_dev_get_drvdata(pctl);

	*pins = state->pingroups[n].pins;
	*num_pins = state->pingroups[n].pincnt;

	return 0;
}

static int tb10x_dt_node_to_map(struct pinctrl_dev *pctl,
				struct device_node *np_config,
				struct pinctrl_map **map, unsigned *num_maps)
{
	const char *string;
	unsigned reserved_maps = 0;
	int ret = 0;

	if (of_property_read_string(np_config, "abilis,function", &string)) {
		pr_err("%pOF: No abilis,function property in device tree.\n",
			np_config);
		return -EINVAL;
	}

	*map = NULL;
	*num_maps = 0;

	ret = pinctrl_utils_reserve_map(pctl, map, &reserved_maps,
					num_maps, 1);
	if (ret)
		goto out;

	ret = pinctrl_utils_add_map_mux(pctl, map, &reserved_maps,
					num_maps, string, np_config->name);

out:
	return ret;
}

static const struct pinctrl_ops tb10x_pinctrl_ops = {
	.get_groups_count = tb10x_get_groups_count,
	.get_group_name   = tb10x_get_group_name,
	.get_group_pins   = tb10x_get_group_pins,
	.dt_node_to_map   = tb10x_dt_node_to_map,
	.dt_free_map      = pinctrl_utils_free_map,
};

static int tb10x_get_functions_count(struct pinctrl_dev *pctl)
{
	struct tb10x_pinctrl *state = pinctrl_dev_get_drvdata(pctl);
	return state->pinfuncnt;
}

static const char *tb10x_get_function_name(struct pinctrl_dev *pctl,
					unsigned n)
{
	struct tb10x_pinctrl *state = pinctrl_dev_get_drvdata(pctl);
	return state->pinfuncs[n].name;
}

static int tb10x_get_function_groups(struct pinctrl_dev *pctl,
				unsigned n, const char * const **groups,
				unsigned * const num_groups)
{
	struct tb10x_pinctrl *state = pinctrl_dev_get_drvdata(pctl);

	*groups = &state->pinfuncs[n].group;
	*num_groups = 1;

	return 0;
}

static int tb10x_gpio_request_enable(struct pinctrl_dev *pctl,
					struct pinctrl_gpio_range *range,
					unsigned pin)
{
	struct tb10x_pinctrl *state = pinctrl_dev_get_drvdata(pctl);
	int muxport = -1;
	int muxmode = -1;
	int i;

	mutex_lock(&state->mutex);

	/*
	 * Figure out to which port the requested GPIO belongs and how to
	 * configure that port.
	 * This loop also checks for pin conflicts between GPIOs and other
	 * functions.
	 */
	for (i = 0; i < state->pinfuncgrpcnt; i++) {
		const struct tb10x_pinfuncgrp *pfg = &state->pingroups[i];
		unsigned int mode = pfg->mode;
		int j, port = pfg->port;

		/*
		 * Skip pin groups which are always mapped and don't need
		 * to be configured.
		 */
		if (port < 0)
			continue;

		for (j = 0; j < pfg->pincnt; j++) {
			if (pin == pfg->pins[j]) {
				if (pfg->isgpio) {
					/*
					 * Remember the GPIO-only setting of
					 * the port this pin belongs to.
					 */
					muxport = port;
					muxmode = mode;
				} else if (state->ports[port].count
					&& (state->ports[port].mode == mode)) {
					/*
					 * Error: The requested pin is already
					 * used for something else.
					 */
					mutex_unlock(&state->mutex);
					return -EBUSY;
				}
				break;
			}
		}
	}

	/*
	 * If we haven't returned an error at this point, the GPIO pin is not
	 * used by another function and the GPIO request can be granted:
	 * Register pin as being used as GPIO so we don't allocate it to
	 * another function later.
	 */
	set_bit(pin, state->gpios);

	/*
	 * Potential conflicts between GPIOs and pin functions were caught
	 * earlier in this function and tb10x_pinctrl_set_config will do the
	 * Right Thing, either configure the port in GPIO only mode or leave
	 * another mode compatible with this GPIO request untouched.
	 */
	if (muxport >= 0)
		tb10x_pinctrl_set_config(state, muxport, muxmode);

	mutex_unlock(&state->mutex);

	return 0;
}

static void tb10x_gpio_disable_free(struct pinctrl_dev *pctl,
					struct pinctrl_gpio_range *range,
					unsigned pin)
{
	struct tb10x_pinctrl *state = pinctrl_dev_get_drvdata(pctl);

	mutex_lock(&state->mutex);

	clear_bit(pin, state->gpios);

	mutex_unlock(&state->mutex);
}

static int tb10x_pctl_set_mux(struct pinctrl_dev *pctl,
			unsigned func_selector, unsigned group_selector)
{
	struct tb10x_pinctrl *state = pinctrl_dev_get_drvdata(pctl);
	const struct tb10x_pinfuncgrp *grp = &state->pingroups[group_selector];
	int i;

	if (grp->port < 0)
		return 0;

	mutex_lock(&state->mutex);

	/*
	 * Check if the requested function is compatible with previously
	 * requested functions.
	 */
	if (state->ports[grp->port].count
			&& (state->ports[grp->port].mode != grp->mode)) {
		mutex_unlock(&state->mutex);
		return -EBUSY;
	}

	/*
	 * Check if the requested function is compatible with previously
	 * requested GPIOs.
	 */
	for (i = 0; i < grp->pincnt; i++)
		if (test_bit(grp->pins[i], state->gpios)) {
			mutex_unlock(&state->mutex);
			return -EBUSY;
		}

	tb10x_pinctrl_set_config(state, grp->port, grp->mode);

	state->ports[grp->port].count++;

	mutex_unlock(&state->mutex);

	return 0;
}

static const struct pinmux_ops tb10x_pinmux_ops = {
	.get_functions_count = tb10x_get_functions_count,
	.get_function_name = tb10x_get_function_name,
	.get_function_groups = tb10x_get_function_groups,
	.gpio_request_enable = tb10x_gpio_request_enable,
	.gpio_disable_free = tb10x_gpio_disable_free,
	.set_mux = tb10x_pctl_set_mux,
};

static struct pinctrl_desc tb10x_pindesc = {
	.name = "TB10x",
	.pins = tb10x_pins,
	.npins = ARRAY_SIZE(tb10x_pins),
	.owner = THIS_MODULE,
	.pctlops = &tb10x_pinctrl_ops,
	.pmxops  = &tb10x_pinmux_ops,
};

static int tb10x_pinctrl_probe(struct platform_device *pdev)
{
	int ret = -EINVAL;
	struct device *dev = &pdev->dev;
	struct device_node *of_node = dev->of_node;
	struct device_node *child;
	struct tb10x_pinctrl *state;
	int i;

	if (!of_node) {
		dev_err(dev, "No device tree node found.\n");
		return -EINVAL;
	}

	state = devm_kzalloc(dev, struct_size(state, pinfuncs,
					      of_get_child_count(of_node)),
			     GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	platform_set_drvdata(pdev, state);
	mutex_init(&state->mutex);

	state->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(state->base)) {
		ret = PTR_ERR(state->base);
		goto fail;
	}

	state->pingroups = tb10x_pingroups;
	state->pinfuncgrpcnt = ARRAY_SIZE(tb10x_pingroups);

	for (i = 0; i < TB10X_PORTS; i++)
		state->ports[i].mode = tb10x_pinctrl_get_config(state, i);

	for_each_child_of_node(of_node, child) {
		const char *name;

		if (!of_property_read_string(child, "abilis,function",
						&name)) {
			state->pinfuncs[state->pinfuncnt].name = child->name;
			state->pinfuncs[state->pinfuncnt].group = name;
			state->pinfuncnt++;
		}
	}

	state->pctl = devm_pinctrl_register(dev, &tb10x_pindesc, state);
	if (IS_ERR(state->pctl)) {
		dev_err(dev, "could not register TB10x pin driver\n");
		ret = PTR_ERR(state->pctl);
		goto fail;
	}

	return 0;

fail:
	mutex_destroy(&state->mutex);
	return ret;
}

static void tb10x_pinctrl_remove(struct platform_device *pdev)
{
	struct tb10x_pinctrl *state = platform_get_drvdata(pdev);

	mutex_destroy(&state->mutex);
}


static const struct of_device_id tb10x_pinctrl_dt_ids[] = {
	{ .compatible = "abilis,tb10x-iomux" },
	{ }
};
MODULE_DEVICE_TABLE(of, tb10x_pinctrl_dt_ids);

static struct platform_driver tb10x_pinctrl_pdrv = {
	.probe   = tb10x_pinctrl_probe,
	.remove_new = tb10x_pinctrl_remove,
	.driver  = {
		.name  = "tb10x_pinctrl",
		.of_match_table = of_match_ptr(tb10x_pinctrl_dt_ids),
	}
};

module_platform_driver(tb10x_pinctrl_pdrv);

MODULE_AUTHOR("Christian Ruppert <christian.ruppert@abilis.com>");
MODULE_DESCRIPTION("Abilis Systems TB10x pinctrl driver");
MODULE_LICENSE("GPL");
