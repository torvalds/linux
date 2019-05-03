// SPDX-License-Identifier: GPL-2.0
/*
 * Ingenic JZ4725B SoC CGU driver
 *
 * Copyright (C) 2018 Paul Cercueil
 * Author: Paul Cercueil <paul@crapouillou.net>
 */

#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <dt-bindings/clock/jz4725b-cgu.h>
#include "cgu.h"

/* CGU register offsets */
#define CGU_REG_CPCCR		0x00
#define CGU_REG_LCR		0x04
#define CGU_REG_CPPCR		0x10
#define CGU_REG_CLKGR		0x20
#define CGU_REG_OPCR		0x24
#define CGU_REG_I2SCDR		0x60
#define CGU_REG_LPCDR		0x64
#define CGU_REG_MSCCDR		0x68
#define CGU_REG_SSICDR		0x74
#define CGU_REG_CIMCDR		0x78

/* bits within the LCR register */
#define LCR_SLEEP		BIT(0)

static struct ingenic_cgu *cgu;

static const s8 pll_od_encoding[4] = {
	0x0, 0x1, -1, 0x3,
};

static const struct ingenic_cgu_clk_info jz4725b_cgu_clocks[] = {

	/* External clocks */

	[JZ4725B_CLK_EXT] = { "ext", CGU_CLK_EXT },
	[JZ4725B_CLK_OSC32K] = { "osc32k", CGU_CLK_EXT },

	[JZ4725B_CLK_PLL] = {
		"pll", CGU_CLK_PLL,
		.parents = { JZ4725B_CLK_EXT, -1, -1, -1 },
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

	[JZ4725B_CLK_PLL_HALF] = {
		"pll half", CGU_CLK_DIV,
		.parents = { JZ4725B_CLK_PLL, -1, -1, -1 },
		.div = { CGU_REG_CPCCR, 21, 1, 1, -1, -1, -1 },
	},

	[JZ4725B_CLK_CCLK] = {
		"cclk", CGU_CLK_DIV,
		.parents = { JZ4725B_CLK_PLL, -1, -1, -1 },
		.div = { CGU_REG_CPCCR, 0, 1, 4, 22, -1, -1 },
	},

	[JZ4725B_CLK_HCLK] = {
		"hclk", CGU_CLK_DIV,
		.parents = { JZ4725B_CLK_PLL, -1, -1, -1 },
		.div = { CGU_REG_CPCCR, 4, 1, 4, 22, -1, -1 },
	},

	[JZ4725B_CLK_PCLK] = {
		"pclk", CGU_CLK_DIV,
		.parents = { JZ4725B_CLK_PLL, -1, -1, -1 },
		.div = { CGU_REG_CPCCR, 8, 1, 4, 22, -1, -1 },
	},

	[JZ4725B_CLK_MCLK] = {
		"mclk", CGU_CLK_DIV,
		.parents = { JZ4725B_CLK_PLL, -1, -1, -1 },
		.div = { CGU_REG_CPCCR, 12, 1, 4, 22, -1, -1 },
	},

	[JZ4725B_CLK_IPU] = {
		"ipu", CGU_CLK_DIV | CGU_CLK_GATE,
		.parents = { JZ4725B_CLK_PLL, -1, -1, -1 },
		.div = { CGU_REG_CPCCR, 16, 1, 4, 22, -1, -1 },
		.gate = { CGU_REG_CLKGR, 13 },
	},

	[JZ4725B_CLK_LCD] = {
		"lcd", CGU_CLK_DIV | CGU_CLK_GATE,
		.parents = { JZ4725B_CLK_PLL_HALF, -1, -1, -1 },
		.div = { CGU_REG_LPCDR, 0, 1, 11, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 9 },
	},

	[JZ4725B_CLK_I2S] = {
		"i2s", CGU_CLK_MUX | CGU_CLK_DIV | CGU_CLK_GATE,
		.parents = { JZ4725B_CLK_EXT, JZ4725B_CLK_PLL_HALF, -1, -1 },
		.mux = { CGU_REG_CPCCR, 31, 1 },
		.div = { CGU_REG_I2SCDR, 0, 1, 9, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 6 },
	},

	[JZ4725B_CLK_SPI] = {
		"spi", CGU_CLK_MUX | CGU_CLK_DIV | CGU_CLK_GATE,
		.parents = { JZ4725B_CLK_EXT, JZ4725B_CLK_PLL, -1, -1 },
		.mux = { CGU_REG_SSICDR, 31, 1 },
		.div = { CGU_REG_SSICDR, 0, 1, 4, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 4 },
	},

	[JZ4725B_CLK_MMC_MUX] = {
		"mmc_mux", CGU_CLK_DIV,
		.parents = { JZ4725B_CLK_PLL_HALF, -1, -1, -1 },
		.div = { CGU_REG_MSCCDR, 0, 1, 5, -1, -1, -1 },
	},

	[JZ4725B_CLK_UDC] = {
		"udc", CGU_CLK_MUX | CGU_CLK_DIV,
		.parents = { JZ4725B_CLK_EXT, JZ4725B_CLK_PLL_HALF, -1, -1 },
		.mux = { CGU_REG_CPCCR, 29, 1 },
		.div = { CGU_REG_CPCCR, 23, 1, 6, -1, -1, -1 },
	},

	/* Gate-only clocks */

	[JZ4725B_CLK_UART] = {
		"uart", CGU_CLK_GATE,
		.parents = { JZ4725B_CLK_EXT, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 0 },
	},

	[JZ4725B_CLK_DMA] = {
		"dma", CGU_CLK_GATE,
		.parents = { JZ4725B_CLK_PCLK, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 12 },
	},

	[JZ4725B_CLK_ADC] = {
		"adc", CGU_CLK_GATE,
		.parents = { JZ4725B_CLK_EXT, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 7 },
	},

	[JZ4725B_CLK_I2C] = {
		"i2c", CGU_CLK_GATE,
		.parents = { JZ4725B_CLK_EXT, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 3 },
	},

	[JZ4725B_CLK_AIC] = {
		"aic", CGU_CLK_GATE,
		.parents = { JZ4725B_CLK_EXT, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 5 },
	},

	[JZ4725B_CLK_MMC0] = {
		"mmc0", CGU_CLK_GATE,
		.parents = { JZ4725B_CLK_MMC_MUX, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 6 },
	},

	[JZ4725B_CLK_MMC1] = {
		"mmc1", CGU_CLK_GATE,
		.parents = { JZ4725B_CLK_MMC_MUX, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 16 },
	},

	[JZ4725B_CLK_BCH] = {
		"bch", CGU_CLK_GATE,
		.parents = { JZ4725B_CLK_MCLK/* not sure */, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 11 },
	},

	[JZ4725B_CLK_TCU] = {
		"tcu", CGU_CLK_GATE,
		.parents = { JZ4725B_CLK_EXT/* not sure */, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 1 },
	},

	[JZ4725B_CLK_EXT512] = {
		"ext/512", CGU_CLK_FIXDIV,
		.parents = { JZ4725B_CLK_EXT },

		/* Doc calls it EXT512, but it seems to be /256... */
		.fixdiv = { 256 },
	},

	[JZ4725B_CLK_RTC] = {
		"rtc", CGU_CLK_MUX,
		.parents = { JZ4725B_CLK_EXT512, JZ4725B_CLK_OSC32K, -1, -1 },
		.mux = { CGU_REG_OPCR, 2, 1},
	},
};

static void __init jz4725b_cgu_init(struct device_node *np)
{
	int retval;

	cgu = ingenic_cgu_new(jz4725b_cgu_clocks,
			      ARRAY_SIZE(jz4725b_cgu_clocks), np);
	if (!cgu) {
		pr_err("%s: failed to initialise CGU\n", __func__);
		return;
	}

	retval = ingenic_cgu_register_clocks(cgu);
	if (retval)
		pr_err("%s: failed to register CGU Clocks\n", __func__);
}
CLK_OF_DECLARE(jz4725b_cgu, "ingenic,jz4725b-cgu", jz4725b_cgu_init);
