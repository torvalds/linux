/*
 * OMAP4 powerdomain control
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
#include <plat/powerdomain.h>
#include <plat/prcm.h>
#include "prm.h"
#include "prm-regbits-44xx.h"
#include "powerdomains.h"

static int omap4_pwrdm_set_next_pwrst(struct powerdomain *pwrdm, u8 pwrst)
{
	prm_rmw_mod_reg_bits(OMAP_POWERSTATE_MASK,
				(pwrst << OMAP_POWERSTATE_SHIFT),
				pwrdm->prcm_offs, OMAP4_PM_PWSTCTRL);
	return 0;
}

static int omap4_pwrdm_read_next_pwrst(struct powerdomain *pwrdm)
{
	return prm_read_mod_bits_shift(pwrdm->prcm_offs,
				OMAP4_PM_PWSTCTRL, OMAP_POWERSTATE_MASK);
}

static int omap4_pwrdm_read_pwrst(struct powerdomain *pwrdm)
{
	return prm_read_mod_bits_shift(pwrdm->prcm_offs,
				OMAP4_PM_PWSTST, OMAP_POWERSTATEST_MASK);
}

static int omap4_pwrdm_read_prev_pwrst(struct powerdomain *pwrdm)
{
	return prm_read_mod_bits_shift(pwrdm->prcm_offs, OMAP4_PM_PWSTST,
				OMAP4430_LASTPOWERSTATEENTERED_MASK);
}

struct pwrdm_ops omap4_pwrdm_operations = {
	.pwrdm_set_next_pwrst	= omap4_pwrdm_set_next_pwrst,
	.pwrdm_read_next_pwrst	= omap4_pwrdm_read_next_pwrst,
	.pwrdm_read_pwrst	= omap4_pwrdm_read_pwrst,
	.pwrdm_read_prev_pwrst	= omap4_pwrdm_read_prev_pwrst,
};
