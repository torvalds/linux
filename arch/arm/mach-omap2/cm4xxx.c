/*
 * OMAP4 CM module functions
 *
 * Copyright (C) 2009 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>

#include <asm/atomic.h>

#include <plat/common.h>

#include "cm.h"
#include "cm-regbits-44xx.h"

/**
 * omap4_cm_wait_module_ready - wait for a module to be in 'func' state
 * @clkctrl_reg: CLKCTRL module address
 *
 * Wait for the module IDLEST to be functional. If the idle state is in any
 * the non functional state (trans, idle or disabled), module and thus the
 * sysconfig cannot be accessed and will probably lead to an "imprecise
 * external abort"
 *
 * Module idle state:
 *   0x0 func:     Module is fully functional, including OCP
 *   0x1 trans:    Module is performing transition: wakeup, or sleep, or sleep
 *                 abortion
 *   0x2 idle:     Module is in Idle mode (only OCP part). It is functional if
 *                 using separate functional clock
 *   0x3 disabled: Module is disabled and cannot be accessed
 *
 * TODO: Need to handle module accessible in idle state
 */
int omap4_cm_wait_module_ready(void __iomem *clkctrl_reg)
{
	int i = 0;

	if (!clkctrl_reg)
		return 0;

	omap_test_timeout(((__raw_readl(clkctrl_reg) &
			    OMAP4430_IDLEST_MASK) == 0),
			  MAX_MODULE_READY_TIME, i);

	return (i < MAX_MODULE_READY_TIME) ? 0 : -EBUSY;
}

