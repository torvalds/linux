// SPDX-License-Identifier: GPL-2.0
/*
 * X1000 SoC CGU driver
 * Copyright (c) 2019 周琰杰 (Zhou Yanjie) <zhouyanjie@wanyeetech.com>
 */

#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>

#include <dt-bindings/clock/x1000-cgu.h>

#include "cgu.h"
#include "pm.h"

/* CGU register offsets */
#define CGU_REG_CPCCR		0x00
#define CGU_REG_APLL		0x10
#define CGU_REG_MPLL		0x14
#define CGU_REG_CLKGR		0x20
#define CGU_REG_OPCR		0x24
#define CGU_REG_DDRCDR		0x2c
#define CGU_REG_USBPCR		0x3c
#define CGU_REG_USBPCR1		0x48
#define CGU_REG_USBCDR		0x50
#define CGU_REG_MACCDR		0x54
#define CGU_REG_I2SCDR		0x60
#define CGU_REG_LPCDR		0x64
#define CGU_REG_MSC0CDR		0x68
#define CGU_REG_I2SCDR1		0x70
#define CGU_REG_SSICDR		0x74
#define CGU_REG_CIMCDR		0x7c
#define CGU_REG_PCMCDR		0x84
#define CGU_REG_MSC1CDR		0xa4
#define CGU_REG_CMP_INTR	0xb0
#define CGU_REG_CMP_INTRE	0xb4
#define CGU_REG_DRCG		0xd0
#define CGU_REG_CPCSR		0xd4
#define CGU_REG_PCMCDR1		0xe0
#define CGU_REG_MACPHYC		0xe8

/* bits within the OPCR register */
#define OPCR_SPENDN0		BIT(7)
#define OPCR_SPENDN1		BIT(6)

/* bits within the USBPCR register */
#define USBPCR_SIDDQ		BIT(21)
#define USBPCR_OTG_DISABLE	BIT(20)

/* bits within the USBPCR1 register */
#define USBPCR1_REFCLKSEL_SHIFT	26
#define USBPCR1_REFCLKSEL_MASK	(0x3 << USBPCR1_REFCLKSEL_SHIFT)
#define USBPCR1_REFCLKSEL_CORE	(0x2 << USBPCR1_REFCLKSEL_SHIFT)
#define USBPCR1_REFCLKDIV_SHIFT	24
#define USBPCR1_REFCLKDIV_MASK	(0x3 << USBPCR1_REFCLKDIV_SHIFT)
#define USBPCR1_REFCLKDIV_48	(0x2 << USBPCR1_REFCLKDIV_SHIFT)
#define USBPCR1_REFCLKDIV_24	(0x1 << USBPCR1_REFCLKDIV_SHIFT)
#define USBPCR1_REFCLKDIV_12	(0x0 << USBPCR1_REFCLKDIV_SHIFT)

static struct ingenic_cgu *cgu;

static unsigned long x1000_otg_phy_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	u32 usbpcr1;
	unsigned refclk_div;

	usbpcr1 = readl(cgu->base + CGU_REG_USBPCR1);
	refclk_div = usbpcr1 & USBPCR1_REFCLKDIV_MASK;

	switch (refclk_div) {
	case USBPCR1_REFCLKDIV_12:
		return 12000000;

	case USBPCR1_REFCLKDIV_24:
		return 24000000;

	case USBPCR1_REFCLKDIV_48:
		return 48000000;
	}

	return parent_rate;
}

static long x1000_otg_phy_round_rate(struct clk_hw *hw, unsigned long req_rate,
				      unsigned long *parent_rate)
{
	if (req_rate < 18000000)
		return 12000000;

	if (req_rate < 36000000)
		return 24000000;

	return 48000000;
}

static int x1000_otg_phy_set_rate(struct clk_hw *hw, unsigned long req_rate,
				   unsigned long parent_rate)
{
	unsigned long flags;
	u32 usbpcr1, div_bits;

	switch (req_rate) {
	case 12000000:
		div_bits = USBPCR1_REFCLKDIV_12;
		break;

	case 24000000:
		div_bits = USBPCR1_REFCLKDIV_24;
		break;

	case 48000000:
		div_bits = USBPCR1_REFCLKDIV_48;
		break;

	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&cgu->lock, flags);

	usbpcr1 = readl(cgu->base + CGU_REG_USBPCR1);
	usbpcr1 &= ~USBPCR1_REFCLKDIV_MASK;
	usbpcr1 |= div_bits;
	writel(usbpcr1, cgu->base + CGU_REG_USBPCR1);

	spin_unlock_irqrestore(&cgu->lock, flags);
	return 0;
}

static int x1000_usb_phy_enable(struct clk_hw *hw)
{
	void __iomem *reg_opcr		= cgu->base + CGU_REG_OPCR;
	void __iomem *reg_usbpcr	= cgu->base + CGU_REG_USBPCR;

	writel(readl(reg_opcr) | OPCR_SPENDN0, reg_opcr);
	writel(readl(reg_usbpcr) & ~USBPCR_OTG_DISABLE & ~USBPCR_SIDDQ, reg_usbpcr);
	return 0;
}

static void x1000_usb_phy_disable(struct clk_hw *hw)
{
	void __iomem *reg_opcr		= cgu->base + CGU_REG_OPCR;
	void __iomem *reg_usbpcr	= cgu->base + CGU_REG_USBPCR;

	writel(readl(reg_opcr) & ~OPCR_SPENDN0, reg_opcr);
	writel(readl(reg_usbpcr) | USBPCR_OTG_DISABLE | USBPCR_SIDDQ, reg_usbpcr);
}

static int x1000_usb_phy_is_enabled(struct clk_hw *hw)
{
	void __iomem *reg_opcr		= cgu->base + CGU_REG_OPCR;
	void __iomem *reg_usbpcr	= cgu->base + CGU_REG_USBPCR;

	return (readl(reg_opcr) & OPCR_SPENDN0) &&
		!(readl(reg_usbpcr) & USBPCR_SIDDQ) &&
		!(readl(reg_usbpcr) & USBPCR_OTG_DISABLE);
}

static const struct clk_ops x1000_otg_phy_ops = {
	.recalc_rate = x1000_otg_phy_recalc_rate,
	.round_rate = x1000_otg_phy_round_rate,
	.set_rate = x1000_otg_phy_set_rate,

	.enable		= x1000_usb_phy_enable,
	.disable	= x1000_usb_phy_disable,
	.is_enabled	= x1000_usb_phy_is_enabled,
};

static const s8 pll_od_encoding[8] = {
	0x0, 0x1, -1, 0x2, -1, -1, -1, 0x3,
};

static const struct ingenic_cgu_clk_info x1000_cgu_clocks[] = {

	/* External clocks */

	[X1000_CLK_EXCLK] = { "ext", CGU_CLK_EXT },
	[X1000_CLK_RTCLK] = { "rtc", CGU_CLK_EXT },

	/* PLLs */

	[X1000_CLK_APLL] = {
		"apll", CGU_CLK_PLL,
		.parents = { X1000_CLK_EXCLK, -1, -1, -1 },
		.pll = {
			.reg = CGU_REG_APLL,
			.rate_multiplier = 1,
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
			.bypass_reg = CGU_REG_APLL,
			.bypass_bit = 9,
			.enable_bit = 8,
			.stable_bit = 10,
		},
	},

	[X1000_CLK_MPLL] = {
		"mpll", CGU_CLK_PLL,
		.parents = { X1000_CLK_EXCLK, -1, -1, -1 },
		.pll = {
			.reg = CGU_REG_MPLL,
			.rate_multiplier = 1,
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
			.bypass_reg = CGU_REG_MPLL,
			.bypass_bit = 6,
			.enable_bit = 7,
			.stable_bit = 0,
		},
	},

	/* Custom (SoC-specific) OTG PHY */

	[X1000_CLK_OTGPHY] = {
		"otg_phy", CGU_CLK_CUSTOM,
		.parents = { -1, -1, X1000_CLK_EXCLK, -1 },
		.custom = { &x1000_otg_phy_ops },
	},

	/* Muxes & dividers */

	[X1000_CLK_SCLKA] = {
		"sclk_a", CGU_CLK_MUX,
		.parents = { -1, X1000_CLK_EXCLK, X1000_CLK_APLL, -1 },
		.mux = { CGU_REG_CPCCR, 30, 2 },
	},

	[X1000_CLK_CPUMUX] = {
		"cpu_mux", CGU_CLK_MUX,
		.parents = { -1, X1000_CLK_SCLKA, X1000_CLK_MPLL, -1 },
		.mux = { CGU_REG_CPCCR, 28, 2 },
	},

	[X1000_CLK_CPU] = {
		"cpu", CGU_CLK_DIV | CGU_CLK_GATE,
		.parents = { X1000_CLK_CPUMUX, -1, -1, -1 },
		.div = { CGU_REG_CPCCR, 0, 1, 4, 22, -1, -1 },
		.gate = { CGU_REG_CLKGR, 30 },
	},

	[X1000_CLK_L2CACHE] = {
		"l2cache", CGU_CLK_DIV,
		.parents = { X1000_CLK_CPUMUX, -1, -1, -1 },
		.div = { CGU_REG_CPCCR, 4, 1, 4, 22, -1, -1 },
	},

	[X1000_CLK_AHB0] = {
		"ahb0", CGU_CLK_MUX | CGU_CLK_DIV,
		.parents = { -1, X1000_CLK_SCLKA, X1000_CLK_MPLL, -1 },
		.mux = { CGU_REG_CPCCR, 26, 2 },
		.div = { CGU_REG_CPCCR, 8, 1, 4, 21, -1, -1 },
	},

	[X1000_CLK_AHB2PMUX] = {
		"ahb2_apb_mux", CGU_CLK_MUX,
		.parents = { -1, X1000_CLK_SCLKA, X1000_CLK_MPLL, -1 },
		.mux = { CGU_REG_CPCCR, 24, 2 },
	},

	[X1000_CLK_AHB2] = {
		"ahb2", CGU_CLK_DIV,
		.parents = { X1000_CLK_AHB2PMUX, -1, -1, -1 },
		.div = { CGU_REG_CPCCR, 12, 1, 4, 20, -1, -1 },
	},

	[X1000_CLK_PCLK] = {
		"pclk", CGU_CLK_DIV | CGU_CLK_GATE,
		.parents = { X1000_CLK_AHB2PMUX, -1, -1, -1 },
		.div = { CGU_REG_CPCCR, 16, 1, 4, 20, -1, -1 },
		.gate = { CGU_REG_CLKGR, 28 },
	},

	[X1000_CLK_DDR] = {
		"ddr", CGU_CLK_MUX | CGU_CLK_DIV | CGU_CLK_GATE,
		.parents = { -1, X1000_CLK_SCLKA, X1000_CLK_MPLL, -1 },
		.mux = { CGU_REG_DDRCDR, 30, 2 },
		.div = { CGU_REG_DDRCDR, 0, 1, 4, 29, 28, 27 },
		.gate = { CGU_REG_CLKGR, 31 },
	},

	[X1000_CLK_MAC] = {
		"mac", CGU_CLK_MUX | CGU_CLK_DIV | CGU_CLK_GATE,
		.parents = { X1000_CLK_SCLKA, X1000_CLK_MPLL },
		.mux = { CGU_REG_MACCDR, 31, 1 },
		.div = { CGU_REG_MACCDR, 0, 1, 8, 29, 28, 27 },
		.gate = { CGU_REG_CLKGR, 25 },
	},

	[X1000_CLK_LCD] = {
		"lcd", CGU_CLK_MUX | CGU_CLK_DIV | CGU_CLK_GATE,
		.parents = { X1000_CLK_SCLKA, X1000_CLK_MPLL },
		.mux = { CGU_REG_LPCDR, 31, 1 },
		.div = { CGU_REG_LPCDR, 0, 1, 8, 28, 27, 26 },
		.gate = { CGU_REG_CLKGR, 23 },
	},

	[X1000_CLK_MSCMUX] = {
		"msc_mux", CGU_CLK_MUX,
		.parents = { X1000_CLK_SCLKA, X1000_CLK_MPLL},
		.mux = { CGU_REG_MSC0CDR, 31, 1 },
	},

	[X1000_CLK_MSC0] = {
		"msc0", CGU_CLK_DIV | CGU_CLK_GATE,
		.parents = { X1000_CLK_MSCMUX, -1, -1, -1 },
		.div = { CGU_REG_MSC0CDR, 0, 2, 8, 29, 28, 27 },
		.gate = { CGU_REG_CLKGR, 4 },
	},

	[X1000_CLK_MSC1] = {
		"msc1", CGU_CLK_DIV | CGU_CLK_GATE,
		.parents = { X1000_CLK_MSCMUX, -1, -1, -1 },
		.div = { CGU_REG_MSC1CDR, 0, 2, 8, 29, 28, 27 },
		.gate = { CGU_REG_CLKGR, 5 },
	},

	[X1000_CLK_OTG] = {
		"otg", CGU_CLK_DIV | CGU_CLK_GATE | CGU_CLK_MUX,
		.parents = { X1000_CLK_EXCLK, -1,
					 X1000_CLK_APLL, X1000_CLK_MPLL },
		.mux = { CGU_REG_USBCDR, 30, 2 },
		.div = { CGU_REG_USBCDR, 0, 1, 8, 29, 28, 27 },
		.gate = { CGU_REG_CLKGR, 3 },
	},

	[X1000_CLK_SSIPLL] = {
		"ssi_pll", CGU_CLK_MUX | CGU_CLK_DIV,
		.parents = { X1000_CLK_SCLKA, X1000_CLK_MPLL, -1, -1 },
		.mux = { CGU_REG_SSICDR, 31, 1 },
		.div = { CGU_REG_SSICDR, 0, 1, 8, 29, 28, 27 },
	},

	[X1000_CLK_SSIPLL_DIV2] = {
		"ssi_pll_div2", CGU_CLK_FIXDIV,
		.parents = { X1000_CLK_SSIPLL },
		.fixdiv = { 2 },
	},

	[X1000_CLK_SSIMUX] = {
		"ssi_mux", CGU_CLK_MUX,
		.parents = { X1000_CLK_EXCLK, X1000_CLK_SSIPLL_DIV2, -1, -1 },
		.mux = { CGU_REG_SSICDR, 30, 1 },
	},

	[X1000_CLK_EXCLK_DIV512] = {
		"exclk_div512", CGU_CLK_FIXDIV,
		.parents = { X1000_CLK_EXCLK },
		.fixdiv = { 512 },
	},

	[X1000_CLK_RTC] = {
		"rtc_ercs", CGU_CLK_MUX | CGU_CLK_GATE,
		.parents = { X1000_CLK_EXCLK_DIV512, X1000_CLK_RTCLK },
		.mux = { CGU_REG_OPCR, 2, 1},
		.gate = { CGU_REG_CLKGR, 27 },
	},

	/* Gate-only clocks */

	[X1000_CLK_EMC] = {
		"emc", CGU_CLK_GATE,
		.parents = { X1000_CLK_AHB2, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 0 },
	},

	[X1000_CLK_EFUSE] = {
		"efuse", CGU_CLK_GATE,
		.parents = { X1000_CLK_AHB2, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 1 },
	},

	[X1000_CLK_SFC] = {
		"sfc", CGU_CLK_GATE,
		.parents = { X1000_CLK_SSIPLL, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 2 },
	},

	[X1000_CLK_I2C0] = {
		"i2c0", CGU_CLK_GATE,
		.parents = { X1000_CLK_PCLK, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 7 },
	},

	[X1000_CLK_I2C1] = {
		"i2c1", CGU_CLK_GATE,
		.parents = { X1000_CLK_PCLK, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 8 },
	},

	[X1000_CLK_I2C2] = {
		"i2c2", CGU_CLK_GATE,
		.parents = { X1000_CLK_PCLK, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 9 },
	},

	[X1000_CLK_UART0] = {
		"uart0", CGU_CLK_GATE,
		.parents = { X1000_CLK_EXCLK, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 14 },
	},

	[X1000_CLK_UART1] = {
		"uart1", CGU_CLK_GATE,
		.parents = { X1000_CLK_EXCLK, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 15 },
	},

	[X1000_CLK_UART2] = {
		"uart2", CGU_CLK_GATE,
		.parents = { X1000_CLK_EXCLK, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 16 },
	},

	[X1000_CLK_TCU] = {
		"tcu", CGU_CLK_GATE,
		.parents = { X1000_CLK_EXCLK, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 18 },
	},

	[X1000_CLK_SSI] = {
		"ssi", CGU_CLK_GATE,
		.parents = { X1000_CLK_SSIMUX, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 19 },
	},

	[X1000_CLK_OST] = {
		"ost", CGU_CLK_GATE,
		.parents = { X1000_CLK_EXCLK, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 20 },
	},

	[X1000_CLK_PDMA] = {
		"pdma", CGU_CLK_GATE,
		.parents = { X1000_CLK_EXCLK, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR, 21 },
	},
};

static void __init x1000_cgu_init(struct device_node *np)
{
	int retval;

	cgu = ingenic_cgu_new(x1000_cgu_clocks,
			      ARRAY_SIZE(x1000_cgu_clocks), np);
	if (!cgu) {
		pr_err("%s: failed to initialise CGU\n", __func__);
		return;
	}

	retval = ingenic_cgu_register_clocks(cgu);
	if (retval) {
		pr_err("%s: failed to register CGU Clocks\n", __func__);
		return;
	}

	ingenic_cgu_register_syscore_ops(cgu);
}
/*
 * CGU has some children devices, this is useful for probing children devices
 * in the case where the device node is compatible with "simple-mfd".
 */
CLK_OF_DECLARE_DRIVER(x1000_cgu, "ingenic,x1000-cgu", x1000_cgu_init);
