/*
 * AM33XX clockdomain control
 *
 * Copyright (C) 2011-2012 Texas Instruments Incorporated - http://www.ti.com/
 * Vaibhav Hiremath <hvaibhav@ti.com>
 *
 * Derived from mach-omap2/clockdomain44xx.c written by Rajendra Nayak
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>

#include "clockdomain.h"
#include "cm33xx.h"


static int am33xx_clkdm_sleep(struct clockdomain *clkdm)
{
	am33xx_cm_clkdm_force_sleep(clkdm->cm_inst, clkdm->clkdm_offs);
	return 0;
}

static int am33xx_clkdm_wakeup(struct clockdomain *clkdm)
{
	am33xx_cm_clkdm_force_wakeup(clkdm->cm_inst, clkdm->clkdm_offs);
	return 0;
}

static void am33xx_clkdm_allow_idle(struct clockdomain *clkdm)
{
	am33xx_cm_clkdm_enable_hwsup(clkdm->cm_inst, clkdm->clkdm_offs);
}

static void am33xx_clkdm_deny_idle(struct clockdomain *clkdm)
{
	am33xx_cm_clkdm_disable_hwsup(clkdm->cm_inst, clkdm->clkdm_offs);
}

static int am33xx_clkdm_clk_enable(struct clockdomain *clkdm)
{
	if (clkdm->flags & CLKDM_CAN_FORCE_WAKEUP)
		return am33xx_clkdm_wakeup(clkdm);

	return 0;
}

static int am33xx_clkdm_clk_disable(struct clockdomain *clkdm)
{
	bool hwsup = false;

	hwsup = am33xx_cm_is_clkdm_in_hwsup(clkdm->cm_inst, clkdm->clkdm_offs);

	if (!hwsup && (clkdm->flags & CLKDM_CAN_FORCE_SLEEP))
		am33xx_clkdm_sleep(clkdm);

	return 0;
}

struct clkdm_ops am33xx_clkdm_operations = {
	.clkdm_sleep		= am33xx_clkdm_sleep,
	.clkdm_wakeup		= am33xx_clkdm_wakeup,
	.clkdm_allow_idle	= am33xx_clkdm_allow_idle,
	.clkdm_deny_idle	= am33xx_clkdm_deny_idle,
	.clkdm_clk_enable	= am33xx_clkdm_clk_enable,
	.clkdm_clk_disable	= am33xx_clkdm_clk_disable,
};
