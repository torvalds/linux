/*
 * OMAP2xxx osc_clk-specific clock code
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
 */
#undef DEBUG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/io.h>

#include "clock.h"
#include "clock2xxx.h"
#include "prm2xxx_3xxx.h"
#include "prm-regbits-24xx.h"

/*
 * XXX This does not actually enable the osc_ck, since the osc_ck must
 * be running for this function to be called.  Instead, this function
 * is used to disable an autoidle mode on the osc_ck.  The existing
 * clk_enable/clk_disable()-based usecounting for osc_ck should be
 * replaced with autoidle-based usecounting.
 */
static int omap2_enable_osc_ck(struct clk *clk)
{
	u32 pcc;

	pcc = __raw_readl(prcm_clksrc_ctrl);

	__raw_writel(pcc & ~OMAP_AUTOEXTCLKMODE_MASK, prcm_clksrc_ctrl);

	return 0;
}

/*
 * XXX This does not actually disable the osc_ck, since doing so would
 * immediately halt the system.  Instead, this function is used to
 * enable an autoidle mode on the osc_ck.  The existing
 * clk_enable/clk_disable()-based usecounting for osc_ck should be
 * replaced with autoidle-based usecounting.
 */
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

unsigned long omap2_osc_clk_recalc(struct clk *clk)
{
	return omap2xxx_get_apll_clkin() * omap2xxx_get_sysclkdiv();
}

