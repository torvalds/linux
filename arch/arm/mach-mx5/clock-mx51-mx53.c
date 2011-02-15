/*
 * Copyright 2008-2010 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2009-2010 Amit Kucheria <amit.kucheria@canonical.com>
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/clkdev.h>

#include <asm/div64.h>

#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/clock.h>

#include "crm_regs.h"

/* External clock values passed-in by the board code */
static unsigned long external_high_reference, external_low_reference;
static unsigned long oscillator_reference, ckih2_reference;

static struct clk osc_clk;
static struct clk pll1_main_clk;
static struct clk pll1_sw_clk;
static struct clk pll2_sw_clk;
static struct clk pll3_sw_clk;
static struct clk mx53_pll4_sw_clk;
static struct clk lp_apm_clk;
static struct clk periph_apm_clk;
static struct clk ahb_clk;
static struct clk ipg_clk;
static struct clk usboh3_clk;
static struct clk emi_fast_clk;
static struct clk ipu_clk;
static struct clk mipi_hsc1_clk;

#define MAX_DPLL_WAIT_TRIES	1000 /* 1000 * udelay(1) = 1ms */

/* calculate best pre and post dividers to get the required divider */
static void __calc_pre_post_dividers(u32 div, u32 *pre, u32 *post,
	u32 max_pre, u32 max_post)
{
	if (div >= max_pre * max_post) {
		*pre = max_pre;
		*post = max_post;
	} else if (div >= max_pre) {
		u32 min_pre, temp_pre, old_err, err;
		min_pre = DIV_ROUND_UP(div, max_post);
		old_err = max_pre;
		for (temp_pre = max_pre; temp_pre >= min_pre; temp_pre--) {
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
		*post = DIV_ROUND_UP(div, *pre);
	} else {
		*pre = div;
		*post = 1;
	}
}

static void _clk_ccgr_setclk(struct clk *clk, unsigned mode)
{
	u32 reg = __raw_readl(clk->enable_reg);

	reg &= ~(MXC_CCM_CCGRx_CG_MASK << clk->enable_shift);
	reg |= mode << clk->enable_shift;

	__raw_writel(reg, clk->enable_reg);
}

static int _clk_ccgr_enable(struct clk *clk)
{
	_clk_ccgr_setclk(clk, MXC_CCM_CCGRx_MOD_ON);
	return 0;
}

static void _clk_ccgr_disable(struct clk *clk)
{
	_clk_ccgr_setclk(clk, MXC_CCM_CCGRx_MOD_OFF);
}

static int _clk_ccgr_enable_inrun(struct clk *clk)
{
	_clk_ccgr_setclk(clk, MXC_CCM_CCGRx_MOD_IDLE);
	return 0;
}

static void _clk_ccgr_disable_inwait(struct clk *clk)
{
	_clk_ccgr_setclk(clk, MXC_CCM_CCGRx_MOD_IDLE);
}

/*
 * For the 4-to-1 muxed input clock
 */
static inline u32 _get_mux(struct clk *parent, struct clk *m0,
			   struct clk *m1, struct clk *m2, struct clk *m3)
{
	if (parent == m0)
		return 0;
	else if (parent == m1)
		return 1;
	else if (parent == m2)
		return 2;
	else if (parent == m3)
		return 3;
	else
		BUG();

	return -EINVAL;
}

static inline void __iomem *_mx51_get_pll_base(struct clk *pll)
{
	if (pll == &pll1_main_clk)
		return MX51_DPLL1_BASE;
	else if (pll == &pll2_sw_clk)
		return MX51_DPLL2_BASE;
	else if (pll == &pll3_sw_clk)
		return MX51_DPLL3_BASE;
	else
		BUG();

	return NULL;
}

static inline void __iomem *_mx53_get_pll_base(struct clk *pll)
{
	if (pll == &pll1_main_clk)
		return MX53_DPLL1_BASE;
	else if (pll == &pll2_sw_clk)
		return MX53_DPLL2_BASE;
	else if (pll == &pll3_sw_clk)
		return MX53_DPLL3_BASE;
	else if (pll == &mx53_pll4_sw_clk)
		return MX53_DPLL4_BASE;
	else
		BUG();

	return NULL;
}

static inline void __iomem *_get_pll_base(struct clk *pll)
{
	if (cpu_is_mx51())
		return _mx51_get_pll_base(pll);
	else
		return _mx53_get_pll_base(pll);
}

static unsigned long clk_pll_get_rate(struct clk *clk)
{
	long mfi, mfn, mfd, pdf, ref_clk, mfn_abs;
	unsigned long dp_op, dp_mfd, dp_mfn, dp_ctl, pll_hfsm, dbl;
	void __iomem *pllbase;
	s64 temp;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	pllbase = _get_pll_base(clk);

	dp_ctl = __raw_readl(pllbase + MXC_PLL_DP_CTL);
	pll_hfsm = dp_ctl & MXC_PLL_DP_CTL_HFSM;
	dbl = dp_ctl & MXC_PLL_DP_CTL_DPDCK0_2_EN;

	if (pll_hfsm == 0) {
		dp_op = __raw_readl(pllbase + MXC_PLL_DP_OP);
		dp_mfd = __raw_readl(pllbase + MXC_PLL_DP_MFD);
		dp_mfn = __raw_readl(pllbase + MXC_PLL_DP_MFN);
	} else {
		dp_op = __raw_readl(pllbase + MXC_PLL_DP_HFS_OP);
		dp_mfd = __raw_readl(pllbase + MXC_PLL_DP_HFS_MFD);
		dp_mfn = __raw_readl(pllbase + MXC_PLL_DP_HFS_MFN);
	}
	pdf = dp_op & MXC_PLL_DP_OP_PDF_MASK;
	mfi = (dp_op & MXC_PLL_DP_OP_MFI_MASK) >> MXC_PLL_DP_OP_MFI_OFFSET;
	mfi = (mfi <= 5) ? 5 : mfi;
	mfd = dp_mfd & MXC_PLL_DP_MFD_MASK;
	mfn = mfn_abs = dp_mfn & MXC_PLL_DP_MFN_MASK;
	/* Sign extend to 32-bits */
	if (mfn >= 0x04000000) {
		mfn |= 0xFC000000;
		mfn_abs = -mfn;
	}

	ref_clk = 2 * parent_rate;
	if (dbl != 0)
		ref_clk *= 2;

	ref_clk /= (pdf + 1);
	temp = (u64) ref_clk * mfn_abs;
	do_div(temp, mfd + 1);
	if (mfn < 0)
		temp = -temp;
	temp = (ref_clk * mfi) + temp;

	return temp;
}

static int _clk_pll_set_rate(struct clk *clk, unsigned long rate)
{
	u32 reg;
	void __iomem *pllbase;

	long mfi, pdf, mfn, mfd = 999999;
	s64 temp64;
	unsigned long quad_parent_rate;
	unsigned long pll_hfsm, dp_ctl;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	pllbase = _get_pll_base(clk);

	quad_parent_rate = 4 * parent_rate;
	pdf = mfi = -1;
	while (++pdf < 16 && mfi < 5)
		mfi = rate * (pdf+1) / quad_parent_rate;
	if (mfi > 15)
		return -EINVAL;
	pdf--;

	temp64 = rate * (pdf+1) - quad_parent_rate * mfi;
	do_div(temp64, quad_parent_rate/1000000);
	mfn = (long)temp64;

	dp_ctl = __raw_readl(pllbase + MXC_PLL_DP_CTL);
	/* use dpdck0_2 */
	__raw_writel(dp_ctl | 0x1000L, pllbase + MXC_PLL_DP_CTL);
	pll_hfsm = dp_ctl & MXC_PLL_DP_CTL_HFSM;
	if (pll_hfsm == 0) {
		reg = mfi << 4 | pdf;
		__raw_writel(reg, pllbase + MXC_PLL_DP_OP);
		__raw_writel(mfd, pllbase + MXC_PLL_DP_MFD);
		__raw_writel(mfn, pllbase + MXC_PLL_DP_MFN);
	} else {
		reg = mfi << 4 | pdf;
		__raw_writel(reg, pllbase + MXC_PLL_DP_HFS_OP);
		__raw_writel(mfd, pllbase + MXC_PLL_DP_HFS_MFD);
		__raw_writel(mfn, pllbase + MXC_PLL_DP_HFS_MFN);
	}

	return 0;
}

static int _clk_pll_enable(struct clk *clk)
{
	u32 reg;
	void __iomem *pllbase;
	int i = 0;

	pllbase = _get_pll_base(clk);
	reg = __raw_readl(pllbase + MXC_PLL_DP_CTL) | MXC_PLL_DP_CTL_UPEN;
	__raw_writel(reg, pllbase + MXC_PLL_DP_CTL);

	/* Wait for lock */
	do {
		reg = __raw_readl(pllbase + MXC_PLL_DP_CTL);
		if (reg & MXC_PLL_DP_CTL_LRF)
			break;

		udelay(1);
	} while (++i < MAX_DPLL_WAIT_TRIES);

	if (i == MAX_DPLL_WAIT_TRIES) {
		pr_err("MX5: pll locking failed\n");
		return -EINVAL;
	}

	return 0;
}

static void _clk_pll_disable(struct clk *clk)
{
	u32 reg;
	void __iomem *pllbase;

	pllbase = _get_pll_base(clk);
	reg = __raw_readl(pllbase + MXC_PLL_DP_CTL) & ~MXC_PLL_DP_CTL_UPEN;
	__raw_writel(reg, pllbase + MXC_PLL_DP_CTL);
}

static int _clk_pll1_sw_set_parent(struct clk *clk, struct clk *parent)
{
	u32 reg, step;

	reg = __raw_readl(MXC_CCM_CCSR);

	/* When switching from pll_main_clk to a bypass clock, first select a
	 * multiplexed clock in 'step_sel', then shift the glitchless mux
	 * 'pll1_sw_clk_sel'.
	 *
	 * When switching back, do it in reverse order
	 */
	if (parent == &pll1_main_clk) {
		/* Switch to pll1_main_clk */
		reg &= ~MXC_CCM_CCSR_PLL1_SW_CLK_SEL;
		__raw_writel(reg, MXC_CCM_CCSR);
		/* step_clk mux switched to lp_apm, to save power. */
		reg = __raw_readl(MXC_CCM_CCSR);
		reg &= ~MXC_CCM_CCSR_STEP_SEL_MASK;
		reg |= (MXC_CCM_CCSR_STEP_SEL_LP_APM <<
				MXC_CCM_CCSR_STEP_SEL_OFFSET);
	} else {
		if (parent == &lp_apm_clk) {
			step = MXC_CCM_CCSR_STEP_SEL_LP_APM;
		} else  if (parent == &pll2_sw_clk) {
			step = MXC_CCM_CCSR_STEP_SEL_PLL2_DIVIDED;
		} else  if (parent == &pll3_sw_clk) {
			step = MXC_CCM_CCSR_STEP_SEL_PLL3_DIVIDED;
		} else
			return -EINVAL;

		reg &= ~MXC_CCM_CCSR_STEP_SEL_MASK;
		reg |= (step << MXC_CCM_CCSR_STEP_SEL_OFFSET);

		__raw_writel(reg, MXC_CCM_CCSR);
		/* Switch to step_clk */
		reg = __raw_readl(MXC_CCM_CCSR);
		reg |= MXC_CCM_CCSR_PLL1_SW_CLK_SEL;
	}
	__raw_writel(reg, MXC_CCM_CCSR);
	return 0;
}

static unsigned long clk_pll1_sw_get_rate(struct clk *clk)
{
	u32 reg, div;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	reg = __raw_readl(MXC_CCM_CCSR);

	if (clk->parent == &pll2_sw_clk) {
		div = ((reg & MXC_CCM_CCSR_PLL2_PODF_MASK) >>
		       MXC_CCM_CCSR_PLL2_PODF_OFFSET) + 1;
	} else if (clk->parent == &pll3_sw_clk) {
		div = ((reg & MXC_CCM_CCSR_PLL3_PODF_MASK) >>
		       MXC_CCM_CCSR_PLL3_PODF_OFFSET) + 1;
	} else
		div = 1;
	return parent_rate / div;
}

static int _clk_pll2_sw_set_parent(struct clk *clk, struct clk *parent)
{
	u32 reg;

	reg = __raw_readl(MXC_CCM_CCSR);

	if (parent == &pll2_sw_clk)
		reg &= ~MXC_CCM_CCSR_PLL2_SW_CLK_SEL;
	else
		reg |= MXC_CCM_CCSR_PLL2_SW_CLK_SEL;

	__raw_writel(reg, MXC_CCM_CCSR);
	return 0;
}

static int _clk_lp_apm_set_parent(struct clk *clk, struct clk *parent)
{
	u32 reg;

	if (parent == &osc_clk)
		reg = __raw_readl(MXC_CCM_CCSR) & ~MXC_CCM_CCSR_LP_APM_SEL;
	else
		return -EINVAL;

	__raw_writel(reg, MXC_CCM_CCSR);

	return 0;
}

static unsigned long clk_cpu_get_rate(struct clk *clk)
{
	u32 cacrr, div;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);
	cacrr = __raw_readl(MXC_CCM_CACRR);
	div = (cacrr & MXC_CCM_CACRR_ARM_PODF_MASK) + 1;

	return parent_rate / div;
}

static int clk_cpu_set_rate(struct clk *clk, unsigned long rate)
{
	u32 reg, cpu_podf;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);
	cpu_podf = parent_rate / rate - 1;
	/* use post divider to change freq */
	reg = __raw_readl(MXC_CCM_CACRR);
	reg &= ~MXC_CCM_CACRR_ARM_PODF_MASK;
	reg |= cpu_podf << MXC_CCM_CACRR_ARM_PODF_OFFSET;
	__raw_writel(reg, MXC_CCM_CACRR);

	return 0;
}

static int _clk_periph_apm_set_parent(struct clk *clk, struct clk *parent)
{
	u32 reg, mux;
	int i = 0;

	mux = _get_mux(parent, &pll1_sw_clk, &pll3_sw_clk, &lp_apm_clk, NULL);

	reg = __raw_readl(MXC_CCM_CBCMR) & ~MXC_CCM_CBCMR_PERIPH_CLK_SEL_MASK;
	reg |= mux << MXC_CCM_CBCMR_PERIPH_CLK_SEL_OFFSET;
	__raw_writel(reg, MXC_CCM_CBCMR);

	/* Wait for lock */
	do {
		reg = __raw_readl(MXC_CCM_CDHIPR);
		if (!(reg &  MXC_CCM_CDHIPR_PERIPH_CLK_SEL_BUSY))
			break;

		udelay(1);
	} while (++i < MAX_DPLL_WAIT_TRIES);

	if (i == MAX_DPLL_WAIT_TRIES) {
		pr_err("MX5: Set parent for periph_apm clock failed\n");
		return -EINVAL;
	}

	return 0;
}

static int _clk_main_bus_set_parent(struct clk *clk, struct clk *parent)
{
	u32 reg;

	reg = __raw_readl(MXC_CCM_CBCDR);

	if (parent == &pll2_sw_clk)
		reg &= ~MXC_CCM_CBCDR_PERIPH_CLK_SEL;
	else if (parent == &periph_apm_clk)
		reg |= MXC_CCM_CBCDR_PERIPH_CLK_SEL;
	else
		return -EINVAL;

	__raw_writel(reg, MXC_CCM_CBCDR);

	return 0;
}

static struct clk main_bus_clk = {
	.parent = &pll2_sw_clk,
	.set_parent = _clk_main_bus_set_parent,
};

static unsigned long clk_ahb_get_rate(struct clk *clk)
{
	u32 reg, div;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	reg = __raw_readl(MXC_CCM_CBCDR);
	div = ((reg & MXC_CCM_CBCDR_AHB_PODF_MASK) >>
	       MXC_CCM_CBCDR_AHB_PODF_OFFSET) + 1;
	return parent_rate / div;
}


static int _clk_ahb_set_rate(struct clk *clk, unsigned long rate)
{
	u32 reg, div;
	unsigned long parent_rate;
	int i = 0;

	parent_rate = clk_get_rate(clk->parent);

	div = parent_rate / rate;
	if (div > 8 || div < 1 || ((parent_rate / div) != rate))
		return -EINVAL;

	reg = __raw_readl(MXC_CCM_CBCDR);
	reg &= ~MXC_CCM_CBCDR_AHB_PODF_MASK;
	reg |= (div - 1) << MXC_CCM_CBCDR_AHB_PODF_OFFSET;
	__raw_writel(reg, MXC_CCM_CBCDR);

	/* Wait for lock */
	do {
		reg = __raw_readl(MXC_CCM_CDHIPR);
		if (!(reg & MXC_CCM_CDHIPR_AHB_PODF_BUSY))
			break;

		udelay(1);
	} while (++i < MAX_DPLL_WAIT_TRIES);

	if (i == MAX_DPLL_WAIT_TRIES) {
		pr_err("MX5: clk_ahb_set_rate failed\n");
		return -EINVAL;
	}

	return 0;
}

static unsigned long _clk_ahb_round_rate(struct clk *clk,
						unsigned long rate)
{
	u32 div;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	div = parent_rate / rate;
	if (div > 8)
		div = 8;
	else if (div == 0)
		div++;
	return parent_rate / div;
}


static int _clk_max_enable(struct clk *clk)
{
	u32 reg;

	_clk_ccgr_enable(clk);

	/* Handshake with MAX when LPM is entered. */
	reg = __raw_readl(MXC_CCM_CLPCR);
	if (cpu_is_mx51())
		reg &= ~MX51_CCM_CLPCR_BYPASS_MAX_LPM_HS;
	else if (cpu_is_mx53())
		reg &= ~MX53_CCM_CLPCR_BYPASS_MAX_LPM_HS;
	__raw_writel(reg, MXC_CCM_CLPCR);

	return 0;
}

static void _clk_max_disable(struct clk *clk)
{
	u32 reg;

	_clk_ccgr_disable_inwait(clk);

	/* No Handshake with MAX when LPM is entered as its disabled. */
	reg = __raw_readl(MXC_CCM_CLPCR);
	if (cpu_is_mx51())
		reg |= MX51_CCM_CLPCR_BYPASS_MAX_LPM_HS;
	else if (cpu_is_mx53())
		reg &= ~MX53_CCM_CLPCR_BYPASS_MAX_LPM_HS;
	__raw_writel(reg, MXC_CCM_CLPCR);
}

static unsigned long clk_ipg_get_rate(struct clk *clk)
{
	u32 reg, div;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	reg = __raw_readl(MXC_CCM_CBCDR);
	div = ((reg & MXC_CCM_CBCDR_IPG_PODF_MASK) >>
	       MXC_CCM_CBCDR_IPG_PODF_OFFSET) + 1;

	return parent_rate / div;
}

static unsigned long clk_ipg_per_get_rate(struct clk *clk)
{
	u32 reg, prediv1, prediv2, podf;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	if (clk->parent == &main_bus_clk || clk->parent == &lp_apm_clk) {
		/* the main_bus_clk is the one before the DVFS engine */
		reg = __raw_readl(MXC_CCM_CBCDR);
		prediv1 = ((reg & MXC_CCM_CBCDR_PERCLK_PRED1_MASK) >>
			   MXC_CCM_CBCDR_PERCLK_PRED1_OFFSET) + 1;
		prediv2 = ((reg & MXC_CCM_CBCDR_PERCLK_PRED2_MASK) >>
			   MXC_CCM_CBCDR_PERCLK_PRED2_OFFSET) + 1;
		podf = ((reg & MXC_CCM_CBCDR_PERCLK_PODF_MASK) >>
			MXC_CCM_CBCDR_PERCLK_PODF_OFFSET) + 1;
		return parent_rate / (prediv1 * prediv2 * podf);
	} else if (clk->parent == &ipg_clk)
		return parent_rate;
	else
		BUG();
}

static int _clk_ipg_per_set_parent(struct clk *clk, struct clk *parent)
{
	u32 reg;

	reg = __raw_readl(MXC_CCM_CBCMR);

	reg &= ~MXC_CCM_CBCMR_PERCLK_LP_APM_CLK_SEL;
	reg &= ~MXC_CCM_CBCMR_PERCLK_IPG_CLK_SEL;

	if (parent == &ipg_clk)
		reg |= MXC_CCM_CBCMR_PERCLK_IPG_CLK_SEL;
	else if (parent == &lp_apm_clk)
		reg |= MXC_CCM_CBCMR_PERCLK_LP_APM_CLK_SEL;
	else if (parent != &main_bus_clk)
		return -EINVAL;

	__raw_writel(reg, MXC_CCM_CBCMR);

	return 0;
}

#define clk_nfc_set_parent	NULL

static unsigned long clk_nfc_get_rate(struct clk *clk)
{
	unsigned long rate;
	u32 reg, div;

	reg = __raw_readl(MXC_CCM_CBCDR);
	div = ((reg & MXC_CCM_CBCDR_NFC_PODF_MASK) >>
	       MXC_CCM_CBCDR_NFC_PODF_OFFSET) + 1;
	rate = clk_get_rate(clk->parent) / div;
	WARN_ON(rate == 0);
	return rate;
}

static unsigned long clk_nfc_round_rate(struct clk *clk,
						unsigned long rate)
{
	u32 div;
	unsigned long parent_rate = clk_get_rate(clk->parent);

	if (!rate)
		return -EINVAL;

	div = parent_rate / rate;

	if (parent_rate % rate)
		div++;

	if (div > 8)
		return -EINVAL;

	return parent_rate / div;

}

static int clk_nfc_set_rate(struct clk *clk, unsigned long rate)
{
	u32 reg, div;

	div = clk_get_rate(clk->parent) / rate;
	if (div == 0)
		div++;
	if (((clk_get_rate(clk->parent) / div) != rate) || (div > 8))
		return -EINVAL;

	reg = __raw_readl(MXC_CCM_CBCDR);
	reg &= ~MXC_CCM_CBCDR_NFC_PODF_MASK;
	reg |= (div - 1) << MXC_CCM_CBCDR_NFC_PODF_OFFSET;
	__raw_writel(reg, MXC_CCM_CBCDR);

	while (__raw_readl(MXC_CCM_CDHIPR) &
			MXC_CCM_CDHIPR_NFC_IPG_INT_MEM_PODF_BUSY){
	}

	return 0;
}

static unsigned long get_high_reference_clock_rate(struct clk *clk)
{
	return external_high_reference;
}

static unsigned long get_low_reference_clock_rate(struct clk *clk)
{
	return external_low_reference;
}

static unsigned long get_oscillator_reference_clock_rate(struct clk *clk)
{
	return oscillator_reference;
}

static unsigned long get_ckih2_reference_clock_rate(struct clk *clk)
{
	return ckih2_reference;
}

static unsigned long clk_emi_slow_get_rate(struct clk *clk)
{
	u32 reg, div;

	reg = __raw_readl(MXC_CCM_CBCDR);
	div = ((reg & MXC_CCM_CBCDR_EMI_PODF_MASK) >>
	       MXC_CCM_CBCDR_EMI_PODF_OFFSET) + 1;

	return clk_get_rate(clk->parent) / div;
}

static unsigned long _clk_ddr_hf_get_rate(struct clk *clk)
{
	unsigned long rate;
	u32 reg, div;

	reg = __raw_readl(MXC_CCM_CBCDR);
	div = ((reg & MXC_CCM_CBCDR_DDR_PODF_MASK) >>
		MXC_CCM_CBCDR_DDR_PODF_OFFSET) + 1;
	rate = clk_get_rate(clk->parent) / div;

	return rate;
}

/* External high frequency clock */
static struct clk ckih_clk = {
	.get_rate = get_high_reference_clock_rate,
};

static struct clk ckih2_clk = {
	.get_rate = get_ckih2_reference_clock_rate,
};

static struct clk osc_clk = {
	.get_rate = get_oscillator_reference_clock_rate,
};

/* External low frequency (32kHz) clock */
static struct clk ckil_clk = {
	.get_rate = get_low_reference_clock_rate,
};

static struct clk pll1_main_clk = {
	.parent = &osc_clk,
	.get_rate = clk_pll_get_rate,
	.enable = _clk_pll_enable,
	.disable = _clk_pll_disable,
};

/* Clock tree block diagram (WIP):
 * 	CCM: Clock Controller Module
 *
 * PLL output -> |
 *               | CCM Switcher -> CCM_CLK_ROOT_GEN ->
 * PLL bypass -> |
 *
 */

/* PLL1 SW supplies to ARM core */
static struct clk pll1_sw_clk = {
	.parent = &pll1_main_clk,
	.set_parent = _clk_pll1_sw_set_parent,
	.get_rate = clk_pll1_sw_get_rate,
};

/* PLL2 SW supplies to AXI/AHB/IP buses */
static struct clk pll2_sw_clk = {
	.parent = &osc_clk,
	.get_rate = clk_pll_get_rate,
	.set_rate = _clk_pll_set_rate,
	.set_parent = _clk_pll2_sw_set_parent,
	.enable = _clk_pll_enable,
	.disable = _clk_pll_disable,
};

/* PLL3 SW supplies to serial clocks like USB, SSI, etc. */
static struct clk pll3_sw_clk = {
	.parent = &osc_clk,
	.set_rate = _clk_pll_set_rate,
	.get_rate = clk_pll_get_rate,
	.enable = _clk_pll_enable,
	.disable = _clk_pll_disable,
};

/* PLL4 SW supplies to LVDS Display Bridge(LDB) */
static struct clk mx53_pll4_sw_clk = {
	.parent = &osc_clk,
	.set_rate = _clk_pll_set_rate,
	.enable = _clk_pll_enable,
	.disable = _clk_pll_disable,
};

/* Low-power Audio Playback Mode clock */
static struct clk lp_apm_clk = {
	.parent = &osc_clk,
	.set_parent = _clk_lp_apm_set_parent,
};

static struct clk periph_apm_clk = {
	.parent = &pll1_sw_clk,
	.set_parent = _clk_periph_apm_set_parent,
};

static struct clk cpu_clk = {
	.parent = &pll1_sw_clk,
	.get_rate = clk_cpu_get_rate,
	.set_rate = clk_cpu_set_rate,
};

static struct clk ahb_clk = {
	.parent = &main_bus_clk,
	.get_rate = clk_ahb_get_rate,
	.set_rate = _clk_ahb_set_rate,
	.round_rate = _clk_ahb_round_rate,
};

static struct clk iim_clk = {
	.parent = &ipg_clk,
	.enable_reg = MXC_CCM_CCGR0,
	.enable_shift = MXC_CCM_CCGRx_CG15_OFFSET,
};

/* Main IP interface clock for access to registers */
static struct clk ipg_clk = {
	.parent = &ahb_clk,
	.get_rate = clk_ipg_get_rate,
};

static struct clk ipg_perclk = {
	.parent = &lp_apm_clk,
	.get_rate = clk_ipg_per_get_rate,
	.set_parent = _clk_ipg_per_set_parent,
};

static struct clk ahb_max_clk = {
	.parent = &ahb_clk,
	.enable_reg = MXC_CCM_CCGR0,
	.enable_shift = MXC_CCM_CCGRx_CG14_OFFSET,
	.enable = _clk_max_enable,
	.disable = _clk_max_disable,
};

static struct clk aips_tz1_clk = {
	.parent = &ahb_clk,
	.secondary = &ahb_max_clk,
	.enable_reg = MXC_CCM_CCGR0,
	.enable_shift = MXC_CCM_CCGRx_CG12_OFFSET,
	.enable = _clk_ccgr_enable,
	.disable = _clk_ccgr_disable_inwait,
};

static struct clk aips_tz2_clk = {
	.parent = &ahb_clk,
	.secondary = &ahb_max_clk,
	.enable_reg = MXC_CCM_CCGR0,
	.enable_shift = MXC_CCM_CCGRx_CG13_OFFSET,
	.enable = _clk_ccgr_enable,
	.disable = _clk_ccgr_disable_inwait,
};

static struct clk gpt_32k_clk = {
	.id = 0,
	.parent = &ckil_clk,
};

static struct clk kpp_clk = {
	.id = 0,
};

static struct clk dummy_clk = {
	.id = 0,
};

static struct clk emi_slow_clk = {
	.parent = &pll2_sw_clk,
	.enable_reg = MXC_CCM_CCGR5,
	.enable_shift = MXC_CCM_CCGRx_CG8_OFFSET,
	.enable = _clk_ccgr_enable,
	.disable = _clk_ccgr_disable_inwait,
	.get_rate = clk_emi_slow_get_rate,
};

static int clk_ipu_enable(struct clk *clk)
{
	u32 reg;

	_clk_ccgr_enable(clk);

	/* Enable handshake with IPU when certain clock rates are changed */
	reg = __raw_readl(MXC_CCM_CCDR);
	reg &= ~MXC_CCM_CCDR_IPU_HS_MASK;
	__raw_writel(reg, MXC_CCM_CCDR);

	/* Enable handshake with IPU when LPM is entered */
	reg = __raw_readl(MXC_CCM_CLPCR);
	reg &= ~MXC_CCM_CLPCR_BYPASS_IPU_LPM_HS;
	__raw_writel(reg, MXC_CCM_CLPCR);

	return 0;
}

static void clk_ipu_disable(struct clk *clk)
{
	u32 reg;

	_clk_ccgr_disable(clk);

	/* Disable handshake with IPU whe dividers are changed */
	reg = __raw_readl(MXC_CCM_CCDR);
	reg |= MXC_CCM_CCDR_IPU_HS_MASK;
	__raw_writel(reg, MXC_CCM_CCDR);

	/* Disable handshake with IPU when LPM is entered */
	reg = __raw_readl(MXC_CCM_CLPCR);
	reg |= MXC_CCM_CLPCR_BYPASS_IPU_LPM_HS;
	__raw_writel(reg, MXC_CCM_CLPCR);
}

static struct clk ahbmux1_clk = {
	.parent = &ahb_clk,
	.secondary = &ahb_max_clk,
	.enable_reg = MXC_CCM_CCGR0,
	.enable_shift = MXC_CCM_CCGRx_CG8_OFFSET,
	.enable = _clk_ccgr_enable,
	.disable = _clk_ccgr_disable_inwait,
};

static struct clk ipu_sec_clk = {
	.parent = &emi_fast_clk,
	.secondary = &ahbmux1_clk,
};

static struct clk ddr_hf_clk = {
	.parent = &pll1_sw_clk,
	.get_rate = _clk_ddr_hf_get_rate,
};

static struct clk ddr_clk = {
	.parent = &ddr_hf_clk,
};

/* clock definitions for MIPI HSC unit which has been removed
 * from documentation, but not from hardware
 */
static int _clk_hsc_enable(struct clk *clk)
{
	u32 reg;

	_clk_ccgr_enable(clk);
	/* Handshake with IPU when certain clock rates are changed. */
	reg = __raw_readl(MXC_CCM_CCDR);
	reg &= ~MXC_CCM_CCDR_HSC_HS_MASK;
	__raw_writel(reg, MXC_CCM_CCDR);

	reg = __raw_readl(MXC_CCM_CLPCR);
	reg &= ~MXC_CCM_CLPCR_BYPASS_HSC_LPM_HS;
	__raw_writel(reg, MXC_CCM_CLPCR);

	return 0;
}

static void _clk_hsc_disable(struct clk *clk)
{
	u32 reg;

	_clk_ccgr_disable(clk);
	/* No handshake with HSC as its not enabled. */
	reg = __raw_readl(MXC_CCM_CCDR);
	reg |= MXC_CCM_CCDR_HSC_HS_MASK;
	__raw_writel(reg, MXC_CCM_CCDR);

	reg = __raw_readl(MXC_CCM_CLPCR);
	reg |= MXC_CCM_CLPCR_BYPASS_HSC_LPM_HS;
	__raw_writel(reg, MXC_CCM_CLPCR);
}

static struct clk mipi_hsp_clk = {
	.parent = &ipu_clk,
	.enable_reg = MXC_CCM_CCGR4,
	.enable_shift = MXC_CCM_CCGRx_CG6_OFFSET,
	.enable = _clk_hsc_enable,
	.disable = _clk_hsc_disable,
	.secondary = &mipi_hsc1_clk,
};

#define DEFINE_CLOCK_CCGR(name, i, er, es, pfx, p, s)	\
	static struct clk name = {			\
		.id		= i,			\
		.enable_reg	= er,			\
		.enable_shift	= es,			\
		.get_rate	= pfx##_get_rate,	\
		.set_rate	= pfx##_set_rate,	\
		.round_rate	= pfx##_round_rate,	\
		.set_parent	= pfx##_set_parent,	\
		.enable		= _clk_ccgr_enable,	\
		.disable	= _clk_ccgr_disable,	\
		.parent		= p,			\
		.secondary	= s,			\
	}

#define DEFINE_CLOCK_MAX(name, i, er, es, pfx, p, s)	\
	static struct clk name = {			\
		.id		= i,			\
		.enable_reg	= er,			\
		.enable_shift	= es,			\
		.get_rate	= pfx##_get_rate,	\
		.set_rate	= pfx##_set_rate,	\
		.set_parent	= pfx##_set_parent,	\
		.enable		= _clk_max_enable,	\
		.disable	= _clk_max_disable,	\
		.parent		= p,			\
		.secondary	= s,			\
	}

#define CLK_GET_RATE(name, nr, bitsname)				\
static unsigned long clk_##name##_get_rate(struct clk *clk)		\
{									\
	u32 reg, pred, podf;						\
									\
	reg = __raw_readl(MXC_CCM_CSCDR##nr);				\
	pred = (reg & MXC_CCM_CSCDR##nr##_##bitsname##_CLK_PRED_MASK)	\
		>> MXC_CCM_CSCDR##nr##_##bitsname##_CLK_PRED_OFFSET;	\
	podf = (reg & MXC_CCM_CSCDR##nr##_##bitsname##_CLK_PODF_MASK)	\
		>> MXC_CCM_CSCDR##nr##_##bitsname##_CLK_PODF_OFFSET;	\
									\
	return DIV_ROUND_CLOSEST(clk_get_rate(clk->parent),		\
			(pred + 1) * (podf + 1));			\
}

#define CLK_SET_PARENT(name, nr, bitsname)				\
static int clk_##name##_set_parent(struct clk *clk, struct clk *parent)	\
{									\
	u32 reg, mux;							\
									\
	mux = _get_mux(parent, &pll1_sw_clk, &pll2_sw_clk,		\
			&pll3_sw_clk, &lp_apm_clk);			\
	reg = __raw_readl(MXC_CCM_CSCMR##nr) &				\
		~MXC_CCM_CSCMR##nr##_##bitsname##_CLK_SEL_MASK;		\
	reg |= mux << MXC_CCM_CSCMR##nr##_##bitsname##_CLK_SEL_OFFSET;	\
	__raw_writel(reg, MXC_CCM_CSCMR##nr);				\
									\
	return 0;							\
}

#define CLK_SET_RATE(name, nr, bitsname)				\
static int clk_##name##_set_rate(struct clk *clk, unsigned long rate)	\
{									\
	u32 reg, div, parent_rate;					\
	u32 pre = 0, post = 0;						\
									\
	parent_rate = clk_get_rate(clk->parent);			\
	div = parent_rate / rate;					\
									\
	if ((parent_rate / div) != rate)				\
		return -EINVAL;						\
									\
	__calc_pre_post_dividers(div, &pre, &post,			\
		(MXC_CCM_CSCDR##nr##_##bitsname##_CLK_PRED_MASK >>	\
		MXC_CCM_CSCDR##nr##_##bitsname##_CLK_PRED_OFFSET) + 1,	\
		(MXC_CCM_CSCDR##nr##_##bitsname##_CLK_PODF_MASK >>	\
		MXC_CCM_CSCDR##nr##_##bitsname##_CLK_PODF_OFFSET) + 1);\
									\
	/* Set sdhc1 clock divider */					\
	reg = __raw_readl(MXC_CCM_CSCDR##nr) &				\
		~(MXC_CCM_CSCDR##nr##_##bitsname##_CLK_PRED_MASK	\
		| MXC_CCM_CSCDR##nr##_##bitsname##_CLK_PODF_MASK);	\
	reg |= (post - 1) <<						\
		MXC_CCM_CSCDR##nr##_##bitsname##_CLK_PODF_OFFSET;	\
	reg |= (pre - 1) <<						\
		MXC_CCM_CSCDR##nr##_##bitsname##_CLK_PRED_OFFSET;	\
	__raw_writel(reg, MXC_CCM_CSCDR##nr);				\
									\
	return 0;							\
}

/* UART */
CLK_GET_RATE(uart, 1, UART)
CLK_SET_PARENT(uart, 1, UART)

static struct clk uart_root_clk = {
	.parent = &pll2_sw_clk,
	.get_rate = clk_uart_get_rate,
	.set_parent = clk_uart_set_parent,
};

/* USBOH3 */
CLK_GET_RATE(usboh3, 1, USBOH3)
CLK_SET_PARENT(usboh3, 1, USBOH3)

static struct clk usboh3_clk = {
	.parent = &pll2_sw_clk,
	.get_rate = clk_usboh3_get_rate,
	.set_parent = clk_usboh3_set_parent,
	.enable = _clk_ccgr_enable,
	.disable = _clk_ccgr_disable,
	.enable_reg = MXC_CCM_CCGR2,
	.enable_shift = MXC_CCM_CCGRx_CG14_OFFSET,
};

static struct clk usb_ahb_clk = {
	.parent = &ipg_clk,
	.enable = _clk_ccgr_enable,
	.disable = _clk_ccgr_disable,
	.enable_reg = MXC_CCM_CCGR2,
	.enable_shift = MXC_CCM_CCGRx_CG13_OFFSET,
};

static int clk_usb_phy1_set_parent(struct clk *clk, struct clk *parent)
{
	u32 reg;

	reg = __raw_readl(MXC_CCM_CSCMR1) & ~MXC_CCM_CSCMR1_USB_PHY_CLK_SEL;

	if (parent == &pll3_sw_clk)
		reg |= 1 << MXC_CCM_CSCMR1_USB_PHY_CLK_SEL_OFFSET;

	__raw_writel(reg, MXC_CCM_CSCMR1);

	return 0;
}

static struct clk usb_phy1_clk = {
	.parent = &pll3_sw_clk,
	.set_parent = clk_usb_phy1_set_parent,
	.enable = _clk_ccgr_enable,
	.enable_reg = MXC_CCM_CCGR2,
	.enable_shift = MXC_CCM_CCGRx_CG0_OFFSET,
	.disable = _clk_ccgr_disable,
};

/* eCSPI */
CLK_GET_RATE(ecspi, 2, CSPI)
CLK_SET_PARENT(ecspi, 1, CSPI)

static struct clk ecspi_main_clk = {
	.parent = &pll3_sw_clk,
	.get_rate = clk_ecspi_get_rate,
	.set_parent = clk_ecspi_set_parent,
};

/* eSDHC */
CLK_GET_RATE(esdhc1, 1, ESDHC1_MSHC1)
CLK_SET_PARENT(esdhc1, 1, ESDHC1_MSHC1)
CLK_SET_RATE(esdhc1, 1, ESDHC1_MSHC1)

CLK_GET_RATE(esdhc2, 1, ESDHC2_MSHC2)
CLK_SET_PARENT(esdhc2, 1, ESDHC2_MSHC2)
CLK_SET_RATE(esdhc2, 1, ESDHC2_MSHC2)

#define DEFINE_CLOCK_FULL(name, i, er, es, gr, sr, e, d, p, s)		\
	static struct clk name = {					\
		.id		= i,					\
		.enable_reg	= er,					\
		.enable_shift	= es,					\
		.get_rate	= gr,					\
		.set_rate	= sr,					\
		.enable		= e,					\
		.disable	= d,					\
		.parent		= p,					\
		.secondary	= s,					\
	}

#define DEFINE_CLOCK(name, i, er, es, gr, sr, p, s)			\
	DEFINE_CLOCK_FULL(name, i, er, es, gr, sr, _clk_ccgr_enable, _clk_ccgr_disable, p, s)

/* Shared peripheral bus arbiter */
DEFINE_CLOCK(spba_clk, 0, MXC_CCM_CCGR5, MXC_CCM_CCGRx_CG0_OFFSET,
	NULL,  NULL, &ipg_clk, NULL);

/* UART */
DEFINE_CLOCK(uart1_ipg_clk, 0, MXC_CCM_CCGR1, MXC_CCM_CCGRx_CG3_OFFSET,
	NULL,  NULL, &ipg_clk, &aips_tz1_clk);
DEFINE_CLOCK(uart2_ipg_clk, 1, MXC_CCM_CCGR1, MXC_CCM_CCGRx_CG5_OFFSET,
	NULL,  NULL, &ipg_clk, &aips_tz1_clk);
DEFINE_CLOCK(uart3_ipg_clk, 2, MXC_CCM_CCGR1, MXC_CCM_CCGRx_CG7_OFFSET,
	NULL,  NULL, &ipg_clk, &spba_clk);
DEFINE_CLOCK(uart1_clk, 0, MXC_CCM_CCGR1, MXC_CCM_CCGRx_CG4_OFFSET,
	NULL,  NULL, &uart_root_clk, &uart1_ipg_clk);
DEFINE_CLOCK(uart2_clk, 1, MXC_CCM_CCGR1, MXC_CCM_CCGRx_CG6_OFFSET,
	NULL,  NULL, &uart_root_clk, &uart2_ipg_clk);
DEFINE_CLOCK(uart3_clk, 2, MXC_CCM_CCGR1, MXC_CCM_CCGRx_CG8_OFFSET,
	NULL,  NULL, &uart_root_clk, &uart3_ipg_clk);

/* GPT */
DEFINE_CLOCK(gpt_ipg_clk, 0, MXC_CCM_CCGR2, MXC_CCM_CCGRx_CG10_OFFSET,
	NULL,  NULL, &ipg_clk, NULL);
DEFINE_CLOCK(gpt_clk, 0, MXC_CCM_CCGR2, MXC_CCM_CCGRx_CG9_OFFSET,
	NULL,  NULL, &ipg_clk, &gpt_ipg_clk);

DEFINE_CLOCK(pwm1_clk, 0, MXC_CCM_CCGR2, MXC_CCM_CCGRx_CG6_OFFSET,
	NULL, NULL, &ipg_clk, NULL);
DEFINE_CLOCK(pwm2_clk, 0, MXC_CCM_CCGR2, MXC_CCM_CCGRx_CG8_OFFSET,
	NULL, NULL, &ipg_clk, NULL);

/* I2C */
DEFINE_CLOCK(i2c1_clk, 0, MXC_CCM_CCGR1, MXC_CCM_CCGRx_CG9_OFFSET,
	NULL, NULL, &ipg_clk, NULL);
DEFINE_CLOCK(i2c2_clk, 1, MXC_CCM_CCGR1, MXC_CCM_CCGRx_CG10_OFFSET,
	NULL, NULL, &ipg_clk, NULL);
DEFINE_CLOCK(hsi2c_clk, 0, MXC_CCM_CCGR1, MXC_CCM_CCGRx_CG11_OFFSET,
	NULL, NULL, &ipg_clk, NULL);

/* FEC */
DEFINE_CLOCK(fec_clk, 0, MXC_CCM_CCGR2, MXC_CCM_CCGRx_CG12_OFFSET,
	NULL,  NULL, &ipg_clk, NULL);

/* NFC */
DEFINE_CLOCK_CCGR(nfc_clk, 0, MXC_CCM_CCGR5, MXC_CCM_CCGRx_CG10_OFFSET,
	clk_nfc, &emi_slow_clk, NULL);

/* SSI */
DEFINE_CLOCK(ssi1_ipg_clk, 0, MXC_CCM_CCGR3, MXC_CCM_CCGRx_CG8_OFFSET,
	NULL, NULL, &ipg_clk, NULL);
DEFINE_CLOCK(ssi1_clk, 0, MXC_CCM_CCGR3, MXC_CCM_CCGRx_CG9_OFFSET,
	NULL, NULL, &pll3_sw_clk, &ssi1_ipg_clk);
DEFINE_CLOCK(ssi2_ipg_clk, 1, MXC_CCM_CCGR3, MXC_CCM_CCGRx_CG10_OFFSET,
	NULL, NULL, &ipg_clk, NULL);
DEFINE_CLOCK(ssi2_clk, 1, MXC_CCM_CCGR3, MXC_CCM_CCGRx_CG11_OFFSET,
	NULL, NULL, &pll3_sw_clk, &ssi2_ipg_clk);
DEFINE_CLOCK(ssi3_ipg_clk, 2, MXC_CCM_CCGR3, MXC_CCM_CCGRx_CG12_OFFSET,
	NULL, NULL, &ipg_clk, NULL);
DEFINE_CLOCK(ssi3_clk, 2, MXC_CCM_CCGR3, MXC_CCM_CCGRx_CG13_OFFSET,
	NULL, NULL, &pll3_sw_clk, &ssi3_ipg_clk);

/* eCSPI */
DEFINE_CLOCK_FULL(ecspi1_ipg_clk, 0, MXC_CCM_CCGR4, MXC_CCM_CCGRx_CG9_OFFSET,
		NULL, NULL, _clk_ccgr_enable_inrun, _clk_ccgr_disable,
		&ipg_clk, &spba_clk);
DEFINE_CLOCK(ecspi1_clk, 0, MXC_CCM_CCGR4, MXC_CCM_CCGRx_CG10_OFFSET,
		NULL, NULL, &ecspi_main_clk, &ecspi1_ipg_clk);
DEFINE_CLOCK_FULL(ecspi2_ipg_clk, 0, MXC_CCM_CCGR4, MXC_CCM_CCGRx_CG11_OFFSET,
		NULL, NULL, _clk_ccgr_enable_inrun, _clk_ccgr_disable,
		&ipg_clk, &aips_tz2_clk);
DEFINE_CLOCK(ecspi2_clk, 0, MXC_CCM_CCGR4, MXC_CCM_CCGRx_CG12_OFFSET,
		NULL, NULL, &ecspi_main_clk, &ecspi2_ipg_clk);

/* CSPI */
DEFINE_CLOCK(cspi_ipg_clk, 0, MXC_CCM_CCGR4, MXC_CCM_CCGRx_CG9_OFFSET,
		NULL, NULL, &ipg_clk, &aips_tz2_clk);
DEFINE_CLOCK(cspi_clk, 0, MXC_CCM_CCGR4, MXC_CCM_CCGRx_CG13_OFFSET,
		NULL, NULL, &ipg_clk, &cspi_ipg_clk);

/* SDMA */
DEFINE_CLOCK(sdma_clk, 1, MXC_CCM_CCGR4, MXC_CCM_CCGRx_CG15_OFFSET,
		NULL, NULL, &ahb_clk, NULL);

/* eSDHC */
DEFINE_CLOCK_FULL(esdhc1_ipg_clk, 0, MXC_CCM_CCGR3, MXC_CCM_CCGRx_CG0_OFFSET,
	NULL,  NULL, _clk_max_enable, _clk_max_disable, &ipg_clk, NULL);
DEFINE_CLOCK_MAX(esdhc1_clk, 0, MXC_CCM_CCGR3, MXC_CCM_CCGRx_CG1_OFFSET,
	clk_esdhc1, &pll2_sw_clk, &esdhc1_ipg_clk);
DEFINE_CLOCK_FULL(esdhc2_ipg_clk, 1, MXC_CCM_CCGR3, MXC_CCM_CCGRx_CG2_OFFSET,
	NULL,  NULL, _clk_max_enable, _clk_max_disable, &ipg_clk, NULL);
DEFINE_CLOCK_MAX(esdhc2_clk, 1, MXC_CCM_CCGR3, MXC_CCM_CCGRx_CG3_OFFSET,
	clk_esdhc2, &pll2_sw_clk, &esdhc2_ipg_clk);

DEFINE_CLOCK(mipi_esc_clk, 0, MXC_CCM_CCGR4, MXC_CCM_CCGRx_CG5_OFFSET, NULL, NULL, NULL, &pll2_sw_clk);
DEFINE_CLOCK(mipi_hsc2_clk, 0, MXC_CCM_CCGR4, MXC_CCM_CCGRx_CG4_OFFSET, NULL, NULL, &mipi_esc_clk, &pll2_sw_clk);
DEFINE_CLOCK(mipi_hsc1_clk, 0, MXC_CCM_CCGR4, MXC_CCM_CCGRx_CG3_OFFSET, NULL, NULL, &mipi_hsc2_clk, &pll2_sw_clk);

/* IPU */
DEFINE_CLOCK_FULL(ipu_clk, 0, MXC_CCM_CCGR5, MXC_CCM_CCGRx_CG5_OFFSET,
	NULL,  NULL, clk_ipu_enable, clk_ipu_disable, &ahb_clk, &ipu_sec_clk);

DEFINE_CLOCK_FULL(emi_fast_clk, 0, MXC_CCM_CCGR5, MXC_CCM_CCGRx_CG7_OFFSET,
		NULL, NULL, _clk_ccgr_enable, _clk_ccgr_disable_inwait,
		&ddr_clk, NULL);

DEFINE_CLOCK(ipu_di0_clk, 0, MXC_CCM_CCGR6, MXC_CCM_CCGRx_CG5_OFFSET,
		NULL, NULL, &pll3_sw_clk, NULL);
DEFINE_CLOCK(ipu_di1_clk, 0, MXC_CCM_CCGR6, MXC_CCM_CCGRx_CG6_OFFSET,
		NULL, NULL, &pll3_sw_clk, NULL);

#define _REGISTER_CLOCK(d, n, c) \
       { \
		.dev_id = d, \
		.con_id = n, \
		.clk = &c,   \
       },

static struct clk_lookup mx51_lookups[] = {
	_REGISTER_CLOCK("imx-uart.0", NULL, uart1_clk)
	_REGISTER_CLOCK("imx-uart.1", NULL, uart2_clk)
	_REGISTER_CLOCK("imx-uart.2", NULL, uart3_clk)
	_REGISTER_CLOCK(NULL, "gpt", gpt_clk)
	_REGISTER_CLOCK("fec.0", NULL, fec_clk)
	_REGISTER_CLOCK("mxc_pwm.0", "pwm", pwm1_clk)
	_REGISTER_CLOCK("mxc_pwm.1", "pwm", pwm2_clk)
	_REGISTER_CLOCK("imx-i2c.0", NULL, i2c1_clk)
	_REGISTER_CLOCK("imx-i2c.1", NULL, i2c2_clk)
	_REGISTER_CLOCK("imx-i2c.2", NULL, hsi2c_clk)
	_REGISTER_CLOCK("mxc-ehci.0", "usb", usboh3_clk)
	_REGISTER_CLOCK("mxc-ehci.0", "usb_ahb", usb_ahb_clk)
	_REGISTER_CLOCK("mxc-ehci.0", "usb_phy1", usb_phy1_clk)
	_REGISTER_CLOCK("mxc-ehci.1", "usb", usboh3_clk)
	_REGISTER_CLOCK("mxc-ehci.1", "usb_ahb", usb_ahb_clk)
	_REGISTER_CLOCK("mxc-ehci.2", "usb", usboh3_clk)
	_REGISTER_CLOCK("mxc-ehci.2", "usb_ahb", usb_ahb_clk)
	_REGISTER_CLOCK("fsl-usb2-udc", "usb", usboh3_clk)
	_REGISTER_CLOCK("fsl-usb2-udc", "usb_ahb", ahb_clk)
	_REGISTER_CLOCK("imx-keypad", NULL, kpp_clk)
	_REGISTER_CLOCK("mxc_nand", NULL, nfc_clk)
	_REGISTER_CLOCK("imx-ssi.0", NULL, ssi1_clk)
	_REGISTER_CLOCK("imx-ssi.1", NULL, ssi2_clk)
	_REGISTER_CLOCK("imx-ssi.2", NULL, ssi3_clk)
	_REGISTER_CLOCK("imx-sdma", NULL, sdma_clk)
	_REGISTER_CLOCK(NULL, "ckih", ckih_clk)
	_REGISTER_CLOCK(NULL, "ckih2", ckih2_clk)
	_REGISTER_CLOCK(NULL, "gpt_32k", gpt_32k_clk)
	_REGISTER_CLOCK("imx51-ecspi.0", NULL, ecspi1_clk)
	_REGISTER_CLOCK("imx51-ecspi.1", NULL, ecspi2_clk)
	_REGISTER_CLOCK("imx51-cspi.0", NULL, cspi_clk)
	_REGISTER_CLOCK("sdhci-esdhc-imx.0", NULL, esdhc1_clk)
	_REGISTER_CLOCK("sdhci-esdhc-imx.1", NULL, esdhc2_clk)
	_REGISTER_CLOCK(NULL, "cpu_clk", cpu_clk)
	_REGISTER_CLOCK(NULL, "iim_clk", iim_clk)
	_REGISTER_CLOCK("imx2-wdt.0", NULL, dummy_clk)
	_REGISTER_CLOCK("imx2-wdt.1", NULL, dummy_clk)
	_REGISTER_CLOCK(NULL, "mipi_hsp", mipi_hsp_clk)
	_REGISTER_CLOCK("imx-ipuv3", NULL, ipu_clk)
	_REGISTER_CLOCK("imx-ipuv3", "di0", ipu_di0_clk)
	_REGISTER_CLOCK("imx-ipuv3", "di1", ipu_di1_clk)
};

static struct clk_lookup mx53_lookups[] = {
	_REGISTER_CLOCK("imx-uart.0", NULL, uart1_clk)
	_REGISTER_CLOCK("imx-uart.1", NULL, uart2_clk)
	_REGISTER_CLOCK("imx-uart.2", NULL, uart3_clk)
	_REGISTER_CLOCK(NULL, "gpt", gpt_clk)
	_REGISTER_CLOCK("fec.0", NULL, fec_clk)
	_REGISTER_CLOCK(NULL, "iim_clk", iim_clk)
	_REGISTER_CLOCK("imx-i2c.0", NULL, i2c1_clk)
	_REGISTER_CLOCK("imx-i2c.1", NULL, i2c2_clk)
	_REGISTER_CLOCK("sdhci-esdhc-imx.0", NULL, esdhc1_clk)
	_REGISTER_CLOCK("sdhci-esdhc-imx.1", NULL, esdhc2_clk)
	_REGISTER_CLOCK("imx53-ecspi.0", NULL, ecspi1_clk)
	_REGISTER_CLOCK("imx53-ecspi.1", NULL, ecspi2_clk)
	_REGISTER_CLOCK("imx53-cspi.0", NULL, cspi_clk)
};

static void clk_tree_init(void)
{
	u32 reg;

	ipg_perclk.set_parent(&ipg_perclk, &lp_apm_clk);

	/*
	 * Initialise the IPG PER CLK dividers to 3. IPG_PER_CLK should be at
	 * 8MHz, its derived from lp_apm.
	 *
	 * FIXME: Verify if true for all boards
	 */
	reg = __raw_readl(MXC_CCM_CBCDR);
	reg &= ~MXC_CCM_CBCDR_PERCLK_PRED1_MASK;
	reg &= ~MXC_CCM_CBCDR_PERCLK_PRED2_MASK;
	reg &= ~MXC_CCM_CBCDR_PERCLK_PODF_MASK;
	reg |= (2 << MXC_CCM_CBCDR_PERCLK_PRED1_OFFSET);
	__raw_writel(reg, MXC_CCM_CBCDR);
}

int __init mx51_clocks_init(unsigned long ckil, unsigned long osc,
			unsigned long ckih1, unsigned long ckih2)
{
	int i;

	external_low_reference = ckil;
	external_high_reference = ckih1;
	ckih2_reference = ckih2;
	oscillator_reference = osc;

	for (i = 0; i < ARRAY_SIZE(mx51_lookups); i++)
		clkdev_add(&mx51_lookups[i]);

	clk_tree_init();

	clk_enable(&cpu_clk);
	clk_enable(&main_bus_clk);

	clk_enable(&iim_clk);
	mx51_revision();
	clk_disable(&iim_clk);

	/* move usb_phy_clk to 24MHz */
	clk_set_parent(&usb_phy1_clk, &osc_clk);

	/* set the usboh3_clk parent to pll2_sw_clk */
	clk_set_parent(&usboh3_clk, &pll2_sw_clk);

	/* Set SDHC parents to be PLL2 */
	clk_set_parent(&esdhc1_clk, &pll2_sw_clk);
	clk_set_parent(&esdhc2_clk, &pll2_sw_clk);

	/* set SDHC root clock as 166.25MHZ*/
	clk_set_rate(&esdhc1_clk, 166250000);
	clk_set_rate(&esdhc2_clk, 166250000);

	/* System timer */
	mxc_timer_init(&gpt_clk, MX51_IO_ADDRESS(MX51_GPT1_BASE_ADDR),
		MX51_MXC_INT_GPT);
	return 0;
}

int __init mx53_clocks_init(unsigned long ckil, unsigned long osc,
			unsigned long ckih1, unsigned long ckih2)
{
	int i;

	external_low_reference = ckil;
	external_high_reference = ckih1;
	ckih2_reference = ckih2;
	oscillator_reference = osc;

	for (i = 0; i < ARRAY_SIZE(mx53_lookups); i++)
		clkdev_add(&mx53_lookups[i]);

	clk_tree_init();

	clk_set_parent(&uart_root_clk, &pll3_sw_clk);
	clk_enable(&cpu_clk);
	clk_enable(&main_bus_clk);

	clk_enable(&iim_clk);
	mx53_revision();
	clk_disable(&iim_clk);

	/* System timer */
	mxc_timer_init(&gpt_clk, MX53_IO_ADDRESS(MX53_GPT1_BASE_ADDR),
		MX53_INT_GPT);
	return 0;
}
