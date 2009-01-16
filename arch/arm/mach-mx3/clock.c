/*
 * Copyright 2005-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2008 by Sascha Hauer <kernel@pengutronix.de>
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

#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <mach/clock.h>
#include <mach/hardware.h>
#include <asm/div64.h>

#include "crm_regs.h"

#define PRE_DIV_MIN_FREQ    10000000 /* Minimum Frequency after Predivider */

static void __calc_pre_post_dividers(u32 div, u32 *pre, u32 *post)
{
	u32 min_pre, temp_pre, old_err, err;

	if (div >= 512) {
		*pre = 8;
		*post = 64;
	} else if (div >= 64) {
		min_pre = (div - 1) / 64 + 1;
		old_err = 8;
		for (temp_pre = 8; temp_pre >= min_pre; temp_pre--) {
			err = div % temp_pre;
			if (err == 0) {
				*pre = temp_pre;
				break;
			}
			err = temp_pre - err;
			if (err < old_err) {
				old_err = err;
				*pre = temp_pre;
			}
		}
		*post = (div + *pre - 1) / *pre;
	} else if (div <= 8) {
		*pre = div;
		*post = 1;
	} else {
		*pre = 1;
		*post = div;
	}
}

static struct clk mcu_pll_clk;
static struct clk mcu_main_clk;
static struct clk usb_pll_clk;
static struct clk serial_pll_clk;
static struct clk ipg_clk;
static struct clk ckih_clk;
static struct clk ahb_clk;

static int _clk_enable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(clk->enable_reg);
	reg |= 3 << clk->enable_shift;
	__raw_writel(reg, clk->enable_reg);

	return 0;
}

static void _clk_disable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(clk->enable_reg);
	reg &= ~(3 << clk->enable_shift);
	__raw_writel(reg, clk->enable_reg);
}

static void _clk_emi_disable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(clk->enable_reg);
	reg &= ~(3 << clk->enable_shift);
	reg |= (1 << clk->enable_shift);
	__raw_writel(reg, clk->enable_reg);
}

static int _clk_pll_set_rate(struct clk *clk, unsigned long rate)
{
	u32 reg;
	signed long pd = 1;	/* Pre-divider */
	signed long mfi;	/* Multiplication Factor (Integer part) */
	signed long mfn;	/* Multiplication Factor (Integer part) */
	signed long mfd;	/* Multiplication Factor (Denominator Part) */
	signed long tmp;
	u32 ref_freq = clk_get_rate(clk->parent);

	while (((ref_freq / pd) * 10) > rate)
		pd++;

	if ((ref_freq / pd) < PRE_DIV_MIN_FREQ)
		return -EINVAL;

	/* the ref_freq/2 in the following is to round up */
	mfi = (((rate / 2) * pd) + (ref_freq / 2)) / ref_freq;
	if (mfi < 5 || mfi > 15)
		return -EINVAL;

	/* pick a mfd value that will work
	 * then solve for mfn */
	mfd = ref_freq / 50000;

	/*
	 *          pll_freq * pd * mfd
	 *   mfn = --------------------  -  (mfi * mfd)
	 *           2 * ref_freq
	 */
	/* the tmp/2 is for rounding */
	tmp = ref_freq / 10000;
	mfn =
	    ((((((rate / 2) + (tmp / 2)) / tmp) * pd) * mfd) / 10000) -
	    (mfi * mfd);

	mfn = mfn & 0x3ff;
	pd--;
	mfd--;

	/* Change the Pll value */
	reg = (mfi << MXC_CCM_PCTL_MFI_OFFSET) |
	    (mfn << MXC_CCM_PCTL_MFN_OFFSET) |
	    (mfd << MXC_CCM_PCTL_MFD_OFFSET) | (pd << MXC_CCM_PCTL_PD_OFFSET);

	if (clk == &mcu_pll_clk)
		__raw_writel(reg, MXC_CCM_MPCTL);
	else if (clk == &usb_pll_clk)
		__raw_writel(reg, MXC_CCM_UPCTL);
	else if (clk == &serial_pll_clk)
		__raw_writel(reg, MXC_CCM_SRPCTL);

	return 0;
}

static unsigned long _clk_pll_get_rate(struct clk *clk)
{
	long mfi, mfn, mfd, pdf, ref_clk, mfn_abs;
	unsigned long reg, ccmr;
	s64 temp;
	unsigned int prcs;

	ccmr = __raw_readl(MXC_CCM_CCMR);
	prcs = (ccmr & MXC_CCM_CCMR_PRCS_MASK) >> MXC_CCM_CCMR_PRCS_OFFSET;
	if (prcs == 0x1)
		ref_clk = CKIL_CLK_FREQ * 1024;
	else
		ref_clk = clk_get_rate(&ckih_clk);

	if (clk == &mcu_pll_clk) {
		if ((ccmr & MXC_CCM_CCMR_MPE) == 0)
			return ref_clk;
		if ((ccmr & MXC_CCM_CCMR_MDS) != 0)
			return ref_clk;
		reg = __raw_readl(MXC_CCM_MPCTL);
	} else if (clk == &usb_pll_clk)
		reg = __raw_readl(MXC_CCM_UPCTL);
	else if (clk == &serial_pll_clk)
		reg = __raw_readl(MXC_CCM_SRPCTL);
	else {
		BUG();
		return 0;
	}

	pdf = (reg & MXC_CCM_PCTL_PD_MASK) >> MXC_CCM_PCTL_PD_OFFSET;
	mfd = (reg & MXC_CCM_PCTL_MFD_MASK) >> MXC_CCM_PCTL_MFD_OFFSET;
	mfi = (reg & MXC_CCM_PCTL_MFI_MASK) >> MXC_CCM_PCTL_MFI_OFFSET;
	mfi = (mfi <= 5) ? 5 : mfi;
	mfn = mfn_abs = reg & MXC_CCM_PCTL_MFN_MASK;

	if (mfn >= 0x200) {
		mfn |= 0xFFFFFE00;
		mfn_abs = -mfn;
	}

	ref_clk *= 2;
	ref_clk /= pdf + 1;

	temp = (u64) ref_clk * mfn_abs;
	do_div(temp, mfd + 1);
	if (mfn < 0)
		temp = -temp;
	temp = (ref_clk * mfi) + temp;

	return temp;
}

static int _clk_usb_pll_enable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(MXC_CCM_CCMR);
	reg |= MXC_CCM_CCMR_UPE;
	__raw_writel(reg, MXC_CCM_CCMR);

	/* No lock bit on MX31, so using max time from spec */
	udelay(80);

	return 0;
}

static void _clk_usb_pll_disable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(MXC_CCM_CCMR);
	reg &= ~MXC_CCM_CCMR_UPE;
	__raw_writel(reg, MXC_CCM_CCMR);
}

static int _clk_serial_pll_enable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(MXC_CCM_CCMR);
	reg |= MXC_CCM_CCMR_SPE;
	__raw_writel(reg, MXC_CCM_CCMR);

	/* No lock bit on MX31, so using max time from spec */
	udelay(80);

	return 0;
}

static void _clk_serial_pll_disable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(MXC_CCM_CCMR);
	reg &= ~MXC_CCM_CCMR_SPE;
	__raw_writel(reg, MXC_CCM_CCMR);
}

#define PDR0(mask, off) ((__raw_readl(MXC_CCM_PDR0) & mask) >> off)
#define PDR1(mask, off) ((__raw_readl(MXC_CCM_PDR1) & mask) >> off)
#define PDR2(mask, off) ((__raw_readl(MXC_CCM_PDR2) & mask) >> off)

static unsigned long _clk_mcu_main_get_rate(struct clk *clk)
{
	u32 pmcr0 = __raw_readl(MXC_CCM_PMCR0);

	if ((pmcr0 & MXC_CCM_PMCR0_DFSUP1) == MXC_CCM_PMCR0_DFSUP1_SPLL)
		return clk_get_rate(&serial_pll_clk);
	else
		return clk_get_rate(&mcu_pll_clk);
}

static unsigned long _clk_hclk_get_rate(struct clk *clk)
{
	unsigned long max_pdf;

	max_pdf = PDR0(MXC_CCM_PDR0_MAX_PODF_MASK,
		       MXC_CCM_PDR0_MAX_PODF_OFFSET);
	return clk_get_rate(clk->parent) / (max_pdf + 1);
}

static unsigned long _clk_ipg_get_rate(struct clk *clk)
{
	unsigned long ipg_pdf;

	ipg_pdf = PDR0(MXC_CCM_PDR0_IPG_PODF_MASK,
		       MXC_CCM_PDR0_IPG_PODF_OFFSET);
	return clk_get_rate(clk->parent) / (ipg_pdf + 1);
}

static unsigned long _clk_nfc_get_rate(struct clk *clk)
{
	unsigned long nfc_pdf;

	nfc_pdf = PDR0(MXC_CCM_PDR0_NFC_PODF_MASK,
		       MXC_CCM_PDR0_NFC_PODF_OFFSET);
	return clk_get_rate(clk->parent) / (nfc_pdf + 1);
}

static unsigned long _clk_hsp_get_rate(struct clk *clk)
{
	unsigned long hsp_pdf;

	hsp_pdf = PDR0(MXC_CCM_PDR0_HSP_PODF_MASK,
		       MXC_CCM_PDR0_HSP_PODF_OFFSET);
	return clk_get_rate(clk->parent) / (hsp_pdf + 1);
}

static unsigned long _clk_usb_get_rate(struct clk *clk)
{
	unsigned long usb_pdf, usb_prepdf;

	usb_pdf = PDR1(MXC_CCM_PDR1_USB_PODF_MASK,
		       MXC_CCM_PDR1_USB_PODF_OFFSET);
	usb_prepdf = PDR1(MXC_CCM_PDR1_USB_PRDF_MASK,
			  MXC_CCM_PDR1_USB_PRDF_OFFSET);
	return clk_get_rate(clk->parent) / (usb_prepdf + 1) / (usb_pdf + 1);
}

static unsigned long _clk_csi_get_rate(struct clk *clk)
{
	u32 reg, pre, post;

	reg = __raw_readl(MXC_CCM_PDR0);
	pre = (reg & MXC_CCM_PDR0_CSI_PRDF_MASK) >>
	    MXC_CCM_PDR0_CSI_PRDF_OFFSET;
	pre++;
	post = (reg & MXC_CCM_PDR0_CSI_PODF_MASK) >>
	    MXC_CCM_PDR0_CSI_PODF_OFFSET;
	post++;
	return clk_get_rate(clk->parent) / (pre * post);
}

static unsigned long _clk_csi_round_rate(struct clk *clk, unsigned long rate)
{
	u32 pre, post, parent = clk_get_rate(clk->parent);
	u32 div = parent / rate;

	if (parent % rate)
		div++;

	__calc_pre_post_dividers(div, &pre, &post);

	return parent / (pre * post);
}

static int _clk_csi_set_rate(struct clk *clk, unsigned long rate)
{
	u32 reg, div, pre, post, parent = clk_get_rate(clk->parent);

	div = parent / rate;

	if ((parent / div) != rate)
		return -EINVAL;

	__calc_pre_post_dividers(div, &pre, &post);

	/* Set CSI clock divider */
	reg = __raw_readl(MXC_CCM_PDR0) &
	    ~(MXC_CCM_PDR0_CSI_PODF_MASK | MXC_CCM_PDR0_CSI_PRDF_MASK);
	reg |= (post - 1) << MXC_CCM_PDR0_CSI_PODF_OFFSET;
	reg |= (pre - 1) << MXC_CCM_PDR0_CSI_PRDF_OFFSET;
	__raw_writel(reg, MXC_CCM_PDR0);

	return 0;
}

static unsigned long _clk_per_get_rate(struct clk *clk)
{
	unsigned long per_pdf;

	per_pdf = PDR0(MXC_CCM_PDR0_PER_PODF_MASK,
		       MXC_CCM_PDR0_PER_PODF_OFFSET);
	return clk_get_rate(clk->parent) / (per_pdf + 1);
}

static unsigned long _clk_ssi1_get_rate(struct clk *clk)
{
	unsigned long ssi1_pdf, ssi1_prepdf;

	ssi1_pdf = PDR1(MXC_CCM_PDR1_SSI1_PODF_MASK,
			MXC_CCM_PDR1_SSI1_PODF_OFFSET);
	ssi1_prepdf = PDR1(MXC_CCM_PDR1_SSI1_PRE_PODF_MASK,
			   MXC_CCM_PDR1_SSI1_PRE_PODF_OFFSET);
	return clk_get_rate(clk->parent) / (ssi1_prepdf + 1) / (ssi1_pdf + 1);
}

static unsigned long _clk_ssi2_get_rate(struct clk *clk)
{
	unsigned long ssi2_pdf, ssi2_prepdf;

	ssi2_pdf = PDR1(MXC_CCM_PDR1_SSI2_PODF_MASK,
			MXC_CCM_PDR1_SSI2_PODF_OFFSET);
	ssi2_prepdf = PDR1(MXC_CCM_PDR1_SSI2_PRE_PODF_MASK,
			   MXC_CCM_PDR1_SSI2_PRE_PODF_OFFSET);
	return clk_get_rate(clk->parent) / (ssi2_prepdf + 1) / (ssi2_pdf + 1);
}

static unsigned long _clk_firi_get_rate(struct clk *clk)
{
	unsigned long firi_pdf, firi_prepdf;

	firi_pdf = PDR1(MXC_CCM_PDR1_FIRI_PODF_MASK,
			MXC_CCM_PDR1_FIRI_PODF_OFFSET);
	firi_prepdf = PDR1(MXC_CCM_PDR1_FIRI_PRE_PODF_MASK,
			   MXC_CCM_PDR1_FIRI_PRE_PODF_OFFSET);
	return clk_get_rate(clk->parent) / (firi_prepdf + 1) / (firi_pdf + 1);
}

static unsigned long _clk_firi_round_rate(struct clk *clk, unsigned long rate)
{
	u32 pre, post;
	u32 parent = clk_get_rate(clk->parent);
	u32 div = parent / rate;

	if (parent % rate)
		div++;

	__calc_pre_post_dividers(div, &pre, &post);

	return parent / (pre * post);

}

static int _clk_firi_set_rate(struct clk *clk, unsigned long rate)
{
	u32 reg, div, pre, post, parent = clk_get_rate(clk->parent);

	div = parent / rate;

	if ((parent / div) != rate)
		return -EINVAL;

	__calc_pre_post_dividers(div, &pre, &post);

	/* Set FIRI clock divider */
	reg = __raw_readl(MXC_CCM_PDR1) &
	    ~(MXC_CCM_PDR1_FIRI_PODF_MASK | MXC_CCM_PDR1_FIRI_PRE_PODF_MASK);
	reg |= (pre - 1) << MXC_CCM_PDR1_FIRI_PRE_PODF_OFFSET;
	reg |= (post - 1) << MXC_CCM_PDR1_FIRI_PODF_OFFSET;
	__raw_writel(reg, MXC_CCM_PDR1);

	return 0;
}

static unsigned long _clk_mbx_get_rate(struct clk *clk)
{
	return clk_get_rate(clk->parent) / 2;
}

static unsigned long _clk_mstick1_get_rate(struct clk *clk)
{
	unsigned long msti_pdf;

	msti_pdf = PDR2(MXC_CCM_PDR2_MST1_PDF_MASK,
			MXC_CCM_PDR2_MST1_PDF_OFFSET);
	return clk_get_rate(clk->parent) / (msti_pdf + 1);
}

static unsigned long _clk_mstick2_get_rate(struct clk *clk)
{
	unsigned long msti_pdf;

	msti_pdf = PDR2(MXC_CCM_PDR2_MST2_PDF_MASK,
			MXC_CCM_PDR2_MST2_PDF_OFFSET);
	return clk_get_rate(clk->parent) / (msti_pdf + 1);
}

static unsigned long ckih_rate;

static unsigned long clk_ckih_get_rate(struct clk *clk)
{
	return ckih_rate;
}

static struct clk ckih_clk = {
	.name = "ckih",
	.get_rate = clk_ckih_get_rate,
};

static unsigned long clk_ckil_get_rate(struct clk *clk)
{
	return CKIL_CLK_FREQ;
}

static struct clk ckil_clk = {
	.name = "ckil",
	.get_rate = clk_ckil_get_rate,
};

static struct clk mcu_pll_clk = {
	.name = "mcu_pll",
	.parent = &ckih_clk,
	.set_rate = _clk_pll_set_rate,
	.get_rate = _clk_pll_get_rate,
};

static struct clk mcu_main_clk = {
	.name = "mcu_main_clk",
	.parent = &mcu_pll_clk,
	.get_rate = _clk_mcu_main_get_rate,
};

static struct clk serial_pll_clk = {
	.name = "serial_pll",
	.parent = &ckih_clk,
	.set_rate = _clk_pll_set_rate,
	.get_rate = _clk_pll_get_rate,
	.enable = _clk_serial_pll_enable,
	.disable = _clk_serial_pll_disable,
};

static struct clk usb_pll_clk = {
	.name = "usb_pll",
	.parent = &ckih_clk,
	.set_rate = _clk_pll_set_rate,
	.get_rate = _clk_pll_get_rate,
	.enable = _clk_usb_pll_enable,
	.disable = _clk_usb_pll_disable,
};

static struct clk ahb_clk = {
	.name = "ahb_clk",
	.parent = &mcu_main_clk,
	.get_rate = _clk_hclk_get_rate,
};

static struct clk per_clk = {
	.name = "per_clk",
	.parent = &usb_pll_clk,
	.get_rate = _clk_per_get_rate,
};

static struct clk perclk_clk = {
	.name = "perclk_clk",
	.parent = &ipg_clk,
};

static struct clk cspi_clk[] = {
	{
	 .name = "cspi_clk",
	 .id = 0,
	 .parent = &ipg_clk,
	 .enable = _clk_enable,
	 .enable_reg = MXC_CCM_CGR2,
	 .enable_shift = MXC_CCM_CGR2_CSPI1_OFFSET,
	 .disable = _clk_disable,},
	{
	 .name = "cspi_clk",
	 .id = 1,
	 .parent = &ipg_clk,
	 .enable = _clk_enable,
	 .enable_reg = MXC_CCM_CGR2,
	 .enable_shift = MXC_CCM_CGR2_CSPI2_OFFSET,
	 .disable = _clk_disable,},
	{
	 .name = "cspi_clk",
	 .id = 2,
	 .parent = &ipg_clk,
	 .enable = _clk_enable,
	 .enable_reg = MXC_CCM_CGR0,
	 .enable_shift = MXC_CCM_CGR0_CSPI3_OFFSET,
	 .disable = _clk_disable,},
};

static struct clk ipg_clk = {
	.name = "ipg_clk",
	.parent = &ahb_clk,
	.get_rate = _clk_ipg_get_rate,
};

static struct clk emi_clk = {
	.name = "emi_clk",
	.parent = &ahb_clk,
	.enable = _clk_enable,
	.enable_reg = MXC_CCM_CGR2,
	.enable_shift = MXC_CCM_CGR2_EMI_OFFSET,
	.disable = _clk_emi_disable,
};

static struct clk gpt_clk = {
	.name = "gpt_clk",
	.parent = &perclk_clk,
	.enable = _clk_enable,
	.enable_reg = MXC_CCM_CGR0,
	.enable_shift = MXC_CCM_CGR0_GPT_OFFSET,
	.disable = _clk_disable,
};

static struct clk pwm_clk = {
	.name = "pwm_clk",
	.parent = &perclk_clk,
	.enable = _clk_enable,
	.enable_reg = MXC_CCM_CGR0,
	.enable_shift = MXC_CCM_CGR1_PWM_OFFSET,
	.disable = _clk_disable,
};

static struct clk epit_clk[] = {
	{
	 .name = "epit_clk",
	 .id = 0,
	 .parent = &perclk_clk,
	 .enable = _clk_enable,
	 .enable_reg = MXC_CCM_CGR0,
	 .enable_shift = MXC_CCM_CGR0_EPIT1_OFFSET,
	 .disable = _clk_disable,},
	{
	 .name = "epit_clk",
	 .id = 1,
	 .parent = &perclk_clk,
	 .enable = _clk_enable,
	 .enable_reg = MXC_CCM_CGR0,
	 .enable_shift = MXC_CCM_CGR0_EPIT2_OFFSET,
	 .disable = _clk_disable,},
};

static struct clk nfc_clk = {
	.name = "nfc_clk",
	.parent = &ahb_clk,
	.get_rate = _clk_nfc_get_rate,
};

static struct clk scc_clk = {
	.name = "scc_clk",
	.parent = &ipg_clk,
};

static struct clk ipu_clk = {
	.name = "ipu_clk",
	.parent = &mcu_main_clk,
	.get_rate = _clk_hsp_get_rate,
	.enable = _clk_enable,
	.enable_reg = MXC_CCM_CGR1,
	.enable_shift = MXC_CCM_CGR1_IPU_OFFSET,
	.disable = _clk_disable,
};

static struct clk kpp_clk = {
	.name = "kpp_clk",
	.parent = &ipg_clk,
	.enable = _clk_enable,
	.enable_reg = MXC_CCM_CGR1,
	.enable_shift = MXC_CCM_CGR1_KPP_OFFSET,
	.disable = _clk_disable,
};

static struct clk wdog_clk = {
	.name = "wdog_clk",
	.parent = &ipg_clk,
	.enable = _clk_enable,
	.enable_reg = MXC_CCM_CGR1,
	.enable_shift = MXC_CCM_CGR1_WDOG_OFFSET,
	.disable = _clk_disable,
};
static struct clk rtc_clk = {
	.name = "rtc_clk",
	.parent = &ipg_clk,
	.enable = _clk_enable,
	.enable_reg = MXC_CCM_CGR1,
	.enable_shift = MXC_CCM_CGR1_RTC_OFFSET,
	.disable = _clk_disable,
};

static struct clk usb_clk[] = {
	{
	 .name = "usb_clk",
	 .parent = &usb_pll_clk,
	 .get_rate = _clk_usb_get_rate,},
	{
	 .name = "usb_ahb_clk",
	 .parent = &ahb_clk,
	 .enable = _clk_enable,
	 .enable_reg = MXC_CCM_CGR1,
	 .enable_shift = MXC_CCM_CGR1_USBOTG_OFFSET,
	 .disable = _clk_disable,},
};

static struct clk csi_clk = {
	.name = "csi_clk",
	.parent = &serial_pll_clk,
	.get_rate = _clk_csi_get_rate,
	.round_rate = _clk_csi_round_rate,
	.set_rate = _clk_csi_set_rate,
	.enable = _clk_enable,
	.enable_reg = MXC_CCM_CGR1,
	.enable_shift = MXC_CCM_CGR1_CSI_OFFSET,
	.disable = _clk_disable,
};

static struct clk uart_clk[] = {
	{
	 .name = "uart_clk",
	 .id = 0,
	 .parent = &perclk_clk,
	 .enable = _clk_enable,
	 .enable_reg = MXC_CCM_CGR0,
	 .enable_shift = MXC_CCM_CGR0_UART1_OFFSET,
	 .disable = _clk_disable,},
	{
	 .name = "uart_clk",
	 .id = 1,
	 .parent = &perclk_clk,
	 .enable = _clk_enable,
	 .enable_reg = MXC_CCM_CGR0,
	 .enable_shift = MXC_CCM_CGR0_UART2_OFFSET,
	 .disable = _clk_disable,},
	{
	 .name = "uart_clk",
	 .id = 2,
	 .parent = &perclk_clk,
	 .enable = _clk_enable,
	 .enable_reg = MXC_CCM_CGR1,
	 .enable_shift = MXC_CCM_CGR1_UART3_OFFSET,
	 .disable = _clk_disable,},
	{
	 .name = "uart_clk",
	 .id = 3,
	 .parent = &perclk_clk,
	 .enable = _clk_enable,
	 .enable_reg = MXC_CCM_CGR1,
	 .enable_shift = MXC_CCM_CGR1_UART4_OFFSET,
	 .disable = _clk_disable,},
	{
	 .name = "uart_clk",
	 .id = 4,
	 .parent = &perclk_clk,
	 .enable = _clk_enable,
	 .enable_reg = MXC_CCM_CGR1,
	 .enable_shift = MXC_CCM_CGR1_UART5_OFFSET,
	 .disable = _clk_disable,},
};

static struct clk i2c_clk[] = {
	{
	 .name = "i2c_clk",
	 .id = 0,
	 .parent = &perclk_clk,
	 .enable = _clk_enable,
	 .enable_reg = MXC_CCM_CGR0,
	 .enable_shift = MXC_CCM_CGR0_I2C1_OFFSET,
	 .disable = _clk_disable,},
	{
	 .name = "i2c_clk",
	 .id = 1,
	 .parent = &perclk_clk,
	 .enable = _clk_enable,
	 .enable_reg = MXC_CCM_CGR0,
	 .enable_shift = MXC_CCM_CGR0_I2C2_OFFSET,
	 .disable = _clk_disable,},
	{
	 .name = "i2c_clk",
	 .id = 2,
	 .parent = &perclk_clk,
	 .enable = _clk_enable,
	 .enable_reg = MXC_CCM_CGR0,
	 .enable_shift = MXC_CCM_CGR0_I2C3_OFFSET,
	 .disable = _clk_disable,},
};

static struct clk owire_clk = {
	.name = "owire_clk",
	.parent = &perclk_clk,
	.enable_reg = MXC_CCM_CGR1,
	.enable_shift = MXC_CCM_CGR1_OWIRE_OFFSET,
	.enable = _clk_enable,
	.disable = _clk_disable,
};

static struct clk sdhc_clk[] = {
	{
	 .name = "sdhc_clk",
	 .id = 0,
	 .parent = &perclk_clk,
	 .enable = _clk_enable,
	 .enable_reg = MXC_CCM_CGR0,
	 .enable_shift = MXC_CCM_CGR0_SD_MMC1_OFFSET,
	 .disable = _clk_disable,},
	{
	 .name = "sdhc_clk",
	 .id = 1,
	 .parent = &perclk_clk,
	 .enable = _clk_enable,
	 .enable_reg = MXC_CCM_CGR0,
	 .enable_shift = MXC_CCM_CGR0_SD_MMC2_OFFSET,
	 .disable = _clk_disable,},
};

static struct clk ssi_clk[] = {
	{
	 .name = "ssi_clk",
	 .parent = &serial_pll_clk,
	 .get_rate = _clk_ssi1_get_rate,
	 .enable = _clk_enable,
	 .enable_reg = MXC_CCM_CGR0,
	 .enable_shift = MXC_CCM_CGR0_SSI1_OFFSET,
	 .disable = _clk_disable,},
	{
	 .name = "ssi_clk",
	 .id = 1,
	 .parent = &serial_pll_clk,
	 .get_rate = _clk_ssi2_get_rate,
	 .enable = _clk_enable,
	 .enable_reg = MXC_CCM_CGR2,
	 .enable_shift = MXC_CCM_CGR2_SSI2_OFFSET,
	 .disable = _clk_disable,},
};

static struct clk firi_clk = {
	.name = "firi_clk",
	.parent = &usb_pll_clk,
	.round_rate = _clk_firi_round_rate,
	.set_rate = _clk_firi_set_rate,
	.get_rate = _clk_firi_get_rate,
	.enable = _clk_enable,
	.enable_reg = MXC_CCM_CGR2,
	.enable_shift = MXC_CCM_CGR2_FIRI_OFFSET,
	.disable = _clk_disable,
};

static struct clk ata_clk = {
	.name = "ata_clk",
	.parent = &ipg_clk,
	.enable = _clk_enable,
	.enable_reg = MXC_CCM_CGR0,
	.enable_shift = MXC_CCM_CGR0_ATA_OFFSET,
	.disable = _clk_disable,
};

static struct clk mbx_clk = {
	.name = "mbx_clk",
	.parent = &ahb_clk,
	.enable = _clk_enable,
	.enable_reg = MXC_CCM_CGR2,
	.enable_shift = MXC_CCM_CGR2_GACC_OFFSET,
	.get_rate = _clk_mbx_get_rate,
};

static struct clk vpu_clk = {
	.name = "vpu_clk",
	.parent = &ahb_clk,
	.enable = _clk_enable,
	.enable_reg = MXC_CCM_CGR2,
	.enable_shift = MXC_CCM_CGR2_GACC_OFFSET,
	.get_rate = _clk_mbx_get_rate,
};

static struct clk rtic_clk = {
	.name = "rtic_clk",
	.parent = &ahb_clk,
	.enable = _clk_enable,
	.enable_reg = MXC_CCM_CGR2,
	.enable_shift = MXC_CCM_CGR2_RTIC_OFFSET,
	.disable = _clk_disable,
};

static struct clk rng_clk = {
	.name = "rng_clk",
	.parent = &ipg_clk,
	.enable = _clk_enable,
	.enable_reg = MXC_CCM_CGR0,
	.enable_shift = MXC_CCM_CGR0_RNG_OFFSET,
	.disable = _clk_disable,
};

static struct clk sdma_clk[] = {
	{
	 .name = "sdma_ahb_clk",
	 .parent = &ahb_clk,
	 .enable = _clk_enable,
	 .enable_reg = MXC_CCM_CGR0,
	 .enable_shift = MXC_CCM_CGR0_SDMA_OFFSET,
	 .disable = _clk_disable,},
	{
	 .name = "sdma_ipg_clk",
	 .parent = &ipg_clk,}
};

static struct clk mpeg4_clk = {
	.name = "mpeg4_clk",
	.parent = &ahb_clk,
	.enable = _clk_enable,
	.enable_reg = MXC_CCM_CGR1,
	.enable_shift = MXC_CCM_CGR1_HANTRO_OFFSET,
	.disable = _clk_disable,
};

static struct clk vl2cc_clk = {
	.name = "vl2cc_clk",
	.parent = &ahb_clk,
	.enable = _clk_enable,
	.enable_reg = MXC_CCM_CGR1,
	.enable_shift = MXC_CCM_CGR1_HANTRO_OFFSET,
	.disable = _clk_disable,
};

static struct clk mstick_clk[] = {
	{
	 .name = "mstick_clk",
	 .id = 0,
	 .parent = &usb_pll_clk,
	 .get_rate = _clk_mstick1_get_rate,
	 .enable = _clk_enable,
	 .enable_reg = MXC_CCM_CGR1,
	 .enable_shift = MXC_CCM_CGR1_MEMSTICK1_OFFSET,
	 .disable = _clk_disable,},
	{
	 .name = "mstick_clk",
	 .id = 1,
	 .parent = &usb_pll_clk,
	 .get_rate = _clk_mstick2_get_rate,
	 .enable = _clk_enable,
	 .enable_reg = MXC_CCM_CGR1,
	 .enable_shift = MXC_CCM_CGR1_MEMSTICK2_OFFSET,
	 .disable = _clk_disable,},
};

static struct clk iim_clk = {
	.name = "iim_clk",
	.parent = &ipg_clk,
	.enable = _clk_enable,
	.enable_reg = MXC_CCM_CGR0,
	.enable_shift = MXC_CCM_CGR0_IIM_OFFSET,
	.disable = _clk_disable,
};

static unsigned long _clk_cko1_round_rate(struct clk *clk, unsigned long rate)
{
	u32 div, parent = clk_get_rate(clk->parent);

	div = parent / rate;
	if (parent % rate)
		div++;

	if (div > 8)
		div = 16;
	else if (div > 4)
		div = 8;
	else if (div > 2)
		div = 4;

	return parent / div;
}

static int _clk_cko1_set_rate(struct clk *clk, unsigned long rate)
{
	u32 reg, div, parent = clk_get_rate(clk->parent);

	div = parent / rate;

	if (div == 16)
		div = 4;
	else if (div == 8)
		div = 3;
	else if (div == 4)
		div = 2;
	else if (div == 2)
		div = 1;
	else if (div == 1)
		div = 0;
	else
		return -EINVAL;

	reg = __raw_readl(MXC_CCM_COSR) & ~MXC_CCM_COSR_CLKOUTDIV_MASK;
	reg |= div << MXC_CCM_COSR_CLKOUTDIV_OFFSET;
	__raw_writel(reg, MXC_CCM_COSR);

	return 0;
}

static unsigned long _clk_cko1_get_rate(struct clk *clk)
{
	u32 div;

	div = __raw_readl(MXC_CCM_COSR) & MXC_CCM_COSR_CLKOUTDIV_MASK >>
	    MXC_CCM_COSR_CLKOUTDIV_OFFSET;

	return clk_get_rate(clk->parent) / (1 << div);
}

static int _clk_cko1_set_parent(struct clk *clk, struct clk *parent)
{
	u32 reg;

	reg = __raw_readl(MXC_CCM_COSR) & ~MXC_CCM_COSR_CLKOSEL_MASK;

	if (parent == &mcu_main_clk)
		reg |= 0 << MXC_CCM_COSR_CLKOSEL_OFFSET;
	else if (parent == &ipg_clk)
		reg |= 1 << MXC_CCM_COSR_CLKOSEL_OFFSET;
	else if (parent == &usb_pll_clk)
		reg |= 2 << MXC_CCM_COSR_CLKOSEL_OFFSET;
	else if (parent == mcu_main_clk.parent)
		reg |= 3 << MXC_CCM_COSR_CLKOSEL_OFFSET;
	else if (parent == &ahb_clk)
		reg |= 5 << MXC_CCM_COSR_CLKOSEL_OFFSET;
	else if (parent == &serial_pll_clk)
		reg |= 7 << MXC_CCM_COSR_CLKOSEL_OFFSET;
	else if (parent == &ckih_clk)
		reg |= 8 << MXC_CCM_COSR_CLKOSEL_OFFSET;
	else if (parent == &emi_clk)
		reg |= 9 << MXC_CCM_COSR_CLKOSEL_OFFSET;
	else if (parent == &ipu_clk)
		reg |= 0xA << MXC_CCM_COSR_CLKOSEL_OFFSET;
	else if (parent == &nfc_clk)
		reg |= 0xB << MXC_CCM_COSR_CLKOSEL_OFFSET;
	else if (parent == &uart_clk[0])
		reg |= 0xC << MXC_CCM_COSR_CLKOSEL_OFFSET;
	else
		return -EINVAL;

	__raw_writel(reg, MXC_CCM_COSR);

	return 0;
}

static int _clk_cko1_enable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(MXC_CCM_COSR) | MXC_CCM_COSR_CLKOEN;
	__raw_writel(reg, MXC_CCM_COSR);

	return 0;
}

static void _clk_cko1_disable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(MXC_CCM_COSR) & ~MXC_CCM_COSR_CLKOEN;
	__raw_writel(reg, MXC_CCM_COSR);
}

static struct clk cko1_clk = {
	.name = "cko1_clk",
	.get_rate = _clk_cko1_get_rate,
	.set_rate = _clk_cko1_set_rate,
	.round_rate = _clk_cko1_round_rate,
	.set_parent = _clk_cko1_set_parent,
	.enable = _clk_cko1_enable,
	.disable = _clk_cko1_disable,
};

static struct clk *mxc_clks[] = {
	&ckih_clk,
	&ckil_clk,
	&mcu_pll_clk,
	&usb_pll_clk,
	&serial_pll_clk,
	&mcu_main_clk,
	&ahb_clk,
	&per_clk,
	&perclk_clk,
	&cko1_clk,
	&emi_clk,
	&cspi_clk[0],
	&cspi_clk[1],
	&cspi_clk[2],
	&ipg_clk,
	&gpt_clk,
	&pwm_clk,
	&wdog_clk,
	&rtc_clk,
	&epit_clk[0],
	&epit_clk[1],
	&nfc_clk,
	&ipu_clk,
	&kpp_clk,
	&usb_clk[0],
	&usb_clk[1],
	&csi_clk,
	&uart_clk[0],
	&uart_clk[1],
	&uart_clk[2],
	&uart_clk[3],
	&uart_clk[4],
	&i2c_clk[0],
	&i2c_clk[1],
	&i2c_clk[2],
	&owire_clk,
	&sdhc_clk[0],
	&sdhc_clk[1],
	&ssi_clk[0],
	&ssi_clk[1],
	&firi_clk,
	&ata_clk,
	&rtic_clk,
	&rng_clk,
	&sdma_clk[0],
	&sdma_clk[1],
	&mstick_clk[0],
	&mstick_clk[1],
	&scc_clk,
	&iim_clk,
};

int __init mxc_clocks_init(unsigned long fref)
{
	u32 reg;
	struct clk **clkp;

	ckih_rate = fref;

	for (clkp = mxc_clks; clkp < mxc_clks + ARRAY_SIZE(mxc_clks); clkp++)
		clk_register(*clkp);

	if (cpu_is_mx31()) {
		clk_register(&mpeg4_clk);
		clk_register(&mbx_clk);
	} else {
		clk_register(&vpu_clk);
		clk_register(&vl2cc_clk);
	}

	/* Turn off all possible clocks */
	__raw_writel(MXC_CCM_CGR0_GPT_MASK, MXC_CCM_CGR0);
	__raw_writel(0, MXC_CCM_CGR1);

	__raw_writel(MXC_CCM_CGR2_EMI_MASK |
		     MXC_CCM_CGR2_IPMUX1_MASK |
		     MXC_CCM_CGR2_IPMUX2_MASK |
		     MXC_CCM_CGR2_MXCCLKENSEL_MASK |	/* for MX32 */
		     MXC_CCM_CGR2_CHIKCAMPEN_MASK |	/* for MX32 */
		     MXC_CCM_CGR2_OVRVPUBUSY_MASK |	/* for MX32 */
		     1 << 27 | 1 << 28, /* Bit 27 and 28 are not defined for
					   MX32, but still required to be set */
		     MXC_CCM_CGR2);

	clk_disable(&cko1_clk);
	clk_disable(&usb_pll_clk);

	pr_info("Clock input source is %ld\n", clk_get_rate(&ckih_clk));

	clk_enable(&gpt_clk);
	clk_enable(&emi_clk);
	clk_enable(&iim_clk);

	clk_enable(&serial_pll_clk);

	if (mx31_revision() >= CHIP_REV_2_0) {
		reg = __raw_readl(MXC_CCM_PMCR1);
		/* No PLL restart on DVFS switch; enable auto EMI handshake */
		reg |= MXC_CCM_PMCR1_PLLRDIS | MXC_CCM_PMCR1_EMIRQ_EN;
		__raw_writel(reg, MXC_CCM_PMCR1);
	}

	return 0;
}

