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
#include "prm2xxx_3xxx.h"
#include "prm44xx.h"
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

static int omap4_pwrdm_set_lowpwrstchange(struct powerdomain *pwrdm)
{
	prm_rmw_mod_reg_bits(OMAP4430_LOWPOWERSTATECHANGE_MASK,
				(1 << OMAP4430_LOWPOWERSTATECHANGE_SHIFT),
				pwrdm->prcm_offs, OMAP4_PM_PWSTCTRL);
	return 0;
}

static int omap4_pwrdm_clear_all_prev_pwrst(struct powerdomain *pwrdm)
{
	prm_rmw_mod_reg_bits(OMAP4430_LASTPOWERSTATEENTERED_MASK,
				OMAP4430_LASTPOWERSTATEENTERED_MASK,
				pwrdm->prcm_offs, OMAP4_PM_PWSTST);
	return 0;
}

static int omap4_pwrdm_set_logic_retst(struct powerdomain *pwrdm, u8 pwrst)
{
	u32 v;

	v = pwrst << __ffs(OMAP4430_LOGICRETSTATE_MASK);
	prm_rmw_mod_reg_bits(OMAP4430_LOGICRETSTATE_MASK, v,
				pwrdm->prcm_offs, OMAP4_PM_PWSTCTRL);

	return 0;
}

static int omap4_pwrdm_set_mem_onst(struct powerdomain *pwrdm, u8 bank,
								u8 pwrst)
{
	u32 m;

	m = omap2_pwrdm_get_mem_bank_onstate_mask(bank);

	prm_rmw_mod_reg_bits(m, (pwrst << __ffs(m)), pwrdm->prcm_offs,
				OMAP4_PM_PWSTCTRL);

	return 0;
}

static int omap4_pwrdm_set_mem_retst(struct powerdomain *pwrdm, u8 bank,
								u8 pwrst)
{
	u32 m;

	m = omap2_pwrdm_get_mem_bank_retst_mask(bank);

	prm_rmw_mod_reg_bits(m, (pwrst << __ffs(m)), pwrdm->prcm_offs,
				OMAP4_PM_PWSTCTRL);

	return 0;
}

static int omap4_pwrdm_read_logic_pwrst(struct powerdomain *pwrdm)
{
	return prm_read_mod_bits_shift(pwrdm->prcm_offs, OMAP4_PM_PWSTST,
				OMAP4430_LOGICSTATEST_MASK);
}

static int omap4_pwrdm_read_logic_retst(struct powerdomain *pwrdm)
{
	return prm_read_mod_bits_shift(pwrdm->prcm_offs, OMAP4_PM_PWSTCTRL,
				OMAP4430_LOGICRETSTATE_MASK);
}

static int omap4_pwrdm_read_mem_pwrst(struct powerdomain *pwrdm, u8 bank)
{
	u32 m;

	m = omap2_pwrdm_get_mem_bank_stst_mask(bank);

	return prm_read_mod_bits_shift(pwrdm->prcm_offs, OMAP4_PM_PWSTST, m);
}

static int omap4_pwrdm_read_mem_retst(struct powerdomain *pwrdm, u8 bank)
{
	u32 m;

	m = omap2_pwrdm_get_mem_bank_retst_mask(bank);

	return prm_read_mod_bits_shift(pwrdm->prcm_offs, OMAP4_PM_PWSTCTRL, m);
}

static int omap4_pwrdm_wait_transition(struct powerdomain *pwrdm)
{
	u32 c = 0;

	/*
	 * REVISIT: pwrdm_wait_transition() may be better implemented
	 * via a callback and a periodic timer check -- how long do we expect
	 * powerdomain transitions to take?
	 */

	/* XXX Is this udelay() value meaningful? */
	while ((prm_read_mod_reg(pwrdm->prcm_offs, OMAP4_PM_PWSTST) &
		OMAP_INTRANSITION_MASK) &&
		(c++ < PWRDM_TRANSITION_BAILOUT))
			udelay(1);

	if (c > PWRDM_TRANSITION_BAILOUT) {
		printk(KERN_ERR "powerdomain: waited too long for "
			"powerdomain %s to complete transition\n", pwrdm->name);
		return -EAGAIN;
	}

	pr_debug("powerdomain: completed transition in %d loops\n", c);

	return 0;
}

struct pwrdm_ops omap4_pwrdm_operations = {
	.pwrdm_set_next_pwrst	= omap4_pwrdm_set_next_pwrst,
	.pwrdm_read_next_pwrst	= omap4_pwrdm_read_next_pwrst,
	.pwrdm_read_pwrst	= omap4_pwrdm_read_pwrst,
	.pwrdm_read_prev_pwrst	= omap4_pwrdm_read_prev_pwrst,
	.pwrdm_set_lowpwrstchange	= omap4_pwrdm_set_lowpwrstchange,
	.pwrdm_clear_all_prev_pwrst	= omap4_pwrdm_clear_all_prev_pwrst,
	.pwrdm_set_logic_retst	= omap4_pwrdm_set_logic_retst,
	.pwrdm_read_logic_pwrst	= omap4_pwrdm_read_logic_pwrst,
	.pwrdm_read_logic_retst	= omap4_pwrdm_read_logic_retst,
	.pwrdm_read_mem_pwrst	= omap4_pwrdm_read_mem_pwrst,
	.pwrdm_read_mem_retst	= omap4_pwrdm_read_mem_retst,
	.pwrdm_set_mem_onst	= omap4_pwrdm_set_mem_onst,
	.pwrdm_set_mem_retst	= omap4_pwrdm_set_mem_retst,
	.pwrdm_wait_transition	= omap4_pwrdm_wait_transition,
};
