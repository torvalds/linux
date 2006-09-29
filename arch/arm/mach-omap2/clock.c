/*
 *  linux/arch/arm/mach-omap2/clock.c
 *
 *  Copyright (C) 2005 Texas Instruments Inc.
 *  Richard Woodruff <r-woodruff2@ti.com>
 *  Created for OMAP2.
 *
 *  Cleaned up and modified to use omap shared clock framework by
 *  Tony Lindgren <tony@atomide.com>
 *
 *  Based on omap1 clock.c, Copyright (C) 2004 - 2005 Nokia corporation
 *  Written by Tuukka Tikkanen <tuukka.tikkanen@elektrobit.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/clk.h>

#include <asm/io.h>

#include <asm/arch/clock.h>
#include <asm/arch/sram.h>

#include "prcm-regs.h"
#include "memory.h"
#include "clock.h"

#undef DEBUG

//#define DOWN_VARIABLE_DPLL 1			/* Experimental */

static struct prcm_config *curr_prcm_set;
static u32 curr_perf_level = PRCM_FULL_SPEED;
static struct clk *vclk;
static struct clk *sclk;

/*-------------------------------------------------------------------------
 * Omap2 specific clock functions
 *-------------------------------------------------------------------------*/

/* Recalculate SYST_CLK */
static void omap2_sys_clk_recalc(struct clk * clk)
{
	u32 div = PRCM_CLKSRC_CTRL;
	div &= (1 << 7) | (1 << 6);	/* Test if ext clk divided by 1 or 2 */
	div >>= clk->rate_offset;
	clk->rate = (clk->parent->rate / div);
	propagate_rate(clk);
}

static u32 omap2_get_dpll_rate(struct clk * tclk)
{
	long long dpll_clk;
	int dpll_mult, dpll_div, amult;

	dpll_mult = (CM_CLKSEL1_PLL >> 12) & 0x03ff;	/* 10 bits */
	dpll_div = (CM_CLKSEL1_PLL >> 8) & 0x0f;	/* 4 bits */
	dpll_clk = (long long)tclk->parent->rate * dpll_mult;
	do_div(dpll_clk, dpll_div + 1);
	amult = CM_CLKSEL2_PLL & 0x3;
	dpll_clk *= amult;

	return dpll_clk;
}

static void omap2_followparent_recalc(struct clk *clk)
{
	followparent_recalc(clk);
}

static void omap2_propagate_rate(struct clk * clk)
{
	if (!(clk->flags & RATE_FIXED))
		clk->rate = clk->parent->rate;

	propagate_rate(clk);
}

static void omap2_set_osc_ck(int enable)
{
	if (enable)
		PRCM_CLKSRC_CTRL &= ~(0x3 << 3);
	else
		PRCM_CLKSRC_CTRL |= 0x3 << 3;
}

/* Enable an APLL if off */
static void omap2_clk_fixed_enable(struct clk *clk)
{
	u32 cval, i=0;

	if (clk->enable_bit == 0xff)			/* Parent will do it */
		return;

	cval = CM_CLKEN_PLL;

	if ((cval & (0x3 << clk->enable_bit)) == (0x3 << clk->enable_bit))
		return;

	cval &= ~(0x3 << clk->enable_bit);
	cval |= (0x3 << clk->enable_bit);
	CM_CLKEN_PLL = cval;

	if (clk == &apll96_ck)
		cval = (1 << 8);
	else if (clk == &apll54_ck)
		cval = (1 << 6);

	while (!(CM_IDLEST_CKGEN & cval)) {		/* Wait for lock */
		++i;
		udelay(1);
		if (i == 100000) {
			printk(KERN_ERR "Clock %s didn't lock\n", clk->name);
			break;
		}
	}
}

static void omap2_clk_wait_ready(struct clk *clk)
{
	unsigned long reg, other_reg, st_reg;
	u32 bit;
	int i;

	reg = (unsigned long) clk->enable_reg;
	if (reg == (unsigned long) &CM_FCLKEN1_CORE ||
	    reg == (unsigned long) &CM_FCLKEN2_CORE)
		other_reg = (reg & ~0xf0) | 0x10;
	else if (reg == (unsigned long) &CM_ICLKEN1_CORE ||
		 reg == (unsigned long) &CM_ICLKEN2_CORE)
		other_reg = (reg & ~0xf0) | 0x00;
	else
		return;

	/* No check for DSS or cam clocks */
	if ((reg & 0x0f) == 0) {
		if (clk->enable_bit <= 1 || clk->enable_bit == 31)
			return;
	}

	/* Check if both functional and interface clocks
	 * are running. */
	bit = 1 << clk->enable_bit;
	if (!(__raw_readl(other_reg) & bit))
		return;
	st_reg = (other_reg & ~0xf0) | 0x20;
	i = 0;
	while (!(__raw_readl(st_reg) & bit)) {
		i++;
		if (i == 100000) {
			printk(KERN_ERR "Timeout enabling clock %s\n", clk->name);
			break;
		}
	}
	if (i)
		pr_debug("Clock %s stable after %d loops\n", clk->name, i);
}

/* Enables clock without considering parent dependencies or use count
 * REVISIT: Maybe change this to use clk->enable like on omap1?
 */
static int _omap2_clk_enable(struct clk * clk)
{
	u32 regval32;

	if (clk->flags & ALWAYS_ENABLED)
		return 0;

	if (unlikely(clk == &osc_ck)) {
		omap2_set_osc_ck(1);
		return 0;
	}

	if (unlikely(clk->enable_reg == 0)) {
		printk(KERN_ERR "clock.c: Enable for %s without enable code\n",
		       clk->name);
		return 0;
	}

	if (clk->enable_reg == (void __iomem *)&CM_CLKEN_PLL) {
		omap2_clk_fixed_enable(clk);
		return 0;
	}

	regval32 = __raw_readl(clk->enable_reg);
	regval32 |= (1 << clk->enable_bit);
	__raw_writel(regval32, clk->enable_reg);
	wmb();

	omap2_clk_wait_ready(clk);

	return 0;
}

/* Stop APLL */
static void omap2_clk_fixed_disable(struct clk *clk)
{
	u32 cval;

	if(clk->enable_bit == 0xff)		/* let parent off do it */
		return;

	cval = CM_CLKEN_PLL;
	cval &= ~(0x3 << clk->enable_bit);
	CM_CLKEN_PLL = cval;
}

/* Disables clock without considering parent dependencies or use count */
static void _omap2_clk_disable(struct clk *clk)
{
	u32 regval32;

	if (unlikely(clk == &osc_ck)) {
		omap2_set_osc_ck(0);
		return;
	}

	if (clk->enable_reg == 0)
		return;

	if (clk->enable_reg == (void __iomem *)&CM_CLKEN_PLL) {
		omap2_clk_fixed_disable(clk);
		return;
	}

	regval32 = __raw_readl(clk->enable_reg);
	regval32 &= ~(1 << clk->enable_bit);
	__raw_writel(regval32, clk->enable_reg);
	wmb();
}

static int omap2_clk_enable(struct clk *clk)
{
	int ret = 0;

	if (clk->usecount++ == 0) {
		if (likely((u32)clk->parent))
			ret = omap2_clk_enable(clk->parent);

		if (unlikely(ret != 0)) {
			clk->usecount--;
			return ret;
		}

		ret = _omap2_clk_enable(clk);

		if (unlikely(ret != 0) && clk->parent) {
			omap2_clk_disable(clk->parent);
			clk->usecount--;
		}
	}

	return ret;
}

static void omap2_clk_disable(struct clk *clk)
{
	if (clk->usecount > 0 && !(--clk->usecount)) {
		_omap2_clk_disable(clk);
		if (likely((u32)clk->parent))
			omap2_clk_disable(clk->parent);
	}
}

/*
 * Uses the current prcm set to tell if a rate is valid.
 * You can go slower, but not faster within a given rate set.
 */
static u32 omap2_dpll_round_rate(unsigned long target_rate)
{
	u32 high, low;

	if ((CM_CLKSEL2_PLL & 0x3) == 1) {	/* DPLL clockout */
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

/*
 * Used for clocks that are part of CLKSEL_xyz governed clocks.
 * REVISIT: Maybe change to use clk->enable() functions like on omap1?
 */
static void omap2_clksel_recalc(struct clk * clk)
{
	u32 fixed = 0, div = 0;

	if (clk == &dpll_ck) {
		clk->rate = omap2_get_dpll_rate(clk);
		fixed = 1;
		div = 0;
	}

	if (clk == &iva1_mpu_int_ifck) {
		div = 2;
		fixed = 1;
	}

	if ((clk == &dss1_fck) && ((CM_CLKSEL1_CORE & (0x1f << 8)) == 0)) {
		clk->rate = sys_ck.rate;
		return;
	}

	if (!fixed) {
		div = omap2_clksel_get_divisor(clk);
		if (div == 0)
			return;
	}

	if (div != 0) {
		if (unlikely(clk->rate == clk->parent->rate / div))
			return;
		clk->rate = clk->parent->rate / div;
	}

	if (unlikely(clk->flags & RATE_PROPAGATES))
		propagate_rate(clk);
}

/*
 * Finds best divider value in an array based on the source and target
 * rates. The divider array must be sorted with smallest divider first.
 */
static inline u32 omap2_divider_from_table(u32 size, u32 *div_array,
					   u32 src_rate, u32 tgt_rate)
{
	int i, test_rate;

	if (div_array == NULL)
		return ~1;

	for (i=0; i < size; i++) {
		test_rate = src_rate / *div_array;
		if (test_rate <= tgt_rate)
			return *div_array;
		++div_array;
	}

	return ~0;	/* No acceptable divider */
}

/*
 * Find divisor for the given clock and target rate.
 *
 * Note that this will not work for clocks which are part of CONFIG_PARTICIPANT,
 * they are only settable as part of virtual_prcm set.
 */
static u32 omap2_clksel_round_rate(struct clk *tclk, u32 target_rate,
	u32 *new_div)
{
	u32 gfx_div[] = {2, 3, 4};
	u32 sysclkout_div[] = {1, 2, 4, 8, 16};
	u32 dss1_div[] = {1, 2, 3, 4, 5, 6, 8, 9, 12, 16};
	u32 vylnq_div[] = {1, 2, 3, 4, 6, 8, 9, 12, 16, 18};
	u32 best_div = ~0, asize = 0;
	u32 *div_array = NULL;

	switch (tclk->flags & SRC_RATE_SEL_MASK) {
	case CM_GFX_SEL1:
		asize = 3;
		div_array = gfx_div;
		break;
	case CM_PLL_SEL1:
		return omap2_dpll_round_rate(target_rate);
	case CM_SYSCLKOUT_SEL1:
		asize = 5;
		div_array = sysclkout_div;
		break;
	case CM_CORE_SEL1:
		if(tclk == &dss1_fck){
			if(tclk->parent == &core_ck){
				asize = 10;
				div_array = dss1_div;
			} else {
				*new_div = 0; /* fixed clk */
				return(tclk->parent->rate);
			}
		} else if((tclk == &vlynq_fck) && cpu_is_omap2420()){
			if(tclk->parent == &core_ck){
				asize = 10;
				div_array = vylnq_div;
			} else {
				*new_div = 0; /* fixed clk */
				return(tclk->parent->rate);
			}
		}
		break;
	}

	best_div = omap2_divider_from_table(asize, div_array,
	 tclk->parent->rate, target_rate);
	if (best_div == ~0){
		*new_div = 1;
		return best_div; /* signal error */
	}

	*new_div = best_div;
	return (tclk->parent->rate / best_div);
}

/* Given a clock and a rate apply a clock specific rounding function */
static long omap2_clk_round_rate(struct clk *clk, unsigned long rate)
{
	u32 new_div = 0;
	int valid_rate;

	if (clk->flags & RATE_FIXED)
		return clk->rate;

	if (clk->flags & RATE_CKCTL) {
		valid_rate = omap2_clksel_round_rate(clk, rate, &new_div);
		return valid_rate;
	}

	if (clk->round_rate != 0)
		return clk->round_rate(clk, rate);

	return clk->rate;
}

/*
 * Check the DLL lock state, and return tue if running in unlock mode.
 * This is needed to compenste for the shifted DLL value in unlock mode.
 */
static u32 omap2_dll_force_needed(void)
{
	u32 dll_state = SDRC_DLLA_CTRL;		/* dlla and dllb are a set */

	if ((dll_state & (1 << 2)) == (1 << 2))
		return 1;
	else
		return 0;
}

static u32 omap2_reprogram_sdrc(u32 level, u32 force)
{
	u32 slow_dll_ctrl, fast_dll_ctrl, m_type;
	u32 prev = curr_perf_level, flags;

	if ((curr_perf_level == level) && !force)
		return prev;

	m_type = omap2_memory_get_type();
	slow_dll_ctrl = omap2_memory_get_slow_dll_ctrl();
	fast_dll_ctrl = omap2_memory_get_fast_dll_ctrl();

	if (level == PRCM_HALF_SPEED) {
		local_irq_save(flags);
		PRCM_VOLTSETUP = 0xffff;
		omap2_sram_reprogram_sdrc(PRCM_HALF_SPEED,
					  slow_dll_ctrl, m_type);
		curr_perf_level = PRCM_HALF_SPEED;
		local_irq_restore(flags);
	}
	if (level == PRCM_FULL_SPEED) {
		local_irq_save(flags);
		PRCM_VOLTSETUP = 0xffff;
		omap2_sram_reprogram_sdrc(PRCM_FULL_SPEED,
					  fast_dll_ctrl, m_type);
		curr_perf_level = PRCM_FULL_SPEED;
		local_irq_restore(flags);
	}

	return prev;
}

static int omap2_reprogram_dpll(struct clk * clk, unsigned long rate)
{
	u32 flags, cur_rate, low, mult, div, valid_rate, done_rate;
	u32 bypass = 0;
	struct prcm_config tmpset;
	int ret = -EINVAL;

	local_irq_save(flags);
	cur_rate = omap2_get_dpll_rate(&dpll_ck);
	mult = CM_CLKSEL2_PLL & 0x3;

	if ((rate == (cur_rate / 2)) && (mult == 2)) {
		omap2_reprogram_sdrc(PRCM_HALF_SPEED, 1);
	} else if ((rate == (cur_rate * 2)) && (mult == 1)) {
		omap2_reprogram_sdrc(PRCM_FULL_SPEED, 1);
	} else if (rate != cur_rate) {
		valid_rate = omap2_dpll_round_rate(rate);
		if (valid_rate != rate)
			goto dpll_exit;

		if ((CM_CLKSEL2_PLL & 0x3) == 1)
			low = curr_prcm_set->dpll_speed;
		else
			low = curr_prcm_set->dpll_speed / 2;

		tmpset.cm_clksel1_pll = CM_CLKSEL1_PLL;
		tmpset.cm_clksel1_pll &= ~(0x3FFF << 8);
		div = ((curr_prcm_set->xtal_speed / 1000000) - 1);
		tmpset.cm_clksel2_pll = CM_CLKSEL2_PLL;
		tmpset.cm_clksel2_pll &= ~0x3;
		if (rate > low) {
			tmpset.cm_clksel2_pll |= 0x2;
			mult = ((rate / 2) / 1000000);
			done_rate = PRCM_FULL_SPEED;
		} else {
			tmpset.cm_clksel2_pll |= 0x1;
			mult = (rate / 1000000);
			done_rate = PRCM_HALF_SPEED;
		}
		tmpset.cm_clksel1_pll |= ((div << 8) | (mult << 12));

		/* Worst case */
		tmpset.base_sdrc_rfr = V24XX_SDRC_RFR_CTRL_BYPASS;

		if (rate == curr_prcm_set->xtal_speed)	/* If asking for 1-1 */
			bypass = 1;

		omap2_reprogram_sdrc(PRCM_FULL_SPEED, 1); /* For init_mem */

		/* Force dll lock mode */
		omap2_set_prcm(tmpset.cm_clksel1_pll, tmpset.base_sdrc_rfr,
			       bypass);

		/* Errata: ret dll entry state */
		omap2_init_memory_params(omap2_dll_force_needed());
		omap2_reprogram_sdrc(done_rate, 0);
	}
	omap2_clksel_recalc(&dpll_ck);
	ret = 0;

dpll_exit:
	local_irq_restore(flags);
	return(ret);
}

/* Just return the MPU speed */
static void omap2_mpu_recalc(struct clk * clk)
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
static long omap2_round_to_table_rate(struct clk * clk, unsigned long rate)
{
	struct prcm_config * ptr;
	long highest_rate;

	if (clk != &virt_prcm_set)
		return -EINVAL;

	highest_rate = -EINVAL;

	for (ptr = rate_table; ptr->mpu_speed; ptr++) {
		if (ptr->xtal_speed != sys_ck.rate)
			continue;

		highest_rate = ptr->mpu_speed;

		/* Can check only after xtal frequency check */
		if (ptr->mpu_speed <= rate)
			break;
	}
	return highest_rate;
}

/*
 * omap2_convert_field_to_div() - turn field value into integer divider
 */
static u32 omap2_clksel_to_divisor(u32 div_sel, u32 field_val)
{
	u32 i;
	u32 clkout_array[] = {1, 2, 4, 8, 16};

	if ((div_sel & SRC_RATE_SEL_MASK) == CM_SYSCLKOUT_SEL1) {
		for (i = 0; i < 5; i++) {
			if (field_val == i)
				return clkout_array[i];
		}
		return ~0;
	} else
		return field_val;
}

/*
 * Returns the CLKSEL divider register value
 * REVISIT: This should be cleaned up to work nicely with void __iomem *
 */
static u32 omap2_get_clksel(u32 *div_sel, u32 *field_mask,
			    struct clk *clk)
{
	int ret = ~0;
	u32 reg_val, div_off;
	u32 div_addr = 0;
	u32 mask = ~0;

	div_off = clk->rate_offset;

	switch ((*div_sel & SRC_RATE_SEL_MASK)) {
	case CM_MPU_SEL1:
		div_addr = (u32)&CM_CLKSEL_MPU;
		mask = 0x1f;
		break;
	case CM_DSP_SEL1:
		div_addr = (u32)&CM_CLKSEL_DSP;
		if (cpu_is_omap2420()) {
			if ((div_off == 0) || (div_off == 8))
				mask = 0x1f;
			else if (div_off == 5)
				mask = 0x3;
		} else if (cpu_is_omap2430()) {
			if (div_off == 0)
				mask = 0x1f;
			else if (div_off == 5)
				mask = 0x3;
		}
		break;
	case CM_GFX_SEL1:
		div_addr = (u32)&CM_CLKSEL_GFX;
		if (div_off == 0)
			mask = 0x7;
		break;
	case CM_MODEM_SEL1:
		div_addr = (u32)&CM_CLKSEL_MDM;
		if (div_off == 0)
			mask = 0xf;
		break;
	case CM_SYSCLKOUT_SEL1:
		div_addr = (u32)&PRCM_CLKOUT_CTRL;
		if ((div_off == 3) || (div_off = 11))
			mask= 0x3;
		break;
	case CM_CORE_SEL1:
		div_addr = (u32)&CM_CLKSEL1_CORE;
		switch (div_off) {
		case 0:					/* l3 */
		case 8:					/* dss1 */
		case 15:				/* vylnc-2420 */
		case 20:				/* ssi */
			mask = 0x1f; break;
		case 5:					/* l4 */
			mask = 0x3; break;
		case 13:				/* dss2 */
			mask = 0x1; break;
		case 25:				/* usb */
			mask = 0x7; break;
		}
	}

	*field_mask = mask;

	if (unlikely(mask == ~0))
		div_addr = 0;

	*div_sel = div_addr;

	if (unlikely(div_addr == 0))
		return ret;

	/* Isolate field */
	reg_val = __raw_readl((void __iomem *)div_addr) & (mask << div_off);

	/* Normalize back to divider value */
	reg_val >>= div_off;

	return reg_val;
}

/*
 * Return divider to be applied to parent clock.
 * Return 0 on error.
 */
static u32 omap2_clksel_get_divisor(struct clk *clk)
{
	int ret = 0;
	u32 div, div_sel, div_off, field_mask, field_val;

	/* isolate control register */
	div_sel = (SRC_RATE_SEL_MASK & clk->flags);

	div_off = clk->rate_offset;
	field_val = omap2_get_clksel(&div_sel, &field_mask, clk);
	if (div_sel == 0)
		return ret;

	div_sel = (SRC_RATE_SEL_MASK & clk->flags);
	div = omap2_clksel_to_divisor(div_sel, field_val);

	return div;
}

/* Set the clock rate for a clock source */
static int omap2_clk_set_rate(struct clk *clk, unsigned long rate)

{
	int ret = -EINVAL;
	void __iomem * reg;
	u32 div_sel, div_off, field_mask, field_val, reg_val, validrate;
	u32 new_div = 0;

	if (!(clk->flags & CONFIG_PARTICIPANT) && (clk->flags & RATE_CKCTL)) {
		if (clk == &dpll_ck)
			return omap2_reprogram_dpll(clk, rate);

		/* Isolate control register */
		div_sel = (SRC_RATE_SEL_MASK & clk->flags);
		div_off = clk->rate_offset;

		validrate = omap2_clksel_round_rate(clk, rate, &new_div);
		if (validrate != rate)
			return(ret);

		field_val = omap2_get_clksel(&div_sel, &field_mask, clk);
		if (div_sel == 0)
			return ret;

		if (clk->flags & CM_SYSCLKOUT_SEL1) {
			switch (new_div) {
			case 16:
				field_val = 4;
				break;
			case 8:
				field_val = 3;
				break;
			case 4:
				field_val = 2;
				break;
			case 2:
				field_val = 1;
				break;
			case 1:
				field_val = 0;
				break;
			}
		} else
			field_val = new_div;

		reg = (void __iomem *)div_sel;

		reg_val = __raw_readl(reg);
		reg_val &= ~(field_mask << div_off);
		reg_val |= (field_val << div_off);
		__raw_writel(reg_val, reg);
		wmb();
		clk->rate = clk->parent->rate / field_val;

		if (clk->flags & DELAYED_APP) {
			__raw_writel(0x1, (void __iomem *)&PRCM_CLKCFG_CTRL);
			wmb();
		}
		ret = 0;
	} else if (clk->set_rate != 0)
		ret = clk->set_rate(clk, rate);

	if (unlikely(ret == 0 && (clk->flags & RATE_PROPAGATES)))
		propagate_rate(clk);

	return ret;
}

/* Converts encoded control register address into a full address */
static u32 omap2_get_src_field(u32 *type_to_addr, u32 reg_offset,
			       struct clk *src_clk, u32 *field_mask)
{
	u32 val = ~0, src_reg_addr = 0, mask = 0;

	/* Find target control register.*/
	switch ((*type_to_addr & SRC_RATE_SEL_MASK)) {
	case CM_CORE_SEL1:
		src_reg_addr = (u32)&CM_CLKSEL1_CORE;
		if (reg_offset == 13) {			/* DSS2_fclk */
			mask = 0x1;
			if (src_clk == &sys_ck)
				val = 0;
			if (src_clk == &func_48m_ck)
				val = 1;
		} else if (reg_offset == 8) {		/* DSS1_fclk */
			mask = 0x1f;
			if (src_clk == &sys_ck)
				val = 0;
			else if (src_clk == &core_ck)	/* divided clock */
				val = 0x10;		/* rate needs fixing */
		} else if ((reg_offset == 15) && cpu_is_omap2420()){ /*vlnyq*/
			mask = 0x1F;
			if(src_clk == &func_96m_ck)
				val = 0;
			else if (src_clk == &core_ck)
				val = 0x10;
		}
		break;
	case CM_CORE_SEL2:
		src_reg_addr = (u32)&CM_CLKSEL2_CORE;
		mask = 0x3;
		if (src_clk == &func_32k_ck)
			val = 0x0;
		if (src_clk == &sys_ck)
			val = 0x1;
		if (src_clk == &alt_ck)
			val = 0x2;
		break;
	case CM_WKUP_SEL1:
		src_reg_addr = (u32)&CM_CLKSEL_WKUP;
		mask = 0x3;
		if (src_clk == &func_32k_ck)
			val = 0x0;
		if (src_clk == &sys_ck)
			val = 0x1;
		if (src_clk == &alt_ck)
			val = 0x2;
		break;
	case CM_PLL_SEL1:
		src_reg_addr = (u32)&CM_CLKSEL1_PLL;
		mask = 0x1;
		if (reg_offset == 0x3) {
			if (src_clk == &apll96_ck)
				val = 0;
			if (src_clk == &alt_ck)
				val = 1;
		}
		else if (reg_offset == 0x5) {
			if (src_clk == &apll54_ck)
				val = 0;
			if (src_clk == &alt_ck)
				val = 1;
		}
		break;
	case CM_PLL_SEL2:
		src_reg_addr = (u32)&CM_CLKSEL2_PLL;
		mask = 0x3;
		if (src_clk == &func_32k_ck)
			val = 0x0;
		if (src_clk == &dpll_ck)
			val = 0x2;
		break;
	case CM_SYSCLKOUT_SEL1:
		src_reg_addr = (u32)&PRCM_CLKOUT_CTRL;
		mask = 0x3;
		if (src_clk == &dpll_ck)
			val = 0;
		if (src_clk == &sys_ck)
			val = 1;
		if (src_clk == &func_96m_ck)
			val = 2;
		if (src_clk == &func_54m_ck)
			val = 3;
		break;
	}

	if (val == ~0)			/* Catch errors in offset */
		*type_to_addr = 0;
	else
		*type_to_addr = src_reg_addr;
	*field_mask = mask;

	return val;
}

static int omap2_clk_set_parent(struct clk *clk, struct clk *new_parent)
{
	void __iomem * reg;
	u32 src_sel, src_off, field_val, field_mask, reg_val, rate;
	int ret = -EINVAL;

	if (unlikely(clk->flags & CONFIG_PARTICIPANT))
		return ret;

	if (clk->flags & SRC_SEL_MASK) {	/* On-chip SEL collection */
		src_sel = (SRC_RATE_SEL_MASK & clk->flags);
		src_off = clk->src_offset;

		if (src_sel == 0)
			goto set_parent_error;

		field_val = omap2_get_src_field(&src_sel, src_off, new_parent,
						&field_mask);

		reg = (void __iomem *)src_sel;

		if (clk->usecount > 0)
			_omap2_clk_disable(clk);

		/* Set new source value (previous dividers if any in effect) */
		reg_val = __raw_readl(reg) & ~(field_mask << src_off);
		reg_val |= (field_val << src_off);
		__raw_writel(reg_val, reg);
		wmb();

		if (clk->flags & DELAYED_APP) {
			__raw_writel(0x1, (void __iomem *)&PRCM_CLKCFG_CTRL);
			wmb();
		}
		if (clk->usecount > 0)
			_omap2_clk_enable(clk);

		clk->parent = new_parent;

		/* SRC_RATE_SEL_MASK clocks follow their parents rates.*/
		if ((new_parent == &core_ck) && (clk == &dss1_fck))
			clk->rate = new_parent->rate / 0x10;
		else
			clk->rate = new_parent->rate;

		if (unlikely(clk->flags & RATE_PROPAGATES))
			propagate_rate(clk);

		return 0;
	} else {
		clk->parent = new_parent;
		rate = new_parent->rate;
		omap2_clk_set_rate(clk, rate);
		ret = 0;
	}

 set_parent_error:
	return ret;
}

/* Sets basic clocks based on the specified rate */
static int omap2_select_table_rate(struct clk * clk, unsigned long rate)
{
	u32 flags, cur_rate, done_rate, bypass = 0;
	u8 cpu_mask = 0;
	struct prcm_config *prcm;
	unsigned long found_speed = 0;

	if (clk != &virt_prcm_set)
		return -EINVAL;

	/* FIXME: Change cpu_is_omap2420() to cpu_is_omap242x() */
	if (cpu_is_omap2420())
		cpu_mask = RATE_IN_242X;
	else if (cpu_is_omap2430())
		cpu_mask = RATE_IN_243X;

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
	cur_rate = omap2_get_dpll_rate(&dpll_ck);

	if (prcm->dpll_speed == cur_rate / 2) {
		omap2_reprogram_sdrc(PRCM_HALF_SPEED, 1);
	} else if (prcm->dpll_speed == cur_rate * 2) {
		omap2_reprogram_sdrc(PRCM_FULL_SPEED, 1);
	} else if (prcm->dpll_speed != cur_rate) {
		local_irq_save(flags);

		if (prcm->dpll_speed == prcm->xtal_speed)
			bypass = 1;

		if ((prcm->cm_clksel2_pll & 0x3) == 2)
			done_rate = PRCM_FULL_SPEED;
		else
			done_rate = PRCM_HALF_SPEED;

		/* MPU divider */
		CM_CLKSEL_MPU = prcm->cm_clksel_mpu;

		/* dsp + iva1 div(2420), iva2.1(2430) */
		CM_CLKSEL_DSP = prcm->cm_clksel_dsp;

		CM_CLKSEL_GFX = prcm->cm_clksel_gfx;

		/* Major subsystem dividers */
		CM_CLKSEL1_CORE = prcm->cm_clksel1_core;
		if (cpu_is_omap2430())
			CM_CLKSEL_MDM = prcm->cm_clksel_mdm;

		/* x2 to enter init_mem */
		omap2_reprogram_sdrc(PRCM_FULL_SPEED, 1);

		omap2_set_prcm(prcm->cm_clksel1_pll, prcm->base_sdrc_rfr,
			       bypass);

		omap2_init_memory_params(omap2_dll_force_needed());
		omap2_reprogram_sdrc(done_rate, 0);

		local_irq_restore(flags);
	}
	omap2_clksel_recalc(&dpll_ck);

	return 0;
}

/*-------------------------------------------------------------------------
 * Omap2 clock reset and init functions
 *-------------------------------------------------------------------------*/

#ifdef CONFIG_OMAP_RESET_CLOCKS
static void __init omap2_clk_disable_unused(struct clk *clk)
{
	u32 regval32;

	regval32 = __raw_readl(clk->enable_reg);
	if ((regval32 & (1 << clk->enable_bit)) == 0)
		return;

	printk(KERN_INFO "Disabling unused clock \"%s\"\n", clk->name);
	_omap2_clk_disable(clk);
}
#else
#define omap2_clk_disable_unused	NULL
#endif

static struct clk_functions omap2_clk_functions = {
	.clk_enable		= omap2_clk_enable,
	.clk_disable		= omap2_clk_disable,
	.clk_round_rate		= omap2_clk_round_rate,
	.clk_set_rate		= omap2_clk_set_rate,
	.clk_set_parent		= omap2_clk_set_parent,
	.clk_disable_unused	= omap2_clk_disable_unused,
};

static void __init omap2_get_crystal_rate(struct clk *osc, struct clk *sys)
{
	u32 div, aplls, sclk = 13000000;

	aplls = CM_CLKSEL1_PLL;
	aplls &= ((1 << 23) | (1 << 24) | (1 << 25));
	aplls >>= 23;			/* Isolate field, 0,2,3 */

	if (aplls == 0)
		sclk = 19200000;
	else if (aplls == 2)
		sclk = 13000000;
	else if (aplls == 3)
		sclk = 12000000;

	div = PRCM_CLKSRC_CTRL;
	div &= ((1 << 7) | (1 << 6));
	div >>= sys->rate_offset;

	osc->rate = sclk * div;
	sys->rate = sclk;
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

	propagate_rate(&osc_ck);		/* update main root fast */
	propagate_rate(&func_32k_ck);		/* update main root slow */

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
	struct clk ** clkp;
	u32 clkrate;

	clk_init(&omap2_clk_functions);
	omap2_get_crystal_rate(&osc_ck, &sys_ck);

	for (clkp = onchip_clks; clkp < onchip_clks + ARRAY_SIZE(onchip_clks);
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
	clkrate = omap2_get_dpll_rate(&dpll_ck);
	for (prcm = rate_table; prcm->mpu_speed; prcm++) {
		if (prcm->xtal_speed != sys_ck.rate)
			continue;
		if (prcm->dpll_speed <= clkrate)
			 break;
	}
	curr_prcm_set = prcm;

	propagate_rate(&osc_ck);		/* update main root fast */
	propagate_rate(&func_32k_ck);		/* update main root slow */

	printk(KERN_INFO "Clocking rate (Crystal/DPLL/MPU): "
	       "%ld.%01ld/%ld/%ld MHz\n",
	       (sys_ck.rate / 1000000), (sys_ck.rate / 100000) % 10,
	       (dpll_ck.rate / 1000000), (mpu_ck.rate / 1000000)) ;

	/*
	 * Only enable those clocks we will need, let the drivers
	 * enable other clocks as necessary
	 */
	clk_enable(&sync_32k_ick);
	clk_enable(&omapctrl_ick);

	/* Force the APLLs active during bootup to avoid disabling and
	 * enabling them unnecessarily. */
	clk_enable(&apll96_ck);
	clk_enable(&apll54_ck);

	if (cpu_is_omap2430())
		clk_enable(&sdrc_ick);

	/* Avoid sleeping sleeping during omap2_clk_prepare_for_reboot() */
	vclk = clk_get(NULL, "virt_prcm_set");
	sclk = clk_get(NULL, "sys_ck");

	return 0;
}

static int __init omap2_disable_aplls(void)
{
	clk_disable(&apll96_ck);
	clk_disable(&apll54_ck);

	return 0;
}
late_initcall(omap2_disable_aplls);
