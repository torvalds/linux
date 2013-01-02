/*
 * OMAP2xxx sys_clk-specific clock code
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
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/io.h>

#include "clock.h"
#include "clock2xxx.h"
#include "prm2xxx_3xxx.h"
#include "prm-regbits-24xx.h"

void __iomem *prcm_clksrc_ctrl;

u32 omap2xxx_get_sysclkdiv(void)
{
	u32 div;

	div = __raw_readl(prcm_clksrc_ctrl);
	div &= OMAP_SYSCLKDIV_MASK;
	div >>= OMAP_SYSCLKDIV_SHIFT;

	return div;
}

unsigned long omap2xxx_sys_clk_recalc(struct clk_hw *clk,
				      unsigned long parent_rate)
{
	return parent_rate / omap2xxx_get_sysclkdiv();
}
