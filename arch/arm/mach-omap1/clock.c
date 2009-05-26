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
#include <asm/clkdev.h>

#include <mach/cpu.h>
#include <mach/usb.h>
#include <mach/clock.h>
#include <mach/sram.h>

static const struct clkops clkops_generic;
static const struct clkops clkops_uart;
static const struct clkops clkops_dspck;

#include "clock.h"

static int clk_omap1_dummy_enable(struct clk *clk)
{
	return 0;
}

static void clk_omap1_dummy_disable(struct clk *clk)
{
}

static const struct clkops clkops_dummy = {
	.enable = clk_omap1_dummy_enable,
	.disable = clk_omap1_dummy_disable,
};

static struct clk dummy_ck = {
	.name	= "dummy",
	.ops	= &clkops_dummy,
	.flags	= RATE_FIXED,
};

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

#define CK_310	(1 << 0)
#define CK_730	(1 << 1)
#define CK_1510	(1 << 2)
#define CK_16XX	(1 << 3)

static struct omap_clk omap_clks[] = {
	/* non-ULPD clocks */
	CLK(NULL,	"ck_ref",	&ck_ref,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"ck_dpll1",	&ck_dpll1,	CK_16XX | CK_1510 | CK_310),
	/* CK_GEN1 clocks */
	CLK(NULL,	"ck_dpll1out",	&ck_dpll1out.clk, CK_16XX),
	CLK(NULL,	"ck_sossi",	&sossi_ck,	CK_16XX),
	CLK(NULL,	"arm_ck",	&arm_ck,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"armper_ck",	&armper_ck.clk,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"arm_gpio_ck",	&arm_gpio_ck,	CK_1510 | CK_310),
	CLK(NULL,	"armxor_ck",	&armxor_ck.clk,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"armtim_ck",	&armtim_ck.clk,	CK_16XX | CK_1510 | CK_310),
	CLK("omap_wdt",	"fck",		&armwdt_ck.clk,	CK_16XX | CK_1510 | CK_310),
	CLK("omap_wdt",	"ick",		&armper_ck.clk,	CK_16XX),
	CLK("omap_wdt", "ick",		&dummy_ck,	CK_1510 | CK_310),
	CLK(NULL,	"arminth_ck",	&arminth_ck1510, CK_1510 | CK_310),
	CLK(NULL,	"arminth_ck",	&arminth_ck16xx, CK_16XX),
	/* CK_GEN2 clocks */
	CLK(NULL,	"dsp_ck",	&dsp_ck,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"dspmmu_ck",	&dspmmu_ck,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"dspper_ck",	&dspper_ck,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"dspxor_ck",	&dspxor_ck,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"dsptim_ck",	&dsptim_ck,	CK_16XX | CK_1510 | CK_310),
	/* CK_GEN3 clocks */
	CLK(NULL,	"tc_ck",	&tc_ck.clk,	CK_16XX | CK_1510 | CK_310 | CK_730),
	CLK(NULL,	"tipb_ck",	&tipb_ck,	CK_1510 | CK_310),
	CLK(NULL,	"l3_ocpi_ck",	&l3_ocpi_ck,	CK_16XX),
	CLK(NULL,	"tc1_ck",	&tc1_ck,	CK_16XX),
	CLK(NULL,	"tc2_ck",	&tc2_ck,	CK_16XX),
	CLK(NULL,	"dma_ck",	&dma_ck,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"dma_lcdfree_ck", &dma_lcdfree_ck, CK_16XX),
	CLK(NULL,	"api_ck",	&api_ck.clk,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"lb_ck",	&lb_ck.clk,	CK_1510 | CK_310),
	CLK(NULL,	"rhea1_ck",	&rhea1_ck,	CK_16XX),
	CLK(NULL,	"rhea2_ck",	&rhea2_ck,	CK_16XX),
	CLK(NULL,	"lcd_ck",	&lcd_ck_16xx,	CK_16XX | CK_730),
	CLK(NULL,	"lcd_ck",	&lcd_ck_1510.clk, CK_1510 | CK_310),
	/* ULPD clocks */
	CLK(NULL,	"uart1_ck",	&uart1_1510,	CK_1510 | CK_310),
	CLK(NULL,	"uart1_ck",	&uart1_16xx.clk, CK_16XX),
	CLK(NULL,	"uart2_ck",	&uart2_ck,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"uart3_ck",	&uart3_1510,	CK_1510 | CK_310),
	CLK(NULL,	"uart3_ck",	&uart3_16xx.clk, CK_16XX),
	CLK(NULL,	"usb_clko",	&usb_clko,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"usb_hhc_ck",	&usb_hhc_ck1510, CK_1510 | CK_310),
	CLK(NULL,	"usb_hhc_ck",	&usb_hhc_ck16xx, CK_16XX),
	CLK(NULL,	"usb_dc_ck",	&usb_dc_ck,	CK_16XX),
	CLK(NULL,	"mclk",		&mclk_1510,	CK_1510 | CK_310),
	CLK(NULL,	"mclk",		&mclk_16xx,	CK_16XX),
	CLK(NULL,	"bclk",		&bclk_1510,	CK_1510 | CK_310),
	CLK(NULL,	"bclk",		&bclk_16xx,	CK_16XX),
	CLK("mmci-omap.0", "fck",	&mmc1_ck,	CK_16XX | CK_1510 | CK_310),
	CLK("mmci-omap.0", "ick",	&armper_ck.clk,	CK_16XX | CK_1510 | CK_310),
	CLK("mmci-omap.1", "fck",	&mmc2_ck,	CK_16XX),
	CLK("mmci-omap.1", "ick",	&armper_ck.clk,	CK_16XX),
	/* Virtual clocks */
	CLK(NULL,	"mpu",		&virtual_ck_mpu, CK_16XX | CK_1510 | CK_310),
	CLK("i2c_omap.1", "fck",	&i2c_fck,	CK_16XX | CK_1510 | CK_310),
	CLK("i2c_omap.1", "ick",	&i2c_ick,	CK_16XX),
	CLK("i2c_omap.1", "ick",	&dummy_ck,	CK_1510 | CK_310),
	CLK("omap_uwire", "fck",	&armxor_ck.clk,	CK_16XX | CK_1510 | CK_310),
	CLK("omap-mcbsp.1", "ick",	&dspper_ck,	CK_16XX),
	CLK("omap-mcbsp.1", "ick",	&dummy_ck,	CK_1510 | CK_310),
	CLK("omap-mcbsp.2", "ick",	&armper_ck.clk,	CK_16XX),
	CLK("omap-mcbsp.2", "ick",	&dummy_ck,	CK_1510 | CK_310),
	CLK("omap-mcbsp.3", "ick",	&dspper_ck,	CK_16XX),
	CLK("omap-mcbsp.3", "ick",	&dummy_ck,	CK_1510 | CK_310),
	CLK("omap-mcbsp.1", "fck",	&dspxor_ck,	CK_16XX | CK_1510 | CK_310),
	CLK("omap-mcbsp.2", "fck",	&armper_ck.clk,	CK_16XX | CK_1510 | CK_310),
	CLK("omap-mcbsp.3", "fck",	&dspxor_ck,	CK_16XX | CK_1510 | CK_310),
};

static int omap1_clk_enable_generic(struct clk * clk);
static int omap1_clk_enable(struct clk *clk);
static void omap1_clk_disable_generic(struct clk * clk);
static void omap1_clk_disable(struct clk *clk);

__u32 arm_idlect1_mask;

/*-------------------------------------------------------------------------
 * Omap1 specific clock functions
 *-------------------------------------------------------------------------*/

static unsigned long omap1_watchdog_recalc(struct clk *clk)
{
	return clk->parent->rate / 14;
}

static unsigned long omap1_uart_recalc(struct clk *clk)
{
	unsigned int val = __raw_readl(clk->enable_reg);
	return val & clk->enable_bit ? 48000000 : 12000000;
}

static unsigned long omap1_sossi_recalc(struct clk *clk)
{
	u32 div = omap_readl(MOD_CONF_CTRL_1);

	div = (div >> 17) & 0x7;
	div++;

	return clk->parent->rate / div;
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

static const struct clkops clkops_dspck = {
	.enable		= &omap1_clk_enable_dsp_domain,
	.disable	= &omap1_clk_disable_dsp_domain,
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

static const struct clkops clkops_uart = {
	.enable		= &omap1_clk_enable_uart_functional,
	.disable	= &omap1_clk_disable_uart_functional,
};

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

static unsigned long omap1_ckctl_recalc(struct clk *clk)
{
	/* Calculate divisor encoded as 2-bit exponent */
	int dsor = 1 << (3 & (omap_readw(ARM_CKCTL) >> clk->rate_offset));

	return clk->parent->rate / dsor;
}

static unsigned long omap1_ckctl_recalc_dsp_domain(struct clk *clk)
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

	return clk->parent->rate / dsor;
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
	return 0;
}

static int omap1_clk_set_rate_dsp_domain(struct clk *clk, unsigned long rate)
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

static long omap1_clk_round_rate_ckctl_arm(struct clk *clk, unsigned long rate)
{
	int dsor_exp = calc_dsor_exp(clk, rate);
	if (dsor_exp < 0)
		return dsor_exp;
	if (dsor_exp > 3)
		dsor_exp = 3;
	return clk->parent->rate / (1 << dsor_exp);
}

static int omap1_clk_set_rate_ckctl_arm(struct clk *clk, unsigned long rate)
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

	ratio_bits |= __raw_readw(clk->enable_reg) & ~0xfd;
	__raw_writew(ratio_bits, clk->enable_reg);

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
	ratio_bits = __raw_readw(clk->enable_reg) & ~1;
	__raw_writew(ratio_bits, clk->enable_reg);

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

static void omap1_clk_disable(struct clk *clk)
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

static const struct clkops clkops_generic = {
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static long omap1_clk_round_rate(struct clk *clk, unsigned long rate)
{
	if (clk->flags & RATE_FIXED)
		return clk->rate;

	if (clk->round_rate != NULL)
		return clk->round_rate(clk, rate);

	return clk->rate;
}

static int omap1_clk_set_rate(struct clk *clk, unsigned long rate)
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

static void __init omap1_clk_disable_unused(struct clk *clk)
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
	clk->ops->disable(clk);
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
	struct omap_clk *c;
	const struct omap_clock_config *info;
	int crystal_type = 0; /* Default 12 MHz */
	u32 reg, cpu_mask;

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

	for (c = omap_clks; c < omap_clks + ARRAY_SIZE(omap_clks); c++)
		clk_preinit(c->lk.clk);

	cpu_mask = 0;
	if (cpu_is_omap16xx())
		cpu_mask |= CK_16XX;
	if (cpu_is_omap1510())
		cpu_mask |= CK_1510;
	if (cpu_is_omap730())
		cpu_mask |= CK_730;
	if (cpu_is_omap310())
		cpu_mask |= CK_310;

	for (c = omap_clks; c < omap_clks + ARRAY_SIZE(omap_clks); c++)
		if (c->cpu & cpu_mask) {
			clkdev_add(&c->lk);
			clk_register(c->lk.clk);
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
#else
	/* Find the highest supported frequency and enable it */
	if (omap1_select_table_rate(&virtual_ck_mpu, ~0)) {
		printk(KERN_ERR "System frequencies not set. Check your config.\n");
		/* Guess sane values (60MHz) */
		omap_writew(0x2290, DPLL_CTL);
		omap_writew(cpu_is_omap730() ? 0x3005 : 0x1005, ARM_CKCTL);
		ck_dpll1.rate = 60000000;
	}
#endif
	propagate_rate(&ck_dpll1);
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
