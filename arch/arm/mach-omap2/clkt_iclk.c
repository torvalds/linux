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
#include <linux/clk-provider.h>
#include <linux/io.h>

#include "clock.h"

/* Register offsets */
#define CM_AUTOIDLE			0x30
#define CM_ICLKEN			0x10

/* Private functions */

/* XXX */
void omap2_clkt_iclk_allow_idle(struct clk_hw_omap *clk)
{
	u32 v;
	void __iomem *r;

	r = (__force void __iomem *)
		((__force u32)clk->enable_reg ^ (CM_AUTOIDLE ^ CM_ICLKEN));

	v = omap2_clk_readl(clk, r);
	v |= (1 << clk->enable_bit);
	omap2_clk_writel(v, clk, r);
}

/* XXX */
void omap2_clkt_iclk_deny_idle(struct clk_hw_omap *clk)
{
	u32 v;
	void __iomem *r;

	r = (__force void __iomem *)
		((__force u32)clk->enable_reg ^ (CM_AUTOIDLE ^ CM_ICLKEN));

	v = omap2_clk_readl(clk, r);
	v &= ~(1 << clk->enable_bit);
	omap2_clk_writel(v, clk, r);
}

/* Public data */

const struct clk_hw_omap_ops clkhwops_iclk = {
	.allow_idle	= omap2_clkt_iclk_allow_idle,
	.deny_idle	= omap2_clkt_iclk_deny_idle,
};

const struct clk_hw_omap_ops clkhwops_iclk_wait = {
	.allow_idle	= omap2_clkt_iclk_allow_idle,
	.deny_idle	= omap2_clkt_iclk_deny_idle,
	.find_idlest	= omap2_clk_dflt_find_idlest,
	.find_companion	= omap2_clk_dflt_find_companion,
};



