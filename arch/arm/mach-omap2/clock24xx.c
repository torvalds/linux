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

#include <asm/arch/clock.h>
#include <asm/arch/sram.h>
#include <asm/div64.h>
#include <asm/bitops.h>

#include "memory.h"
#include "clock.h"
#include "clock24xx.h"
#include "prm.h"
#include "prm-regbits-24xx.h"
#include "cm.h"
#include "cm-regbits-24xx.h"

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

/*-------------------------------------------------------------------------
 * Omap24xx specific clock functions
 *-------------------------------------------------------------------------*/

/* This actually returns the rate of core_ck, not dpll_ck. */
static u32 omap2_get_dpll_rate_24xx(struct clk *tclk)
{
	long long dpll_clk;
	u8 amult;

	dpll_clk = omap2_get_dpll_rate(tclk);

	amult = cm_read_mod_reg(PLL_MOD, CM_CLKSEL2);
	amult &= OMAP24XX_CORE_CLK_SRC_MASK;
	dpll_clk *= amult;

	return dpll_clk;
}

static int omap2_enable_osc_ck(struct clk *clk)
{
	u32 pcc;

	pcc = __raw_readl(OMAP24XX_PRCM_CLKSRC_CTRL);

	__raw_writel(pcc & ~OMAP_AUTOEXTCLKMODE_MASK,
		      OMAP24XX_PRCM_CLKSRC_CTRL);

	return 0;
}

static void omap2_disable_osc_ck(struct clk *clk)
{
	u32 pcc;

	pcc = __raw_readl(OMAP24XX_PRCM_CLKSRC_CTRL);

	__raw_writel(pcc | OMAP_AUTOEXTCLKMODE_MASK,
		      OMAP24XX_PRCM_CLKSRC_CTRL);
}

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

/*
 * Uses the current prcm set to tell if a rate is valid.
 * You can go slower, but not faster within a given rate set.
 */
static u32 omap2_dpll_round_rate(unsigned long target_rate)
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

static void omap2_dpll_recalc(struct clk *clk)
{
	clk->rate = omap2_get_dpll_rate_24xx(clk);

	propagate_rate(clk);
}

static int omap2_reprogram_dpll(struct clk *clk, unsigned long rate)
{
	u32 cur_rate, low, mult, div, valid_rate, done_rate;
	u32 bypass = 0;
	struct prcm_config tmpset;
	const struct dpll_data *dd;
	unsigned long flags;
	int ret = -EINVAL;

	local_irq_save(flags);
	cur_rate = omap2_get_dpll_rate_24xx(&dpll_ck);
	mult = cm_read_mod_reg(PLL_MOD, CM_CLKSEL2);
	mult &= OMAP24XX_CORE_CLK_SRC_MASK;

	if ((rate == (cur_rate / 2)) && (mult == 2)) {
		omap2_reprogram_sdrc(CORE_CLK_SRC_DPLL, 1);
	} else if ((rate == (cur_rate * 2)) && (mult == 1)) {
		omap2_reprogram_sdrc(CORE_CLK_SRC_DPLL_X2, 1);
	} else if (rate != cur_rate) {
		valid_rate = omap2_dpll_round_rate(rate);
		if (valid_rate != rate)
			goto dpll_exit;

		if (mult == 1)
			low = curr_prcm_set->dpll_speed;
		else
			low = curr_prcm_set->dpll_speed / 2;

		dd = clk->dpll_data;
		if (!dd)
			goto dpll_exit;

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

		omap2_reprogram_sdrc(CORE_CLK_SRC_DPLL_X2, 1); /* For init_mem */

		/* Force dll lock mode */
		omap2_set_prcm(tmpset.cm_clksel1_pll, tmpset.base_sdrc_rfr,
			       bypass);

		/* Errata: ret dll entry state */
		omap2_init_memory_params(omap2_dll_force_needed());
		omap2_reprogram_sdrc(done_rate, 0);
	}
	omap2_dpll_recalc(&dpll_ck);
	ret = 0;

dpll_exit:
	local_irq_restore(flags);
	return(ret);
}

/**
 * omap2_table_mpu_recalc - just return the MPU speed
 * @clk: virt_prcm_set struct clk
 *
 * Set virt_prcm_set's rate to the mpu_speed field of the current PRCM set.
 */
static void omap2_table_mpu_recalc(struct clk *clk)
{
	clk->rate = curr_prcm_set->mpu_speed;
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
	cur_rate = omap2_get_dpll_rate_24xx(&dpll_ck);

	if (prcm->dpll_speed == cur_rate / 2) {
		omap2_reprogram_sdrc(CORE_CLK_SRC_DPLL, 1);
	} else if (prcm->dpll_speed == cur_rate * 2) {
		omap2_reprogram_sdrc(CORE_CLK_SRC_DPLL_X2, 1);
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
		cm_write_mod_reg(prcm->cm_clksel1_core | tmp, CORE_MOD, CM_CLKSEL1);
		if (cpu_is_omap2430())
			cm_write_mod_reg(prcm->cm_clksel_mdm,
					 OMAP2430_MDM_MOD, CM_CLKSEL);

		/* x2 to enter init_mem */
		omap2_reprogram_sdrc(CORE_CLK_SRC_DPLL_X2, 1);

		omap2_set_prcm(prcm->cm_clksel1_pll, prcm->base_sdrc_rfr,
			       bypass);

		omap2_init_memory_params(omap2_dll_force_needed());
		omap2_reprogram_sdrc(done_rate, 0);

		local_irq_restore(flags);
	}
	omap2_dpll_recalc(&dpll_ck);

	return 0;
}

static struct clk_functions omap2_clk_functions = {
	.clk_enable		= omap2_clk_enable,
	.clk_disable		= omap2_clk_disable,
	.clk_round_rate		= omap2_clk_round_rate,
	.clk_set_rate		= omap2_clk_set_rate,
	.clk_set_parent		= omap2_clk_set_parent,
	.clk_disable_unused	= omap2_clk_disable_unused,
};

static u32 omap2_get_apll_clkin(void)
{
	u32 aplls, sclk = 0;

	aplls = cm_read_mod_reg(PLL_MOD, CM_CLKSEL1);
	aplls &= OMAP24XX_APLLS_CLKIN_MASK;
	aplls >>= OMAP24XX_APLLS_CLKIN_SHIFT;

	if (aplls == APLLS_CLKIN_19_2MHZ)
		sclk = 19200000;
	else if (aplls == APLLS_CLKIN_13MHZ)
		sclk = 13000000;
	else if (aplls == APLLS_CLKIN_12MHZ)
		sclk = 12000000;

	return sclk;
}

static u32 omap2_get_sysclkdiv(void)
{
	u32 div;

	div = __raw_readl(OMAP24XX_PRCM_CLKSRC_CTRL);
	div &= OMAP_SYSCLKDIV_MASK;
	div >>= OMAP_SYSCLKDIV_SHIFT;

	return div;
}

static void omap2_osc_clk_recalc(struct clk *clk)
{
	clk->rate = omap2_get_apll_clkin() * omap2_get_sysclkdiv();
	propagate_rate(clk);
}

static void omap2_sys_clk_recalc(struct clk *clk)
{
	clk->rate = clk->parent->rate / omap2_get_sysclkdiv();
	propagate_rate(clk);
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

	if (omap2_select_table_rate(&virt_prcm_set, mpurate))
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
	struct clk **clkp;
	u32 clkrate;

	if (cpu_is_omap242x())
		cpu_mask = RATE_IN_242X;
	else if (cpu_is_omap2430())
		cpu_mask = RATE_IN_243X;

	clk_init(&omap2_clk_functions);

	omap2_osc_clk_recalc(&osc_ck);
	omap2_sys_clk_recalc(&sys_ck);

	for (clkp = onchip_24xx_clks;
	     clkp < onchip_24xx_clks + ARRAY_SIZE(onchip_24xx_clks);
	     clkp++) {

		if ((*clkp)->flags & CLOCK_IN_OMAP242X && cpu_is_omap2420()) {
			clk_register(*clkp);
			continue;
		}

		if ((*clkp)->flags & CLOCK_IN_OMAP243X && cpu_is_omap2430()) {
			clk_register(*clkp);
			continue;
		}
	}

	/* Check the MPU rate set by bootloader */
	clkrate = omap2_get_dpll_rate_24xx(&dpll_ck);
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
