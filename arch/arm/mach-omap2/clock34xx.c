/*
 * OMAP3-specific clock framework functions
 *
 * Copyright (C) 2007-2008 Texas Instruments, Inc.
 * Copyright (C) 2007-2010 Nokia Corporation
 *
 * Paul Walmsley
 * Jouni Högander
 *
 * Parts of this code are based on code written by
 * Richard Woodruff, Tony Lindgren, Tuukka Tikkanen, Karthik Dasu
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#undef DEBUG

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/err.h>

#include <plat/cpu.h>
#include <plat/clock.h>

#include "clock.h"
#include "clock34xx.h"
#include "prm.h"
#include "prm-regbits-34xx.h"
#include "cm.h"
#include "cm-regbits-34xx.h"

/*
 * DPLL5_FREQ_FOR_USBHOST: USBHOST and USBTLL are the only clocks
 * that are sourced by DPLL5, and both of these require this clock
 * to be at 120 MHz for proper operation.
 */
#define DPLL5_FREQ_FOR_USBHOST		120000000

/*
 * In AM35xx IPSS, the {ICK,FCK} enable bits for modules are exported
 * in the same register at a bit offset of 0x8. The EN_ACK for ICK is
 * at an offset of 4 from ICK enable bit.
 */
#define AM35XX_IPSS_ICK_MASK			0xF
#define AM35XX_IPSS_ICK_EN_ACK_OFFSET 	0x4
#define AM35XX_IPSS_ICK_FCK_OFFSET		0x8
#define AM35XX_IPSS_CLK_IDLEST_VAL		0

/* needed by omap3_core_dpll_m2_set_rate() */
struct clk *sdrc_ick_p, *arm_fck_p;

/**
 * omap3430es2_clk_ssi_find_idlest - return CM_IDLEST info for SSI
 * @clk: struct clk * being enabled
 * @idlest_reg: void __iomem ** to store CM_IDLEST reg address into
 * @idlest_bit: pointer to a u8 to store the CM_IDLEST bit shift into
 * @idlest_val: pointer to a u8 to store the CM_IDLEST indicator
 *
 * The OMAP3430ES2 SSI target CM_IDLEST bit is at a different shift
 * from the CM_{I,F}CLKEN bit.  Pass back the correct info via
 * @idlest_reg and @idlest_bit.  No return value.
 */
static void omap3430es2_clk_ssi_find_idlest(struct clk *clk,
					    void __iomem **idlest_reg,
					    u8 *idlest_bit,
					    u8 *idlest_val)
{
	u32 r;

	r = (((__force u32)clk->enable_reg & ~0xf0) | 0x20);
	*idlest_reg = (__force void __iomem *)r;
	*idlest_bit = OMAP3430ES2_ST_SSI_IDLE_SHIFT;
	*idlest_val = OMAP34XX_CM_IDLEST_VAL;
}

const struct clkops clkops_omap3430es2_ssi_wait = {
	.enable		= omap2_dflt_clk_enable,
	.disable	= omap2_dflt_clk_disable,
	.find_idlest	= omap3430es2_clk_ssi_find_idlest,
	.find_companion = omap2_clk_dflt_find_companion,
};

/**
 * omap3430es2_clk_dss_usbhost_find_idlest - CM_IDLEST info for DSS, USBHOST
 * @clk: struct clk * being enabled
 * @idlest_reg: void __iomem ** to store CM_IDLEST reg address into
 * @idlest_bit: pointer to a u8 to store the CM_IDLEST bit shift into
 * @idlest_val: pointer to a u8 to store the CM_IDLEST indicator
 *
 * Some OMAP modules on OMAP3 ES2+ chips have both initiator and
 * target IDLEST bits.  For our purposes, we are concerned with the
 * target IDLEST bits, which exist at a different bit position than
 * the *CLKEN bit position for these modules (DSS and USBHOST) (The
 * default find_idlest code assumes that they are at the same
 * position.)  No return value.
 */
static void omap3430es2_clk_dss_usbhost_find_idlest(struct clk *clk,
						    void __iomem **idlest_reg,
						    u8 *idlest_bit,
						    u8 *idlest_val)
{
	u32 r;

	r = (((__force u32)clk->enable_reg & ~0xf0) | 0x20);
	*idlest_reg = (__force void __iomem *)r;
	/* USBHOST_IDLE has same shift */
	*idlest_bit = OMAP3430ES2_ST_DSS_IDLE_SHIFT;
	*idlest_val = OMAP34XX_CM_IDLEST_VAL;
}

const struct clkops clkops_omap3430es2_dss_usbhost_wait = {
	.enable		= omap2_dflt_clk_enable,
	.disable	= omap2_dflt_clk_disable,
	.find_idlest	= omap3430es2_clk_dss_usbhost_find_idlest,
	.find_companion = omap2_clk_dflt_find_companion,
};

/**
 * omap3430es2_clk_hsotgusb_find_idlest - return CM_IDLEST info for HSOTGUSB
 * @clk: struct clk * being enabled
 * @idlest_reg: void __iomem ** to store CM_IDLEST reg address into
 * @idlest_bit: pointer to a u8 to store the CM_IDLEST bit shift into
 * @idlest_val: pointer to a u8 to store the CM_IDLEST indicator
 *
 * The OMAP3430ES2 HSOTGUSB target CM_IDLEST bit is at a different
 * shift from the CM_{I,F}CLKEN bit.  Pass back the correct info via
 * @idlest_reg and @idlest_bit.  No return value.
 */
static void omap3430es2_clk_hsotgusb_find_idlest(struct clk *clk,
						 void __iomem **idlest_reg,
						 u8 *idlest_bit,
						 u8 *idlest_val)
{
	u32 r;

	r = (((__force u32)clk->enable_reg & ~0xf0) | 0x20);
	*idlest_reg = (__force void __iomem *)r;
	*idlest_bit = OMAP3430ES2_ST_HSOTGUSB_IDLE_SHIFT;
	*idlest_val = OMAP34XX_CM_IDLEST_VAL;
}

const struct clkops clkops_omap3430es2_hsotgusb_wait = {
	.enable		= omap2_dflt_clk_enable,
	.disable	= omap2_dflt_clk_disable,
	.find_idlest	= omap3430es2_clk_hsotgusb_find_idlest,
	.find_companion = omap2_clk_dflt_find_companion,
};

/**
 * omap36xx_pwrdn_clk_enable_with_hsdiv_restore - enable clocks suffering
 *         from HSDivider PWRDN problem Implements Errata ID: i556.
 * @clk: DPLL output struct clk
 *
 * 3630 only: dpll3_m3_ck, dpll4_m2_ck, dpll4_m3_ck, dpll4_m4_ck,
 * dpll4_m5_ck & dpll4_m6_ck dividers gets loaded with reset
 * valueafter their respective PWRDN bits are set.  Any dummy write
 * (Any other value different from the Read value) to the
 * corresponding CM_CLKSEL register will refresh the dividers.
 */
static int omap36xx_pwrdn_clk_enable_with_hsdiv_restore(struct clk *clk)
{
	u32 dummy_v, orig_v, clksel_shift;
	int ret;

	/* Clear PWRDN bit of HSDIVIDER */
	ret = omap2_dflt_clk_enable(clk);

	/* Restore the dividers */
	if (!ret) {
		clksel_shift = __ffs(clk->parent->clksel_mask);
		orig_v = __raw_readl(clk->parent->clksel_reg);
		dummy_v = orig_v;

		/* Write any other value different from the Read value */
		dummy_v ^= (1 << clksel_shift);
		__raw_writel(dummy_v, clk->parent->clksel_reg);

		/* Write the original divider */
		__raw_writel(orig_v, clk->parent->clksel_reg);
	}

	return ret;
}

const struct clkops clkops_omap36xx_pwrdn_with_hsdiv_wait_restore = {
	.enable		= omap36xx_pwrdn_clk_enable_with_hsdiv_restore,
	.disable	= omap2_dflt_clk_disable,
	.find_companion	= omap2_clk_dflt_find_companion,
	.find_idlest	= omap2_clk_dflt_find_idlest,
};

const struct clkops omap3_clkops_noncore_dpll_ops = {
	.enable		= omap3_noncore_dpll_enable,
	.disable	= omap3_noncore_dpll_disable,
};

/**
 * am35xx_clk_find_idlest - return clock ACK info for AM35XX IPSS
 * @clk: struct clk * being enabled
 * @idlest_reg: void __iomem ** to store CM_IDLEST reg address into
 * @idlest_bit: pointer to a u8 to store the CM_IDLEST bit shift into
 * @idlest_val: pointer to a u8 to store the CM_IDLEST indicator
 *
 * The interface clocks on AM35xx IPSS reflects the clock idle status
 * in the enable register itsel at a bit offset of 4 from the enable
 * bit. A value of 1 indicates that clock is enabled.
 */
static void am35xx_clk_find_idlest(struct clk *clk,
					    void __iomem **idlest_reg,
					    u8 *idlest_bit,
					    u8 *idlest_val)
{
	*idlest_reg = (__force void __iomem *)(clk->enable_reg);
	*idlest_bit = clk->enable_bit + AM35XX_IPSS_ICK_EN_ACK_OFFSET;
	*idlest_val = AM35XX_IPSS_CLK_IDLEST_VAL;
}

/**
 * am35xx_clk_find_companion - find companion clock to @clk
 * @clk: struct clk * to find the companion clock of
 * @other_reg: void __iomem ** to return the companion clock CM_*CLKEN va in
 * @other_bit: u8 ** to return the companion clock bit shift in
 *
 * Some clocks don't have companion clocks.  For example, modules with
 * only an interface clock (such as HECC) don't have a companion
 * clock.  Right now, this code relies on the hardware exporting a bit
 * in the correct companion register that indicates that the
 * nonexistent 'companion clock' is active.  Future patches will
 * associate this type of code with per-module data structures to
 * avoid this issue, and remove the casts.  No return value.
 */
static void am35xx_clk_find_companion(struct clk *clk, void __iomem **other_reg,
					    u8 *other_bit)
{
	*other_reg = (__force void __iomem *)(clk->enable_reg);
	if (clk->enable_bit & AM35XX_IPSS_ICK_MASK)
		*other_bit = clk->enable_bit + AM35XX_IPSS_ICK_FCK_OFFSET;
	else
		*other_bit = clk->enable_bit - AM35XX_IPSS_ICK_FCK_OFFSET;
}

const struct clkops clkops_am35xx_ipss_module_wait = {
	.enable		= omap2_dflt_clk_enable,
	.disable	= omap2_dflt_clk_disable,
	.find_idlest	= am35xx_clk_find_idlest,
	.find_companion	= am35xx_clk_find_companion,
};

/**
 * am35xx_clk_ipss_find_idlest - return CM_IDLEST info for IPSS
 * @clk: struct clk * being enabled
 * @idlest_reg: void __iomem ** to store CM_IDLEST reg address into
 * @idlest_bit: pointer to a u8 to store the CM_IDLEST bit shift into
 * @idlest_val: pointer to a u8 to store the CM_IDLEST indicator
 *
 * The IPSS target CM_IDLEST bit is at a different shift from the
 * CM_{I,F}CLKEN bit.  Pass back the correct info via @idlest_reg
 * and @idlest_bit.  No return value.
 */
static void am35xx_clk_ipss_find_idlest(struct clk *clk,
					    void __iomem **idlest_reg,
					    u8 *idlest_bit,
					    u8 *idlest_val)
{
	u32 r;

	r = (((__force u32)clk->enable_reg & ~0xf0) | 0x20);
	*idlest_reg = (__force void __iomem *)r;
	*idlest_bit = AM35XX_ST_IPSS_SHIFT;
	*idlest_val = OMAP34XX_CM_IDLEST_VAL;
}

const struct clkops clkops_am35xx_ipss_wait = {
	.enable		= omap2_dflt_clk_enable,
	.disable	= omap2_dflt_clk_disable,
	.find_idlest	= am35xx_clk_ipss_find_idlest,
	.find_companion	= omap2_clk_dflt_find_companion,
};

int omap3_dpll4_set_rate(struct clk *clk, unsigned long rate)
{
	/*
	 * According to the 12-5 CDP code from TI, "Limitation 2.5"
	 * on 3430ES1 prevents us from changing DPLL multipliers or dividers
	 * on DPLL4.
	 */
	if (omap_rev() == OMAP3430_REV_ES1_0) {
		printk(KERN_ERR "clock: DPLL4 cannot change rate due to "
		       "silicon 'Limitation 2.5' on 3430ES1.\n");
		return -EINVAL;
	}
	return omap3_noncore_dpll_set_rate(clk, rate);
}

void __init omap3_clk_lock_dpll5(void)
{
	struct clk *dpll5_clk;
	struct clk *dpll5_m2_clk;

	dpll5_clk = clk_get(NULL, "dpll5_ck");
	clk_set_rate(dpll5_clk, DPLL5_FREQ_FOR_USBHOST);
	clk_enable(dpll5_clk);

	/* Enable autoidle to allow it to enter low power bypass */
	omap3_dpll_allow_idle(dpll5_clk);

	/* Program dpll5_m2_clk divider for no division */
	dpll5_m2_clk = clk_get(NULL, "dpll5_m2_ck");
	clk_enable(dpll5_m2_clk);
	clk_set_rate(dpll5_m2_clk, DPLL5_FREQ_FOR_USBHOST);

	clk_disable(dpll5_m2_clk);
	clk_disable(dpll5_clk);
	return;
}

/* Common clock code */

/* REVISIT: Move this init stuff out into clock.c */

/*
 * Switch the MPU rate if specified on cmdline.
 * We cannot do this early until cmdline is parsed.
 */
static int __init omap3xxx_clk_arch_init(void)
{
	struct clk *osc_sys_ck, *dpll1_ck, *arm_fck, *core_ck;
	unsigned long osc_sys_rate;
	bool err = 0;

	if (!cpu_is_omap34xx())
		return 0;

	if (!mpurate)
		return -EINVAL;

	/* XXX test these for success */
	dpll1_ck = clk_get(NULL, "dpll1_ck");
	if (WARN(IS_ERR(dpll1_ck), "Failed to get dpll1_ck.\n"))
		err = 1;

	arm_fck = clk_get(NULL, "arm_fck");
	if (WARN(IS_ERR(arm_fck), "Failed to get arm_fck.\n"))
		err = 1;

	core_ck = clk_get(NULL, "core_ck");
	if (WARN(IS_ERR(core_ck), "Failed to get core_ck.\n"))
		err = 1;

	osc_sys_ck = clk_get(NULL, "osc_sys_ck");
	if (WARN(IS_ERR(osc_sys_ck), "Failed to get osc_sys_ck.\n"))
		err = 1;

	if (err)
		return -ENOENT;

	/* REVISIT: not yet ready for 343x */
	if (clk_set_rate(dpll1_ck, mpurate))
		printk(KERN_ERR "*** Unable to set MPU rate\n");

	recalculate_root_clocks();

	osc_sys_rate = clk_get_rate(osc_sys_ck);

	pr_info("Switched to new clocking rate (Crystal/Core/MPU): "
		"%ld.%01ld/%ld/%ld MHz\n",
		(osc_sys_rate / 1000000),
		((osc_sys_rate / 100000) % 10),
		(clk_get_rate(core_ck) / 1000000),
		(clk_get_rate(arm_fck) / 1000000));

	calibrate_delay();

	return 0;
}
arch_initcall(omap3xxx_clk_arch_init);


