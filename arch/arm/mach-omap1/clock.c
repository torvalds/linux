/*
 *  linux/arch/arm/mach-omap1/clock.c
 *
 *  Copyright (C) 2004 - 2005 Nokia corporation
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

#include <mach/cpu.h>
#include <mach/usb.h>
#include <mach/clock.h>
#include <mach/sram.h>

#include "clock.h"

__u32 arm_idlect1_mask;

/*-------------------------------------------------------------------------
 * Omap1 specific clock functions
 *-------------------------------------------------------------------------*/

static void omap1_watchdog_recalc(struct clk * clk)
{
	clk->rate = clk->parent->rate / 14;
}

static void omap1_uart_recalc(struct clk * clk)
{
	unsigned int val = omap_readl(clk->enable_reg);
	if (val & clk->enable_bit)
		clk->rate = 48000000;
	else
		clk->rate = 12000000;
}

static void omap1_sossi_recalc(struct clk *clk)
{
	u32 div = omap_readl(MOD_CONF_CTRL_1);

	div = (div >> 17) & 0x7;
	div++;
	clk->rate = clk->parent->rate / div;
}

static int omap1_clk_enable_dsp_domain(struct clk *clk)
{
	int retval;

	retval = omap1_clk_enable(&api_ck.clk);
	if (!retval) {
		retval = omap1_clk_enable_generic(clk);
		omap1_clk_disable(&api_ck.clk);
	}

	return retval;
}

static void omap1_clk_disable_dsp_domain(struct clk *clk)
{
	if (omap1_clk_enable(&api_ck.clk) == 0) {
		omap1_clk_disable_generic(clk);
		omap1_clk_disable(&api_ck.clk);
	}
}

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

	if (unlikely(!(clk->flags & RATE_CKCTL)))
		return -EINVAL;

	parent = clk->parent;
	if (unlikely(parent == 0))
		return -EIO;

	realrate = parent->rate;
	for (dsor_exp=0; dsor_exp<4; dsor_exp++) {
		if (realrate <= rate)
			break;

		realrate /= 2;
	}

	return dsor_exp;
}

static void omap1_ckctl_recalc(struct clk * clk)
{
	int dsor;

	/* Calculate divisor encoded as 2-bit exponent */
	dsor = 1 << (3 & (omap_readw(ARM_CKCTL) >> clk->rate_offset));

	if (unlikely(clk->rate == clk->parent->rate / dsor))
		return; /* No change, quick exit */
	clk->rate = clk->parent->rate / dsor;

	if (unlikely(clk->flags & RATE_PROPAGATES))
		propagate_rate(clk);
}

static void omap1_ckctl_recalc_dsp_domain(struct clk * clk)
{
	int dsor;

	/* Calculate divisor encoded as 2-bit exponent
	 *
	 * The clock control bits are in DSP domain,
	 * so api_ck is needed for access.
	 * Note that DSP_CKCTL virt addr = phys addr, so
	 * we must use __raw_readw() instead of omap_readw().
	 */
	omap1_clk_enable(&api_ck.clk);
	dsor = 1 << (3 & (__raw_readw(DSP_CKCTL) >> clk->rate_offset));
	omap1_clk_disable(&api_ck.clk);

	if (unlikely(clk->rate == clk->parent->rate / dsor))
		return; /* No change, quick exit */
	clk->rate = clk->parent->rate / dsor;

	if (unlikely(clk->flags & RATE_PROPAGATES))
		propagate_rate(clk);
}

/* MPU virtual clock functions */
static int omap1_select_table_rate(struct clk * clk, unsigned long rate)
{
	/* Find the highest supported frequency <= rate and switch to it */
	struct mpu_rate * ptr;

	if (clk != &virtual_ck_mpu)
		return -EINVAL;

	for (ptr = rate_table; ptr->rate; ptr++) {
		if (ptr->xtal != ck_ref.rate)
			continue;

		/* DPLL1 cannot be reprogrammed without risking system crash */
		if (likely(ck_dpll1.rate!=0) && ptr->pll_rate != ck_dpll1.rate)
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
	if (cpu_is_omap730())
		omap_sram_reprogram_clock(ptr->dpllctl_val, ptr->ckctl_val | 0x2000);
	else
		omap_sram_reprogram_clock(ptr->dpllctl_val, ptr->ckctl_val);

	ck_dpll1.rate = ptr->pll_rate;
	propagate_rate(&ck_dpll1);
	return 0;
}

static int omap1_clk_set_rate_dsp_domain(struct clk *clk, unsigned long rate)
{
	int  ret = -EINVAL;
	int  dsor_exp;
	__u16  regval;

	if (clk->flags & RATE_CKCTL) {
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
		ret = 0;
	}

	if (unlikely(ret == 0 && (clk->flags & RATE_PROPAGATES)))
		propagate_rate(clk);

	return ret;
}

static long omap1_round_to_table_rate(struct clk * clk, unsigned long rate)
{
	/* Find the highest supported frequency <= rate */
	struct mpu_rate * ptr;
	long  highest_rate;

	if (clk != &virtual_ck_mpu)
		return -EINVAL;

	highest_rate = -EINVAL;

	for (ptr = rate_table; ptr->rate; ptr++) {
		if (ptr->xtal != ck_ref.rate)
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

/* Only needed on 1510 */
static int omap1_set_uart_rate(struct clk * clk, unsigned long rate)
{
	unsigned int val;

	val = omap_readl(clk->enable_reg);
	if (rate == 12000000)
		val &= ~(1 << clk->enable_bit);
	else if (rate == 48000000)
		val |= (1 << clk->enable_bit);
	else
		return -EINVAL;
	omap_writel(val, clk->enable_reg);
	clk->rate = rate;

	return 0;
}

/* External clock (MCLK & BCLK) functions */
static int omap1_set_ext_clk_rate(struct clk * clk, unsigned long rate)
{
	unsigned dsor;
	__u16 ratio_bits;

	dsor = calc_ext_dsor(rate);
	clk->rate = 96000000 / dsor;
	if (dsor > 8)
		ratio_bits = ((dsor - 8) / 2 + 6) << 2;
	else
		ratio_bits = (dsor - 2) << 2;

	ratio_bits |= omap_readw(clk->enable_reg) & ~0xfd;
	omap_writew(ratio_bits, clk->enable_reg);

	return 0;
}

static int omap1_set_sossi_rate(struct clk *clk, unsigned long rate)
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
	if (unlikely(clk->flags & RATE_PROPAGATES))
		propagate_rate(clk);

	return 0;
}

static long omap1_round_ext_clk_rate(struct clk * clk, unsigned long rate)
{
	return 96000000 / calc_ext_dsor(rate);
}

static void omap1_init_ext_clk(struct clk * clk)
{
	unsigned dsor;
	__u16 ratio_bits;

	/* Determine current rate and ensure clock is based on 96MHz APLL */
	ratio_bits = omap_readw(clk->enable_reg) & ~1;
	omap_writew(ratio_bits, clk->enable_reg);

	ratio_bits = (ratio_bits & 0xfc) >> 2;
	if (ratio_bits > 6)
		dsor = (ratio_bits - 6) * 2 + 8;
	else
		dsor = ratio_bits + 2;

	clk-> rate = 96000000 / dsor;
}

static int omap1_clk_enable(struct clk *clk)
{
	int ret = 0;
	if (clk->usecount++ == 0) {
		if (likely(clk->parent)) {
			ret = omap1_clk_enable(clk->parent);

			if (unlikely(ret != 0)) {
				clk->usecount--;
				return ret;
			}

			if (clk->flags & CLOCK_NO_IDLE_PARENT)
				omap1_clk_deny_idle(clk->parent);
		}

		ret = clk->enable(clk);

		if (unlikely(ret != 0) && clk->parent) {
			omap1_clk_disable(clk->parent);
			clk->usecount--;
		}
	}

	return ret;
}

static void omap1_clk_disable(struct clk *clk)
{
	if (clk->usecount > 0 && !(--clk->usecount)) {
		clk->disable(clk);
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

	if (clk->flags & ALWAYS_ENABLED)
		return 0;

	if (unlikely(clk->enable_reg == 0)) {
		printk(KERN_ERR "clock.c: Enable for %s without enable code\n",
		       clk->name);
		return -EINVAL;
	}

	if (clk->flags & ENABLE_REG_32BIT) {
		if (clk->flags & VIRTUAL_IO_ADDRESS) {
			regval32 = __raw_readl(clk->enable_reg);
			regval32 |= (1 << clk->enable_bit);
			__raw_writel(regval32, clk->enable_reg);
		} else {
			regval32 = omap_readl(clk->enable_reg);
			regval32 |= (1 << clk->enable_bit);
			omap_writel(regval32, clk->enable_reg);
		}
	} else {
		if (clk->flags & VIRTUAL_IO_ADDRESS) {
			regval16 = __raw_readw(clk->enable_reg);
			regval16 |= (1 << clk->enable_bit);
			__raw_writew(regval16, clk->enable_reg);
		} else {
			regval16 = omap_readw(clk->enable_reg);
			regval16 |= (1 << clk->enable_bit);
			omap_writew(regval16, clk->enable_reg);
		}
	}

	return 0;
}

static void omap1_clk_disable_generic(struct clk *clk)
{
	__u16 regval16;
	__u32 regval32;

	if (clk->enable_reg == 0)
		return;

	if (clk->flags & ENABLE_REG_32BIT) {
		if (clk->flags & VIRTUAL_IO_ADDRESS) {
			regval32 = __raw_readl(clk->enable_reg);
			regval32 &= ~(1 << clk->enable_bit);
			__raw_writel(regval32, clk->enable_reg);
		} else {
			regval32 = omap_readl(clk->enable_reg);
			regval32 &= ~(1 << clk->enable_bit);
			omap_writel(regval32, clk->enable_reg);
		}
	} else {
		if (clk->flags & VIRTUAL_IO_ADDRESS) {
			regval16 = __raw_readw(clk->enable_reg);
			regval16 &= ~(1 << clk->enable_bit);
			__raw_writew(regval16, clk->enable_reg);
		} else {
			regval16 = omap_readw(clk->enable_reg);
			regval16 &= ~(1 << clk->enable_bit);
			omap_writew(regval16, clk->enable_reg);
		}
	}
}

static long omap1_clk_round_rate(struct clk *clk, unsigned long rate)
{
	int dsor_exp;

	if (clk->flags & RATE_FIXED)
		return clk->rate;

	if (clk->flags & RATE_CKCTL) {
		dsor_exp = calc_dsor_exp(clk, rate);
		if (dsor_exp < 0)
			return dsor_exp;
		if (dsor_exp > 3)
			dsor_exp = 3;
		return clk->parent->rate / (1 << dsor_exp);
	}

	if(clk->round_rate != 0)
		return clk->round_rate(clk, rate);

	return clk->rate;
}

static int omap1_clk_set_rate(struct clk *clk, unsigned long rate)
{
	int  ret = -EINVAL;
	int  dsor_exp;
	__u16  regval;

	if (clk->set_rate)
		ret = clk->set_rate(clk, rate);
	else if (clk->flags & RATE_CKCTL) {
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
		ret = 0;
	}

	if (unlikely(ret == 0 && (clk->flags & RATE_PROPAGATES)))
		propagate_rate(clk);

	return ret;
}

/*-------------------------------------------------------------------------
 * Omap1 clock reset and init functions
 *-------------------------------------------------------------------------*/

#ifdef CONFIG_OMAP_RESET_CLOCKS

static void __init omap1_clk_disable_unused(struct clk *clk)
{
	__u32 regval32;

	/* Clocks in the DSP domain need api_ck. Just assume bootloader
	 * has not enabled any DSP clocks */
	if ((u32)clk->enable_reg == DSP_IDLECT2) {
		printk(KERN_INFO "Skipping reset check for DSP domain "
		       "clock \"%s\"\n", clk->name);
		return;
	}

	/* Is the clock already disabled? */
	if (clk->flags & ENABLE_REG_32BIT) {
		if (clk->flags & VIRTUAL_IO_ADDRESS)
			regval32 = __raw_readl(clk->enable_reg);
			else
				regval32 = omap_readl(clk->enable_reg);
	} else {
		if (clk->flags & VIRTUAL_IO_ADDRESS)
			regval32 = __raw_readw(clk->enable_reg);
		else
			regval32 = omap_readw(clk->enable_reg);
	}

	if ((regval32 & (1 << clk->enable_bit)) == 0)
		return;

	/* FIXME: This clock seems to be necessary but no-one
	 * has asked for its activation. */
	if (clk == &tc2_ck		/* FIX: pm.c (SRAM), CCP, Camera */
	    || clk == &ck_dpll1out.clk	/* FIX: SoSSI, SSR */
	    || clk == &arm_gpio_ck	/* FIX: GPIO code for 1510 */
		) {
		printk(KERN_INFO "FIXME: Clock \"%s\" seems unused\n",
		       clk->name);
		return;
	}

	printk(KERN_INFO "Disabling unused clock \"%s\"... ", clk->name);
	clk->disable(clk);
	printk(" done\n");
}

#else
#define omap1_clk_disable_unused	NULL
#endif

static struct clk_functions omap1_clk_functions = {
	.clk_enable		= omap1_clk_enable,
	.clk_disable		= omap1_clk_disable,
	.clk_round_rate		= omap1_clk_round_rate,
	.clk_set_rate		= omap1_clk_set_rate,
	.clk_disable_unused	= omap1_clk_disable_unused,
};

int __init omap1_clk_init(void)
{
	struct clk ** clkp;
	const struct omap_clock_config *info;
	int crystal_type = 0; /* Default 12 MHz */
	u32 reg;

#ifdef CONFIG_DEBUG_LL
	/* Resets some clocks that may be left on from bootloader,
	 * but leaves serial clocks on.
 	 */
	omap_writel(0x3 << 29, MOD_CONF_CTRL_0);
#endif

	/* USB_REQ_EN will be disabled later if necessary (usb_dc_ck) */
	reg = omap_readw(SOFT_REQ_REG) & (1 << 4);
	omap_writew(reg, SOFT_REQ_REG);
	if (!cpu_is_omap15xx())
		omap_writew(0, SOFT_REQ_REG2);

	clk_init(&omap1_clk_functions);

	/* By default all idlect1 clocks are allowed to idle */
	arm_idlect1_mask = ~0;

	for (clkp = onchip_clks; clkp < onchip_clks+ARRAY_SIZE(onchip_clks); clkp++) {
		if (((*clkp)->flags &CLOCK_IN_OMAP1510) && cpu_is_omap1510()) {
			clk_register(*clkp);
			continue;
		}

		if (((*clkp)->flags &CLOCK_IN_OMAP16XX) && cpu_is_omap16xx()) {
			clk_register(*clkp);
			continue;
		}

		if (((*clkp)->flags &CLOCK_IN_OMAP730) && cpu_is_omap730()) {
			clk_register(*clkp);
			continue;
		}

		if (((*clkp)->flags &CLOCK_IN_OMAP310) && cpu_is_omap310()) {
			clk_register(*clkp);
			continue;
		}
	}

	info = omap_get_config(OMAP_TAG_CLOCK, struct omap_clock_config);
	if (info != NULL) {
		if (!cpu_is_omap15xx())
			crystal_type = info->system_clock_type;
	}

#if defined(CONFIG_ARCH_OMAP730)
	ck_ref.rate = 13000000;
#elif defined(CONFIG_ARCH_OMAP16XX)
	if (crystal_type == 2)
		ck_ref.rate = 19200000;
#endif

	printk("Clocks: ARM_SYSST: 0x%04x DPLL_CTL: 0x%04x ARM_CKCTL: 0x%04x\n",
	       omap_readw(ARM_SYSST), omap_readw(DPLL_CTL),
	       omap_readw(ARM_CKCTL));

	/* We want to be in syncronous scalable mode */
	omap_writew(0x1000, ARM_SYSST);

#ifdef CONFIG_OMAP_CLOCKS_SET_BY_BOOTLOADER
	/* Use values set by bootloader. Determine PLL rate and recalculate
	 * dependent clocks as if kernel had changed PLL or divisors.
	 */
	{
		unsigned pll_ctl_val = omap_readw(DPLL_CTL);

		ck_dpll1.rate = ck_ref.rate; /* Base xtal rate */
		if (pll_ctl_val & 0x10) {
			/* PLL enabled, apply multiplier and divisor */
			if (pll_ctl_val & 0xf80)
				ck_dpll1.rate *= (pll_ctl_val & 0xf80) >> 7;
			ck_dpll1.rate /= ((pll_ctl_val & 0x60) >> 5) + 1;
		} else {
			/* PLL disabled, apply bypass divisor */
			switch (pll_ctl_val & 0xc) {
			case 0:
				break;
			case 0x4:
				ck_dpll1.rate /= 2;
				break;
			default:
				ck_dpll1.rate /= 4;
				break;
			}
		}
	}
	propagate_rate(&ck_dpll1);
#else
	/* Find the highest supported frequency and enable it */
	if (omap1_select_table_rate(&virtual_ck_mpu, ~0)) {
		printk(KERN_ERR "System frequencies not set. Check your config.\n");
		/* Guess sane values (60MHz) */
		omap_writew(0x2290, DPLL_CTL);
		omap_writew(cpu_is_omap730() ? 0x3005 : 0x1005, ARM_CKCTL);
		ck_dpll1.rate = 60000000;
		propagate_rate(&ck_dpll1);
	}
#endif
	/* Cache rates for clocks connected to ck_ref (not dpll1) */
	propagate_rate(&ck_ref);
	printk(KERN_INFO "Clocking rate (xtal/DPLL1/MPU): "
		"%ld.%01ld/%ld.%01ld/%ld.%01ld MHz\n",
	       ck_ref.rate / 1000000, (ck_ref.rate / 100000) % 10,
	       ck_dpll1.rate / 1000000, (ck_dpll1.rate / 100000) % 10,
	       arm_ck.rate / 1000000, (arm_ck.rate / 100000) % 10);

#if defined(CONFIG_MACH_OMAP_PERSEUS2) || defined(CONFIG_MACH_OMAP_FSAMPLE)
	/* Select slicer output as OMAP input clock */
	omap_writew(omap_readw(OMAP730_PCC_UPLD_CTRL) & ~0x1, OMAP730_PCC_UPLD_CTRL);
#endif

	/* Amstrad Delta wants BCLK high when inactive */
	if (machine_is_ams_delta())
		omap_writel(omap_readl(ULPD_CLOCK_CTRL) |
				(1 << SDW_MCLK_INV_BIT),
				ULPD_CLOCK_CTRL);

	/* Turn off DSP and ARM_TIMXO. Make sure ARM_INTHCK is not divided */
	/* (on 730, bit 13 must not be cleared) */
	if (cpu_is_omap730())
		omap_writew(omap_readw(ARM_CKCTL) & 0x2fff, ARM_CKCTL);
	else
		omap_writew(omap_readw(ARM_CKCTL) & 0x0fff, ARM_CKCTL);

	/* Put DSP/MPUI into reset until needed */
	omap_writew(0, ARM_RSTCT1);
	omap_writew(1, ARM_RSTCT2);
	omap_writew(0x400, ARM_IDLECT1);

	/*
	 * According to OMAP5910 Erratum SYS_DMA_1, bit DMACK_REQ (bit 8)
	 * of the ARM_IDLECT2 register must be set to zero. The power-on
	 * default value of this bit is one.
	 */
	omap_writew(0x0000, ARM_IDLECT2);	/* Turn LCD clock off also */

	/*
	 * Only enable those clocks we will need, let the drivers
	 * enable other clocks as necessary
	 */
	clk_enable(&armper_ck.clk);
	clk_enable(&armxor_ck.clk);
	clk_enable(&armtim_ck.clk); /* This should be done by timer code */

	if (cpu_is_omap15xx())
		clk_enable(&arm_gpio_ck);

	return 0;
}

