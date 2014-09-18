/*
 * OMAP3517/3505-specific clock framework functions
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Copyright (C) 2011 Nokia Corporation
 *
 * Ranjith Lohithakshan
 * Paul Walmsley
 *
 * Parts of this code are based on code written by
 * Richard Woodruff, Tony Lindgren, Tuukka Tikkanen, Karthik Dasu,
 * Russell King
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
#include "clock3517.h"
#include "cm3xxx.h"
#include "cm-regbits-34xx.h"

/*
 * In AM35xx IPSS, the {ICK,FCK} enable bits for modules are exported
 * in the same register at a bit offset of 0x8. The EN_ACK for ICK is
 * at an offset of 4 from ICK enable bit.
 */
#define AM35XX_IPSS_ICK_MASK			0xF
#define AM35XX_IPSS_ICK_EN_ACK_OFFSET 		0x4
#define AM35XX_IPSS_ICK_FCK_OFFSET		0x8
#define AM35XX_IPSS_CLK_IDLEST_VAL		0

/**
 * am35xx_clk_find_idlest - return clock ACK info for AM35XX IPSS
 * @clk: struct clk * being enabled
 * @idlest_reg: void __iomem ** to store CM_IDLEST reg address into
 * @idlest_bit: pointer to a u8 to store the CM_IDLEST bit shift into
 * @idlest_val: pointer to a u8 to store the CM_IDLEST indicator
 *
 * The interface clocks on AM35xx IPSS reflects the clock idle status
 * in the enable register itsel at a bit offset of 4 from the enable
 * bit. A value of 1 indicates that clock is enabled.
 */
static void am35xx_clk_find_idlest(struct clk_hw_omap *clk,
					    void __iomem **idlest_reg,
					    u8 *idlest_bit,
					    u8 *idlest_val)
{
	*idlest_reg = (__force void __iomem *)(clk->enable_reg);
	*idlest_bit = clk->enable_bit + AM35XX_IPSS_ICK_EN_ACK_OFFSET;
	*idlest_val = AM35XX_IPSS_CLK_IDLEST_VAL;
}

/**
 * am35xx_clk_find_companion - find companion clock to @clk
 * @clk: struct clk * to find the companion clock of
 * @other_reg: void __iomem ** to return the companion clock CM_*CLKEN va in
 * @other_bit: u8 ** to return the companion clock bit shift in
 *
 * Some clocks don't have companion clocks.  For example, modules with
 * only an interface clock (such as HECC) don't have a companion
 * clock.  Right now, this code relies on the hardware exporting a bit
 * in the correct companion register that indicates that the
 * nonexistent 'companion clock' is active.  Future patches will
 * associate this type of code with per-module data structures to
 * avoid this issue, and remove the casts.  No return value.
 */
static void am35xx_clk_find_companion(struct clk_hw_omap *clk,
				      void __iomem **other_reg,
				      u8 *other_bit)
{
	*other_reg = (__force void __iomem *)(clk->enable_reg);
	if (clk->enable_bit & AM35XX_IPSS_ICK_MASK)
		*other_bit = clk->enable_bit + AM35XX_IPSS_ICK_FCK_OFFSET;
	else
		*other_bit = clk->enable_bit - AM35XX_IPSS_ICK_FCK_OFFSET;
}
const struct clk_hw_omap_ops clkhwops_am35xx_ipss_module_wait = {
	.find_idlest	= am35xx_clk_find_idlest,
	.find_companion	= am35xx_clk_find_companion,
};

/**
 * am35xx_clk_ipss_find_idlest - return CM_IDLEST info for IPSS
 * @clk: struct clk * being enabled
 * @idlest_reg: void __iomem ** to store CM_IDLEST reg address into
 * @idlest_bit: pointer to a u8 to store the CM_IDLEST bit shift into
 * @idlest_val: pointer to a u8 to store the CM_IDLEST indicator
 *
 * The IPSS target CM_IDLEST bit is at a different shift from the
 * CM_{I,F}CLKEN bit.  Pass back the correct info via @idlest_reg
 * and @idlest_bit.  No return value.
 */
static void am35xx_clk_ipss_find_idlest(struct clk_hw_omap *clk,
					    void __iomem **idlest_reg,
					    u8 *idlest_bit,
					    u8 *idlest_val)
{
	u32 r;

	r = (((__force u32)clk->enable_reg & ~0xf0) | 0x20);
	*idlest_reg = (__force void __iomem *)r;
	*idlest_bit = AM35XX_ST_IPSS_SHIFT;
	*idlest_val = OMAP34XX_CM_IDLEST_VAL;
}

const struct clk_hw_omap_ops clkhwops_am35xx_ipss_wait = {
	.allow_idle	= omap2_clkt_iclk_allow_idle,
	.deny_idle	= omap2_clkt_iclk_deny_idle,
	.find_idlest	= am35xx_clk_ipss_find_idlest,
	.find_companion	= omap2_clk_dflt_find_companion,
};
