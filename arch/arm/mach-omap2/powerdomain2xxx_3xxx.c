/*
 * OMAP2 and OMAP3 powerdomain control
 *
 * Copyright (C) 2009-2011 Texas Instruments, Inc.
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
#include <linux/bug.h>

#include <plat/prcm.h>

#include "powerdomain.h"
#include "prm.h"
#include "prm-regbits-24xx.h"
#include "prm-regbits-34xx.h"


/* Common functions across OMAP2 and OMAP3 */
static int omap2_pwrdm_set_next_pwrst(struct powerdomain *pwrdm, u8 pwrst)
{
	omap2_prm_rmw_mod_reg_bits(OMAP_POWERSTATE_MASK,
				(pwrst << OMAP_POWERSTATE_SHIFT),
				pwrdm->prcm_offs, OMAP2_PM_PWSTCTRL);
	return 0;
}

static int omap2_pwrdm_read_next_pwrst(struct powerdomain *pwrdm)
{
	return omap2_prm_read_mod_bits_shift(pwrdm->prcm_offs,
					     OMAP2_PM_PWSTCTRL,
					     OMAP_POWERSTATE_MASK);
}

static int omap2_pwrdm_read_pwrst(struct powerdomain *pwrdm)
{
	return omap2_prm_read_mod_bits_shift(pwrdm->prcm_offs,
					     OMAP2_PM_PWSTST,
					     OMAP_POWERSTATEST_MASK);
}

static int omap2_pwrdm_set_mem_onst(struct powerdomain *pwrdm, u8 bank,
								u8 pwrst)
{
	u32 m;

	m = omap2_pwrdm_get_mem_bank_onstate_mask(bank);

	omap2_prm_rmw_mod_reg_bits(m, (pwrst << __ffs(m)), pwrdm->prcm_offs,
				   OMAP2_PM_PWSTCTRL);

	return 0;
}

static int omap2_pwrdm_set_mem_retst(struct powerdomain *pwrdm, u8 bank,
								u8 pwrst)
{
	u32 m;

	m = omap2_pwrdm_get_mem_bank_retst_mask(bank);

	omap2_prm_rmw_mod_reg_bits(m, (pwrst << __ffs(m)), pwrdm->prcm_offs,
				   OMAP2_PM_PWSTCTRL);

	return 0;
}

static int omap2_pwrdm_read_mem_pwrst(struct powerdomain *pwrdm, u8 bank)
{
	u32 m;

	m = omap2_pwrdm_get_mem_bank_stst_mask(bank);

	return omap2_prm_read_mod_bits_shift(pwrdm->prcm_offs, OMAP2_PM_PWSTST,
					     m);
}

static int omap2_pwrdm_read_mem_retst(struct powerdomain *pwrdm, u8 bank)
{
	u32 m;

	m = omap2_pwrdm_get_mem_bank_retst_mask(bank);

	return omap2_prm_read_mod_bits_shift(pwrdm->prcm_offs,
					     OMAP2_PM_PWSTCTRL, m);
}

static int omap2_pwrdm_set_logic_retst(struct powerdomain *pwrdm, u8 pwrst)
{
	u32 v;

	v = pwrst << __ffs(OMAP3430_LOGICL1CACHERETSTATE_MASK);
	omap2_prm_rmw_mod_reg_bits(OMAP3430_LOGICL1CACHERETSTATE_MASK, v,
				   pwrdm->prcm_offs, OMAP2_PM_PWSTCTRL);

	return 0;
}

static int omap2_pwrdm_wait_transition(struct powerdomain *pwrdm)
{
	u32 c = 0;

	/*
	 * REVISIT: pwrdm_wait_transition() may be better implemented
	 * via a callback and a periodic timer check -- how long do we expect
	 * powerdomain transitions to take?
	 */

	/* XXX Is this udelay() value meaningful? */
	while ((omap2_prm_read_mod_reg(pwrdm->prcm_offs, OMAP2_PM_PWSTST) &
		OMAP_INTRANSITION_MASK) &&
		(c++ < PWRDM_TRANSITION_BAILOUT))
			udelay(1);

	if (c > PWRDM_TRANSITION_BAILOUT) {
		pr_err("powerdomain: %s: waited too long to complete transition\n",
		       pwrdm->name);
		return -EAGAIN;
	}

	pr_debug("powerdomain: completed transition in %d loops\n", c);

	return 0;
}

/* Applicable only for OMAP3. Not supported on OMAP2 */
static int omap3_pwrdm_read_prev_pwrst(struct powerdomain *pwrdm)
{
	return omap2_prm_read_mod_bits_shift(pwrdm->prcm_offs,
					     OMAP3430_PM_PREPWSTST,
					     OMAP3430_LASTPOWERSTATEENTERED_MASK);
}

static int omap3_pwrdm_read_logic_pwrst(struct powerdomain *pwrdm)
{
	return omap2_prm_read_mod_bits_shift(pwrdm->prcm_offs,
					     OMAP2_PM_PWSTST,
					     OMAP3430_LOGICSTATEST_MASK);
}

static int omap3_pwrdm_read_logic_retst(struct powerdomain *pwrdm)
{
	return omap2_prm_read_mod_bits_shift(pwrdm->prcm_offs,
					     OMAP2_PM_PWSTCTRL,
					     OMAP3430_LOGICSTATEST_MASK);
}

static int omap3_pwrdm_read_prev_logic_pwrst(struct powerdomain *pwrdm)
{
	return omap2_prm_read_mod_bits_shift(pwrdm->prcm_offs,
					     OMAP3430_PM_PREPWSTST,
					     OMAP3430_LASTLOGICSTATEENTERED_MASK);
}

static int omap3_get_mem_bank_lastmemst_mask(u8 bank)
{
	switch (bank) {
	case 0:
		return OMAP3430_LASTMEM1STATEENTERED_MASK;
	case 1:
		return OMAP3430_LASTMEM2STATEENTERED_MASK;
	case 2:
		return OMAP3430_LASTSHAREDL2CACHEFLATSTATEENTERED_MASK;
	case 3:
		return OMAP3430_LASTL2FLATMEMSTATEENTERED_MASK;
	default:
		WARN_ON(1); /* should never happen */
		return -EEXIST;
	}
	return 0;
}

static int omap3_pwrdm_read_prev_mem_pwrst(struct powerdomain *pwrdm, u8 bank)
{
	u32 m;

	m = omap3_get_mem_bank_lastmemst_mask(bank);

	return omap2_prm_read_mod_bits_shift(pwrdm->prcm_offs,
				OMAP3430_PM_PREPWSTST, m);
}

static int omap3_pwrdm_clear_all_prev_pwrst(struct powerdomain *pwrdm)
{
	omap2_prm_write_mod_reg(0, pwrdm->prcm_offs, OMAP3430_PM_PREPWSTST);
	return 0;
}

static int omap3_pwrdm_enable_hdwr_sar(struct powerdomain *pwrdm)
{
	return omap2_prm_rmw_mod_reg_bits(0,
					  1 << OMAP3430ES2_SAVEANDRESTORE_SHIFT,
					  pwrdm->prcm_offs, OMAP2_PM_PWSTCTRL);
}

static int omap3_pwrdm_disable_hdwr_sar(struct powerdomain *pwrdm)
{
	return omap2_prm_rmw_mod_reg_bits(1 << OMAP3430ES2_SAVEANDRESTORE_SHIFT,
					  0, pwrdm->prcm_offs,
					  OMAP2_PM_PWSTCTRL);
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

struct pwrdm_ops omap3_pwrdm_operations = {
	.pwrdm_set_next_pwrst	= omap2_pwrdm_set_next_pwrst,
	.pwrdm_read_next_pwrst	= omap2_pwrdm_read_next_pwrst,
	.pwrdm_read_pwrst	= omap2_pwrdm_read_pwrst,
	.pwrdm_read_prev_pwrst	= omap3_pwrdm_read_prev_pwrst,
	.pwrdm_set_logic_retst	= omap2_pwrdm_set_logic_retst,
	.pwrdm_read_logic_pwrst	= omap3_pwrdm_read_logic_pwrst,
	.pwrdm_read_logic_retst	= omap3_pwrdm_read_logic_retst,
	.pwrdm_read_prev_logic_pwrst	= omap3_pwrdm_read_prev_logic_pwrst,
	.pwrdm_set_mem_onst	= omap2_pwrdm_set_mem_onst,
	.pwrdm_set_mem_retst	= omap2_pwrdm_set_mem_retst,
	.pwrdm_read_mem_pwrst	= omap2_pwrdm_read_mem_pwrst,
	.pwrdm_read_mem_retst	= omap2_pwrdm_read_mem_retst,
	.pwrdm_read_prev_mem_pwrst	= omap3_pwrdm_read_prev_mem_pwrst,
	.pwrdm_clear_all_prev_pwrst	= omap3_pwrdm_clear_all_prev_pwrst,
	.pwrdm_enable_hdwr_sar	= omap3_pwrdm_enable_hdwr_sar,
	.pwrdm_disable_hdwr_sar	= omap3_pwrdm_disable_hdwr_sar,
	.pwrdm_wait_transition	= omap2_pwrdm_wait_transition,
};
