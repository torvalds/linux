/*
 *  linux/arch/arm/mach-omap2/clock.c
 *
 *  Copyright (C) 2005-2008 Texas Instruments, Inc.
 *  Copyright (C) 2004-2008 Nokia Corporation
 *
 *  Contacts:
 *  Richard Woodruff <r-woodruff2@ti.com>
 *  Paul Walmsley
 *
 *  Based on earlier work by Tuukka Tikkanen, Tony Lindgren,
 *  Gordon McNutt and RidgeRun, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#undef DEBUG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/cpufreq.h>
#include <linux/bitops.h>

#include <mach/clock.h>
#include <mach/sram.h>
#include <asm/div64.h>
#include <asm/clkdev.h>

#include <mach/sdrc.h>
#include "clock.h"
#include "prm.h"
#include "prm-regbits-24xx.h"
#include "cm.h"
#include "cm-regbits-24xx.h"

static const struct clkops clkops_oscck;
static const struct clkops clkops_fixed;

#include "clock24xx.h"

struct omap_clk {
	u32		cpu;
	struct clk_lookup lk;
};

#define CLK(dev, con, ck, cp) 		\
	{				\
		 .cpu = cp,		\
		.lk = {			\
			.dev_id = dev,	\
			.con_id = con,	\
			.clk = ck,	\
		},			\
	}

#define CK_243X			RATE_IN_243X
#define CK_242X			RATE_IN_242X

static struct omap_clk omap24xx_clks[] = {
	/* external root sources */
	CLK(NULL,	"func_32k_ck",	&func_32k_ck,	CK_243X | CK_242X),
	CLK(NULL,	"secure_32k_ck", &secure_32k_ck, CK_243X | CK_242X),
	CLK(NULL,	"osc_ck",	&osc_ck,	CK_243X | CK_242X),
	CLK(NULL,	"sys_ck",	&sys_ck,	CK_243X | CK_242X),
	CLK(NULL,	"alt_ck",	&alt_ck,	CK_243X | CK_242X),
	/* internal analog sources */
	CLK(NULL,	"dpll_ck",	&dpll_ck,	CK_243X | CK_242X),
	CLK(NULL,	"apll96_ck",	&apll96_ck,	CK_243X | CK_242X),
	CLK(NULL,	"apll54_ck",	&apll54_ck,	CK_243X | CK_242X),
	/* internal prcm root sources */
	CLK(NULL,	"func_54m_ck",	&func_54m_ck,	CK_243X | CK_242X),
	CLK(NULL,	"core_ck",	&core_ck,	CK_243X | CK_242X),
	CLK(NULL,	"func_96m_ck",	&func_96m_ck,	CK_243X | CK_242X),
	CLK(NULL,	"func_48m_ck",	&func_48m_ck,	CK_243X | CK_242X),
	CLK(NULL,	"func_12m_ck",	&func_12m_ck,	CK_243X | CK_242X),
	CLK(NULL,	"ck_wdt1_osc",	&wdt1_osc_ck,	CK_243X | CK_242X),
	CLK(NULL,	"sys_clkout_src", &sys_clkout_src, CK_243X | CK_242X),
	CLK(NULL,	"sys_clkout",	&sys_clkout,	CK_243X | CK_242X),
	CLK(NULL,	"sys_clkout2_src", &sys_clkout2_src, CK_242X),
	CLK(NULL,	"sys_clkout2",	&sys_clkout2,	CK_242X),
	CLK(NULL,	"emul_ck",	&emul_ck,	CK_242X),
	/* mpu domain clocks */
	CLK(NULL,	"mpu_ck",	&mpu_ck,	CK_243X | CK_242X),
	/* dsp domain clocks */
	CLK(NULL,	"dsp_fck",	&dsp_fck,	CK_243X | CK_242X),
	CLK(NULL,	"dsp_irate_ick", &dsp_irate_ick, CK_243X | CK_242X),
	CLK(NULL,	"dsp_ick",	&dsp_ick,	CK_242X),
	CLK(NULL,	"iva2_1_ick",	&iva2_1_ick,	CK_243X),
	CLK(NULL,	"iva1_ifck",	&iva1_ifck,	CK_242X),
	CLK(NULL,	"iva1_mpu_int_ifck", &iva1_mpu_int_ifck, CK_242X),
	/* GFX domain clocks */
	CLK(NULL,	"gfx_3d_fck",	&gfx_3d_fck,	CK_243X | CK_242X),
	CLK(NULL,	"gfx_2d_fck",	&gfx_2d_fck,	CK_243X | CK_242X),
	CLK(NULL,	"gfx_ick",	&gfx_ick,	CK_243X | CK_242X),
	/* Modem domain clocks */
	CLK(NULL,	"mdm_ick",	&mdm_ick,	CK_243X),
	CLK(NULL,	"mdm_osc_ck",	&mdm_osc_ck,	CK_243X),
	/* DSS domain clocks */
	CLK("omapfb",	"ick",		&dss_ick,	CK_243X | CK_242X),
	CLK("omapfb",	"dss1_fck",	&dss1_fck,	CK_243X | CK_242X),
	CLK("omapfb",	"dss2_fck",	&dss2_fck,	CK_243X | CK_242X),
	CLK("omapfb",	"tv_fck",	&dss_54m_fck,	CK_243X | CK_242X),
	/* L3 domain clocks */
	CLK(NULL,	"core_l3_ck",	&core_l3_ck,	CK_243X | CK_242X),
	CLK(NULL,	"ssi_fck",	&ssi_ssr_sst_fck, CK_243X | CK_242X),
	CLK(NULL,	"usb_l4_ick",	&usb_l4_ick,	CK_243X | CK_242X),
	/* L4 domain clocks */
	CLK(NULL,	"l4_ck",	&l4_ck,		CK_243X | CK_242X),
	CLK(NULL,	"ssi_l4_ick",	&ssi_l4_ick,	CK_243X | CK_242X),
	/* virtual meta-group clock */
	CLK(NULL,	"virt_prcm_set", &virt_prcm_set, CK_243X | CK_242X),
	/* general l4 interface ck, multi-parent functional clk */
	CLK(NULL,	"gpt1_ick",	&gpt1_ick,	CK_243X | CK_242X),
	CLK(NULL,	"gpt1_fck",	&gpt1_fck,	CK_243X | CK_242X),
	CLK(NULL,	"gpt2_ick",	&gpt2_ick,	CK_243X | CK_242X),
	CLK(NULL,	"gpt2_fck",	&gpt2_fck,	CK_243X | CK_242X),
	CLK(NULL,	"gpt3_ick",	&gpt3_ick,	CK_243X | CK_242X),
	CLK(NULL,	"gpt3_fck",	&gpt3_fck,	CK_243X | CK_242X),
	CLK(NULL,	"gpt4_ick",	&gpt4_ick,	CK_243X | CK_242X),
	CLK(NULL,	"gpt4_fck",	&gpt4_fck,	CK_243X | CK_242X),
	CLK(NULL,	"gpt5_ick",	&gpt5_ick,	CK_243X | CK_242X),
	CLK(NULL,	"gpt5_fck",	&gpt5_fck,	CK_243X | CK_242X),
	CLK(NULL,	"gpt6_ick",	&gpt6_ick,	CK_243X | CK_242X),
	CLK(NULL,	"gpt6_fck",	&gpt6_fck,	CK_243X | CK_242X),
	CLK(NULL,	"gpt7_ick",	&gpt7_ick,	CK_243X | CK_242X),
	CLK(NULL,	"gpt7_fck",	&gpt7_fck,	CK_243X | CK_242X),
	CLK(NULL,	"gpt8_ick",	&gpt8_ick,	CK_243X | CK_242X),
	CLK(NULL,	"gpt8_fck",	&gpt8_fck,	CK_243X | CK_242X),
	CLK(NULL,	"gpt9_ick",	&gpt9_ick,	CK_243X | CK_242X),
	CLK(NULL,	"gpt9_fck",	&gpt9_fck,	CK_243X | CK_242X),
	CLK(NULL,	"gpt10_ick",	&gpt10_ick,	CK_243X | CK_242X),
	CLK(NULL,	"gpt10_fck",	&gpt10_fck,	CK_243X | CK_242X),
	CLK(NULL,	"gpt11_ick",	&gpt11_ick,	CK_243X | CK_242X),
	CLK(NULL,	"gpt11_fck",	&gpt11_fck,	CK_243X | CK_242X),
	CLK(NULL,	"gpt12_ick",	&gpt12_ick,	CK_243X | CK_242X),
	CLK(NULL,	"gpt12_fck",	&gpt12_fck,	CK_243X | CK_242X),
	CLK("omap-mcbsp.1", "ick",	&mcbsp1_ick,	CK_243X | CK_242X),
	CLK("omap-mcbsp.1", "fck",	&mcbsp1_fck,	CK_243X | CK_242X),
	CLK("omap-mcbsp.2", "ick",	&mcbsp2_ick,	CK_243X | CK_242X),
	CLK("omap-mcbsp.2", "fck",	&mcbsp2_fck,	CK_243X | CK_242X),
	CLK("omap-mcbsp.3", "ick",	&mcbsp3_ick,	CK_243X),
	CLK("omap-mcbsp.3", "fck",	&mcbsp3_fck,	CK_243X),
	CLK("omap-mcbsp.4", "ick",	&mcbsp4_ick,	CK_243X),
	CLK("omap-mcbsp.4", "fck",	&mcbsp4_fck,	CK_243X),
	CLK("omap-mcbsp.5", "ick",	&mcbsp5_ick,	CK_243X),
	CLK("omap-mcbsp.5", "fck",	&mcbsp5_fck,	CK_243X),
	CLK("omap2_mcspi.1", "ick",	&mcspi1_ick,	CK_243X | CK_242X),
	CLK("omap2_mcspi.1", "fck",	&mcspi1_fck,	CK_243X | CK_242X),
	CLK("omap2_mcspi.2", "ick",	&mcspi2_ick,	CK_243X | CK_242X),
	CLK("omap2_mcspi.2", "fck",	&mcspi2_fck,	CK_243X | CK_242X),
	CLK("omap2_mcspi.3", "ick",	&mcspi3_ick,	CK_243X),
	CLK("omap2_mcspi.3", "fck",	&mcspi3_fck,	CK_243X),
	CLK(NULL,	"uart1_ick",	&uart1_ick,	CK_243X | CK_242X),
	CLK(NULL,	"uart1_fck",	&uart1_fck,	CK_243X | CK_242X),
	CLK(NULL,	"uart2_ick",	&uart2_ick,	CK_243X | CK_242X),
	CLK(NULL,	"uart2_fck",	&uart2_fck,	CK_243X | CK_242X),
	CLK(NULL,	"uart3_ick",	&uart3_ick,	CK_243X | CK_242X),
	CLK(NULL,	"uart3_fck",	&uart3_fck,	CK_243X | CK_242X),
	CLK(NULL,	"gpios_ick",	&gpios_ick,	CK_243X | CK_242X),
	CLK(NULL,	"gpios_fck",	&gpios_fck,	CK_243X | CK_242X),
	CLK("omap_wdt",	"ick",		&mpu_wdt_ick,	CK_243X | CK_242X),
	CLK("omap_wdt",	"fck",		&mpu_wdt_fck,	CK_243X | CK_242X),
	CLK(NULL,	"sync_32k_ick",	&sync_32k_ick,	CK_243X | CK_242X),
	CLK(NULL,	"wdt1_ick",	&wdt1_ick,	CK_243X | CK_242X),
	CLK(NULL,	"omapctrl_ick",	&omapctrl_ick,	CK_243X | CK_242X),
	CLK(NULL,	"icr_ick",	&icr_ick,	CK_243X),
	CLK("omap24xxcam", "fck",	&cam_fck,	CK_243X | CK_242X),
	CLK("omap24xxcam", "ick",	&cam_ick,	CK_243X | CK_242X),
	CLK(NULL,	"mailboxes_ick", &mailboxes_ick,	CK_243X | CK_242X),
	CLK(NULL,	"wdt4_ick",	&wdt4_ick,	CK_243X | CK_242X),
	CLK(NULL,	"wdt4_fck",	&wdt4_fck,	CK_243X | CK_242X),
	CLK(NULL,	"wdt3_ick",	&wdt3_ick,	CK_242X),
	CLK(NULL,	"wdt3_fck",	&wdt3_fck,	CK_242X),
	CLK(NULL,	"mspro_ick",	&mspro_ick,	CK_243X | CK_242X),
	CLK(NULL,	"mspro_fck",	&mspro_fck,	CK_243X | CK_242X),
	CLK("mmci-omap.0", "ick",	&mmc_ick,	CK_242X),
	CLK("mmci-omap.0", "fck",	&mmc_fck,	CK_242X),
	CLK(NULL,	"fac_ick",	&fac_ick,	CK_243X | CK_242X),
	CLK(NULL,	"fac_fck",	&fac_fck,	CK_243X | CK_242X),
	CLK(NULL,	"eac_ick",	&eac_ick,	CK_242X),
	CLK(NULL,	"eac_fck",	&eac_fck,	CK_242X),
	CLK("omap_hdq.0", "ick",	&hdq_ick,	CK_243X | CK_242X),
	CLK("omap_hdq.1", "fck",	&hdq_fck,	CK_243X | CK_242X),
	CLK("i2c_omap.1", "ick",	&i2c1_ick,	CK_243X | CK_242X),
	CLK("i2c_omap.1", "fck",	&i2c1_fck,	CK_242X),
	CLK("i2c_omap.1", "fck",	&i2chs1_fck,	CK_243X),
	CLK("i2c_omap.2", "ick",	&i2c2_ick,	CK_243X | CK_242X),
	CLK("i2c_omap.2", "fck",	&i2c2_fck,	CK_242X),
	CLK("i2c_omap.2", "fck",	&i2chs2_fck,	CK_243X),
	CLK(NULL,	"gpmc_fck",	&gpmc_fck,	CK_243X | CK_242X),
	CLK(NULL,	"sdma_fck",	&sdma_fck,	CK_243X | CK_242X),
	CLK(NULL,	"sdma_ick",	&sdma_ick,	CK_243X | CK_242X),
	CLK(NULL,	"vlynq_ick",	&vlynq_ick,	CK_242X),
	CLK(NULL,	"vlynq_fck",	&vlynq_fck,	CK_242X),
	CLK(NULL,	"sdrc_ick",	&sdrc_ick,	CK_243X),
	CLK(NULL,	"des_ick",	&des_ick,	CK_243X | CK_242X),
	CLK(NULL,	"sha_ick",	&sha_ick,	CK_243X | CK_242X),
	CLK("omap_rng",	"ick",		&rng_ick,	CK_243X | CK_242X),
	CLK(NULL,	"aes_ick",	&aes_ick,	CK_243X | CK_242X),
	CLK(NULL,	"pka_ick",	&pka_ick,	CK_243X | CK_242X),
	CLK(NULL,	"usb_fck",	&usb_fck,	CK_243X | CK_242X),
	CLK("musb_hdrc",	"ick",	&usbhs_ick,	CK_243X),
	CLK("mmci-omap-hs.0", "ick",	&mmchs1_ick,	CK_243X),
	CLK("mmci-omap-hs.0", "fck",	&mmchs1_fck,	CK_243X),
	CLK("mmci-omap-hs.1", "ick",	&mmchs2_ick,	CK_243X),
	CLK("mmci-omap-hs.1", "fck",	&mmchs2_fck,	CK_243X),
	CLK(NULL,	"gpio5_ick",	&gpio5_ick,	CK_243X),
	CLK(NULL,	"gpio5_fck",	&gpio5_fck,	CK_243X),
	CLK(NULL,	"mdm_intc_ick",	&mdm_intc_ick,	CK_243X),
	CLK("mmci-omap-hs.0", "mmchsdb_fck",	&mmchsdb1_fck,	CK_243X),
	CLK("mmci-omap-hs.1", "mmchsdb_fck", 	&mmchsdb2_fck,	CK_243X),
};

/* CM_CLKEN_PLL.EN_{54,96}M_PLL options (24XX) */
#define EN_APLL_STOPPED			0
#define EN_APLL_LOCKED			3

/* CM_CLKSEL1_PLL.APLLS_CLKIN options (24XX) */
#define APLLS_CLKIN_19_2MHZ		0
#define APLLS_CLKIN_13MHZ		2
#define APLLS_CLKIN_12MHZ		3

/* #define DOWN_VARIABLE_DPLL 1 */		/* Experimental */

static struct prcm_config *curr_prcm_set;
static struct clk *vclk;
static struct clk *sclk;

static void __iomem *prcm_clksrc_ctrl;

/*-------------------------------------------------------------------------
 * Omap24xx specific clock functions
 *-------------------------------------------------------------------------*/

/**
 * omap2xxx_clk_get_core_rate - return the CORE_CLK rate
 * @clk: pointer to the combined dpll_ck + core_ck (currently "dpll_ck")
 *
 * Returns the CORE_CLK rate.  CORE_CLK can have one of three rate
 * sources on OMAP2xxx: the DPLL CLKOUT rate, DPLL CLKOUTX2, or 32KHz
 * (the latter is unusual).  This currently should be called with
 * struct clk *dpll_ck, which is a composite clock of dpll_ck and
 * core_ck.
 */
static unsigned long omap2xxx_clk_get_core_rate(struct clk *clk)
{
	long long core_clk;
	u32 v;

	core_clk = omap2_get_dpll_rate(clk);

	v = cm_read_mod_reg(PLL_MOD, CM_CLKSEL2);
	v &= OMAP24XX_CORE_CLK_SRC_MASK;

	if (v == CORE_CLK_SRC_32K)
		core_clk = 32768;
	else
		core_clk *= v;

	return core_clk;
}

static int omap2_enable_osc_ck(struct clk *clk)
{
	u32 pcc;

	pcc = __raw_readl(prcm_clksrc_ctrl);

	__raw_writel(pcc & ~OMAP_AUTOEXTCLKMODE_MASK, prcm_clksrc_ctrl);

	return 0;
}

static void omap2_disable_osc_ck(struct clk *clk)
{
	u32 pcc;

	pcc = __raw_readl(prcm_clksrc_ctrl);

	__raw_writel(pcc | OMAP_AUTOEXTCLKMODE_MASK, prcm_clksrc_ctrl);
}

static const struct clkops clkops_oscck = {
	.enable		= &omap2_enable_osc_ck,
	.disable	= &omap2_disable_osc_ck,
};

#ifdef OLD_CK
/* Recalculate SYST_CLK */
static void omap2_sys_clk_recalc(struct clk * clk)
{
	u32 div = PRCM_CLKSRC_CTRL;
	div &= (1 << 7) | (1 << 6);	/* Test if ext clk divided by 1 or 2 */
	div >>= clk->rate_offset;
	clk->rate = (clk->parent->rate / div);
	propagate_rate(clk);
}
#endif	/* OLD_CK */

/* Enable an APLL if off */
static int omap2_clk_fixed_enable(struct clk *clk)
{
	u32 cval, apll_mask;

	apll_mask = EN_APLL_LOCKED << clk->enable_bit;

	cval = cm_read_mod_reg(PLL_MOD, CM_CLKEN);

	if ((cval & apll_mask) == apll_mask)
		return 0;   /* apll already enabled */

	cval &= ~apll_mask;
	cval |= apll_mask;
	cm_write_mod_reg(cval, PLL_MOD, CM_CLKEN);

	if (clk == &apll96_ck)
		cval = OMAP24XX_ST_96M_APLL;
	else if (clk == &apll54_ck)
		cval = OMAP24XX_ST_54M_APLL;

	omap2_wait_clock_ready(OMAP_CM_REGADDR(PLL_MOD, CM_IDLEST), cval,
			    clk->name);

	/*
	 * REVISIT: Should we return an error code if omap2_wait_clock_ready()
	 * fails?
	 */
	return 0;
}

/* Stop APLL */
static void omap2_clk_fixed_disable(struct clk *clk)
{
	u32 cval;

	cval = cm_read_mod_reg(PLL_MOD, CM_CLKEN);
	cval &= ~(EN_APLL_LOCKED << clk->enable_bit);
	cm_write_mod_reg(cval, PLL_MOD, CM_CLKEN);
}

static const struct clkops clkops_fixed = {
	.enable		= &omap2_clk_fixed_enable,
	.disable	= &omap2_clk_fixed_disable,
};

/*
 * Uses the current prcm set to tell if a rate is valid.
 * You can go slower, but not faster within a given rate set.
 */
static long omap2_dpllcore_round_rate(unsigned long target_rate)
{
	u32 high, low, core_clk_src;

	core_clk_src = cm_read_mod_reg(PLL_MOD, CM_CLKSEL2);
	core_clk_src &= OMAP24XX_CORE_CLK_SRC_MASK;

	if (core_clk_src == CORE_CLK_SRC_DPLL) {	/* DPLL clockout */
		high = curr_prcm_set->dpll_speed * 2;
		low = curr_prcm_set->dpll_speed;
	} else {				/* DPLL clockout x 2 */
		high = curr_prcm_set->dpll_speed;
		low = curr_prcm_set->dpll_speed / 2;
	}

#ifdef DOWN_VARIABLE_DPLL
	if (target_rate > high)
		return high;
	else
		return target_rate;
#else
	if (target_rate > low)
		return high;
	else
		return low;
#endif

}

static unsigned long omap2_dpllcore_recalc(struct clk *clk)
{
	return omap2xxx_clk_get_core_rate(clk);
}

static int omap2_reprogram_dpllcore(struct clk *clk, unsigned long rate)
{
	u32 cur_rate, low, mult, div, valid_rate, done_rate;
	u32 bypass = 0;
	struct prcm_config tmpset;
	const struct dpll_data *dd;

	cur_rate = omap2xxx_clk_get_core_rate(&dpll_ck);
	mult = cm_read_mod_reg(PLL_MOD, CM_CLKSEL2);
	mult &= OMAP24XX_CORE_CLK_SRC_MASK;

	if ((rate == (cur_rate / 2)) && (mult == 2)) {
		omap2xxx_sdrc_reprogram(CORE_CLK_SRC_DPLL, 1);
	} else if ((rate == (cur_rate * 2)) && (mult == 1)) {
		omap2xxx_sdrc_reprogram(CORE_CLK_SRC_DPLL_X2, 1);
	} else if (rate != cur_rate) {
		valid_rate = omap2_dpllcore_round_rate(rate);
		if (valid_rate != rate)
			return -EINVAL;

		if (mult == 1)
			low = curr_prcm_set->dpll_speed;
		else
			low = curr_prcm_set->dpll_speed / 2;

		dd = clk->dpll_data;
		if (!dd)
			return -EINVAL;

		tmpset.cm_clksel1_pll = __raw_readl(dd->mult_div1_reg);
		tmpset.cm_clksel1_pll &= ~(dd->mult_mask |
					   dd->div1_mask);
		div = ((curr_prcm_set->xtal_speed / 1000000) - 1);
		tmpset.cm_clksel2_pll = cm_read_mod_reg(PLL_MOD, CM_CLKSEL2);
		tmpset.cm_clksel2_pll &= ~OMAP24XX_CORE_CLK_SRC_MASK;
		if (rate > low) {
			tmpset.cm_clksel2_pll |= CORE_CLK_SRC_DPLL_X2;
			mult = ((rate / 2) / 1000000);
			done_rate = CORE_CLK_SRC_DPLL_X2;
		} else {
			tmpset.cm_clksel2_pll |= CORE_CLK_SRC_DPLL;
			mult = (rate / 1000000);
			done_rate = CORE_CLK_SRC_DPLL;
		}
		tmpset.cm_clksel1_pll |= (div << __ffs(dd->mult_mask));
		tmpset.cm_clksel1_pll |= (mult << __ffs(dd->div1_mask));

		/* Worst case */
		tmpset.base_sdrc_rfr = SDRC_RFR_CTRL_BYPASS;

		if (rate == curr_prcm_set->xtal_speed)	/* If asking for 1-1 */
			bypass = 1;

		/* For omap2xxx_sdrc_init_params() */
		omap2xxx_sdrc_reprogram(CORE_CLK_SRC_DPLL_X2, 1);

		/* Force dll lock mode */
		omap2_set_prcm(tmpset.cm_clksel1_pll, tmpset.base_sdrc_rfr,
			       bypass);

		/* Errata: ret dll entry state */
		omap2xxx_sdrc_init_params(omap2xxx_sdrc_dll_is_unlocked());
		omap2xxx_sdrc_reprogram(done_rate, 0);
	}

	return 0;
}

/**
 * omap2_table_mpu_recalc - just return the MPU speed
 * @clk: virt_prcm_set struct clk
 *
 * Set virt_prcm_set's rate to the mpu_speed field of the current PRCM set.
 */
static unsigned long omap2_table_mpu_recalc(struct clk *clk)
{
	return curr_prcm_set->mpu_speed;
}

/*
 * Look for a rate equal or less than the target rate given a configuration set.
 *
 * What's not entirely clear is "which" field represents the key field.
 * Some might argue L3-DDR, others ARM, others IVA. This code is simple and
 * just uses the ARM rates.
 */
static long omap2_round_to_table_rate(struct clk *clk, unsigned long rate)
{
	struct prcm_config *ptr;
	long highest_rate;

	if (clk != &virt_prcm_set)
		return -EINVAL;

	highest_rate = -EINVAL;

	for (ptr = rate_table; ptr->mpu_speed; ptr++) {
		if (!(ptr->flags & cpu_mask))
			continue;
		if (ptr->xtal_speed != sys_ck.rate)
			continue;

		highest_rate = ptr->mpu_speed;

		/* Can check only after xtal frequency check */
		if (ptr->mpu_speed <= rate)
			break;
	}
	return highest_rate;
}

/* Sets basic clocks based on the specified rate */
static int omap2_select_table_rate(struct clk *clk, unsigned long rate)
{
	u32 cur_rate, done_rate, bypass = 0, tmp;
	struct prcm_config *prcm;
	unsigned long found_speed = 0;
	unsigned long flags;

	if (clk != &virt_prcm_set)
		return -EINVAL;

	for (prcm = rate_table; prcm->mpu_speed; prcm++) {
		if (!(prcm->flags & cpu_mask))
			continue;

		if (prcm->xtal_speed != sys_ck.rate)
			continue;

		if (prcm->mpu_speed <= rate) {
			found_speed = prcm->mpu_speed;
			break;
		}
	}

	if (!found_speed) {
		printk(KERN_INFO "Could not set MPU rate to %luMHz\n",
		       rate / 1000000);
		return -EINVAL;
	}

	curr_prcm_set = prcm;
	cur_rate = omap2xxx_clk_get_core_rate(&dpll_ck);

	if (prcm->dpll_speed == cur_rate / 2) {
		omap2xxx_sdrc_reprogram(CORE_CLK_SRC_DPLL, 1);
	} else if (prcm->dpll_speed == cur_rate * 2) {
		omap2xxx_sdrc_reprogram(CORE_CLK_SRC_DPLL_X2, 1);
	} else if (prcm->dpll_speed != cur_rate) {
		local_irq_save(flags);

		if (prcm->dpll_speed == prcm->xtal_speed)
			bypass = 1;

		if ((prcm->cm_clksel2_pll & OMAP24XX_CORE_CLK_SRC_MASK) ==
		    CORE_CLK_SRC_DPLL_X2)
			done_rate = CORE_CLK_SRC_DPLL_X2;
		else
			done_rate = CORE_CLK_SRC_DPLL;

		/* MPU divider */
		cm_write_mod_reg(prcm->cm_clksel_mpu, MPU_MOD, CM_CLKSEL);

		/* dsp + iva1 div(2420), iva2.1(2430) */
		cm_write_mod_reg(prcm->cm_clksel_dsp,
				 OMAP24XX_DSP_MOD, CM_CLKSEL);

		cm_write_mod_reg(prcm->cm_clksel_gfx, GFX_MOD, CM_CLKSEL);

		/* Major subsystem dividers */
		tmp = cm_read_mod_reg(CORE_MOD, CM_CLKSEL1) & OMAP24XX_CLKSEL_DSS2_MASK;
		cm_write_mod_reg(prcm->cm_clksel1_core | tmp, CORE_MOD,
				 CM_CLKSEL1);

		if (cpu_is_omap2430())
			cm_write_mod_reg(prcm->cm_clksel_mdm,
					 OMAP2430_MDM_MOD, CM_CLKSEL);

		/* x2 to enter omap2xxx_sdrc_init_params() */
		omap2xxx_sdrc_reprogram(CORE_CLK_SRC_DPLL_X2, 1);

		omap2_set_prcm(prcm->cm_clksel1_pll, prcm->base_sdrc_rfr,
			       bypass);

		omap2xxx_sdrc_init_params(omap2xxx_sdrc_dll_is_unlocked());
		omap2xxx_sdrc_reprogram(done_rate, 0);

		local_irq_restore(flags);
	}

	return 0;
}

#ifdef CONFIG_CPU_FREQ
/*
 * Walk PRCM rate table and fillout cpufreq freq_table
 */
static struct cpufreq_frequency_table freq_table[ARRAY_SIZE(rate_table)];

void omap2_clk_init_cpufreq_table(struct cpufreq_frequency_table **table)
{
	struct prcm_config *prcm;
	int i = 0;

	for (prcm = rate_table; prcm->mpu_speed; prcm++) {
		if (!(prcm->flags & cpu_mask))
			continue;
		if (prcm->xtal_speed != sys_ck.rate)
			continue;

		/* don't put bypass rates in table */
		if (prcm->dpll_speed == prcm->xtal_speed)
			continue;

		freq_table[i].index = i;
		freq_table[i].frequency = prcm->mpu_speed / 1000;
		i++;
	}

	if (i == 0) {
		printk(KERN_WARNING "%s: failed to initialize frequency "
		       "table\n", __func__);
		return;
	}

	freq_table[i].index = i;
	freq_table[i].frequency = CPUFREQ_TABLE_END;

	*table = &freq_table[0];
}
#endif

static struct clk_functions omap2_clk_functions = {
	.clk_enable		= omap2_clk_enable,
	.clk_disable		= omap2_clk_disable,
	.clk_round_rate		= omap2_clk_round_rate,
	.clk_set_rate		= omap2_clk_set_rate,
	.clk_set_parent		= omap2_clk_set_parent,
	.clk_disable_unused	= omap2_clk_disable_unused,
#ifdef	CONFIG_CPU_FREQ
	.clk_init_cpufreq_table	= omap2_clk_init_cpufreq_table,
#endif
};

static u32 omap2_get_apll_clkin(void)
{
	u32 aplls, srate = 0;

	aplls = cm_read_mod_reg(PLL_MOD, CM_CLKSEL1);
	aplls &= OMAP24XX_APLLS_CLKIN_MASK;
	aplls >>= OMAP24XX_APLLS_CLKIN_SHIFT;

	if (aplls == APLLS_CLKIN_19_2MHZ)
		srate = 19200000;
	else if (aplls == APLLS_CLKIN_13MHZ)
		srate = 13000000;
	else if (aplls == APLLS_CLKIN_12MHZ)
		srate = 12000000;

	return srate;
}

static u32 omap2_get_sysclkdiv(void)
{
	u32 div;

	div = __raw_readl(prcm_clksrc_ctrl);
	div &= OMAP_SYSCLKDIV_MASK;
	div >>= OMAP_SYSCLKDIV_SHIFT;

	return div;
}

static unsigned long omap2_osc_clk_recalc(struct clk *clk)
{
	return omap2_get_apll_clkin() * omap2_get_sysclkdiv();
}

static unsigned long omap2_sys_clk_recalc(struct clk *clk)
{
	return clk->parent->rate / omap2_get_sysclkdiv();
}

/*
 * Set clocks for bypass mode for reboot to work.
 */
void omap2_clk_prepare_for_reboot(void)
{
	u32 rate;

	if (vclk == NULL || sclk == NULL)
		return;

	rate = clk_get_rate(sclk);
	clk_set_rate(vclk, rate);
}

/*
 * Switch the MPU rate if specified on cmdline.
 * We cannot do this early until cmdline is parsed.
 */
static int __init omap2_clk_arch_init(void)
{
	if (!mpurate)
		return -EINVAL;

	if (clk_set_rate(&virt_prcm_set, mpurate))
		printk(KERN_ERR "Could not find matching MPU rate\n");

	recalculate_root_clocks();

	printk(KERN_INFO "Switched to new clocking rate (Crystal/DPLL/MPU): "
	       "%ld.%01ld/%ld/%ld MHz\n",
	       (sys_ck.rate / 1000000), (sys_ck.rate / 100000) % 10,
	       (dpll_ck.rate / 1000000), (mpu_ck.rate / 1000000)) ;

	return 0;
}
arch_initcall(omap2_clk_arch_init);

int __init omap2_clk_init(void)
{
	struct prcm_config *prcm;
	struct omap_clk *c;
	u32 clkrate;

	if (cpu_is_omap242x()) {
		prcm_clksrc_ctrl = OMAP2420_PRCM_CLKSRC_CTRL;
		cpu_mask = RATE_IN_242X;
	} else if (cpu_is_omap2430()) {
		prcm_clksrc_ctrl = OMAP2430_PRCM_CLKSRC_CTRL;
		cpu_mask = RATE_IN_243X;
	}

	clk_init(&omap2_clk_functions);

	for (c = omap24xx_clks; c < omap24xx_clks + ARRAY_SIZE(omap24xx_clks); c++)
		clk_preinit(c->lk.clk);

	osc_ck.rate = omap2_osc_clk_recalc(&osc_ck);
	propagate_rate(&osc_ck);
	sys_ck.rate = omap2_sys_clk_recalc(&sys_ck);
	propagate_rate(&sys_ck);

	for (c = omap24xx_clks; c < omap24xx_clks + ARRAY_SIZE(omap24xx_clks); c++)
		if (c->cpu & cpu_mask) {
			clkdev_add(&c->lk);
			clk_register(c->lk.clk);
		}

	/* Check the MPU rate set by bootloader */
	clkrate = omap2xxx_clk_get_core_rate(&dpll_ck);
	for (prcm = rate_table; prcm->mpu_speed; prcm++) {
		if (!(prcm->flags & cpu_mask))
			continue;
		if (prcm->xtal_speed != sys_ck.rate)
			continue;
		if (prcm->dpll_speed <= clkrate)
			 break;
	}
	curr_prcm_set = prcm;

	recalculate_root_clocks();

	printk(KERN_INFO "Clocking rate (Crystal/DPLL/MPU): "
	       "%ld.%01ld/%ld/%ld MHz\n",
	       (sys_ck.rate / 1000000), (sys_ck.rate / 100000) % 10,
	       (dpll_ck.rate / 1000000), (mpu_ck.rate / 1000000)) ;

	/*
	 * Only enable those clocks we will need, let the drivers
	 * enable other clocks as necessary
	 */
	clk_enable_init_clocks();

	/* Avoid sleeping sleeping during omap2_clk_prepare_for_reboot() */
	vclk = clk_get(NULL, "virt_prcm_set");
	sclk = clk_get(NULL, "sys_ck");

	return 0;
}
