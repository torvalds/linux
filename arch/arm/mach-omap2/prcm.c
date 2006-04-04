/*
 * linux/arch/arm/mach-omap2/prcm.c
 *
 * OMAP 24xx Power Reset and Clock Management (PRCM) functions
 *
 * Copyright (C) 2005 Nokia Corporation
 *
 * Written by Tony Lindgren <tony.lindgren@nokia.com>
 *
 * Some pieces of code Copyright (C) 2005 Texas Instruments, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/clk.h>

#include "prcm-regs.h"

u32 omap_prcm_get_reset_sources(void)
{
	return RM_RSTST_WKUP & 0x7f;
}
EXPORT_SYMBOL(omap_prcm_get_reset_sources);

/* Resets clock rates and reboots the system. Only called from system.h */
void omap_prcm_arch_reset(char mode)
{
	u32 rate;
	struct clk *vclk, *sclk;

	vclk = clk_get(NULL, "virt_prcm_set");
	sclk = clk_get(NULL, "sys_ck");
	rate = clk_get_rate(sclk);
	clk_set_rate(vclk, rate);	/* go to bypass for OMAP limitation */
	RM_RSTCTRL_WKUP |= 2;
}
