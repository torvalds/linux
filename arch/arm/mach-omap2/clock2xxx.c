/*
 * clock2xxx.c - OMAP2xxx-specific clock integration code
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

#include "soc.h"
#include "clock.h"
#include "clock2xxx.h"
#include "cm.h"
#include "cm-regbits-24xx.h"

struct clk_hw *dclk_hw;
/*
 * Omap24xx specific clock functions
 */

/*
 * Switch the MPU rate if specified on cmdline.  We cannot do this
 * early until cmdline is parsed.  XXX This should be removed from the
 * clock code and handled by the OPP layer code in the near future.
 */
static int __init omap2xxx_clk_arch_init(void)
{
	int ret;

	if (!cpu_is_omap24xx())
		return 0;

	ret = omap2_clk_switch_mpurate_at_boot("virt_prcm_set");
	if (!ret)
		omap2_clk_print_new_rates("sys_ck", "dpll_ck", "mpu_ck");

	return ret;
}

arch_initcall(omap2xxx_clk_arch_init);


