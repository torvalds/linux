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

struct clkdm_ops omap4_clkdm_operations = {
	.clkdm_sleep		= omap4_clkdm_sleep,
	.clkdm_wakeup		= omap4_clkdm_wakeup,
};
