/*
 * OMAP2xxx APLL clock control functions
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

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <plat/clock.h>
#include <plat/prcm.h>

#include "clock.h"
#include "clock2xxx.h"
#include "cm.h"
#include "cm-regbits-24xx.h"

/* CM_CLKEN_PLL.EN_{54,96}M_PLL options (24XX) */
#define EN_APLL_STOPPED			0
#define EN_APLL_LOCKED			3

/* CM_CLKSEL1_PLL.APLLS_CLKIN options (24XX) */
#define APLLS_CLKIN_19_2MHZ		0
#define APLLS_CLKIN_13MHZ		2
#define APLLS_CLKIN_12MHZ		3

void __iomem *cm_idlest_pll;

/* Private functions */

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

	omap2_cm_wait_idlest(cm_idlest_pll, status_mask,
			     OMAP24XX_CM_IDLEST_VAL, clk->name);

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

/* Public data */

const struct clkops clkops_apll96 = {
	.enable		= omap2_clk_apll96_enable,
	.disable	= omap2_clk_apll_disable,
};

const struct clkops clkops_apll54 = {
	.enable		= omap2_clk_apll54_enable,
	.disable	= omap2_clk_apll_disable,
};

/* Public functions */

u32 omap2xxx_get_apll_clkin(void)
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

