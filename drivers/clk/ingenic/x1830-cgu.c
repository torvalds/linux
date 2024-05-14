// SPDX-License-Identifier: GPL-2.0
/*
 * X1830 SoC CGU driver
 * Copyright (c) 2019 周琰杰 (Zhou Yanjie) <zhouyanjie@wanyeetech.com>
 */

#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>

#include <dt-bindings/clock/ingenic,x1830-cgu.h>

#include "cgu.h"
#include "pm.h"

/* CGU register offsets */
#define CGU_REG_CPCCR		0x00
#define CGU_REG_CPPCR		0x0c
#define CGU_REG_APLL		0x10
#define CGU_REG_MPLL		0x14
#define CGU_REG_CLKGR0		0x20
#define CGU_REG_OPCR		0x24
#define CGU_REG_CLKGR1		0x28
#define CGU_REG_DDRCDR		0x2c
#define CGU_REG_USBPCR		0x3c
#define CGU_REG_USBRDT		0x40
#define CGU_REG_USBVBFIL	0x44
#define CGU_REG_USBPCR1		0x48
#define CGU_REG_MACCDR		0x54
#define CGU_REG_EPLL		0x58
#define CGU_REG_I2SCDR		0x60
#define CGU_REG_LPCDR		0x64
#define CGU_REG_MSC0CDR		0x68
#define CGU_REG_I2SCDR1		0x70
#define CGU_REG_SSICDR		0x74
#define CGU_REG_CIMCDR		0x7c
#define CGU_REG_MSC1CDR		0xa4
#define CGU_REG_CMP_INTR	0xb0
#define CGU_REG_CMP_INTRE	0xb4
#define CGU_REG_DRCG		0xd0
#define CGU_REG_CPCSR		0xd4
#define CGU_REG_VPLL		0xe0
#define CGU_REG_MACPHYC		0xe8

/* bits within the OPCR register */
#define OPCR_GATE_USBPHYCLK	BIT(23)
#define OPCR_SPENDN0		BIT(7)
#define OPCR_SPENDN1		BIT(6)

/* bits within the USBPCR register */
#define USBPCR_SIDDQ		BIT(21)
#define USBPCR_OTG_DISABLE	BIT(20)

static struct ingenic_cgu *cgu;

static int x1830_usb_phy_enable(struct clk_hw *hw)
{
	void __iomem *reg_opcr		= cgu->base + CGU_REG_OPCR;
	void __iomem *reg_usbpcr	= cgu->base + CGU_REG_USBPCR;

	writel((readl(reg_opcr) | OPCR_SPENDN0) & ~OPCR_GATE_USBPHYCLK, reg_opcr);
	writel(readl(reg_usbpcr) & ~USBPCR_OTG_DISABLE & ~USBPCR_SIDDQ, reg_usbpcr);
	return 0;
}

static void x1830_usb_phy_disable(struct clk_hw *hw)
{
	void __iomem *reg_opcr		= cgu->base + CGU_REG_OPCR;
	void __iomem *reg_usbpcr	= cgu->base + CGU_REG_USBPCR;

	writel((readl(reg_opcr) & ~OPCR_SPENDN0) | OPCR_GATE_USBPHYCLK, reg_opcr);
	writel(readl(reg_usbpcr) | USBPCR_OTG_DISABLE | USBPCR_SIDDQ, reg_usbpcr);
}

static int x1830_usb_phy_is_enabled(struct clk_hw *hw)
{
	void __iomem *reg_opcr		= cgu->base + CGU_REG_OPCR;
	void __iomem *reg_usbpcr	= cgu->base + CGU_REG_USBPCR;

	return (readl(reg_opcr) & OPCR_SPENDN0) &&
		!(readl(reg_usbpcr) & USBPCR_SIDDQ) &&
		!(readl(reg_usbpcr) & USBPCR_OTG_DISABLE);
}

static const struct clk_ops x1830_otg_phy_ops = {
	.enable		= x1830_usb_phy_enable,
	.disable	= x1830_usb_phy_disable,
	.is_enabled	= x1830_usb_phy_is_enabled,
};

static const s8 pll_od_encoding[64] = {
	0x0, 0x1,  -1, 0x2,  -1,  -1,  -1, 0x3,
	 -1,  -1,  -1,  -1,  -1,  -1,  -1, 0x4,
	 -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	 -1,  -1,  -1,  -1,  -1,  -1,  -1, 0x5,
	 -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	 -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	 -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	 -1,  -1,  -1,  -1,  -1,  -1,  -1, 0x6,
};

static const struct ingenic_cgu_clk_info x1830_cgu_clocks[] = {

	/* External clocks */

	[X1830_CLK_EXCLK] = { "ext", CGU_CLK_EXT },
	[X1830_CLK_RTCLK] = { "rtc", CGU_CLK_EXT },

	/* PLLs */

	[X1830_CLK_APLL] = {
		"apll", CGU_CLK_PLL,
		.parents = { X1830_CLK_EXCLK, -1, -1, -1 },
		.pll = {
			.reg = CGU_REG_APLL,
			.rate_multiplier = 2,
			.m_shift = 20,
			.m_bits = 9,
			.m_offset = 1,
			.n_shift = 14,
			.n_bits = 6,
			.n_offset = 1,
			.od_shift = 11,
			.od_bits = 3,
			.od_max = 64,
			.od_encoding = pll_od_encoding,
			.bypass_reg = CGU_REG_CPPCR,
			.bypass_bit = 30,
			.enable_bit = 0,
			.stable_bit = 3,
		},
	},

	[X1830_CLK_MPLL] = {
		"mpll", CGU_CLK_PLL,
		.parents = { X1830_CLK_EXCLK, -1, -1, -1 },
		.pll = {
			.reg = CGU_REG_MPLL,
			.rate_multiplier = 2,
			.m_shift = 20,
			.m_bits = 9,
			.m_offset = 1,
			.n_shift = 14,
			.n_bits = 6,
			.n_offset = 1,
			.od_shift = 11,
			.od_bits = 3,
			.od_max = 64,
			.od_encoding = pll_od_encoding,
			.bypass_reg = CGU_REG_CPPCR,
			.bypass_bit = 28,
			.enable_bit = 0,
			.stable_bit = 3,
		},
	},

	[X1830_CLK_EPLL] = {
		"epll", CGU_CLK_PLL,
		.parents = { X1830_CLK_EXCLK, -1, -1, -1 },
		.pll = {
			.reg = CGU_REG_EPLL,
			.rate_multiplier = 2,
			.m_shift = 20,
			.m_bits = 9,
			.m_offset = 1,
			.n_shift = 14,
			.n_bits = 6,
			.n_offset = 1,
			.od_shift = 11,
			.od_bits = 3,
			.od_max = 64,
			.od_encoding = pll_od_encoding,
			.bypass_reg = CGU_REG_CPPCR,
			.bypass_bit = 24,
			.enable_bit = 0,
			.stable_bit = 3,
		},
	},

	[X1830_CLK_VPLL] = {
		"vpll", CGU_CLK_PLL,
		.parents = { X1830_CLK_EXCLK, -1, -1, -1 },
		.pll = {
			.reg = CGU_REG_VPLL,
			.rate_multiplier = 2,
			.m_shift = 20,
			.m_bits = 9,
			.m_offset = 1,
			.n_shift = 14,
			.n_bits = 6,
			.n_offset = 1,
			.od_shift = 11,
			.od_bits = 3,
			.od_max = 64,
			.od_encoding = pll_od_encoding,
			.bypass_reg = CGU_REG_CPPCR,
			.bypass_bit = 26,
			.enable_bit = 0,
			.stable_bit = 3,
		},
	},

	/* Custom (SoC-specific) OTG PHY */

	[X1830_CLK_OTGPHY] = {
		"otg_phy", CGU_CLK_CUSTOM,
		.parents = { X1830_CLK_EXCLK, -1, -1, -1 },
		.custom = { &x1830_otg_phy_ops },
	},

	/* Muxes & dividers */

	[X1830_CLK_SCLKA] = {
		"sclk_a", CGU_CLK_MUX,
		.parents = { -1, X1830_CLK_EXCLK, X1830_CLK_APLL, -1 },
		.mux = { CGU_REG_CPCCR, 30, 2 },
	},

	[X1830_CLK_CPUMUX] = {
		"cpu_mux", CGU_CLK_MUX,
		.parents = { -1, X1830_CLK_SCLKA, X1830_CLK_MPLL, -1 },
		.mux = { CGU_REG_CPCCR, 28, 2 },
	},

	[X1830_CLK_CPU] = {
		"cpu", CGU_CLK_DIV | CGU_CLK_GATE,
		.flags = CLK_IS_CRITICAL,
		.parents = { X1830_CLK_CPUMUX, -1, -1, -1 },
		.div = { CGU_REG_CPCCR, 0, 1, 4, 22, -1, -1 },
		.gate = { CGU_REG_CLKGR1, 15 },
	},

	[X1830_CLK_L2CACHE] = {
		"l2cache", CGU_CLK_DIV,
		/*
		 * The L2 cache clock is critical if caches are enabled and
		 * disabling it or any parent clocks will hang the system.
		 */
		.flags = CLK_IS_CRITICAL,
		.parents = { X1830_CLK_CPUMUX, -1, -1, -1 },
		.div = { CGU_REG_CPCCR, 4, 1, 4, 22, -1, -1 },
	},

	[X1830_CLK_AHB0] = {
		"ahb0", CGU_CLK_MUX | CGU_CLK_DIV,
		.parents = { -1, X1830_CLK_SCLKA, X1830_CLK_MPLL, -1 },
		.mux = { CGU_REG_CPCCR, 26, 2 },
		.div = { CGU_REG_CPCCR, 8, 1, 4, 21, -1, -1 },
	},

	[X1830_CLK_AHB2PMUX] = {
		"ahb2_apb_mux", CGU_CLK_MUX,
		.parents = { -1, X1830_CLK_SCLKA, X1830_CLK_MPLL, -1 },
		.mux = { CGU_REG_CPCCR, 24, 2 },
	},

	[X1830_CLK_AHB2] = {
		"ahb2", CGU_CLK_DIV,
		.parents = { X1830_CLK_AHB2PMUX, -1, -1, -1 },
		.div = { CGU_REG_CPCCR, 12, 1, 4, 20, -1, -1 },
	},

	[X1830_CLK_PCLK] = {
		"pclk", CGU_CLK_DIV | CGU_CLK_GATE,
		.parents = { X1830_CLK_AHB2PMUX, -1, -1, -1 },
		.div = { CGU_REG_CPCCR, 16, 1, 4, 20, -1, -1 },
		.gate = { CGU_REG_CLKGR1, 14 },
	},

	[X1830_CLK_DDR] = {
		"ddr", CGU_CLK_MUX | CGU_CLK_DIV | CGU_CLK_GATE,
		/*
		 * Disabling DDR clock or its parents will render DRAM
		 * inaccessible; mark it critical.
		 */
		.flags = CLK_IS_CRITICAL,
		.parents = { -1, X1830_CLK_SCLKA, X1830_CLK_MPLL, -1 },
		.mux = { CGU_REG_DDRCDR, 30, 2 },
		.div = { CGU_REG_DDRCDR, 0, 1, 4, 29, 28, 27 },
		.gate = { CGU_REG_CLKGR0, 31 },
	},

	[X1830_CLK_MAC] = {
		"mac", CGU_CLK_MUX | CGU_CLK_DIV | CGU_CLK_GATE,
		.parents = { X1830_CLK_SCLKA, X1830_CLK_MPLL,
					 X1830_CLK_VPLL, X1830_CLK_EPLL },
		.mux = { CGU_REG_MACCDR, 30, 2 },
		.div = { CGU_REG_MACCDR, 0, 1, 8, 29, 28, 27 },
		.gate = { CGU_REG_CLKGR1, 4 },
	},

	[X1830_CLK_LCD] = {
		"lcd", CGU_CLK_MUX | CGU_CLK_DIV | CGU_CLK_GATE,
		.parents = { X1830_CLK_SCLKA, X1830_CLK_MPLL,
					 X1830_CLK_VPLL, X1830_CLK_EPLL },
		.mux = { CGU_REG_LPCDR, 30, 2 },
		.div = { CGU_REG_LPCDR, 0, 1, 8, 28, 27, 26 },
		.gate = { CGU_REG_CLKGR1, 9 },
	},

	[X1830_CLK_MSCMUX] = {
		"msc_mux", CGU_CLK_MUX,
		.parents = { X1830_CLK_SCLKA, X1830_CLK_MPLL,
					 X1830_CLK_VPLL, X1830_CLK_EPLL },
		.mux = { CGU_REG_MSC0CDR, 30, 2 },
	},

	[X1830_CLK_MSC0] = {
		"msc0", CGU_CLK_DIV | CGU_CLK_GATE,
		.parents = { X1830_CLK_MSCMUX, -1, -1, -1 },
		.div = { CGU_REG_MSC0CDR, 0, 2, 8, 29, 28, 27 },
		.gate = { CGU_REG_CLKGR0, 4 },
	},

	[X1830_CLK_MSC1] = {
		"msc1", CGU_CLK_DIV | CGU_CLK_GATE,
		.parents = { X1830_CLK_MSCMUX, -1, -1, -1 },
		.div = { CGU_REG_MSC1CDR, 0, 2, 8, 29, 28, 27 },
		.gate = { CGU_REG_CLKGR0, 5 },
	},

	[X1830_CLK_SSIPLL] = {
		"ssi_pll", CGU_CLK_MUX | CGU_CLK_DIV,
		.parents = { X1830_CLK_SCLKA, X1830_CLK_MPLL,
					 X1830_CLK_VPLL, X1830_CLK_EPLL },
		.mux = { CGU_REG_SSICDR, 30, 2 },
		.div = { CGU_REG_SSICDR, 0, 1, 8, 28, 27, 26 },
	},

	[X1830_CLK_SSIPLL_DIV2] = {
		"ssi_pll_div2", CGU_CLK_FIXDIV,
		.parents = { X1830_CLK_SSIPLL },
		.fixdiv = { 2 },
	},

	[X1830_CLK_SSIMUX] = {
		"ssi_mux", CGU_CLK_MUX,
		.parents = { X1830_CLK_EXCLK, X1830_CLK_SSIPLL_DIV2, -1, -1 },
		.mux = { CGU_REG_SSICDR, 29, 1 },
	},

	[X1830_CLK_EXCLK_DIV512] = {
		"exclk_div512", CGU_CLK_FIXDIV,
		.parents = { X1830_CLK_EXCLK },
		.fixdiv = { 512 },
	},

	[X1830_CLK_RTC] = {
		"rtc_ercs", CGU_CLK_MUX | CGU_CLK_GATE,
		.parents = { X1830_CLK_EXCLK_DIV512, X1830_CLK_RTCLK },
		.mux = { CGU_REG_OPCR, 2, 1},
		.gate = { CGU_REG_CLKGR0, 29 },
	},

	/* Gate-only clocks */

	[X1830_CLK_EMC] = {
		"emc", CGU_CLK_GATE,
		.parents = { X1830_CLK_AHB2, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR0, 0 },
	},

	[X1830_CLK_EFUSE] = {
		"efuse", CGU_CLK_GATE,
		.parents = { X1830_CLK_AHB2, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR0, 1 },
	},

	[X1830_CLK_OTG] = {
		"otg", CGU_CLK_GATE,
		.parents = { X1830_CLK_EXCLK, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR0, 3 },
	},

	[X1830_CLK_SSI0] = {
		"ssi0", CGU_CLK_GATE,
		.parents = { X1830_CLK_SSIMUX, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR0, 6 },
	},

	[X1830_CLK_SMB0] = {
		"smb0", CGU_CLK_GATE,
		.parents = { X1830_CLK_PCLK, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR0, 7 },
	},

	[X1830_CLK_SMB1] = {
		"smb1", CGU_CLK_GATE,
		.parents = { X1830_CLK_PCLK, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR0, 8 },
	},

	[X1830_CLK_SMB2] = {
		"smb2", CGU_CLK_GATE,
		.parents = { X1830_CLK_PCLK, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR0, 9 },
	},

	[X1830_CLK_UART0] = {
		"uart0", CGU_CLK_GATE,
		.parents = { X1830_CLK_EXCLK, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR0, 14 },
	},

	[X1830_CLK_UART1] = {
		"uart1", CGU_CLK_GATE,
		.parents = { X1830_CLK_EXCLK, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR0, 15 },
	},

	[X1830_CLK_SSI1] = {
		"ssi1", CGU_CLK_GATE,
		.parents = { X1830_CLK_SSIMUX, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR0, 19 },
	},

	[X1830_CLK_SFC] = {
		"sfc", CGU_CLK_GATE,
		.parents = { X1830_CLK_SSIPLL, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR0, 20 },
	},

	[X1830_CLK_PDMA] = {
		"pdma", CGU_CLK_GATE,
		.parents = { X1830_CLK_EXCLK, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR0, 21 },
	},

	[X1830_CLK_TCU] = {
		"tcu", CGU_CLK_GATE,
		.parents = { X1830_CLK_EXCLK, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR0, 30 },
	},

	[X1830_CLK_DTRNG] = {
		"dtrng", CGU_CLK_GATE,
		.parents = { X1830_CLK_PCLK, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR1, 1 },
	},

	[X1830_CLK_OST] = {
		"ost", CGU_CLK_GATE,
		.parents = { X1830_CLK_EXCLK, -1, -1, -1 },
		.gate = { CGU_REG_CLKGR1, 11 },
	},
};

static void __init x1830_cgu_init(struct device_node *np)
{
	int retval;

	cgu = ingenic_cgu_new(x1830_cgu_clocks,
			      ARRAY_SIZE(x1830_cgu_clocks), np);
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
CLK_OF_DECLARE_DRIVER(x1830_cgu, "ingenic,x1830-cgu", x1830_cgu_init);
