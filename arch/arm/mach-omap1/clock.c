/*
 *  linux/arch/arm/mach-omap1/clock.c
 *
 *  Copyright (C) 2004 - 2005, 2009-2010 Nokia Corporation
 *  Written by Tuukka Tikkanen <tuukka.tikkanen@elektrobit.com>
 *
 *  Modified to use omap shared clock framework by
 *  Tony Lindgren <tony@atomide.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <asm/mach-types.h>
#include <asm/clkdev.h>

#include <plat/cpu.h>
#include <plat/usb.h>
#include <plat/clock.h>
#include <plat/sram.h>
#include <plat/clkdev_omap.h>

#include "clock.h"
#include "opp.h"

__u32 arm_idlect1_mask;
struct clk *api_ck_p, *ck_dpll1_p, *ck_ref_p;

/*-------------------------------------------------------------------------
 * Omap1 specific clock functions
 *-------------------------------------------------------------------------*/

unsigned long omap1_uart_recalc(struct clk *clk)
{
	unsigned int val = __raw_readl(clk->enable_reg);
	return val & clk->enable_bit ? 48000000 : 12000000;
}

unsigned long omap1_sossi_recalc(struct clk *clk)
{
	u32 div = omap_readl(MOD_CONF_CTRL_1);

	div = (div >> 17) & 0x7;
	div++;

	return clk->parent->rate / div;
}

static void omap1_clk_allow_idle(struct clk *clk)
{
	struct arm_idlect1_clk * iclk = (struct arm_idlect1_clk *)clk;

	if (!(clk->flags & CLOCK_IDLE_CONTROL))
		return;

	if (iclk->no_idle_count > 0 && !(--iclk->no_idle_count))
		arm_idlect1_mask |= 1 << iclk->idlect_shift;
}

static void omap1_clk_deny_idle(struct clk *clk)
{
	struct arm_idlect1_clk * iclk = (struct arm_idlect1_clk *)clk;

	if (!(clk->flags & CLOCK_IDLE_CONTROL))
		return;

	if (iclk->no_idle_count++ == 0)
		arm_idlect1_mask &= ~(1 << iclk->idlect_shift);
}

static __u16 verify_ckctl_value(__u16 newval)
{
	/* This function checks for following limitations set
	 * by the hardware (all conditions must be true):
	 * DSPMMU_CK == DSP_CK  or  DSPMMU_CK == DSP_CK/2
	 * ARM_CK >= TC_CK
	 * DSP_CK >= TC_CK
	 * DSPMMU_CK >= TC_CK
	 *
	 * In addition following rules are enforced:
	 * LCD_CK <= TC_CK
	 * ARMPER_CK <= TC_CK
	 *
	 * However, maximum frequencies are not checked for!
	 */
	__u8 per_exp;
	__u8 lcd_exp;
	__u8 arm_exp;
	__u8 dsp_exp;
	__u8 tc_exp;
	__u8 dspmmu_exp;

	per_exp = (newval >> CKCTL_PERDIV_OFFSET) & 3;
	lcd_exp = (newval >> CKCTL_LCDDIV_OFFSET) & 3;
	arm_exp = (newval >> CKCTL_ARMDIV_OFFSET) & 3;
	dsp_exp = (newval >> CKCTL_DSPDIV_OFFSET) & 3;
	tc_exp = (newval >> CKCTL_TCDIV_OFFSET) & 3;
	dspmmu_exp = (newval >> CKCTL_DSPMMUDIV_OFFSET) & 3;

	if (dspmmu_exp < dsp_exp)
		dspmmu_exp = dsp_exp;
	if (dspmmu_exp > dsp_exp+1)
		dspmmu_exp = dsp_exp+1;
	if (tc_exp < arm_exp)
		tc_exp = arm_exp;
	if (tc_exp < dspmmu_exp)
		tc_exp = dspmmu_exp;
	if (tc_exp > lcd_exp)
		lcd_exp = tc_exp;
	if (tc_exp > per_exp)
		per_exp = tc_exp;

	newval &= 0xf000;
	newval |= per_exp << CKCTL_PERDIV_OFFSET;
	newval |= lcd_exp << CKCTL_LCDDIV_OFFSET;
	newval |= arm_exp << CKCTL_ARMDIV_OFFSET;
	newval |= dsp_exp << CKCTL_DSPDIV_OFFSET;
	newval |= tc_exp << CKCTL_TCDIV_OFFSET;
	newval |= dspmmu_exp << CKCTL_DSPMMUDIV_OFFSET;

	return newval;
}

static int calc_dsor_exp(struct clk *clk, unsigned long rate)
{
	/* Note: If target frequency is too low, this function will return 4,
	 * which is invalid value. Caller must check for this value and act
	 * accordingly.
	 *
	 * Note: This function does not check for following limitations set
	 * by the hardware (all conditions must be true):
	 * DSPMMU_CK == DSP_CK  or  DSPMMU_CK == DSP_CK/2
	 * ARM_CK >= TC_CK
	 * DSP_CK >= TC_CK
	 * DSPMMU_CK >= TC_CK
	 */
	unsigned long realrate;
	struct clk * parent;
	unsigned  dsor_exp;

	parent = clk->parent;
	if (unlikely(parent == NULL))
		return -EIO;

	realrate = parent->rate;
	for (dsor_exp=0; dsor_exp<4; dsor_exp++) {
		if (realrate <= rate)
			break;

		realrate /= 2;
	}

	return dsor_exp;
}

unsigned long omap1_ckctl_recalc(struct clk *clk)
{
	/* Calculate divisor encoded as 2-bit exponent */
	int dsor = 1 << (3 & (omap_readw(ARM_CKCTL) >> clk->rate_offset));

	return clk->parent->rate / dsor;
}

unsigned long omap1_ckctl_recalc_dsp_domain(struct clk *clk)
{
	int dsor;

	/* Calculate divisor encoded as 2-bit exponent
	 *
	 * The clock control bits are in DSP domain,
	 * so api_ck is needed for access.
	 * Note that DSP_CKCTL virt addr = phys addr, so
	 * we must use __raw_readw() instead of omap_readw().
	 */
	omap1_clk_enable(api_ck_p);
	dsor = 1 << (3 & (__raw_readw(DSP_CKCTL) >> clk->rate_offset));
	omap1_clk_disable(api_ck_p);

	return clk->parent->rate / dsor;
}

/* MPU virtual clock functions */
int omap1_select_table_rate(struct clk *clk, unsigned long rate)
{
	/* Find the highest supported frequency <= rate and switch to it */
	struct mpu_rate * ptr;
	unsigned long dpll1_rate, ref_rate;

	dpll1_rate = ck_dpll1_p->rate;
	ref_rate = ck_ref_p->rate;

	for (ptr = omap1_rate_table; ptr->rate; ptr++) {
		if (ptr->xtal != ref_rate)
			continue;

		/* DPLL1 cannot be reprogrammed without risking system crash */
		if (likely(dpll1_rate != 0) && ptr->pll_rate != dpll1_rate)
			continue;

		/* Can check only after xtal frequency check */
		if (ptr->rate <= rate)
			break;
	}

	if (!ptr->rate)
		return -EINVAL;

	/*
	 * In most cases we should not need to reprogram DPLL.
	 * Reprogramming the DPLL is tricky, it must be done from SRAM.
	 * (on 730, bit 13 must always be 1)
	 */
	if (cpu_is_omap7xx())
		omap_sram_reprogram_clock(ptr->dpllctl_val, ptr->ckctl_val | 0x2000);
	else
		omap_sram_reprogram_clock(ptr->dpllctl_val, ptr->ckctl_val);

	/* XXX Do we need to recalculate the tree below DPLL1 at this point? */
	ck_dpll1_p->rate = ptr->pll_rate;

	return 0;
}

int omap1_clk_set_rate_dsp_domain(struct clk *clk, unsigned long rate)
{
	int dsor_exp;
	u16 regval;

	dsor_exp = calc_dsor_exp(clk, rate);
	if (dsor_exp > 3)
		dsor_exp = -EINVAL;
	if (dsor_exp < 0)
		return dsor_exp;

	regval = __raw_readw(DSP_CKCTL);
	regval &= ~(3 << clk->rate_offset);
	regval |= dsor_exp << clk->rate_offset;
	__raw_writew(regval, DSP_CKCTL);
	clk->rate = clk->parent->rate / (1 << dsor_exp);

	return 0;
}

long omap1_clk_round_rate_ckctl_arm(struct clk *clk, unsigned long rate)
{
	int dsor_exp = calc_dsor_exp(clk, rate);
	if (dsor_exp < 0)
		return dsor_exp;
	if (dsor_exp > 3)
		dsor_exp = 3;
	return clk->parent->rate / (1 << dsor_exp);
}

int omap1_clk_set_rate_ckctl_arm(struct clk *clk, unsigned long rate)
{
	int dsor_exp;
	u16 regval;

	dsor_exp = calc_dsor_exp(clk, rate);
	if (dsor_exp > 3)
		dsor_exp = -EINVAL;
	if (dsor_exp < 0)
		return dsor_exp;

	regval = omap_readw(ARM_CKCTL);
	regval &= ~(3 << clk->rate_offset);
	regval |= dsor_exp << clk->rate_offset;
	regval = verify_ckctl_value(regval);
	omap_writew(regval, ARM_CKCTL);
	clk->rate = clk->parent->rate / (1 << dsor_exp);
	return 0;
}

long omap1_round_to_table_rate(struct clk *clk, unsigned long rate)
{
	/* Find the highest supported frequency <= rate */
	struct mpu_rate * ptr;
	long highest_rate;
	unsigned long ref_rate;

	ref_rate = ck_ref_p->rate;

	highest_rate = -EINVAL;

	for (ptr = omap1_rate_table; ptr->rate; ptr++) {
		if (ptr->xtal != ref_rate)
			continue;

		highest_rate = ptr->rate;

		/* Can check only after xtal frequency check */
		if (ptr->rate <= rate)
			break;
	}

	return highest_rate;
}

static unsigned calc_ext_dsor(unsigned long rate)
{
	unsigned dsor;

	/* MCLK and BCLK divisor selection is not linear:
	 * freq = 96MHz / dsor
	 *
	 * RATIO_SEL range: dsor <-> RATIO_SEL
	 * 0..6: (RATIO_SEL+2) <-> (dsor-2)
	 * 6..48:  (8+(RATIO_SEL-6)*2) <-> ((dsor-8)/2+6)
	 * Minimum dsor is 2 and maximum is 96. Odd divisors starting from 9
	 * can not be used.
	 */
	for (dsor = 2; dsor < 96; ++dsor) {
		if ((dsor & 1) && dsor > 8)
			continue;
		if (rate >= 96000000 / dsor)
			break;
	}
	return dsor;
}

/* XXX Only needed on 1510 */
int omap1_set_uart_rate(struct clk *clk, unsigned long rate)
{
	unsigned int val;

	val = __raw_readl(clk->enable_reg);
	if (rate == 12000000)
		val &= ~(1 << clk->enable_bit);
	else if (rate == 48000000)
		val |= (1 << clk->enable_bit);
	else
		return -EINVAL;
	__raw_writel(val, clk->enable_reg);
	clk->rate = rate;

	return 0;
}

/* External clock (MCLK & BCLK) functions */
int omap1_set_ext_clk_rate(struct clk *clk, unsigned long rate)
{
	unsigned dsor;
	__u16 ratio_bits;

	dsor = calc_ext_dsor(rate);
	clk->rate = 96000000 / dsor;
	if (dsor > 8)
		ratio_bits = ((dsor - 8) / 2 + 6) << 2;
	else
		ratio_bits = (dsor - 2) << 2;

	ratio_bits |= __raw_readw(clk->enable_reg) & ~0xfd;
	__raw_writew(ratio_bits, clk->enable_reg);

	return 0;
}

int omap1_set_sossi_rate(struct clk *clk, unsigned long rate)
{
	u32 l;
	int div;
	unsigned long p_rate;

	p_rate = clk->parent->rate;
	/* Round towards slower frequency */
	div = (p_rate + rate - 1) / rate;
	div--;
	if (div < 0 || div > 7)
		return -EINVAL;

	l = omap_readl(MOD_CONF_CTRL_1);
	l &= ~(7 << 17);
	l |= div << 17;
	omap_writel(l, MOD_CONF_CTRL_1);

	clk->rate = p_rate / (div + 1);

	return 0;
}

long omap1_round_ext_clk_rate(struct clk *clk, unsigned long rate)
{
	return 96000000 / calc_ext_dsor(rate);
}

void omap1_init_ext_clk(struct clk *clk)
{
	unsigned dsor;
	__u16 ratio_bits;

	/* Determine current rate and ensure clock is based on 96MHz APLL */
	ratio_bits = __raw_readw(clk->enable_reg) & ~1;
	__raw_writew(ratio_bits, clk->enable_reg);

	ratio_bits = (ratio_bits & 0xfc) >> 2;
	if (ratio_bits > 6)
		dsor = (ratio_bits - 6) * 2 + 8;
	else
		dsor = ratio_bits + 2;

	clk-> rate = 96000000 / dsor;
}

int omap1_clk_enable(struct clk *clk)
{
	int ret = 0;

	if (clk->usecount++ == 0) {
		if (clk->parent) {
			ret = omap1_clk_enable(clk->parent);
			if (ret)
				goto err;

			if (clk->flags & CLOCK_NO_IDLE_PARENT)
				omap1_clk_deny_idle(clk->parent);
		}

		ret = clk->ops->enable(clk);
		if (ret) {
			if (clk->parent)
				omap1_clk_disable(clk->parent);
			goto err;
		}
	}
	return ret;

err:
	clk->usecount--;
	return ret;
}

void omap1_clk_disable(struct clk *clk)
{
	if (clk->usecount > 0 && !(--clk->usecount)) {
		clk->ops->disable(clk);
		if (likely(clk->parent)) {
			omap1_clk_disable(clk->parent);
			if (clk->flags & CLOCK_NO_IDLE_PARENT)
				omap1_clk_allow_idle(clk->parent);
		}
	}
}

static int omap1_clk_enable_generic(struct clk *clk)
{
	__u16 regval16;
	__u32 regval32;

	if (unlikely(clk->enable_reg == NULL)) {
		printk(KERN_ERR "clock.c: Enable for %s without enable code\n",
		       clk->name);
		return -EINVAL;
	}

	if (clk->flags & ENABLE_REG_32BIT) {
		regval32 = __raw_readl(clk->enable_reg);
		regval32 |= (1 << clk->enable_bit);
		__raw_writel(regval32, clk->enable_reg);
	} else {
		regval16 = __raw_readw(clk->enable_reg);
		regval16 |= (1 << clk->enable_bit);
		__raw_writew(regval16, clk->enable_reg);
	}

	return 0;
}

static void omap1_clk_disable_generic(struct clk *clk)
{
	__u16 regval16;
	__u32 regval32;

	if (clk->enable_reg == NULL)
		return;

	if (clk->flags & ENABLE_REG_32BIT) {
		regval32 = __raw_readl(clk->enable_reg);
		regval32 &= ~(1 << clk->enable_bit);
		__raw_writel(regval32, clk->enable_reg);
	} else {
		regval16 = __raw_readw(clk->enable_reg);
		regval16 &= ~(1 << clk->enable_bit);
		__raw_writew(regval16, clk->enable_reg);
	}
}

const struct clkops clkops_generic = {
	.enable		= omap1_clk_enable_generic,
	.disable	= omap1_clk_disable_generic,
};

static int omap1_clk_enable_dsp_domain(struct clk *clk)
{
	int retval;

	retval = omap1_clk_enable(api_ck_p);
	if (!retval) {
		retval = omap1_clk_enable_generic(clk);
		omap1_clk_disable(api_ck_p);
	}

	return retval;
}

static void omap1_clk_disable_dsp_domain(struct clk *clk)
{
	if (omap1_clk_enable(api_ck_p) == 0) {
		omap1_clk_disable_generic(clk);
		omap1_clk_disable(api_ck_p);
	}
}

const struct clkops clkops_dspck = {
	.enable		= omap1_clk_enable_dsp_domain,
	.disable	= omap1_clk_disable_dsp_domain,
};

static int omap1_clk_enable_uart_functional(struct clk *clk)
{
	int ret;
	struct uart_clk *uclk;

	ret = omap1_clk_enable_generic(clk);
	if (ret == 0) {
		/* Set smart idle acknowledgement mode */
		uclk = (struct uart_clk *)clk;
		omap_writeb((omap_readb(uclk->sysc_addr) & ~0x10) | 8,
			    uclk->sysc_addr);
	}

	return ret;
}

static void omap1_clk_disable_uart_functional(struct clk *clk)
{
	struct uart_clk *uclk;

	/* Set force idle acknowledgement mode */
	uclk = (struct uart_clk *)clk;
	omap_writeb((omap_readb(uclk->sysc_addr) & ~0x18), uclk->sysc_addr);

	omap1_clk_disable_generic(clk);
}

const struct clkops clkops_uart = {
	.enable		= omap1_clk_enable_uart_functional,
	.disable	= omap1_clk_disable_uart_functional,
};

long omap1_clk_round_rate(struct clk *clk, unsigned long rate)
{
	if (clk->round_rate != NULL)
		return clk->round_rate(clk, rate);

	return clk->rate;
}

int omap1_clk_set_rate(struct clk *clk, unsigned long rate)
{
	int  ret = -EINVAL;

	if (clk->set_rate)
		ret = clk->set_rate(clk, rate);
	return ret;
}

/*-------------------------------------------------------------------------
 * Omap1 clock reset and init functions
 *-------------------------------------------------------------------------*/

#ifdef CONFIG_OMAP_RESET_CLOCKS

void __init omap1_clk_disable_unused(struct clk *clk)
{
	__u32 regval32;

	/* Clocks in the DSP domain need api_ck. Just assume bootloader
	 * has not enabled any DSP clocks */
	if (clk->enable_reg == DSP_IDLECT2) {
		printk(KERN_INFO "Skipping reset check for DSP domain "
		       "clock \"%s\"\n", clk->name);
		return;
	}

	/* Is the clock already disabled? */
	if (clk->flags & ENABLE_REG_32BIT)
		regval32 = __raw_readl(clk->enable_reg);
	else
		regval32 = __raw_readw(clk->enable_reg);

	if ((regval32 & (1 << clk->enable_bit)) == 0)
		return;

	printk(KERN_INFO "Disabling unused clock \"%s\"... ", clk->name);
	clk->ops->disable(clk);
	printk(" done\n");
}

#endif
