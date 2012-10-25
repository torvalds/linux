/*
 * OMAP2xxx CM module functions
 *
 * Copyright (C) 2009 Nokia Corporation
 * Copyright (C) 2008-2010, 2012 Texas Instruments, Inc.
 * Paul Walmsley
 * Rajendra Nayak <rnayak@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>

#include "soc.h"
#include "iomap.h"
#include "common.h"
#include "prm2xxx.h"
#include "cm.h"
#include "cm2xxx.h"
#include "cm-regbits-24xx.h"
#include "clockdomain.h"

/* CM_AUTOIDLE_PLL.AUTO_* bit values for DPLLs */
#define DPLL_AUTOIDLE_DISABLE				0x0
#define OMAP2XXX_DPLL_AUTOIDLE_LOW_POWER_STOP		0x3

/* CM_AUTOIDLE_PLL.AUTO_* bit values for APLLs (OMAP2xxx only) */
#define OMAP2XXX_APLL_AUTOIDLE_DISABLE			0x0
#define OMAP2XXX_APLL_AUTOIDLE_LOW_POWER_STOP		0x3

static const u8 omap2xxx_cm_idlest_offs[] = {
	CM_IDLEST1, CM_IDLEST2, OMAP2430_CM_IDLEST3, OMAP24XX_CM_IDLEST4
};

/*
 *
 */

static void _write_clktrctrl(u8 c, s16 module, u32 mask)
{
	u32 v;

	v = omap2_cm_read_mod_reg(module, OMAP2_CM_CLKSTCTRL);
	v &= ~mask;
	v |= c << __ffs(mask);
	omap2_cm_write_mod_reg(v, module, OMAP2_CM_CLKSTCTRL);
}

bool omap2xxx_cm_is_clkdm_in_hwsup(s16 module, u32 mask)
{
	u32 v;

	v = omap2_cm_read_mod_reg(module, OMAP2_CM_CLKSTCTRL);
	v &= mask;
	v >>= __ffs(mask);

	return (v == OMAP24XX_CLKSTCTRL_ENABLE_AUTO) ? 1 : 0;
}

void omap2xxx_cm_clkdm_enable_hwsup(s16 module, u32 mask)
{
	_write_clktrctrl(OMAP24XX_CLKSTCTRL_ENABLE_AUTO, module, mask);
}

void omap2xxx_cm_clkdm_disable_hwsup(s16 module, u32 mask)
{
	_write_clktrctrl(OMAP24XX_CLKSTCTRL_DISABLE_AUTO, module, mask);
}

/*
 * DPLL autoidle control
 */

static void _omap2xxx_set_dpll_autoidle(u8 m)
{
	u32 v;

	v = omap2_cm_read_mod_reg(PLL_MOD, CM_AUTOIDLE);
	v &= ~OMAP24XX_AUTO_DPLL_MASK;
	v |= m << OMAP24XX_AUTO_DPLL_SHIFT;
	omap2_cm_write_mod_reg(v, PLL_MOD, CM_AUTOIDLE);
}

void omap2xxx_cm_set_dpll_disable_autoidle(void)
{
	_omap2xxx_set_dpll_autoidle(OMAP2XXX_DPLL_AUTOIDLE_LOW_POWER_STOP);
}

void omap2xxx_cm_set_dpll_auto_low_power_stop(void)
{
	_omap2xxx_set_dpll_autoidle(DPLL_AUTOIDLE_DISABLE);
}

/*
 * APLL autoidle control
 */

static void _omap2xxx_set_apll_autoidle(u8 m, u32 mask)
{
	u32 v;

	v = omap2_cm_read_mod_reg(PLL_MOD, CM_AUTOIDLE);
	v &= ~mask;
	v |= m << __ffs(mask);
	omap2_cm_write_mod_reg(v, PLL_MOD, CM_AUTOIDLE);
}

void omap2xxx_cm_set_apll54_disable_autoidle(void)
{
	_omap2xxx_set_apll_autoidle(OMAP2XXX_APLL_AUTOIDLE_LOW_POWER_STOP,
				    OMAP24XX_AUTO_54M_MASK);
}

void omap2xxx_cm_set_apll54_auto_low_power_stop(void)
{
	_omap2xxx_set_apll_autoidle(OMAP2XXX_APLL_AUTOIDLE_DISABLE,
				    OMAP24XX_AUTO_54M_MASK);
}

void omap2xxx_cm_set_apll96_disable_autoidle(void)
{
	_omap2xxx_set_apll_autoidle(OMAP2XXX_APLL_AUTOIDLE_LOW_POWER_STOP,
				    OMAP24XX_AUTO_96M_MASK);
}

void omap2xxx_cm_set_apll96_auto_low_power_stop(void)
{
	_omap2xxx_set_apll_autoidle(OMAP2XXX_APLL_AUTOIDLE_DISABLE,
				    OMAP24XX_AUTO_96M_MASK);
}

/*
 *
 */

/**
 * omap2xxx_cm_wait_module_ready - wait for a module to leave idle or standby
 * @prcm_mod: PRCM module offset
 * @idlest_id: CM_IDLESTx register ID (i.e., x = 1, 2, 3)
 * @idlest_shift: shift of the bit in the CM_IDLEST* register to check
 *
 * Wait for the PRCM to indicate that the module identified by
 * (@prcm_mod, @idlest_id, @idlest_shift) is clocked.  Return 0 upon
 * success or -EBUSY if the module doesn't enable in time.
 */
int omap2xxx_cm_wait_module_ready(s16 prcm_mod, u8 idlest_id, u8 idlest_shift)
{
	int ena = 0, i = 0;
	u8 cm_idlest_reg;
	u32 mask;

	if (!idlest_id || (idlest_id > ARRAY_SIZE(omap2xxx_cm_idlest_offs)))
		return -EINVAL;

	cm_idlest_reg = omap2xxx_cm_idlest_offs[idlest_id - 1];

	mask = 1 << idlest_shift;
	ena = mask;

	omap_test_timeout(((omap2_cm_read_mod_reg(prcm_mod, cm_idlest_reg) &
			    mask) == ena), MAX_MODULE_READY_TIME, i);

	return (i < MAX_MODULE_READY_TIME) ? 0 : -EBUSY;
}

/* Clockdomain low-level functions */

static void omap2xxx_clkdm_allow_idle(struct clockdomain *clkdm)
{
	if (atomic_read(&clkdm->usecount) > 0)
		_clkdm_add_autodeps(clkdm);

	omap2xxx_cm_clkdm_enable_hwsup(clkdm->pwrdm.ptr->prcm_offs,
				       clkdm->clktrctrl_mask);
}

static void omap2xxx_clkdm_deny_idle(struct clockdomain *clkdm)
{
	omap2xxx_cm_clkdm_disable_hwsup(clkdm->pwrdm.ptr->prcm_offs,
					clkdm->clktrctrl_mask);

	if (atomic_read(&clkdm->usecount) > 0)
		_clkdm_del_autodeps(clkdm);
}

static int omap2xxx_clkdm_clk_enable(struct clockdomain *clkdm)
{
	bool hwsup = false;

	if (!clkdm->clktrctrl_mask)
		return 0;

	hwsup = omap2xxx_cm_is_clkdm_in_hwsup(clkdm->pwrdm.ptr->prcm_offs,
					      clkdm->clktrctrl_mask);

	if (hwsup) {
		/* Disable HW transitions when we are changing deps */
		omap2xxx_cm_clkdm_disable_hwsup(clkdm->pwrdm.ptr->prcm_offs,
						clkdm->clktrctrl_mask);
		_clkdm_add_autodeps(clkdm);
		omap2xxx_cm_clkdm_enable_hwsup(clkdm->pwrdm.ptr->prcm_offs,
					       clkdm->clktrctrl_mask);
	} else {
		if (clkdm->flags & CLKDM_CAN_FORCE_WAKEUP)
			omap2xxx_clkdm_wakeup(clkdm);
	}

	return 0;
}

static int omap2xxx_clkdm_clk_disable(struct clockdomain *clkdm)
{
	bool hwsup = false;

	if (!clkdm->clktrctrl_mask)
		return 0;

	hwsup = omap2xxx_cm_is_clkdm_in_hwsup(clkdm->pwrdm.ptr->prcm_offs,
					      clkdm->clktrctrl_mask);

	if (hwsup) {
		/* Disable HW transitions when we are changing deps */
		omap2xxx_cm_clkdm_disable_hwsup(clkdm->pwrdm.ptr->prcm_offs,
						clkdm->clktrctrl_mask);
		_clkdm_del_autodeps(clkdm);
		omap2xxx_cm_clkdm_enable_hwsup(clkdm->pwrdm.ptr->prcm_offs,
					       clkdm->clktrctrl_mask);
	} else {
		if (clkdm->flags & CLKDM_CAN_FORCE_SLEEP)
			omap2xxx_clkdm_sleep(clkdm);
	}

	return 0;
}

struct clkdm_ops omap2_clkdm_operations = {
	.clkdm_add_wkdep	= omap2_clkdm_add_wkdep,
	.clkdm_del_wkdep	= omap2_clkdm_del_wkdep,
	.clkdm_read_wkdep	= omap2_clkdm_read_wkdep,
	.clkdm_clear_all_wkdeps	= omap2_clkdm_clear_all_wkdeps,
	.clkdm_sleep		= omap2xxx_clkdm_sleep,
	.clkdm_wakeup		= omap2xxx_clkdm_wakeup,
	.clkdm_allow_idle	= omap2xxx_clkdm_allow_idle,
	.clkdm_deny_idle	= omap2xxx_clkdm_deny_idle,
	.clkdm_clk_enable	= omap2xxx_clkdm_clk_enable,
	.clkdm_clk_disable	= omap2xxx_clkdm_clk_disable,
};

