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
#include <linux/clkdev.h>

#include <asm/div64.h>

#include <mach/clock.h>
#include <mach/hardware.h>
#include <mach/mx31.h>
#include <mach/common.h>

#include "crmregs-imx3.h"

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
static struct clk serial_pll_clk;
static struct clk ipg_clk;
static struct clk ckih_clk;

static int cgr_enable(struct clk *clk)
{
	u32 reg;

	if (!clk->enable_reg)
		return 0;

	reg = __raw_readl(clk->enable_reg);
	reg |= 3 << clk->enable_shift;
	__raw_writel(reg, clk->enable_reg);

	return 0;
}

static void cgr_disable(struct clk *clk)
{
	u32 reg;

	if (!clk->enable_reg)
		return;

	reg = __raw_readl(clk->enable_reg);
	reg &= ~(3 << clk->enable_shift);

	/* special case for EMI clock */
	if (clk->enable_reg == MXC_CCM_CGR2 && clk->enable_shift == 8)
		reg |= (1 << clk->enable_shift);

	__raw_writel(reg, clk->enable_reg);
}

static unsigned long pll_ref_get_rate(void)
{
	unsigned long ccmr;
	unsigned int prcs;

	ccmr = __raw_readl(MXC_CCM_CCMR);
	prcs = (ccmr & MXC_CCM_CCMR_PRCS_MASK) >> MXC_CCM_CCMR_PRCS_OFFSET;
	if (prcs == 0x1)
		return CKIL_CLK_FREQ * 1024;
	else
		return clk_get_rate(&ckih_clk);
}

static unsigned long usb_pll_get_rate(struct clk *clk)
{
	unsigned long reg;

	reg = __raw_readl(MXC_CCM_UPCTL);

	return mxc_decode_pll(reg, pll_ref_get_rate());
}

static unsigned long serial_pll_get_rate(struct clk *clk)
{
	unsigned long reg;

	reg = __raw_readl(MXC_CCM_SRPCTL);

	return mxc_decode_pll(reg, pll_ref_get_rate());
}

static unsigned long mcu_pll_get_rate(struct clk *clk)
{
	unsigned long reg, ccmr;

	ccmr = __raw_readl(MXC_CCM_CCMR);

	if (!(ccmr & MXC_CCM_CCMR_MPE) || (ccmr & MXC_CCM_CCMR_MDS))
		return clk_get_rate(&ckih_clk);

	reg = __raw_readl(MXC_CCM_MPCTL);

	return mxc_decode_pll(reg, pll_ref_get_rate());
}

static int usb_pll_enable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(MXC_CCM_CCMR);
	reg |= MXC_CCM_CCMR_UPE;
	__raw_writel(reg, MXC_CCM_CCMR);

	/* No lock bit on MX31, so using max time from spec */
	udelay(80);

	return 0;
}

static void usb_pll_disable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(MXC_CCM_CCMR);
	reg &= ~MXC_CCM_CCMR_UPE;
	__raw_writel(reg, MXC_CCM_CCMR);
}

static int serial_pll_enable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(MXC_CCM_CCMR);
	reg |= MXC_CCM_CCMR_SPE;
	__raw_writel(reg, MXC_CCM_CCMR);

	/* No lock bit on MX31, so using max time from spec */
	udelay(80);

	return 0;
}

static void serial_pll_disable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(MXC_CCM_CCMR);
	reg &= ~MXC_CCM_CCMR_SPE;
	__raw_writel(reg, MXC_CCM_CCMR);
}

#define PDR0(mask, off) ((__raw_readl(MXC_CCM_PDR0) & mask) >> off)
#define PDR1(mask, off) ((__raw_readl(MXC_CCM_PDR1) & mask) >> off)
#define PDR2(mask, off) ((__raw_readl(MXC_CCM_PDR2) & mask) >> off)

static unsigned long mcu_main_get_rate(struct clk *clk)
{
	u32 pmcr0 = __raw_readl(MXC_CCM_PMCR0);

	if ((pmcr0 & MXC_CCM_PMCR0_DFSUP1) == MXC_CCM_PMCR0_DFSUP1_SPLL)
		return clk_get_rate(&serial_pll_clk);
	else
		return clk_get_rate(&mcu_pll_clk);
}

static unsigned long ahb_get_rate(struct clk *clk)
{
	unsigned long max_pdf;

	max_pdf = PDR0(MXC_CCM_PDR0_MAX_PODF_MASK,
		       MXC_CCM_PDR0_MAX_PODF_OFFSET);
	return clk_get_rate(clk->parent) / (max_pdf + 1);
}

static unsigned long ipg_get_rate(struct clk *clk)
{
	unsigned long ipg_pdf;

	ipg_pdf = PDR0(MXC_CCM_PDR0_IPG_PODF_MASK,
		       MXC_CCM_PDR0_IPG_PODF_OFFSET);
	return clk_get_rate(clk->parent) / (ipg_pdf + 1);
}

static unsigned long nfc_get_rate(struct clk *clk)
{
	unsigned long nfc_pdf;

	nfc_pdf = PDR0(MXC_CCM_PDR0_NFC_PODF_MASK,
		       MXC_CCM_PDR0_NFC_PODF_OFFSET);
	return clk_get_rate(clk->parent) / (nfc_pdf + 1);
}

static unsigned long hsp_get_rate(struct clk *clk)
{
	unsigned long hsp_pdf;

	hsp_pdf = PDR0(MXC_CCM_PDR0_HSP_PODF_MASK,
		       MXC_CCM_PDR0_HSP_PODF_OFFSET);
	return clk_get_rate(clk->parent) / (hsp_pdf + 1);
}

static unsigned long usb_get_rate(struct clk *clk)
{
	unsigned long usb_pdf, usb_prepdf;

	usb_pdf = PDR1(MXC_CCM_PDR1_USB_PODF_MASK,
		       MXC_CCM_PDR1_USB_PODF_OFFSET);
	usb_prepdf = PDR1(MXC_CCM_PDR1_USB_PRDF_MASK,
			  MXC_CCM_PDR1_USB_PRDF_OFFSET);
	return clk_get_rate(clk->parent) / (usb_prepdf + 1) / (usb_pdf + 1);
}

static unsigned long csi_get_rate(struct clk *clk)
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

static unsigned long csi_round_rate(struct clk *clk, unsigned long rate)
{
	u32 pre, post, parent = clk_get_rate(clk->parent);
	u32 div = parent / rate;

	if (parent % rate)
		div++;

	__calc_pre_post_dividers(div, &pre, &post);

	return parent / (pre * post);
}

static int csi_set_rate(struct clk *clk, unsigned long rate)
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

static unsigned long ssi1_get_rate(struct clk *clk)
{
	unsigned long ssi1_pdf, ssi1_prepdf;

	ssi1_pdf = PDR1(MXC_CCM_PDR1_SSI1_PODF_MASK,
			MXC_CCM_PDR1_SSI1_PODF_OFFSET);
	ssi1_prepdf = PDR1(MXC_CCM_PDR1_SSI1_PRE_PODF_MASK,
			   MXC_CCM_PDR1_SSI1_PRE_PODF_OFFSET);
	return clk_get_rate(clk->parent) / (ssi1_prepdf + 1) / (ssi1_pdf + 1);
}

static unsigned long ssi2_get_rate(struct clk *clk)
{
	unsigned long ssi2_pdf, ssi2_prepdf;

	ssi2_pdf = PDR1(MXC_CCM_PDR1_SSI2_PODF_MASK,
			MXC_CCM_PDR1_SSI2_PODF_OFFSET);
	ssi2_prepdf = PDR1(MXC_CCM_PDR1_SSI2_PRE_PODF_MASK,
			   MXC_CCM_PDR1_SSI2_PRE_PODF_OFFSET);
	return clk_get_rate(clk->parent) / (ssi2_prepdf + 1) / (ssi2_pdf + 1);
}

static unsigned long firi_get_rate(struct clk *clk)
{
	unsigned long firi_pdf, firi_prepdf;

	firi_pdf = PDR1(MXC_CCM_PDR1_FIRI_PODF_MASK,
			MXC_CCM_PDR1_FIRI_PODF_OFFSET);
	firi_prepdf = PDR1(MXC_CCM_PDR1_FIRI_PRE_PODF_MASK,
			   MXC_CCM_PDR1_FIRI_PRE_PODF_OFFSET);
	return clk_get_rate(clk->parent) / (firi_prepdf + 1) / (firi_pdf + 1);
}

static unsigned long firi_round_rate(struct clk *clk, unsigned long rate)
{
	u32 pre, post;
	u32 parent = clk_get_rate(clk->parent);
	u32 div = parent / rate;

	if (parent % rate)
		div++;

	__calc_pre_post_dividers(div, &pre, &post);

	return parent / (pre * post);

}

static int firi_set_rate(struct clk *clk, unsigned long rate)
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

static unsigned long mbx_get_rate(struct clk *clk)
{
	return clk_get_rate(clk->parent) / 2;
}

static unsigned long mstick1_get_rate(struct clk *clk)
{
	unsigned long msti_pdf;

	msti_pdf = PDR2(MXC_CCM_PDR2_MST1_PDF_MASK,
			MXC_CCM_PDR2_MST1_PDF_OFFSET);
	return clk_get_rate(clk->parent) / (msti_pdf + 1);
}

static unsigned long mstick2_get_rate(struct clk *clk)
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

static unsigned long clk_ckil_get_rate(struct clk *clk)
{
	return CKIL_CLK_FREQ;
}

static struct clk ckih_clk = {
	.get_rate = clk_ckih_get_rate,
};

static struct clk mcu_pll_clk = {
	.parent = &ckih_clk,
	.get_rate = mcu_pll_get_rate,
};

static struct clk mcu_main_clk = {
	.parent = &mcu_pll_clk,
	.get_rate = mcu_main_get_rate,
};

static struct clk serial_pll_clk = {
	.parent = &ckih_clk,
	.get_rate = serial_pll_get_rate,
	.enable = serial_pll_enable,
	.disable = serial_pll_disable,
};

static struct clk usb_pll_clk = {
	.parent = &ckih_clk,
	.get_rate = usb_pll_get_rate,
	.enable = usb_pll_enable,
	.disable = usb_pll_disable,
};

static struct clk ahb_clk = {
	.parent = &mcu_main_clk,
	.get_rate = ahb_get_rate,
};

#define DEFINE_CLOCK(name, i, er, es, gr, s, p)		\
	static struct clk name = {			\
		.id		= i,			\
		.enable_reg	= er,			\
		.enable_shift	= es,			\
		.get_rate	= gr,			\
		.enable		= cgr_enable,		\
		.disable	= cgr_disable,		\
		.secondary	= s,			\
		.parent		= p,			\
	}

#define DEFINE_CLOCK1(name, i, er, es, getsetround, s, p)	\
	static struct clk name = {				\
		.id		= i,				\
		.enable_reg	= er,				\
		.enable_shift	= es,				\
		.get_rate	= getsetround##_get_rate,	\
		.set_rate	= getsetround##_set_rate,	\
		.round_rate	= getsetround##_round_rate,	\
		.enable		= cgr_enable,			\
		.disable	= cgr_disable,			\
		.secondary	= s,				\
		.parent		= p,				\
	}

DEFINE_CLOCK(perclk_clk,  0, NULL,          0, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(ckil_clk,    0, NULL,          0, clk_ckil_get_rate, NULL, NULL);

DEFINE_CLOCK(sdhc1_clk,   0, MXC_CCM_CGR0,  0, NULL, NULL, &perclk_clk);
DEFINE_CLOCK(sdhc2_clk,   1, MXC_CCM_CGR0,  2, NULL, NULL, &perclk_clk);
DEFINE_CLOCK(gpt_clk,     0, MXC_CCM_CGR0,  4, NULL, NULL, &perclk_clk);
DEFINE_CLOCK(epit1_clk,   0, MXC_CCM_CGR0,  6, NULL, NULL, &perclk_clk);
DEFINE_CLOCK(epit2_clk,   1, MXC_CCM_CGR0,  8, NULL, NULL, &perclk_clk);
DEFINE_CLOCK(iim_clk,     0, MXC_CCM_CGR0, 10, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(pata_clk,     0, MXC_CCM_CGR0, 12, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(sdma_clk1,   0, MXC_CCM_CGR0, 14, NULL, NULL, &ahb_clk);
DEFINE_CLOCK(cspi3_clk,   2, MXC_CCM_CGR0, 16, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(rng_clk,     0, MXC_CCM_CGR0, 18, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(uart1_clk,   0, MXC_CCM_CGR0, 20, NULL, NULL, &perclk_clk);
DEFINE_CLOCK(uart2_clk,   1, MXC_CCM_CGR0, 22, NULL, NULL, &perclk_clk);
DEFINE_CLOCK(ssi1_clk,    0, MXC_CCM_CGR0, 24, ssi1_get_rate, NULL, &serial_pll_clk);
DEFINE_CLOCK(i2c1_clk,    0, MXC_CCM_CGR0, 26, NULL, NULL, &perclk_clk);
DEFINE_CLOCK(i2c2_clk,    1, MXC_CCM_CGR0, 28, NULL, NULL, &perclk_clk);
DEFINE_CLOCK(i2c3_clk,    2, MXC_CCM_CGR0, 30, NULL, NULL, &perclk_clk);

DEFINE_CLOCK(mpeg4_clk,   0, MXC_CCM_CGR1,  0, NULL, NULL, &ahb_clk);
DEFINE_CLOCK(mstick1_clk, 0, MXC_CCM_CGR1,  2, mstick1_get_rate, NULL, &usb_pll_clk);
DEFINE_CLOCK(mstick2_clk, 1, MXC_CCM_CGR1,  4, mstick2_get_rate, NULL, &usb_pll_clk);
DEFINE_CLOCK1(csi_clk,    0, MXC_CCM_CGR1,  6, csi, NULL, &serial_pll_clk);
DEFINE_CLOCK(rtc_clk,     0, MXC_CCM_CGR1,  8, NULL, NULL, &ckil_clk);
DEFINE_CLOCK(wdog_clk,    0, MXC_CCM_CGR1, 10, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(pwm_clk,     0, MXC_CCM_CGR1, 12, NULL, NULL, &perclk_clk);
DEFINE_CLOCK(usb_clk2,    0, MXC_CCM_CGR1, 18, usb_get_rate, NULL, &ahb_clk);
DEFINE_CLOCK(kpp_clk,     0, MXC_CCM_CGR1, 20, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(ipu_clk,     0, MXC_CCM_CGR1, 22, hsp_get_rate, NULL, &mcu_main_clk);
DEFINE_CLOCK(uart3_clk,   2, MXC_CCM_CGR1, 24, NULL, NULL, &perclk_clk);
DEFINE_CLOCK(uart4_clk,   3, MXC_CCM_CGR1, 26, NULL, NULL, &perclk_clk);
DEFINE_CLOCK(uart5_clk,   4, MXC_CCM_CGR1, 28, NULL, NULL, &perclk_clk);
DEFINE_CLOCK(owire_clk,   0, MXC_CCM_CGR1, 30, NULL, NULL, &perclk_clk);

DEFINE_CLOCK(ssi2_clk,    1, MXC_CCM_CGR2,  0, ssi2_get_rate, NULL, &serial_pll_clk);
DEFINE_CLOCK(cspi1_clk,   0, MXC_CCM_CGR2,  2, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(cspi2_clk,   1, MXC_CCM_CGR2,  4, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(mbx_clk,     0, MXC_CCM_CGR2,  6, mbx_get_rate, NULL, &ahb_clk);
DEFINE_CLOCK(emi_clk,     0, MXC_CCM_CGR2,  8, NULL, NULL, &ahb_clk);
DEFINE_CLOCK(rtic_clk,    0, MXC_CCM_CGR2, 10, NULL, NULL, &ahb_clk);
DEFINE_CLOCK1(firi_clk,   0, MXC_CCM_CGR2, 12, firi, NULL, &usb_pll_clk);

DEFINE_CLOCK(sdma_clk2,   0, NULL,          0, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(usb_clk1,    0, NULL,          0, usb_get_rate, NULL, &usb_pll_clk);
DEFINE_CLOCK(nfc_clk,     0, NULL,          0, nfc_get_rate, NULL, &ahb_clk);
DEFINE_CLOCK(scc_clk,     0, NULL,          0, NULL, NULL, &ipg_clk);
DEFINE_CLOCK(ipg_clk,     0, NULL,          0, ipg_get_rate, NULL, &ahb_clk);

#define _REGISTER_CLOCK(d, n, c) \
	{ \
		.dev_id = d, \
		.con_id = n, \
		.clk = &c, \
	},

static struct clk_lookup lookups[] = {
	_REGISTER_CLOCK(NULL, "emi", emi_clk)
	_REGISTER_CLOCK("imx31-cspi.0", NULL, cspi1_clk)
	_REGISTER_CLOCK("imx31-cspi.1", NULL, cspi2_clk)
	_REGISTER_CLOCK("imx31-cspi.2", NULL, cspi3_clk)
	_REGISTER_CLOCK(NULL, "gpt", gpt_clk)
	_REGISTER_CLOCK(NULL, "pwm", pwm_clk)
	_REGISTER_CLOCK("imx2-wdt.0", NULL, wdog_clk)
	_REGISTER_CLOCK(NULL, "rtc", rtc_clk)
	_REGISTER_CLOCK(NULL, "epit", epit1_clk)
	_REGISTER_CLOCK(NULL, "epit", epit2_clk)
	_REGISTER_CLOCK("mxc_nand.0", NULL, nfc_clk)
	_REGISTER_CLOCK("ipu-core", NULL, ipu_clk)
	_REGISTER_CLOCK("mx3_sdc_fb", NULL, ipu_clk)
	_REGISTER_CLOCK(NULL, "kpp", kpp_clk)
	_REGISTER_CLOCK("mxc-ehci.0", "usb", usb_clk1)
	_REGISTER_CLOCK("mxc-ehci.0", "usb_ahb", usb_clk2)
	_REGISTER_CLOCK("mxc-ehci.1", "usb", usb_clk1)
	_REGISTER_CLOCK("mxc-ehci.1", "usb_ahb", usb_clk2)
	_REGISTER_CLOCK("mxc-ehci.2", "usb", usb_clk1)
	_REGISTER_CLOCK("mxc-ehci.2", "usb_ahb", usb_clk2)
	_REGISTER_CLOCK("fsl-usb2-udc", "usb", usb_clk1)
	_REGISTER_CLOCK("fsl-usb2-udc", "usb_ahb", usb_clk2)
	_REGISTER_CLOCK("mx3-camera.0", NULL, csi_clk)
	/* i.mx31 has the i.mx21 type uart */
	_REGISTER_CLOCK("imx21-uart.0", NULL, uart1_clk)
	_REGISTER_CLOCK("imx21-uart.1", NULL, uart2_clk)
	_REGISTER_CLOCK("imx21-uart.2", NULL, uart3_clk)
	_REGISTER_CLOCK("imx21-uart.3", NULL, uart4_clk)
	_REGISTER_CLOCK("imx21-uart.4", NULL, uart5_clk)
	_REGISTER_CLOCK("imx-i2c.0", NULL, i2c1_clk)
	_REGISTER_CLOCK("imx-i2c.1", NULL, i2c2_clk)
	_REGISTER_CLOCK("imx-i2c.2", NULL, i2c3_clk)
	_REGISTER_CLOCK("mxc_w1.0", NULL, owire_clk)
	_REGISTER_CLOCK("mxc-mmc.0", NULL, sdhc1_clk)
	_REGISTER_CLOCK("mxc-mmc.1", NULL, sdhc2_clk)
	_REGISTER_CLOCK("imx-ssi.0", NULL, ssi1_clk)
	_REGISTER_CLOCK("imx-ssi.1", NULL, ssi2_clk)
	_REGISTER_CLOCK(NULL, "firi", firi_clk)
	_REGISTER_CLOCK("pata_imx", NULL, pata_clk)
	_REGISTER_CLOCK(NULL, "rtic", rtic_clk)
	_REGISTER_CLOCK(NULL, "rng", rng_clk)
	_REGISTER_CLOCK("imx31-sdma", NULL, sdma_clk1)
	_REGISTER_CLOCK(NULL, "sdma_ipg", sdma_clk2)
	_REGISTER_CLOCK(NULL, "mstick", mstick1_clk)
	_REGISTER_CLOCK(NULL, "mstick", mstick2_clk)
	_REGISTER_CLOCK(NULL, "scc", scc_clk)
	_REGISTER_CLOCK(NULL, "iim", iim_clk)
	_REGISTER_CLOCK(NULL, "mpeg4", mpeg4_clk)
	_REGISTER_CLOCK(NULL, "mbx", mbx_clk)
};

int __init mx31_clocks_init(unsigned long fref)
{
	u32 reg;

	ckih_rate = fref;

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	/* change the csi_clk parent if necessary */
	reg = __raw_readl(MXC_CCM_CCMR);
	if (!(reg & MXC_CCM_CCMR_CSCS))
		if (clk_set_parent(&csi_clk, &usb_pll_clk))
			pr_err("%s: error changing csi_clk parent\n", __func__);


	/* Turn off all possible clocks */
	__raw_writel((3 << 4), MXC_CCM_CGR0);
	__raw_writel(0, MXC_CCM_CGR1);
	__raw_writel((3 << 8) | (3 << 14) | (3 << 16)|
		     1 << 27 | 1 << 28, /* Bit 27 and 28 are not defined for
					   MX32, but still required to be set */
		     MXC_CCM_CGR2);

	/*
	 * Before turning off usb_pll make sure ipg_per_clk is generated
	 * by ipg_clk and not usb_pll.
	 */
	__raw_writel(__raw_readl(MXC_CCM_CCMR) | (1 << 24), MXC_CCM_CCMR);

	usb_pll_disable(&usb_pll_clk);

	pr_info("Clock input source is %ld\n", clk_get_rate(&ckih_clk));

	clk_enable(&gpt_clk);
	clk_enable(&emi_clk);
	clk_enable(&iim_clk);
	mx31_revision();
	clk_disable(&iim_clk);

	clk_enable(&serial_pll_clk);

	if (mx31_revision() >= IMX_CHIP_REVISION_2_0) {
		reg = __raw_readl(MXC_CCM_PMCR1);
		/* No PLL restart on DVFS switch; enable auto EMI handshake */
		reg |= MXC_CCM_PMCR1_PLLRDIS | MXC_CCM_PMCR1_EMIRQ_EN;
		__raw_writel(reg, MXC_CCM_PMCR1);
	}

	mxc_timer_init(&ipg_clk, MX31_IO_ADDRESS(MX31_GPT1_BASE_ADDR),
			MX31_INT_GPT);

	return 0;
}
