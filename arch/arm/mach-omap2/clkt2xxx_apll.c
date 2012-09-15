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

#ifdef CONFIG_COMMON_CLK
/**
 * omap2xxx_clk_apll_locked - is the APLL locked?
 * @hw: struct clk_hw * of the APLL to check
 *
 * If the APLL IP block referred to by @hw indicates that it's locked,
 * return true; otherwise, return false.
 */
static bool omap2xxx_clk_apll_locked(struct clk_hw *hw)
{
	struct clk_hw_omap *clk = to_clk_hw_omap(hw);
	u32 r, apll_mask;

	apll_mask = EN_APLL_LOCKED << clk->enable_bit;

	r = omap2_cm_read_mod_reg(PLL_MOD, CM_CLKEN);

	return ((r & apll_mask) == apll_mask) ? true : false;
}
#endif

#ifdef CONFIG_COMMON_CLK
int omap2_clk_apll96_enable(struct clk_hw *hw)
#else
static int _apll96_enable(struct clk *clk)
#endif
{
	return omap2xxx_cm_apll96_enable();
}

#ifdef CONFIG_COMMON_CLK
int omap2_clk_apll54_enable(struct clk_hw *hw)
#else
static int _apll54_enable(struct clk *clk)
#endif
{
	return omap2xxx_cm_apll54_enable();
}

#ifdef CONFIG_COMMON_CLK
static void _apll96_allow_idle(struct clk_hw_omap *clk)
#else
static void _apll96_allow_idle(struct clk *clk)
#endif
{
	omap2xxx_cm_set_apll96_auto_low_power_stop();
}

#ifdef CONFIG_COMMON_CLK
static void _apll96_deny_idle(struct clk_hw_omap *clk)
#else
static void _apll96_deny_idle(struct clk *clk)
#endif
{
	omap2xxx_cm_set_apll96_disable_autoidle();
}

#ifdef CONFIG_COMMON_CLK
static void _apll54_allow_idle(struct clk_hw_omap *clk)
#else
static void _apll54_allow_idle(struct clk *clk)
#endif
{
	omap2xxx_cm_set_apll54_auto_low_power_stop();
}

#ifdef CONFIG_COMMON_CLK
static void _apll54_deny_idle(struct clk_hw_omap *clk)
#else
static void _apll54_deny_idle(struct clk *clk)
#endif
{
	omap2xxx_cm_set_apll54_disable_autoidle();
}

#ifdef CONFIG_COMMON_CLK
void omap2_clk_apll96_disable(struct clk_hw *hw)
#else
static void _apll96_disable(struct clk *clk)
#endif
{
	omap2xxx_cm_apll96_disable();
}

#ifdef CONFIG_COMMON_CLK
void omap2_clk_apll54_disable(struct clk_hw *hw)
#else
static void _apll54_disable(struct clk *clk)
#endif
{
	omap2xxx_cm_apll54_disable();
}

#ifdef CONFIG_COMMON_CLK
unsigned long omap2_clk_apll54_recalc(struct clk_hw *hw,
				      unsigned long parent_rate)
{
	return (omap2xxx_clk_apll_locked(hw)) ? 54000000 : 0;
}

unsigned long omap2_clk_apll96_recalc(struct clk_hw *hw,
				      unsigned long parent_rate)
{
	return (omap2xxx_clk_apll_locked(hw)) ? 96000000 : 0;
}
#endif

/* Public data */
#ifdef CONFIG_COMMON_CLK
const struct clk_hw_omap_ops clkhwops_apll54 = {
	.allow_idle	= _apll54_allow_idle,
	.deny_idle	= _apll54_deny_idle,
};

const struct clk_hw_omap_ops clkhwops_apll96 = {
	.allow_idle	= _apll96_allow_idle,
	.deny_idle	= _apll96_deny_idle,
};
#else
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
#endif

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

