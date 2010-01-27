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


