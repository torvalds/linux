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

u32 omap2xxx_get_sysclkdiv(void)
{
	u32 div;

	div = __raw_readl(prcm_clksrc_ctrl);
	div &= OMAP_SYSCLKDIV_MASK;
	div >>= OMAP_SYSCLKDIV_SHIFT;

	return div;
}

unsigned long omap2_sys_clk_recalc(struct clk *clk)
{
	return clk->parent->rate / omap2xxx_get_sysclkdiv();
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


