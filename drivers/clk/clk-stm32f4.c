/*
 * Author: Daniel Thompson <daniel.thompson@linaro.org>
 *
 * Inspired by clk-asm9260.c .
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

/*
 * Include list of clocks wich are not derived from system clock (SYSCLOCK)
 * The index of these clocks is the secondary index of DT bindings
 *
 */
#include <dt-bindings/clock/stm32fx-clock.h>

#define STM32F4_RCC_CR			0x00
#define STM32F4_RCC_PLLCFGR		0x04
#define STM32F4_RCC_CFGR		0x08
#define STM32F4_RCC_AHB1ENR		0x30
#define STM32F4_RCC_AHB2ENR		0x34
#define STM32F4_RCC_AHB3ENR		0x38
#define STM32F4_RCC_APB1ENR		0x40
#define STM32F4_RCC_APB2ENR		0x44
#define STM32F4_RCC_BDCR		0x70
#define STM32F4_RCC_CSR			0x74
#define STM32F4_RCC_PLLI2SCFGR		0x84
#define STM32F4_RCC_PLLSAICFGR		0x88
#define STM32F4_RCC_DCKCFGR		0x8c
#define STM32F7_RCC_DCKCFGR2		0x90

#define NONE -1
#define NO_IDX  NONE
#define NO_MUX  NONE
#define NO_GATE NONE

struct stm32f4_gate_data {
	u8	offset;
	u8	bit_idx;
	const char *name;
	const char *parent_name;
	unsigned long flags;
};

static const struct stm32f4_gate_data stm32f429_gates[] __initconst = {
	{ STM32F4_RCC_AHB1ENR,  0,	"gpioa",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  1,	"gpiob",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  2,	"gpioc",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  3,	"gpiod",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  4,	"gpioe",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  5,	"gpiof",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  6,	"gpiog",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  7,	"gpioh",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  8,	"gpioi",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  9,	"gpioj",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 10,	"gpiok",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 12,	"crc",		"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 18,	"bkpsra",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 20,	"ccmdatam",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 21,	"dma1",		"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 22,	"dma2",		"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 23,	"dma2d",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 25,	"ethmac",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 26,	"ethmactx",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 27,	"ethmacrx",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 28,	"ethmacptp",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 29,	"otghs",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 30,	"otghsulpi",	"ahb_div" },

	{ STM32F4_RCC_AHB2ENR,  0,	"dcmi",		"ahb_div" },
	{ STM32F4_RCC_AHB2ENR,  4,	"cryp",		"ahb_div" },
	{ STM32F4_RCC_AHB2ENR,  5,	"hash",		"ahb_div" },
	{ STM32F4_RCC_AHB2ENR,  6,	"rng",		"pll48" },
	{ STM32F4_RCC_AHB2ENR,  7,	"otgfs",	"pll48" },

	{ STM32F4_RCC_AHB3ENR,  0,	"fmc",		"ahb_div",
		CLK_IGNORE_UNUSED },

	{ STM32F4_RCC_APB1ENR,  0,	"tim2",		"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  1,	"tim3",		"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  2,	"tim4",		"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  3,	"tim5",		"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  4,	"tim6",		"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  5,	"tim7",		"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  6,	"tim12",	"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  7,	"tim13",	"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  8,	"tim14",	"apb1_mul" },
	{ STM32F4_RCC_APB1ENR, 11,	"wwdg",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 14,	"spi2",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 15,	"spi3",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 17,	"uart2",	"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 18,	"uart3",	"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 19,	"uart4",	"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 20,	"uart5",	"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 21,	"i2c1",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 22,	"i2c2",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 23,	"i2c3",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 25,	"can1",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 26,	"can2",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 28,	"pwr",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 29,	"dac",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 30,	"uart7",	"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 31,	"uart8",	"apb1_div" },

	{ STM32F4_RCC_APB2ENR,  0,	"tim1",		"apb2_mul" },
	{ STM32F4_RCC_APB2ENR,  1,	"tim8",		"apb2_mul" },
	{ STM32F4_RCC_APB2ENR,  4,	"usart1",	"apb2_div" },
	{ STM32F4_RCC_APB2ENR,  5,	"usart6",	"apb2_div" },
	{ STM32F4_RCC_APB2ENR,  8,	"adc1",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR,  9,	"adc2",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 10,	"adc3",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 11,	"sdio",		"pll48" },
	{ STM32F4_RCC_APB2ENR, 12,	"spi1",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 13,	"spi4",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 14,	"syscfg",	"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 16,	"tim9",		"apb2_mul" },
	{ STM32F4_RCC_APB2ENR, 17,	"tim10",	"apb2_mul" },
	{ STM32F4_RCC_APB2ENR, 18,	"tim11",	"apb2_mul" },
	{ STM32F4_RCC_APB2ENR, 20,	"spi5",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 21,	"spi6",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 22,	"sai1",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 26,	"ltdc",		"apb2_div" },
};

static const struct stm32f4_gate_data stm32f469_gates[] __initconst = {
	{ STM32F4_RCC_AHB1ENR,  0,	"gpioa",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  1,	"gpiob",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  2,	"gpioc",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  3,	"gpiod",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  4,	"gpioe",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  5,	"gpiof",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  6,	"gpiog",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  7,	"gpioh",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  8,	"gpioi",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  9,	"gpioj",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 10,	"gpiok",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 12,	"crc",		"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 18,	"bkpsra",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 20,	"ccmdatam",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 21,	"dma1",		"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 22,	"dma2",		"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 23,	"dma2d",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 25,	"ethmac",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 26,	"ethmactx",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 27,	"ethmacrx",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 28,	"ethmacptp",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 29,	"otghs",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 30,	"otghsulpi",	"ahb_div" },

	{ STM32F4_RCC_AHB2ENR,  0,	"dcmi",		"ahb_div" },
	{ STM32F4_RCC_AHB2ENR,  4,	"cryp",		"ahb_div" },
	{ STM32F4_RCC_AHB2ENR,  5,	"hash",		"ahb_div" },
	{ STM32F4_RCC_AHB2ENR,  6,	"rng",		"pll48" },
	{ STM32F4_RCC_AHB2ENR,  7,	"otgfs",	"pll48" },

	{ STM32F4_RCC_AHB3ENR,  0,	"fmc",		"ahb_div",
		CLK_IGNORE_UNUSED },
	{ STM32F4_RCC_AHB3ENR,  1,	"qspi",		"ahb_div",
		CLK_IGNORE_UNUSED },

	{ STM32F4_RCC_APB1ENR,  0,	"tim2",		"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  1,	"tim3",		"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  2,	"tim4",		"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  3,	"tim5",		"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  4,	"tim6",		"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  5,	"tim7",		"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  6,	"tim12",	"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  7,	"tim13",	"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  8,	"tim14",	"apb1_mul" },
	{ STM32F4_RCC_APB1ENR, 11,	"wwdg",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 14,	"spi2",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 15,	"spi3",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 17,	"uart2",	"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 18,	"uart3",	"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 19,	"uart4",	"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 20,	"uart5",	"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 21,	"i2c1",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 22,	"i2c2",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 23,	"i2c3",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 25,	"can1",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 26,	"can2",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 28,	"pwr",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 29,	"dac",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 30,	"uart7",	"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 31,	"uart8",	"apb1_div" },

	{ STM32F4_RCC_APB2ENR,  0,	"tim1",		"apb2_mul" },
	{ STM32F4_RCC_APB2ENR,  1,	"tim8",		"apb2_mul" },
	{ STM32F4_RCC_APB2ENR,  4,	"usart1",	"apb2_div" },
	{ STM32F4_RCC_APB2ENR,  5,	"usart6",	"apb2_div" },
	{ STM32F4_RCC_APB2ENR,  8,	"adc1",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR,  9,	"adc2",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 10,	"adc3",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 11,	"sdio",		"sdmux" },
	{ STM32F4_RCC_APB2ENR, 12,	"spi1",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 13,	"spi4",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 14,	"syscfg",	"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 16,	"tim9",		"apb2_mul" },
	{ STM32F4_RCC_APB2ENR, 17,	"tim10",	"apb2_mul" },
	{ STM32F4_RCC_APB2ENR, 18,	"tim11",	"apb2_mul" },
	{ STM32F4_RCC_APB2ENR, 20,	"spi5",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 21,	"spi6",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 22,	"sai1",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 26,	"ltdc",		"apb2_div" },
};

static const struct stm32f4_gate_data stm32f746_gates[] __initconst = {
	{ STM32F4_RCC_AHB1ENR,  0,	"gpioa",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  1,	"gpiob",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  2,	"gpioc",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  3,	"gpiod",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  4,	"gpioe",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  5,	"gpiof",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  6,	"gpiog",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  7,	"gpioh",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  8,	"gpioi",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  9,	"gpioj",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 10,	"gpiok",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 12,	"crc",		"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 18,	"bkpsra",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 20,	"dtcmram",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 21,	"dma1",		"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 22,	"dma2",		"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 23,	"dma2d",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 25,	"ethmac",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 26,	"ethmactx",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 27,	"ethmacrx",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 28,	"ethmacptp",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 29,	"otghs",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 30,	"otghsulpi",	"ahb_div" },

	{ STM32F4_RCC_AHB2ENR,  0,	"dcmi",		"ahb_div" },
	{ STM32F4_RCC_AHB2ENR,  4,	"cryp",		"ahb_div" },
	{ STM32F4_RCC_AHB2ENR,  5,	"hash",		"ahb_div" },
	{ STM32F4_RCC_AHB2ENR,  6,	"rng",		"pll48"   },
	{ STM32F4_RCC_AHB2ENR,  7,	"otgfs",	"pll48"   },

	{ STM32F4_RCC_AHB3ENR,  0,	"fmc",		"ahb_div",
		CLK_IGNORE_UNUSED },
	{ STM32F4_RCC_AHB3ENR,  1,	"qspi",		"ahb_div",
		CLK_IGNORE_UNUSED },

	{ STM32F4_RCC_APB1ENR,  0,	"tim2",		"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  1,	"tim3",		"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  2,	"tim4",		"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  3,	"tim5",		"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  4,	"tim6",		"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  5,	"tim7",		"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  6,	"tim12",	"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  7,	"tim13",	"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  8,	"tim14",	"apb1_mul" },
	{ STM32F4_RCC_APB1ENR, 11,	"wwdg",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 14,	"spi2",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 15,	"spi3",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 16,	"spdifrx",	"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 25,	"can1",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 26,	"can2",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 27,	"cec",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 28,	"pwr",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 29,	"dac",		"apb1_div" },

	{ STM32F4_RCC_APB2ENR,  0,	"tim1",		"apb2_mul" },
	{ STM32F4_RCC_APB2ENR,  1,	"tim8",		"apb2_mul" },
	{ STM32F4_RCC_APB2ENR,  8,	"adc1",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR,  9,	"adc2",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 10,	"adc3",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 11,	"sdmmc",	"sdmux"    },
	{ STM32F4_RCC_APB2ENR, 12,	"spi1",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 13,	"spi4",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 14,	"syscfg",	"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 16,	"tim9",		"apb2_mul" },
	{ STM32F4_RCC_APB2ENR, 17,	"tim10",	"apb2_mul" },
	{ STM32F4_RCC_APB2ENR, 18,	"tim11",	"apb2_mul" },
	{ STM32F4_RCC_APB2ENR, 20,	"spi5",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 21,	"spi6",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 22,	"sai1",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 23,	"sai2",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 26,	"ltdc",		"apb2_div" },
};

/*
 * This bitmask tells us which bit offsets (0..192) on STM32F4[23]xxx
 * have gate bits associated with them. Its combined hweight is 71.
 */
#define MAX_GATE_MAP 3

static const u64 stm32f42xx_gate_map[MAX_GATE_MAP] = { 0x000000f17ef417ffull,
						       0x0000000000000001ull,
						       0x04777f33f6fec9ffull };

static const u64 stm32f46xx_gate_map[MAX_GATE_MAP] = { 0x000000f17ef417ffull,
						       0x0000000000000003ull,
						       0x0c777f33f6fec9ffull };

static const u64 stm32f746_gate_map[MAX_GATE_MAP] = { 0x000000f17ef417ffull,
						      0x0000000000000003ull,
						      0x04f77f033e01c9ffull };

static const u64 *stm32f4_gate_map;

static struct clk_hw **clks;

static DEFINE_SPINLOCK(stm32f4_clk_lock);
static void __iomem *base;

static struct regmap *pdrm;

static int stm32fx_end_primary_clk;

/*
 * "Multiplier" device for APBx clocks.
 *
 * The APBx dividers are power-of-two dividers and, if *not* running in 1:1
 * mode, they also tap out the one of the low order state bits to run the
 * timers. ST datasheets represent this feature as a (conditional) clock
 * multiplier.
 */
struct clk_apb_mul {
	struct clk_hw hw;
	u8 bit_idx;
};

#define to_clk_apb_mul(_hw) container_of(_hw, struct clk_apb_mul, hw)

static unsigned long clk_apb_mul_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct clk_apb_mul *am = to_clk_apb_mul(hw);

	if (readl(base + STM32F4_RCC_CFGR) & BIT(am->bit_idx))
		return parent_rate * 2;

	return parent_rate;
}

static long clk_apb_mul_round_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long *prate)
{
	struct clk_apb_mul *am = to_clk_apb_mul(hw);
	unsigned long mult = 1;

	if (readl(base + STM32F4_RCC_CFGR) & BIT(am->bit_idx))
		mult = 2;

	if (clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT) {
		unsigned long best_parent = rate / mult;

		*prate = clk_hw_round_rate(clk_hw_get_parent(hw), best_parent);
	}

	return *prate * mult;
}

static int clk_apb_mul_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	/*
	 * We must report success but we can do so unconditionally because
	 * clk_apb_mul_round_rate returns values that ensure this call is a
	 * nop.
	 */

	return 0;
}

static const struct clk_ops clk_apb_mul_factor_ops = {
	.round_rate = clk_apb_mul_round_rate,
	.set_rate = clk_apb_mul_set_rate,
	.recalc_rate = clk_apb_mul_recalc_rate,
};

static struct clk *clk_register_apb_mul(struct device *dev, const char *name,
					const char *parent_name,
					unsigned long flags, u8 bit_idx)
{
	struct clk_apb_mul *am;
	struct clk_init_data init;
	struct clk *clk;

	am = kzalloc(sizeof(*am), GFP_KERNEL);
	if (!am)
		return ERR_PTR(-ENOMEM);

	am->bit_idx = bit_idx;
	am->hw.init = &init;

	init.name = name;
	init.ops = &clk_apb_mul_factor_ops;
	init.flags = flags;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	clk = clk_register(dev, &am->hw);

	if (IS_ERR(clk))
		kfree(am);

	return clk;
}

enum {
	PLL,
	PLL_I2S,
	PLL_SAI,
};

static const struct clk_div_table pll_divp_table[] = {
	{ 0, 2 }, { 1, 4 }, { 2, 6 }, { 3, 8 }, { 0 }
};

static const struct clk_div_table pll_divr_table[] = {
	{ 2, 2 }, { 3, 3 }, { 4, 4 }, { 5, 5 }, { 6, 6 }, { 7, 7 }, { 0 }
};

struct stm32f4_pll {
	spinlock_t *lock;
	struct	clk_gate gate;
	u8 offset;
	u8 bit_rdy_idx;
	u8 status;
	u8 n_start;
};

#define to_stm32f4_pll(_gate) container_of(_gate, struct stm32f4_pll, gate)

struct stm32f4_pll_post_div_data {
	int idx;
	u8 pll_num;
	const char *name;
	const char *parent;
	u8 flag;
	u8 offset;
	u8 shift;
	u8 width;
	u8 flag_div;
	const struct clk_div_table *div_table;
};

struct stm32f4_vco_data {
	const char *vco_name;
	u8 offset;
	u8 bit_idx;
	u8 bit_rdy_idx;
};

static const struct stm32f4_vco_data  vco_data[] = {
	{ "vco",     STM32F4_RCC_PLLCFGR,    24, 25 },
	{ "vco-i2s", STM32F4_RCC_PLLI2SCFGR, 26, 27 },
	{ "vco-sai", STM32F4_RCC_PLLSAICFGR, 28, 29 },
};


static const struct clk_div_table post_divr_table[] = {
	{ 0, 2 }, { 1, 4 }, { 2, 8 }, { 3, 16 }, { 0 }
};

#define MAX_POST_DIV 3
static const struct stm32f4_pll_post_div_data  post_div_data[MAX_POST_DIV] = {
	{ CLK_I2SQ_PDIV, PLL_I2S, "plli2s-q-div", "plli2s-q",
		CLK_SET_RATE_PARENT, STM32F4_RCC_DCKCFGR, 0, 5, 0, NULL},

	{ CLK_SAIQ_PDIV, PLL_SAI, "pllsai-q-div", "pllsai-q",
		CLK_SET_RATE_PARENT, STM32F4_RCC_DCKCFGR, 8, 5, 0, NULL },

	{ NO_IDX, PLL_SAI, "pllsai-r-div", "pllsai-r", CLK_SET_RATE_PARENT,
		STM32F4_RCC_DCKCFGR, 16, 2, 0, post_divr_table },
};

struct stm32f4_div_data {
	u8 shift;
	u8 width;
	u8 flag_div;
	const struct clk_div_table *div_table;
};

#define MAX_PLL_DIV 3
static const struct stm32f4_div_data  div_data[MAX_PLL_DIV] = {
	{ 16, 2, 0,			pll_divp_table	},
	{ 24, 4, CLK_DIVIDER_ONE_BASED, NULL		},
	{ 28, 3, 0,			pll_divr_table	},
};

struct stm32f4_pll_data {
	u8 pll_num;
	u8 n_start;
	const char *div_name[MAX_PLL_DIV];
};

static const struct stm32f4_pll_data stm32f429_pll[MAX_PLL_DIV] = {
	{ PLL,	   192, { "pll", "pll48",    NULL	} },
	{ PLL_I2S, 192, { NULL,  "plli2s-q", "plli2s-r" } },
	{ PLL_SAI,  49, { NULL,  "pllsai-q", "pllsai-r" } },
};

static const struct stm32f4_pll_data stm32f469_pll[MAX_PLL_DIV] = {
	{ PLL,	   50, { "pll",	     "pll-q",    NULL	    } },
	{ PLL_I2S, 50, { "plli2s-p", "plli2s-q", "plli2s-r" } },
	{ PLL_SAI, 50, { "pllsai-p", "pllsai-q", "pllsai-r" } },
};

static int stm32f4_pll_is_enabled(struct clk_hw *hw)
{
	return clk_gate_ops.is_enabled(hw);
}

static int stm32f4_pll_enable(struct clk_hw *hw)
{
	struct clk_gate *gate = to_clk_gate(hw);
	struct stm32f4_pll *pll = to_stm32f4_pll(gate);
	int ret = 0;
	unsigned long reg;

	ret = clk_gate_ops.enable(hw);

	ret = readl_relaxed_poll_timeout_atomic(base + STM32F4_RCC_CR, reg,
			reg & (1 << pll->bit_rdy_idx), 0, 10000);

	return ret;
}

static void stm32f4_pll_disable(struct clk_hw *hw)
{
	clk_gate_ops.disable(hw);
}

static unsigned long stm32f4_pll_recalc(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clk_gate *gate = to_clk_gate(hw);
	struct stm32f4_pll *pll = to_stm32f4_pll(gate);
	unsigned long n;

	n = (readl(base + pll->offset) >> 6) & 0x1ff;

	return parent_rate * n;
}

static long stm32f4_pll_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	struct clk_gate *gate = to_clk_gate(hw);
	struct stm32f4_pll *pll = to_stm32f4_pll(gate);
	unsigned long n;

	n = rate / *prate;

	if (n < pll->n_start)
		n = pll->n_start;
	else if (n > 432)
		n = 432;

	return *prate * n;
}

static int stm32f4_pll_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct clk_gate *gate = to_clk_gate(hw);
	struct stm32f4_pll *pll = to_stm32f4_pll(gate);

	unsigned long n;
	unsigned long val;
	int pll_state;

	pll_state = stm32f4_pll_is_enabled(hw);

	if (pll_state)
		stm32f4_pll_disable(hw);

	n = rate  / parent_rate;

	val = readl(base + pll->offset) & ~(0x1ff << 6);

	writel(val | ((n & 0x1ff) <<  6), base + pll->offset);

	if (pll_state)
		stm32f4_pll_enable(hw);

	return 0;
}

static const struct clk_ops stm32f4_pll_gate_ops = {
	.enable		= stm32f4_pll_enable,
	.disable	= stm32f4_pll_disable,
	.is_enabled	= stm32f4_pll_is_enabled,
	.recalc_rate	= stm32f4_pll_recalc,
	.round_rate	= stm32f4_pll_round_rate,
	.set_rate	= stm32f4_pll_set_rate,
};

struct stm32f4_pll_div {
	struct clk_divider div;
	struct clk_hw *hw_pll;
};

#define to_pll_div_clk(_div) container_of(_div, struct stm32f4_pll_div, div)

static unsigned long stm32f4_pll_div_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	return clk_divider_ops.recalc_rate(hw, parent_rate);
}

static long stm32f4_pll_div_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	return clk_divider_ops.round_rate(hw, rate, prate);
}

static int stm32f4_pll_div_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	int pll_state, ret;

	struct clk_divider *div = to_clk_divider(hw);
	struct stm32f4_pll_div *pll_div = to_pll_div_clk(div);

	pll_state = stm32f4_pll_is_enabled(pll_div->hw_pll);

	if (pll_state)
		stm32f4_pll_disable(pll_div->hw_pll);

	ret = clk_divider_ops.set_rate(hw, rate, parent_rate);

	if (pll_state)
		stm32f4_pll_enable(pll_div->hw_pll);

	return ret;
}

static const struct clk_ops stm32f4_pll_div_ops = {
	.recalc_rate = stm32f4_pll_div_recalc_rate,
	.round_rate = stm32f4_pll_div_round_rate,
	.set_rate = stm32f4_pll_div_set_rate,
};

static struct clk_hw *clk_register_pll_div(const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 shift, u8 width,
		u8 clk_divider_flags, const struct clk_div_table *table,
		struct clk_hw *pll_hw, spinlock_t *lock)
{
	struct stm32f4_pll_div *pll_div;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	/* allocate the divider */
	pll_div = kzalloc(sizeof(*pll_div), GFP_KERNEL);
	if (!pll_div)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &stm32f4_pll_div_ops;
	init.flags = flags;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);

	/* struct clk_divider assignments */
	pll_div->div.reg = reg;
	pll_div->div.shift = shift;
	pll_div->div.width = width;
	pll_div->div.flags = clk_divider_flags;
	pll_div->div.lock = lock;
	pll_div->div.table = table;
	pll_div->div.hw.init = &init;

	pll_div->hw_pll = pll_hw;

	/* register the clock */
	hw = &pll_div->div.hw;
	ret = clk_hw_register(NULL, hw);
	if (ret) {
		kfree(pll_div);
		hw = ERR_PTR(ret);
	}

	return hw;
}

static struct clk_hw *stm32f4_rcc_register_pll(const char *pllsrc,
		const struct stm32f4_pll_data *data,  spinlock_t *lock)
{
	struct stm32f4_pll *pll;
	struct clk_init_data init = { NULL };
	void __iomem *reg;
	struct clk_hw *pll_hw;
	int ret;
	int i;
	const struct stm32f4_vco_data *vco;


	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	vco = &vco_data[data->pll_num];

	init.name = vco->vco_name;
	init.ops = &stm32f4_pll_gate_ops;
	init.flags = CLK_SET_RATE_GATE;
	init.parent_names = &pllsrc;
	init.num_parents = 1;

	pll->gate.lock = lock;
	pll->gate.reg = base + STM32F4_RCC_CR;
	pll->gate.bit_idx = vco->bit_idx;
	pll->gate.hw.init = &init;

	pll->offset = vco->offset;
	pll->n_start = data->n_start;
	pll->bit_rdy_idx = vco->bit_rdy_idx;
	pll->status = (readl(base + STM32F4_RCC_CR) >> vco->bit_idx) & 0x1;

	reg = base + pll->offset;

	pll_hw = &pll->gate.hw;
	ret = clk_hw_register(NULL, pll_hw);
	if (ret) {
		kfree(pll);
		return ERR_PTR(ret);
	}

	for (i = 0; i < MAX_PLL_DIV; i++)
		if (data->div_name[i])
			clk_register_pll_div(data->div_name[i],
					vco->vco_name,
					0,
					reg,
					div_data[i].shift,
					div_data[i].width,
					div_data[i].flag_div,
					div_data[i].div_table,
					pll_hw,
					lock);
	return pll_hw;
}

/*
 * Converts the primary and secondary indices (as they appear in DT) to an
 * offset into our struct clock array.
 */
static int stm32f4_rcc_lookup_clk_idx(u8 primary, u8 secondary)
{
	u64 table[MAX_GATE_MAP];

	if (primary == 1) {
		if (WARN_ON(secondary >= stm32fx_end_primary_clk))
			return -EINVAL;
		return secondary;
	}

	memcpy(table, stm32f4_gate_map, sizeof(table));

	/* only bits set in table can be used as indices */
	if (WARN_ON(secondary >= BITS_PER_BYTE * sizeof(table) ||
		    0 == (table[BIT_ULL_WORD(secondary)] &
			  BIT_ULL_MASK(secondary))))
		return -EINVAL;

	/* mask out bits above our current index */
	table[BIT_ULL_WORD(secondary)] &=
	    GENMASK_ULL(secondary % BITS_PER_LONG_LONG, 0);

	return stm32fx_end_primary_clk - 1 + hweight64(table[0]) +
	       (BIT_ULL_WORD(secondary) >= 1 ? hweight64(table[1]) : 0) +
	       (BIT_ULL_WORD(secondary) >= 2 ? hweight64(table[2]) : 0);
}

static struct clk_hw *
stm32f4_rcc_lookup_clk(struct of_phandle_args *clkspec, void *data)
{
	int i = stm32f4_rcc_lookup_clk_idx(clkspec->args[0], clkspec->args[1]);

	if (i < 0)
		return ERR_PTR(-EINVAL);

	return clks[i];
}

#define to_rgclk(_rgate) container_of(_rgate, struct stm32_rgate, gate)

static inline void disable_power_domain_write_protection(void)
{
	if (pdrm)
		regmap_update_bits(pdrm, 0x00, (1 << 8), (1 << 8));
}

static inline void enable_power_domain_write_protection(void)
{
	if (pdrm)
		regmap_update_bits(pdrm, 0x00, (1 << 8), (0 << 8));
}

static inline void sofware_reset_backup_domain(void)
{
	unsigned long val;

	val = readl(base + STM32F4_RCC_BDCR);
	writel(val | BIT(16), base + STM32F4_RCC_BDCR);
	writel(val & ~BIT(16), base + STM32F4_RCC_BDCR);
}

struct stm32_rgate {
	struct	clk_gate gate;
	u8	bit_rdy_idx;
};

#define RTC_TIMEOUT 1000000

static int rgclk_enable(struct clk_hw *hw)
{
	struct clk_gate *gate = to_clk_gate(hw);
	struct stm32_rgate *rgate = to_rgclk(gate);
	u32 reg;
	int ret;

	disable_power_domain_write_protection();

	clk_gate_ops.enable(hw);

	ret = readl_relaxed_poll_timeout_atomic(gate->reg, reg,
			reg & rgate->bit_rdy_idx, 1000, RTC_TIMEOUT);

	enable_power_domain_write_protection();
	return ret;
}

static void rgclk_disable(struct clk_hw *hw)
{
	clk_gate_ops.disable(hw);
}

static int rgclk_is_enabled(struct clk_hw *hw)
{
	return clk_gate_ops.is_enabled(hw);
}

static const struct clk_ops rgclk_ops = {
	.enable = rgclk_enable,
	.disable = rgclk_disable,
	.is_enabled = rgclk_is_enabled,
};

static struct clk_hw *clk_register_rgate(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 bit_idx, u8 bit_rdy_idx,
		u8 clk_gate_flags, spinlock_t *lock)
{
	struct stm32_rgate *rgate;
	struct clk_init_data init = { NULL };
	struct clk_hw *hw;
	int ret;

	rgate = kzalloc(sizeof(*rgate), GFP_KERNEL);
	if (!rgate)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &rgclk_ops;
	init.flags = flags;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	rgate->bit_rdy_idx = bit_rdy_idx;

	rgate->gate.lock = lock;
	rgate->gate.reg = reg;
	rgate->gate.bit_idx = bit_idx;
	rgate->gate.hw.init = &init;

	hw = &rgate->gate.hw;
	ret = clk_hw_register(dev, hw);
	if (ret) {
		kfree(rgate);
		hw = ERR_PTR(ret);
	}

	return hw;
}

static int cclk_gate_enable(struct clk_hw *hw)
{
	int ret;

	disable_power_domain_write_protection();

	ret = clk_gate_ops.enable(hw);

	enable_power_domain_write_protection();

	return ret;
}

static void cclk_gate_disable(struct clk_hw *hw)
{
	disable_power_domain_write_protection();

	clk_gate_ops.disable(hw);

	enable_power_domain_write_protection();
}

static int cclk_gate_is_enabled(struct clk_hw *hw)
{
	return clk_gate_ops.is_enabled(hw);
}

static const struct clk_ops cclk_gate_ops = {
	.enable		= cclk_gate_enable,
	.disable	= cclk_gate_disable,
	.is_enabled	= cclk_gate_is_enabled,
};

static u8 cclk_mux_get_parent(struct clk_hw *hw)
{
	return clk_mux_ops.get_parent(hw);
}

static int cclk_mux_set_parent(struct clk_hw *hw, u8 index)
{
	int ret;

	disable_power_domain_write_protection();

	sofware_reset_backup_domain();

	ret = clk_mux_ops.set_parent(hw, index);

	enable_power_domain_write_protection();

	return ret;
}

static const struct clk_ops cclk_mux_ops = {
	.get_parent = cclk_mux_get_parent,
	.set_parent = cclk_mux_set_parent,
};

static struct clk_hw *stm32_register_cclk(struct device *dev, const char *name,
		const char * const *parent_names, int num_parents,
		void __iomem *reg, u8 bit_idx, u8 shift, unsigned long flags,
		spinlock_t *lock)
{
	struct clk_hw *hw;
	struct clk_gate *gate;
	struct clk_mux *mux;

	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate) {
		hw = ERR_PTR(-EINVAL);
		goto fail;
	}

	mux = kzalloc(sizeof(*mux), GFP_KERNEL);
	if (!mux) {
		kfree(gate);
		hw = ERR_PTR(-EINVAL);
		goto fail;
	}

	gate->reg = reg;
	gate->bit_idx = bit_idx;
	gate->flags = 0;
	gate->lock = lock;

	mux->reg = reg;
	mux->shift = shift;
	mux->mask = 3;
	mux->flags = 0;

	hw = clk_hw_register_composite(dev, name, parent_names, num_parents,
			&mux->hw, &cclk_mux_ops,
			NULL, NULL,
			&gate->hw, &cclk_gate_ops,
			flags);

	if (IS_ERR(hw)) {
		kfree(gate);
		kfree(mux);
	}

fail:
	return hw;
}

static const char *sys_parents[] __initdata =   { "hsi", NULL, "pll" };

static const struct clk_div_table ahb_div_table[] = {
	{ 0x0,   1 }, { 0x1,   1 }, { 0x2,   1 }, { 0x3,   1 },
	{ 0x4,   1 }, { 0x5,   1 }, { 0x6,   1 }, { 0x7,   1 },
	{ 0x8,   2 }, { 0x9,   4 }, { 0xa,   8 }, { 0xb,  16 },
	{ 0xc,  64 }, { 0xd, 128 }, { 0xe, 256 }, { 0xf, 512 },
	{ 0 },
};

static const struct clk_div_table apb_div_table[] = {
	{ 0,  1 }, { 0,  1 }, { 0,  1 }, { 0,  1 },
	{ 4,  2 }, { 5,  4 }, { 6,  8 }, { 7, 16 },
	{ 0 },
};

static const char *rtc_parents[4] = {
	"no-clock", "lse", "lsi", "hse-rtc"
};

static const char *lcd_parent[1] = { "pllsai-r-div" };

static const char *i2s_parents[2] = { "plli2s-r", NULL };

static const char *sai_parents[4] = { "pllsai-q-div", "plli2s-q-div", NULL,
	"no-clock" };

static const char *pll48_parents[2] = { "pll-q", "pllsai-p" };

static const char *sdmux_parents[2] = { "pll48", "sys" };

static const char *hdmi_parents[2] = { "lse", "hsi_div488" };

static const char *spdif_parent[1] = { "plli2s-p" };

static const char *lptim_parent[4] = { "apb1_mul", "lsi", "hsi", "lse" };

static const char *uart_parents1[4] = { "apb2_div", "sys", "hsi", "lse" };
static const char *uart_parents2[4] = { "apb1_div", "sys", "hsi", "lse" };

static const char *i2c_parents[4] = { "apb1_div", "sys", "hsi", "no-clock" };

struct stm32_aux_clk {
	int idx;
	const char *name;
	const char * const *parent_names;
	int num_parents;
	int offset_mux;
	u8 shift;
	u8 mask;
	int offset_gate;
	u8 bit_idx;
	unsigned long flags;
};

struct stm32f4_clk_data {
	const struct stm32f4_gate_data *gates_data;
	const u64 *gates_map;
	int gates_num;
	const struct stm32f4_pll_data *pll_data;
	const struct stm32_aux_clk *aux_clk;
	int aux_clk_num;
	int end_primary;
};

static const struct stm32_aux_clk stm32f429_aux_clk[] = {
	{
		CLK_LCD, "lcd-tft", lcd_parent, ARRAY_SIZE(lcd_parent),
		NO_MUX, 0, 0,
		STM32F4_RCC_APB2ENR, 26,
		CLK_SET_RATE_PARENT
	},
	{
		CLK_I2S, "i2s", i2s_parents, ARRAY_SIZE(i2s_parents),
		STM32F4_RCC_CFGR, 23, 1,
		NO_GATE, 0,
		CLK_SET_RATE_PARENT
	},
	{
		CLK_SAI1, "sai1-a", sai_parents, ARRAY_SIZE(sai_parents),
		STM32F4_RCC_DCKCFGR, 20, 3,
		STM32F4_RCC_APB2ENR, 22,
		CLK_SET_RATE_PARENT
	},
	{
		CLK_SAI2, "sai1-b", sai_parents, ARRAY_SIZE(sai_parents),
		STM32F4_RCC_DCKCFGR, 22, 3,
		STM32F4_RCC_APB2ENR, 22,
		CLK_SET_RATE_PARENT
	},
};

static const struct stm32_aux_clk stm32f469_aux_clk[] = {
	{
		CLK_LCD, "lcd-tft", lcd_parent, ARRAY_SIZE(lcd_parent),
		NO_MUX, 0, 0,
		STM32F4_RCC_APB2ENR, 26,
		CLK_SET_RATE_PARENT
	},
	{
		CLK_I2S, "i2s", i2s_parents, ARRAY_SIZE(i2s_parents),
		STM32F4_RCC_CFGR, 23, 1,
		NO_GATE, 0,
		CLK_SET_RATE_PARENT
	},
	{
		CLK_SAI1, "sai1-a", sai_parents, ARRAY_SIZE(sai_parents),
		STM32F4_RCC_DCKCFGR, 20, 3,
		STM32F4_RCC_APB2ENR, 22,
		CLK_SET_RATE_PARENT
	},
	{
		CLK_SAI2, "sai1-b", sai_parents, ARRAY_SIZE(sai_parents),
		STM32F4_RCC_DCKCFGR, 22, 3,
		STM32F4_RCC_APB2ENR, 22,
		CLK_SET_RATE_PARENT
	},
	{
		NO_IDX, "pll48", pll48_parents, ARRAY_SIZE(pll48_parents),
		STM32F4_RCC_DCKCFGR, 27, 1,
		NO_GATE, 0,
		0
	},
	{
		NO_IDX, "sdmux", sdmux_parents, ARRAY_SIZE(sdmux_parents),
		STM32F4_RCC_DCKCFGR, 28, 1,
		NO_GATE, 0,
		0
	},
};

static const struct stm32_aux_clk stm32f746_aux_clk[] = {
	{
		CLK_LCD, "lcd-tft", lcd_parent, ARRAY_SIZE(lcd_parent),
		NO_MUX, 0, 0,
		STM32F4_RCC_APB2ENR, 26,
		CLK_SET_RATE_PARENT
	},
	{
		CLK_I2S, "i2s", i2s_parents, ARRAY_SIZE(i2s_parents),
		STM32F4_RCC_CFGR, 23, 1,
		NO_GATE, 0,
		CLK_SET_RATE_PARENT
	},
	{
		CLK_SAI1, "sai1_clk", sai_parents, ARRAY_SIZE(sai_parents),
		STM32F4_RCC_DCKCFGR, 20, 3,
		STM32F4_RCC_APB2ENR, 22,
		CLK_SET_RATE_PARENT
	},
	{
		CLK_SAI2, "sai2_clk", sai_parents, ARRAY_SIZE(sai_parents),
		STM32F4_RCC_DCKCFGR, 22, 3,
		STM32F4_RCC_APB2ENR, 23,
		CLK_SET_RATE_PARENT
	},
	{
		NO_IDX, "pll48", pll48_parents, ARRAY_SIZE(pll48_parents),
		STM32F7_RCC_DCKCFGR2, 27, 1,
		NO_GATE, 0,
		0
	},
	{
		NO_IDX, "sdmux", sdmux_parents, ARRAY_SIZE(sdmux_parents),
		STM32F7_RCC_DCKCFGR2, 28, 1,
		NO_GATE, 0,
		0
	},
	{
		CLK_HDMI_CEC, "hdmi-cec",
		hdmi_parents, ARRAY_SIZE(hdmi_parents),
		STM32F7_RCC_DCKCFGR2, 26, 1,
		NO_GATE, 0,
		0
	},
	{
		CLK_SPDIF, "spdif-rx",
		spdif_parent, ARRAY_SIZE(spdif_parent),
		STM32F7_RCC_DCKCFGR2, 22, 3,
		STM32F4_RCC_APB2ENR, 23,
		CLK_SET_RATE_PARENT
	},
	{
		CLK_USART1, "usart1",
		uart_parents1, ARRAY_SIZE(uart_parents1),
		STM32F7_RCC_DCKCFGR2, 0, 3,
		STM32F4_RCC_APB2ENR, 4,
		CLK_SET_RATE_PARENT,
	},
	{
		CLK_USART2, "usart2",
		uart_parents2, ARRAY_SIZE(uart_parents1),
		STM32F7_RCC_DCKCFGR2, 2, 3,
		STM32F4_RCC_APB1ENR, 17,
		CLK_SET_RATE_PARENT,
	},
	{
		CLK_USART3, "usart3",
		uart_parents2, ARRAY_SIZE(uart_parents1),
		STM32F7_RCC_DCKCFGR2, 4, 3,
		STM32F4_RCC_APB1ENR, 18,
		CLK_SET_RATE_PARENT,
	},
	{
		CLK_UART4, "uart4",
		uart_parents2, ARRAY_SIZE(uart_parents1),
		STM32F7_RCC_DCKCFGR2, 6, 3,
		STM32F4_RCC_APB1ENR, 19,
		CLK_SET_RATE_PARENT,
	},
	{
		CLK_UART5, "uart5",
		uart_parents2, ARRAY_SIZE(uart_parents1),
		STM32F7_RCC_DCKCFGR2, 8, 3,
		STM32F4_RCC_APB1ENR, 20,
		CLK_SET_RATE_PARENT,
	},
	{
		CLK_USART6, "usart6",
		uart_parents1, ARRAY_SIZE(uart_parents1),
		STM32F7_RCC_DCKCFGR2, 10, 3,
		STM32F4_RCC_APB2ENR, 5,
		CLK_SET_RATE_PARENT,
	},

	{
		CLK_UART7, "uart7",
		uart_parents2, ARRAY_SIZE(uart_parents1),
		STM32F7_RCC_DCKCFGR2, 12, 3,
		STM32F4_RCC_APB1ENR, 30,
		CLK_SET_RATE_PARENT,
	},
	{
		CLK_UART8, "uart8",
		uart_parents2, ARRAY_SIZE(uart_parents1),
		STM32F7_RCC_DCKCFGR2, 14, 3,
		STM32F4_RCC_APB1ENR, 31,
		CLK_SET_RATE_PARENT,
	},
	{
		CLK_I2C1, "i2c1",
		i2c_parents, ARRAY_SIZE(i2c_parents),
		STM32F7_RCC_DCKCFGR2, 16, 3,
		STM32F4_RCC_APB1ENR, 21,
		CLK_SET_RATE_PARENT,
	},
	{
		CLK_I2C2, "i2c2",
		i2c_parents, ARRAY_SIZE(i2c_parents),
		STM32F7_RCC_DCKCFGR2, 18, 3,
		STM32F4_RCC_APB1ENR, 22,
		CLK_SET_RATE_PARENT,
	},
	{
		CLK_I2C3, "i2c3",
		i2c_parents, ARRAY_SIZE(i2c_parents),
		STM32F7_RCC_DCKCFGR2, 20, 3,
		STM32F4_RCC_APB1ENR, 23,
		CLK_SET_RATE_PARENT,
	},
	{
		CLK_I2C4, "i2c4",
		i2c_parents, ARRAY_SIZE(i2c_parents),
		STM32F7_RCC_DCKCFGR2, 22, 3,
		STM32F4_RCC_APB1ENR, 24,
		CLK_SET_RATE_PARENT,
	},

	{
		CLK_LPTIMER, "lptim1",
		lptim_parent, ARRAY_SIZE(lptim_parent),
		STM32F7_RCC_DCKCFGR2, 24, 3,
		STM32F4_RCC_APB1ENR, 9,
		CLK_SET_RATE_PARENT
	},
};

static const struct stm32f4_clk_data stm32f429_clk_data = {
	.end_primary	= END_PRIMARY_CLK,
	.gates_data	= stm32f429_gates,
	.gates_map	= stm32f42xx_gate_map,
	.gates_num	= ARRAY_SIZE(stm32f429_gates),
	.pll_data	= stm32f429_pll,
	.aux_clk	= stm32f429_aux_clk,
	.aux_clk_num	= ARRAY_SIZE(stm32f429_aux_clk),
};

static const struct stm32f4_clk_data stm32f469_clk_data = {
	.end_primary	= END_PRIMARY_CLK,
	.gates_data	= stm32f469_gates,
	.gates_map	= stm32f46xx_gate_map,
	.gates_num	= ARRAY_SIZE(stm32f469_gates),
	.pll_data	= stm32f469_pll,
	.aux_clk	= stm32f469_aux_clk,
	.aux_clk_num	= ARRAY_SIZE(stm32f469_aux_clk),
};

static const struct stm32f4_clk_data stm32f746_clk_data = {
	.end_primary	= END_PRIMARY_CLK_F7,
	.gates_data	= stm32f746_gates,
	.gates_map	= stm32f746_gate_map,
	.gates_num	= ARRAY_SIZE(stm32f746_gates),
	.pll_data	= stm32f469_pll,
	.aux_clk	= stm32f746_aux_clk,
	.aux_clk_num	= ARRAY_SIZE(stm32f746_aux_clk),
};

static const struct of_device_id stm32f4_of_match[] = {
	{
		.compatible = "st,stm32f42xx-rcc",
		.data = &stm32f429_clk_data
	},
	{
		.compatible = "st,stm32f469-rcc",
		.data = &stm32f469_clk_data
	},
	{
		.compatible = "st,stm32f746-rcc",
		.data = &stm32f746_clk_data
	},
	{}
};

static struct clk_hw *stm32_register_aux_clk(const char *name,
		const char * const *parent_names, int num_parents,
		int offset_mux, u8 shift, u8 mask,
		int offset_gate, u8 bit_idx,
		unsigned long flags, spinlock_t *lock)
{
	struct clk_hw *hw;
	struct clk_gate *gate = NULL;
	struct clk_mux *mux = NULL;
	struct clk_hw *mux_hw = NULL, *gate_hw = NULL;
	const struct clk_ops *mux_ops = NULL, *gate_ops = NULL;

	if (offset_gate != NO_GATE) {
		gate = kzalloc(sizeof(*gate), GFP_KERNEL);
		if (!gate) {
			hw = ERR_PTR(-EINVAL);
			goto fail;
		}

		gate->reg = base + offset_gate;
		gate->bit_idx = bit_idx;
		gate->flags = 0;
		gate->lock = lock;
		gate_hw = &gate->hw;
		gate_ops = &clk_gate_ops;
	}

	if (offset_mux != NO_MUX) {
		mux = kzalloc(sizeof(*mux), GFP_KERNEL);
		if (!mux) {
			hw = ERR_PTR(-EINVAL);
			goto fail;
		}

		mux->reg = base + offset_mux;
		mux->shift = shift;
		mux->mask = mask;
		mux->flags = 0;
		mux_hw = &mux->hw;
		mux_ops = &clk_mux_ops;
	}

	if (mux_hw == NULL && gate_hw == NULL) {
		hw = ERR_PTR(-EINVAL);
		goto fail;
	}

	hw = clk_hw_register_composite(NULL, name, parent_names, num_parents,
			mux_hw, mux_ops,
			NULL, NULL,
			gate_hw, gate_ops,
			flags);

fail:
	if (IS_ERR(hw)) {
		kfree(gate);
		kfree(mux);
	}

	return hw;
}

static void __init stm32f4_rcc_init(struct device_node *np)
{
	const char *hse_clk, *i2s_in_clk;
	int n;
	const struct of_device_id *match;
	const struct stm32f4_clk_data *data;
	unsigned long pllcfgr;
	const char *pllsrc;
	unsigned long pllm;

	base = of_iomap(np, 0);
	if (!base) {
		pr_err("%s: unable to map resource", np->name);
		return;
	}

	pdrm = syscon_regmap_lookup_by_phandle(np, "st,syscfg");
	if (IS_ERR(pdrm)) {
		pdrm = NULL;
		pr_warn("%s: Unable to get syscfg\n", __func__);
	}

	match = of_match_node(stm32f4_of_match, np);
	if (WARN_ON(!match))
		return;

	data = match->data;

	stm32fx_end_primary_clk = data->end_primary;

	clks = kmalloc_array(data->gates_num + stm32fx_end_primary_clk,
			sizeof(*clks), GFP_KERNEL);
	if (!clks)
		goto fail;

	stm32f4_gate_map = data->gates_map;

	hse_clk = of_clk_get_parent_name(np, 0);

	i2s_in_clk = of_clk_get_parent_name(np, 1);

	i2s_parents[1] = i2s_in_clk;
	sai_parents[2] = i2s_in_clk;

	clks[CLK_HSI] = clk_hw_register_fixed_rate_with_accuracy(NULL, "hsi",
			NULL, 0, 16000000, 160000);

	pllcfgr = readl(base + STM32F4_RCC_PLLCFGR);
	pllsrc = pllcfgr & BIT(22) ? hse_clk : "hsi";
	pllm = pllcfgr & 0x3f;

	clk_hw_register_fixed_factor(NULL, "vco_in", pllsrc,
					       0, 1, pllm);

	stm32f4_rcc_register_pll("vco_in", &data->pll_data[0],
			&stm32f4_clk_lock);

	clks[PLL_VCO_I2S] = stm32f4_rcc_register_pll("vco_in",
			&data->pll_data[1], &stm32f4_clk_lock);

	clks[PLL_VCO_SAI] = stm32f4_rcc_register_pll("vco_in",
			&data->pll_data[2], &stm32f4_clk_lock);

	for (n = 0; n < MAX_POST_DIV; n++) {
		const struct stm32f4_pll_post_div_data *post_div;
		struct clk_hw *hw;

		post_div = &post_div_data[n];

		hw = clk_register_pll_div(post_div->name,
				post_div->parent,
				post_div->flag,
				base + post_div->offset,
				post_div->shift,
				post_div->width,
				post_div->flag_div,
				post_div->div_table,
				clks[post_div->pll_num],
				&stm32f4_clk_lock);

		if (post_div->idx != NO_IDX)
			clks[post_div->idx] = hw;
	}

	sys_parents[1] = hse_clk;

	clks[CLK_SYSCLK] = clk_hw_register_mux_table(
	    NULL, "sys", sys_parents, ARRAY_SIZE(sys_parents), 0,
	    base + STM32F4_RCC_CFGR, 0, 3, 0, NULL, &stm32f4_clk_lock);

	clk_register_divider_table(NULL, "ahb_div", "sys",
				   CLK_SET_RATE_PARENT, base + STM32F4_RCC_CFGR,
				   4, 4, 0, ahb_div_table, &stm32f4_clk_lock);

	clk_register_divider_table(NULL, "apb1_div", "ahb_div",
				   CLK_SET_RATE_PARENT, base + STM32F4_RCC_CFGR,
				   10, 3, 0, apb_div_table, &stm32f4_clk_lock);
	clk_register_apb_mul(NULL, "apb1_mul", "apb1_div",
			     CLK_SET_RATE_PARENT, 12);

	clk_register_divider_table(NULL, "apb2_div", "ahb_div",
				   CLK_SET_RATE_PARENT, base + STM32F4_RCC_CFGR,
				   13, 3, 0, apb_div_table, &stm32f4_clk_lock);
	clk_register_apb_mul(NULL, "apb2_mul", "apb2_div",
			     CLK_SET_RATE_PARENT, 15);

	clks[SYSTICK] = clk_hw_register_fixed_factor(NULL, "systick", "ahb_div",
						  0, 1, 8);
	clks[FCLK] = clk_hw_register_fixed_factor(NULL, "fclk", "ahb_div",
					       0, 1, 1);

	for (n = 0; n < data->gates_num; n++) {
		const struct stm32f4_gate_data *gd;
		unsigned int secondary;
		int idx;

		gd = &data->gates_data[n];
		secondary = 8 * (gd->offset - STM32F4_RCC_AHB1ENR) +
			gd->bit_idx;
		idx = stm32f4_rcc_lookup_clk_idx(0, secondary);

		if (idx < 0)
			goto fail;

		clks[idx] = clk_hw_register_gate(
		    NULL, gd->name, gd->parent_name, gd->flags,
		    base + gd->offset, gd->bit_idx, 0, &stm32f4_clk_lock);

		if (IS_ERR(clks[idx])) {
			pr_err("%s: Unable to register leaf clock %s\n",
			       np->full_name, gd->name);
			goto fail;
		}
	}

	clks[CLK_LSI] = clk_register_rgate(NULL, "lsi", "clk-lsi", 0,
			base + STM32F4_RCC_CSR, 0, 2, 0, &stm32f4_clk_lock);

	if (IS_ERR(clks[CLK_LSI])) {
		pr_err("Unable to register lsi clock\n");
		goto fail;
	}

	clks[CLK_LSE] = clk_register_rgate(NULL, "lse", "clk-lse", 0,
			base + STM32F4_RCC_BDCR, 0, 2, 0, &stm32f4_clk_lock);

	if (IS_ERR(clks[CLK_LSE])) {
		pr_err("Unable to register lse clock\n");
		goto fail;
	}

	clks[CLK_HSE_RTC] = clk_hw_register_divider(NULL, "hse-rtc", "clk-hse",
			0, base + STM32F4_RCC_CFGR, 16, 5, 0,
			&stm32f4_clk_lock);

	if (IS_ERR(clks[CLK_HSE_RTC])) {
		pr_err("Unable to register hse-rtc clock\n");
		goto fail;
	}

	clks[CLK_RTC] = stm32_register_cclk(NULL, "rtc", rtc_parents, 4,
			base + STM32F4_RCC_BDCR, 15, 8, 0, &stm32f4_clk_lock);

	if (IS_ERR(clks[CLK_RTC])) {
		pr_err("Unable to register rtc clock\n");
		goto fail;
	}

	for (n = 0; n < data->aux_clk_num; n++) {
		const struct stm32_aux_clk *aux_clk;
		struct clk_hw *hw;

		aux_clk = &data->aux_clk[n];

		hw = stm32_register_aux_clk(aux_clk->name,
				aux_clk->parent_names, aux_clk->num_parents,
				aux_clk->offset_mux, aux_clk->shift,
				aux_clk->mask, aux_clk->offset_gate,
				aux_clk->bit_idx, aux_clk->flags,
				&stm32f4_clk_lock);

		if (IS_ERR(hw)) {
			pr_warn("Unable to register %s clk\n", aux_clk->name);
			continue;
		}

		if (aux_clk->idx != NO_IDX)
			clks[aux_clk->idx] = hw;
	}

	if (of_device_is_compatible(np, "st,stm32f746-rcc"))

		clk_hw_register_fixed_factor(NULL, "hsi_div488", "hsi", 0,
				1, 488);

	of_clk_add_hw_provider(np, stm32f4_rcc_lookup_clk, NULL);
	return;
fail:
	kfree(clks);
	iounmap(base);
}
CLK_OF_DECLARE_DRIVER(stm32f42xx_rcc, "st,stm32f42xx-rcc", stm32f4_rcc_init);
CLK_OF_DECLARE_DRIVER(stm32f46xx_rcc, "st,stm32f469-rcc", stm32f4_rcc_init);
CLK_OF_DECLARE_DRIVER(stm32f746_rcc, "st,stm32f746-rcc", stm32f4_rcc_init);
