// SPDX-License-Identifier: GPL-2.0
/*
 * JZ4770 SoC CGU driver
 * Copyright 2018, Paul Cercueil <paul@crapouillou.net>
 */

#include <linux/bitops.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <dt-bindings/clock/jz4770-cgu.h>
#include "cgu.h"
#include "pm.h"

/*
 * CPM registers offset address definition
 */
#define CGU_REG_CPCCR		0x00
#define CGU_REG_LCR		0x04
#define CGU_REG_CPPCR0		0x10
#define CGU_REG_CLKGR0		0x20
#define CGU_REG_OPCR		0x24
#define CGU_REG_CLKGR1		0x28
#define CGU_REG_CPPCR1		0x30
#define CGU_REG_USBPCR1		0x48
#define CGU_REG_USBCDR		0x50
#define CGU_REG_I2SCDR		0x60
#define CGU_REG_LPCDR		0x64
#define CGU_REG_MSC0CDR		0x68
#define CGU_REG_UHCCDR		0x6c
#define CGU_REG_SSICDR		0x74
#define CGU_REG_CIMCDR		0x7c
#define CGU_REG_GPSCDR		0x80
#define CGU_REG_PCMCDR		0x84
#define CGU_REG_GPUCDR		0x88
#define CGU_REG_MSC1CDR		0xA4
#define CGU_REG_MSC2CDR		0xA8
#define CGU_REG_BCHCDR		0xAC

/* bits within the OPCR register */
#define OPCR_SPENDH		BIT(5)		/* UHC PHY suspend */

/* bits within the USBPCR1 register */
#define USBPCR1_UHC_POWER	BIT(5)		/* UHC PHY power down */

static struct ingenic_cgu *cgu;

static int jz4770_uhc_phy_enable(struct clk_hw *hw)
{
	void __iomem *reg_opcr		= cgu->base + CGU_REG_OPCR;
	void __iomem *reg_usbpcr1	= cgu->base + CGU_REG_USBPCR1;

	writel(readl(reg_opcr) & ~OPCR_SPENDH, reg_opcr);
	writel(readl(reg_usbpcr1) | USBPCR1_UHC_POWER, reg_usbpcr1);
	return 0;
}

static void jz4770_uhc_phy_disable(struct clk_hw *hw)
{
	void __iomem *reg_opcr		= cgu->base + CGU_REG_OPCR;
	void __iomem *reg_usbpcr1	= cgu->base + CGU_REG_USBPCR1;

	writel(readl(reg_usbpcr1) & ~USBPCR1_UHC_POWER, reg_usbpcr1);
	writel(readl(reg_opcr) | OPCR_SPENDH, reg_opcr);
}

static int jz4770_uhc_phy_is_enabled(struct clk_hw *hw)
{
	void __iomem *reg_opcr		= cgu->base + CGU_REG_OPCR;
	void __iomem *reg_usbpcr1	= cgu->base + CGU_REG_USBPCR1;

	return !(readl(reg_opcr) & OPCR_SPENDH) &&
		(readl(reg_usbpcr1) & USBPCR1_UHC_POWER);
}

static const struct clk_ops jz4770_uhc_phy_ops = {
	.enable = jz4770_uhc_phy_enable,
	.disable = jz4770_uhc_phy_disable,
	.is_enabled = jz4770_uhc_phy_is_enabled,
};

static const s8 pll_od_encoding[8] = {
	0x0, 0x1, -1, 0x2, -1, -1, -1, 0x3,
};

static const u8 jz4770_cgu_cpccr_div_table[] = {
	1, 2, 3, 4, 6, 8, 12,
};

static const struct ingenic_cgu_clk_info jz4770_cgu_clocks[] = {

	/* External clocks */

	[JZ4770_CLK_EXT] = { "ext", CGU_CLK_EXT },
	[JZ4770_CLK_OSC32K] = { "osc32k", CGU_CLK_EXT },

	/* PLLs */

	[JZ4770_CLK_PLL0] = {
		"pll0", CGU_CLK_PLL,
		.parents = { JZ4770_CLK_EXT },
		.pll = {
			.reg = CGU_REG_CPPCR0,
			.m_shift = 24,
			.m_bits = 7,
			.m_offset = 1,
			.n_shift = 18,
			.n_bits = 5,
			.n_offset = 1,
			.od_shift = 16,
			.od_bits = 2,
			.od_max = 8,
			.od_encoding = pll_od_encoding,
			.bypass_bit = 9,
			.enable_bit = 8,
			.stable_bit = 10,
		},
	},

	[JZ4770_CLK_PLL1] = {
		/* TODO: PLL1 can depend on PLL0 */
		"pll1", CGU_CLK_PLL,
		.parents = { JZ4770_CLK_EXT },
		.pll = {
			.reg = CGU_REG_CPPCR1,
			.m_shift = 24,
			.m_bits = 7,
			.m_offset = 1,
			.n_shift = 18,
			.n_bits = 5,
			.n_offset = 1,
			.od_shift = 16,
			.od_bits = 2,
			.od_max = 8,
			.od_encoding = pll_od_encoding,
			.enable_bit = 7,
			.stable_bit = 6,
			.no_bypass_bit = true,
		},
	},

	/* Main clocks */

	[JZ4770_CLK_CCLK] = {
		"cclk", CGU_CLK_DIV,
		.parents = { JZ4770_CLK_PLL0, },
		.div = {
			CGU_REG_CPCCR, 0, 1, 4, 22, -1, -1,
			jz4770_cgu_cpccr_div_table,
		},
	},
	[JZ4770_CLK_H0CLK] = {
		"h0clk", CGU_CLK_DIV,
		.parents = { JZ4770_CLK_PLL0, },
		.div = {
			CGU_REG_CPCCR, 4, 1, 4, 22, -1, -1,
			jz4770_cgu_cpccr_div_table,
		},
	},
	[JZ4770_CLK_H1CLK] = {
		"h1clk", CGU_CLK_DIV | CGU_CLK_GATE,
		.parents = { JZ4770_CLK_PLL0, },
		.div = {
			CGU_REG_CPCCR, 24, 1, 4, 22, -1, -1,
			jz4770_cgu_cpccr_div_table,
		},
		.gate = { CGU_REG_CLKGR1, 7 },
	},
	[JZ4770_CLK_H2CLK] = {
		"h2clk", CGU_CLK_DIV,
		.parents = { JZ4770_CLK_PLL0, },
		.div = {
			CGU_REG_CPCCR, 16, 1, 4, 22, -1, -1,
			jz4770_cgu_cpccr_div_table,
		},
	},
	[JZ4770_CLK_C1CLK] = {
		"c1clk", CGU_CLK_DIV | CGU_CLK_GATE,
		.parents = { JZ4770_CLK_PLL0, },
		.div = {
			CGU_REG_CPCCR, 12, 1, 4, 22, -1, -1,
			jz4770_cgu_cpccr_div_table,
		},
		.gate = { CGU_REG_OPCR, 31, true }, // disable CCLK stop on idle
	},
	[JZ4770_CLK_PCLK] = {
		"pclk", CGU_CLK_DIV,
		.parents = { JZ4770_CLK_PLL0, },
		.div = {
			CGU_REG_CPCCR, 8, 1, 4, 22, -1, -1,
			jz4770_cgu_cpccr_div_table,
		},
	},

	/* Those divided clocks can connect to PLL0 or PLL1 */

	[JZ4770_CLK_MMC0_MUX] = {
		"mmc0_mux", CGU_CLK_DIV | CGU_CLK_GATE | CGU_CLK_MUX,
		.parents = { JZ4770_CLK_PLL0, JZ4770_CLK_PLL1, },
		.mux = { CGU_REG_MSC0CDR, 30, 1 },
		.div = { CGU_REG_MSC0CDR, 0, 1, 7, -1, -1, 31 },
		.gate = { CGU_REG_MSC0CDR, 31 },
	},
	[JZ4770_CLK_MMC1_MUX] = {
		"mmc1_mux", CGU_CLK_DIV | CGU_CLK_GATE | CGU_CLK_MUX,
		.parents = { JZ4770_CLK_PLL0, JZ4770_CLK_PLL1, },
		.mux = { CGU_REG_MSC1CDR, 30, 1 },
		.div = { CGU_REG_MSC1CDR, 0, 1, 7, -1, -1, 31 },
		.gate = { CGU_REG_MSC1CDR, 31 },
	},
	[JZ4770_CLK_MMC2_MUX] = {
		"mmc2_mux", CGU_CLK_DIV | CGU_CLK_GATE | CGU_CLK_MUX,
		.parents = { JZ4770_CLK_PLL0, JZ4770_CLK_PLL1, },
		.mux = { CGU_REG_MSC2CDR, 30, 1 },
		.div = { CGU_REG_MSC2CDR, 0, 1, 7, -1, -1, 31 },
		.gate = { CGU_REG_MSC2CDR, 31 },
	},
	[JZ4770_CLK_CIM] = {
		"cim", CGU_CLK_DIV | CGU_CLK_GATE | CGU_CLK_MUX,
		.parents = { JZ4770_CLK_PLL0, JZ4770_CLK_PLL1, },
		.mux = { CGU_REG_CIMCDR, 31, 1 },
		.div = { CGU_REG_CIMCDR, 0, 1, 8, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR0, 26 },
	},
	[JZ4770_CLK_UHC] = {
		"uhc", CGU_CLK_DIV | CGU_CLK_GATE | CGU_CLK_MUX,
		.parents = { JZ4770_CLK_PLL0, JZ4770_CLK_PLL1, },
		.mux = { CGU_REG_UHCCDR, 29, 1 },
		.div = { CGU_REG_UHCCDR, 0, 1, 4, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR0, 24 },
	},
	[JZ4770_CLK_GPU] = {
		"gpu", CGU_CLK_DIV | CGU_CLK_GATE | CGU_CLK_MUX,
		.parents = { JZ4770_CLK_PLL0, JZ4770_CLK_PLL1, -1 },
		.mux = { CGU_REG_GPUCDR, 31, 1 },
		.div = { CGU_REG_GPUCDR, 0, 1, 3, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR1, 9 },
	},
	[JZ4770_CLK_BCH] = {
		"bch", CGU_CLK_DIV | CGU_CLK_GATE | CGU_CLK_MUX,
		.parents = { JZ4770_CLK_PLL0, JZ4770_CLK_PLL1, },
		.mux = { CGU_REG_BCHCDR, 31, 1 },
		.div = { CGU_REG_BCHCDR, 0, 1, 3, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR0, 1 },
	},
	[JZ4770_CLK_LPCLK_MUX] = {
		"lpclk", CGU_CLK_DIV | CGU_CLK_GATE | CGU_CLK_MUX,
		.parents = { JZ4770_CLK_PLL0, JZ4770_CLK_PLL1, },
		.mux = { CGU_REG_LPCDR, 29, 1 },
		.div = { CGU_REG_LPCDR, 0, 1, 11, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR0, 28 },
	},
	[JZ4770_CLK_GPS] = {
		"gps", CGU_CLK_DIV | CGU_CLK_GATE | CGU_CLK_MUX,
		.parents = { JZ4770_CLK_PLL0, JZ4770_CLK_PLL1, },
		.mux = { CGU_REG_GPSCDR, 31, 1 },
		.div = { CGU_REG_GPSCDR, 0, 1, 4, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR0, 22 },
	},

	/* Those divided clocks can connect to EXT, PLL0 or PLL1 */

	[JZ4770_CLK_SSI_MUX] = {
		"ssi_mux", CGU_CLK_DIV | CGU_CLK_MUX,
		.parents = { JZ4770_CLK_EXT, -1,
			JZ4770_CLK_PLL0, JZ4770_CLK_PLL1 },
		.mux = { CGU_REG_SSICDR, 30, 2 },
		.div = { CGU_REG_SSICDR, 0, 1, 6, -1, -1, -1 },
	},
	[JZ4770_CLK_PCM_MUX] = {
		"pcm_mux", CGU_CLK_DIV | CGU_CLK_MUX,
		.parents = { JZ4770_CLK_EXT, -1,
			JZ4770_CLK_PLL0, JZ4770_CLK_PLL1 },
		.mux = { CGU_REG_PCMCDR, 30, 2 },
		.div = { CGU_REG_PCMCDR, 0, 1, 9, -1, -1, -1 },
	},
	[JZ4770_CLK_I2S] = {
		"i2s", CGU_CLK_DIV | CGU_CLK_GATE | CGU_CLK_MUX,
		.parents = { JZ4770_CLK_EXT, -1,
			JZ4770_CLK_PLL0, JZ4770_CLK_PLL1 },
		.mux = { CGU_REG_I2SCDR, 30, 2 },
		.div = { CGU_REG_I2SCDR, 0, 1, 9, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR1, 13 },
	},
	[JZ4770_CLK_OTG] = {
		"usb", CGU_CLK_DIV | CGU_CLK_GATE | CGU_CLK_MUX,
		.parents = { JZ4770_CLK_EXT, -1,
			JZ4770_CLK_PLL0, JZ4770_CLK_PLL1 },
		.mux = { CGU_REG_USBCDR, 30, 2 },
		.div = { CGU_REG_USBCDR, 0, 1, 8, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR0, 2 },
	},

	/* Gate-only clocks */

	[JZ4770_CLK_SSI0] = {
		"ssi0", CGU_CLK_GATE,
		.parents = { JZ4770_CLK_SSI_MUX, },
		.gate = { CGU_REG_CLKGR0, 4 },
	},
	[JZ4770_CLK_SSI1] = {
		"ssi1", CGU_CLK_GATE,
		.parents = { JZ4770_CLK_SSI_MUX, },
		.gate = { CGU_REG_CLKGR0, 19 },
	},
	[JZ4770_CLK_SSI2] = {
		"ssi2", CGU_CLK_GATE,
		.parents = { JZ4770_CLK_SSI_MUX, },
		.gate = { CGU_REG_CLKGR0, 20 },
	},
	[JZ4770_CLK_PCM0] = {
		"pcm0", CGU_CLK_GATE,
		.parents = { JZ4770_CLK_PCM_MUX, },
		.gate = { CGU_REG_CLKGR1, 8 },
	},
	[JZ4770_CLK_PCM1] = {
		"pcm1", CGU_CLK_GATE,
		.parents = { JZ4770_CLK_PCM_MUX, },
		.gate = { CGU_REG_CLKGR1, 10 },
	},
	[JZ4770_CLK_DMA] = {
		"dma", CGU_CLK_GATE,
		.parents = { JZ4770_CLK_H2CLK, },
		.gate = { CGU_REG_CLKGR0, 21 },
	},
	[JZ4770_CLK_I2C0] = {
		"i2c0", CGU_CLK_GATE,
		.parents = { JZ4770_CLK_EXT, },
		.gate = { CGU_REG_CLKGR0, 5 },
	},
	[JZ4770_CLK_I2C1] = {
		"i2c1", CGU_CLK_GATE,
		.parents = { JZ4770_CLK_EXT, },
		.gate = { CGU_REG_CLKGR0, 6 },
	},
	[JZ4770_CLK_I2C2] = {
		"i2c2", CGU_CLK_GATE,
		.parents = { JZ4770_CLK_EXT, },
		.gate = { CGU_REG_CLKGR1, 15 },
	},
	[JZ4770_CLK_UART0] = {
		"uart0", CGU_CLK_GATE,
		.parents = { JZ4770_CLK_EXT, },
		.gate = { CGU_REG_CLKGR0, 15 },
	},
	[JZ4770_CLK_UART1] = {
		"uart1", CGU_CLK_GATE,
		.parents = { JZ4770_CLK_EXT, },
		.gate = { CGU_REG_CLKGR0, 16 },
	},
	[JZ4770_CLK_UART2] = {
		"uart2", CGU_CLK_GATE,
		.parents = { JZ4770_CLK_EXT, },
		.gate = { CGU_REG_CLKGR0, 17 },
	},
	[JZ4770_CLK_UART3] = {
		"uart3", CGU_CLK_GATE,
		.parents = { JZ4770_CLK_EXT, },
		.gate = { CGU_REG_CLKGR0, 18 },
	},
	[JZ4770_CLK_IPU] = {
		"ipu", CGU_CLK_GATE,
		.parents = { JZ4770_CLK_H0CLK, },
		.gate = { CGU_REG_CLKGR0, 29 },
	},
	[JZ4770_CLK_ADC] = {
		"adc", CGU_CLK_GATE,
		.parents = { JZ4770_CLK_EXT, },
		.gate = { CGU_REG_CLKGR0, 14 },
	},
	[JZ4770_CLK_AIC] = {
		"aic", CGU_CLK_GATE,
		.parents = { JZ4770_CLK_EXT, },
		.gate = { CGU_REG_CLKGR0, 8 },
	},
	[JZ4770_CLK_AUX] = {
		"aux", CGU_CLK_GATE,
		.parents = { JZ4770_CLK_C1CLK, },
		.gate = { CGU_REG_CLKGR1, 14 },
	},
	[JZ4770_CLK_VPU] = {
		"vpu", CGU_CLK_GATE,
		.parents = { JZ4770_CLK_H1CLK, },
		.gate = { CGU_REG_LCR, 30, false, 150 },
	},
	[JZ4770_CLK_MMC0] = {
		"mmc0", CGU_CLK_GATE,
		.parents = { JZ4770_CLK_MMC0_MUX, },
		.gate = { CGU_REG_CLKGR0, 3 },
	},
	[JZ4770_CLK_MMC1] = {
		"mmc1", CGU_CLK_GATE,
		.parents = { JZ4770_CLK_MMC1_MUX, },
		.gate = { CGU_REG_CLKGR0, 11 },
	},
	[JZ4770_CLK_MMC2] = {
		"mmc2", CGU_CLK_GATE,
		.parents = { JZ4770_CLK_MMC2_MUX, },
		.gate = { CGU_REG_CLKGR0, 12 },
	},
	[JZ4770_CLK_OTG_PHY] = {
		"usb_phy", CGU_CLK_GATE,
		.parents = { JZ4770_CLK_OTG },
		.gate = { CGU_REG_OPCR, 7, true, 50 },
	},

	/* Custom clocks */

	[JZ4770_CLK_UHC_PHY] = {
		"uhc_phy", CGU_CLK_CUSTOM,
		.parents = { JZ4770_CLK_UHC, -1, -1, -1 },
		.custom = { &jz4770_uhc_phy_ops },
	},

	[JZ4770_CLK_EXT512] = {
		"ext/512", CGU_CLK_FIXDIV,
		.parents = { JZ4770_CLK_EXT },
		.fixdiv = { 512 },
	},

	[JZ4770_CLK_RTC] = {
		"rtc", CGU_CLK_MUX,
		.parents = { JZ4770_CLK_EXT512, JZ4770_CLK_OSC32K, },
		.mux = { CGU_REG_OPCR, 2, 1},
	},
};

static void __init jz4770_cgu_init(struct device_node *np)
{
	int retval;

	cgu = ingenic_cgu_new(jz4770_cgu_clocks,
			      ARRAY_SIZE(jz4770_cgu_clocks), np);
	if (!cgu)
		pr_err("%s: failed to initialise CGU\n", __func__);

	retval = ingenic_cgu_register_clocks(cgu);
	if (retval)
		pr_err("%s: failed to register CGU Clocks\n", __func__);

	ingenic_cgu_register_syscore_ops(cgu);
}

/* We only probe via devicetree, no need for a platform driver */
CLK_OF_DECLARE_DRIVER(jz4770_cgu, "ingenic,jz4770-cgu", jz4770_cgu_init);
