// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Ingenic JZ4740 SoC CGU driver
 *
 * Copyright (c) 2015 Imagination Technologies
 * Author: Paul Burton <paul.burton@mips.com>
 */

#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <dt-bindings/clock/jz4740-cgu.h>
#include "cgu.h"
#include "pm.h"

/* CGU register offsets */
#define CGU_REG_CPCCR		0x00
#define CGU_REG_LCR		0x04
#define CGU_REG_CPPCR		0x10
#define CGU_REG_CLKGR		0x20
#define CGU_REG_SCR		0x24
#define CGU_REG_I2SCDR		0x60
#define CGU_REG_LPCDR		0x64
#define CGU_REG_MSCCDR		0x68
#define CGU_REG_UHCCDR		0x6c
#define CGU_REG_SSICDR		0x74

/* bits within a PLL control register */
#define PLLCTL_M_SHIFT		23
#define PLLCTL_M_MASK		(0x1ff << PLLCTL_M_SHIFT)
#define PLLCTL_N_SHIFT		18
#define PLLCTL_N_MASK		(0x1f << PLLCTL_N_SHIFT)
#define PLLCTL_OD_SHIFT		16
#define PLLCTL_OD_MASK		(0x3 << PLLCTL_OD_SHIFT)
#define PLLCTL_STABLE		(1 << 10)
#define PLLCTL_BYPASS		(1 << 9)
#define PLLCTL_ENABLE		(1 << 8)

/* bits within the LCR register */
#define LCR_SLEEP		(1 << 0)

/* bits within the CLKGR register */
#define CLKGR_UDC		(1 << 11)

static struct ingenic_cgu *cgu;

static const s8 pll_od_encoding[4] = {
	0x0, 0x1, -1, 0x3,
};

static const u8 jz4740_cgu_cpccr_div_table[] = {
	1, 2, 3, 4, 6, 8, 12, 16, 24, 32,
};

static const struct ingenic_cgu_clk_info jz4740_cgu_clocks[] = {

	/* External clocks */

	[JZ4740_CLK_EXT] = { "ext", CGU_CLK_EXT },
	[JZ4740_CLK_RTC] = { "rtc", CGU_CLK_EXT },

	[JZ4740_CLK_PLL] = {
		"pll", CGU_CLK_PLL,
		.parents = { JZ4740_CLK_EXT, -1, -1, -1 },
		.pll = {
			.reg = CGU_REG_CPPCR,
			.m_shift = 23,
			.m_bits = 9,
			.m_offset = 2,
			.n_shift = 18,
			.n_bits = 5,
			.n_offset = 2,
			.od_shift = 16,
			.od_bits = 2,
			.od_max = 4,
			.od_encoding = pll_od_encoding,
			.stable_bit = 10,
			.bypass_bit = 9,
			.enable_bit = 8,
		},
	},

	/* Muxes & dividers */

	[JZ4740_CLK_PLL_HALF] = {
		"pll half", CGU_CLK_DIV,
		.parents = { JZ4740_CLK_PLL, -1, -1, -1 },
		.div = { CGU_REG_CPCCR, 21, 1, 1, -1, -1, -1 },
	},

	[JZ4740_CLK_CCLK] = {
		"cclk", CGU_CLK_DIV,
		.parents = { JZ4740_CLK_PLL, -1, -1, -1 },
		.div = {
			CGU_REG_CPCCR, 0, 1, 4, 22, -1, -1,
			jz4740_cgu_cpccr_div_table,
		},
	},

	[JZ4740_CLK_HCLK] = {
		"hclk", CGU_CLK_DIV,
		.parents = { JZ4740_CLK_PLL, -1, -1, -1 },
		.div = {
			CGU_REG_CPCCR, 4, 1, 4, 22, -1, -1,
			jz4740_cgu_cpccr_div_table,
		},
	},

	[JZ4740_CLK_PCLK] = {
		"pclk", CGU_CLK_DIV,
		.parents = { JZ4740_CLK_PLL, -1, -1, -1 },
		.div = {
			CGU_REG_CPCCR, 8, 1, 4, 22, -1, -1,
			jz4740_cgu_cpccr_div_table,
		},
	},

	[JZ4740_CLK_MCLK] = {
		"mclk", CGU_CLK_DIV,
		.parents = { JZ4740_CLK_PLL, -1, -1, -1 },
		.div = {
			CGU_REG_CPCCR, 12, 1, 4, 22, -1, -1,
			jz4740_cgu_cpccr_div_table,
		},
	},

	[JZ4740_CLK_LCD] = {
		"lcd", CGU_CLK_DIV | CGU_CLK_GATE,
		.parents = { JZ4740_CLK_PLL_HALF, -1, -1, -1 },
		.div = {
			CGU_REG_CPCCR, 16, 1, 5, 22, -1, -1,
			jz4740_cgu_cpccr_div_table,
		},
		.gate = { CGU_REG_CLKGR, 10 },
	},

	[JZ4740_CLK_LCD_PCLK] = {
		"lcd_pclk", CGU_CLK_DIV,
		.parents = { JZ4740_CLK_PLL_HALF, -1, -1, -1 },
		.div = { CGU_REG_LPCDR, 0, 1, 11, -1, -1, -1 },
	},

	[JZ4740_CLK_I2S] = {
		"i2s", CGU_CLK_MUX | CGU_CLK_DIV | CGU_CLK_GATE,
		.parents = { JZ4740_CLK_EXT, JZ4740_CLK_PLL_HALF, -1, -1 },
		.mux = { CGU_REG_CPCCR, 31, 1 },
		.div = { CGU_REG_I2SCDR, 0, 1, 9, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 6 },
	},

	[JZ4740_CLK_SPI] = {
		"spi", CGU_CLK_MUX | CGU_CLK_DIV | CGU_CLK_GATE,
		.parents = { JZ4740_CLK_EXT, JZ4740_CLK_PLL, -1, -1 },
		.mux = { CGU_REG_SSICDR, 31, 1 },
		.div = { CGU_REG_SSICDR, 0, 1, 4, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 4 },
	},

	[JZ4740_CLK_MMC] = {
		"mmc", CGU_CLK_DIV | CGU_CLK_GATE,
		.parents = { JZ4740_CLK_PLL_HALF, -1, -1, -1 },
		.div = { CGU_REG_MSCCDR, 0, 1, 5, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 7 },
	},

	[JZ4740_CLK_UHC] = {
		"uhc", CGU_CLK_DIV | CGU_CLK_GATE,
		.parents = { JZ4740_CLK_PLL_HALF, -1, -1, -1 },
		.div = { CGU_REG_UHCCDR, 0, 1, 4, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 14 },
	},

	[JZ4740_CLK_UDC] = {
		"udc", CGU_CLK_MUX | CGU_CLK_DIV | CGU_CLK_GATE,
		.parents = { JZ4740_CLK_EXT, JZ4740_CLK_PLL_HALF, -1, -1 },
		.mux = { CGU_REG_CPCCR, 29, 1 },
		.div = { CGU_REG_CPCCR, 23, 1, 6, -1, -1, -1 },
		.gate = { CGU_REG_SCR, 6, true },
	},

	/* Gate-only clocks */

	[JZ4740_CLK_UART0] = {
		"uart0", CGU_CLK_GATE,
		.parents = { JZ4740_CLK_EXT, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 0 },
	},

	[JZ4740_CLK_UART1] = {
		"uart1", CGU_CLK_GATE,
		.parents = { JZ4740_CLK_EXT, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 15 },
	},

	[JZ4740_CLK_DMA] = {
		"dma", CGU_CLK_GATE,
		.parents = { JZ4740_CLK_PCLK, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 12 },
	},

	[JZ4740_CLK_IPU] = {
		"ipu", CGU_CLK_GATE,
		.parents = { JZ4740_CLK_PCLK, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 13 },
	},

	[JZ4740_CLK_ADC] = {
		"adc", CGU_CLK_GATE,
		.parents = { JZ4740_CLK_EXT, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 8 },
	},

	[JZ4740_CLK_I2C] = {
		"i2c", CGU_CLK_GATE,
		.parents = { JZ4740_CLK_EXT, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 3 },
	},

	[JZ4740_CLK_AIC] = {
		"aic", CGU_CLK_GATE,
		.parents = { JZ4740_CLK_EXT, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 5 },
	},
};

static void __init jz4740_cgu_init(struct device_node *np)
{
	int retval;

	cgu = ingenic_cgu_new(jz4740_cgu_clocks,
			      ARRAY_SIZE(jz4740_cgu_clocks), np);
	if (!cgu) {
		pr_err("%s: failed to initialise CGU\n", __func__);
		return;
	}

	retval = ingenic_cgu_register_clocks(cgu);
	if (retval)
		pr_err("%s: failed to register CGU Clocks\n", __func__);

	ingenic_cgu_register_syscore_ops(cgu);
}
CLK_OF_DECLARE(jz4740_cgu, "ingenic,jz4740-cgu", jz4740_cgu_init);
