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


#include "clock.h"
#include "clock2xxx.h"
#include "cm2xxx.h"
#include "cm-regbits-24xx.h"

/* CM_CLKEN_PLL.EN_{54,96}M_PLL options (24XX) */
#define EN_APLL_STOPPED			0
#define EN_APLL_LOCKED			3

/* CM_CLKSEL1_PLL.APLLS_CLKIN options (24XX) */
#define APLLS_CLKIN_19_2MHZ		0
#define APLLS_CLKIN_13MHZ		2
#define APLLS_CLKIN_12MHZ		3

/* Private functions */

static int _apll96_enable(struct clk *clk)
{
	return omap2xxx_cm_apll96_enable();
}

static int _apll54_enable(struct clk *clk)
{
	return omap2xxx_cm_apll54_enable();
}

static void _apll96_allow_idle(struct clk *clk)
{
	omap2xxx_cm_set_apll96_auto_low_power_stop();
}

static void _apll96_deny_idle(struct clk *clk)
{
	omap2xxx_cm_set_apll96_disable_autoidle();
}

static void _apll54_allow_idle(struct clk *clk)
{
	omap2xxx_cm_set_apll54_auto_low_power_stop();
}

static void _apll54_deny_idle(struct clk *clk)
{
	omap2xxx_cm_set_apll54_disable_autoidle();
}

static void _apll96_disable(struct clk *clk)
{
	omap2xxx_cm_apll96_disable();
}

static void _apll54_disable(struct clk *clk)
{
	omap2xxx_cm_apll54_disable();
}

/* Public data */

const struct clkops clkops_apll96 = {
	.enable		= _apll96_enable,
	.disable	= _apll96_disable,
	.allow_idle	= _apll96_allow_idle,
	.deny_idle	= _apll96_deny_idle,
};

const struct clkops clkops_apll54 = {
	.enable		= _apll54_enable,
	.disable	= _apll54_disable,
	.allow_idle	= _apll54_allow_idle,
	.deny_idle	= _apll54_deny_idle,
};

/* Public functions */

u32 omap2xxx_get_apll_clkin(void)
{
	u32 aplls, srate = 0;

	aplls = omap2_cm_read_mod_reg(PLL_MOD, CM_CLKSEL1);
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

