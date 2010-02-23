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

#include <plat/clock.h>

#include "clock.h"
#include "clock2xxx.h"
#include "cm.h"
#include "cm-regbits-24xx.h"

struct clk *vclk, *sclk, *dclk;

/*
 * Omap24xx specific clock functions
 */

/*
 * Set clocks for bypass mode for reboot to work.
 */
void omap2xxx_clk_prepare_for_reboot(void)
{
	u32 rate;

	if (vclk == NULL || sclk == NULL)
		return;

	rate = clk_get_rate(sclk);
	clk_set_rate(vclk, rate);
}

/*
 * Switch the MPU rate if specified on cmdline.
 * We cannot do this early until cmdline is parsed.
 */
static int __init omap2xxx_clk_arch_init(void)
{
	struct clk *virt_prcm_set, *sys_ck, *dpll_ck, *mpu_ck;
	unsigned long sys_ck_rate;

	if (!cpu_is_omap24xx())
		return 0;

	if (!mpurate)
		return -EINVAL;

	virt_prcm_set = clk_get(NULL, "virt_prcm_set");
	sys_ck = clk_get(NULL, "sys_ck");
	dpll_ck = clk_get(NULL, "dpll_ck");
	mpu_ck = clk_get(NULL, "mpu_ck");

	if (clk_set_rate(virt_prcm_set, mpurate))
		pr_err("Could not find matching MPU rate\n");

	recalculate_root_clocks();

	sys_ck_rate = clk_get_rate(sys_ck);

	pr_info("Switched to new clocking rate (Crystal/DPLL/MPU): "
		"%ld.%01ld/%ld/%ld MHz\n",
		(sys_ck_rate / 1000000), (sys_ck_rate / 100000) % 10,
		(clk_get_rate(dpll_ck) / 1000000),
		(clk_get_rate(mpu_ck) / 1000000));

	return 0;
}
arch_initcall(omap2xxx_clk_arch_init);


