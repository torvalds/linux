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

#include <asm/clkdev.h>
#include <asm/div64.h>

#include <mach/clock.h>
#include <mach/common.h>
#include <mach/hardware.h>

#define IO_ADDR_CCM(off)	(MX27_IO_ADDRESS(MX27_CCM_BASE_ADDR + (off)))

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

#define CCM_CSCR_UPDATE_DIS	(1 << 31)
#define CCM_CSCR_SSI2		(1 << 23)
#define CCM_CSCR_SSI1		(1 << 22)
#define CCM_CSCR_VPU		(1 << 21)
#define CCM_CSCR_MSHC           (1 << 20)
#define CCM_CSCR_SPLLRES        (1 << 19)
#define CCM_CSCR_MPLLRES        (1 << 18)
#define CCM_CSCR_SP             (1 << 17)
#define CCM_CSCR_MCU            (1 << 16)
#define CCM_CSCR_OSC26MDIV      (1 << 4)
#define CCM_CSCR_OSC26M         (1 << 3)
#define CCM_CSCR_FPM            (1 << 2)
#define CCM_CSCR_SPEN           (1 << 1)
#define CCM_CSCR_MPEN           (1 << 0)

/* i.MX27 TO 2+ */
#define CCM_CSCR_ARM_SRC        (1 << 15)

#define CCM_SPCTL1_LF           (1 << 15)
#define CCM_SPCTL1_BRMO         (1 << 6)

static struct clk mpll_main1_clk, mpll_main2_clk;

static int clk_pccr_enable(struct clk *clk)
{
	unsigned long reg;

	if (!clk->enable_reg)
		return 0;

	reg = __raw_readl(clk->enable_reg);
	reg |= 1 << clk->enable_shift;
	__raw_writel(reg, clk->enable_reg);

	return 0;
}

static void clk_pccr_disable(struct clk *clk)
{
	unsigned long reg;

	if (!clk->enable_reg)
		return;

	reg = __raw_readl(clk->enable_reg);
	reg &= ~(1 << clk->enable_shift);
	__raw_writel(reg, clk->enable_reg);
}

static int clk_spll_enable(struct clk *clk)
{
	unsigned long reg;

	reg = __raw_readl(CCM_CSCR);
	reg |= CCM_CSCR_SPEN;
	__raw_writel(reg, CCM_CSCR);

	while (!(__raw_readl(CCM_SPCTL1) & CCM_SPCTL1_LF));

	return 0;
}

static void clk_spll_disable(struct clk *clk)
{
	unsigned long reg;

	reg = __raw_readl(CCM_CSCR);
	reg &= ~CCM_CSCR_SPEN;
	__raw_writel(reg, CCM_CSCR);
}

static int clk_cpu_set_parent(struct clk *clk, struct clk *parent)
{
	int cscr = __raw_readl(CCM_CSCR);

	if (clk->parent == parent)
		return 0;

	if (mx27_revision() >= IMX_CHIP_REVISION_2_0) {
		if (parent == &mpll_main1_clk) {
			cscr |= CCM_CSCR_ARM_SRC;
		} else {
			if (parent == &mpll_main2_clk)
				cscr &= ~CCM_CSCR_ARM_SRC;
			else
				return -EINVAL;
		}
		__raw_writel(cscr, CCM_CSCR);
		clk->parent = parent;
		return 0;
	}
	return -ENODEV;
}

static unsigned long round_rate_cpu(struct clk *clk, unsigned long rate)
{
	int div;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	div = parent_rate / rate;
	if (parent_rate % rate)
		div++;

	if (div > 4)
		div = 4;

	return parent_rate / div;
}

static int set_rate_cpu(struct clk *clk, unsigned long rate)
{
	unsigned int div;
	uint32_t reg;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	div = parent_rate / rate;

	if (div > 4 || div < 1 || ((parent_rate / div) != rate))
		return -EINVAL;

	div--;

	reg = __raw_readl(CCM_CSCR);
	if (mx27_revision() >= IMX_CHIP_REVISION_2_0) {
		reg &= ~(3 << 12);
		reg |= div << 12;
		reg &= ~(CCM_CSCR_FPM | CCM_CSCR_SPEN);
		__raw_writel(reg | CCM_CSCR_UPDATE_DIS, CCM_CSCR);
	} else {
		printk(KERN_ERR "Can't set CPU frequency!\n");
	}

	return 0;
}

static unsigned long round_rate_per(struct clk *clk, unsigned long rate)
{
	u32 div;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	div = parent_rate / rate;
	if (parent_rate % rate)
		div++;

	if (div > 64)
		div = 64;

	return parent_rate / div;
}

static int set_rate_per(struct clk *clk, unsigned long rate)
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

	reg = __raw_readl(CCM_PCDR1) & ~(0x3f << (clk->id << 3));
	reg |= div << (clk->id << 3);
	__raw_writel(reg, CCM_PCDR1);

	return 0;
}

static unsigned long get_rate_usb(struct clk *clk)
{
	unsigned long usb_pdf;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	usb_pdf = (__raw_readl(CCM_CSCR) >> 28) & 0x7;

	return parent_rate / (usb_pdf + 1U);
}

static unsigned long get_rate_ssix(struct clk *clk, unsigned long pdf)
{
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	if (mx27_revision() >= IMX_CHIP_REVISION_2_0)
		pdf += 4;  /* MX27 TO2+ */
	else
		pdf = (pdf < 2) ? 124UL : pdf;  /* MX21 & MX27 TO1 */

	return 2UL * parent_rate / pdf;
}

static unsigned long get_rate_ssi1(struct clk *clk)
{
	return get_rate_ssix(clk, (__raw_readl(CCM_PCDR0) >> 16) & 0x3f);
}

static unsigned long get_rate_ssi2(struct clk *clk)
{
	return get_rate_ssix(clk, (__raw_readl(CCM_PCDR0) >> 26) & 0x3f);
}

static unsigned long get_rate_nfc(struct clk *clk)
{
	unsigned long nfc_pdf;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	if (mx27_revision() >= IMX_CHIP_REVISION_2_0)
		nfc_pdf = (__raw_readl(CCM_PCDR0) >> 6) & 0xf;
	else
		nfc_pdf = (__raw_readl(CCM_PCDR0) >> 12) & 0xf;

	return parent_rate / (nfc_pdf + 1);
}

static unsigned long get_rate_vpu(struct clk *clk)
{
	unsigned long vpu_pdf;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	if (mx27_revision() >= IMX_CHIP_REVISION_2_0) {
		vpu_pdf = (__raw_readl(CCM_PCDR0) >> 10) & 0x3f;
		vpu_pdf += 4;
	} else {
		vpu_pdf = (__raw_readl(CCM_PCDR0) >> 8) & 0xf;
		vpu_pdf = (vpu_pdf < 2) ? 124 : vpu_pdf;
	}

	return 2UL * parent_rate / vpu_pdf;
}

static unsigned long round_rate_parent(struct clk *clk, unsigned long rate)
{
	return clk->parent->round_rate(clk->parent, rate);
}

static unsigned long get_rate_parent(struct clk *clk)
{
	return clk_get_rate(clk->parent);
}

static int set_rate_parent(struct clk *clk, unsigned long rate)
{
	return clk->parent->set_rate(clk->parent, rate);
}

/* in Hz */
static unsigned long external_high_reference = 26000000;

static unsigned long get_rate_high_reference(struct clk *clk)
{
	return external_high_reference;
}

/* in Hz */
static unsigned long external_low_reference = 32768;

static unsigned long get_rate_low_reference(struct clk *clk)
{
	return external_low_reference;
}

static unsigned long get_rate_fpm(struct clk *clk)
{
	return clk_get_rate(clk->parent) * 1024;
}

static unsigned long get_rate_mpll(struct clk *clk)
{
	return mxc_decode_pll(__raw_readl(CCM_MPCTL0),
			clk_get_rate(clk->parent));
}

static unsigned long get_rate_mpll_main(struct clk *clk)
{
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	/* i.MX27 TO2:
	 * clk->id == 0: arm clock source path 1 which is from 2 * MPLL / 2
	 * clk->id == 1: arm clock source path 2 which is from 2 * MPLL / 3
	 */
	if (mx27_revision() >= IMX_CHIP_REVISION_2_0 && clk->id == 1)
		return 2UL * parent_rate / 3UL;

	return parent_rate;
}

static unsigned long get_rate_spll(struct clk *clk)
{
	uint32_t reg;
	unsigned long rate;

	rate = clk_get_rate(clk->parent);

	reg = __raw_readl(CCM_SPCTL0);

	/* On TO2 we have to write the value back. Otherwise we
	 * read 0 from this register the next time.
	 */
	if (mx27_revision() >= IMX_CHIP_REVISION_2_0)
		__raw_writel(reg, CCM_SPCTL0);

	return mxc_decode_pll(reg, rate);
}

static unsigned long get_rate_cpu(struct clk *clk)
{
	u32 div;
	unsigned long rate;

	if (mx27_revision() >= IMX_CHIP_REVISION_2_0)
		div = (__raw_readl(CCM_CSCR) >> 12) & 0x3;
	else
		div = (__raw_readl(CCM_CSCR) >> 13) & 0x7;

	rate = clk_get_rate(clk->parent);
	return rate / (div + 1);
}

static unsigned long get_rate_ahb(struct clk *clk)
{
	unsigned long rate, bclk_pdf;

	if (mx27_revision() >= IMX_CHIP_REVISION_2_0)
		bclk_pdf = (__raw_readl(CCM_CSCR) >> 8) & 0x3;
	else
		bclk_pdf = (__raw_readl(CCM_CSCR) >> 9) & 0xf;

	rate = clk_get_rate(clk->parent);
	return rate / (bclk_pdf + 1);
}

static unsigned long get_rate_ipg(struct clk *clk)
{
	unsigned long rate, ipg_pdf;

	if (mx27_revision() >= IMX_CHIP_REVISION_2_0)
		return clk_get_rate(clk->parent);
	else
		ipg_pdf = (__raw_readl(CCM_CSCR) >> 8) & 1;

	rate = clk_get_rate(clk->parent);
	return rate / (ipg_pdf + 1);
}

static unsigned long get_rate_per(struct clk *clk)
{
	unsigned long perclk_pdf, parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	if (clk->id < 0 || clk->id > 3)
		return 0;

	perclk_pdf = (__raw_readl(CCM_PCDR1) >> (clk->id << 3)) & 0x3f;

	return parent_rate / (perclk_pdf + 1);
}

/*
 * the high frequency external clock reference
 * Default case is 26MHz. Could be changed at runtime
 * with a call to change_external_high_reference()
 */
static struct clk ckih_clk = {
	.get_rate	= get_rate_high_reference,
};

static struct clk mpll_clk = {
	.parent		= &ckih_clk,
	.get_rate	= get_rate_mpll,
};

/* For i.MX27 TO2, it is the MPLL path 1 of ARM core
 * It provides the clock source whose rate is same as MPLL
 */
static struct clk mpll_main1_clk = {
	.id		= 0,
	.parent		= &mpll_clk,
	.get_rate	= get_rate_mpll_main,
};

/* For i.MX27 TO2, it is the MPLL path 2 of ARM core
 * It provides the clock source whose rate is same MPLL * 2 / 3
 */
static struct clk mpll_main2_clk = {
	.id		= 1,
	.parent		= &mpll_clk,
	.get_rate	= get_rate_mpll_main,
};

static struct clk ahb_clk = {
	.parent		= &mpll_main2_clk,
	.get_rate	= get_rate_ahb,
};

static struct clk ipg_clk = {
	.parent		= &ahb_clk,
	.get_rate	= get_rate_ipg,
};

static struct clk cpu_clk = {
	.parent = &mpll_main2_clk,
	.set_parent = clk_cpu_set_parent,
	.round_rate = round_rate_cpu,
	.get_rate = get_rate_cpu,
	.set_rate = set_rate_cpu,
};

static struct clk spll_clk = {
	.parent = &ckih_clk,
	.get_rate = get_rate_spll,
	.enable = clk_spll_enable,
	.disable = clk_spll_disable,
};

/*
 * the low frequency external clock reference
 * Default case is 32.768kHz.
 */
static struct clk ckil_clk = {
	.get_rate = get_rate_low_reference,
};

/* Output of frequency pre multiplier */
static struct clk fpm_clk = {
	.parent = &ckil_clk,
	.get_rate = get_rate_fpm,
};

#define PCCR0 CCM_PCCR0
#define PCCR1 CCM_PCCR1

#define DEFINE_CLOCK(name, i, er, es, gr, s, p)		\
	static struct clk name = {			\
		.id		= i,			\
		.enable_reg	= er,			\
		.enable_shift	= es,			\
		.get_rate	= gr,			\
		.enable		= clk_pccr_enable,	\
		.disable	= clk_pccr_disable,	\
		.secondary	= s,			\
		.parent		= p,			\
	}

#define DEFINE_CLOCK1(name, i, er, es, getsetround, s, p)	\
	static struct clk name = {				\
		.id		= i,				\
		.enable_reg	= er,				\
		.enable_shift	= es,				\
		.get_rate	= get_rate_##getsetround,	\
		.set_rate	= set_rate_##getsetround,	\
		.round_rate	= round_rate_##getsetround,	\
		.enable		= clk_pccr_enable,		\
		.disable	= clk_pccr_disable,		\
		.secondary	= s,				\
		.parent		= p,				\
	}

/* Forward declaration to keep the following list in order */
static struct clk slcdc_clk1, sahara2_clk1, rtic_clk1, fec_clk1, emma_clk1,
		  dma_clk1, lcdc_clk2, vpu_clk1;

/* All clocks we can gate through PCCRx in the order of PCCRx bits */
DEFINE_CLOCK(ssi2_clk1,    1, PCCR0,  0, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(ssi1_clk1,    0, PCCR0,  1, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(slcdc_clk,    0, PCCR0,  2, NULL, &slcdc_clk1, &ahb_clk);
DEFINE_CLOCK(sdhc3_clk1,   0, PCCR0,  3, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(sdhc2_clk1,   0, PCCR0,  4, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(sdhc1_clk1,   0, PCCR0,  5, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(scc_clk,      0, PCCR0,  6, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(sahara2_clk,  0, PCCR0,  7, NULL, &sahara2_clk1, &ahb_clk);
DEFINE_CLOCK(rtic_clk,     0, PCCR0,  8, NULL, &rtic_clk1, &ahb_clk);
DEFINE_CLOCK(rtc_clk,      0, PCCR0,  9, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(pwm_clk1,     0, PCCR0, 11, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(owire_clk,    0, PCCR0, 12, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(mstick_clk1,  0, PCCR0, 13, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(lcdc_clk1,    0, PCCR0, 14, NULL, &lcdc_clk2, &ipg_clk);
DEFINE_CLOCK(kpp_clk,      0, PCCR0, 15, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(iim_clk,      0, PCCR0, 16, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(i2c2_clk,     1, PCCR0, 17, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(i2c1_clk,     0, PCCR0, 18, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(gpt6_clk1,    0, PCCR0, 29, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(gpt5_clk1,    0, PCCR0, 20, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(gpt4_clk1,    0, PCCR0, 21, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(gpt3_clk1,    0, PCCR0, 22, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(gpt2_clk1,    0, PCCR0, 23, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(gpt1_clk1,    0, PCCR0, 24, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(gpio_clk,     0, PCCR0, 25, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(fec_clk,      0, PCCR0, 26, NULL, &fec_clk1, &ahb_clk);
DEFINE_CLOCK(emma_clk,     0, PCCR0, 27, NULL, &emma_clk1, &ahb_clk);
DEFINE_CLOCK(dma_clk,      0, PCCR0, 28, NULL, &dma_clk1, &ahb_clk);
DEFINE_CLOCK(cspi13_clk1,  0, PCCR0, 29, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(cspi2_clk1,   0, PCCR0, 30, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(cspi1_clk1,   0, PCCR0, 31, NULL, NULL, &ipg_clk);

DEFINE_CLOCK(mstick_clk,   0, PCCR1,  2, NULL, &mstick_clk1, &ipg_clk);
DEFINE_CLOCK(nfc_clk,      0, PCCR1,  3, get_rate_nfc, NULL, &cpu_clk);
DEFINE_CLOCK(ssi2_clk,     1, PCCR1,  4, get_rate_ssi2, &ssi2_clk1, &mpll_main2_clk);
DEFINE_CLOCK(ssi1_clk,     0, PCCR1,  5, get_rate_ssi1, &ssi1_clk1, &mpll_main2_clk);
DEFINE_CLOCK(vpu_clk,      0, PCCR1,  6, get_rate_vpu, &vpu_clk1, &mpll_main2_clk);
DEFINE_CLOCK1(per4_clk,    3, PCCR1,  7, per, NULL, &mpll_main2_clk);
DEFINE_CLOCK1(per3_clk,    2, PCCR1,  8, per, NULL, &mpll_main2_clk);
DEFINE_CLOCK1(per2_clk,    1, PCCR1,  9, per, NULL, &mpll_main2_clk);
DEFINE_CLOCK1(per1_clk,    0, PCCR1, 10, per, NULL, &mpll_main2_clk);
DEFINE_CLOCK(usb_clk1,     0, PCCR1, 11, NULL, NULL, &ahb_clk);
DEFINE_CLOCK(slcdc_clk1,   0, PCCR1, 12, NULL, NULL, &ahb_clk);
DEFINE_CLOCK(sahara2_clk1, 0, PCCR1, 13, NULL, NULL, &ahb_clk);
DEFINE_CLOCK(rtic_clk1,    0, PCCR1, 14, NULL, NULL, &ahb_clk);
DEFINE_CLOCK(lcdc_clk2,    0, PCCR1, 15, NULL, NULL, &ahb_clk);
DEFINE_CLOCK(vpu_clk1,     0, PCCR1, 16, NULL, NULL, &ahb_clk);
DEFINE_CLOCK(fec_clk1,     0, PCCR1, 17, NULL, NULL, &ahb_clk);
DEFINE_CLOCK(emma_clk1,    0, PCCR1, 18, NULL, NULL, &ahb_clk);
DEFINE_CLOCK(emi_clk,      0, PCCR1, 19, NULL, NULL, &ahb_clk);
DEFINE_CLOCK(dma_clk1,     0, PCCR1, 20, NULL, NULL, &ahb_clk);
DEFINE_CLOCK(csi_clk1,     0, PCCR1, 21, NULL, NULL, &ahb_clk);
DEFINE_CLOCK(brom_clk,     0, PCCR1, 22, NULL, NULL, &ahb_clk);
DEFINE_CLOCK(ata_clk,      0, PCCR1, 23, NULL, NULL, &ahb_clk);
DEFINE_CLOCK(wdog_clk,     0, PCCR1, 24, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(usb_clk,      0, PCCR1, 25, get_rate_usb, &usb_clk1, &spll_clk);
DEFINE_CLOCK(uart6_clk1,   0, PCCR1, 26, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(uart5_clk1,   0, PCCR1, 27, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(uart4_clk1,   0, PCCR1, 28, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(uart3_clk1,   0, PCCR1, 29, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(uart2_clk1,   0, PCCR1, 30, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(uart1_clk1,   0, PCCR1, 31, NULL, NULL, &ipg_clk);

/* Clocks we cannot directly gate, but drivers need their rates */
DEFINE_CLOCK(cspi1_clk,    0, NULL,   0, NULL, &cspi1_clk1, &per2_clk);
DEFINE_CLOCK(cspi2_clk,    1, NULL,   0, NULL, &cspi2_clk1, &per2_clk);
DEFINE_CLOCK(cspi3_clk,    2, NULL,   0, NULL, &cspi13_clk1, &per2_clk);
DEFINE_CLOCK(sdhc1_clk,    0, NULL,   0, NULL, &sdhc1_clk1, &per2_clk);
DEFINE_CLOCK(sdhc2_clk,    1, NULL,   0, NULL, &sdhc2_clk1, &per2_clk);
DEFINE_CLOCK(sdhc3_clk,    2, NULL,   0, NULL, &sdhc3_clk1, &per2_clk);
DEFINE_CLOCK(pwm_clk,      0, NULL,   0, NULL, &pwm_clk1, &per1_clk);
DEFINE_CLOCK(gpt1_clk,     0, NULL,   0, NULL, &gpt1_clk1, &per1_clk);
DEFINE_CLOCK(gpt2_clk,     1, NULL,   0, NULL, &gpt2_clk1, &per1_clk);
DEFINE_CLOCK(gpt3_clk,     2, NULL,   0, NULL, &gpt3_clk1, &per1_clk);
DEFINE_CLOCK(gpt4_clk,     3, NULL,   0, NULL, &gpt4_clk1, &per1_clk);
DEFINE_CLOCK(gpt5_clk,     4, NULL,   0, NULL, &gpt5_clk1, &per1_clk);
DEFINE_CLOCK(gpt6_clk,     5, NULL,   0, NULL, &gpt6_clk1, &per1_clk);
DEFINE_CLOCK(uart1_clk,    0, NULL,   0, NULL, &uart1_clk1, &per1_clk);
DEFINE_CLOCK(uart2_clk,    1, NULL,   0, NULL, &uart2_clk1, &per1_clk);
DEFINE_CLOCK(uart3_clk,    2, NULL,   0, NULL, &uart3_clk1, &per1_clk);
DEFINE_CLOCK(uart4_clk,    3, NULL,   0, NULL, &uart4_clk1, &per1_clk);
DEFINE_CLOCK(uart5_clk,    4, NULL,   0, NULL, &uart5_clk1, &per1_clk);
DEFINE_CLOCK(uart6_clk,    5, NULL,   0, NULL, &uart6_clk1, &per1_clk);
DEFINE_CLOCK1(lcdc_clk,    0, NULL,   0, parent, &lcdc_clk1, &per3_clk);
DEFINE_CLOCK1(csi_clk,     0, NULL,   0, parent, &csi_clk1, &per4_clk);

#define _REGISTER_CLOCK(d, n, c) \
	{ \
		.dev_id = d, \
		.con_id = n, \
		.clk = &c, \
	},

static struct clk_lookup lookups[] = {
	_REGISTER_CLOCK("imx-uart.0", NULL, uart1_clk)
	_REGISTER_CLOCK("imx-uart.1", NULL, uart2_clk)
	_REGISTER_CLOCK("imx-uart.2", NULL, uart3_clk)
	_REGISTER_CLOCK("imx-uart.3", NULL, uart4_clk)
	_REGISTER_CLOCK("imx-uart.4", NULL, uart5_clk)
	_REGISTER_CLOCK("imx-uart.5", NULL, uart6_clk)
	_REGISTER_CLOCK(NULL, "gpt1", gpt1_clk)
	_REGISTER_CLOCK(NULL, "gpt2", gpt2_clk)
	_REGISTER_CLOCK(NULL, "gpt3", gpt3_clk)
	_REGISTER_CLOCK(NULL, "gpt4", gpt4_clk)
	_REGISTER_CLOCK(NULL, "gpt5", gpt5_clk)
	_REGISTER_CLOCK(NULL, "gpt6", gpt6_clk)
	_REGISTER_CLOCK("mxc_pwm.0", NULL, pwm_clk)
	_REGISTER_CLOCK("mxc-mmc.0", NULL, sdhc1_clk)
	_REGISTER_CLOCK("mxc-mmc.1", NULL, sdhc2_clk)
	_REGISTER_CLOCK("mxc-mmc.2", NULL, sdhc3_clk)
	_REGISTER_CLOCK("imx27-cspi.0", NULL, cspi1_clk)
	_REGISTER_CLOCK("imx27-cspi.1", NULL, cspi2_clk)
	_REGISTER_CLOCK("imx27-cspi.2", NULL, cspi3_clk)
	_REGISTER_CLOCK("imx-fb.0", NULL, lcdc_clk)
	_REGISTER_CLOCK("mx2-camera.0", NULL, csi_clk)
	_REGISTER_CLOCK("fsl-usb2-udc", "usb", usb_clk)
	_REGISTER_CLOCK("fsl-usb2-udc", "usb_ahb", usb_clk1)
	_REGISTER_CLOCK("mxc-ehci.0", "usb", usb_clk)
	_REGISTER_CLOCK("mxc-ehci.0", "usb_ahb", usb_clk1)
	_REGISTER_CLOCK("mxc-ehci.1", "usb", usb_clk)
	_REGISTER_CLOCK("mxc-ehci.1", "usb_ahb", usb_clk1)
	_REGISTER_CLOCK("mxc-ehci.2", "usb", usb_clk)
	_REGISTER_CLOCK("mxc-ehci.2", "usb_ahb", usb_clk1)
	_REGISTER_CLOCK("imx-ssi.0", NULL, ssi1_clk)
	_REGISTER_CLOCK("imx-ssi.1", NULL, ssi2_clk)
	_REGISTER_CLOCK("mxc_nand.0", NULL, nfc_clk)
	_REGISTER_CLOCK(NULL, "vpu", vpu_clk)
	_REGISTER_CLOCK(NULL, "dma", dma_clk)
	_REGISTER_CLOCK(NULL, "rtic", rtic_clk)
	_REGISTER_CLOCK(NULL, "brom", brom_clk)
	_REGISTER_CLOCK(NULL, "emma", emma_clk)
	_REGISTER_CLOCK(NULL, "slcdc", slcdc_clk)
	_REGISTER_CLOCK("fec.0", NULL, fec_clk)
	_REGISTER_CLOCK(NULL, "emi", emi_clk)
	_REGISTER_CLOCK(NULL, "sahara2", sahara2_clk)
	_REGISTER_CLOCK(NULL, "ata", ata_clk)
	_REGISTER_CLOCK(NULL, "mstick", mstick_clk)
	_REGISTER_CLOCK("imx-wdt.0", NULL, wdog_clk)
	_REGISTER_CLOCK(NULL, "gpio", gpio_clk)
	_REGISTER_CLOCK("imx-i2c.0", NULL, i2c1_clk)
	_REGISTER_CLOCK("imx-i2c.1", NULL, i2c2_clk)
	_REGISTER_CLOCK(NULL, "iim", iim_clk)
	_REGISTER_CLOCK(NULL, "kpp", kpp_clk)
	_REGISTER_CLOCK("mxc_w1.0", NULL, owire_clk)
	_REGISTER_CLOCK(NULL, "rtc", rtc_clk)
	_REGISTER_CLOCK(NULL, "scc", scc_clk)
};

/* Adjust the clock path for TO2 and later */
static void __init to2_adjust_clocks(void)
{
	unsigned long cscr = __raw_readl(CCM_CSCR);

	if (mx27_revision() >= IMX_CHIP_REVISION_2_0) {
		if (cscr & CCM_CSCR_ARM_SRC)
			cpu_clk.parent = &mpll_main1_clk;

		if (!(cscr & CCM_CSCR_SSI2))
			ssi1_clk.parent = &spll_clk;

		if (!(cscr & CCM_CSCR_SSI1))
			ssi1_clk.parent = &spll_clk;

		if (!(cscr & CCM_CSCR_VPU))
			vpu_clk.parent = &spll_clk;
	} else {
		cpu_clk.parent = &mpll_clk;
		cpu_clk.set_parent = NULL;
		cpu_clk.round_rate = NULL;
		cpu_clk.set_rate = NULL;
		ahb_clk.parent = &mpll_clk;

		per1_clk.parent = &mpll_clk;
		per2_clk.parent = &mpll_clk;
		per3_clk.parent = &mpll_clk;
		per4_clk.parent = &mpll_clk;

		ssi1_clk.parent = &mpll_clk;
		ssi2_clk.parent = &mpll_clk;

		vpu_clk.parent = &mpll_clk;
	}
}

/*
 * must be called very early to get information about the
 * available clock rate when the timer framework starts
 */
int __init mx27_clocks_init(unsigned long fref)
{
	u32 cscr = __raw_readl(CCM_CSCR);

	external_high_reference = fref;

	/* detect clock reference for both system PLLs */
	if (cscr & CCM_CSCR_MCU)
		mpll_clk.parent = &ckih_clk;
	else
		mpll_clk.parent = &fpm_clk;

	if (cscr & CCM_CSCR_SP)
		spll_clk.parent = &ckih_clk;
	else
		spll_clk.parent = &fpm_clk;

	to2_adjust_clocks();

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	/* Turn off all clocks we do not need */
	__raw_writel(0, CCM_PCCR0);
	__raw_writel((1 << 10) | (1 << 19), CCM_PCCR1);

	spll_clk.disable(&spll_clk);

	/* enable basic clocks */
	clk_enable(&per1_clk);
	clk_enable(&gpio_clk);
	clk_enable(&emi_clk);
	clk_enable(&iim_clk);

#if defined(CONFIG_DEBUG_LL) && !defined(CONFIG_DEBUG_ICEDCC)
	clk_enable(&uart1_clk);
#endif

	mxc_timer_init(&gpt1_clk, MX27_IO_ADDRESS(MX27_GPT1_BASE_ADDR),
			MX27_INT_GPT1);

	return 0;
}

