// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/mach-omap1/clock.c
 *
 *  Copyright (C) 2004 - 2005, 2009-2010 Nokia Corporation
 *  Written by Tuukka Tikkanen <tuukka.tikkanen@elektrobit.com>
 *
 *  Modified to use omap shared clock framework by
 *  Tony Lindgren <tony@atomide.com>
 */
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/soc/ti/omap1-io.h>
#include <linux/spinlock.h>

#include <asm/mach-types.h>

#include "hardware.h"
#include "soc.h"
#include "iomap.h"
#include "clock.h"
#include "opp.h"
#include "sram.h"

__u32 arm_idlect1_mask;
/* provide direct internal access (not via clk API) to some clocks */
struct omap1_clk *api_ck_p, *ck_dpll1_p, *ck_ref_p;

/* protect registeres shared among clk_enable/disable() and clk_set_rate() operations */
static DEFINE_SPINLOCK(arm_ckctl_lock);
static DEFINE_SPINLOCK(arm_idlect2_lock);
static DEFINE_SPINLOCK(mod_conf_ctrl_0_lock);
static DEFINE_SPINLOCK(mod_conf_ctrl_1_lock);
static DEFINE_SPINLOCK(swd_clk_div_ctrl_sel_lock);

/*
 * Omap1 specific clock functions
 */

unsigned long omap1_uart_recalc(struct omap1_clk *clk, unsigned long p_rate)
{
	unsigned int val = __raw_readl(clk->enable_reg);
	return val & 1 << clk->enable_bit ? 48000000 : 12000000;
}

unsigned long omap1_sossi_recalc(struct omap1_clk *clk, unsigned long p_rate)
{
	u32 div = omap_readl(MOD_CONF_CTRL_1);

	div = (div >> 17) & 0x7;
	div++;

	return p_rate / div;
}

static void omap1_clk_allow_idle(struct omap1_clk *clk)
{
	struct arm_idlect1_clk * iclk = (struct arm_idlect1_clk *)clk;

	if (!(clk->flags & CLOCK_IDLE_CONTROL))
		return;

	if (iclk->no_idle_count > 0 && !(--iclk->no_idle_count))
		arm_idlect1_mask |= 1 << iclk->idlect_shift;
}

static void omap1_clk_deny_idle(struct omap1_clk *clk)
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

static int calc_dsor_exp(unsigned long rate, unsigned long realrate)
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
	unsigned  dsor_exp;

	if (unlikely(realrate == 0))
		return -EIO;

	for (dsor_exp=0; dsor_exp<4; dsor_exp++) {
		if (realrate <= rate)
			break;

		realrate /= 2;
	}

	return dsor_exp;
}

unsigned long omap1_ckctl_recalc(struct omap1_clk *clk, unsigned long p_rate)
{
	/* Calculate divisor encoded as 2-bit exponent */
	int dsor = 1 << (3 & (omap_readw(ARM_CKCTL) >> clk->rate_offset));

	/* update locally maintained rate, required by arm_ck for omap1_show_rates() */
	clk->rate = p_rate / dsor;
	return clk->rate;
}

static int omap1_clk_is_enabled(struct clk_hw *hw)
{
	struct omap1_clk *clk = to_omap1_clk(hw);
	bool api_ck_was_enabled = true;
	__u32 regval32;
	int ret;

	if (!clk->ops)	/* no gate -- always enabled */
		return 1;

	if (clk->ops == &clkops_dspck) {
		api_ck_was_enabled = omap1_clk_is_enabled(&api_ck_p->hw);
		if (!api_ck_was_enabled)
			if (api_ck_p->ops->enable(api_ck_p) < 0)
				return 0;
	}

	if (clk->flags & ENABLE_REG_32BIT)
		regval32 = __raw_readl(clk->enable_reg);
	else
		regval32 = __raw_readw(clk->enable_reg);

	ret = regval32 & (1 << clk->enable_bit);

	if (!api_ck_was_enabled)
		api_ck_p->ops->disable(api_ck_p);

	return ret;
}


unsigned long omap1_ckctl_recalc_dsp_domain(struct omap1_clk *clk, unsigned long p_rate)
{
	bool api_ck_was_enabled;
	int dsor;

	/* Calculate divisor encoded as 2-bit exponent
	 *
	 * The clock control bits are in DSP domain,
	 * so api_ck is needed for access.
	 * Note that DSP_CKCTL virt addr = phys addr, so
	 * we must use __raw_readw() instead of omap_readw().
	 */
	api_ck_was_enabled = omap1_clk_is_enabled(&api_ck_p->hw);
	if (!api_ck_was_enabled)
		api_ck_p->ops->enable(api_ck_p);
	dsor = 1 << (3 & (__raw_readw(DSP_CKCTL) >> clk->rate_offset));
	if (!api_ck_was_enabled)
		api_ck_p->ops->disable(api_ck_p);

	return p_rate / dsor;
}

/* MPU virtual clock functions */
int omap1_select_table_rate(struct omap1_clk *clk, unsigned long rate, unsigned long p_rate)
{
	/* Find the highest supported frequency <= rate and switch to it */
	struct mpu_rate * ptr;
	unsigned long ref_rate;

	ref_rate = ck_ref_p->rate;

	for (ptr = omap1_rate_table; ptr->rate; ptr++) {
		if (!(ptr->flags & cpu_mask))
			continue;

		if (ptr->xtal != ref_rate)
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
	 */
	omap_sram_reprogram_clock(ptr->dpllctl_val, ptr->ckctl_val);

	/* XXX Do we need to recalculate the tree below DPLL1 at this point? */
	ck_dpll1_p->rate = ptr->pll_rate;

	return 0;
}

int omap1_clk_set_rate_dsp_domain(struct omap1_clk *clk, unsigned long rate, unsigned long p_rate)
{
	int dsor_exp;
	u16 regval;

	dsor_exp = calc_dsor_exp(rate, p_rate);
	if (dsor_exp > 3)
		dsor_exp = -EINVAL;
	if (dsor_exp < 0)
		return dsor_exp;

	regval = __raw_readw(DSP_CKCTL);
	regval &= ~(3 << clk->rate_offset);
	regval |= dsor_exp << clk->rate_offset;
	__raw_writew(regval, DSP_CKCTL);
	clk->rate = p_rate / (1 << dsor_exp);

	return 0;
}

long omap1_clk_round_rate_ckctl_arm(struct omap1_clk *clk, unsigned long rate,
				    unsigned long *p_rate)
{
	int dsor_exp = calc_dsor_exp(rate, *p_rate);

	if (dsor_exp < 0)
		return dsor_exp;
	if (dsor_exp > 3)
		dsor_exp = 3;
	return *p_rate / (1 << dsor_exp);
}

int omap1_clk_set_rate_ckctl_arm(struct omap1_clk *clk, unsigned long rate, unsigned long p_rate)
{
	unsigned long flags;
	int dsor_exp;
	u16 regval;

	dsor_exp = calc_dsor_exp(rate, p_rate);
	if (dsor_exp > 3)
		dsor_exp = -EINVAL;
	if (dsor_exp < 0)
		return dsor_exp;

	/* protect ARM_CKCTL register from concurrent access via clk_enable/disable() */
	spin_lock_irqsave(&arm_ckctl_lock, flags);

	regval = omap_readw(ARM_CKCTL);
	regval &= ~(3 << clk->rate_offset);
	regval |= dsor_exp << clk->rate_offset;
	regval = verify_ckctl_value(regval);
	omap_writew(regval, ARM_CKCTL);
	clk->rate = p_rate / (1 << dsor_exp);

	spin_unlock_irqrestore(&arm_ckctl_lock, flags);

	return 0;
}

long omap1_round_to_table_rate(struct omap1_clk *clk, unsigned long rate, unsigned long *p_rate)
{
	/* Find the highest supported frequency <= rate */
	struct mpu_rate * ptr;
	long highest_rate;
	unsigned long ref_rate;

	ref_rate = ck_ref_p->rate;

	highest_rate = -EINVAL;

	for (ptr = omap1_rate_table; ptr->rate; ptr++) {
		if (!(ptr->flags & cpu_mask))
			continue;

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
long omap1_round_uart_rate(struct omap1_clk *clk, unsigned long rate, unsigned long *p_rate)
{
	return rate > 24000000 ? 48000000 : 12000000;
}

int omap1_set_uart_rate(struct omap1_clk *clk, unsigned long rate, unsigned long p_rate)
{
	unsigned long flags;
	unsigned int val;

	if (rate == 12000000)
		val = 0;
	else if (rate == 48000000)
		val = 1 << clk->enable_bit;
	else
		return -EINVAL;

	/* protect MOD_CONF_CTRL_0 register from concurrent access via clk_enable/disable() */
	spin_lock_irqsave(&mod_conf_ctrl_0_lock, flags);

	val |= __raw_readl(clk->enable_reg) & ~(1 << clk->enable_bit);
	__raw_writel(val, clk->enable_reg);

	spin_unlock_irqrestore(&mod_conf_ctrl_0_lock, flags);

	clk->rate = rate;

	return 0;
}

/* External clock (MCLK & BCLK) functions */
int omap1_set_ext_clk_rate(struct omap1_clk *clk, unsigned long rate, unsigned long p_rate)
{
	unsigned long flags;
	unsigned dsor;
	__u16 ratio_bits;

	dsor = calc_ext_dsor(rate);
	clk->rate = 96000000 / dsor;
	if (dsor > 8)
		ratio_bits = ((dsor - 8) / 2 + 6) << 2;
	else
		ratio_bits = (dsor - 2) << 2;

	/* protect SWD_CLK_DIV_CTRL_SEL register from concurrent access via clk_enable/disable() */
	spin_lock_irqsave(&swd_clk_div_ctrl_sel_lock, flags);

	ratio_bits |= __raw_readw(clk->enable_reg) & ~0xfd;
	__raw_writew(ratio_bits, clk->enable_reg);

	spin_unlock_irqrestore(&swd_clk_div_ctrl_sel_lock, flags);

	return 0;
}

static int calc_div_sossi(unsigned long rate, unsigned long p_rate)
{
	int div;

	/* Round towards slower frequency */
	div = (p_rate + rate - 1) / rate;

	return --div;
}

long omap1_round_sossi_rate(struct omap1_clk *clk, unsigned long rate, unsigned long *p_rate)
{
	int div;

	div = calc_div_sossi(rate, *p_rate);
	if (div < 0)
		div = 0;
	else if (div > 7)
		div = 7;

	return *p_rate / (div + 1);
}

int omap1_set_sossi_rate(struct omap1_clk *clk, unsigned long rate, unsigned long p_rate)
{
	unsigned long flags;
	u32 l;
	int div;

	div = calc_div_sossi(rate, p_rate);
	if (div < 0 || div > 7)
		return -EINVAL;

	/* protect MOD_CONF_CTRL_1 register from concurrent access via clk_enable/disable() */
	spin_lock_irqsave(&mod_conf_ctrl_1_lock, flags);

	l = omap_readl(MOD_CONF_CTRL_1);
	l &= ~(7 << 17);
	l |= div << 17;
	omap_writel(l, MOD_CONF_CTRL_1);

	clk->rate = p_rate / (div + 1);

	spin_unlock_irqrestore(&mod_conf_ctrl_1_lock, flags);

	return 0;
}

long omap1_round_ext_clk_rate(struct omap1_clk *clk, unsigned long rate, unsigned long *p_rate)
{
	return 96000000 / calc_ext_dsor(rate);
}

int omap1_init_ext_clk(struct omap1_clk *clk)
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

	return 0;
}

static int omap1_clk_enable(struct clk_hw *hw)
{
	struct omap1_clk *clk = to_omap1_clk(hw), *parent = to_omap1_clk(clk_hw_get_parent(hw));
	int ret = 0;

	if (parent && clk->flags & CLOCK_NO_IDLE_PARENT)
		omap1_clk_deny_idle(parent);

	if (clk->ops && !(WARN_ON(!clk->ops->enable)))
		ret = clk->ops->enable(clk);

	return ret;
}

static void omap1_clk_disable(struct clk_hw *hw)
{
	struct omap1_clk *clk = to_omap1_clk(hw), *parent = to_omap1_clk(clk_hw_get_parent(hw));

	if (clk->ops && !(WARN_ON(!clk->ops->disable)))
		clk->ops->disable(clk);

	if (likely(parent) && clk->flags & CLOCK_NO_IDLE_PARENT)
		omap1_clk_allow_idle(parent);
}

static int omap1_clk_enable_generic(struct omap1_clk *clk)
{
	unsigned long flags;
	__u16 regval16;
	__u32 regval32;

	if (unlikely(clk->enable_reg == NULL)) {
		printk(KERN_ERR "clock.c: Enable for %s without enable code\n",
		       clk_hw_get_name(&clk->hw));
		return -EINVAL;
	}

	/* protect clk->enable_reg from concurrent access via clk_set_rate() */
	if (clk->enable_reg == OMAP1_IO_ADDRESS(ARM_CKCTL))
		spin_lock_irqsave(&arm_ckctl_lock, flags);
	else if (clk->enable_reg == OMAP1_IO_ADDRESS(ARM_IDLECT2))
		spin_lock_irqsave(&arm_idlect2_lock, flags);
	else if (clk->enable_reg == OMAP1_IO_ADDRESS(MOD_CONF_CTRL_0))
		spin_lock_irqsave(&mod_conf_ctrl_0_lock, flags);
	else if (clk->enable_reg == OMAP1_IO_ADDRESS(MOD_CONF_CTRL_1))
		spin_lock_irqsave(&mod_conf_ctrl_1_lock, flags);
	else if (clk->enable_reg == OMAP1_IO_ADDRESS(SWD_CLK_DIV_CTRL_SEL))
		spin_lock_irqsave(&swd_clk_div_ctrl_sel_lock, flags);

	if (clk->flags & ENABLE_REG_32BIT) {
		regval32 = __raw_readl(clk->enable_reg);
		regval32 |= (1 << clk->enable_bit);
		__raw_writel(regval32, clk->enable_reg);
	} else {
		regval16 = __raw_readw(clk->enable_reg);
		regval16 |= (1 << clk->enable_bit);
		__raw_writew(regval16, clk->enable_reg);
	}

	if (clk->enable_reg == OMAP1_IO_ADDRESS(ARM_CKCTL))
		spin_unlock_irqrestore(&arm_ckctl_lock, flags);
	else if (clk->enable_reg == OMAP1_IO_ADDRESS(ARM_IDLECT2))
		spin_unlock_irqrestore(&arm_idlect2_lock, flags);
	else if (clk->enable_reg == OMAP1_IO_ADDRESS(MOD_CONF_CTRL_0))
		spin_unlock_irqrestore(&mod_conf_ctrl_0_lock, flags);
	else if (clk->enable_reg == OMAP1_IO_ADDRESS(MOD_CONF_CTRL_1))
		spin_unlock_irqrestore(&mod_conf_ctrl_1_lock, flags);
	else if (clk->enable_reg == OMAP1_IO_ADDRESS(SWD_CLK_DIV_CTRL_SEL))
		spin_unlock_irqrestore(&swd_clk_div_ctrl_sel_lock, flags);

	return 0;
}

static void omap1_clk_disable_generic(struct omap1_clk *clk)
{
	unsigned long flags;
	__u16 regval16;
	__u32 regval32;

	if (clk->enable_reg == NULL)
		return;

	/* protect clk->enable_reg from concurrent access via clk_set_rate() */
	if (clk->enable_reg == OMAP1_IO_ADDRESS(ARM_CKCTL))
		spin_lock_irqsave(&arm_ckctl_lock, flags);
	else if (clk->enable_reg == OMAP1_IO_ADDRESS(ARM_IDLECT2))
		spin_lock_irqsave(&arm_idlect2_lock, flags);
	else if (clk->enable_reg == OMAP1_IO_ADDRESS(MOD_CONF_CTRL_0))
		spin_lock_irqsave(&mod_conf_ctrl_0_lock, flags);
	else if (clk->enable_reg == OMAP1_IO_ADDRESS(MOD_CONF_CTRL_1))
		spin_lock_irqsave(&mod_conf_ctrl_1_lock, flags);
	else if (clk->enable_reg == OMAP1_IO_ADDRESS(SWD_CLK_DIV_CTRL_SEL))
		spin_lock_irqsave(&swd_clk_div_ctrl_sel_lock, flags);

	if (clk->flags & ENABLE_REG_32BIT) {
		regval32 = __raw_readl(clk->enable_reg);
		regval32 &= ~(1 << clk->enable_bit);
		__raw_writel(regval32, clk->enable_reg);
	} else {
		regval16 = __raw_readw(clk->enable_reg);
		regval16 &= ~(1 << clk->enable_bit);
		__raw_writew(regval16, clk->enable_reg);
	}

	if (clk->enable_reg == OMAP1_IO_ADDRESS(ARM_CKCTL))
		spin_unlock_irqrestore(&arm_ckctl_lock, flags);
	else if (clk->enable_reg == OMAP1_IO_ADDRESS(ARM_IDLECT2))
		spin_unlock_irqrestore(&arm_idlect2_lock, flags);
	else if (clk->enable_reg == OMAP1_IO_ADDRESS(MOD_CONF_CTRL_0))
		spin_unlock_irqrestore(&mod_conf_ctrl_0_lock, flags);
	else if (clk->enable_reg == OMAP1_IO_ADDRESS(MOD_CONF_CTRL_1))
		spin_unlock_irqrestore(&mod_conf_ctrl_1_lock, flags);
	else if (clk->enable_reg == OMAP1_IO_ADDRESS(SWD_CLK_DIV_CTRL_SEL))
		spin_unlock_irqrestore(&swd_clk_div_ctrl_sel_lock, flags);
}

const struct clkops clkops_generic = {
	.enable		= omap1_clk_enable_generic,
	.disable	= omap1_clk_disable_generic,
};

static int omap1_clk_enable_dsp_domain(struct omap1_clk *clk)
{
	bool api_ck_was_enabled;
	int retval = 0;

	api_ck_was_enabled = omap1_clk_is_enabled(&api_ck_p->hw);
	if (!api_ck_was_enabled)
		retval = api_ck_p->ops->enable(api_ck_p);

	if (!retval) {
		retval = omap1_clk_enable_generic(clk);

		if (!api_ck_was_enabled)
			api_ck_p->ops->disable(api_ck_p);
	}

	return retval;
}

static void omap1_clk_disable_dsp_domain(struct omap1_clk *clk)
{
	bool api_ck_was_enabled;

	api_ck_was_enabled = omap1_clk_is_enabled(&api_ck_p->hw);
	if (!api_ck_was_enabled)
		if (api_ck_p->ops->enable(api_ck_p) < 0)
			return;

	omap1_clk_disable_generic(clk);

	if (!api_ck_was_enabled)
		api_ck_p->ops->disable(api_ck_p);
}

const struct clkops clkops_dspck = {
	.enable		= omap1_clk_enable_dsp_domain,
	.disable	= omap1_clk_disable_dsp_domain,
};

/* XXX SYSC register handling does not belong in the clock framework */
static int omap1_clk_enable_uart_functional_16xx(struct omap1_clk *clk)
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

/* XXX SYSC register handling does not belong in the clock framework */
static void omap1_clk_disable_uart_functional_16xx(struct omap1_clk *clk)
{
	struct uart_clk *uclk;

	/* Set force idle acknowledgement mode */
	uclk = (struct uart_clk *)clk;
	omap_writeb((omap_readb(uclk->sysc_addr) & ~0x18), uclk->sysc_addr);

	omap1_clk_disable_generic(clk);
}

/* XXX SYSC register handling does not belong in the clock framework */
const struct clkops clkops_uart_16xx = {
	.enable		= omap1_clk_enable_uart_functional_16xx,
	.disable	= omap1_clk_disable_uart_functional_16xx,
};

static unsigned long omap1_clk_recalc_rate(struct clk_hw *hw, unsigned long p_rate)
{
	struct omap1_clk *clk = to_omap1_clk(hw);

	if (clk->recalc)
		return clk->recalc(clk, p_rate);

	return clk->rate;
}

static long omap1_clk_round_rate(struct clk_hw *hw, unsigned long rate, unsigned long *p_rate)
{
	struct omap1_clk *clk = to_omap1_clk(hw);

	if (clk->round_rate != NULL)
		return clk->round_rate(clk, rate, p_rate);

	return omap1_clk_recalc_rate(hw, *p_rate);
}

static int omap1_clk_set_rate(struct clk_hw *hw, unsigned long rate, unsigned long p_rate)
{
	struct omap1_clk *clk = to_omap1_clk(hw);
	int  ret = -EINVAL;

	if (clk->set_rate)
		ret = clk->set_rate(clk, rate, p_rate);
	return ret;
}

/*
 * Omap1 clock reset and init functions
 */

static int omap1_clk_init_op(struct clk_hw *hw)
{
	struct omap1_clk *clk = to_omap1_clk(hw);

	if (clk->init)
		return clk->init(clk);

	return 0;
}

#ifdef CONFIG_OMAP_RESET_CLOCKS

static void omap1_clk_disable_unused(struct clk_hw *hw)
{
	struct omap1_clk *clk = to_omap1_clk(hw);
	const char *name = clk_hw_get_name(hw);

	/* Clocks in the DSP domain need api_ck. Just assume bootloader
	 * has not enabled any DSP clocks */
	if (clk->enable_reg == DSP_IDLECT2) {
		pr_info("Skipping reset check for DSP domain clock \"%s\"\n", name);
		return;
	}

	pr_info("Disabling unused clock \"%s\"... ", name);
	omap1_clk_disable(hw);
	printk(" done\n");
}

#endif

const struct clk_ops omap1_clk_gate_ops = {
	.enable		= omap1_clk_enable,
	.disable	= omap1_clk_disable,
	.is_enabled	= omap1_clk_is_enabled,
#ifdef CONFIG_OMAP_RESET_CLOCKS
	.disable_unused	= omap1_clk_disable_unused,
#endif
};

const struct clk_ops omap1_clk_rate_ops = {
	.recalc_rate	= omap1_clk_recalc_rate,
	.round_rate	= omap1_clk_round_rate,
	.set_rate	= omap1_clk_set_rate,
	.init		= omap1_clk_init_op,
};

const struct clk_ops omap1_clk_full_ops = {
	.enable		= omap1_clk_enable,
	.disable	= omap1_clk_disable,
	.is_enabled	= omap1_clk_is_enabled,
#ifdef CONFIG_OMAP_RESET_CLOCKS
	.disable_unused	= omap1_clk_disable_unused,
#endif
	.recalc_rate	= omap1_clk_recalc_rate,
	.round_rate	= omap1_clk_round_rate,
	.set_rate	= omap1_clk_set_rate,
	.init		= omap1_clk_init_op,
};

/*
 * OMAP specific clock functions shared between omap1 and omap2
 */

/* Used for clocks that always have same value as the parent clock */
unsigned long followparent_recalc(struct omap1_clk *clk, unsigned long p_rate)
{
	return p_rate;
}

/*
 * Used for clocks that have the same value as the parent clock,
 * divided by some factor
 */
unsigned long omap_fixed_divisor_recalc(struct omap1_clk *clk, unsigned long p_rate)
{
	WARN_ON(!clk->fixed_div);

	return p_rate / clk->fixed_div;
}

/* Propagate rate to children */
void propagate_rate(struct omap1_clk *tclk)
{
	struct clk *clkp;

	/* depend on CCF ability to recalculate new rates across whole clock subtree */
	if (WARN_ON(!(clk_hw_get_flags(&tclk->hw) & CLK_GET_RATE_NOCACHE)))
		return;

	clkp = clk_get_sys(NULL, clk_hw_get_name(&tclk->hw));
	if (WARN_ON(!clkp))
		return;

	clk_get_rate(clkp);
	clk_put(clkp);
}

const struct clk_ops omap1_clk_null_ops = {
};

/*
 * Dummy clock
 *
 * Used for clock aliases that are needed on some OMAPs, but not others
 */
struct omap1_clk dummy_ck __refdata = {
	.hw.init	= CLK_HW_INIT_NO_PARENT("dummy", &omap1_clk_null_ops, 0),
};
