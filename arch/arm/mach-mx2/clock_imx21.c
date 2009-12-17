/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
 * Copyright 2008 Martin Fuzzey, mfuzzey@gmail.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>

#include <mach/clock.h>
#include <mach/hardware.h>
#include <mach/common.h>
#include <asm/clkdev.h>
#include <asm/div64.h>

#define IO_ADDR_CCM(off)	(MX21_IO_ADDRESS(MX21_CCM_BASE_ADDR + (off)))

/* Register offsets */
#define CCM_CSCR		IO_ADDR_CCM(0x0)
#define CCM_MPCTL0		IO_ADDR_CCM(0x4)
#define CCM_MPCTL1		IO_ADDR_CCM(0x8)
#define CCM_SPCTL0		IO_ADDR_CCM(0xc)
#define CCM_SPCTL1		IO_ADDR_CCM(0x10)
#define CCM_OSC26MCTL		IO_ADDR_CCM(0x14)
#define CCM_PCDR0		IO_ADDR_CCM(0x18)
#define CCM_PCDR1		IO_ADDR_CCM(0x1c)
#define CCM_PCCR0		IO_ADDR_CCM(0x20)
#define CCM_PCCR1		IO_ADDR_CCM(0x24)
#define CCM_CCSR		IO_ADDR_CCM(0x28)
#define CCM_PMCTL		IO_ADDR_CCM(0x2c)
#define CCM_PMCOUNT		IO_ADDR_CCM(0x30)
#define CCM_WKGDCTL		IO_ADDR_CCM(0x34)

#define CCM_CSCR_PRESC_OFFSET	29
#define CCM_CSCR_PRESC_MASK	(0x7 << CCM_CSCR_PRESC_OFFSET)

#define CCM_CSCR_USB_OFFSET	26
#define CCM_CSCR_USB_MASK	(0x7 << CCM_CSCR_USB_OFFSET)
#define CCM_CSCR_SD_OFFSET	24
#define CCM_CSCR_SD_MASK	(0x3 << CCM_CSCR_SD_OFFSET)
#define CCM_CSCR_SPLLRES	(1 << 22)
#define CCM_CSCR_MPLLRES	(1 << 21)
#define CCM_CSCR_SSI2_OFFSET	20
#define CCM_CSCR_SSI2		(1 << CCM_CSCR_SSI2_OFFSET)
#define CCM_CSCR_SSI1_OFFSET	19
#define CCM_CSCR_SSI1		(1 << CCM_CSCR_SSI1_OFFSET)
#define CCM_CSCR_FIR_OFFSET	18
#define CCM_CSCR_FIR		(1 << CCM_CSCR_FIR_OFFSET)
#define CCM_CSCR_SP		(1 << 17)
#define CCM_CSCR_MCU		(1 << 16)
#define CCM_CSCR_BCLK_OFFSET	10
#define CCM_CSCR_BCLK_MASK	(0xf << CCM_CSCR_BCLK_OFFSET)
#define CCM_CSCR_IPDIV_OFFSET	9
#define CCM_CSCR_IPDIV		(1 << CCM_CSCR_IPDIV_OFFSET)

#define CCM_CSCR_OSC26MDIV	(1 << 4)
#define CCM_CSCR_OSC26M		(1 << 3)
#define CCM_CSCR_FPM		(1 << 2)
#define CCM_CSCR_SPEN		(1 << 1)
#define CCM_CSCR_MPEN		1

#define CCM_MPCTL0_CPLM		(1 << 31)
#define CCM_MPCTL0_PD_OFFSET	26
#define CCM_MPCTL0_PD_MASK	(0xf << 26)
#define CCM_MPCTL0_MFD_OFFSET	16
#define CCM_MPCTL0_MFD_MASK	(0x3ff << 16)
#define CCM_MPCTL0_MFI_OFFSET	10
#define CCM_MPCTL0_MFI_MASK	(0xf << 10)
#define CCM_MPCTL0_MFN_OFFSET	0
#define CCM_MPCTL0_MFN_MASK	0x3ff

#define CCM_MPCTL1_LF		(1 << 15)
#define CCM_MPCTL1_BRMO		(1 << 6)

#define CCM_SPCTL0_CPLM		(1 << 31)
#define CCM_SPCTL0_PD_OFFSET	26
#define CCM_SPCTL0_PD_MASK	(0xf << 26)
#define CCM_SPCTL0_MFD_OFFSET	16
#define CCM_SPCTL0_MFD_MASK	(0x3ff << 16)
#define CCM_SPCTL0_MFI_OFFSET	10
#define CCM_SPCTL0_MFI_MASK	(0xf << 10)
#define CCM_SPCTL0_MFN_OFFSET	0
#define CCM_SPCTL0_MFN_MASK	0x3ff

#define CCM_SPCTL1_LF		(1 << 15)
#define CCM_SPCTL1_BRMO		(1 << 6)

#define CCM_OSC26MCTL_PEAK_OFFSET	16
#define CCM_OSC26MCTL_PEAK_MASK		(0x3 << 16)
#define CCM_OSC26MCTL_AGC_OFFSET	8
#define CCM_OSC26MCTL_AGC_MASK		(0x3f << 8)
#define CCM_OSC26MCTL_ANATEST_OFFSET	0
#define CCM_OSC26MCTL_ANATEST_MASK	0x3f

#define CCM_PCDR0_SSI2BAUDDIV_OFFSET	26
#define CCM_PCDR0_SSI2BAUDDIV_MASK	(0x3f << 26)
#define CCM_PCDR0_SSI1BAUDDIV_OFFSET	16
#define CCM_PCDR0_SSI1BAUDDIV_MASK	(0x3f << 16)
#define CCM_PCDR0_NFCDIV_OFFSET		12
#define CCM_PCDR0_NFCDIV_MASK		(0xf << 12)
#define CCM_PCDR0_48MDIV_OFFSET		5
#define CCM_PCDR0_48MDIV_MASK		(0x7 << CCM_PCDR0_48MDIV_OFFSET)
#define CCM_PCDR0_FIRIDIV_OFFSET	0
#define CCM_PCDR0_FIRIDIV_MASK		0x1f
#define CCM_PCDR1_PERDIV4_OFFSET	24
#define CCM_PCDR1_PERDIV4_MASK		(0x3f << 24)
#define CCM_PCDR1_PERDIV3_OFFSET	16
#define CCM_PCDR1_PERDIV3_MASK		(0x3f << 16)
#define CCM_PCDR1_PERDIV2_OFFSET	8
#define CCM_PCDR1_PERDIV2_MASK		(0x3f << 8)
#define CCM_PCDR1_PERDIV1_OFFSET	0
#define CCM_PCDR1_PERDIV1_MASK		0x3f

#define CCM_PCCR_HCLK_CSI_OFFSET	31
#define CCM_PCCR_HCLK_CSI_REG		CCM_PCCR0
#define CCM_PCCR_HCLK_DMA_OFFSET	30
#define CCM_PCCR_HCLK_DMA_REG		CCM_PCCR0
#define CCM_PCCR_HCLK_BROM_OFFSET	28
#define CCM_PCCR_HCLK_BROM_REG		CCM_PCCR0
#define CCM_PCCR_HCLK_EMMA_OFFSET	27
#define CCM_PCCR_HCLK_EMMA_REG		CCM_PCCR0
#define CCM_PCCR_HCLK_LCDC_OFFSET	26
#define CCM_PCCR_HCLK_LCDC_REG		CCM_PCCR0
#define CCM_PCCR_HCLK_SLCDC_OFFSET	25
#define CCM_PCCR_HCLK_SLCDC_REG		CCM_PCCR0
#define CCM_PCCR_HCLK_USBOTG_OFFSET	24
#define CCM_PCCR_HCLK_USBOTG_REG	CCM_PCCR0
#define CCM_PCCR_HCLK_BMI_OFFSET	23
#define CCM_PCCR_BMI_MASK		(1 << CCM_PCCR_BMI_MASK)
#define CCM_PCCR_HCLK_BMI_REG		CCM_PCCR0
#define CCM_PCCR_PERCLK4_OFFSET		22
#define CCM_PCCR_PERCLK4_REG		CCM_PCCR0
#define CCM_PCCR_SLCDC_OFFSET		21
#define CCM_PCCR_SLCDC_REG		CCM_PCCR0
#define CCM_PCCR_FIRI_BAUD_OFFSET	20
#define CCM_PCCR_FIRI_BAUD_MASK		(1 << CCM_PCCR_FIRI_BAUD_MASK)
#define CCM_PCCR_FIRI_BAUD_REG		CCM_PCCR0
#define CCM_PCCR_NFC_OFFSET		19
#define CCM_PCCR_NFC_REG		CCM_PCCR0
#define CCM_PCCR_LCDC_OFFSET		18
#define CCM_PCCR_LCDC_REG		CCM_PCCR0
#define CCM_PCCR_SSI1_BAUD_OFFSET	17
#define CCM_PCCR_SSI1_BAUD_REG		CCM_PCCR0
#define CCM_PCCR_SSI2_BAUD_OFFSET	16
#define CCM_PCCR_SSI2_BAUD_REG		CCM_PCCR0
#define CCM_PCCR_EMMA_OFFSET		15
#define CCM_PCCR_EMMA_REG		CCM_PCCR0
#define CCM_PCCR_USBOTG_OFFSET		14
#define CCM_PCCR_USBOTG_REG		CCM_PCCR0
#define CCM_PCCR_DMA_OFFSET		13
#define CCM_PCCR_DMA_REG		CCM_PCCR0
#define CCM_PCCR_I2C1_OFFSET		12
#define CCM_PCCR_I2C1_REG		CCM_PCCR0
#define CCM_PCCR_GPIO_OFFSET		11
#define CCM_PCCR_GPIO_REG		CCM_PCCR0
#define CCM_PCCR_SDHC2_OFFSET		10
#define CCM_PCCR_SDHC2_REG		CCM_PCCR0
#define CCM_PCCR_SDHC1_OFFSET		9
#define CCM_PCCR_SDHC1_REG		CCM_PCCR0
#define CCM_PCCR_FIRI_OFFSET		8
#define CCM_PCCR_FIRI_MASK		(1 << CCM_PCCR_BAUD_MASK)
#define CCM_PCCR_FIRI_REG		CCM_PCCR0
#define CCM_PCCR_SSI2_IPG_OFFSET	7
#define CCM_PCCR_SSI2_REG		CCM_PCCR0
#define CCM_PCCR_SSI1_IPG_OFFSET	6
#define CCM_PCCR_SSI1_REG		CCM_PCCR0
#define CCM_PCCR_CSPI2_OFFSET		5
#define	CCM_PCCR_CSPI2_REG		CCM_PCCR0
#define CCM_PCCR_CSPI1_OFFSET		4
#define	CCM_PCCR_CSPI1_REG		CCM_PCCR0
#define CCM_PCCR_UART4_OFFSET		3
#define CCM_PCCR_UART4_REG		CCM_PCCR0
#define CCM_PCCR_UART3_OFFSET		2
#define CCM_PCCR_UART3_REG		CCM_PCCR0
#define CCM_PCCR_UART2_OFFSET		1
#define CCM_PCCR_UART2_REG		CCM_PCCR0
#define CCM_PCCR_UART1_OFFSET		0
#define CCM_PCCR_UART1_REG		CCM_PCCR0

#define CCM_PCCR_OWIRE_OFFSET		31
#define CCM_PCCR_OWIRE_REG		CCM_PCCR1
#define CCM_PCCR_KPP_OFFSET		30
#define CCM_PCCR_KPP_REG		CCM_PCCR1
#define CCM_PCCR_RTC_OFFSET		29
#define CCM_PCCR_RTC_REG		CCM_PCCR1
#define CCM_PCCR_PWM_OFFSET		28
#define CCM_PCCR_PWM_REG		CCM_PCCR1
#define CCM_PCCR_GPT3_OFFSET		27
#define CCM_PCCR_GPT3_REG		CCM_PCCR1
#define CCM_PCCR_GPT2_OFFSET		26
#define CCM_PCCR_GPT2_REG		CCM_PCCR1
#define CCM_PCCR_GPT1_OFFSET		25
#define CCM_PCCR_GPT1_REG		CCM_PCCR1
#define CCM_PCCR_WDT_OFFSET		24
#define CCM_PCCR_WDT_REG		CCM_PCCR1
#define CCM_PCCR_CSPI3_OFFSET		23
#define	CCM_PCCR_CSPI3_REG		CCM_PCCR1

#define CCM_PCCR_CSPI1_MASK		(1 << CCM_PCCR_CSPI1_OFFSET)
#define CCM_PCCR_CSPI2_MASK		(1 << CCM_PCCR_CSPI2_OFFSET)
#define CCM_PCCR_CSPI3_MASK		(1 << CCM_PCCR_CSPI3_OFFSET)
#define CCM_PCCR_DMA_MASK		(1 << CCM_PCCR_DMA_OFFSET)
#define CCM_PCCR_EMMA_MASK		(1 << CCM_PCCR_EMMA_OFFSET)
#define CCM_PCCR_GPIO_MASK		(1 << CCM_PCCR_GPIO_OFFSET)
#define CCM_PCCR_GPT1_MASK		(1 << CCM_PCCR_GPT1_OFFSET)
#define CCM_PCCR_GPT2_MASK		(1 << CCM_PCCR_GPT2_OFFSET)
#define CCM_PCCR_GPT3_MASK		(1 << CCM_PCCR_GPT3_OFFSET)
#define CCM_PCCR_HCLK_BROM_MASK		(1 << CCM_PCCR_HCLK_BROM_OFFSET)
#define CCM_PCCR_HCLK_CSI_MASK		(1 << CCM_PCCR_HCLK_CSI_OFFSET)
#define CCM_PCCR_HCLK_DMA_MASK		(1 << CCM_PCCR_HCLK_DMA_OFFSET)
#define CCM_PCCR_HCLK_EMMA_MASK		(1 << CCM_PCCR_HCLK_EMMA_OFFSET)
#define CCM_PCCR_HCLK_LCDC_MASK		(1 << CCM_PCCR_HCLK_LCDC_OFFSET)
#define CCM_PCCR_HCLK_SLCDC_MASK	(1 << CCM_PCCR_HCLK_SLCDC_OFFSET)
#define CCM_PCCR_HCLK_USBOTG_MASK	(1 << CCM_PCCR_HCLK_USBOTG_OFFSET)
#define CCM_PCCR_I2C1_MASK		(1 << CCM_PCCR_I2C1_OFFSET)
#define CCM_PCCR_KPP_MASK		(1 << CCM_PCCR_KPP_OFFSET)
#define CCM_PCCR_LCDC_MASK		(1 << CCM_PCCR_LCDC_OFFSET)
#define CCM_PCCR_NFC_MASK		(1 << CCM_PCCR_NFC_OFFSET)
#define CCM_PCCR_OWIRE_MASK		(1 << CCM_PCCR_OWIRE_OFFSET)
#define CCM_PCCR_PERCLK4_MASK		(1 << CCM_PCCR_PERCLK4_OFFSET)
#define CCM_PCCR_PWM_MASK		(1 << CCM_PCCR_PWM_OFFSET)
#define CCM_PCCR_RTC_MASK		(1 << CCM_PCCR_RTC_OFFSET)
#define CCM_PCCR_SDHC1_MASK		(1 << CCM_PCCR_SDHC1_OFFSET)
#define CCM_PCCR_SDHC2_MASK		(1 << CCM_PCCR_SDHC2_OFFSET)
#define CCM_PCCR_SLCDC_MASK		(1 << CCM_PCCR_SLCDC_OFFSET)
#define CCM_PCCR_SSI1_BAUD_MASK		(1 << CCM_PCCR_SSI1_BAUD_OFFSET)
#define CCM_PCCR_SSI1_IPG_MASK		(1 << CCM_PCCR_SSI1_IPG_OFFSET)
#define CCM_PCCR_SSI2_BAUD_MASK		(1 << CCM_PCCR_SSI2_BAUD_OFFSET)
#define CCM_PCCR_SSI2_IPG_MASK		(1 << CCM_PCCR_SSI2_IPG_OFFSET)
#define CCM_PCCR_UART1_MASK		(1 << CCM_PCCR_UART1_OFFSET)
#define CCM_PCCR_UART2_MASK		(1 << CCM_PCCR_UART2_OFFSET)
#define CCM_PCCR_UART3_MASK		(1 << CCM_PCCR_UART3_OFFSET)
#define CCM_PCCR_UART4_MASK		(1 << CCM_PCCR_UART4_OFFSET)
#define CCM_PCCR_USBOTG_MASK		(1 << CCM_PCCR_USBOTG_OFFSET)
#define CCM_PCCR_WDT_MASK		(1 << CCM_PCCR_WDT_OFFSET)

#define CCM_CCSR_32KSR		(1 << 15)

#define CCM_CCSR_CLKMODE1	(1 << 9)
#define CCM_CCSR_CLKMODE0	(1 << 8)

#define CCM_CCSR_CLKOSEL_OFFSET 0
#define CCM_CCSR_CLKOSEL_MASK	0x1f

#define SYS_FMCR		0x14	/* Functional Muxing Control Reg */
#define SYS_CHIP_ID		0x00	/* The offset of CHIP ID register */

static int _clk_enable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(clk->enable_reg);
	reg |= 1 << clk->enable_shift;
	__raw_writel(reg, clk->enable_reg);
	return 0;
}

static void _clk_disable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(clk->enable_reg);
	reg &= ~(1 << clk->enable_shift);
	__raw_writel(reg, clk->enable_reg);
}

static unsigned long _clk_generic_round_rate(struct clk *clk,
			unsigned long rate,
			u32 max_divisor)
{
	u32 div;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	div = parent_rate / rate;
	if (parent_rate % rate)
		div++;

	if (div > max_divisor)
		div = max_divisor;

	return parent_rate / div;
}

static int _clk_spll_enable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(CCM_CSCR);
	reg |= CCM_CSCR_SPEN;
	__raw_writel(reg, CCM_CSCR);

	while ((__raw_readl(CCM_SPCTL1) & CCM_SPCTL1_LF) == 0)
		;
	return 0;
}

static void _clk_spll_disable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(CCM_CSCR);
	reg &= ~CCM_CSCR_SPEN;
	__raw_writel(reg, CCM_CSCR);
}


#define CSCR() (__raw_readl(CCM_CSCR))
#define PCDR0() (__raw_readl(CCM_PCDR0))
#define PCDR1() (__raw_readl(CCM_PCDR1))

static unsigned long _clk_perclkx_round_rate(struct clk *clk,
					     unsigned long rate)
{
	return _clk_generic_round_rate(clk, rate, 64);
}

static int _clk_perclkx_set_rate(struct clk *clk, unsigned long rate)
{
	u32 reg;
	u32 div;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	if (clk->id < 0 || clk->id > 3)
		return -EINVAL;

	div = parent_rate / rate;
	if (div > 64 || div < 1 || ((parent_rate / div) != rate))
		return -EINVAL;
	div--;

	reg =
	    __raw_readl(CCM_PCDR1) & ~(CCM_PCDR1_PERDIV1_MASK <<
				       (clk->id << 3));
	reg |= div << (clk->id << 3);
	__raw_writel(reg, CCM_PCDR1);

	return 0;
}

static unsigned long _clk_usb_recalc(struct clk *clk)
{
	unsigned long usb_pdf;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	usb_pdf = (CSCR() & CCM_CSCR_USB_MASK) >> CCM_CSCR_USB_OFFSET;

	return parent_rate / (usb_pdf + 1U);
}

static unsigned long _clk_usb_round_rate(struct clk *clk,
					     unsigned long rate)
{
	return _clk_generic_round_rate(clk, rate, 8);
}

static int _clk_usb_set_rate(struct clk *clk, unsigned long rate)
{
	u32 reg;
	u32 div;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	div = parent_rate / rate;
	if (div > 8 || div < 1 || ((parent_rate / div) != rate))
		return -EINVAL;
	div--;

	reg = CSCR() & ~CCM_CSCR_USB_MASK;
	reg |= div << CCM_CSCR_USB_OFFSET;
	__raw_writel(reg, CCM_CSCR);

	return 0;
}

static unsigned long _clk_ssix_recalc(struct clk *clk, unsigned long pdf)
{
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	pdf = (pdf < 2) ? 124UL : pdf;  /* MX21 & MX27 TO1 */

	return 2UL * parent_rate / pdf;
}

static unsigned long _clk_ssi1_recalc(struct clk *clk)
{
	return _clk_ssix_recalc(clk,
		(PCDR0() & CCM_PCDR0_SSI1BAUDDIV_MASK)
		>> CCM_PCDR0_SSI1BAUDDIV_OFFSET);
}

static unsigned long _clk_ssi2_recalc(struct clk *clk)
{
	return _clk_ssix_recalc(clk,
		(PCDR0() & CCM_PCDR0_SSI2BAUDDIV_MASK) >>
		CCM_PCDR0_SSI2BAUDDIV_OFFSET);
}

static unsigned long _clk_nfc_recalc(struct clk *clk)
{
	unsigned long nfc_pdf;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	nfc_pdf = (PCDR0() & CCM_PCDR0_NFCDIV_MASK)
		>> CCM_PCDR0_NFCDIV_OFFSET;

	return parent_rate / (nfc_pdf + 1);
}

static unsigned long _clk_parent_round_rate(struct clk *clk, unsigned long rate)
{
	return clk->parent->round_rate(clk->parent, rate);
}

static int _clk_parent_set_rate(struct clk *clk, unsigned long rate)
{
	return clk->parent->set_rate(clk->parent, rate);
}

static unsigned long external_high_reference; /* in Hz */

static unsigned long get_high_reference_clock_rate(struct clk *clk)
{
	return external_high_reference;
}

/*
 * the high frequency external clock reference
 * Default case is 26MHz.
 */
static struct clk ckih_clk = {
	.get_rate = get_high_reference_clock_rate,
};

static unsigned long external_low_reference; /* in Hz */

static unsigned long get_low_reference_clock_rate(struct clk *clk)
{
	return external_low_reference;
}

/*
 * the low frequency external clock reference
 * Default case is 32.768kHz.
 */
static struct clk ckil_clk = {
	.get_rate = get_low_reference_clock_rate,
};


static unsigned long _clk_fpm_recalc(struct clk *clk)
{
	return clk_get_rate(clk->parent) * 512;
}

/* Output of frequency pre multiplier */
static struct clk fpm_clk = {
	.parent = &ckil_clk,
	.get_rate = _clk_fpm_recalc,
};

static unsigned long get_mpll_clk(struct clk *clk)
{
	uint32_t reg;
	unsigned long ref_clk;
	unsigned long mfi = 0, mfn = 0, mfd = 0, pdf = 0;
	unsigned long long temp;

	ref_clk = clk_get_rate(clk->parent);

	reg = __raw_readl(CCM_MPCTL0);
	pdf = (reg & CCM_MPCTL0_PD_MASK)  >> CCM_MPCTL0_PD_OFFSET;
	mfd = (reg & CCM_MPCTL0_MFD_MASK) >> CCM_MPCTL0_MFD_OFFSET;
	mfi = (reg & CCM_MPCTL0_MFI_MASK) >> CCM_MPCTL0_MFI_OFFSET;
	mfn = (reg & CCM_MPCTL0_MFN_MASK) >> CCM_MPCTL0_MFN_OFFSET;

	mfi = (mfi <= 5) ? 5 : mfi;
	temp = 2LL * ref_clk * mfn;
	do_div(temp, mfd + 1);
	temp = 2LL * ref_clk * mfi + temp;
	do_div(temp, pdf + 1);

	return (unsigned long)temp;
}

static struct clk mpll_clk = {
	.parent = &ckih_clk,
	.get_rate = get_mpll_clk,
};

static unsigned long _clk_fclk_get_rate(struct clk *clk)
{
	unsigned long parent_rate;
	u32 div;

	div = (CSCR() & CCM_CSCR_PRESC_MASK) >> CCM_CSCR_PRESC_OFFSET;
	parent_rate = clk_get_rate(clk->parent);

	return parent_rate / (div+1);
}

static struct clk fclk_clk = {
	.parent = &mpll_clk,
	.get_rate = _clk_fclk_get_rate
};

static unsigned long get_spll_clk(struct clk *clk)
{
	uint32_t reg;
	unsigned long ref_clk;
	unsigned long mfi = 0, mfn = 0, mfd = 0, pdf = 0;
	unsigned long long temp;

	ref_clk = clk_get_rate(clk->parent);

	reg = __raw_readl(CCM_SPCTL0);
	pdf = (reg & CCM_SPCTL0_PD_MASK) >> CCM_SPCTL0_PD_OFFSET;
	mfd = (reg & CCM_SPCTL0_MFD_MASK) >> CCM_SPCTL0_MFD_OFFSET;
	mfi = (reg & CCM_SPCTL0_MFI_MASK) >> CCM_SPCTL0_MFI_OFFSET;
	mfn = (reg & CCM_SPCTL0_MFN_MASK) >> CCM_SPCTL0_MFN_OFFSET;

	mfi = (mfi <= 5) ? 5 : mfi;
	temp = 2LL * ref_clk * mfn;
	do_div(temp, mfd + 1);
	temp = 2LL * ref_clk * mfi + temp;
	do_div(temp, pdf + 1);

	return (unsigned long)temp;
}

static struct clk spll_clk = {
	.parent = &ckih_clk,
	.get_rate = get_spll_clk,
	.enable = _clk_spll_enable,
	.disable = _clk_spll_disable,
};

static unsigned long get_hclk_clk(struct clk *clk)
{
	unsigned long rate;
	unsigned long bclk_pdf;

	bclk_pdf = (CSCR() & CCM_CSCR_BCLK_MASK)
		>> CCM_CSCR_BCLK_OFFSET;

	rate = clk_get_rate(clk->parent);
	return rate / (bclk_pdf + 1);
}

static struct clk hclk_clk = {
	.parent = &fclk_clk,
	.get_rate = get_hclk_clk,
};

static unsigned long get_ipg_clk(struct clk *clk)
{
	unsigned long rate;
	unsigned long ipg_pdf;

	ipg_pdf = (CSCR() & CCM_CSCR_IPDIV) >> CCM_CSCR_IPDIV_OFFSET;

	rate = clk_get_rate(clk->parent);
	return rate / (ipg_pdf + 1);
}

static struct clk ipg_clk = {
	.parent = &hclk_clk,
	.get_rate = get_ipg_clk,
};

static unsigned long _clk_perclkx_recalc(struct clk *clk)
{
	unsigned long perclk_pdf;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	if (clk->id < 0 || clk->id > 3)
		return 0;

	perclk_pdf = (PCDR1() >> (clk->id << 3)) & CCM_PCDR1_PERDIV1_MASK;

	return parent_rate / (perclk_pdf + 1);
}

static struct clk per_clk[] = {
	{
		.id = 0,
		.parent = &mpll_clk,
		.get_rate = _clk_perclkx_recalc,
	}, {
		.id = 1,
		.parent = &mpll_clk,
		.get_rate = _clk_perclkx_recalc,
	}, {
		.id = 2,
		.parent = &mpll_clk,
		.round_rate = _clk_perclkx_round_rate,
		.set_rate = _clk_perclkx_set_rate,
		.get_rate = _clk_perclkx_recalc,
		/* Enable/Disable done via lcd_clkc[1] */
	}, {
		.id = 3,
		.parent = &mpll_clk,
		.round_rate = _clk_perclkx_round_rate,
		.set_rate = _clk_perclkx_set_rate,
		.get_rate = _clk_perclkx_recalc,
		/* Enable/Disable done via csi_clk[1] */
	},
};

static struct clk uart_ipg_clk[];

static struct clk uart_clk[] = {
	{
		.id = 0,
		.parent = &per_clk[0],
		.secondary = &uart_ipg_clk[0],
	}, {
		.id = 1,
		.parent = &per_clk[0],
		.secondary = &uart_ipg_clk[1],
	}, {
		.id = 2,
		.parent = &per_clk[0],
		.secondary = &uart_ipg_clk[2],
	}, {
		.id = 3,
		.parent = &per_clk[0],
		.secondary = &uart_ipg_clk[3],
	},
};

static struct clk uart_ipg_clk[] = {
	{
		.id = 0,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_UART1_REG,
		.enable_shift = CCM_PCCR_UART1_OFFSET,
		.disable = _clk_disable,
	}, {
		.id = 1,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_UART2_REG,
		.enable_shift = CCM_PCCR_UART2_OFFSET,
		.disable = _clk_disable,
	}, {
		.id = 2,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_UART3_REG,
		.enable_shift = CCM_PCCR_UART3_OFFSET,
		.disable = _clk_disable,
	}, {
		.id = 3,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_UART4_REG,
		.enable_shift = CCM_PCCR_UART4_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk gpt_ipg_clk[];

static struct clk gpt_clk[] = {
	{
		.id = 0,
		.parent = &per_clk[0],
		.secondary = &gpt_ipg_clk[0],
	}, {
		.id = 1,
		.parent = &per_clk[0],
		.secondary = &gpt_ipg_clk[1],
	}, {
		.id = 2,
		.parent = &per_clk[0],
		.secondary = &gpt_ipg_clk[2],
	},
};

static struct clk gpt_ipg_clk[] = {
	{
		.id = 0,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_GPT1_REG,
		.enable_shift = CCM_PCCR_GPT1_OFFSET,
		.disable = _clk_disable,
	}, {
		.id = 1,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_GPT2_REG,
		.enable_shift = CCM_PCCR_GPT2_OFFSET,
		.disable = _clk_disable,
	}, {
		.id = 2,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_GPT3_REG,
		.enable_shift = CCM_PCCR_GPT3_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk pwm_clk[] = {
	{
		.parent = &per_clk[0],
		.secondary = &pwm_clk[1],
	}, {
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_PWM_REG,
		.enable_shift = CCM_PCCR_PWM_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk sdhc_ipg_clk[];

static struct clk sdhc_clk[] = {
	{
		.id = 0,
		.parent = &per_clk[1],
		.secondary = &sdhc_ipg_clk[0],
	}, {
		.id = 1,
		.parent = &per_clk[1],
		.secondary = &sdhc_ipg_clk[1],
	},
};

static struct clk sdhc_ipg_clk[] = {
	{
		.id = 0,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_SDHC1_REG,
		.enable_shift = CCM_PCCR_SDHC1_OFFSET,
		.disable = _clk_disable,
	}, {
		.id = 1,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_SDHC2_REG,
		.enable_shift = CCM_PCCR_SDHC2_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk cspi_ipg_clk[];

static struct clk cspi_clk[] = {
	{
		.id = 0,
		.parent = &per_clk[1],
		.secondary = &cspi_ipg_clk[0],
	}, {
		.id = 1,
		.parent = &per_clk[1],
		.secondary = &cspi_ipg_clk[1],
	}, {
		.id = 2,
		.parent = &per_clk[1],
		.secondary = &cspi_ipg_clk[2],
	},
};

static struct clk cspi_ipg_clk[] = {
	{
		.id = 0,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_CSPI1_REG,
		.enable_shift = CCM_PCCR_CSPI1_OFFSET,
		.disable = _clk_disable,
	}, {
		.id = 1,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_CSPI2_REG,
		.enable_shift = CCM_PCCR_CSPI2_OFFSET,
		.disable = _clk_disable,
	}, {
		.id = 3,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_CSPI3_REG,
		.enable_shift = CCM_PCCR_CSPI3_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk lcdc_clk[] = {
	{
		.parent = &per_clk[2],
		.secondary = &lcdc_clk[1],
		.round_rate = _clk_parent_round_rate,
		.set_rate = _clk_parent_set_rate,
	}, {
		.parent = &ipg_clk,
		.secondary = &lcdc_clk[2],
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_LCDC_REG,
		.enable_shift = CCM_PCCR_LCDC_OFFSET,
		.disable = _clk_disable,
	}, {
		.parent = &hclk_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_HCLK_LCDC_REG,
		.enable_shift = CCM_PCCR_HCLK_LCDC_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk csi_clk[] = {
	{
		.parent = &per_clk[3],
		.secondary = &csi_clk[1],
		.round_rate = _clk_parent_round_rate,
		.set_rate = _clk_parent_set_rate,
	}, {
		.parent = &hclk_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_HCLK_CSI_REG,
		.enable_shift = CCM_PCCR_HCLK_CSI_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk usb_clk[] = {
	{
		.parent = &spll_clk,
		.secondary = &usb_clk[1],
		.get_rate = _clk_usb_recalc,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_USBOTG_REG,
		.enable_shift = CCM_PCCR_USBOTG_OFFSET,
		.disable = _clk_disable,
		.round_rate = _clk_usb_round_rate,
		.set_rate = _clk_usb_set_rate,
	}, {
		.parent = &hclk_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_HCLK_USBOTG_REG,
		.enable_shift = CCM_PCCR_HCLK_USBOTG_OFFSET,
		.disable = _clk_disable,
	}
};

static struct clk ssi_ipg_clk[];

static struct clk ssi_clk[] = {
	{
		.id = 0,
		.parent = &mpll_clk,
		.secondary = &ssi_ipg_clk[0],
		.get_rate = _clk_ssi1_recalc,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_SSI1_BAUD_REG,
		.enable_shift = CCM_PCCR_SSI1_BAUD_OFFSET,
		.disable = _clk_disable,
	}, {
		.id = 1,
		.parent = &mpll_clk,
		.secondary = &ssi_ipg_clk[1],
		.get_rate = _clk_ssi2_recalc,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_SSI2_BAUD_REG,
		.enable_shift = CCM_PCCR_SSI2_BAUD_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk ssi_ipg_clk[] = {
	{
		.id = 0,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_SSI1_REG,
		.enable_shift = CCM_PCCR_SSI1_IPG_OFFSET,
		.disable = _clk_disable,
	}, {
		.id = 1,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_SSI2_REG,
		.enable_shift = CCM_PCCR_SSI2_IPG_OFFSET,
		.disable = _clk_disable,
	},
};


static struct clk nfc_clk = {
	.parent = &fclk_clk,
	.get_rate = _clk_nfc_recalc,
	.enable = _clk_enable,
	.enable_reg = CCM_PCCR_NFC_REG,
	.enable_shift = CCM_PCCR_NFC_OFFSET,
	.disable = _clk_disable,
};

static struct clk dma_clk[] = {
	{
		.parent = &hclk_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_DMA_REG,
		.enable_shift = CCM_PCCR_DMA_OFFSET,
		.disable = _clk_disable,
		.secondary = &dma_clk[1],
	},  {
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_HCLK_DMA_REG,
		.enable_shift = CCM_PCCR_HCLK_DMA_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk brom_clk = {
	.parent = &hclk_clk,
	.enable = _clk_enable,
	.enable_reg = CCM_PCCR_HCLK_BROM_REG,
	.enable_shift = CCM_PCCR_HCLK_BROM_OFFSET,
	.disable = _clk_disable,
};

static struct clk emma_clk[] = {
	{
		.parent = &hclk_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_EMMA_REG,
		.enable_shift = CCM_PCCR_EMMA_OFFSET,
		.disable = _clk_disable,
		.secondary = &emma_clk[1],
	}, {
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_HCLK_EMMA_REG,
		.enable_shift = CCM_PCCR_HCLK_EMMA_OFFSET,
		.disable = _clk_disable,
	}
};

static struct clk slcdc_clk[] = {
	{
		.parent = &hclk_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_SLCDC_REG,
		.enable_shift = CCM_PCCR_SLCDC_OFFSET,
		.disable = _clk_disable,
		.secondary = &slcdc_clk[1],
	}, {
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR_HCLK_SLCDC_REG,
		.enable_shift = CCM_PCCR_HCLK_SLCDC_OFFSET,
		.disable = _clk_disable,
	}
};

static struct clk wdog_clk = {
	.parent = &ipg_clk,
	.enable = _clk_enable,
	.enable_reg = CCM_PCCR_WDT_REG,
	.enable_shift = CCM_PCCR_WDT_OFFSET,
	.disable = _clk_disable,
};

static struct clk gpio_clk = {
	.parent = &ipg_clk,
	.enable = _clk_enable,
	.enable_reg = CCM_PCCR_GPIO_REG,
	.enable_shift = CCM_PCCR_GPIO_OFFSET,
	.disable = _clk_disable,
};

static struct clk i2c_clk = {
	.id = 0,
	.parent = &ipg_clk,
	.enable = _clk_enable,
	.enable_reg = CCM_PCCR_I2C1_REG,
	.enable_shift = CCM_PCCR_I2C1_OFFSET,
	.disable = _clk_disable,
};

static struct clk kpp_clk = {
	.parent = &ipg_clk,
	.enable = _clk_enable,
	.enable_reg = CCM_PCCR_KPP_REG,
	.enable_shift = CCM_PCCR_KPP_OFFSET,
	.disable = _clk_disable,
};

static struct clk owire_clk = {
	.parent = &ipg_clk,
	.enable = _clk_enable,
	.enable_reg = CCM_PCCR_OWIRE_REG,
	.enable_shift = CCM_PCCR_OWIRE_OFFSET,
	.disable = _clk_disable,
};

static struct clk rtc_clk = {
	.parent = &ipg_clk,
	.enable = _clk_enable,
	.enable_reg = CCM_PCCR_RTC_REG,
	.enable_shift = CCM_PCCR_RTC_OFFSET,
	.disable = _clk_disable,
};

static unsigned long _clk_clko_round_rate(struct clk *clk, unsigned long rate)
{
	return _clk_generic_round_rate(clk, rate, 8);
}

static int _clk_clko_set_rate(struct clk *clk, unsigned long rate)
{
	u32 reg;
	u32 div;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	div = parent_rate / rate;

	if (div > 8 || div < 1 || ((parent_rate / div) != rate))
		return -EINVAL;
	div--;

	reg = __raw_readl(CCM_PCDR0);

	if (clk->parent == &usb_clk[0]) {
		reg &= ~CCM_PCDR0_48MDIV_MASK;
		reg |= div << CCM_PCDR0_48MDIV_OFFSET;
	}
	__raw_writel(reg, CCM_PCDR0);

	return 0;
}

static unsigned long _clk_clko_recalc(struct clk *clk)
{
	u32 div = 0;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	if (clk->parent == &usb_clk[0]) /* 48M */
		div = __raw_readl(CCM_PCDR0) & CCM_PCDR0_48MDIV_MASK
			 >> CCM_PCDR0_48MDIV_OFFSET;
	div++;

	return parent_rate / div;
}

static struct clk clko_clk;

static int _clk_clko_set_parent(struct clk *clk, struct clk *parent)
{
	u32 reg;

	reg = __raw_readl(CCM_CCSR) & ~CCM_CCSR_CLKOSEL_MASK;

	if (parent == &ckil_clk)
		reg |= 0 << CCM_CCSR_CLKOSEL_OFFSET;
	else if (parent == &fpm_clk)
		reg |= 1 << CCM_CCSR_CLKOSEL_OFFSET;
	else if (parent == &ckih_clk)
		reg |= 2 << CCM_CCSR_CLKOSEL_OFFSET;
	else if (parent == mpll_clk.parent)
		reg |= 3 << CCM_CCSR_CLKOSEL_OFFSET;
	else if (parent == spll_clk.parent)
		reg |= 4 << CCM_CCSR_CLKOSEL_OFFSET;
	else if (parent == &mpll_clk)
		reg |= 5 << CCM_CCSR_CLKOSEL_OFFSET;
	else if (parent == &spll_clk)
		reg |= 6 << CCM_CCSR_CLKOSEL_OFFSET;
	else if (parent == &fclk_clk)
		reg |= 7 << CCM_CCSR_CLKOSEL_OFFSET;
	else if (parent == &hclk_clk)
		reg |= 8 << CCM_CCSR_CLKOSEL_OFFSET;
	else if (parent == &ipg_clk)
		reg |= 9 << CCM_CCSR_CLKOSEL_OFFSET;
	else if (parent == &per_clk[0])
		reg |= 0xA << CCM_CCSR_CLKOSEL_OFFSET;
	else if (parent == &per_clk[1])
		reg |= 0xB << CCM_CCSR_CLKOSEL_OFFSET;
	else if (parent == &per_clk[2])
		reg |= 0xC << CCM_CCSR_CLKOSEL_OFFSET;
	else if (parent == &per_clk[3])
		reg |= 0xD << CCM_CCSR_CLKOSEL_OFFSET;
	else if (parent == &ssi_clk[0])
		reg |= 0xE << CCM_CCSR_CLKOSEL_OFFSET;
	else if (parent == &ssi_clk[1])
		reg |= 0xF << CCM_CCSR_CLKOSEL_OFFSET;
	else if (parent == &nfc_clk)
		reg |= 0x10 << CCM_CCSR_CLKOSEL_OFFSET;
	else if (parent == &usb_clk[0])
		reg |= 0x14 << CCM_CCSR_CLKOSEL_OFFSET;
	else if (parent == &clko_clk)
		reg |= 0x15 << CCM_CCSR_CLKOSEL_OFFSET;
	else
		return -EINVAL;

	__raw_writel(reg, CCM_CCSR);

	return 0;
}

static struct clk clko_clk = {
	.get_rate = _clk_clko_recalc,
	.set_rate = _clk_clko_set_rate,
	.round_rate = _clk_clko_round_rate,
	.set_parent = _clk_clko_set_parent,
};


#define _REGISTER_CLOCK(d, n, c) \
	{ \
		.dev_id = d, \
		.con_id = n, \
		.clk = &c, \
	},
static struct clk_lookup lookups[] = {
/* It's unlikely that any driver wants one of them directly:
	_REGISTER_CLOCK(NULL, "ckih", ckih_clk)
	_REGISTER_CLOCK(NULL, "ckil", ckil_clk)
	_REGISTER_CLOCK(NULL, "fpm", fpm_clk)
	_REGISTER_CLOCK(NULL, "mpll", mpll_clk)
	_REGISTER_CLOCK(NULL, "spll", spll_clk)
	_REGISTER_CLOCK(NULL, "fclk", fclk_clk)
	_REGISTER_CLOCK(NULL, "hclk", hclk_clk)
	_REGISTER_CLOCK(NULL, "ipg", ipg_clk)
*/
	_REGISTER_CLOCK(NULL, "perclk1", per_clk[0])
	_REGISTER_CLOCK(NULL, "perclk2", per_clk[1])
	_REGISTER_CLOCK(NULL, "perclk3", per_clk[2])
	_REGISTER_CLOCK(NULL, "perclk4", per_clk[3])
	_REGISTER_CLOCK(NULL, "clko", clko_clk)
	_REGISTER_CLOCK("imx-uart.0", NULL, uart_clk[0])
	_REGISTER_CLOCK("imx-uart.1", NULL, uart_clk[1])
	_REGISTER_CLOCK("imx-uart.2", NULL, uart_clk[2])
	_REGISTER_CLOCK("imx-uart.3", NULL, uart_clk[3])
	_REGISTER_CLOCK(NULL, "gpt1", gpt_clk[0])
	_REGISTER_CLOCK(NULL, "gpt1", gpt_clk[1])
	_REGISTER_CLOCK(NULL, "gpt1", gpt_clk[2])
	_REGISTER_CLOCK(NULL, "pwm", pwm_clk[0])
	_REGISTER_CLOCK(NULL, "sdhc1", sdhc_clk[0])
	_REGISTER_CLOCK(NULL, "sdhc2", sdhc_clk[1])
	_REGISTER_CLOCK(NULL, "cspi1", cspi_clk[0])
	_REGISTER_CLOCK(NULL, "cspi2", cspi_clk[1])
	_REGISTER_CLOCK(NULL, "cspi3", cspi_clk[2])
	_REGISTER_CLOCK("imx-fb.0", NULL, lcdc_clk[0])
	_REGISTER_CLOCK(NULL, "csi", csi_clk[0])
	_REGISTER_CLOCK("imx21-hcd.0", NULL, usb_clk[0])
	_REGISTER_CLOCK(NULL, "ssi1", ssi_clk[0])
	_REGISTER_CLOCK(NULL, "ssi2", ssi_clk[1])
	_REGISTER_CLOCK("mxc_nand.0", NULL, nfc_clk)
	_REGISTER_CLOCK(NULL, "dma", dma_clk[0])
	_REGISTER_CLOCK(NULL, "brom", brom_clk)
	_REGISTER_CLOCK(NULL, "emma", emma_clk[0])
	_REGISTER_CLOCK(NULL, "slcdc", slcdc_clk[0])
	_REGISTER_CLOCK("imx-wdt.0", NULL, wdog_clk)
	_REGISTER_CLOCK(NULL, "gpio", gpio_clk)
	_REGISTER_CLOCK("imx-i2c.0", NULL, i2c_clk)
	_REGISTER_CLOCK("mxc-keypad", NULL, kpp_clk)
	_REGISTER_CLOCK(NULL, "owire", owire_clk)
	_REGISTER_CLOCK(NULL, "rtc", rtc_clk)
};

/*
 * must be called very early to get information about the
 * available clock rate when the timer framework starts
 */
int __init mx21_clocks_init(unsigned long lref, unsigned long href)
{
	int i;
	u32 cscr;

	external_low_reference = lref;
	external_high_reference = href;

	/* detect clock reference for both system PLL */
	cscr = CSCR();
	if (cscr & CCM_CSCR_MCU)
		mpll_clk.parent = &ckih_clk;
	else
		mpll_clk.parent = &fpm_clk;

	if (cscr & CCM_CSCR_SP)
		spll_clk.parent = &ckih_clk;
	else
		spll_clk.parent = &fpm_clk;

	for (i = 0; i < ARRAY_SIZE(lookups); i++)
		clkdev_add(&lookups[i]);

	/* Turn off all clock gates */
	__raw_writel(0, CCM_PCCR0);
	__raw_writel(CCM_PCCR_GPT1_MASK, CCM_PCCR1);

	/* This turns of the serial PLL as well */
	spll_clk.disable(&spll_clk);

	/* This will propagate to all children and init all the clock rates. */
	clk_enable(&per_clk[0]);
	clk_enable(&gpio_clk);

#if defined(CONFIG_DEBUG_LL) && !defined(CONFIG_DEBUG_ICEDCC)
	clk_enable(&uart_clk[0]);
#endif

	mxc_timer_init(&gpt_clk[0], MX21_IO_ADDRESS(MX21_GPT1_BASE_ADDR),
			MX21_INT_GPT1);
	return 0;
}
