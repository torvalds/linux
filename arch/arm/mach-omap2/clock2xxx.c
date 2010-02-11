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

#include <plat/clock.h>
#include <plat/sram.h>
#include <plat/prcm.h>
#include <plat/clkdev_omap.h>
#include <asm/div64.h>
#include <asm/clkdev.h>

#include <plat/sdrc.h>
#include "clock.h"
#include "clock2xxx.h"
#include "opp2xxx.h"
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

const struct prcm_config *curr_prcm_set;
const struct prcm_config *rate_table;

struct clk *vclk, *sclk, *dclk;

void __iomem *prcm_clksrc_ctrl;

/*-------------------------------------------------------------------------
 * Omap24xx specific clock functions
 *-------------------------------------------------------------------------*/

/**
 * omap2430_clk_i2chs_find_idlest - return CM_IDLEST info for 2430 I2CHS
 * @clk: struct clk * being enabled
 * @idlest_reg: void __iomem ** to store CM_IDLEST reg address into
 * @idlest_bit: pointer to a u8 to store the CM_IDLEST bit shift into
 *
 * OMAP2430 I2CHS CM_IDLEST bits are in CM_IDLEST1_CORE, but the
 * CM_*CLKEN bits are in CM_{I,F}CLKEN2_CORE.  This custom function
 * passes back the correct CM_IDLEST register address for I2CHS
 * modules.  No return value.
 */
static void omap2430_clk_i2chs_find_idlest(struct clk *clk,
					   void __iomem **idlest_reg,
					   u8 *idlest_bit)
{
	*idlest_reg = OMAP_CM_REGADDR(CORE_MOD, CM_IDLEST);
	*idlest_bit = clk->enable_bit;
}

/* 2430 I2CHS has non-standard IDLEST register */
const struct clkops clkops_omap2430_i2chs_wait = {
	.enable		= omap2_dflt_clk_enable,
	.disable	= omap2_dflt_clk_disable,
	.find_idlest	= omap2430_clk_i2chs_find_idlest,
	.find_companion = omap2_clk_dflt_find_companion,
};

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
unsigned long omap2xxx_clk_get_core_rate(struct clk *clk)
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

const struct clkops clkops_oscck = {
	.enable		= omap2_enable_osc_ck,
	.disable	= omap2_disable_osc_ck,
};

#ifdef OLD_CK
/* Recalculate SYST_CLK */
static void omap2_sys_clk_recalc(struct clk *clk)
{
	u32 div = PRCM_CLKSRC_CTRL;
	div &= (1 << 7) | (1 << 6);	/* Test if ext clk divided by 1 or 2 */
	div >>= clk->rate_offset;
	clk->rate = (clk->parent->rate / div);
	propagate_rate(clk);
}
#endif	/* OLD_CK */

/* Enable an APLL if off */
static int omap2_clk_apll_enable(struct clk *clk, u32 status_mask)
{
	u32 cval, apll_mask;

	apll_mask = EN_APLL_LOCKED << clk->enable_bit;

	cval = cm_read_mod_reg(PLL_MOD, CM_CLKEN);

	if ((cval & apll_mask) == apll_mask)
		return 0;   /* apll already enabled */

	cval &= ~apll_mask;
	cval |= apll_mask;
	cm_write_mod_reg(cval, PLL_MOD, CM_CLKEN);

	omap2_cm_wait_idlest(OMAP_CM_REGADDR(PLL_MOD, CM_IDLEST), status_mask,
			     clk->name);

	/*
	 * REVISIT: Should we return an error code if omap2_wait_clock_ready()
	 * fails?
	 */
	return 0;
}

static int omap2_clk_apll96_enable(struct clk *clk)
{
	return omap2_clk_apll_enable(clk, OMAP24XX_ST_96M_APLL);
}

static int omap2_clk_apll54_enable(struct clk *clk)
{
	return omap2_clk_apll_enable(clk, OMAP24XX_ST_54M_APLL);
}

/* Stop APLL */
static void omap2_clk_apll_disable(struct clk *clk)
{
	u32 cval;

	cval = cm_read_mod_reg(PLL_MOD, CM_CLKEN);
	cval &= ~(EN_APLL_LOCKED << clk->enable_bit);
	cm_write_mod_reg(cval, PLL_MOD, CM_CLKEN);
}

const struct clkops clkops_apll96 = {
	.enable		= omap2_clk_apll96_enable,
	.disable	= omap2_clk_apll_disable,
};

const struct clkops clkops_apll54 = {
	.enable		= omap2_clk_apll54_enable,
	.disable	= omap2_clk_apll_disable,
};

/*
 * Uses the current prcm set to tell if a rate is valid.
 * You can go slower, but not faster within a given rate set.
 */
long omap2_dpllcore_round_rate(unsigned long target_rate)
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

unsigned long omap2_dpllcore_recalc(struct clk *clk)
{
	return omap2xxx_clk_get_core_rate(clk);
}

int omap2_reprogram_dpllcore(struct clk *clk, unsigned long rate)
{
	u32 cur_rate, low, mult, div, valid_rate, done_rate;
	u32 bypass = 0;
	struct prcm_config tmpset;
	const struct dpll_data *dd;

	cur_rate = omap2xxx_clk_get_core_rate(dclk);
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
unsigned long omap2_table_mpu_recalc(struct clk *clk)
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
long omap2_round_to_table_rate(struct clk *clk, unsigned long rate)
{
	const struct prcm_config *ptr;
	long highest_rate;
	long sys_ck_rate;

	sys_ck_rate = clk_get_rate(sclk);

	highest_rate = -EINVAL;

	for (ptr = rate_table; ptr->mpu_speed; ptr++) {
		if (!(ptr->flags & cpu_mask))
			continue;
		if (ptr->xtal_speed != sys_ck_rate)
			continue;

		highest_rate = ptr->mpu_speed;

		/* Can check only after xtal frequency check */
		if (ptr->mpu_speed <= rate)
			break;
	}
	return highest_rate;
}

/* Sets basic clocks based on the specified rate */
int omap2_select_table_rate(struct clk *clk, unsigned long rate)
{
	u32 cur_rate, done_rate, bypass = 0, tmp;
	const struct prcm_config *prcm;
	unsigned long found_speed = 0;
	unsigned long flags;
	long sys_ck_rate;

	sys_ck_rate = clk_get_rate(sclk);

	for (prcm = rate_table; prcm->mpu_speed; prcm++) {
		if (!(prcm->flags & cpu_mask))
			continue;

		if (prcm->xtal_speed != sys_ck_rate)
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
	cur_rate = omap2xxx_clk_get_core_rate(dclk);

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
 * XXX This should be replaced by an OPP layer in the near future
 */
static struct cpufreq_frequency_table *freq_table;

void omap2_clk_init_cpufreq_table(struct cpufreq_frequency_table **table)
{
	const struct prcm_config *prcm;
	long sys_ck_rate;
	int i = 0;
	int tbl_sz = 0;

	sys_ck_rate = clk_get_rate(sclk);

	for (prcm = rate_table; prcm->mpu_speed; prcm++) {
		if (!(prcm->flags & cpu_mask))
			continue;
		if (prcm->xtal_speed != sys_ck_rate)
			continue;

		/* don't put bypass rates in table */
		if (prcm->dpll_speed == prcm->xtal_speed)
			continue;

		tbl_sz++;
	}

	/*
	 * XXX Ensure that we're doing what CPUFreq expects for this error
	 * case and the following one
	 */
	if (tbl_sz == 0) {
		pr_warning("%s: no matching entries in rate_table\n",
			   __func__);
		return;
	}

	/* Include the CPUFREQ_TABLE_END terminator entry */
	tbl_sz++;

	freq_table = kzalloc(sizeof(struct cpufreq_frequency_table) * tbl_sz,
			     GFP_ATOMIC);
	if (!freq_table) {
		pr_err("%s: could not kzalloc frequency table\n", __func__);
		return;
	}

	for (prcm = rate_table; prcm->mpu_speed; prcm++) {
		if (!(prcm->flags & cpu_mask))
			continue;
		if (prcm->xtal_speed != sys_ck_rate)
			continue;

		/* don't put bypass rates in table */
		if (prcm->dpll_speed == prcm->xtal_speed)
			continue;

		freq_table[i].index = i;
		freq_table[i].frequency = prcm->mpu_speed / 1000;
		i++;
	}

	freq_table[i].index = i;
	freq_table[i].frequency = CPUFREQ_TABLE_END;

	*table = &freq_table[0];
}

void omap2_clk_exit_cpufreq_table(struct cpufreq_frequency_table **table)
{
	kfree(freq_table);
}

#endif

struct clk_functions omap2_clk_functions = {
	.clk_enable		= omap2_clk_enable,
	.clk_disable		= omap2_clk_disable,
	.clk_round_rate		= omap2_clk_round_rate,
	.clk_set_rate		= omap2_clk_set_rate,
	.clk_set_parent		= omap2_clk_set_parent,
	.clk_disable_unused	= omap2_clk_disable_unused,
#ifdef	CONFIG_CPU_FREQ
	.clk_init_cpufreq_table	= omap2_clk_init_cpufreq_table,
	.clk_exit_cpufreq_table	= omap2_clk_exit_cpufreq_table,
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

unsigned long omap2_osc_clk_recalc(struct clk *clk)
{
	return omap2_get_apll_clkin() * omap2_get_sysclkdiv();
}

unsigned long omap2_sys_clk_recalc(struct clk *clk)
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
	struct clk *virt_prcm_set, *sys_ck, *dpll_ck, *mpu_ck;
	unsigned long sys_ck_rate;

	if (!mpurate)
		return -EINVAL;

	virt_prcm_set = clk_get(NULL, "virt_prcm_set");
	sys_ck = clk_get(NULL, "sys_ck");
	dpll_ck = clk_get(NULL, "dpll_ck");
	mpu_ck = clk_get(NULL, "mpu_ck");

	if (clk_set_rate(virt_prcm_set, mpurate))
		printk(KERN_ERR "Could not find matching MPU rate\n");

	recalculate_root_clocks();

	sys_ck_rate = clk_get_rate(sys_ck);

	pr_info("Switched to new clocking rate (Crystal/DPLL/MPU): "
		"%ld.%01ld/%ld/%ld MHz\n",
		(sys_ck_rate / 1000000), (sys_ck_rate / 100000) % 10,
		(clk_get_rate(dpll_ck) / 1000000),
		(clk_get_rate(mpu_ck) / 1000000));

	return 0;
}
arch_initcall(omap2_clk_arch_init);


