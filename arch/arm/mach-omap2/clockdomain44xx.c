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

#include <linux/kernel.h>
#include "clockdomain.h"
#include "cminst44xx.h"
#include "cm44xx.h"

static int omap4_clkdm_add_wkup_sleep_dep(struct clockdomain *clkdm1,
					struct clockdomain *clkdm2)
{
	omap4_cminst_set_inst_reg_bits((1 << clkdm2->dep_bit),
					clkdm1->prcm_partition,
					clkdm1->cm_inst, clkdm1->clkdm_offs +
					OMAP4_CM_STATICDEP);
	return 0;
}

static int omap4_clkdm_del_wkup_sleep_dep(struct clockdomain *clkdm1,
					struct clockdomain *clkdm2)
{
	omap4_cminst_clear_inst_reg_bits((1 << clkdm2->dep_bit),
					clkdm1->prcm_partition,
					clkdm1->cm_inst, clkdm1->clkdm_offs +
					OMAP4_CM_STATICDEP);
	return 0;
}

static int omap4_clkdm_read_wkup_sleep_dep(struct clockdomain *clkdm1,
					struct clockdomain *clkdm2)
{
	return omap4_cminst_read_inst_reg_bits(clkdm1->prcm_partition,
					clkdm1->cm_inst, clkdm1->clkdm_offs +
					OMAP4_CM_STATICDEP,
					(1 << clkdm2->dep_bit));
}

static int omap4_clkdm_clear_all_wkup_sleep_deps(struct clockdomain *clkdm)
{
	struct clkdm_dep *cd;
	u32 mask = 0;

	if (!clkdm->prcm_partition)
		return 0;

	for (cd = clkdm->wkdep_srcs; cd && cd->clkdm_name; cd++) {
		if (!cd->clkdm)
			continue; /* only happens if data is erroneous */

		mask |= 1 << cd->clkdm->dep_bit;
		atomic_set(&cd->wkdep_usecount, 0);
	}

	omap4_cminst_clear_inst_reg_bits(mask, clkdm->prcm_partition,
					clkdm->cm_inst, clkdm->clkdm_offs +
					OMAP4_CM_STATICDEP);
	return 0;
}

static int omap4_clkdm_sleep(struct clockdomain *clkdm)
{
	omap4_cminst_clkdm_enable_hwsup(clkdm->prcm_partition,
					clkdm->cm_inst, clkdm->clkdm_offs);
	return 0;
}

static int omap4_clkdm_wakeup(struct clockdomain *clkdm)
{
	omap4_cminst_clkdm_force_wakeup(clkdm->prcm_partition,
					clkdm->cm_inst, clkdm->clkdm_offs);
	return 0;
}

static void omap4_clkdm_allow_idle(struct clockdomain *clkdm)
{
	omap4_cminst_clkdm_enable_hwsup(clkdm->prcm_partition,
					clkdm->cm_inst, clkdm->clkdm_offs);
}

static void omap4_clkdm_deny_idle(struct clockdomain *clkdm)
{
	if (clkdm->flags & CLKDM_CAN_FORCE_WAKEUP)
		omap4_clkdm_wakeup(clkdm);
	else
		omap4_cminst_clkdm_disable_hwsup(clkdm->prcm_partition,
						 clkdm->cm_inst,
						 clkdm->clkdm_offs);
}

static int omap4_clkdm_clk_enable(struct clockdomain *clkdm)
{
	if (clkdm->flags & CLKDM_CAN_FORCE_WAKEUP)
		return omap4_clkdm_wakeup(clkdm);

	return 0;
}

static int omap4_clkdm_clk_disable(struct clockdomain *clkdm)
{
	bool hwsup = false;

	if (!clkdm->prcm_partition)
		return 0;

	hwsup = omap4_cminst_is_clkdm_in_hwsup(clkdm->prcm_partition,
					clkdm->cm_inst, clkdm->clkdm_offs);

	if (!hwsup && (clkdm->flags & CLKDM_CAN_FORCE_SLEEP))
		omap4_clkdm_sleep(clkdm);

	return 0;
}

struct clkdm_ops omap4_clkdm_operations = {
	.clkdm_add_wkdep	= omap4_clkdm_add_wkup_sleep_dep,
	.clkdm_del_wkdep	= omap4_clkdm_del_wkup_sleep_dep,
	.clkdm_read_wkdep	= omap4_clkdm_read_wkup_sleep_dep,
	.clkdm_clear_all_wkdeps	= omap4_clkdm_clear_all_wkup_sleep_deps,
	.clkdm_add_sleepdep	= omap4_clkdm_add_wkup_sleep_dep,
	.clkdm_del_sleepdep	= omap4_clkdm_del_wkup_sleep_dep,
	.clkdm_read_sleepdep	= omap4_clkdm_read_wkup_sleep_dep,
	.clkdm_clear_all_sleepdeps	= omap4_clkdm_clear_all_wkup_sleep_deps,
	.clkdm_sleep		= omap4_clkdm_sleep,
	.clkdm_wakeup		= omap4_clkdm_wakeup,
	.clkdm_allow_idle	= omap4_clkdm_allow_idle,
	.clkdm_deny_idle	= omap4_clkdm_deny_idle,
	.clkdm_clk_enable	= omap4_clkdm_clk_enable,
	.clkdm_clk_disable	= omap4_clkdm_clk_disable,
};
