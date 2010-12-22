/*
 * OMAP2 and OMAP3 powerdomain control
 *
 * Copyright (C) 2009-2010 Texas Instruments, Inc.
 * Copyright (C) 2007-2009 Nokia Corporation
 *
 * Derived from mach-omap2/powerdomain.c written by Paul Walmsley
 * Rajendra Nayak <rnayak@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <plat/prcm.h>
#include "prm.h"
#include "prm-regbits-34xx.h"
#include "powerdomains.h"

/* Common functions across OMAP2 and OMAP3 */
static int omap2_pwrdm_set_next_pwrst(struct powerdomain *pwrdm, u8 pwrst)
{
	prm_rmw_mod_reg_bits(OMAP_POWERSTATE_MASK,
				(pwrst << OMAP_POWERSTATE_SHIFT),
				pwrdm->prcm_offs, OMAP2_PM_PWSTCTRL);
	return 0;
}

static int omap2_pwrdm_read_next_pwrst(struct powerdomain *pwrdm)
{
	return prm_read_mod_bits_shift(pwrdm->prcm_offs,
				OMAP2_PM_PWSTCTRL, OMAP_POWERSTATE_MASK);
}

static int omap2_pwrdm_read_pwrst(struct powerdomain *pwrdm)
{
	return prm_read_mod_bits_shift(pwrdm->prcm_offs,
				OMAP2_PM_PWSTST, OMAP_POWERSTATEST_MASK);
}

/* Applicable only for OMAP3. Not supported on OMAP2 */
static int omap3_pwrdm_read_prev_pwrst(struct powerdomain *pwrdm)
{
	return prm_read_mod_bits_shift(pwrdm->prcm_offs, OMAP3430_PM_PREPWSTST,
				OMAP3430_LASTPOWERSTATEENTERED_MASK);
}

struct pwrdm_ops omap2_pwrdm_operations = {
	.pwrdm_set_next_pwrst	= omap2_pwrdm_set_next_pwrst,
	.pwrdm_read_next_pwrst	= omap2_pwrdm_read_next_pwrst,
	.pwrdm_read_pwrst	= omap2_pwrdm_read_pwrst,
};

struct pwrdm_ops omap3_pwrdm_operations = {
	.pwrdm_set_next_pwrst	= omap2_pwrdm_set_next_pwrst,
	.pwrdm_read_next_pwrst	= omap2_pwrdm_read_next_pwrst,
	.pwrdm_read_pwrst	= omap2_pwrdm_read_pwrst,
	.pwrdm_read_prev_pwrst	= omap3_pwrdm_read_prev_pwrst,
};
