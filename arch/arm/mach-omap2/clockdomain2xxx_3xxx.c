/*
 * OMAP2 and OMAP3 clockdomain control
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

#include <linux/types.h>
#include <plat/prcm.h>
#include "prm.h"
#include "prm2xxx_3xxx.h"
#include "cm.h"
#include "cm2xxx_3xxx.h"
#include "cm-regbits-24xx.h"
#include "cm-regbits-34xx.h"
#include "prm-regbits-24xx.h"
#include "clockdomain.h"

static int omap2_clkdm_add_wkdep(struct clockdomain *clkdm1,
						struct clockdomain *clkdm2)
{
	omap2_prm_set_mod_reg_bits((1 << clkdm2->dep_bit),
				clkdm1->pwrdm.ptr->prcm_offs, PM_WKDEP);
	return 0;
}

static int omap2_clkdm_del_wkdep(struct clockdomain *clkdm1,
						 struct clockdomain *clkdm2)
{
	omap2_prm_clear_mod_reg_bits((1 << clkdm2->dep_bit),
				clkdm1->pwrdm.ptr->prcm_offs, PM_WKDEP);
	return 0;
}

static int omap2_clkdm_read_wkdep(struct clockdomain *clkdm1,
						 struct clockdomain *clkdm2)
{
	return omap2_prm_read_mod_bits_shift(clkdm1->pwrdm.ptr->prcm_offs,
				PM_WKDEP, (1 << clkdm2->dep_bit));
}

static int omap2_clkdm_clear_all_wkdeps(struct clockdomain *clkdm)
{
	struct clkdm_dep *cd;
	u32 mask = 0;

	for (cd = clkdm->wkdep_srcs; cd && cd->clkdm_name; cd++) {
		if (!omap_chip_is(cd->omap_chip))
			continue;
		if (!cd->clkdm)
			continue; /* only happens if data is erroneous */

		/* PRM accesses are slow, so minimize them */
		mask |= 1 << cd->clkdm->dep_bit;
		atomic_set(&cd->wkdep_usecount, 0);
	}

	omap2_prm_clear_mod_reg_bits(mask, clkdm->pwrdm.ptr->prcm_offs,
				 PM_WKDEP);
	return 0;
}

static int omap3_clkdm_add_sleepdep(struct clockdomain *clkdm1,
						 struct clockdomain *clkdm2)
{
	omap2_cm_set_mod_reg_bits((1 << clkdm2->dep_bit),
				clkdm1->pwrdm.ptr->prcm_offs,
				OMAP3430_CM_SLEEPDEP);
	return 0;
}

static int omap3_clkdm_del_sleepdep(struct clockdomain *clkdm1,
						 struct clockdomain *clkdm2)
{
	omap2_cm_clear_mod_reg_bits((1 << clkdm2->dep_bit),
				clkdm1->pwrdm.ptr->prcm_offs,
				OMAP3430_CM_SLEEPDEP);
	return 0;
}

static int omap3_clkdm_read_sleepdep(struct clockdomain *clkdm1,
						 struct clockdomain *clkdm2)
{
	return omap2_prm_read_mod_bits_shift(clkdm1->pwrdm.ptr->prcm_offs,
				OMAP3430_CM_SLEEPDEP, (1 << clkdm2->dep_bit));
}

static int omap3_clkdm_clear_all_sleepdeps(struct clockdomain *clkdm)
{
	struct clkdm_dep *cd;
	u32 mask = 0;

	for (cd = clkdm->sleepdep_srcs; cd && cd->clkdm_name; cd++) {
		if (!omap_chip_is(cd->omap_chip))
			continue;
		if (!cd->clkdm)
			continue; /* only happens if data is erroneous */

		/* PRM accesses are slow, so minimize them */
		mask |= 1 << cd->clkdm->dep_bit;
		atomic_set(&cd->sleepdep_usecount, 0);
	}
	omap2_prm_clear_mod_reg_bits(mask, clkdm->pwrdm.ptr->prcm_offs,
				OMAP3430_CM_SLEEPDEP);
	return 0;
}

static int omap2_clkdm_sleep(struct clockdomain *clkdm)
{
	omap2_cm_set_mod_reg_bits(OMAP24XX_FORCESTATE_MASK,
				clkdm->pwrdm.ptr->prcm_offs,
				OMAP2_PM_PWSTCTRL);
	return 0;
}

static int omap2_clkdm_wakeup(struct clockdomain *clkdm)
{
	omap2_cm_clear_mod_reg_bits(OMAP24XX_FORCESTATE_MASK,
				clkdm->pwrdm.ptr->prcm_offs,
				OMAP2_PM_PWSTCTRL);
	return 0;
}

static void omap2_clkdm_allow_idle(struct clockdomain *clkdm)
{
	if (atomic_read(&clkdm->usecount) > 0)
		_clkdm_add_autodeps(clkdm);

	omap2xxx_cm_clkdm_enable_hwsup(clkdm->pwrdm.ptr->prcm_offs,
				clkdm->clktrctrl_mask);
}

static void omap2_clkdm_deny_idle(struct clockdomain *clkdm)
{
	omap2xxx_cm_clkdm_disable_hwsup(clkdm->pwrdm.ptr->prcm_offs,
				clkdm->clktrctrl_mask);

	if (atomic_read(&clkdm->usecount) > 0)
		_clkdm_del_autodeps(clkdm);
}

static void _enable_hwsup(struct clockdomain *clkdm)
{
	if (cpu_is_omap24xx())
		omap2xxx_cm_clkdm_enable_hwsup(clkdm->pwrdm.ptr->prcm_offs,
					       clkdm->clktrctrl_mask);
	else if (cpu_is_omap34xx())
		omap3xxx_cm_clkdm_enable_hwsup(clkdm->pwrdm.ptr->prcm_offs,
					       clkdm->clktrctrl_mask);
}

static void _disable_hwsup(struct clockdomain *clkdm)
{
	if (cpu_is_omap24xx())
		omap2xxx_cm_clkdm_disable_hwsup(clkdm->pwrdm.ptr->prcm_offs,
						clkdm->clktrctrl_mask);
	else if (cpu_is_omap34xx())
		omap3xxx_cm_clkdm_disable_hwsup(clkdm->pwrdm.ptr->prcm_offs,
						clkdm->clktrctrl_mask);
}


static int omap2_clkdm_clk_enable(struct clockdomain *clkdm)
{
	bool hwsup = false;

	if (!clkdm->clktrctrl_mask)
		return 0;

	hwsup = omap2_cm_is_clkdm_in_hwsup(clkdm->pwrdm.ptr->prcm_offs,
				clkdm->clktrctrl_mask);

	if (hwsup) {
		/* Disable HW transitions when we are changing deps */
		_disable_hwsup(clkdm);
		_clkdm_add_autodeps(clkdm);
		_enable_hwsup(clkdm);
	} else {
		if (clkdm->flags & CLKDM_CAN_FORCE_WAKEUP)
			omap2_clkdm_wakeup(clkdm);
	}

	return 0;
}

static int omap2_clkdm_clk_disable(struct clockdomain *clkdm)
{
	bool hwsup = false;

	if (!clkdm->clktrctrl_mask)
		return 0;

	hwsup = omap2_cm_is_clkdm_in_hwsup(clkdm->pwrdm.ptr->prcm_offs,
				clkdm->clktrctrl_mask);

	if (hwsup) {
		/* Disable HW transitions when we are changing deps */
		_disable_hwsup(clkdm);
		_clkdm_del_autodeps(clkdm);
		_enable_hwsup(clkdm);
	} else {
		if (clkdm->flags & CLKDM_CAN_FORCE_SLEEP)
			omap2_clkdm_sleep(clkdm);
	}

	return 0;
}

static int omap3_clkdm_sleep(struct clockdomain *clkdm)
{
	omap3xxx_cm_clkdm_force_sleep(clkdm->pwrdm.ptr->prcm_offs,
				clkdm->clktrctrl_mask);
	return 0;
}

static int omap3_clkdm_wakeup(struct clockdomain *clkdm)
{
	omap3xxx_cm_clkdm_force_wakeup(clkdm->pwrdm.ptr->prcm_offs,
				clkdm->clktrctrl_mask);
	return 0;
}

static void omap3_clkdm_allow_idle(struct clockdomain *clkdm)
{
	if (atomic_read(&clkdm->usecount) > 0)
		_clkdm_add_autodeps(clkdm);

	omap3xxx_cm_clkdm_enable_hwsup(clkdm->pwrdm.ptr->prcm_offs,
				clkdm->clktrctrl_mask);
}

static void omap3_clkdm_deny_idle(struct clockdomain *clkdm)
{
	omap3xxx_cm_clkdm_disable_hwsup(clkdm->pwrdm.ptr->prcm_offs,
				clkdm->clktrctrl_mask);

	if (atomic_read(&clkdm->usecount) > 0)
		_clkdm_del_autodeps(clkdm);
}

struct clkdm_ops omap2_clkdm_operations = {
	.clkdm_add_wkdep	= omap2_clkdm_add_wkdep,
	.clkdm_del_wkdep	= omap2_clkdm_del_wkdep,
	.clkdm_read_wkdep	= omap2_clkdm_read_wkdep,
	.clkdm_clear_all_wkdeps	= omap2_clkdm_clear_all_wkdeps,
	.clkdm_sleep		= omap2_clkdm_sleep,
	.clkdm_wakeup		= omap2_clkdm_wakeup,
	.clkdm_allow_idle	= omap2_clkdm_allow_idle,
	.clkdm_deny_idle	= omap2_clkdm_deny_idle,
	.clkdm_clk_enable	= omap2_clkdm_clk_enable,
	.clkdm_clk_disable	= omap2_clkdm_clk_disable,
};

struct clkdm_ops omap3_clkdm_operations = {
	.clkdm_add_wkdep	= omap2_clkdm_add_wkdep,
	.clkdm_del_wkdep	= omap2_clkdm_del_wkdep,
	.clkdm_read_wkdep	= omap2_clkdm_read_wkdep,
	.clkdm_clear_all_wkdeps	= omap2_clkdm_clear_all_wkdeps,
	.clkdm_add_sleepdep	= omap3_clkdm_add_sleepdep,
	.clkdm_del_sleepdep	= omap3_clkdm_del_sleepdep,
	.clkdm_read_sleepdep	= omap3_clkdm_read_sleepdep,
	.clkdm_clear_all_sleepdeps	= omap3_clkdm_clear_all_sleepdeps,
	.clkdm_sleep		= omap3_clkdm_sleep,
	.clkdm_wakeup		= omap3_clkdm_wakeup,
	.clkdm_allow_idle	= omap3_clkdm_allow_idle,
	.clkdm_deny_idle	= omap3_clkdm_deny_idle,
	.clkdm_clk_enable	= omap2_clkdm_clk_enable,
	.clkdm_clk_disable	= omap2_clkdm_clk_disable,
};
