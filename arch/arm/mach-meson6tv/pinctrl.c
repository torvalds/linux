/*
 * arch/arm/mach-meson6tv/pinctrl.c
 *
 * Amlogic pin controller driver
 *
 * Copyright (C) 2013 Amlogic, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>

#include <plat/io.h>
#include <mach/gpio.h>
#include <mach/am_regs.h>

#include <linux/amlogic/pinctrl-amlogic.h>
#include <linux/amlogic/aml_gpio_consumer.h>

DEFINE_MUTEX(spi_nand_mutex);

unsigned int p_pull_up_addr[] = {
	P_PAD_PULL_UP_REG0,
	P_PAD_PULL_UP_REG1,
	P_PAD_PULL_UP_REG2,
	P_PAD_PULL_UP_REG3,
	P_PAD_PULL_UP_REG4,
	P_PAD_PULL_UP_REG5,
	P_PAD_PULL_UP_REG6,
};

unsigned int p_pin_mux_reg_addr[] = {
	P_PERIPHS_PIN_MUX_0,
	P_PERIPHS_PIN_MUX_1,
	P_PERIPHS_PIN_MUX_2,
	P_PERIPHS_PIN_MUX_3,
	P_PERIPHS_PIN_MUX_4,
	P_PERIPHS_PIN_MUX_5,
	P_PERIPHS_PIN_MUX_6,
	P_PERIPHS_PIN_MUX_7,
	P_PERIPHS_PIN_MUX_8,
	P_PERIPHS_PIN_MUX_9,
	P_AO_RTI_PIN_MUX_REG,
};

static const struct pinctrl_pin_desc m6tv_pads[] = {

	PINCTRL_PIN(GPIOZ_0,	"GPIOZ_0"),
	PINCTRL_PIN(GPIOZ_1,	"GPIOZ_1"),
	PINCTRL_PIN(GPIOZ_2,	"GPIOZ_2"),
	PINCTRL_PIN(GPIOZ_3,	"GPIOZ_3"),
	PINCTRL_PIN(GPIOZ_4,	"GPIOZ_4"),
	PINCTRL_PIN(GPIOZ_5,	"GPIOZ_5"),
	PINCTRL_PIN(GPIOZ_6,	"GPIOZ_6"),
	PINCTRL_PIN(GPIOZ_7,	"GPIOZ_7"),
	PINCTRL_PIN(GPIOZ_8,	"GPIOZ_8"),
	PINCTRL_PIN(GPIOZ_9,	"GPIOZ_9"),
	PINCTRL_PIN(GPIOZ_10,	"GPIOZ_10"),
	PINCTRL_PIN(GPIOZ_11,	"GPIOZ_11"),
	PINCTRL_PIN(GPIOZ_12,	"GPIOZ_12"),
	PINCTRL_PIN(GPIOZ_13,	"GPIOZ_13"),
	PINCTRL_PIN(GPIOZ_14,	"GPIOZ_14"),
	PINCTRL_PIN(GPIOZ_15,	"GPIOZ_15"),
	PINCTRL_PIN(GPIOZ_16,	"GPIOZ_16"),
	PINCTRL_PIN(GPIOZ_17,	"GPIOZ_17"),
	PINCTRL_PIN(GPIOZ_18,	"GPIOZ_18"),
	PINCTRL_PIN(GPIOZ_19,	"GPIOZ_19"),

	PINCTRL_PIN(GPIOY_0,	"GPIOY_0"),
	PINCTRL_PIN(GPIOY_1,	"GPIOY_1"),
	PINCTRL_PIN(GPIOY_2,	"GPIOY_2"),
	PINCTRL_PIN(GPIOY_3,	"GPIOY_3"),
	PINCTRL_PIN(GPIOY_4,	"GPIOY_4"),
	PINCTRL_PIN(GPIOY_5,	"GPIOY_5"),
	PINCTRL_PIN(GPIOY_6,	"GPIOY_6"),
	PINCTRL_PIN(GPIOY_7,	"GPIOY_7"),
	PINCTRL_PIN(GPIOY_8,	"GPIOY_8"),
	PINCTRL_PIN(GPIOY_9,	"GPIOY_9"),
	PINCTRL_PIN(GPIOY_10,	"GPIOY_10"),
	PINCTRL_PIN(GPIOY_11,	"GPIOY_11"),
	PINCTRL_PIN(GPIOY_12,	"GPIOY_12"),
	PINCTRL_PIN(GPIOY_13,	"GPIOY_13"),
	PINCTRL_PIN(GPIOY_14,	"GPIOY_14"),
	PINCTRL_PIN(GPIOY_15,	"GPIOY_15"),
	PINCTRL_PIN(GPIOY_16,	"GPIOY_16"),
	PINCTRL_PIN(GPIOY_17,	"GPIOY_17"),
	PINCTRL_PIN(GPIOY_18,	"GPIOY_18"),
	PINCTRL_PIN(GPIOY_19,	"GPIOY_19"),
	PINCTRL_PIN(GPIOY_20,	"GPIOY_20"),
	PINCTRL_PIN(GPIOY_21,	"GPIOY_21"),
	PINCTRL_PIN(GPIOY_22,	"GPIOY_22"),
	PINCTRL_PIN(GPIOY_23,	"GPIOY_23"),
	PINCTRL_PIN(GPIOY_24,	"GPIOY_24"),
	PINCTRL_PIN(GPIOY_25,	"GPIOY_25"),
	PINCTRL_PIN(GPIOY_26,	"GPIOY_26"),
	PINCTRL_PIN(GPIOY_27,	"GPIOY_27"),

	PINCTRL_PIN(GPIOW_0,	"GPIOW_0"),
	PINCTRL_PIN(GPIOW_1,	"GPIOW_1"),
	PINCTRL_PIN(GPIOW_2,	"GPIOW_2"),
	PINCTRL_PIN(GPIOW_3,	"GPIOW_3"),
	PINCTRL_PIN(GPIOW_4,	"GPIOW_4"),
	PINCTRL_PIN(GPIOW_5,	"GPIOW_5"),
	PINCTRL_PIN(GPIOW_6,	"GPIOW_6"),
	PINCTRL_PIN(GPIOW_7,	"GPIOW_7"),
	PINCTRL_PIN(GPIOW_8,	"GPIOW_8"),
	PINCTRL_PIN(GPIOW_9,	"GPIOW_9"),
	PINCTRL_PIN(GPIOW_10,	"GPIOW_10"),
	PINCTRL_PIN(GPIOW_11,	"GPIOW_11"),
	PINCTRL_PIN(GPIOW_12,	"GPIOW_12"),
	PINCTRL_PIN(GPIOW_13,	"GPIOW_13"),
	PINCTRL_PIN(GPIOW_14,	"GPIOW_14"),
	PINCTRL_PIN(GPIOW_15,	"GPIOW_15"),
	PINCTRL_PIN(GPIOW_16,	"GPIOW_16"),
	PINCTRL_PIN(GPIOW_17,	"GPIOW_17"),
	PINCTRL_PIN(GPIOW_18,	"GPIOW_18"),
	PINCTRL_PIN(GPIOW_19,	"GPIOW_19"),

	PINCTRL_PIN(GPIOX_0,	"GPIOX_0"),
	PINCTRL_PIN(GPIOX_1,	"GPIOX_1"),
	PINCTRL_PIN(GPIOX_2,	"GPIOX_2"),
	PINCTRL_PIN(GPIOX_3,	"GPIOX_3"),
	PINCTRL_PIN(GPIOX_4,	"GPIOX_4"),
	PINCTRL_PIN(GPIOX_5,	"GPIOX_5"),
	PINCTRL_PIN(GPIOX_6,	"GPIOX_6"),
	PINCTRL_PIN(GPIOX_7,	"GPIOX_7"),
	PINCTRL_PIN(GPIOX_8,	"GPIOX_8"),
	PINCTRL_PIN(GPIOX_9,	"GPIOX_9"),
	PINCTRL_PIN(GPIOX_10,	"GPIOX_10"),
	PINCTRL_PIN(GPIOX_11,	"GPIOX_11"),
	PINCTRL_PIN(GPIOX_12,	"GPIOX_12"),

	PINCTRL_PIN(BOOT_0,	"BOOT_0"),
	PINCTRL_PIN(BOOT_1,	"BOOT_1"),
	PINCTRL_PIN(BOOT_2,	"BOOT_2"),
	PINCTRL_PIN(BOOT_3,	"BOOT_3"),
	PINCTRL_PIN(BOOT_4,	"BOOT_4"),
	PINCTRL_PIN(BOOT_5,	"BOOT_5"),
	PINCTRL_PIN(BOOT_6,	"BOOT_6"),
	PINCTRL_PIN(BOOT_7,	"BOOT_7"),
	PINCTRL_PIN(BOOT_8,	"BOOT_8"),
	PINCTRL_PIN(BOOT_9,	"BOOT_9"),
	PINCTRL_PIN(BOOT_10,	"BOOT_10"),
	PINCTRL_PIN(BOOT_11,	"BOOT_11"),
	PINCTRL_PIN(BOOT_12,	"BOOT_12"),
	PINCTRL_PIN(BOOT_13,	"BOOT_13"),
	PINCTRL_PIN(BOOT_14,	"BOOT_14"),
	PINCTRL_PIN(BOOT_15,	"BOOT_15"),
	PINCTRL_PIN(BOOT_16,	"BOOT_16"),
	PINCTRL_PIN(BOOT_17,	"BOOT_17"),

	PINCTRL_PIN(GPIOP_0,	"GPIOP_0"),
	PINCTRL_PIN(GPIOP_1,	"GPIOP_1"),
	PINCTRL_PIN(GPIOP_2,	"GPIOP_2"),
	PINCTRL_PIN(GPIOP_3,	"GPIOP_3"),
	PINCTRL_PIN(GPIOP_4,	"GPIOP_4"),
	PINCTRL_PIN(GPIOP_5,	"GPIOP_5"),
	PINCTRL_PIN(GPIOP_6,	"GPIOP_6"),
	PINCTRL_PIN(GPIOP_7,	"GPIOP_7"),

	PINCTRL_PIN(CARD_0,	"CARD_0"),
	PINCTRL_PIN(CARD_1,	"CARD_1"),
	PINCTRL_PIN(CARD_2,	"CARD_2"),
	PINCTRL_PIN(CARD_3,	"CARD_3"),
	PINCTRL_PIN(CARD_4,	"CARD_4"),
	PINCTRL_PIN(CARD_5,	"CARD_5"),
	PINCTRL_PIN(CARD_6,	"CARD_6"),
	PINCTRL_PIN(CARD_7,	"CARD_7"),
	PINCTRL_PIN(CARD_8,	"CARD_8"),

	PINCTRL_PIN(GPIOB_0,	"GPIOB_0"),
	PINCTRL_PIN(GPIOB_1,	"GPIOB_1"),
	PINCTRL_PIN(GPIOB_2,	"GPIOB_2"),
	PINCTRL_PIN(GPIOB_3,	"GPIOB_3"),
	PINCTRL_PIN(GPIOB_4,	"GPIOB_4"),
	PINCTRL_PIN(GPIOB_5,	"GPIOB_5"),
	PINCTRL_PIN(GPIOB_6,	"GPIOB_6"),
	PINCTRL_PIN(GPIOB_7,	"GPIOB_7"),
	PINCTRL_PIN(GPIOB_8,	"GPIOB_8"),
	PINCTRL_PIN(GPIOB_9,	"GPIOB_9"),
	PINCTRL_PIN(GPIOB_10,	"GPIOB_10"),
	PINCTRL_PIN(GPIOB_11,	"GPIOB_11"),
	PINCTRL_PIN(GPIOB_12,	"GPIOB_12"),
	PINCTRL_PIN(GPIOB_13,	"GPIOB_13"),
	PINCTRL_PIN(GPIOB_14,	"GPIOB_14"),
	PINCTRL_PIN(GPIOB_15,	"GPIOB_15"),
	PINCTRL_PIN(GPIOB_16,	"GPIOB_16"),
	PINCTRL_PIN(GPIOB_17,	"GPIOB_17"),
	PINCTRL_PIN(GPIOB_18,	"GPIOB_18"),
	PINCTRL_PIN(GPIOB_19,	"GPIOB_19"),
	PINCTRL_PIN(GPIOB_20,	"GPIOB_20"),
	PINCTRL_PIN(GPIOB_21,	"GPIOB_21"),
	PINCTRL_PIN(GPIOB_22,	"GPIOB_22"),
	PINCTRL_PIN(GPIOB_23,	"GPIOB_23"),

	PINCTRL_PIN(GPIOA_0,	"GPIOA_0"),
	PINCTRL_PIN(GPIOA_1,	"GPIOA_1"),
	PINCTRL_PIN(GPIOA_2,	"GPIOA_2"),
	PINCTRL_PIN(GPIOA_3,	"GPIOA_3"),
	PINCTRL_PIN(GPIOA_4,	"GPIOA_4"),
	PINCTRL_PIN(GPIOA_5,	"GPIOA_5"),
	PINCTRL_PIN(GPIOA_6,	"GPIOA_6"),
	PINCTRL_PIN(GPIOA_7,	"GPIOA_7"),
	PINCTRL_PIN(GPIOA_8,	"GPIOA_8"),
	PINCTRL_PIN(GPIOA_9,	"GPIOA_9"),
	PINCTRL_PIN(GPIOA_10,	"GPIOA_10"),
	PINCTRL_PIN(GPIOA_11,	"GPIOA_11"),
	PINCTRL_PIN(GPIOA_12,	"GPIOA_12"),
	PINCTRL_PIN(GPIOA_13,	"GPIOA_13"),
	PINCTRL_PIN(GPIOA_14,	"GPIOA_14"),
	PINCTRL_PIN(GPIOA_15,	"GPIOA_15"),
	PINCTRL_PIN(GPIOA_16,	"GPIOA_16"),
	PINCTRL_PIN(GPIOA_17,	"GPIOA_17"),
	PINCTRL_PIN(GPIOA_18,	"GPIOA_18"),
	PINCTRL_PIN(GPIOA_19,	"GPIOA_19"),
	PINCTRL_PIN(GPIOA_20,	"GPIOA_20"),
	PINCTRL_PIN(GPIOA_21,	"GPIOA_21"),
	PINCTRL_PIN(GPIOA_22,	"GPIOA_22"),
	PINCTRL_PIN(GPIOA_23,	"GPIOA_23"),
	PINCTRL_PIN(GPIOA_24,	"GPIOA_24"),
	PINCTRL_PIN(GPIOA_25,	"GPIOA_25"),
	PINCTRL_PIN(GPIOA_26,	"GPIOA_26"),
	PINCTRL_PIN(GPIOA_27,	"GPIOA_27"),
	PINCTRL_PIN(GPIOA_28,	"GPIOA_28"),
	PINCTRL_PIN(GPIOA_29,	"GPIOA_29"),

	PINCTRL_PIN(GPIOAO_0,	"GPIOAO_0"),
	PINCTRL_PIN(GPIOAO_1,	"GPIOAO_1"),
	PINCTRL_PIN(GPIOAO_2,	"GPIOAO_2"),
	PINCTRL_PIN(GPIOAO_3,	"GPIOAO_3"),
	PINCTRL_PIN(GPIOAO_4,	"GPIOAO_4"),
	PINCTRL_PIN(GPIOAO_5,	"GPIOAO_5"),
	PINCTRL_PIN(GPIOAO_6,	"GPIOAO_6"),
	PINCTRL_PIN(GPIOAO_7,	"GPIOAO_7"),
	PINCTRL_PIN(GPIOAO_8,	"GPIOAO_8"),
	PINCTRL_PIN(GPIOAO_9,	"GPIOAO_9"),
	PINCTRL_PIN(GPIOAO_10,	"GPIOAO_10"),
	PINCTRL_PIN(GPIOAO_11,	"GPIOAO_11"),
};

#define PULL_UP_MAP(reg, bit)	((reg << 5) | bit)
#define PULL_UP_REG(val)	(val >> 5)
#define PULL_UP_BIT(val)	(val & 0x1F)
#define PULL_UP_NONE		(0xFFFF)

static unsigned int m6tv_pull_up[] = {
	[BOOT_0	]	=	PULL_UP_MAP(3, 0),
	[BOOT_1	]	=	PULL_UP_MAP(3, 1),
	[BOOT_2	]	=	PULL_UP_MAP(3, 2),
	[BOOT_3	]	=	PULL_UP_MAP(3, 3),
	[BOOT_4	]	=	PULL_UP_MAP(3, 4),
	[BOOT_5	]	=	PULL_UP_MAP(3, 5),
	[BOOT_6	]	=	PULL_UP_MAP(3, 6),
	[BOOT_7	]	=	PULL_UP_MAP(3, 7),
	[BOOT_8	]	=	PULL_UP_MAP(3, 8),
	[BOOT_9	]	=	PULL_UP_MAP(3, 9),
	[BOOT_10]	=	PULL_UP_MAP(3, 10),
	[BOOT_11]	=	PULL_UP_MAP(3, 11),
	[BOOT_12]	=	PULL_UP_MAP(3, 12),
	[BOOT_13]	=	PULL_UP_MAP(3, 13),
	[BOOT_14]	=	PULL_UP_MAP(3, 14),
	[BOOT_15]	=	PULL_UP_MAP(3, 15),
	[BOOT_16]	=	PULL_UP_MAP(3, 16),
	[BOOT_17]	=	PULL_UP_MAP(3, 17),

	[CARD_0	]	=	PULL_UP_MAP(3, 20),
	[CARD_1	]	=	PULL_UP_MAP(3, 21),
	[CARD_2	]	=	PULL_UP_MAP(3, 22),
	[CARD_3	]	=	PULL_UP_MAP(3, 23),
	[CARD_4	]	=	PULL_UP_MAP(3, 24),
	[CARD_5	]	=	PULL_UP_MAP(3, 25),
	[CARD_6	]	=	PULL_UP_MAP(3, 26),
	[CARD_7	]	=	PULL_UP_MAP(3, 27),
	[CARD_8	]	=	PULL_UP_MAP(3, 28),

	[GPIOA_0]	=	PULL_UP_MAP(0, 0),
	[GPIOA_1]	=	PULL_UP_MAP(0, 1),
	[GPIOA_2]	=	PULL_UP_MAP(0, 2),
	[GPIOA_3]	=	PULL_UP_MAP(0, 3),
	[GPIOA_4]	=	PULL_UP_MAP(0, 4),
	[GPIOA_5]	=	PULL_UP_MAP(0, 5),
	[GPIOA_6]	=	PULL_UP_MAP(0, 6),
	[GPIOA_7]	=	PULL_UP_MAP(0, 7),
	[GPIOA_8]	=	PULL_UP_MAP(0, 8),
	[GPIOA_9]	=	PULL_UP_MAP(0, 9),
	[GPIOA_10]	=	PULL_UP_MAP(0, 10),
	[GPIOA_11]	=	PULL_UP_MAP(0, 11),
	[GPIOA_12]	=	PULL_UP_MAP(0, 12),
	[GPIOA_13]	=	PULL_UP_MAP(0, 13),
	[GPIOA_14]	=	PULL_UP_MAP(0, 14),
	[GPIOA_15]	=	PULL_UP_MAP(0, 15),
	[GPIOA_16]	=	PULL_UP_MAP(0, 16),
	[GPIOA_17]	=	PULL_UP_MAP(0, 17),
	[GPIOA_18]	=	PULL_UP_MAP(0, 18),
	[GPIOA_19]	=	PULL_UP_MAP(0, 19),
	[GPIOA_20]	=	PULL_UP_MAP(0, 20),
	[GPIOA_21]	=	PULL_UP_MAP(0, 21),
	[GPIOA_22]	=	PULL_UP_MAP(0, 22),
	[GPIOA_23]	=	PULL_UP_MAP(0, 23),
	[GPIOA_24]	=	PULL_UP_MAP(0, 24),
	[GPIOA_25]	=	PULL_UP_MAP(0, 25),
	[GPIOA_26]	=	PULL_UP_MAP(0, 26),
	[GPIOA_27]	=	PULL_UP_MAP(0, 27),
	[GPIOA_28]	=	PULL_UP_MAP(0, 28),
	[GPIOA_29]	=	PULL_UP_MAP(0, 29),

	[GPIOAO_0]	=	PULL_UP_NONE,
	[GPIOAO_1]	=	PULL_UP_NONE,
	[GPIOAO_2]	=	PULL_UP_NONE,
	[GPIOAO_3]	=	PULL_UP_NONE,
	[GPIOAO_4]	=	PULL_UP_NONE,
	[GPIOAO_5]	=	PULL_UP_NONE,
	[GPIOAO_6]	=	PULL_UP_NONE,
	[GPIOAO_7]	=	PULL_UP_NONE,
	[GPIOAO_8]	=	PULL_UP_NONE,
	[GPIOAO_9]	=	PULL_UP_NONE,
	[GPIOAO_10]	=	PULL_UP_NONE,
	[GPIOAO_11]	=	PULL_UP_NONE,

	[GPIOB_0]	=	PULL_UP_MAP(1, 0),
	[GPIOB_1]	=	PULL_UP_MAP(1, 1),
	[GPIOB_2]	=	PULL_UP_MAP(1, 2),
	[GPIOB_3]	=	PULL_UP_MAP(1, 3),
	[GPIOB_4]	=	PULL_UP_MAP(1, 4),
	[GPIOB_5]	=	PULL_UP_MAP(1, 5),
	[GPIOB_6]	=	PULL_UP_MAP(1, 6),
	[GPIOB_7]	=	PULL_UP_MAP(1, 7),
	[GPIOB_8]	=	PULL_UP_MAP(1, 8),
	[GPIOB_9]	=	PULL_UP_MAP(1, 9),
	[GPIOB_10]	=	PULL_UP_MAP(1, 10),
	[GPIOB_11]	=	PULL_UP_MAP(1, 11),
	[GPIOB_12]	=	PULL_UP_MAP(1, 12),
	[GPIOB_13]	=	PULL_UP_MAP(1, 13),
	[GPIOB_14]	=	PULL_UP_MAP(1, 14),
	[GPIOB_15]	=	PULL_UP_MAP(1, 15),
	[GPIOB_16]	=	PULL_UP_MAP(1, 16),
	[GPIOB_17]	=	PULL_UP_MAP(1, 17),
	[GPIOB_18]	=	PULL_UP_MAP(1, 18),
	[GPIOB_19]	=	PULL_UP_MAP(1, 19),
	[GPIOB_20]	=	PULL_UP_MAP(1, 20),
	[GPIOB_21]	=	PULL_UP_MAP(1, 21),
	[GPIOB_22]	=	PULL_UP_MAP(1, 22),
	[GPIOB_23]	=	PULL_UP_MAP(1, 23),

	[GPIOP_0]	=	PULL_UP_MAP(1, 24),
	[GPIOP_1]	=	PULL_UP_MAP(1, 25),
	[GPIOP_2]	=	PULL_UP_MAP(1, 26),
	[GPIOP_3]	=	PULL_UP_MAP(1, 27),
	[GPIOP_4]	=	PULL_UP_MAP(1, 28),
	[GPIOP_5]	=	PULL_UP_MAP(1, 29),
	[GPIOP_6]	=	PULL_UP_MAP(1, 30),
	[GPIOP_7]	=	PULL_UP_MAP(1, 31),

	[GPIOW_0]	=	PULL_UP_MAP(2, 0),
	[GPIOW_1]	=	PULL_UP_MAP(2, 1),
	[GPIOW_2]	=	PULL_UP_MAP(2, 2),
	[GPIOW_3]	=	PULL_UP_MAP(2, 3),
	[GPIOW_4]	=	PULL_UP_MAP(2, 4),
	[GPIOW_5]	=	PULL_UP_MAP(2, 5),
	[GPIOW_6]	=	PULL_UP_MAP(2, 6),
	[GPIOW_7]	=	PULL_UP_MAP(2, 7),
	[GPIOW_8]	=	PULL_UP_MAP(2, 8),
	[GPIOW_9]	=	PULL_UP_MAP(2, 9),
	[GPIOW_10]	=	PULL_UP_MAP(2, 10),
	[GPIOW_11]	=	PULL_UP_MAP(2, 11),
	[GPIOW_12]	=	PULL_UP_MAP(2, 12),
	[GPIOW_13]	=	PULL_UP_MAP(2, 13),
	[GPIOW_14]	=	PULL_UP_MAP(2, 14),
	[GPIOW_15]	=	PULL_UP_MAP(2, 15),
	[GPIOW_16]	=	PULL_UP_MAP(2, 16),
	[GPIOW_17]	=	PULL_UP_MAP(2, 17),
	[GPIOW_18]	=	PULL_UP_MAP(2, 18),
	[GPIOW_19]	=	PULL_UP_MAP(2, 19),

	[GPIOX_0]	=	PULL_UP_MAP(4, 0),
	[GPIOX_1]	=	PULL_UP_MAP(4, 1),
	[GPIOX_2]	=	PULL_UP_MAP(4, 2),
	[GPIOX_3]	=	PULL_UP_MAP(4, 3),
	[GPIOX_4]	=	PULL_UP_MAP(4, 4),
	[GPIOX_5]	=	PULL_UP_MAP(4, 5),
	[GPIOX_6]	=	PULL_UP_MAP(4, 6),
	[GPIOX_7]	=	PULL_UP_MAP(4, 7),
	[GPIOX_8]	=	PULL_UP_MAP(4, 8),
	[GPIOX_9]	=	PULL_UP_MAP(4, 9),
	[GPIOX_10]	=	PULL_UP_MAP(4, 10),
	[GPIOX_11]	=	PULL_UP_MAP(4, 11),
	[GPIOX_12]	=	PULL_UP_MAP(4, 12),

	[GPIOY_0]	=	PULL_UP_MAP(5, 0),
	[GPIOY_1]	=	PULL_UP_MAP(5, 1),
	[GPIOY_2]	=	PULL_UP_MAP(5, 2),
	[GPIOY_3]	=	PULL_UP_MAP(5, 3),
	[GPIOY_4]	=	PULL_UP_MAP(5, 4),
	[GPIOY_5]	=	PULL_UP_MAP(5, 5),
	[GPIOY_6]	=	PULL_UP_MAP(5, 6),
	[GPIOY_7]	=	PULL_UP_MAP(5, 7),
	[GPIOY_8]	=	PULL_UP_MAP(5, 8),
	[GPIOY_9]	=	PULL_UP_MAP(5, 9),
	[GPIOY_10]	=	PULL_UP_MAP(5, 10),
	[GPIOY_11]	=	PULL_UP_MAP(5, 11),
	[GPIOY_12]	=	PULL_UP_MAP(5, 12),
	[GPIOY_13]	=	PULL_UP_MAP(5, 13),
	[GPIOY_14]	=	PULL_UP_MAP(5, 14),
	[GPIOY_15]	=	PULL_UP_MAP(5, 15),
	[GPIOY_16]	=	PULL_UP_MAP(5, 16),
	[GPIOY_17]	=	PULL_UP_MAP(5, 17),
	[GPIOY_18]	=	PULL_UP_MAP(5, 18),
	[GPIOY_19]	=	PULL_UP_MAP(5, 19),
	[GPIOY_20]	=	PULL_UP_MAP(5, 20),
	[GPIOY_21]	=	PULL_UP_MAP(5, 21),
	[GPIOY_22]	=	PULL_UP_MAP(5, 22),
	[GPIOY_23]	=	PULL_UP_MAP(5, 23),
	[GPIOY_24]	=	PULL_UP_MAP(5, 24),
	[GPIOY_25]	=	PULL_UP_MAP(5, 25),
	[GPIOY_26]	=	PULL_UP_MAP(5, 26),
	[GPIOY_27]	=	PULL_UP_MAP(5, 27),

	[GPIOZ_0]	=	PULL_UP_MAP(6, 0),
	[GPIOZ_1]	=	PULL_UP_MAP(6, 1),
	[GPIOZ_2]	=	PULL_UP_MAP(6, 2),
	[GPIOZ_3]	=	PULL_UP_MAP(6, 3),
	[GPIOZ_4]	=	PULL_UP_MAP(6, 4),
	[GPIOZ_5]	=	PULL_UP_MAP(6, 5),
	[GPIOZ_6]	=	PULL_UP_MAP(6, 6),
	[GPIOZ_7]	=	PULL_UP_MAP(6, 7),
	[GPIOZ_8]	=	PULL_UP_MAP(6, 8),
	[GPIOZ_9]	=	PULL_UP_MAP(6, 9),
	[GPIOZ_10]	=	PULL_UP_MAP(6, 10),
	[GPIOZ_11]	=	PULL_UP_MAP(6, 11),
	[GPIOZ_12]	=	PULL_UP_MAP(6, 12),
	[GPIOZ_13]	=	PULL_UP_MAP(6, 13),
	[GPIOZ_14]	=	PULL_UP_MAP(6, 14),
	[GPIOZ_15]	=	PULL_UP_MAP(6, 15),
	[GPIOZ_16]	=	PULL_UP_MAP(6, 16),
	[GPIOZ_17]	=	PULL_UP_MAP(6, 17),
	[GPIOZ_18]	=	PULL_UP_MAP(6, 18),
	[GPIOZ_19]	=	PULL_UP_MAP(6, 19),
};

#define PIN_MAP(reg, bit)	((reg << 5) | bit)
#define PIN_REG(val)		(val >> 5)
#define PIN_BIT(val)		(val & 0x1F)
#define PIN_NONE		(0xFFFF)


static unsigned int m6tv_pin_map[] = {
	[BOOT_0]	=	PIN_MAP(3, 0),
	[BOOT_1]	= 	PIN_MAP(3, 1),
	[BOOT_2]	= 	PIN_MAP(3, 2),
	[BOOT_3]	= 	PIN_MAP(3, 3),
	[BOOT_4]	= 	PIN_MAP(3, 4),
	[BOOT_5]	= 	PIN_MAP(3, 5),
	[BOOT_6]	= 	PIN_MAP(3, 6),
	[BOOT_7]	= 	PIN_MAP(3, 7),
	[BOOT_8]	= 	PIN_MAP(3, 8),
	[BOOT_9]	= 	PIN_MAP(3, 9),
	[BOOT_10]	=	PIN_MAP(3, 10),
	[BOOT_11]	=	PIN_MAP(3, 11),
	[BOOT_12]	=	PIN_MAP(3, 12),
	[BOOT_13]	=	PIN_MAP(3, 13),
	[BOOT_14]	=	PIN_MAP(3, 14),
	[BOOT_15]	=	PIN_MAP(3, 15),
	[BOOT_16]	=	PIN_MAP(3, 16),
	[BOOT_17]	=	PIN_MAP(3, 17),

	[CARD_0]	=  	PIN_MAP(5, 23),
	[CARD_1]	=  	PIN_MAP(5, 24),
	[CARD_2]	=  	PIN_MAP(5, 25),
	[CARD_3]	=  	PIN_MAP(5, 26),
	[CARD_4]	=  	PIN_MAP(5, 27),
	[CARD_5]	=  	PIN_MAP(5, 28),
	[CARD_6]	=  	PIN_MAP(5, 29),
	[CARD_7]	=  	PIN_MAP(5, 30),
	[CARD_8]	=  	PIN_MAP(5, 31),

	[GPIOA_0]	= 	PIN_MAP(0, 0),
	[GPIOA_1]	=  	PIN_MAP(0, 1),
	[GPIOA_2]	=  	PIN_MAP(0, 2),
	[GPIOA_3]	=  	PIN_MAP(0, 3),
	[GPIOA_4]	=  	PIN_MAP(0, 4),
	[GPIOA_5]	=  	PIN_MAP(0, 5),
	[GPIOA_6]	=  	PIN_MAP(0, 6),
	[GPIOA_7]	=  	PIN_MAP(0, 7),
	[GPIOA_8]	=  	PIN_MAP(0, 8),
	[GPIOA_9]	=  	PIN_MAP(0, 9),
	[GPIOA_10]	= 	PIN_MAP(0, 10),
	[GPIOA_11]	=  	PIN_MAP(0, 11),
	[GPIOA_12]	=  	PIN_MAP(0, 12),
	[GPIOA_13]	=  	PIN_MAP(0, 13),
	[GPIOA_14]	=  	PIN_MAP(0, 14),
	[GPIOA_15]	=  	PIN_MAP(0, 15),
	[GPIOA_16]	=  	PIN_MAP(0, 16),
	[GPIOA_17]	=  	PIN_MAP(0, 17),
	[GPIOA_18]	=  	PIN_MAP(0, 18),
	[GPIOA_19]	=  	PIN_MAP(0, 19),
	[GPIOA_20]	=	PIN_MAP(0, 20),
	[GPIOA_21]	= 	PIN_MAP(0, 21),
	[GPIOA_22]	= 	PIN_MAP(0, 22),
	[GPIOA_23]	= 	PIN_MAP(0, 23),
	[GPIOA_24]	= 	PIN_MAP(0, 24),
	[GPIOA_25]	= 	PIN_MAP(0, 25),
	[GPIOA_26]	= 	PIN_MAP(0, 26),
	[GPIOA_27]	= 	PIN_MAP(0, 27),
	[GPIOA_28]	= 	PIN_MAP(0, 28),
	[GPIOA_29]	= 	PIN_MAP(0, 29),

	[GPIOAO_0]	=	PIN_MAP(7, 0),
	[GPIOAO_1]	=	PIN_MAP(7, 1),
	[GPIOAO_2]	=	PIN_MAP(7, 2),
	[GPIOAO_3]	=	PIN_MAP(7, 3),
	[GPIOAO_4]	=	PIN_MAP(7, 4),
	[GPIOAO_5]	=	PIN_MAP(7, 5),
	[GPIOAO_6]	=	PIN_MAP(7, 6),
	[GPIOAO_7]	=	PIN_MAP(7, 7),
	[GPIOAO_8]	=	PIN_MAP(7, 8),
	[GPIOAO_9]	=	PIN_MAP(7, 9),
	[GPIOAO_10]	=	PIN_MAP(7, 10),
	[GPIOAO_11]	=	PIN_MAP(7, 11),


	[GPIOB_0]	=	PIN_MAP(1, 0),
	[GPIOB_1]	=	PIN_MAP(1, 1),
	[GPIOB_2]	=	PIN_MAP(1, 2),
	[GPIOB_3]	=	PIN_MAP(1, 3),
	[GPIOB_4]	=	PIN_MAP(1, 4),
	[GPIOB_5]	=	PIN_MAP(1, 5),
	[GPIOB_6]	=	PIN_MAP(1, 6),
	[GPIOB_7]	=	PIN_MAP(1, 7),
	[GPIOB_8]	=	PIN_MAP(1, 8),
	[GPIOB_9]	=	PIN_MAP(1, 9),
	[GPIOB_10]	=	PIN_MAP(1, 10),
	[GPIOB_11]	=	PIN_MAP(1, 11),
	[GPIOB_12]	=	PIN_MAP(1, 12),
	[GPIOB_13]	=	PIN_MAP(1, 13),
	[GPIOB_14]	=	PIN_MAP(1, 14),
	[GPIOB_15]	=	PIN_MAP(1, 15),
	[GPIOB_16]	=	PIN_MAP(1, 16),
	[GPIOB_17]	=	PIN_MAP(1, 17),
	[GPIOB_18]	=	PIN_MAP(1, 18),
	[GPIOB_19]	=	PIN_MAP(1, 19),
	[GPIOB_20]	=	PIN_MAP(1, 20),
	[GPIOB_21]	=	PIN_MAP(1, 21),
	[GPIOB_22]	=	PIN_MAP(1, 22),
	[GPIOB_23]	=	PIN_MAP(1, 23),


	[GPIOP_0]	=	PIN_MAP(1, 24),
	[GPIOP_1]	=	PIN_MAP(1, 25),
	[GPIOP_2]	=	PIN_MAP(1, 26),
	[GPIOP_3]	=	PIN_MAP(1, 27),
	[GPIOP_4]	=	PIN_MAP(1, 28),
	[GPIOP_5]	=	PIN_MAP(1, 29),
	[GPIOP_6]	=	PIN_MAP(1, 30),
	[GPIOP_7]	=	PIN_MAP(1, 31),

	[GPIOW_0]	=	PIN_MAP(5, 0),
	[GPIOW_1]	=	PIN_MAP(5, 1),
	[GPIOW_2]	=	PIN_MAP(5, 2),
	[GPIOW_3]	=	PIN_MAP(5, 3),
	[GPIOW_4]	=	PIN_MAP(5, 4),
	[GPIOW_5]	=	PIN_MAP(5, 5),
	[GPIOW_6]	=	PIN_MAP(5, 6),
	[GPIOW_7]	=	PIN_MAP(5, 7),
	[GPIOW_8]	=	PIN_MAP(5, 8),
	[GPIOW_9]	=	PIN_MAP(5, 9),
	[GPIOW_10]	=	PIN_MAP(5, 10),
	[GPIOW_11]	=	PIN_MAP(5, 11),
	[GPIOW_12]	=	PIN_MAP(5, 12),
	[GPIOW_13]	=	PIN_MAP(5, 13),
	[GPIOW_14]	=	PIN_MAP(5, 14),
	[GPIOW_15]	=	PIN_MAP(5, 15),
	[GPIOW_16]	=	PIN_MAP(5, 16),
	[GPIOW_17]	=	PIN_MAP(5, 17),
	[GPIOW_18]	=	PIN_MAP(5, 18),
	[GPIOW_19]	=	PIN_MAP(5, 19),


	[GPIOX_0]	= 	PIN_MAP(4, 0),
	[GPIOX_1]	= 	PIN_MAP(4, 1),
	[GPIOX_2]	= 	PIN_MAP(4, 2),
	[GPIOX_3]	= 	PIN_MAP(4, 3),
	[GPIOX_4]	= 	PIN_MAP(4, 4),
	[GPIOX_5]	= 	PIN_MAP(4, 5),
	[GPIOX_6]	= 	PIN_MAP(4, 6),
	[GPIOX_7]	= 	PIN_MAP(4, 7),
	[GPIOX_8]	= 	PIN_MAP(4, 8),
	[GPIOX_9]	= 	PIN_MAP(4, 9),
	[GPIOX_10]	= 	PIN_MAP(4, 10),
	[GPIOX_11]	= 	PIN_MAP(4, 11),
	[GPIOX_12]	= 	PIN_MAP(4, 12),

	[GPIOY_0]	=  	PIN_MAP(2, 0),
	[GPIOY_1]	=  	PIN_MAP(2, 1),
	[GPIOY_2]	=  	PIN_MAP(2, 2),
	[GPIOY_3]	=  	PIN_MAP(2, 3),
	[GPIOY_4]	=  	PIN_MAP(2, 4),
	[GPIOY_5]	=  	PIN_MAP(2, 5),
	[GPIOY_6]	=  	PIN_MAP(2, 6),
	[GPIOY_7]	=  	PIN_MAP(2, 7),
	[GPIOY_8]	=  	PIN_MAP(2, 8),
	[GPIOY_9]	=  	PIN_MAP(2, 9),
	[GPIOY_10]	=  	PIN_MAP(2, 10),
	[GPIOY_11]	=  	PIN_MAP(2, 11),
	[GPIOY_12]	=  	PIN_MAP(2, 12),
	[GPIOY_13]	=  	PIN_MAP(2, 13),
	[GPIOY_14]	=  	PIN_MAP(2, 14),
	[GPIOY_15]	=  	PIN_MAP(2, 15),
	[GPIOY_16]	=  	PIN_MAP(2, 16),
	[GPIOY_17]	=  	PIN_MAP(2, 17),
	[GPIOY_18]	=  	PIN_MAP(2, 18),
	[GPIOY_19]	=  	PIN_MAP(2, 19),
	[GPIOY_20]	= 	PIN_MAP(2, 20),
	[GPIOY_21]	=  	PIN_MAP(2, 21),
	[GPIOY_22]	=  	PIN_MAP(2, 22),
	[GPIOY_23]	=  	PIN_MAP(2, 23),
	[GPIOY_24]	=  	PIN_MAP(2, 24),
	[GPIOY_25]	=  	PIN_MAP(2, 25),
	[GPIOY_26]	=  	PIN_MAP(2, 26),
	[GPIOY_27]	=  	PIN_MAP(2, 27),


	[GPIOZ_0]	= 	PIN_MAP(6, 0),
	[GPIOZ_1]	= 	PIN_MAP(6, 1),
	[GPIOZ_2]	= 	PIN_MAP(6, 2),
	[GPIOZ_3]	= 	PIN_MAP(6, 3),
	[GPIOZ_4]	= 	PIN_MAP(6, 4),
	[GPIOZ_5]	= 	PIN_MAP(6, 5),
	[GPIOZ_6]	= 	PIN_MAP(6, 6),
	[GPIOZ_7]	= 	PIN_MAP(6, 7),
	[GPIOZ_8]	= 	PIN_MAP(6, 8),
	[GPIOZ_9]	= 	PIN_MAP(6, 9),
	[GPIOZ_10]	= 	PIN_MAP(6, 10),
	[GPIOZ_11]	= 	PIN_MAP(6, 11),
	[GPIOZ_12]	= 	PIN_MAP(6, 12),
	[GPIOZ_13]	= 	PIN_MAP(6, 13),
	[GPIOZ_14]	= 	PIN_MAP(6, 14),
	[GPIOZ_15]	= 	PIN_MAP(6, 15),
	[GPIOZ_16]	= 	PIN_MAP(6, 16),
	[GPIOZ_17]	= 	PIN_MAP(6, 17),
	[GPIOZ_18]	= 	PIN_MAP(6, 18),
	[GPIOZ_19]	= 	PIN_MAP(6, 19),
};


#if 0
static int m6tv_pin_to_pullup(unsigned int pin ,unsigned int *reg,unsigned int *bit)
{
	if (GPIOZ_0 <= pin <= GPIOZ_19) {
		*reg = 6;
		*bit = pin - GPIOZ_0;
	}
	else if (GPIOA_0 <= pin <= GPIOA_29) {
		*reg = 0;
		*bit = pin - GPIOA_0;
	}
	else if (GPIOP_0 <= pin <= GPIOP_7) {
		*reg = 1;
		*bit = pin - GPIOP_0 + 24;
	}
	else if (GPIOB_0 <= pin <= GPIOB_23) {
		*reg = 1;
		*bit = pin - GPIOB_0;
	}
	else if (GPIOW_0 <= pin <= GPIOW_19) {
		*reg = 2;
		*bit = pin - GPIOW_0;
	}
	else if (CARD_0 <= pin <= CARD_8) {
		*reg = 3;
		*bit = pin - CARD_0 + 20;
	}
	else if (BOOT_0 <= pin <= BOOT_17) {
		*reg = 3;
		*bit = pin - BOOT_0;
	}
	else if (GPIOX_0 <= pin <= GPIOX_12) {
		*reg = 4;
		*bit = pin - GPIOX_0;
	}
	else if (GPIOY_0 <= pin <= GPIOY_27) {
		*reg = 5;
		*bit = pin - GPIOY_0;
	}
	else {
		return -1;
	}



	return 0;
}
#endif

static int m6tv_pin_map_to_direction(unsigned int pin, unsigned int *reg, unsigned int *bit)
{
	*reg = PIN_REG(m6tv_pin_map[pin]);
	*bit = PIN_BIT(m6tv_pin_map[pin]);
	return 0;
}

static int m6tv_set_pullup(unsigned int pin, unsigned int config)
{
	unsigned int reg = 0, bit = 0;
	u16 pullarg = AML_PINCONF_UNPACK_PULL_ARG(config);

	if (m6tv_pull_up[pin] == PULL_UP_NONE)
		return -1;

	reg = PULL_UP_REG(m6tv_pull_up[pin]);
	bit = PULL_UP_BIT(m6tv_pull_up[pin]);

	if(pullarg)
		aml_set_reg32_mask(p_pull_up_addr[reg], 1<<bit);
	else
		aml_clr_reg32_mask(p_pull_up_addr[reg], 1<<bit);
	return 0;
}

static struct amlogic_pinctrl_soc_data m6tv_pinctrl = {
	.pins = m6tv_pads,
	.npins = ARRAY_SIZE(m6tv_pads),
	.meson_set_pullup = m6tv_set_pullup,
	.pin_map_to_direction = m6tv_pin_map_to_direction,
};

static struct of_device_id m6tv_pinctrl_of_table[] = {
	{
		.compatible="amlogic,pinmux-m6tv",
	},
	{},
};

static int m6tv_pmx_probe(struct platform_device *pdev)
{
	return amlogic_pmx_probe(pdev, &m6tv_pinctrl);
}

static int m6tv_pmx_remove(struct platform_device *pdev)
{
	return amlogic_pmx_remove(pdev);
}

static struct platform_driver m6tv_pmx_driver = {
	.driver = {
		.name = "pinmux-m6tv",
		.owner = THIS_MODULE,
		.of_match_table=of_match_ptr(m6tv_pinctrl_of_table),
	},
	.probe  = m6tv_pmx_probe,
	.remove = m6tv_pmx_remove,
};

static int __init m6tv_pmx_init(void)
{
	return platform_driver_register(&m6tv_pmx_driver);
}
arch_initcall(m6tv_pmx_init);

static void __exit m6tv_pmx_exit(void)
{
	platform_driver_unregister(&m6tv_pmx_driver);
}
module_exit(m6tv_pmx_exit);
MODULE_DESCRIPTION("amlogic m6tv pin control driver");
MODULE_LICENSE("GPL v2");
