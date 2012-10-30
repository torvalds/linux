/*
 * linux/arch/arm/mach-omap2/prcm.c
 *
 * OMAP 24xx Power Reset and Clock Management (PRCM) functions
 *
 * Copyright (C) 2005 Nokia Corporation
 *
 * Written by Tony Lindgren <tony.lindgren@nokia.com>
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
 * Rajendra Nayak <rnayak@ti.com>
 *
 * Some pieces of code Copyright (C) 2005 Texas Instruments, Inc.
 * Upgraded with OMAP4 support by Abhijit Pagare <abhijitpagare@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/export.h>

#include "common.h"
#include <plat/prcm.h>

#include "soc.h"
#include "clock.h"
#include "clock2xxx.h"
#include "cm2xxx_3xxx.h"
#include "prm2xxx_3xxx.h"
#include "prm44xx.h"
#include "prminst44xx.h"
#include "cminst44xx.h"
#include "prm-regbits-24xx.h"
#include "prm-regbits-44xx.h"
#include "control.h"

void __iomem *prm_base;
void __iomem *cm_base;
void __iomem *cm2_base;
void __iomem *prcm_mpu_base;

#define MAX_MODULE_ENABLE_WAIT		100000

/**
 * omap2_cm_wait_idlest - wait for IDLEST bit to indicate module readiness
 * @reg: physical address of module IDLEST register
 * @mask: value to mask against to determine if the module is active
 * @idlest: idle state indicator (0 or 1) for the clock
 * @name: name of the clock (for printk)
 *
 * Returns 1 if the module indicated readiness in time, or 0 if it
 * failed to enable in roughly MAX_MODULE_ENABLE_WAIT microseconds.
 *
 * XXX This function is deprecated.  It should be removed once the
 * hwmod conversion is complete.
 */
int omap2_cm_wait_idlest(void __iomem *reg, u32 mask, u8 idlest,
				const char *name)
{
	int i = 0;
	int ena = 0;

	if (idlest)
		ena = 0;
	else
		ena = mask;

	/* Wait for lock */
	omap_test_timeout(((__raw_readl(reg) & mask) == ena),
			  MAX_MODULE_ENABLE_WAIT, i);

	if (i < MAX_MODULE_ENABLE_WAIT)
		pr_debug("cm: Module associated with clock %s ready after %d loops\n",
			 name, i);
	else
		pr_err("cm: Module associated with clock %s didn't enable in %d tries\n",
		       name, MAX_MODULE_ENABLE_WAIT);

	return (i < MAX_MODULE_ENABLE_WAIT) ? 1 : 0;
};

void __init omap2_set_globals_prcm(void __iomem *prm, void __iomem *cm,
				   void __iomem *cm2, void __iomem *prcm_mpu)
{
	prm_base = prm;
	cm_base = cm;
	cm2_base = cm2;
	prcm_mpu_base = prcm_mpu;

	if (cpu_is_omap44xx() || soc_is_omap54xx()) {
		omap_prm_base_init();
		omap_cm_base_init();
	}
}

/*
 * Stubbed functions so that common files continue to build when
 * custom builds are used
 * XXX These are temporary and should be removed at the earliest possible
 * opportunity
 */
int __weak omap4_cminst_wait_module_idle(u8 part, u16 inst, s16 cdoffs,
					u16 clkctrl_offs)
{
	return 0;
}

void __weak omap4_cminst_module_enable(u8 mode, u8 part, u16 inst,
				s16 cdoffs, u16 clkctrl_offs)
{
}

void __weak omap4_cminst_module_disable(u8 part, u16 inst, s16 cdoffs,
				 u16 clkctrl_offs)
{
}
