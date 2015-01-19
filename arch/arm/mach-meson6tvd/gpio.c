/*
 * arch/arm/mach-meson6tv/gpio.c
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
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/gpio.h>

#include <linux/amlogic/gpio-amlogic.h>

#include <mach/am_regs.h>
#include <mach/gpio.h>

#include "common.h"

unsigned p_gpio_oen_addr[] = {
	P_PREG_PAD_GPIO0_EN_N,	// 0x200c
	P_PREG_PAD_GPIO1_EN_N,	// 0x200f
	P_PREG_PAD_GPIO2_EN_N,	// 0x2012
	P_PREG_PAD_GPIO3_EN_N,	// 0x2015
	P_PREG_PAD_GPIO4_EN_N,	// 0x2018
	P_PREG_PAD_GPIO5_EN_N,	// 0x201b
	P_PREG_PAD_GPIO6_EN_N,	// 0x2008
	P_AO_GPIO_O_EN_N,
};
static unsigned p_gpio_output_addr[] = {
	P_PREG_PAD_GPIO0_O,	// 0x200d
	P_PREG_PAD_GPIO1_O,	// 0x2010
	P_PREG_PAD_GPIO2_O,	// 0x2013
	P_PREG_PAD_GPIO3_O,	// 0x2016
	P_PREG_PAD_GPIO4_O,	// 0x2019
	P_PREG_PAD_GPIO5_O,	// 0x201c
	P_PREG_PAD_GPIO6_O,	// 0x2009
	P_AO_GPIO_O_EN_N,
};
static unsigned p_gpio_input_addr[] = {
	P_PREG_PAD_GPIO0_I,	// 0x200e
	P_PREG_PAD_GPIO1_I,	// 0x2011
	P_PREG_PAD_GPIO2_I,	// 0x2014
	P_PREG_PAD_GPIO3_I,	// 0x2017
	P_PREG_PAD_GPIO4_I,	// 0x201a
	P_PREG_PAD_GPIO5_I,	// 0x201d
	P_PREG_PAD_GPIO6_I,	// 0x200a
	P_AO_GPIO_I,
};

#define PIN_MUX(reg, bit)	((reg << 5) | bit)
#define NONE			(0xFFFF)
#define MUX_SIZE		6

static unsigned int m6tv_pin_mux[][MUX_SIZE] = {
	[GPIOZ_0]	=	{PIN_MUX(4,13),NONE,NONE,NONE,NONE,NONE},
	[GPIOZ_1]	=	{PIN_MUX(4,12),NONE,NONE,NONE,NONE,NONE},
	[GPIOZ_2]	=	{PIN_MUX(4,11),NONE,NONE,NONE,NONE,NONE},
	[GPIOZ_3]	=	{PIN_MUX(4,10),NONE,NONE,NONE,NONE,NONE},
	[GPIOZ_4]	=	{NONE,PIN_MUX(4, 9),NONE,NONE,NONE,NONE},
	[GPIOZ_5]	=	{NONE,PIN_MUX(4, 8),NONE,NONE,NONE,NONE},
	[GPIOZ_6]	=	{NONE,NONE,NONE,NONE,PIN_MUX(5,31),PIN_MUX(5,29)},
	[GPIOZ_7]	=	{NONE,NONE,NONE,NONE,PIN_MUX(5,30),PIN_MUX(5,28)},
	[GPIOZ_8]	=	{NONE,NONE,NONE,NONE,PIN_MUX(5,27),PIN_MUX(5,25)},
	[GPIOZ_9]	=	{NONE,NONE,NONE,NONE,PIN_MUX(5,26),PIN_MUX(5,24)},
	[GPIOZ_10]	=	{PIN_MUX(4,25),PIN_MUX(8,16),PIN_MUX(8,30),NONE,NONE,NONE},
	[GPIOZ_11]	=	{PIN_MUX(4,24),PIN_MUX(8,15),NONE,PIN_MUX(8,29),NONE,NONE},
	[GPIOZ_12]	=	{PIN_MUX(4,23),PIN_MUX(8,14),NONE,PIN_MUX(8,28),NONE,PIN_MUX(9,12)},
	[GPIOZ_13]	=	{PIN_MUX(4,22),PIN_MUX(8,13),NONE,PIN_MUX(8,27),NONE,NONE},
	[GPIOZ_14]	=	{NONE,PIN_MUX(8,12),NONE,PIN_MUX(8,26),NONE,NONE},
	[GPIOZ_15]	=	{NONE,PIN_MUX(8,18),PIN_MUX(8,24),PIN_MUX(8,25),NONE,NONE},
	[GPIOZ_16]	=	{NONE,PIN_MUX(8,17),PIN_MUX(8,22),PIN_MUX(8,23),NONE,NONE},
	[GPIOZ_17]	=	{NONE,NONE,NONE,PIN_MUX(8,21),NONE,NONE},
	[GPIOZ_18]	=	{PIN_MUX(3,24),NONE,NONE,NONE,NONE,NONE},
	[GPIOZ_19]	=	{PIN_MUX(3,23),NONE,NONE,NONE,PIN_MUX(7,28),PIN_MUX(3,26)},

	[GPIOY_0]	=	{PIN_MUX(6,31),PIN_MUX(6,30),NONE,PIN_MUX(6, 4),NONE,NONE},
	[GPIOY_1]	=	{PIN_MUX(6,18),NONE,PIN_MUX(6, 3),NONE,NONE,NONE},
	[GPIOY_2]	=	{PIN_MUX(6,17),NONE,PIN_MUX(6, 2),NONE,NONE,NONE},
	[GPIOY_3]	=	{PIN_MUX(6,16),NONE,PIN_MUX(6, 1),NONE,NONE,NONE},
	[GPIOY_4]	=	{PIN_MUX(6,15),NONE,PIN_MUX(6, 0),NONE,NONE,NONE},
	[GPIOY_5]	=	{PIN_MUX(6,14),NONE,NONE,NONE,NONE,NONE},
	[GPIOY_6]	=	{PIN_MUX(6,13),NONE,NONE,NONE,NONE,NONE},
	[GPIOY_7]	=	{PIN_MUX(6,12),NONE,NONE,NONE,NONE,NONE},
	[GPIOY_8]	=	{PIN_MUX(6,11),NONE,NONE,NONE,NONE,NONE},
	[GPIOY_9]	=	{PIN_MUX(6,10),NONE,NONE,NONE,NONE,NONE},
	[GPIOY_10]	=	{PIN_MUX(6, 9),NONE,NONE,NONE,NONE,NONE},
	[GPIOY_11]	=	{PIN_MUX(6, 8),NONE,NONE,NONE,NONE,NONE},
	[GPIOY_12]	=	{PIN_MUX(6, 7),NONE,NONE,NONE,NONE,NONE},
	[GPIOY_13]	=	{PIN_MUX(6, 6),NONE,NONE,NONE,NONE,NONE},
	[GPIOY_14]	=	{PIN_MUX(6, 5),NONE,NONE,NONE,NONE,NONE},
	[GPIOY_15]	=	{NONE,NONE,NONE,NONE,NONE,NONE},
	[GPIOY_16]	=	{NONE,NONE,NONE,NONE,NONE,NONE},
	[GPIOY_17]	=	{NONE,NONE,NONE,NONE,NONE,NONE},
	[GPIOY_18]	=	{NONE,PIN_MUX(9, 1),NONE,NONE,NONE,NONE},
	[GPIOY_19]	=	{NONE,PIN_MUX(9, 0),NONE,NONE,NONE,NONE},
	[GPIOY_20]	=	{NONE,NONE,PIN_MUX(9,10),PIN_MUX(7,27),PIN_MUX(2,31),NONE},
	[GPIOY_21]	=	{NONE,NONE,PIN_MUX(9, 9),PIN_MUX(7,26),PIN_MUX(2,30),NONE},
	[GPIOY_22]	=	{NONE,NONE,PIN_MUX(9, 8),PIN_MUX(7,25),PIN_MUX(2,29),NONE},
	[GPIOY_23]	=	{NONE,NONE,PIN_MUX(9, 7),PIN_MUX(7,24),PIN_MUX(2,28),NONE},
	[GPIOY_24]	=	{NONE,NONE,PIN_MUX(9, 6),NONE,NONE,NONE},
	[GPIOY_25]	=	{NONE,NONE,PIN_MUX(9, 5),NONE,NONE,NONE},
	[GPIOY_26]	=	{NONE,NONE,PIN_MUX(9, 4),NONE,NONE,NONE},
	[GPIOY_27]	=	{NONE,NONE,PIN_MUX(9, 3),NONE,NONE,NONE},

	[GPIOW_0]	=	{PIN_MUX(0,27),NONE,NONE,PIN_MUX(8, 9),PIN_MUX(8,10),NONE},
	[GPIOW_1]	=	{PIN_MUX(0,26),NONE,NONE,PIN_MUX(8, 9),PIN_MUX(8,10),NONE},
	[GPIOW_2]	=	{PIN_MUX(0,25),NONE,NONE,PIN_MUX(8, 9),PIN_MUX(8,10),NONE},
	[GPIOW_3]	=	{PIN_MUX(0,24),NONE,NONE,PIN_MUX(8, 9),PIN_MUX(8,10),NONE},
	[GPIOW_4]	=	{PIN_MUX(0,23),NONE,NONE,PIN_MUX(8, 9),PIN_MUX(8,10),NONE},
	[GPIOW_5]	=	{PIN_MUX(0,22),NONE,NONE,PIN_MUX(8, 9),PIN_MUX(8,10),NONE},
	[GPIOW_6]	=	{PIN_MUX(0,21),NONE,NONE,PIN_MUX(8, 9),PIN_MUX(8,10),NONE},
	[GPIOW_7]	=	{PIN_MUX(0,20),NONE,NONE,PIN_MUX(8, 9),PIN_MUX(8,10),NONE},
	[GPIOW_8]	=	{PIN_MUX(0,19),NONE,NONE,PIN_MUX(8, 9),PIN_MUX(8,10),NONE},
	[GPIOW_9]	=	{PIN_MUX(0,18),NONE,NONE,PIN_MUX(8, 9),PIN_MUX(8,10),NONE},
	[GPIOW_10]	=	{PIN_MUX(0,17),NONE,NONE,PIN_MUX(8, 9),PIN_MUX(8,10),NONE},
	[GPIOW_11]	=	{PIN_MUX(0,16),NONE,NONE,NONE,NONE,NONE},
	[GPIOW_12]	=	{PIN_MUX(0,15),NONE,NONE,NONE,NONE,NONE},
	[GPIOW_13]	=	{PIN_MUX(0,14),NONE,NONE,NONE,NONE,NONE},
	[GPIOW_14]	=	{PIN_MUX(0,13),NONE,NONE,NONE,NONE,NONE},
	[GPIOW_15]	=	{PIN_MUX(0,12),NONE,NONE,NONE,NONE,NONE},
	[GPIOW_16]	=	{PIN_MUX(0,11),NONE,NONE,NONE,NONE,NONE},
	[GPIOW_17]	=	{PIN_MUX(1, 2),NONE,NONE,NONE,NONE,NONE},
	[GPIOW_18]	=	{PIN_MUX(1, 1),NONE,NONE,NONE,NONE,NONE},
	[GPIOW_19]	=	{PIN_MUX(1, 0),PIN_MUX(3,22),NONE,NONE,NONE,NONE},

	[GPIOX_0]	=	{PIN_MUX(1,12),PIN_MUX(5,14),PIN_MUX(8, 5),NONE,NONE,NONE},
	[GPIOX_1]	=	{PIN_MUX(1,13),PIN_MUX(5,13),PIN_MUX(8, 4),NONE,NONE,NONE},
	[GPIOX_2]	=	{PIN_MUX(1,14),PIN_MUX(5,13),PIN_MUX(8, 3),NONE,NONE,NONE},
	[GPIOX_3]	=	{PIN_MUX(1,15),PIN_MUX(5,13),PIN_MUX(8, 2),NONE,NONE,NONE},
	[GPIOX_4]	=	{PIN_MUX(1,16),PIN_MUX(5,12),NONE,PIN_MUX(3,30),PIN_MUX(7,21),NONE},
	[GPIOX_5]	=	{PIN_MUX(1,17),PIN_MUX(5,12),NONE,PIN_MUX(3,29),PIN_MUX(7,20),NONE},
	[GPIOX_6]	=	{PIN_MUX(1,18),PIN_MUX(5,12),NONE,PIN_MUX(3,28),PIN_MUX(7,19),NONE},
	[GPIOX_7]	=	{PIN_MUX(1,19),PIN_MUX(5,12),NONE,PIN_MUX(3,27),PIN_MUX(7,18),NONE},
	[GPIOX_8]	=	{NONE,PIN_MUX(5,11),PIN_MUX(8, 1),NONE,NONE,NONE},
	[GPIOX_9]	=	{NONE,PIN_MUX(5,10),PIN_MUX(8, 0),NONE,NONE,NONE},
	[GPIOX_10]	=	{NONE,NONE,NONE,NONE,PIN_MUX(7,22),NONE},
	[GPIOX_11]	=	{NONE,NONE,NONE,NONE,NONE,NONE},
	[GPIOX_12]	=	{NONE,NONE,NONE,NONE,NONE,PIN_MUX(3,21)},

	[BOOT_0	]	=	{PIN_MUX(2,26),NONE,PIN_MUX(4,30),PIN_MUX(6,29),NONE,NONE},
	[BOOT_1	]	=	{PIN_MUX(2,26),NONE,PIN_MUX(4,29),PIN_MUX(6,28),NONE,NONE},
	[BOOT_2	]	=	{PIN_MUX(2,26),NONE,PIN_MUX(4,29),PIN_MUX(6,27),NONE,NONE},
	[BOOT_3	]	=	{PIN_MUX(2,26),NONE,PIN_MUX(4,29),PIN_MUX(6,26),NONE,NONE},
	[BOOT_4	]	=	{PIN_MUX(2,26),NONE,PIN_MUX(4,28),NONE,NONE,NONE},
	[BOOT_5	]	=	{PIN_MUX(2,26),NONE,PIN_MUX(4,28),NONE,NONE,NONE},
	[BOOT_6	]	=	{PIN_MUX(2,26),NONE,PIN_MUX(4,28),NONE,NONE,NONE},
	[BOOT_7	]	=	{PIN_MUX(2,26),NONE,PIN_MUX(4,28),NONE,NONE,NONE},
	[BOOT_8	]	=	{PIN_MUX(2,25),NONE,NONE,NONE,NONE,NONE},
	[BOOT_9	]	=	{PIN_MUX(2,24),NONE,NONE,NONE,NONE,NONE},
	[BOOT_10]	=	{PIN_MUX(2,23),PIN_MUX(2,17),PIN_MUX(4,27),PIN_MUX(6,25),NONE,NONE},
	[BOOT_11]	=	{PIN_MUX(2,22),PIN_MUX(2,16),PIN_MUX(4,26),PIN_MUX(6,24),NONE,NONE},
	[BOOT_12]	=	{PIN_MUX(2,21),PIN_MUX(5, 1),NONE,NONE,NONE,NONE},
	[BOOT_13]	=	{PIN_MUX(2,20),PIN_MUX(5, 3),NONE,NONE,NONE,NONE},
	[BOOT_14]	=	{PIN_MUX(2,19),PIN_MUX(5, 2),NONE,NONE,NONE,NONE},
	[BOOT_15]	=	{PIN_MUX(2,18),NONE,NONE,NONE,NONE,NONE},
	[BOOT_16]	=	{PIN_MUX(2,27),NONE,NONE,NONE,NONE,NONE},
	[BOOT_17]	=	{NONE,PIN_MUX(5, 0),PIN_MUX(3, 25),NONE,PIN_MUX(7, 29),NONE},

	[GPIOP_0]	=	{PIN_MUX(4, 3),NONE,NONE,PIN_MUX(9,22),PIN_MUX(9,21),PIN_MUX(9,20)},//
	[GPIOP_1]	=	{PIN_MUX(4, 2),PIN_MUX(1,22),NONE,NONE,NONE,PIN_MUX(9,23)},
	[GPIOP_2]	=	{PIN_MUX(4,17),PIN_MUX(1,23),PIN_MUX(4,15),NONE,NONE,PIN_MUX(9,24)},
	[GPIOP_3]	=	{PIN_MUX(4,16),PIN_MUX(1,24),PIN_MUX(4,14),NONE,NONE,PIN_MUX(9,25)},
	[GPIOP_4]	=	{NONE,PIN_MUX(1,25),NONE,NONE,NONE,PIN_MUX(9,26)},
	[GPIOP_5]	=	{NONE,NONE,PIN_MUX(1,21),PIN_MUX(2, 1),PIN_MUX(7,30),PIN_MUX(9,27)},
	[GPIOP_6]	=	{NONE,NONE,PIN_MUX(1,20),PIN_MUX(2, 0),PIN_MUX(2, 2),PIN_MUX(9,28)},
	[GPIOP_7]	=	{NONE,NONE,NONE,NONE,NONE,PIN_MUX(9,29)},

	[CARD_0	]	=	{NONE,NONE,PIN_MUX(2, 7),PIN_MUX(2,15),NONE,NONE},
	[CARD_1	]	=	{NONE,NONE,PIN_MUX(2, 6),PIN_MUX(2,14),NONE,NONE},
	[CARD_2	]	=	{NONE,NONE,PIN_MUX(2, 6),PIN_MUX(2,13),NONE,NONE},
	[CARD_3	]	=	{NONE,NONE,PIN_MUX(2, 6),PIN_MUX(2,12),NONE,NONE},
	[CARD_4	]	=	{PIN_MUX(1, 4),NONE,PIN_MUX(2, 5),PIN_MUX(2,11),NONE,NONE},
	[CARD_5	]	=	{PIN_MUX(1, 5),NONE,PIN_MUX(2, 4),PIN_MUX(2,10),NONE,NONE},
	[CARD_6	]	=	{PIN_MUX(1, 6),NONE,NONE,NONE,NONE,NONE},
	[CARD_7	]	=	{PIN_MUX(1, 7),NONE,NONE,NONE,PIN_MUX(6, 23),PIN_MUX(8, 11)},
	[CARD_8	]	=	{NONE,NONE,NONE,NONE,PIN_MUX(6,23),PIN_MUX(8,11)},

	[GPIOB_0]	=	{PIN_MUX(0, 1),PIN_MUX(3,10),NONE,NONE,NONE,NONE},
	[GPIOB_1]	=	{PIN_MUX(0, 0),PIN_MUX(3,11),NONE,NONE,NONE,NONE},
	[GPIOB_2]	=	{PIN_MUX(0, 0),PIN_MUX(3,11),NONE,NONE,NONE,NONE},
	[GPIOB_3]	=	{PIN_MUX(0, 0),PIN_MUX(3,11),PIN_MUX(5,23),NONE,NONE,NONE},
	[GPIOB_4]	=	{PIN_MUX(0, 0),PIN_MUX(3,11),PIN_MUX(5,22),NONE,NONE,NONE},
	[GPIOB_5]	=	{PIN_MUX(0, 0),PIN_MUX(3,11),PIN_MUX(5,21),NONE,NONE,NONE},
	[GPIOB_6]	=	{PIN_MUX(0, 0),PIN_MUX(3,11),PIN_MUX(5,20),NONE,NONE,NONE},
	[GPIOB_7]	=	{PIN_MUX(0, 0),PIN_MUX(3,11),PIN_MUX(5,19),NONE,NONE,NONE},
	[GPIOB_8]	=	{PIN_MUX(0, 3),PIN_MUX(3, 9),PIN_MUX(5,18),NONE,NONE,NONE},
	[GPIOB_9]	=	{PIN_MUX(0, 3),PIN_MUX(3, 8),PIN_MUX(5,17),NONE,NONE,NONE},
	[GPIOB_10]	=	{PIN_MUX(0, 2),PIN_MUX(3, 7),PIN_MUX(5,16),NONE,NONE,NONE},
	[GPIOB_11]	=	{PIN_MUX(0, 2),PIN_MUX(3, 6),PIN_MUX(5,15),NONE,NONE,NONE},
	[GPIOB_12]	=	{PIN_MUX(0, 2),NONE,PIN_MUX(3,17),PIN_MUX(5, 5),NONE,NONE},
	[GPIOB_13]	=	{PIN_MUX(0, 2),NONE,PIN_MUX(3,16),NONE,NONE,NONE},
	[GPIOB_14]	=	{PIN_MUX(0, 2),NONE,PIN_MUX(3,15),NONE,NONE,NONE},
	[GPIOB_15]	=	{PIN_MUX(0, 2),NONE,PIN_MUX(3,14),NONE,NONE,NONE},
	[GPIOB_16]	=	{PIN_MUX(0, 5),NONE,PIN_MUX(3,13),NONE,NONE,NONE},
	[GPIOB_17]	=	{PIN_MUX(0, 5),NONE,PIN_MUX(3,12),NONE,NONE,NONE},
	[GPIOB_18]	=	{PIN_MUX(0, 4),NONE,PIN_MUX(3,12),NONE,NONE,NONE},
	[GPIOB_19]	=	{PIN_MUX(0, 4),NONE,PIN_MUX(3,12),NONE,NONE,NONE},
	[GPIOB_20]	=	{PIN_MUX(0, 4),NONE,PIN_MUX(3,12),NONE,NONE,NONE},
	[GPIOB_21]	=	{PIN_MUX(0, 4),NONE,PIN_MUX(3,12),NONE,NONE,NONE},
	[GPIOB_22]	=	{PIN_MUX(0, 4),NONE,PIN_MUX(3,12),NONE,NONE,NONE},
	[GPIOB_23]	=	{PIN_MUX(0, 4),NONE,PIN_MUX(3,12),NONE,NONE,NONE},

	[GPIOA_0]	=	{PIN_MUX(0, 6),PIN_MUX(3, 4),NONE,NONE,PIN_MUX(6,20),PIN_MUX(8,11)},
	[GPIOA_1]	=	{PIN_MUX(0, 6),PIN_MUX(3, 5),NONE,NONE,PIN_MUX(6,20),PIN_MUX(8,11)},
	[GPIOA_2]	=	{PIN_MUX(0, 6),PIN_MUX(3, 5),NONE,NONE,PIN_MUX(6,20),PIN_MUX(8,11)},
	[GPIOA_3]	=	{PIN_MUX(0, 6),PIN_MUX(3, 5),NONE,NONE,PIN_MUX(6,20),PIN_MUX(8,11)},
	[GPIOA_4]	=	{PIN_MUX(0, 6),PIN_MUX(3, 5),NONE,NONE,PIN_MUX(6,20),PIN_MUX(8,11)},
	[GPIOA_5]	=	{PIN_MUX(0, 6),PIN_MUX(3, 5),NONE,NONE,PIN_MUX(6,20),PIN_MUX(8,11)},
	[GPIOA_6]	=	{PIN_MUX(0, 6),PIN_MUX(3, 5),NONE,NONE,PIN_MUX(6,20),PIN_MUX(8,11)},
	[GPIOA_7]	=	{PIN_MUX(0, 6),PIN_MUX(3, 5),NONE,NONE,PIN_MUX(6,20),PIN_MUX(8,11)},
	[GPIOA_8]	=	{PIN_MUX(0, 6),PIN_MUX(3, 0),NONE,NONE,PIN_MUX(6,21),PIN_MUX(8,11)},
	[GPIOA_9]	=	{PIN_MUX(0, 6),PIN_MUX(3, 1),NONE,NONE,PIN_MUX(6,21),PIN_MUX(8,11)},
	[GPIOA_10]	=	{PIN_MUX(0, 6),PIN_MUX(3, 2),NONE,NONE,PIN_MUX(6,21),PIN_MUX(8,11)},
	[GPIOA_11]	=	{PIN_MUX(0, 6),PIN_MUX(3, 3),NONE,NONE,PIN_MUX(6,21),PIN_MUX(8,11)},
	[GPIOA_12]	=	{PIN_MUX(0, 6),NONE,PIN_MUX(4,21),PIN_MUX(7, 0),PIN_MUX(6,21),PIN_MUX(8,11)},
	[GPIOA_13]	=	{PIN_MUX(0, 6),NONE,PIN_MUX(4,20),PIN_MUX(7, 1),PIN_MUX(6,21),PIN_MUX(8,11)},
	[GPIOA_14]	=	{PIN_MUX(0, 6),NONE,PIN_MUX(4,19),PIN_MUX(7, 2),PIN_MUX(6,21),PIN_MUX(8,11)},
	[GPIOA_15]	=	{PIN_MUX(0, 6),NONE,PIN_MUX(4,18),PIN_MUX(7, 3),PIN_MUX(6,21),PIN_MUX(8,11)},
	[GPIOA_16]	=	{PIN_MUX(0, 6),NONE,NONE,PIN_MUX(7, 4),PIN_MUX(6,22),PIN_MUX(8,11)},
	[GPIOA_17]	=	{PIN_MUX(0, 6),NONE,NONE,PIN_MUX(7, 5),PIN_MUX(6,22),PIN_MUX(8,11)},
	[GPIOA_18]	=	{PIN_MUX(0, 6),NONE,NONE,PIN_MUX(7, 6),PIN_MUX(6,22),PIN_MUX(8,11)},
	[GPIOA_19]	=	{PIN_MUX(0, 6),NONE,NONE,PIN_MUX(7, 7),PIN_MUX(6,22),PIN_MUX(8,11)},
	[GPIOA_20]	=	{PIN_MUX(0, 6),NONE,NONE,PIN_MUX(7, 8),PIN_MUX(6,22),PIN_MUX(8,11)},
	[GPIOA_21]	=	{PIN_MUX(0, 6),NONE,NONE,PIN_MUX(7, 9),PIN_MUX(6,22),PIN_MUX(8,11)},
	[GPIOA_22]	=	{PIN_MUX(0, 6),NONE,NONE,PIN_MUX(7,10),PIN_MUX(6,22),PIN_MUX(8,11)},
	[GPIOA_23]	=	{PIN_MUX(0, 6),NONE,NONE,PIN_MUX(7,11),PIN_MUX(6,22),PIN_MUX(8,11)},
	[GPIOA_24]	=	{PIN_MUX(0, 7),NONE,NONE,PIN_MUX(7,12),PIN_MUX(6,23),PIN_MUX(8,11)},
	[GPIOA_25]	=	{PIN_MUX(0, 8),NONE,NONE,PIN_MUX(7,13),PIN_MUX(6,23),PIN_MUX(8,11)},
	[GPIOA_26]	=	{PIN_MUX(0, 9),NONE,NONE,PIN_MUX(7,14),PIN_MUX(6,23),PIN_MUX(8,11)},
	[GPIOA_27]	=	{PIN_MUX(0,10),NONE,NONE,PIN_MUX(7,15),PIN_MUX(6,23),PIN_MUX(8,11)},
	[GPIOA_28]	=	{NONE,NONE,NONE,PIN_MUX(7,16),PIN_MUX(6,23),PIN_MUX(8,11)},
	[GPIOA_29]	=	{NONE,NONE,NONE,PIN_MUX(7,17),PIN_MUX(6,23),PIN_MUX(8,11)},

	[GPIOAO_0]	=	{PIN_MUX(10,12),NONE,NONE,NONE,NONE,NONE},
	[GPIOAO_1]	=	{PIN_MUX(10,11),NONE,NONE,NONE,NONE,NONE},
	[GPIOAO_2]	=	{PIN_MUX(10,10),PIN_MUX(10,26),PIN_MUX(10, 8),PIN_MUX(10, 4),NONE,NONE},
	[GPIOAO_3]	=	{PIN_MUX(10, 9),PIN_MUX(10,25),PIN_MUX(10, 7),PIN_MUX(10, 3),NONE,NONE},
	[GPIOAO_4]	=	{NONE,PIN_MUX(10,24),PIN_MUX(10, 6),PIN_MUX(10, 2),NONE,NONE},
	[GPIOAO_5]	=	{NONE,PIN_MUX(10,23),PIN_MUX(10, 5),PIN_MUX(10, 1),NONE,NONE},
	[GPIOAO_6]	=	{PIN_MUX(10,19),NONE,NONE,PIN_MUX(10,14),PIN_MUX(10,31),PIN_MUX(10,22)},
	[GPIOAO_7]	=	{NONE,NONE,NONE,NONE,PIN_MUX(10, 0),NONE},
	[GPIOAO_8]	=	{NONE,PIN_MUX(10,30),NONE,NONE,NONE,NONE},
	[GPIOAO_9]	=	{NONE,PIN_MUX(10,29),NONE,NONE,NONE,NONE},
	[GPIOAO_10]	=	{NONE,PIN_MUX(10,28),NONE,PIN_MUX(10,17),NONE,NONE},
	[GPIOAO_11]	=	{NONE,PIN_MUX(10,27),NONE,NONE,NONE,PIN_MUX(10,21)},
};


#define PIN_MAP(pin,reg,bit) \
{ \
	.num=pin, \
	.name=#pin, \
	.out_en_reg_bit=GPIO_REG_BIT(reg,bit), \
	.out_value_reg_bit=GPIO_REG_BIT(reg,bit), \
	.input_value_reg_bit=GPIO_REG_BIT(reg,bit), \
}
#define PIN_AOMAP(pin,en_reg,en_bit,out_reg,out_bit,in_reg,in_bit) \
{ \
	.num=pin, \
	.name=#pin, \
	.out_en_reg_bit=GPIO_REG_BIT(en_reg,en_bit), \
	.out_value_reg_bit=GPIO_REG_BIT(out_reg,out_bit), \
	.input_value_reg_bit=GPIO_REG_BIT(in_reg,in_bit), \
	.gpio_owner=NULL, \
}

struct amlogic_gpio_desc amlogic_pins[] = {
	PIN_MAP(GPIOZ_0,  6, 0),
	PIN_MAP(GPIOZ_1,  6, 1),
	PIN_MAP(GPIOZ_2,  6, 2),
	PIN_MAP(GPIOZ_3,  6, 3),
	PIN_MAP(GPIOZ_4,  6, 4),
	PIN_MAP(GPIOZ_5,  6, 5),
	PIN_MAP(GPIOZ_6,  6, 6),
	PIN_MAP(GPIOZ_7,  6, 7),
	PIN_MAP(GPIOZ_8,  6, 8),
	PIN_MAP(GPIOZ_9,  6, 9),
	PIN_MAP(GPIOZ_10, 6, 10),
	PIN_MAP(GPIOZ_11, 6, 11),
	PIN_MAP(GPIOZ_12, 6, 12),
	PIN_MAP(GPIOZ_13, 6, 13),
	PIN_MAP(GPIOZ_14, 6, 14),
	PIN_MAP(GPIOZ_15, 6, 15),
	PIN_MAP(GPIOZ_16, 6, 16),
	PIN_MAP(GPIOZ_17, 6, 17),
	PIN_MAP(GPIOZ_18, 6, 18),
	PIN_MAP(GPIOZ_19, 6, 19),

	PIN_MAP(GPIOY_0,  2, 0),
	PIN_MAP(GPIOY_1,  2, 1),
	PIN_MAP(GPIOY_2,  2, 2),
	PIN_MAP(GPIOY_3,  2, 3),
	PIN_MAP(GPIOY_4,  2, 4),
	PIN_MAP(GPIOY_5,  2, 5),
	PIN_MAP(GPIOY_6,  2, 6),
	PIN_MAP(GPIOY_7,  2, 7),
	PIN_MAP(GPIOY_8,  2, 8),
	PIN_MAP(GPIOY_9,  2, 9),
	PIN_MAP(GPIOY_10, 2, 10),
	PIN_MAP(GPIOY_11, 2, 11),
	PIN_MAP(GPIOY_12, 2, 12),
	PIN_MAP(GPIOY_13, 2, 13),
	PIN_MAP(GPIOY_14, 2, 14),
	PIN_MAP(GPIOY_15, 2, 15),
	PIN_MAP(GPIOY_16, 2, 16),
	PIN_MAP(GPIOY_17, 2, 17),
	PIN_MAP(GPIOY_18, 2, 18),
	PIN_MAP(GPIOY_19, 2, 19),
	PIN_MAP(GPIOY_20, 2, 20),
	PIN_MAP(GPIOY_21, 2, 21),
	PIN_MAP(GPIOY_22, 2, 22),
	PIN_MAP(GPIOY_23, 2, 23),
	PIN_MAP(GPIOY_24, 2, 24),
	PIN_MAP(GPIOY_25, 2, 25),
	PIN_MAP(GPIOY_26, 2, 26),
	PIN_MAP(GPIOY_27, 2, 27),

	PIN_MAP(GPIOW_0,  5, 0),
	PIN_MAP(GPIOW_1,  5, 1),
	PIN_MAP(GPIOW_2,  5, 2),
	PIN_MAP(GPIOW_3,  5, 3),
	PIN_MAP(GPIOW_4,  5, 4),
	PIN_MAP(GPIOW_5,  5, 5),
	PIN_MAP(GPIOW_6,  5, 6),
	PIN_MAP(GPIOW_7,  5, 7),
	PIN_MAP(GPIOW_8,  5, 8),
	PIN_MAP(GPIOW_9,  5, 9),
	PIN_MAP(GPIOW_10, 5, 10),
	PIN_MAP(GPIOW_11, 5, 11),
	PIN_MAP(GPIOW_12, 5, 12),
	PIN_MAP(GPIOW_13, 5, 13),
	PIN_MAP(GPIOW_14, 5, 14),
	PIN_MAP(GPIOW_15, 5, 15),
	PIN_MAP(GPIOW_16, 5, 16),
	PIN_MAP(GPIOW_17, 5, 17),
	PIN_MAP(GPIOW_18, 5, 18),
	PIN_MAP(GPIOW_19, 5, 19),


	PIN_MAP(GPIOX_0,  4, 0),
	PIN_MAP(GPIOX_1,  4, 1),
	PIN_MAP(GPIOX_2,  4, 2),
	PIN_MAP(GPIOX_3,  4, 3),
	PIN_MAP(GPIOX_4,  4, 4),
	PIN_MAP(GPIOX_5,  4, 5),
	PIN_MAP(GPIOX_6,  4, 6),
	PIN_MAP(GPIOX_7,  4, 7),
	PIN_MAP(GPIOX_8,  4, 8),
	PIN_MAP(GPIOX_9,  4, 9),
	PIN_MAP(GPIOX_10, 4, 10),
	PIN_MAP(GPIOX_11, 4, 11),
	PIN_MAP(GPIOX_12, 4, 12),

	PIN_MAP(BOOT_0,  3, 0),
	PIN_MAP(BOOT_1,  3, 1),
	PIN_MAP(BOOT_2,  3, 2),
	PIN_MAP(BOOT_3,  3, 3),
	PIN_MAP(BOOT_4,  3, 4),
	PIN_MAP(BOOT_5,  3, 5),
	PIN_MAP(BOOT_6,  3, 6),
	PIN_MAP(BOOT_7,  3, 7),
	PIN_MAP(BOOT_8,  3, 8),
	PIN_MAP(BOOT_9,  3, 9),
	PIN_MAP(BOOT_10, 3, 10),
	PIN_MAP(BOOT_11, 3, 11),
	PIN_MAP(BOOT_12, 3, 12),
	PIN_MAP(BOOT_13, 3, 13),
	PIN_MAP(BOOT_14, 3, 14),
	PIN_MAP(BOOT_15, 3, 15),
	PIN_MAP(BOOT_16, 3, 16),
	PIN_MAP(BOOT_17, 3, 17),

	PIN_MAP(GPIOP_0,  1, 24),
	PIN_MAP(GPIOP_1,  1, 25),
	PIN_MAP(GPIOP_2,  1, 26),
	PIN_MAP(GPIOP_3,  1, 27),
	PIN_MAP(GPIOP_4,  1, 28),
	PIN_MAP(GPIOP_5,  1, 29),
	PIN_MAP(GPIOP_6,  1, 30),
	PIN_MAP(GPIOP_7,  1, 31),

	PIN_MAP(CARD_0,  5, 23),
	PIN_MAP(CARD_1,  5, 24),
	PIN_MAP(CARD_2,  5, 25),
	PIN_MAP(CARD_3,  5, 26),
	PIN_MAP(CARD_4,  5, 27),
	PIN_MAP(CARD_5,  5, 28),
	PIN_MAP(CARD_6,  5, 29),
	PIN_MAP(CARD_7,  5, 30),
	PIN_MAP(CARD_8,  5, 31),

	PIN_MAP(GPIOB_0,  1, 0),
	PIN_MAP(GPIOB_1,  1, 1),
	PIN_MAP(GPIOB_2,  1, 2),
	PIN_MAP(GPIOB_3,  1, 3),
	PIN_MAP(GPIOB_4,  1, 4),
	PIN_MAP(GPIOB_5,  1, 5),
	PIN_MAP(GPIOB_6,  1, 6),
	PIN_MAP(GPIOB_7,  1, 7),
	PIN_MAP(GPIOB_8,  1, 8),
	PIN_MAP(GPIOB_9,  1, 9),
	PIN_MAP(GPIOB_10, 1, 10),
	PIN_MAP(GPIOB_11, 1, 11),
	PIN_MAP(GPIOB_12, 1, 12),
	PIN_MAP(GPIOB_13, 1, 13),
	PIN_MAP(GPIOB_14, 1, 14),
	PIN_MAP(GPIOB_15, 1, 15),
	PIN_MAP(GPIOB_16, 1, 16),
	PIN_MAP(GPIOB_17, 1, 17),
	PIN_MAP(GPIOB_18, 1, 18),
	PIN_MAP(GPIOB_19, 1, 19),
	PIN_MAP(GPIOB_20, 1, 20),
	PIN_MAP(GPIOB_21, 1, 21),
	PIN_MAP(GPIOB_22, 1, 22),
	PIN_MAP(GPIOB_23, 1, 23),

	PIN_MAP(GPIOA_0,  0, 0),
	PIN_MAP(GPIOA_1,  0, 1),
	PIN_MAP(GPIOA_2,  0, 2),
	PIN_MAP(GPIOA_3,  0, 3),
	PIN_MAP(GPIOA_4,  0, 4),
	PIN_MAP(GPIOA_5,  0, 5),
	PIN_MAP(GPIOA_6,  0, 6),
	PIN_MAP(GPIOA_7,  0, 7),
	PIN_MAP(GPIOA_8,  0, 8),
	PIN_MAP(GPIOA_9,  0, 9),
	PIN_MAP(GPIOA_10, 0, 10),
	PIN_MAP(GPIOA_11, 0, 11),
	PIN_MAP(GPIOA_12, 0, 12),
	PIN_MAP(GPIOA_13, 0, 13),
	PIN_MAP(GPIOA_14, 0, 14),
	PIN_MAP(GPIOA_15, 0, 15),
	PIN_MAP(GPIOA_16, 0, 16),
	PIN_MAP(GPIOA_17, 0, 17),
	PIN_MAP(GPIOA_18, 0, 18),
	PIN_MAP(GPIOA_19, 0, 19),
	PIN_MAP(GPIOA_20, 0, 20),
	PIN_MAP(GPIOA_21, 0, 21),
	PIN_MAP(GPIOA_22, 0, 22),
	PIN_MAP(GPIOA_23, 0, 23),
	PIN_MAP(GPIOA_24, 0, 24),
	PIN_MAP(GPIOA_25, 0, 25),
	PIN_MAP(GPIOA_26, 0, 26),
	PIN_MAP(GPIOA_27, 0, 27),
	PIN_MAP(GPIOA_28, 0, 28),
	PIN_MAP(GPIOA_29, 0, 29),

	PIN_AOMAP(GPIOAO_0,  7, 0,  7, 16, 7, 0),
	PIN_AOMAP(GPIOAO_1,  7, 1,  7, 17, 7, 1),
	PIN_AOMAP(GPIOAO_2,  7, 2,  7, 18, 7, 2),
	PIN_AOMAP(GPIOAO_3,  7, 3,  7, 19, 7, 3),
	PIN_AOMAP(GPIOAO_4,  7, 4,  7, 20, 7, 4),
	PIN_AOMAP(GPIOAO_5,  7, 5,  7, 21, 7, 5),
	PIN_AOMAP(GPIOAO_6,  7, 6,  7, 22, 7, 6),
	PIN_AOMAP(GPIOAO_7,  7, 7,  7, 23, 7, 7),
	PIN_AOMAP(GPIOAO_8,  7, 8,  7, 24, 7, 8),
	PIN_AOMAP(GPIOAO_9,  7, 9,  7, 25, 7, 9),
	PIN_AOMAP(GPIOAO_10, 7, 10, 7, 26, 7, 10),
	PIN_AOMAP(GPIOAO_11, 7, 11, 7, 27, 7, 11),
};

static int m6tv_gpio_requst(struct gpio_chip *chip, unsigned offset)
{
	int ret;
	unsigned int i, reg, bit;

	ret = pinctrl_request_gpio(offset);
	if (!ret) {
		for (i = 0; i < MUX_SIZE; i++) {
			if (m6tv_pin_mux[offset][i] != NONE) {
				reg = GPIO_REG(m6tv_pin_mux[offset][i]);
				bit = GPIO_BIT(m6tv_pin_mux[offset][i]);
				aml_clr_reg32_mask(p_pin_mux_reg_addr[reg], 1<<bit);
			}
		}
	}
	return ret;
}

static void m6tv_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	pinctrl_free_gpio(offset);
	return;
}

static int m6tv_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	unsigned int reg,bit;
	reg = GPIO_REG(amlogic_pins[offset].out_en_reg_bit);
	bit = GPIO_BIT(amlogic_pins[offset].out_en_reg_bit);
	aml_set_reg32_mask(p_gpio_oen_addr[reg], 1<<bit);
	return 0;
}

static int m6tv_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	unsigned int reg,bit;
	reg = GPIO_REG(amlogic_pins[offset].input_value_reg_bit);
	bit = GPIO_BIT(amlogic_pins[offset].input_value_reg_bit);
	return aml_get_reg32_bits(p_gpio_input_addr[reg], bit, 1);
}

static int m6tv_gpio_direction_output(struct gpio_chip *chip, unsigned offset, int value)
{
	unsigned int reg,bit;
	reg = GPIO_REG(amlogic_pins[offset].out_value_reg_bit);
	bit = GPIO_BIT(amlogic_pins[offset].out_value_reg_bit);
	if (value)
		aml_set_reg32_mask(p_gpio_output_addr[reg], 1<<bit);
	else
		aml_clr_reg32_mask(p_gpio_output_addr[reg], 1<<bit);
	reg = GPIO_REG(amlogic_pins[offset].out_en_reg_bit);
	bit = GPIO_BIT(amlogic_pins[offset].out_en_reg_bit);
	aml_clr_reg32_mask(p_gpio_oen_addr[reg], 1<<bit);
	return 0;
}

static void m6tv_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	unsigned int reg,bit;
	reg=GPIO_REG(amlogic_pins[offset].out_value_reg_bit);
	bit=GPIO_BIT(amlogic_pins[offset].out_value_reg_bit);
	if (value)
		aml_set_reg32_mask(p_gpio_output_addr[reg], 1<<bit);
	else
		aml_clr_reg32_mask(p_gpio_output_addr[reg], 1<<bit);
}

/* defined in gpio-amlogic.c */
extern int gpio_flag;

static int m6tv_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	unsigned reg, start_bit;
	/* referrence AML_GPIO_IRQ used in device driver to set GPIO IRQ.
	 * AML_GPIO_IRQ(irq_bank,filter,type) ((irq_bank&0x7)|(filter&0x7)<<8|(type&0x3)<<16)
	 */
	unsigned irq_bank = gpio_flag & 0x7;
	unsigned filter   = (gpio_flag >> 8) & 0x7;
	unsigned irq_type = (gpio_flag >> 16) & 0x3;

	unsigned type[] = { 0x0, 	/*GPIO_IRQ_HIGH*/
			    0x10000,	/*GPIO_IRQ_LOW*/
			    0x1,	/*GPIO_IRQ_RISING*/
			    0x10001,	/*GPIO_IRQ_FALLING*/};

	/* set trigger type */
	aml_clrset_reg32_bits(P_GPIO_INTR_EDGE_POL,
			0x10001 << irq_bank,
			type[irq_type] << irq_bank);


	/* select pin */
	reg = irq_bank < 4 ? P_GPIO_INTR_GPIO_SEL0:P_GPIO_INTR_GPIO_SEL1;
	start_bit = (irq_bank & 3) * 8;
	aml_clrset_reg32_bits(reg, 0xff << start_bit, amlogic_pins[offset].num << start_bit);

	/* set filter */
	start_bit = (irq_bank)*4;
	aml_clrset_reg32_bits(P_GPIO_INTR_FILTER_SEL0, 0x7 << start_bit, filter << start_bit);

	return 0;
}

/*
 * extern
 */
int gpio_amlogic_name_to_num(const char *name)
{
	int i,tmp=100, num=0;
	int len = strlen(name);
	char *p = kzalloc(len+1, GFP_KERNEL);
	char *start = p;
	if(!p) {
		printk("%s:malloc error\n",__func__);
		return -1;
	}

	p = strcpy(p, name);
	for ( i= 0; i < len; p++, i++) {
		if (*p == '_') {
			*p='\0';
			tmp=i;
		}
		if (i > tmp && *p >= '0' && *p <= '9')
			num = num*10 + *p-'0';
	}

	p = start;

	if (!strcmp(p, "GPIOZ"))
		num = num + 0;
	else if (!strcmp(p, "GPIOY"))
		num = num + 20;
	else if(!strcmp(p, "GPIOW"))
		num = num + 48;
	else if(!strcmp(p, "GPIOX"))
		num = num + 68;
	else if(!strcmp(p, "BOOT"))
		num = num + 81;
	else if(!strcmp(p, "GPIOP"))
		num = num + 99;
	else if(!strcmp(p, "CARD"))
		num = num + 107;
	else if(!strcmp(p, "GPIOB"))
		num = num + 116;
	else if(!strcmp(p,"GPIOA"))
		num = num + 140;
	else if(!strcmp(p,"GPIOAO"))
		num = num + 170;
	else
		num = -1;
	kzfree(start);
	return num;
}

static struct gpio_chip m6tv_gpio_chip = {
	.request		= m6tv_gpio_requst,
	.free			= m6tv_gpio_free,
	.direction_input	= m6tv_gpio_direction_input,
	.get			= m6tv_gpio_get,
	.direction_output	= m6tv_gpio_direction_output,
	.set			= m6tv_gpio_set,
	.to_irq			= m6tv_gpio_to_irq,
};

static int m6tv_gpio_probe(struct platform_device *pdev)
{
#ifdef CONFIG_OF_GPIO
	m6tv_gpio_chip.of_node = pdev->dev.of_node;
#endif

	m6tv_gpio_chip.base = 0;
	m6tv_gpio_chip.ngpio = ARRAY_SIZE(amlogic_pins);
	gpiochip_add(&m6tv_gpio_chip);
	return 0;
}

static const struct of_device_id m6tv_gpio_match_table[] = {
	{
	.compatible = "amlogic,m6tv-gpio",
	},
	{ },
};

static struct platform_driver m6tv_gpio_driver = {
	.probe		= m6tv_gpio_probe,
	.driver		= {
		.name	= "amlogic_gpio",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(m6tv_gpio_match_table),
	},
};

static int __init m6tv_gpio_init(void)
{
	return platform_driver_register(&m6tv_gpio_driver);
}
postcore_initcall(m6tv_gpio_init);

