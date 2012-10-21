/*
 * OMAP2xxx PRM module functions
 *
 * Copyright (C) 2010-2012 Texas Instruments, Inc.
 * Copyright (C) 2010 Nokia Corporation
 * Benoît Cousson
 * Paul Walmsley
 * Rajendra Nayak <rnayak@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/irq.h>

#include "common.h"
#include <plat/cpu.h>
#include <plat/prcm.h>

#include "vp.h"
#include "powerdomain.h"
#include "clockdomain.h"
#include "prm2xxx.h"
#include "cm2xxx_3xxx.h"
#include "prm-regbits-24xx.h"

int omap2xxx_clkdm_sleep(struct clockdomain *clkdm)
{
	omap2_prm_set_mod_reg_bits(OMAP24XX_FORCESTATE_MASK,
				   clkdm->pwrdm.ptr->prcm_offs,
				   OMAP2_PM_PWSTCTRL);
	return 0;
}

int omap2xxx_clkdm_wakeup(struct clockdomain *clkdm)
{
	omap2_prm_clear_mod_reg_bits(OMAP24XX_FORCESTATE_MASK,
				     clkdm->pwrdm.ptr->prcm_offs,
				     OMAP2_PM_PWSTCTRL);
	return 0;
}

struct pwrdm_ops omap2_pwrdm_operations = {
	.pwrdm_set_next_pwrst	= omap2_pwrdm_set_next_pwrst,
	.pwrdm_read_next_pwrst	= omap2_pwrdm_read_next_pwrst,
	.pwrdm_read_pwrst	= omap2_pwrdm_read_pwrst,
	.pwrdm_set_logic_retst	= omap2_pwrdm_set_logic_retst,
	.pwrdm_set_mem_onst	= omap2_pwrdm_set_mem_onst,
	.pwrdm_set_mem_retst	= omap2_pwrdm_set_mem_retst,
	.pwrdm_read_mem_pwrst	= omap2_pwrdm_read_mem_pwrst,
	.pwrdm_read_mem_retst	= omap2_pwrdm_read_mem_retst,
	.pwrdm_wait_transition	= omap2_pwrdm_wait_transition,
};
