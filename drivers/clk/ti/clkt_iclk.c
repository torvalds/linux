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
#include <linux/clk/ti.h>

#include "clock.h"

/* Register offsets */
#define OMAP24XX_CM_FCLKEN2		0x04
#define CM_AUTOIDLE			0x30
#define CM_ICLKEN			0x10
#define CM_IDLEST			0x20

#define OMAP24XX_CM_IDLEST_VAL		0

/* Private functions */

/* XXX */
void omap2_clkt_iclk_allow_idle(struct clk_hw_omap *clk)
{
	u32 v;
	void __iomem *r;

	r = (__force void __iomem *)
		((__force u32)clk->enable_reg ^ (CM_AUTOIDLE ^ CM_ICLKEN));

	v = ti_clk_ll_ops->clk_readl(r);
	v |= (1 << clk->enable_bit);
	ti_clk_ll_ops->clk_writel(v, r);
}

/* XXX */
void omap2_clkt_iclk_deny_idle(struct clk_hw_omap *clk)
{
	u32 v;
	void __iomem *r;

	r = (__force void __iomem *)
		((__force u32)clk->enable_reg ^ (CM_AUTOIDLE ^ CM_ICLKEN));

	v = ti_clk_ll_ops->clk_readl(r);
	v &= ~(1 << clk->enable_bit);
	ti_clk_ll_ops->clk_writel(v, r);
}

/**
 * omap2430_clk_i2chs_find_idlest - return CM_IDLEST info for 2430 I2CHS
 * @clk: struct clk * being enabled
 * @idlest_reg: void __iomem ** to store CM_IDLEST reg address into
 * @idlest_bit: pointer to a u8 to store the CM_IDLEST bit shift into
 * @idlest_val: pointer to a u8 to store the CM_IDLEST indicator
 *
 * OMAP2430 I2CHS CM_IDLEST bits are in CM_IDLEST1_CORE, but the
 * CM_*CLKEN bits are in CM_{I,F}CLKEN2_CORE.  This custom function
 * passes back the correct CM_IDLEST register address for I2CHS
 * modules.  No return value.
 */
static void omap2430_clk_i2chs_find_idlest(struct clk_hw_omap *clk,
					   void __iomem **idlest_reg,
					   u8 *idlest_bit,
					   u8 *idlest_val)
{
	u32 r;

	r = ((__force u32)clk->enable_reg ^ (OMAP24XX_CM_FCLKEN2 ^ CM_IDLEST));
	*idlest_reg = (__force void __iomem *)r;
	*idlest_bit = clk->enable_bit;
	*idlest_val = OMAP24XX_CM_IDLEST_VAL;
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

/* 2430 I2CHS has non-standard IDLEST register */
const struct clk_hw_omap_ops clkhwops_omap2430_i2chs_wait = {
	.find_idlest	= omap2430_clk_i2chs_find_idlest,
	.find_companion	= omap2_clk_dflt_find_companion,
};
