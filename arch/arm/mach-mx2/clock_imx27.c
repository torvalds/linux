/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
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
#include <linux/spinlock.h>

#include <mach/clock.h>
#include <mach/common.h>
#include <asm/div64.h>

#include "crm_regs.h"

static struct clk ckil_clk;
static struct clk mpll_clk;
static struct clk mpll_main_clk[];
static struct clk spll_clk;

static int _clk_enable(struct clk *clk)
{
	unsigned long reg;

	reg = __raw_readl(clk->enable_reg);
	reg |= 1 << clk->enable_shift;
	__raw_writel(reg, clk->enable_reg);

	return 0;
}

static void _clk_disable(struct clk *clk)
{
	unsigned long reg;

	reg = __raw_readl(clk->enable_reg);
	reg &= ~(1 << clk->enable_shift);
	__raw_writel(reg, clk->enable_reg);
}

static int _clk_spll_enable(struct clk *clk)
{
	unsigned long reg;

	reg = __raw_readl(CCM_CSCR);
	reg |= CCM_CSCR_SPEN;
	__raw_writel(reg, CCM_CSCR);

	while ((__raw_readl(CCM_SPCTL1) & CCM_SPCTL1_LF) == 0)
		;

	return 0;
}

static void _clk_spll_disable(struct clk *clk)
{
	unsigned long reg;

	reg = __raw_readl(CCM_CSCR);
	reg &= ~CCM_CSCR_SPEN;
	__raw_writel(reg, CCM_CSCR);
}

static void _clk_pccr01_enable(unsigned long mask0, unsigned long mask1)
{
	unsigned long reg;

	reg = __raw_readl(CCM_PCCR0);
	reg |= mask0;
	__raw_writel(reg, CCM_PCCR0);

	reg = __raw_readl(CCM_PCCR1);
	reg |= mask1;
	__raw_writel(reg, CCM_PCCR1);

}

static void _clk_pccr01_disable(unsigned long mask0, unsigned long mask1)
{
	unsigned long reg;

	reg = __raw_readl(CCM_PCCR0);
	reg &= ~mask0;
	__raw_writel(reg, CCM_PCCR0);

	reg = __raw_readl(CCM_PCCR1);
	reg &= ~mask1;
	__raw_writel(reg, CCM_PCCR1);
}

static void _clk_pccr10_enable(unsigned long mask1, unsigned long mask0)
{
	unsigned long reg;

	reg = __raw_readl(CCM_PCCR1);
	reg |= mask1;
	__raw_writel(reg, CCM_PCCR1);

	reg = __raw_readl(CCM_PCCR0);
	reg |= mask0;
	__raw_writel(reg, CCM_PCCR0);
}

static void _clk_pccr10_disable(unsigned long mask1, unsigned long mask0)
{
	unsigned long reg;

	reg = __raw_readl(CCM_PCCR1);
	reg &= ~mask1;
	__raw_writel(reg, CCM_PCCR1);

	reg = __raw_readl(CCM_PCCR0);
	reg &= ~mask0;
	__raw_writel(reg, CCM_PCCR0);
}

static int _clk_dma_enable(struct clk *clk)
{
	_clk_pccr01_enable(CCM_PCCR0_DMA_MASK, CCM_PCCR1_HCLK_DMA_MASK);

	return 0;
}

static void _clk_dma_disable(struct clk *clk)
{
	_clk_pccr01_disable(CCM_PCCR0_DMA_MASK, CCM_PCCR1_HCLK_DMA_MASK);
}

static int _clk_rtic_enable(struct clk *clk)
{
	_clk_pccr01_enable(CCM_PCCR0_RTIC_MASK, CCM_PCCR1_HCLK_RTIC_MASK);

	return 0;
}

static void _clk_rtic_disable(struct clk *clk)
{
	_clk_pccr01_disable(CCM_PCCR0_RTIC_MASK, CCM_PCCR1_HCLK_RTIC_MASK);
}

static int _clk_emma_enable(struct clk *clk)
{
	_clk_pccr01_enable(CCM_PCCR0_EMMA_MASK, CCM_PCCR1_HCLK_EMMA_MASK);

	return 0;
}

static void _clk_emma_disable(struct clk *clk)
{
	_clk_pccr01_disable(CCM_PCCR0_EMMA_MASK, CCM_PCCR1_HCLK_EMMA_MASK);
}

static int _clk_slcdc_enable(struct clk *clk)
{
	_clk_pccr01_enable(CCM_PCCR0_SLCDC_MASK, CCM_PCCR1_HCLK_SLCDC_MASK);

	return 0;
}

static void _clk_slcdc_disable(struct clk *clk)
{
	_clk_pccr01_disable(CCM_PCCR0_SLCDC_MASK, CCM_PCCR1_HCLK_SLCDC_MASK);
}

static int _clk_fec_enable(struct clk *clk)
{
	_clk_pccr01_enable(CCM_PCCR0_FEC_MASK, CCM_PCCR1_HCLK_FEC_MASK);

	return 0;
}

static void _clk_fec_disable(struct clk *clk)
{
	_clk_pccr01_disable(CCM_PCCR0_FEC_MASK, CCM_PCCR1_HCLK_FEC_MASK);
}

static int _clk_vpu_enable(struct clk *clk)
{
	unsigned long reg;

	reg = __raw_readl(CCM_PCCR1);
	reg |= CCM_PCCR1_VPU_BAUD_MASK | CCM_PCCR1_HCLK_VPU_MASK;
	__raw_writel(reg, CCM_PCCR1);

	return 0;
}

static void _clk_vpu_disable(struct clk *clk)
{
	unsigned long reg;

	reg = __raw_readl(CCM_PCCR1);
	reg &= ~(CCM_PCCR1_VPU_BAUD_MASK | CCM_PCCR1_HCLK_VPU_MASK);
	__raw_writel(reg, CCM_PCCR1);
}

static int _clk_sahara2_enable(struct clk *clk)
{
	_clk_pccr01_enable(CCM_PCCR0_SAHARA_MASK, CCM_PCCR1_HCLK_SAHARA_MASK);

	return 0;
}

static void _clk_sahara2_disable(struct clk *clk)
{
	_clk_pccr01_disable(CCM_PCCR0_SAHARA_MASK, CCM_PCCR1_HCLK_SAHARA_MASK);
}

static int _clk_mstick1_enable(struct clk *clk)
{
	_clk_pccr10_enable(CCM_PCCR1_MSHC_BAUD_MASK, CCM_PCCR0_MSHC_MASK);

	return 0;
}

static void _clk_mstick1_disable(struct clk *clk)
{
	_clk_pccr10_disable(CCM_PCCR1_MSHC_BAUD_MASK, CCM_PCCR0_MSHC_MASK);
}

#define CSCR() (__raw_readl(CCM_CSCR))
#define PCDR0() (__raw_readl(CCM_PCDR0))
#define PCDR1() (__raw_readl(CCM_PCDR1))

static int _clk_cpu_set_parent(struct clk *clk, struct clk *parent)
{
	int cscr = CSCR();

	if (clk->parent == parent)
		return 0;

	if (mx27_revision() >= CHIP_REV_2_0) {
		if (parent == &mpll_main_clk[0]) {
			cscr |= CCM_CSCR_ARM_SRC;
		} else {
			if (parent == &mpll_main_clk[1])
				cscr &= ~CCM_CSCR_ARM_SRC;
			else
				return -EINVAL;
		}
		__raw_writel(cscr, CCM_CSCR);
	} else
		return -ENODEV;

	clk->parent = parent;
	return 0;
}

static unsigned long _clk_cpu_round_rate(struct clk *clk, unsigned long rate)
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

static int _clk_cpu_set_rate(struct clk *clk, unsigned long rate)
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
	if (mx27_revision() >= CHIP_REV_2_0) {
		reg &= ~CCM_CSCR_ARM_MASK;
		reg |= div << CCM_CSCR_ARM_OFFSET;
		reg &= ~0x06;
		__raw_writel(reg | 0x80000000, CCM_CSCR);
	} else {
		printk(KERN_ERR "Cant set CPU frequency!\n");
	}

	return 0;
}

static unsigned long _clk_perclkx_round_rate(struct clk *clk,
					     unsigned long rate)
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

static unsigned long _clk_ssi1_recalc(struct clk *clk)
{
	unsigned long ssi1_pdf;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	ssi1_pdf = (PCDR0() & CCM_PCDR0_SSI1BAUDDIV_MASK) >>
					CCM_PCDR0_SSI1BAUDDIV_OFFSET;

	if (mx27_revision() >= CHIP_REV_2_0)
		ssi1_pdf += 4;
	else
		ssi1_pdf = (ssi1_pdf < 2) ? 124UL : ssi1_pdf;

	return 2UL * parent_rate / ssi1_pdf;
}

static unsigned long _clk_ssi2_recalc(struct clk *clk)
{
	unsigned long ssi2_pdf;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	ssi2_pdf = (PCDR0() & CCM_PCDR0_SSI2BAUDDIV_MASK) >>
	    CCM_PCDR0_SSI2BAUDDIV_OFFSET;

	if (mx27_revision() >= CHIP_REV_2_0)
		ssi2_pdf += 4;
	else
		ssi2_pdf = (ssi2_pdf < 2) ? 124UL : ssi2_pdf;

	return 2UL * parent_rate / ssi2_pdf;
}

static unsigned long _clk_nfc_recalc(struct clk *clk)
{
	unsigned long nfc_pdf;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	if (mx27_revision() >= CHIP_REV_2_0) {
		nfc_pdf =
		    (PCDR0() & CCM_PCDR0_NFCDIV2_MASK) >>
		    CCM_PCDR0_NFCDIV2_OFFSET;
	} else {
		nfc_pdf =
		    (PCDR0() & CCM_PCDR0_NFCDIV_MASK) >>
		    CCM_PCDR0_NFCDIV_OFFSET;
	}

	return parent_rate / (nfc_pdf + 1);
}

static unsigned long _clk_vpu_recalc(struct clk *clk)
{
	unsigned long vpu_pdf;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	if (mx27_revision() >= CHIP_REV_2_0) {
		vpu_pdf =
		    (PCDR0() & CCM_PCDR0_VPUDIV2_MASK) >>
		    CCM_PCDR0_VPUDIV2_OFFSET;
		vpu_pdf += 4;
	} else {
		vpu_pdf =
		    (PCDR0() & CCM_PCDR0_VPUDIV_MASK) >>
		    CCM_PCDR0_VPUDIV_OFFSET;
		vpu_pdf = (vpu_pdf < 2) ? 124 : vpu_pdf;
	}
	return 2UL * parent_rate / vpu_pdf;
}

static unsigned long _clk_parent_round_rate(struct clk *clk, unsigned long rate)
{
	return clk->parent->round_rate(clk->parent, rate);
}

static int _clk_parent_set_rate(struct clk *clk, unsigned long rate)
{
	return clk->parent->set_rate(clk->parent, rate);
}

/* in Hz */
static unsigned long external_high_reference = 26000000;

static unsigned long get_high_reference_clock_rate(struct clk *clk)
{
	return external_high_reference;
}

/*
 * the high frequency external clock reference
 * Default case is 26MHz. Could be changed at runtime
 * with a call to change_external_high_reference()
 */
static struct clk ckih_clk = {
	.name = "ckih",
	.get_rate = get_high_reference_clock_rate,
};

/* in Hz */
static unsigned long external_low_reference = 32768;

static unsigned long get_low_reference_clock_rate(struct clk *clk)
{
	return external_low_reference;
}

/*
 * the low frequency external clock reference
 * Default case is 32.768kHz Could be changed at runtime
 * with a call to change_external_low_reference()
 */
static struct clk ckil_clk = {
	.name = "ckil",
	.get_rate = get_low_reference_clock_rate,
};

static unsigned long get_mpll_clk(struct clk *clk)
{
	uint32_t reg;
	unsigned long ref_clk;
	unsigned long mfi = 0, mfn = 0, mfd = 0, pdf = 0;
	unsigned long long temp;

	ref_clk = clk_get_rate(clk->parent);

	reg = __raw_readl(CCM_MPCTL0);
	pdf = (reg & CCM_MPCTL0_PD_MASK) >> CCM_MPCTL0_PD_OFFSET;
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
	.name = "mpll",
	.parent = &ckih_clk,
	.get_rate = get_mpll_clk,
};

static unsigned long _clk_mpll_main_get_rate(struct clk *clk)
{
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	/* i.MX27 TO2:
	 * clk->id == 0: arm clock source path 1 which is from 2*MPLL/DIV_2
	 * clk->id == 1: arm clock source path 2 which is from 2*MPLL/DIV_3
	 */

	if (mx27_revision() >= CHIP_REV_2_0 && clk->id == 1)
		return 2UL * parent_rate / 3UL;

	return parent_rate;
}

static struct clk mpll_main_clk[] = {
	{
		/* For i.MX27 TO2, it is the MPLL path 1 of ARM core
		 * It provide the clock source whose rate is same as MPLL
		 */
		.name = "mpll_main",
		.id = 0,
		.parent = &mpll_clk,
		.get_rate = _clk_mpll_main_get_rate
	}, {
		/* For i.MX27 TO2, it is the MPLL path 2 of ARM core
		 * It provide the clock source whose rate is same MPLL * 2/3
		 */
		.name = "mpll_main",
		.id = 1,
		.parent = &mpll_clk,
		.get_rate = _clk_mpll_main_get_rate
	}
};

static unsigned long get_spll_clk(struct clk *clk)
{
	uint32_t reg;
	unsigned long ref_clk;
	unsigned long mfi = 0, mfn = 0, mfd = 0, pdf = 0;
	unsigned long long temp;

	ref_clk = clk_get_rate(clk->parent);

	reg = __raw_readl(CCM_SPCTL0);
	/*TODO: This is TO2 Bug */
	if (mx27_revision() >= CHIP_REV_2_0)
		__raw_writel(reg, CCM_SPCTL0);

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
	.name = "spll",
	.parent = &ckih_clk,
	.get_rate = get_spll_clk,
	.enable = _clk_spll_enable,
	.disable = _clk_spll_disable,
};

static unsigned long get_cpu_clk(struct clk *clk)
{
	u32 div;
	unsigned long rate;

	if (mx27_revision() >= CHIP_REV_2_0)
		div = (CSCR() & CCM_CSCR_ARM_MASK) >> CCM_CSCR_ARM_OFFSET;
	else
		div = (CSCR() & CCM_CSCR_PRESC_MASK) >> CCM_CSCR_PRESC_OFFSET;

	rate = clk_get_rate(clk->parent);
	return rate / (div + 1);
}

static struct clk cpu_clk = {
	.name = "cpu_clk",
	.parent = &mpll_main_clk[1],
	.set_parent = _clk_cpu_set_parent,
	.round_rate = _clk_cpu_round_rate,
	.get_rate = get_cpu_clk,
	.set_rate = _clk_cpu_set_rate,
};

static unsigned long get_ahb_clk(struct clk *clk)
{
	unsigned long rate;
	unsigned long bclk_pdf;

	if (mx27_revision() >= CHIP_REV_2_0)
		bclk_pdf = (CSCR() & CCM_CSCR_AHB_MASK)
					>> CCM_CSCR_AHB_OFFSET;
	else
		bclk_pdf = (CSCR() & CCM_CSCR_BCLK_MASK)
					>> CCM_CSCR_BCLK_OFFSET;

	rate = clk_get_rate(clk->parent);
	return rate / (bclk_pdf + 1);
}

static struct clk ahb_clk = {
	.name = "ahb_clk",
	.parent = &mpll_main_clk[1],
	.get_rate = get_ahb_clk,
};

static unsigned long get_ipg_clk(struct clk *clk)
{
	unsigned long rate;
	unsigned long ipg_pdf;

	if (mx27_revision() >= CHIP_REV_2_0)
		return clk_get_rate(clk->parent);
	else
		ipg_pdf = (CSCR() & CCM_CSCR_IPDIV) >> CCM_CSCR_IPDIV_OFFSET;

	rate = clk_get_rate(clk->parent);
	return rate / (ipg_pdf + 1);
}

static struct clk ipg_clk = {
	.name = "ipg_clk",
	.parent = &ahb_clk,
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
		.name = "per_clk",
		.id = 0,
		.parent = &mpll_main_clk[1],
		.get_rate = _clk_perclkx_recalc,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR1,
		.enable_shift = CCM_PCCR1_PERCLK1_OFFSET,
		.disable = _clk_disable,
	}, {
		.name = "per_clk",
		.id = 1,
		.parent = &mpll_main_clk[1],
		.get_rate = _clk_perclkx_recalc,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR1,
		.enable_shift = CCM_PCCR1_PERCLK2_OFFSET,
		.disable = _clk_disable,
	}, {
		.name = "per_clk",
		.id = 2,
		.parent = &mpll_main_clk[1],
		.round_rate = _clk_perclkx_round_rate,
		.set_rate = _clk_perclkx_set_rate,
		.get_rate = _clk_perclkx_recalc,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR1,
		.enable_shift = CCM_PCCR1_PERCLK3_OFFSET,
		.disable = _clk_disable,
	}, {
		.name = "per_clk",
		.id = 3,
		.parent = &mpll_main_clk[1],
		.round_rate = _clk_perclkx_round_rate,
		.set_rate = _clk_perclkx_set_rate,
		.get_rate = _clk_perclkx_recalc,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR1,
		.enable_shift = CCM_PCCR1_PERCLK4_OFFSET,
		.disable = _clk_disable,
	},
};

struct clk uart1_clk[] = {
	{
		.name = "uart_clk",
		.id = 0,
		.parent = &per_clk[0],
		.secondary = &uart1_clk[1],
	}, {
		.name = "uart_ipg_clk",
		.id = 0,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR1,
		.enable_shift = CCM_PCCR1_UART1_OFFSET,
		.disable = _clk_disable,
	},
};

struct clk uart2_clk[] = {
	{
		.name = "uart_clk",
		.id = 1,
		.parent = &per_clk[0],
		.secondary = &uart2_clk[1],
	}, {
		.name = "uart_ipg_clk",
		.id = 1,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR1,
		.enable_shift = CCM_PCCR1_UART2_OFFSET,
		.disable = _clk_disable,
	},
};

struct clk uart3_clk[] = {
	{
		.name = "uart_clk",
		.id = 2,
		.parent = &per_clk[0],
		.secondary = &uart3_clk[1],
	}, {
		.name = "uart_ipg_clk",
		.id = 2,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR1,
		.enable_shift = CCM_PCCR1_UART3_OFFSET,
		.disable = _clk_disable,
	},
};

struct clk uart4_clk[] = {
	{
		.name = "uart_clk",
		.id = 3,
		.parent = &per_clk[0],
		.secondary = &uart4_clk[1],
	}, {
		.name = "uart_ipg_clk",
		.id = 3,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR1,
		.enable_shift = CCM_PCCR1_UART4_OFFSET,
		.disable = _clk_disable,
	},
};

struct clk uart5_clk[] = {
	{
		.name = "uart_clk",
		.id = 4,
		.parent = &per_clk[0],
		.secondary = &uart5_clk[1],
	}, {
		.name = "uart_ipg_clk",
		.id = 4,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR1,
		.enable_shift = CCM_PCCR1_UART5_OFFSET,
		.disable = _clk_disable,
	},
};

struct clk uart6_clk[] = {
	{
		.name = "uart_clk",
		.id = 5,
		.parent = &per_clk[0],
		.secondary = &uart6_clk[1],
	}, {
		.name = "uart_ipg_clk",
		.id = 5,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR1,
		.enable_shift = CCM_PCCR1_UART6_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk gpt1_clk[] = {
	{
		.name = "gpt_clk",
		.id = 0,
		.parent = &per_clk[0],
		.secondary = &gpt1_clk[1],
	}, {
		.name = "gpt_ipg_clk",
		.id = 0,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR0,
		.enable_shift = CCM_PCCR0_GPT1_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk gpt2_clk[] = {
	{
		.name = "gpt_clk",
		.id = 1,
		.parent = &per_clk[0],
		.secondary = &gpt2_clk[1],
	}, {
		.name = "gpt_ipg_clk",
		.id = 1,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR0,
		.enable_shift = CCM_PCCR0_GPT2_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk gpt3_clk[] = {
	{
		.name = "gpt_clk",
		.id = 2,
		.parent = &per_clk[0],
		.secondary = &gpt3_clk[1],
	}, {
		.name = "gpt_ipg_clk",
		.id = 2,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR0,
		.enable_shift = CCM_PCCR0_GPT3_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk gpt4_clk[] = {
	{
		.name = "gpt_clk",
		.id = 3,
		.parent = &per_clk[0],
		.secondary = &gpt4_clk[1],
	}, {
		.name = "gpt_ipg_clk",
		.id = 3,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR0,
		.enable_shift = CCM_PCCR0_GPT4_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk gpt5_clk[] = {
	{
		.name = "gpt_clk",
		.id = 4,
		.parent = &per_clk[0],
		.secondary = &gpt5_clk[1],
	}, {
		.name = "gpt_ipg_clk",
		.id = 4,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR0,
		.enable_shift = CCM_PCCR0_GPT5_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk gpt6_clk[] = {
	{
		.name = "gpt_clk",
		.id = 5,
		.parent = &per_clk[0],
		.secondary = &gpt6_clk[1],
	}, {
		.name = "gpt_ipg_clk",
		.id = 5,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR0,
		.enable_shift = CCM_PCCR0_GPT6_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk pwm_clk[] = {
	{
		.name = "pwm_clk",
		.parent = &per_clk[0],
		.secondary = &pwm_clk[1],
	}, {
		.name = "pwm_clk",
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR0,
		.enable_shift = CCM_PCCR0_PWM_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk sdhc1_clk[] = {
	{
		.name = "sdhc_clk",
		.id = 0,
		.parent = &per_clk[1],
		.secondary = &sdhc1_clk[1],
	}, {
		.name = "sdhc_ipg_clk",
		.id = 0,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR0,
		.enable_shift = CCM_PCCR0_SDHC1_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk sdhc2_clk[] = {
	{
		.name = "sdhc_clk",
		.id = 1,
		.parent = &per_clk[1],
		.secondary = &sdhc2_clk[1],
	}, {
		.name = "sdhc_ipg_clk",
		.id = 1,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR0,
		.enable_shift = CCM_PCCR0_SDHC2_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk sdhc3_clk[] = {
	{
		.name = "sdhc_clk",
		.id = 2,
		.parent = &per_clk[1],
		.secondary = &sdhc3_clk[1],
	}, {
		.name = "sdhc_ipg_clk",
		.id = 2,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR0,
		.enable_shift = CCM_PCCR0_SDHC3_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk cspi1_clk[] = {
	{
		.name = "cspi_clk",
		.id = 0,
		.parent = &per_clk[1],
		.secondary = &cspi1_clk[1],
	}, {
		.name = "cspi_ipg_clk",
		.id = 0,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR0,
		.enable_shift = CCM_PCCR0_CSPI1_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk cspi2_clk[] = {
	{
		.name = "cspi_clk",
		.id = 1,
		.parent = &per_clk[1],
		.secondary = &cspi2_clk[1],
	}, {
		.name = "cspi_ipg_clk",
		.id = 1,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR0,
		.enable_shift = CCM_PCCR0_CSPI2_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk cspi3_clk[] = {
	{
		.name = "cspi_clk",
		.id = 2,
		.parent = &per_clk[1],
		.secondary = &cspi3_clk[1],
	}, {
		.name = "cspi_ipg_clk",
		.id = 2,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR0,
		.enable_shift = CCM_PCCR0_CSPI3_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk lcdc_clk[] = {
	{
		.name = "lcdc_clk",
		.parent = &per_clk[2],
		.secondary = &lcdc_clk[1],
		.round_rate = _clk_parent_round_rate,
		.set_rate = _clk_parent_set_rate,
	}, {
		.name = "lcdc_ipg_clk",
		.parent = &ipg_clk,
		.secondary = &lcdc_clk[2],
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR0,
		.enable_shift = CCM_PCCR0_LCDC_OFFSET,
		.disable = _clk_disable,
	}, {
		.name = "lcdc_ahb_clk",
		.parent = &ahb_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR1,
		.enable_shift = CCM_PCCR1_HCLK_LCDC_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk csi_clk[] = {
	{
		.name = "csi_perclk",
		.parent = &per_clk[3],
		.secondary = &csi_clk[1],
		.round_rate = _clk_parent_round_rate,
		.set_rate = _clk_parent_set_rate,
	}, {
		.name = "csi_ahb_clk",
		.parent = &ahb_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR1,
		.enable_shift = CCM_PCCR1_HCLK_CSI_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk usb_clk[] = {
	{
		.name = "usb_clk",
		.parent = &spll_clk,
		.get_rate = _clk_usb_recalc,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR1,
		.enable_shift = CCM_PCCR1_USBOTG_OFFSET,
		.disable = _clk_disable,
	}, {
		.name = "usb_ahb_clk",
		.parent = &ahb_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR1,
		.enable_shift = CCM_PCCR1_HCLK_USBOTG_OFFSET,
		.disable = _clk_disable,
	}
};

static struct clk ssi1_clk[] = {
	{
		.name = "ssi_clk",
		.id = 0,
		.parent = &mpll_main_clk[1],
		.secondary = &ssi1_clk[1],
		.get_rate = _clk_ssi1_recalc,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR1,
		.enable_shift = CCM_PCCR1_SSI1_BAUD_OFFSET,
		.disable = _clk_disable,
	}, {
		.name = "ssi_ipg_clk",
		.id = 0,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR0,
		.enable_shift = CCM_PCCR0_SSI1_IPG_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk ssi2_clk[] = {
	{
		.name = "ssi_clk",
		.id = 1,
		.parent = &mpll_main_clk[1],
		.secondary = &ssi2_clk[1],
		.get_rate = _clk_ssi2_recalc,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR1,
		.enable_shift = CCM_PCCR1_SSI2_BAUD_OFFSET,
		.disable = _clk_disable,
	}, {
		.name = "ssi_ipg_clk",
		.id = 1,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR0,
		.enable_shift = CCM_PCCR0_SSI2_IPG_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk nfc_clk = {
	.name = "nfc_clk",
	.parent = &cpu_clk,
	.get_rate = _clk_nfc_recalc,
	.enable = _clk_enable,
	.enable_reg = CCM_PCCR1,
	.enable_shift = CCM_PCCR1_NFC_BAUD_OFFSET,
	.disable = _clk_disable,
};

static struct clk vpu_clk = {
	.name = "vpu_clk",
	.parent = &mpll_main_clk[1],
	.get_rate = _clk_vpu_recalc,
	.enable = _clk_vpu_enable,
	.disable = _clk_vpu_disable,
};

static struct clk dma_clk = {
	.name = "dma_clk",
	.parent = &ahb_clk,
	.enable = _clk_dma_enable,
	.disable = _clk_dma_disable,
};

static struct clk rtic_clk = {
	.name = "rtic_clk",
	.parent = &ahb_clk,
	.enable = _clk_rtic_enable,
	.disable = _clk_rtic_disable,
};

static struct clk brom_clk = {
	.name = "brom_clk",
	.parent = &ahb_clk,
	.enable = _clk_enable,
	.enable_reg = CCM_PCCR1,
	.enable_shift = CCM_PCCR1_HCLK_BROM_OFFSET,
	.disable = _clk_disable,
};

static struct clk emma_clk = {
	.name = "emma_clk",
	.parent = &ahb_clk,
	.enable = _clk_emma_enable,
	.disable = _clk_emma_disable,
};

static struct clk slcdc_clk = {
	.name = "slcdc_clk",
	.parent = &ahb_clk,
	.enable = _clk_slcdc_enable,
	.disable = _clk_slcdc_disable,
};

static struct clk fec_clk = {
	.name = "fec_clk",
	.parent = &ahb_clk,
	.enable = _clk_fec_enable,
	.disable = _clk_fec_disable,
};

static struct clk emi_clk = {
	.name = "emi_clk",
	.parent = &ahb_clk,
	.enable = _clk_enable,
	.enable_reg = CCM_PCCR1,
	.enable_shift = CCM_PCCR1_HCLK_EMI_OFFSET,
	.disable = _clk_disable,
};

static struct clk sahara2_clk = {
	.name = "sahara_clk",
	.parent = &ahb_clk,
	.enable = _clk_sahara2_enable,
	.disable = _clk_sahara2_disable,
};

static struct clk ata_clk = {
	.name = "ata_clk",
	.parent = &ahb_clk,
	.enable = _clk_enable,
	.enable_reg = CCM_PCCR1,
	.enable_shift = CCM_PCCR1_HCLK_ATA_OFFSET,
	.disable = _clk_disable,
};

static struct clk mstick1_clk = {
	.name = "mstick1_clk",
	.parent = &ipg_clk,
	.enable = _clk_mstick1_enable,
	.disable = _clk_mstick1_disable,
};

static struct clk wdog_clk = {
	.name = "wdog_clk",
	.parent = &ipg_clk,
	.enable = _clk_enable,
	.enable_reg = CCM_PCCR1,
	.enable_shift = CCM_PCCR1_WDT_OFFSET,
	.disable = _clk_disable,
};

static struct clk gpio_clk = {
	.name = "gpio_clk",
	.parent = &ipg_clk,
	.enable = _clk_enable,
	.enable_reg = CCM_PCCR1,
	.enable_shift = CCM_PCCR0_GPIO_OFFSET,
	.disable = _clk_disable,
};

static struct clk i2c_clk[] = {
	{
		.name = "i2c_clk",
		.id = 0,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR0,
		.enable_shift = CCM_PCCR0_I2C1_OFFSET,
		.disable = _clk_disable,
	}, {
		.name = "i2c_clk",
		.id = 1,
		.parent = &ipg_clk,
		.enable = _clk_enable,
		.enable_reg = CCM_PCCR0,
		.enable_shift = CCM_PCCR0_I2C2_OFFSET,
		.disable = _clk_disable,
	},
};

static struct clk iim_clk = {
	.name = "iim_clk",
	.parent = &ipg_clk,
	.enable = _clk_enable,
	.enable_reg = CCM_PCCR0,
	.enable_shift = CCM_PCCR0_IIM_OFFSET,
	.disable = _clk_disable,
};

static struct clk kpp_clk = {
	.name = "kpp_clk",
	.parent = &ipg_clk,
	.enable = _clk_enable,
	.enable_reg = CCM_PCCR0,
	.enable_shift = CCM_PCCR0_KPP_OFFSET,
	.disable = _clk_disable,
};

static struct clk owire_clk = {
	.name = "owire_clk",
	.parent = &ipg_clk,
	.enable = _clk_enable,
	.enable_reg = CCM_PCCR0,
	.enable_shift = CCM_PCCR0_OWIRE_OFFSET,
	.disable = _clk_disable,
};

static struct clk rtc_clk = {
	.name = "rtc_clk",
	.parent = &ipg_clk,
	.enable = _clk_enable,
	.enable_reg = CCM_PCCR0,
	.enable_shift = CCM_PCCR0_RTC_OFFSET,
	.disable = _clk_disable,
};

static struct clk scc_clk = {
	.name = "scc_clk",
	.parent = &ipg_clk,
	.enable = _clk_enable,
	.enable_reg = CCM_PCCR0,
	.enable_shift = CCM_PCCR0_SCC_OFFSET,
	.disable = _clk_disable,
};

static unsigned long _clk_clko_round_rate(struct clk *clk, unsigned long rate)
{
	u32 div;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);
	div = parent_rate / rate;
	if (parent_rate % rate)
		div++;

	if (div > 8)
		div = 8;

	return parent_rate / div;
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

	reg = __raw_readl(CCM_PCDR0) & ~CCM_PCDR0_CLKODIV_MASK;
	reg |= div << CCM_PCDR0_CLKODIV_OFFSET;
	__raw_writel(reg, CCM_PCDR0);

	return 0;
}

static unsigned long _clk_clko_recalc(struct clk *clk)
{
	u32 div;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	div = __raw_readl(CCM_PCDR0) & CCM_PCDR0_CLKODIV_MASK >>
		CCM_PCDR0_CLKODIV_OFFSET;
	div++;

	return parent_rate / div;
}

static int _clk_clko_set_parent(struct clk *clk, struct clk *parent)
{
	u32 reg;

	reg = __raw_readl(CCM_CCSR) & ~CCM_CCSR_CLKOSEL_MASK;

	if (parent == &ckil_clk)
		reg |= 0 << CCM_CCSR_CLKOSEL_OFFSET;
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
	else if (parent == &cpu_clk)
		reg |= 7 << CCM_CCSR_CLKOSEL_OFFSET;
	else if (parent == &ahb_clk)
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
	else if (parent == &ssi1_clk[0])
		reg |= 0xE << CCM_CCSR_CLKOSEL_OFFSET;
	else if (parent == &ssi2_clk[0])
		reg |= 0xF << CCM_CCSR_CLKOSEL_OFFSET;
	else if (parent == &nfc_clk)
		reg |= 0x10 << CCM_CCSR_CLKOSEL_OFFSET;
	else if (parent == &mstick1_clk)
		reg |= 0x11 << CCM_CCSR_CLKOSEL_OFFSET;
	else if (parent == &vpu_clk)
		reg |= 0x12 << CCM_CCSR_CLKOSEL_OFFSET;
	else if (parent == &usb_clk[0])
		reg |= 0x15 << CCM_CCSR_CLKOSEL_OFFSET;
	else
		return -EINVAL;

	__raw_writel(reg, CCM_CCSR);

	return 0;
}

static int _clk_clko_enable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(CCM_PCDR0) | CCM_PCDR0_CLKO_EN;
	__raw_writel(reg, CCM_PCDR0);

	return 0;
}

static void _clk_clko_disable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(CCM_PCDR0) & ~CCM_PCDR0_CLKO_EN;
	__raw_writel(reg, CCM_PCDR0);
}

static struct clk clko_clk = {
	.name = "clko_clk",
	.get_rate = _clk_clko_recalc,
	.set_rate = _clk_clko_set_rate,
	.round_rate = _clk_clko_round_rate,
	.set_parent = _clk_clko_set_parent,
	.enable = _clk_clko_enable,
	.disable = _clk_clko_disable,
};

static struct clk *mxc_clks[] = {
	&ckih_clk,
	&ckil_clk,
	&mpll_clk,
	&mpll_main_clk[0],
	&mpll_main_clk[1],
	&spll_clk,
	&cpu_clk,
	&ahb_clk,
	&ipg_clk,
	&per_clk[0],
	&per_clk[1],
	&per_clk[2],
	&per_clk[3],
	&clko_clk,
	&uart1_clk[0],
	&uart1_clk[1],
	&uart2_clk[0],
	&uart2_clk[1],
	&uart3_clk[0],
	&uart3_clk[1],
	&uart4_clk[0],
	&uart4_clk[1],
	&uart5_clk[0],
	&uart5_clk[1],
	&uart6_clk[0],
	&uart6_clk[1],
	&gpt1_clk[0],
	&gpt1_clk[1],
	&gpt2_clk[0],
	&gpt2_clk[1],
	&gpt3_clk[0],
	&gpt3_clk[1],
	&gpt4_clk[0],
	&gpt4_clk[1],
	&gpt5_clk[0],
	&gpt5_clk[1],
	&gpt6_clk[0],
	&gpt6_clk[1],
	&pwm_clk[0],
	&pwm_clk[1],
	&sdhc1_clk[0],
	&sdhc1_clk[1],
	&sdhc2_clk[0],
	&sdhc2_clk[1],
	&sdhc3_clk[0],
	&sdhc3_clk[1],
	&cspi1_clk[0],
	&cspi1_clk[1],
	&cspi2_clk[0],
	&cspi2_clk[1],
	&cspi3_clk[0],
	&cspi3_clk[1],
	&lcdc_clk[0],
	&lcdc_clk[1],
	&lcdc_clk[2],
	&csi_clk[0],
	&csi_clk[1],
	&usb_clk[0],
	&usb_clk[1],
	&ssi1_clk[0],
	&ssi1_clk[1],
	&ssi2_clk[0],
	&ssi2_clk[1],
	&nfc_clk,
	&vpu_clk,
	&dma_clk,
	&rtic_clk,
	&brom_clk,
	&emma_clk,
	&slcdc_clk,
	&fec_clk,
	&emi_clk,
	&sahara2_clk,
	&ata_clk,
	&mstick1_clk,
	&wdog_clk,
	&gpio_clk,
	&i2c_clk[0],
	&i2c_clk[1],
	&iim_clk,
	&kpp_clk,
	&owire_clk,
	&rtc_clk,
	&scc_clk,
};

void __init change_external_low_reference(unsigned long new_ref)
{
	external_low_reference = new_ref;
}

unsigned long __init clk_early_get_timer_rate(void)
{
	return clk_get_rate(&per_clk[0]);
}

static void __init probe_mxc_clocks(void)
{
	int i;

	if (mx27_revision() >= CHIP_REV_2_0) {
		if (CSCR() & 0x8000)
			cpu_clk.parent = &mpll_main_clk[0];

		if (!(CSCR() & 0x00800000))
			ssi2_clk[0].parent = &spll_clk;

		if (!(CSCR() & 0x00400000))
			ssi1_clk[0].parent = &spll_clk;

		if (!(CSCR() & 0x00200000))
			vpu_clk.parent = &spll_clk;
	} else {
		cpu_clk.parent = &mpll_clk;
		cpu_clk.set_parent = NULL;
		cpu_clk.round_rate = NULL;
		cpu_clk.set_rate = NULL;
		ahb_clk.parent = &mpll_clk;

		for (i = 0; i < sizeof(per_clk) / sizeof(per_clk[0]); i++)
			per_clk[i].parent = &mpll_clk;

		ssi1_clk[0].parent = &mpll_clk;
		ssi2_clk[0].parent = &mpll_clk;

		vpu_clk.parent = &mpll_clk;
	}
}

/*
 * must be called very early to get information about the
 * available clock rate when the timer framework starts
 */
int __init mxc_clocks_init(unsigned long fref)
{
	u32 cscr;
	struct clk **clkp;

	external_high_reference = fref;

	/* detect clock reference for both system PLL */
	cscr = CSCR();
	if (cscr & CCM_CSCR_MCU)
		mpll_clk.parent = &ckih_clk;
	else
		mpll_clk.parent = &ckil_clk;

	if (cscr & CCM_CSCR_SP)
		spll_clk.parent = &ckih_clk;
	else
		spll_clk.parent = &ckil_clk;

	probe_mxc_clocks();

	per_clk[0].enable(&per_clk[0]);
	gpt1_clk[1].enable(&gpt1_clk[1]);

	for (clkp = mxc_clks; clkp < mxc_clks + ARRAY_SIZE(mxc_clks); clkp++)
		clk_register(*clkp);

	/* Turn off all possible clocks */
	__raw_writel(CCM_PCCR0_GPT1_MASK, CCM_PCCR0);
	__raw_writel(CCM_PCCR1_PERCLK1_MASK | CCM_PCCR1_HCLK_EMI_MASK,
		     CCM_PCCR1);
	spll_clk.disable(&spll_clk);

	/* This will propagate to all children and init all the clock rates */

	clk_enable(&emi_clk);
	clk_enable(&gpio_clk);
	clk_enable(&iim_clk);
	clk_enable(&gpt1_clk[0]);
#ifdef CONFIG_DEBUG_LL_CONSOLE
	clk_enable(&uart1_clk[0]);
#endif
	return 0;
}
