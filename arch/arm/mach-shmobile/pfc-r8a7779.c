/*
 * r8a7779 processor support - PFC hardware block
 *
 * Copyright (C) 2011  Renesas Solutions Corp.
 * Copyright (C) 2011  Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/ioport.h>
#include <mach/r8a7779.h>

#define CPU_32_PORT(fn, pfx, sfx)				\
	PORT_10(fn, pfx, sfx), PORT_10(fn, pfx##1, sfx),	\
	PORT_10(fn, pfx##2, sfx), PORT_1(fn, pfx##30, sfx),	\
	PORT_1(fn, pfx##31, sfx)

#define CPU_32_PORT6(fn, pfx, sfx)				\
	PORT_1(fn, pfx##0, sfx), PORT_1(fn, pfx##1, sfx),	\
	PORT_1(fn, pfx##2, sfx), PORT_1(fn, pfx##3, sfx),	\
	PORT_1(fn, pfx##4, sfx), PORT_1(fn, pfx##5, sfx),	\
	PORT_1(fn, pfx##6, sfx), PORT_1(fn, pfx##7, sfx),	\
	PORT_1(fn, pfx##8, sfx)

#define CPU_ALL_PORT(fn, pfx, sfx)				\
	CPU_32_PORT(fn, pfx##_0_, sfx),				\
	CPU_32_PORT(fn, pfx##_1_, sfx),				\
	CPU_32_PORT(fn, pfx##_2_, sfx),				\
	CPU_32_PORT(fn, pfx##_3_, sfx),				\
	CPU_32_PORT(fn, pfx##_4_, sfx),				\
	CPU_32_PORT(fn, pfx##_5_, sfx),				\
	CPU_32_PORT6(fn, pfx##_6_, sfx)

#define _GP_GPIO(pfx, sfx) PINMUX_GPIO(GPIO_GP##pfx, GP##pfx##_DATA)
#define _GP_DATA(pfx, sfx) PINMUX_DATA(GP##pfx##_DATA, GP##pfx##_FN,	\
				       GP##pfx##_IN, GP##pfx##_OUT)

#define _GP_INOUTSEL(pfx, sfx) GP##pfx##_IN, GP##pfx##_OUT
#define _GP_INDT(pfx, sfx) GP##pfx##_DATA

#define GP_ALL(str)	CPU_ALL_PORT(_PORT_ALL, GP, str)
#define PINMUX_GPIO_GP_ALL()	CPU_ALL_PORT(_GP_GPIO, , unused)
#define PINMUX_DATA_GP_ALL()	CPU_ALL_PORT(_GP_DATA, , unused)


#define PORT_10_REV(fn, pfx, sfx)				\
	PORT_1(fn, pfx##9, sfx), PORT_1(fn, pfx##8, sfx),	\
	PORT_1(fn, pfx##7, sfx), PORT_1(fn, pfx##6, sfx),	\
	PORT_1(fn, pfx##5, sfx), PORT_1(fn, pfx##4, sfx),	\
	PORT_1(fn, pfx##3, sfx), PORT_1(fn, pfx##2, sfx),	\
	PORT_1(fn, pfx##1, sfx), PORT_1(fn, pfx##0, sfx)

#define CPU_32_PORT_REV(fn, pfx, sfx)					\
	PORT_1(fn, pfx##31, sfx), PORT_1(fn, pfx##30, sfx),		\
	PORT_10_REV(fn, pfx##2, sfx), PORT_10_REV(fn, pfx##1, sfx),	\
	PORT_10_REV(fn, pfx, sfx)

#define GP_INOUTSEL(bank) CPU_32_PORT_REV(_GP_INOUTSEL, _##bank##_, unused)
#define GP_INDT(bank) CPU_32_PORT_REV(_GP_INDT, _##bank##_, unused)

enum {
	PINMUX_RESERVED = 0,

	PINMUX_DATA_BEGIN,
	GP_ALL(DATA), /* GP_0_0_DATA -> GP_6_8_DATA */
	PINMUX_DATA_END,

	PINMUX_INPUT_BEGIN,
	GP_ALL(IN), /* GP_0_0_IN -> GP_6_8_IN */
	PINMUX_INPUT_END,

	PINMUX_OUTPUT_BEGIN,
	GP_ALL(OUT), /* GP_0_0_OUT -> GP_6_8_OUT */
	PINMUX_OUTPUT_END,

	PINMUX_FUNCTION_BEGIN,
	GP_ALL(FN), /* GP_0_0_FN -> GP_6_8_FN */

	/* GPSR0 */
	FN_AVS1, FN_AVS2, FN_IP0_7_6, FN_A17,
	FN_A18, FN_A19, FN_IP0_9_8, FN_IP0_11_10,
	FN_IP0_13_12, FN_IP0_15_14, FN_IP0_18_16, FN_IP0_22_19,
	FN_IP0_24_23, FN_IP0_25, FN_IP0_27_26, FN_IP1_1_0,
	FN_IP1_3_2, FN_IP1_6_4, FN_IP1_10_7, FN_IP1_14_11,
	FN_IP1_18_15, FN_IP0_5_3, FN_IP0_30_28, FN_IP2_18_16,
	FN_IP2_21_19, FN_IP2_30_28, FN_IP3_2_0, FN_IP3_11_9,
	FN_IP3_14_12, FN_IP3_22_21, FN_IP3_26_24, FN_IP3_31_29,

	/* GPSR1 */
	FN_IP4_1_0, FN_IP4_4_2, FN_IP4_7_5, FN_IP4_10_8,
	FN_IP4_11, FN_IP4_12, FN_IP4_13, FN_IP4_14,
	FN_IP4_15, FN_IP4_16, FN_IP4_19_17, FN_IP4_22_20,
	FN_IP4_23, FN_IP4_24, FN_IP4_25, FN_IP4_26,
	FN_IP4_27, FN_IP4_28, FN_IP4_31_29, FN_IP5_2_0,
	FN_IP5_3, FN_IP5_4, FN_IP5_5, FN_IP5_6,
	FN_IP5_7, FN_IP5_8, FN_IP5_10_9, FN_IP5_12_11,
	FN_IP5_14_13, FN_IP5_16_15, FN_IP5_20_17, FN_IP5_23_21,

	/* GPSR2 */
	FN_IP5_27_24, FN_IP8_20, FN_IP8_22_21, FN_IP8_24_23,
	FN_IP8_27_25, FN_IP8_30_28, FN_IP9_1_0, FN_IP9_3_2,
	FN_IP9_4, FN_IP9_5, FN_IP9_6, FN_IP9_7,
	FN_IP9_9_8, FN_IP9_11_10, FN_IP9_13_12, FN_IP9_15_14,
	FN_IP9_18_16, FN_IP9_21_19, FN_IP9_23_22, FN_IP9_25_24,
	FN_IP9_27_26, FN_IP9_29_28, FN_IP10_2_0, FN_IP10_5_3,
	FN_IP10_8_6, FN_IP10_11_9, FN_IP10_14_12, FN_IP10_17_15,
	FN_IP10_20_18, FN_IP10_23_21, FN_IP10_25_24, FN_IP10_28_26,

	/* GPSR3 */
	FN_IP10_31_29, FN_IP11_2_0, FN_IP11_5_3, FN_IP11_8_6,
	FN_IP11_11_9, FN_IP11_14_12, FN_IP11_17_15, FN_IP11_20_18,
	FN_IP11_23_21, FN_IP11_26_24, FN_IP11_29_27, FN_IP12_2_0,
	FN_IP12_5_3, FN_IP12_8_6, FN_IP12_11_9, FN_IP12_14_12,
	FN_IP12_17_15, FN_IP7_16_15, FN_IP7_18_17, FN_IP7_28_27,
	FN_IP7_30_29, FN_IP7_20_19, FN_IP7_22_21, FN_IP7_24_23,
	FN_IP7_26_25, FN_IP1_20_19, FN_IP1_22_21, FN_IP1_24_23,
	FN_IP5_28, FN_IP5_30_29, FN_IP6_1_0, FN_IP6_3_2,

	/* GPSR4 */
	FN_IP6_5_4, FN_IP6_7_6, FN_IP6_8, FN_IP6_11_9,
	FN_IP6_14_12, FN_IP6_17_15, FN_IP6_19_18, FN_IP6_22_20,
	FN_IP6_24_23, FN_IP6_26_25, FN_IP6_30_29, FN_IP7_1_0,
	FN_IP7_3_2, FN_IP7_6_4, FN_IP7_9_7, FN_IP7_12_10,
	FN_IP7_14_13, FN_IP2_7_4, FN_IP2_11_8, FN_IP2_15_12,
	FN_IP1_28_25, FN_IP2_3_0, FN_IP8_3_0, FN_IP8_7_4,
	FN_IP8_11_8, FN_IP8_15_12, FN_PENC0, FN_PENC1,
	FN_IP0_2_0, FN_IP8_17_16, FN_IP8_18, FN_IP8_19,

	/* GPSR5 */
	FN_A1, FN_A2, FN_A3, FN_A4,
	FN_A5, FN_A6, FN_A7, FN_A8,
	FN_A9, FN_A10, FN_A11, FN_A12,
	FN_A13, FN_A14, FN_A15, FN_A16,
	FN_RD, FN_WE0, FN_WE1, FN_EX_WAIT0,
	FN_IP3_23, FN_IP3_27, FN_IP3_28, FN_IP2_22,
	FN_IP2_23, FN_IP2_24, FN_IP2_25, FN_IP2_26,
	FN_IP2_27, FN_IP3_3, FN_IP3_4, FN_IP3_5,

	/* GPSR6 */
	FN_IP3_6, FN_IP3_7, FN_IP3_8, FN_IP3_15,
	FN_IP3_16, FN_IP3_17, FN_IP3_18, FN_IP3_19,
	FN_IP3_20,
	PINMUX_FUNCTION_END,
};

static pinmux_enum_t pinmux_data[] = {
	PINMUX_DATA_GP_ALL(), /* PINMUX_DATA(GP_M_N_DATA, GP_M_N_FN...), */
};

static struct pinmux_gpio pinmux_gpios[] = {
	PINMUX_GPIO_GP_ALL(),
};

static struct pinmux_cfg_reg pinmux_config_regs[] = {
	{ PINMUX_CFG_REG("GPSR0", 0xfffc0004, 32, 1) {
		GP_0_31_FN, FN_IP3_31_29,
		GP_0_30_FN, FN_IP3_26_24,
		GP_0_29_FN, FN_IP3_22_21,
		GP_0_28_FN, FN_IP3_14_12,
		GP_0_27_FN, FN_IP3_11_9,
		GP_0_26_FN, FN_IP3_2_0,
		GP_0_25_FN, FN_IP2_30_28,
		GP_0_24_FN, FN_IP2_21_19,
		GP_0_23_FN, FN_IP2_18_16,
		GP_0_22_FN, FN_IP0_30_28,
		GP_0_21_FN, FN_IP0_5_3,
		GP_0_20_FN, FN_IP1_18_15,
		GP_0_19_FN, FN_IP1_14_11,
		GP_0_18_FN, FN_IP1_10_7,
		GP_0_17_FN, FN_IP1_6_4,
		GP_0_16_FN, FN_IP1_3_2,
		GP_0_15_FN, FN_IP1_1_0,
		GP_0_14_FN, FN_IP0_27_26,
		GP_0_13_FN, FN_IP0_25,
		GP_0_12_FN, FN_IP0_24_23,
		GP_0_11_FN, FN_IP0_22_19,
		GP_0_10_FN, FN_IP0_18_16,
		GP_0_9_FN, FN_IP0_15_14,
		GP_0_8_FN, FN_IP0_13_12,
		GP_0_7_FN, FN_IP0_11_10,
		GP_0_6_FN, FN_IP0_9_8,
		GP_0_5_FN, FN_A19,
		GP_0_4_FN, FN_A18,
		GP_0_3_FN, FN_A17,
		GP_0_2_FN, FN_IP0_7_6,
		GP_0_1_FN, FN_AVS2,
		GP_0_0_FN, FN_AVS1 }
	},
	{ PINMUX_CFG_REG("GPSR1", 0xfffc0008, 32, 1) {
		GP_1_31_FN, FN_IP5_23_21,
		GP_1_30_FN, FN_IP5_20_17,
		GP_1_29_FN, FN_IP5_16_15,
		GP_1_28_FN, FN_IP5_14_13,
		GP_1_27_FN, FN_IP5_12_11,
		GP_1_26_FN, FN_IP5_10_9,
		GP_1_25_FN, FN_IP5_8,
		GP_1_24_FN, FN_IP5_7,
		GP_1_23_FN, FN_IP5_6,
		GP_1_22_FN, FN_IP5_5,
		GP_1_21_FN, FN_IP5_4,
		GP_1_20_FN, FN_IP5_3,
		GP_1_19_FN, FN_IP5_2_0,
		GP_1_18_FN, FN_IP4_31_29,
		GP_1_17_FN, FN_IP4_28,
		GP_1_16_FN, FN_IP4_27,
		GP_1_15_FN, FN_IP4_26,
		GP_1_14_FN, FN_IP4_25,
		GP_1_13_FN, FN_IP4_24,
		GP_1_12_FN, FN_IP4_23,
		GP_1_11_FN, FN_IP4_22_20,
		GP_1_10_FN, FN_IP4_19_17,
		GP_1_9_FN, FN_IP4_16,
		GP_1_8_FN, FN_IP4_15,
		GP_1_7_FN, FN_IP4_14,
		GP_1_6_FN, FN_IP4_13,
		GP_1_5_FN, FN_IP4_12,
		GP_1_4_FN, FN_IP4_11,
		GP_1_3_FN, FN_IP4_10_8,
		GP_1_2_FN, FN_IP4_7_5,
		GP_1_1_FN, FN_IP4_4_2,
		GP_1_0_FN, FN_IP4_1_0 }
	},
	{ PINMUX_CFG_REG("GPSR2", 0xfffc000c, 32, 1) {
		GP_2_31_FN, FN_IP10_28_26,
		GP_2_30_FN, FN_IP10_25_24,
		GP_2_29_FN, FN_IP10_23_21,
		GP_2_28_FN, FN_IP10_20_18,
		GP_2_27_FN, FN_IP10_17_15,
		GP_2_26_FN, FN_IP10_14_12,
		GP_2_25_FN, FN_IP10_11_9,
		GP_2_24_FN, FN_IP10_8_6,
		GP_2_23_FN, FN_IP10_5_3,
		GP_2_22_FN, FN_IP10_2_0,
		GP_2_21_FN, FN_IP9_29_28,
		GP_2_20_FN, FN_IP9_27_26,
		GP_2_19_FN, FN_IP9_25_24,
		GP_2_18_FN, FN_IP9_23_22,
		GP_2_17_FN, FN_IP9_21_19,
		GP_2_16_FN, FN_IP9_18_16,
		GP_2_15_FN, FN_IP9_15_14,
		GP_2_14_FN, FN_IP9_13_12,
		GP_2_13_FN, FN_IP9_11_10,
		GP_2_12_FN, FN_IP9_9_8,
		GP_2_11_FN, FN_IP9_7,
		GP_2_10_FN, FN_IP9_6,
		GP_2_9_FN, FN_IP9_5,
		GP_2_8_FN, FN_IP9_4,
		GP_2_7_FN, FN_IP9_3_2,
		GP_2_6_FN, FN_IP9_1_0,
		GP_2_5_FN, FN_IP8_30_28,
		GP_2_4_FN, FN_IP8_27_25,
		GP_2_3_FN, FN_IP8_24_23,
		GP_2_2_FN, FN_IP8_22_21,
		GP_2_1_FN, FN_IP8_20,
		GP_2_0_FN, FN_IP5_27_24 }
	},
	{ PINMUX_CFG_REG("GPSR3", 0xfffc0010, 32, 1) {
		GP_3_31_FN, FN_IP6_3_2,
		GP_3_30_FN, FN_IP6_1_0,
		GP_3_29_FN, FN_IP5_30_29,
		GP_3_28_FN, FN_IP5_28,
		GP_3_27_FN, FN_IP1_24_23,
		GP_3_26_FN, FN_IP1_22_21,
		GP_3_25_FN, FN_IP1_20_19,
		GP_3_24_FN, FN_IP7_26_25,
		GP_3_23_FN, FN_IP7_24_23,
		GP_3_22_FN, FN_IP7_22_21,
		GP_3_21_FN, FN_IP7_20_19,
		GP_3_20_FN, FN_IP7_30_29,
		GP_3_19_FN, FN_IP7_28_27,
		GP_3_18_FN, FN_IP7_18_17,
		GP_3_17_FN, FN_IP7_16_15,
		GP_3_16_FN, FN_IP12_17_15,
		GP_3_15_FN, FN_IP12_14_12,
		GP_3_14_FN, FN_IP12_11_9,
		GP_3_13_FN, FN_IP12_8_6,
		GP_3_12_FN, FN_IP12_5_3,
		GP_3_11_FN, FN_IP12_2_0,
		GP_3_10_FN, FN_IP11_29_27,
		GP_3_9_FN, FN_IP11_26_24,
		GP_3_8_FN, FN_IP11_23_21,
		GP_3_7_FN, FN_IP11_20_18,
		GP_3_6_FN, FN_IP11_17_15,
		GP_3_5_FN, FN_IP11_14_12,
		GP_3_4_FN, FN_IP11_11_9,
		GP_3_3_FN, FN_IP11_8_6,
		GP_3_2_FN, FN_IP11_5_3,
		GP_3_1_FN, FN_IP11_2_0,
		GP_3_0_FN, FN_IP10_31_29 }
	},
	{ PINMUX_CFG_REG("GPSR4", 0xfffc0014, 32, 1) {
		GP_4_31_FN, FN_IP8_19,
		GP_4_30_FN, FN_IP8_18,
		GP_4_29_FN, FN_IP8_17_16,
		GP_4_28_FN, FN_IP0_2_0,
		GP_4_27_FN, FN_PENC1,
		GP_4_26_FN, FN_PENC0,
		GP_4_25_FN, FN_IP8_15_12,
		GP_4_24_FN, FN_IP8_11_8,
		GP_4_23_FN, FN_IP8_7_4,
		GP_4_22_FN, FN_IP8_3_0,
		GP_4_21_FN, FN_IP2_3_0,
		GP_4_20_FN, FN_IP1_28_25,
		GP_4_19_FN, FN_IP2_15_12,
		GP_4_18_FN, FN_IP2_11_8,
		GP_4_17_FN, FN_IP2_7_4,
		GP_4_16_FN, FN_IP7_14_13,
		GP_4_15_FN, FN_IP7_12_10,
		GP_4_14_FN, FN_IP7_9_7,
		GP_4_13_FN, FN_IP7_6_4,
		GP_4_12_FN, FN_IP7_3_2,
		GP_4_11_FN, FN_IP7_1_0,
		GP_4_10_FN, FN_IP6_30_29,
		GP_4_9_FN, FN_IP6_26_25,
		GP_4_8_FN, FN_IP6_24_23,
		GP_4_7_FN, FN_IP6_22_20,
		GP_4_6_FN, FN_IP6_19_18,
		GP_4_5_FN, FN_IP6_17_15,
		GP_4_4_FN, FN_IP6_14_12,
		GP_4_3_FN, FN_IP6_11_9,
		GP_4_2_FN, FN_IP6_8,
		GP_4_1_FN, FN_IP6_7_6,
		GP_4_0_FN, FN_IP6_5_4 }
	},
	{ PINMUX_CFG_REG("GPSR5", 0xfffc0018, 32, 1) {
		GP_5_31_FN, FN_IP3_5,
		GP_5_30_FN, FN_IP3_4,
		GP_5_29_FN, FN_IP3_3,
		GP_5_28_FN, FN_IP2_27,
		GP_5_27_FN, FN_IP2_26,
		GP_5_26_FN, FN_IP2_25,
		GP_5_25_FN, FN_IP2_24,
		GP_5_24_FN, FN_IP2_23,
		GP_5_23_FN, FN_IP2_22,
		GP_5_22_FN, FN_IP3_28,
		GP_5_21_FN, FN_IP3_27,
		GP_5_20_FN, FN_IP3_23,
		GP_5_19_FN, FN_EX_WAIT0,
		GP_5_18_FN, FN_WE1,
		GP_5_17_FN, FN_WE0,
		GP_5_16_FN, FN_RD,
		GP_5_15_FN, FN_A16,
		GP_5_14_FN, FN_A15,
		GP_5_13_FN, FN_A14,
		GP_5_12_FN, FN_A13,
		GP_5_11_FN, FN_A12,
		GP_5_10_FN, FN_A11,
		GP_5_9_FN, FN_A10,
		GP_5_8_FN, FN_A9,
		GP_5_7_FN, FN_A8,
		GP_5_6_FN, FN_A7,
		GP_5_5_FN, FN_A6,
		GP_5_4_FN, FN_A5,
		GP_5_3_FN, FN_A4,
		GP_5_2_FN, FN_A3,
		GP_5_1_FN, FN_A2,
		GP_5_0_FN, FN_A1 }
	},
	{ PINMUX_CFG_REG("GPSR6", 0xfffc001c, 32, 1) {
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0,
		0, 0,
		0, 0,
		GP_6_8_FN, FN_IP3_20,
		GP_6_7_FN, FN_IP3_19,
		GP_6_6_FN, FN_IP3_18,
		GP_6_5_FN, FN_IP3_17,
		GP_6_4_FN, FN_IP3_16,
		GP_6_3_FN, FN_IP3_15,
		GP_6_2_FN, FN_IP3_8,
		GP_6_1_FN, FN_IP3_7,
		GP_6_0_FN, FN_IP3_6 }
	},
	{ PINMUX_CFG_REG("INOUTSEL0", 0xffc40004, 32, 1) { GP_INOUTSEL(0) } },
	{ PINMUX_CFG_REG("INOUTSEL1", 0xffc41004, 32, 1) { GP_INOUTSEL(1) } },
	{ PINMUX_CFG_REG("INOUTSEL2", 0xffc42004, 32, 1) { GP_INOUTSEL(2) } },
	{ PINMUX_CFG_REG("INOUTSEL3", 0xffc43004, 32, 1) { GP_INOUTSEL(3) } },
	{ PINMUX_CFG_REG("INOUTSEL4", 0xffc44004, 32, 1) { GP_INOUTSEL(4) } },
	{ PINMUX_CFG_REG("INOUTSEL5", 0xffc45004, 32, 1) { GP_INOUTSEL(5) } },
	{ PINMUX_CFG_REG("INOUTSEL6", 0xffc46004, 32, 1) {
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0,
		0, 0,
		0, 0,
		GP_6_8_IN, GP_6_8_OUT,
		GP_6_7_IN, GP_6_7_OUT,
		GP_6_6_IN, GP_6_6_OUT,
		GP_6_5_IN, GP_6_5_OUT,
		GP_6_4_IN, GP_6_4_OUT,
		GP_6_3_IN, GP_6_3_OUT,
		GP_6_2_IN, GP_6_2_OUT,
		GP_6_1_IN, GP_6_1_OUT,
		GP_6_0_IN, GP_6_0_OUT, }
	},
	{ },
};

static struct pinmux_data_reg pinmux_data_regs[] = {
	{ PINMUX_DATA_REG("INDT0", 0xffc40008, 32) { GP_INDT(0) } },
	{ PINMUX_DATA_REG("INDT1", 0xffc41008, 32) { GP_INDT(1) } },
	{ PINMUX_DATA_REG("INDT2", 0xffc42008, 32) { GP_INDT(2) } },
	{ PINMUX_DATA_REG("INDT3", 0xffc43008, 32) { GP_INDT(3) } },
	{ PINMUX_DATA_REG("INDT4", 0xffc44008, 32) { GP_INDT(4) } },
	{ PINMUX_DATA_REG("INDT5", 0xffc45008, 32) { GP_INDT(5) } },
	{ PINMUX_DATA_REG("INDT6", 0xffc46008, 32) {
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, GP_6_8_DATA,
		GP_6_7_DATA, GP_6_6_DATA, GP_6_5_DATA, GP_6_4_DATA,
		GP_6_3_DATA, GP_6_2_DATA, GP_6_1_DATA, GP_6_0_DATA }
	},
	{ },
};

static struct resource r8a7779_pfc_resources[] = {
	[0] = {
		.start	= 0xfffc0000,
		.end	= 0xfffc023b,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 0xffc40000,
		.end	= 0xffc46fff,
		.flags	= IORESOURCE_MEM,
	}
};

static struct pinmux_info r8a7779_pinmux_info = {
	.name = "r8a7779_pfc",

	.resource = r8a7779_pfc_resources,
	.num_resources = ARRAY_SIZE(r8a7779_pfc_resources),

	.unlock_reg = 0xfffc0000, /* PMMR */

	.reserved_id = PINMUX_RESERVED,
	.data = { PINMUX_DATA_BEGIN, PINMUX_DATA_END },
	.input = { PINMUX_INPUT_BEGIN, PINMUX_INPUT_END },
	.output = { PINMUX_OUTPUT_BEGIN, PINMUX_OUTPUT_END },
	.function = { PINMUX_FUNCTION_BEGIN, PINMUX_FUNCTION_END },

	.first_gpio = GPIO_GP_0_0,
	.last_gpio = GPIO_GP_6_8,

	.gpios = pinmux_gpios,
	.cfg_regs = pinmux_config_regs,
	.data_regs = pinmux_data_regs,

	.gpio_data = pinmux_data,
	.gpio_data_size = ARRAY_SIZE(pinmux_data),
};

void r8a7779_pinmux_init(void)
{
	register_pinmux(&r8a7779_pinmux_info);
}
