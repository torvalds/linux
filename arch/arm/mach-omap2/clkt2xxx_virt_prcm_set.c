/*
 * OMAP2xxx DVFS virtual clock functions
 *
 * Copyright (C) 2005-2008 Texas Instruments, Inc.
 * Copyright (C) 2004-2010 Nokia Corporation
 *
 * Contacts:
 * Richard Woodruff <r-woodruff2@ti.com>
 * Paul Walmsley
 *
 * Based on earlier work by Tuukka Tikkanen, Tony Lindgren,
 * Gordon McNutt and RidgeRun, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * XXX Some of this code should be replaceable by the upcoming OPP layer
 * code.  However, some notion of "rate set" is probably still necessary
 * for OMAP2xxx at least.  Rate sets should be generalized so they can be
 * used for any OMAP chip, not just OMAP2xxx.  In particular, Richard Woodruff
 * has in the past expressed a preference to use rate sets for OPP changes,
 * rather than dynamically recalculating the clock tree, so if someone wants
 * this badly enough to write the code to handle it, we should support it
 * as an option.
 */
#undef DEBUG

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/cpufreq.h>
#include <linux/slab.h>

#include <plat/clock.h>
#include <plat/sram.h>
#include <plat/sdrc.h>

#include "clock.h"
#include "clock2xxx.h"
#include "opp2xxx.h"
#include "cm.h"
#include "cm-regbits-24xx.h"

const struct prcm_config *curr_prcm_set;
const struct prcm_config *rate_table;

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

	highest_rate = -EINVAL;

	for (ptr = rate_table; ptr->mpu_speed; ptr++) {
		if (!(ptr->flags & cpu_mask))
			continue;
		if (ptr->xtal_speed != sclk->rate)
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

	for (prcm = rate_table; prcm->mpu_speed; prcm++) {
		if (!(prcm->flags & cpu_mask))
			continue;

		if (prcm->xtal_speed != sclk->rate)
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
	int i = 0;
	int tbl_sz = 0;

	if (!cpu_is_omap24xx())
		return;

	for (prcm = rate_table; prcm->mpu_speed; prcm++) {
		if (!(prcm->flags & cpu_mask))
			continue;
		if (prcm->xtal_speed != sclk->rate)
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
		if (prcm->xtal_speed != sclk->rate)
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
	if (!cpu_is_omap24xx())
		return;

	kfree(freq_table);
}

#endif
