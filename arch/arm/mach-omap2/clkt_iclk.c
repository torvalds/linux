/*
 * OMAP2/3 interface clock control
 *
 * Copyright (C) 2011 Nokia Corporation
 * Paul Walmsley
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
#include "cm2xxx_3xxx.h"
#include "cm-regbits-24xx.h"

/* Private functions */

/* XXX */
void omap2_clkt_iclk_allow_idle(struct clk *clk)
{
	u32 v, r;

	r = ((__force u32)clk->enable_reg ^ (CM_AUTOIDLE ^ CM_ICLKEN));

	v = __raw_readl((__force void __iomem *)r);
	v |= (1 << clk->enable_bit);
	__raw_writel(v, (__force void __iomem *)r);
}

/* XXX */
void omap2_clkt_iclk_deny_idle(struct clk *clk)
{
	u32 v, r;

	r = ((__force u32)clk->enable_reg ^ (CM_AUTOIDLE ^ CM_ICLKEN));

	v = __raw_readl((__force void __iomem *)r);
	v &= ~(1 << clk->enable_bit);
	__raw_writel(v, (__force void __iomem *)r);
}

/* Public data */

const struct clkops clkops_omap2_iclk_dflt_wait = {
	.enable		= omap2_dflt_clk_enable,
	.disable	= omap2_dflt_clk_disable,
	.find_companion	= omap2_clk_dflt_find_companion,
	.find_idlest	= omap2_clk_dflt_find_idlest,
	.allow_idle	= omap2_clkt_iclk_allow_idle,
	.deny_idle	= omap2_clkt_iclk_deny_idle,
};

const struct clkops clkops_omap2_iclk_dflt = {
	.enable		= omap2_dflt_clk_enable,
	.disable	= omap2_dflt_clk_disable,
	.allow_idle	= omap2_clkt_iclk_allow_idle,
	.deny_idle	= omap2_clkt_iclk_deny_idle,
};

const struct clkops clkops_omap2_iclk_idle_only = {
	.allow_idle	= omap2_clkt_iclk_allow_idle,
	.deny_idle	= omap2_clkt_iclk_deny_idle,
};

const struct clkops clkops_omap2_mdmclk_dflt_wait = {
	.enable		= omap2_dflt_clk_enable,
	.disable	= omap2_dflt_clk_disable,
	.find_companion	= omap2_clk_dflt_find_companion,
	.find_idlest	= omap2_clk_dflt_find_idlest,
	.allow_idle	= omap2_clkt_iclk_allow_idle,
	.deny_idle	= omap2_clkt_iclk_deny_idle,
};

