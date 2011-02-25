/*
 * OMAP4 clockdomain control
 *
 * Copyright (C) 2008-2010 Texas Instruments, Inc.
 * Copyright (C) 2008-2010 Nokia Corporation
 *
 * Derived from mach-omap2/clockdomain.c written by Paul Walmsley
 * Rajendra Nayak <rnayak@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "clockdomain.h"
#include "cminst44xx.h"

static int omap4_clkdm_sleep(struct clockdomain *clkdm)
{
	omap4_cminst_clkdm_force_sleep(clkdm->prcm_partition,
					clkdm->cm_inst, clkdm->clkdm_offs);
	return 0;
}

static int omap4_clkdm_wakeup(struct clockdomain *clkdm)
{
	omap4_cminst_clkdm_force_wakeup(clkdm->prcm_partition,
					clkdm->cm_inst, clkdm->clkdm_offs);
	return 0;
}

static void omap4_clkdm_allow_idle(struct clockdomain *clkdm)
{
	omap4_cminst_clkdm_enable_hwsup(clkdm->prcm_partition,
					clkdm->cm_inst, clkdm->clkdm_offs);
}

static void omap4_clkdm_deny_idle(struct clockdomain *clkdm)
{
	omap4_cminst_clkdm_disable_hwsup(clkdm->prcm_partition,
					clkdm->cm_inst, clkdm->clkdm_offs);
}

static int omap4_clkdm_clk_enable(struct clockdomain *clkdm)
{
	bool hwsup = false;

	hwsup = omap4_cminst_is_clkdm_in_hwsup(clkdm->prcm_partition,
					clkdm->cm_inst, clkdm->clkdm_offs);

	if (!hwsup)
		clkdm_wakeup(clkdm);

	return 0;
}

static int omap4_clkdm_clk_disable(struct clockdomain *clkdm)
{
	bool hwsup = false;

	hwsup = omap4_cminst_is_clkdm_in_hwsup(clkdm->prcm_partition,
					clkdm->cm_inst, clkdm->clkdm_offs);

	if (!hwsup)
		clkdm_sleep(clkdm);

	return 0;
}

struct clkdm_ops omap4_clkdm_operations = {
	.clkdm_sleep		= omap4_clkdm_sleep,
	.clkdm_wakeup		= omap4_clkdm_wakeup,
	.clkdm_allow_idle	= omap4_clkdm_allow_idle,
	.clkdm_deny_idle	= omap4_clkdm_deny_idle,
	.clkdm_clk_enable	= omap4_clkdm_clk_enable,
	.clkdm_clk_disable	= omap4_clkdm_clk_disable,
};
