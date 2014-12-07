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

static void _apll96_allow_idle(struct clk_hw_omap *clk)
{
	omap2xxx_cm_set_apll96_auto_low_power_stop();
}

static void _apll96_deny_idle(struct clk_hw_omap *clk)
{
	omap2xxx_cm_set_apll96_disable_autoidle();
}

static void _apll54_allow_idle(struct clk_hw_omap *clk)
{
	omap2xxx_cm_set_apll54_auto_low_power_stop();
}

static void _apll54_deny_idle(struct clk_hw_omap *clk)
{
	omap2xxx_cm_set_apll54_disable_autoidle();
}

/* Public data */
const struct clk_hw_omap_ops clkhwops_apll54 = {
	.allow_idle	= _apll54_allow_idle,
	.deny_idle	= _apll54_deny_idle,
};

const struct clk_hw_omap_ops clkhwops_apll96 = {
	.allow_idle	= _apll96_allow_idle,
	.deny_idle	= _apll96_deny_idle,
};

