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

#include <asm/clkdev.h>
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
static struct clk lp_apm_clk;
static struct clk periph_apm_clk;
static struct clk ahb_clk;
static struct clk ipg_clk;
static struct clk usboh3_clk;

#define MAX_DPLL_WAIT_TRIES	1000 /* 1000 * udelay(1) = 1ms */

static int _clk_ccgr_enable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(clk->enable_reg);
	reg |= MXC_CCM_CCGRx_MOD_ON << clk->enable_shift;
	__raw_writel(reg, clk->enable_reg);

	return 0;
}

static void _clk_ccgr_disable(struct clk *clk)
{
	u32 reg;
	reg = __raw_readl(clk->enable_reg);
	reg &= ~(MXC_CCM_CCGRx_MOD_OFF << clk->enable_shift);
	__raw_writel(reg, clk->enable_reg);

}

static void _clk_ccgr_disable_inwait(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(clk->enable_reg);
	reg &= ~(MXC_CCM_CCGRx_CG_MASK << clk->enable_shift);
	reg |= MXC_CCM_CCGRx_MOD_IDLE << clk->enable_shift;
	__raw_writel(reg, clk->enable_reg);
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

static inline void __iomem *_get_pll_base(struct clk *pll)
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

static unsigned long clk_arm_get_rate(struct clk *clk)
{
	u32 cacrr, div;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);
	cacrr = __raw_readl(MXC_CCM_CACRR);
	div = (cacrr & MXC_CCM_CACRR_ARM_PODF_MASK) + 1;

	return parent_rate / div;
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
	reg &= ~MXC_CCM_CLPCR_BYPASS_MAX_LPM_HS;
	__raw_writel(reg, MXC_CCM_CLPCR);

	return 0;
}

static void _clk_max_disable(struct clk *clk)
{
	u32 reg;

	_clk_ccgr_disable_inwait(clk);

	/* No Handshake with MAX when LPM is entered as its disabled. */
	reg = __raw_readl(MXC_CCM_CLPCR);
	reg |= MXC_CCM_CLPCR_BYPASS_MAX_LPM_HS;
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

static unsigned long clk_uart_get_rate(struct clk *clk)
{
	u32 reg, prediv, podf;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	reg = __raw_readl(MXC_CCM_CSCDR1);
	prediv = ((reg & MXC_CCM_CSCDR1_UART_CLK_PRED_MASK) >>
		  MXC_CCM_CSCDR1_UART_CLK_PRED_OFFSET) + 1;
	podf = ((reg & MXC_CCM_CSCDR1_UART_CLK_PODF_MASK) >>
		MXC_CCM_CSCDR1_UART_CLK_PODF_OFFSET) + 1;

	return parent_rate / (prediv * podf);
}

static int _clk_uart_set_parent(struct clk *clk, struct clk *parent)
{
	u32 reg, mux;

	mux = _get_mux(parent, &pll1_sw_clk, &pll2_sw_clk, &pll3_sw_clk,
		       &lp_apm_clk);
	reg = __raw_readl(MXC_CCM_CSCMR1) & ~MXC_CCM_CSCMR1_UART_CLK_SEL_MASK;
	reg |= mux << MXC_CCM_CSCMR1_UART_CLK_SEL_OFFSET;
	__raw_writel(reg, MXC_CCM_CSCMR1);

	return 0;
}

static unsigned long clk_usboh3_get_rate(struct clk *clk)
{
	u32 reg, prediv, podf;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	reg = __raw_readl(MXC_CCM_CSCDR1);
	prediv = ((reg & MXC_CCM_CSCDR1_USBOH3_CLK_PRED_MASK) >>
		  MXC_CCM_CSCDR1_USBOH3_CLK_PRED_OFFSET) + 1;
	podf = ((reg & MXC_CCM_CSCDR1_USBOH3_CLK_PODF_MASK) >>
		MXC_CCM_CSCDR1_USBOH3_CLK_PODF_OFFSET) + 1;

	return parent_rate / (prediv * podf);
}

static int _clk_usboh3_set_parent(struct clk *clk, struct clk *parent)
{
	u32 reg, mux;

	mux = _get_mux(parent, &pll1_sw_clk, &pll2_sw_clk, &pll3_sw_clk,
		       &lp_apm_clk);
	reg = __raw_readl(MXC_CCM_CSCMR1) & ~MXC_CCM_CSCMR1_USBOH3_CLK_SEL_MASK;
	reg |= mux << MXC_CCM_CSCMR1_USBOH3_CLK_SEL_OFFSET;
	__raw_writel(reg, MXC_CCM_CSCMR1);

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
	.get_rate = clk_arm_get_rate,
};

static struct clk ahb_clk = {
	.parent = &main_bus_clk,
	.get_rate = clk_ahb_get_rate,
	.set_rate = _clk_ahb_set_rate,
	.round_rate = _clk_ahb_round_rate,
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

static struct clk uart_root_clk = {
	.parent = &pll2_sw_clk,
	.get_rate = clk_uart_get_rate,
	.set_parent = _clk_uart_set_parent,
};

static struct clk usboh3_clk = {
	.parent = &pll2_sw_clk,
	.get_rate = clk_usboh3_get_rate,
	.set_parent = _clk_usboh3_set_parent,
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

#define DEFINE_CLOCK(name, i, er, es, gr, sr, p, s)	\
	static struct clk name = {			\
		.id		= i,			\
		.enable_reg	= er,			\
		.enable_shift	= es,			\
		.get_rate	= gr,			\
		.set_rate	= sr,			\
		.enable		= _clk_ccgr_enable,	\
		.disable	= _clk_ccgr_disable,	\
		.parent		= p,			\
		.secondary	= s,			\
	}

/* DEFINE_CLOCK(name, id, enable_reg, enable_shift,
   get_rate, set_rate, parent, secondary); */

/* Shared peripheral bus arbiter */
DEFINE_CLOCK(spba_clk, 0, MXC_CCM_CCGR5, MXC_CCM_CCGRx_CG0_OFFSET,
	NULL,  NULL, &ipg_clk, NULL);

/* UART */
DEFINE_CLOCK(uart1_clk, 0, MXC_CCM_CCGR1, MXC_CCM_CCGRx_CG4_OFFSET,
	NULL,  NULL, &uart_root_clk, NULL);
DEFINE_CLOCK(uart2_clk, 1, MXC_CCM_CCGR1, MXC_CCM_CCGRx_CG6_OFFSET,
	NULL,  NULL, &uart_root_clk, NULL);
DEFINE_CLOCK(uart3_clk, 2, MXC_CCM_CCGR1, MXC_CCM_CCGRx_CG8_OFFSET,
	NULL,  NULL, &uart_root_clk, NULL);
DEFINE_CLOCK(uart1_ipg_clk, 0, MXC_CCM_CCGR1, MXC_CCM_CCGRx_CG3_OFFSET,
	NULL,  NULL, &ipg_clk, &aips_tz1_clk);
DEFINE_CLOCK(uart2_ipg_clk, 1, MXC_CCM_CCGR1, MXC_CCM_CCGRx_CG5_OFFSET,
	NULL,  NULL, &ipg_clk, &aips_tz1_clk);
DEFINE_CLOCK(uart3_ipg_clk, 2, MXC_CCM_CCGR1, MXC_CCM_CCGRx_CG7_OFFSET,
	NULL,  NULL, &ipg_clk, &spba_clk);

/* GPT */
DEFINE_CLOCK(gpt_clk, 0, MXC_CCM_CCGR2, MXC_CCM_CCGRx_CG9_OFFSET,
	NULL,  NULL, &ipg_clk, NULL);
DEFINE_CLOCK(gpt_ipg_clk, 0, MXC_CCM_CCGR2, MXC_CCM_CCGRx_CG10_OFFSET,
	NULL,  NULL, &ipg_clk, NULL);

/* FEC */
DEFINE_CLOCK(fec_clk, 0, MXC_CCM_CCGR2, MXC_CCM_CCGRx_CG12_OFFSET,
	NULL,  NULL, &ipg_clk, NULL);

#define _REGISTER_CLOCK(d, n, c) \
       { \
		.dev_id = d, \
		.con_id = n, \
		.clk = &c,   \
       },

static struct clk_lookup lookups[] = {
	_REGISTER_CLOCK("imx-uart.0", NULL, uart1_clk)
	_REGISTER_CLOCK("imx-uart.1", NULL, uart2_clk)
	_REGISTER_CLOCK("imx-uart.2", NULL, uart3_clk)
	_REGISTER_CLOCK(NULL, "gpt", gpt_clk)
	_REGISTER_CLOCK("fec.0", NULL, fec_clk)
	_REGISTER_CLOCK("mxc-ehci.0", "usb", usboh3_clk)
	_REGISTER_CLOCK("mxc-ehci.0", "usb_ahb", ahb_clk)
	_REGISTER_CLOCK("mxc-ehci.1", "usb", usboh3_clk)
	_REGISTER_CLOCK("mxc-ehci.1", "usb_ahb", ahb_clk)
	_REGISTER_CLOCK("fsl-usb2-udc", "usb", usboh3_clk)
	_REGISTER_CLOCK("fsl-usb2-udc", "usb_ahb", ahb_clk)
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

	for (i = 0; i < ARRAY_SIZE(lookups); i++)
		clkdev_add(&lookups[i]);

	clk_tree_init();

	clk_enable(&cpu_clk);
	clk_enable(&main_bus_clk);

	/* set the usboh3_clk parent to pll2_sw_clk */
	clk_set_parent(&usboh3_clk, &pll2_sw_clk);

	/* System timer */
	mxc_timer_init(&gpt_clk, MX51_IO_ADDRESS(MX51_GPT1_BASE_ADDR),
		MX51_MXC_INT_GPT);
	return 0;
}
