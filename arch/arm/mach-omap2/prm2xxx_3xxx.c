/*
 * OMAP2/3 PRM module functions
 *
 * Copyright (C) 2010-2011 Texas Instruments, Inc.
 * Copyright (C) 2010 Nokia Corporation
 * Benoît Cousson
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>

#include "powerdomain.h"
#include "prm2xxx_3xxx.h"
#include "prm-regbits-24xx.h"
#include "clockdomain.h"

/**
 * omap2_prm_is_hardreset_asserted - read the HW reset line state of
 * submodules contained in the hwmod module
 * @shift: register bit shift corresponding to the reset line to check
 * @part: PRM partition, ignored for OMAP2
 * @prm_mod: PRM submodule base (e.g. CORE_MOD)
 * @offset: register offset, ignored for OMAP2
 *
 * Returns 1 if the (sub)module hardreset line is currently asserted,
 * 0 if the (sub)module hardreset line is not currently asserted, or
 * -EINVAL if called while running on a non-OMAP2/3 chip.
 */
int omap2_prm_is_hardreset_asserted(u8 shift, u8 part, s16 prm_mod, u16 offset)
{
	return omap2_prm_read_mod_bits_shift(prm_mod, OMAP2_RM_RSTCTRL,
				       (1 << shift));
}

/**
 * omap2_prm_assert_hardreset - assert the HW reset line of a submodule
 * @shift: register bit shift corresponding to the reset line to assert
 * @part: PRM partition, ignored for OMAP2
 * @prm_mod: PRM submodule base (e.g. CORE_MOD)
 * @offset: register offset, ignored for OMAP2
 *
 * Some IPs like dsp or iva contain processors that require an HW
 * reset line to be asserted / deasserted in order to fully enable the
 * IP.  These modules may have multiple hard-reset lines that reset
 * different 'submodules' inside the IP block.  This function will
 * place the submodule into reset.  Returns 0 upon success or -EINVAL
 * upon an argument error.
 */
int omap2_prm_assert_hardreset(u8 shift, u8 part, s16 prm_mod, u16 offset)
{
	u32 mask;

	mask = 1 << shift;
	omap2_prm_rmw_mod_reg_bits(mask, mask, prm_mod, OMAP2_RM_RSTCTRL);

	return 0;
}

/**
 * omap2_prm_deassert_hardreset - deassert a submodule hardreset line and wait
 * @prm_mod: PRM submodule base (e.g. CORE_MOD)
 * @rst_shift: register bit shift corresponding to the reset line to deassert
 * @st_shift: register bit shift for the status of the deasserted submodule
 * @part: PRM partition, not used for OMAP2
 * @prm_mod: PRM submodule base (e.g. CORE_MOD)
 * @rst_offset: reset register offset, not used for OMAP2
 * @st_offset: reset status register offset, not used for OMAP2
 *
 * Some IPs like dsp or iva contain processors that require an HW
 * reset line to be asserted / deasserted in order to fully enable the
 * IP.  These modules may have multiple hard-reset lines that reset
 * different 'submodules' inside the IP block.  This function will
 * take the submodule out of reset and wait until the PRCM indicates
 * that the reset has completed before returning.  Returns 0 upon success or
 * -EINVAL upon an argument error, -EEXIST if the submodule was already out
 * of reset, or -EBUSY if the submodule did not exit reset promptly.
 */
int omap2_prm_deassert_hardreset(u8 rst_shift, u8 st_shift, u8 part,
				 s16 prm_mod, u16 rst_offset, u16 st_offset)
{
	u32 rst, st;
	int c;

	rst = 1 << rst_shift;
	st = 1 << st_shift;

	/* Check the current status to avoid de-asserting the line twice */
	if (omap2_prm_read_mod_bits_shift(prm_mod, OMAP2_RM_RSTCTRL, rst) == 0)
		return -EEXIST;

	/* Clear the reset status by writing 1 to the status bit */
	omap2_prm_rmw_mod_reg_bits(0xffffffff, st, prm_mod, OMAP2_RM_RSTST);
	/* de-assert the reset control line */
	omap2_prm_rmw_mod_reg_bits(rst, 0, prm_mod, OMAP2_RM_RSTCTRL);
	/* wait the status to be set */
	omap_test_timeout(omap2_prm_read_mod_bits_shift(prm_mod, OMAP2_RM_RSTST,
						  st),
			  MAX_MODULE_HARDRESET_WAIT, c);

	return (c == MAX_MODULE_HARDRESET_WAIT) ? -EBUSY : 0;
}


/* Powerdomain low-level functions */

/* Common functions across OMAP2 and OMAP3 */
int omap2_pwrdm_set_mem_onst(struct powerdomain *pwrdm, u8 bank,
								u8 pwrst)
{
	u32 m;

	m = omap2_pwrdm_get_mem_bank_onstate_mask(bank);

	omap2_prm_rmw_mod_reg_bits(m, (pwrst << __ffs(m)), pwrdm->prcm_offs,
				   OMAP2_PM_PWSTCTRL);

	return 0;
}

int omap2_pwrdm_set_mem_retst(struct powerdomain *pwrdm, u8 bank,
								u8 pwrst)
{
	u32 m;

	m = omap2_pwrdm_get_mem_bank_retst_mask(bank);

	omap2_prm_rmw_mod_reg_bits(m, (pwrst << __ffs(m)), pwrdm->prcm_offs,
				   OMAP2_PM_PWSTCTRL);

	return 0;
}

int omap2_pwrdm_read_mem_pwrst(struct powerdomain *pwrdm, u8 bank)
{
	u32 m;

	m = omap2_pwrdm_get_mem_bank_stst_mask(bank);

	return omap2_prm_read_mod_bits_shift(pwrdm->prcm_offs, OMAP2_PM_PWSTST,
					     m);
}

int omap2_pwrdm_read_mem_retst(struct powerdomain *pwrdm, u8 bank)
{
	u32 m;

	m = omap2_pwrdm_get_mem_bank_retst_mask(bank);

	return omap2_prm_read_mod_bits_shift(pwrdm->prcm_offs,
					     OMAP2_PM_PWSTCTRL, m);
}

int omap2_pwrdm_set_logic_retst(struct powerdomain *pwrdm, u8 pwrst)
{
	u32 v;

	v = pwrst << __ffs(OMAP_LOGICRETSTATE_MASK);
	omap2_prm_rmw_mod_reg_bits(OMAP_LOGICRETSTATE_MASK, v, pwrdm->prcm_offs,
				   OMAP2_PM_PWSTCTRL);

	return 0;
}

int omap2_pwrdm_wait_transition(struct powerdomain *pwrdm)
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

int omap2_clkdm_add_wkdep(struct clockdomain *clkdm1,
			  struct clockdomain *clkdm2)
{
	omap2_prm_set_mod_reg_bits((1 << clkdm2->dep_bit),
				   clkdm1->pwrdm.ptr->prcm_offs, PM_WKDEP);
	return 0;
}

int omap2_clkdm_del_wkdep(struct clockdomain *clkdm1,
			  struct clockdomain *clkdm2)
{
	omap2_prm_clear_mod_reg_bits((1 << clkdm2->dep_bit),
				     clkdm1->pwrdm.ptr->prcm_offs, PM_WKDEP);
	return 0;
}

int omap2_clkdm_read_wkdep(struct clockdomain *clkdm1,
			   struct clockdomain *clkdm2)
{
	return omap2_prm_read_mod_bits_shift(clkdm1->pwrdm.ptr->prcm_offs,
					     PM_WKDEP, (1 << clkdm2->dep_bit));
}

/* XXX Caller must hold the clkdm's powerdomain lock */
int omap2_clkdm_clear_all_wkdeps(struct clockdomain *clkdm)
{
	struct clkdm_dep *cd;
	u32 mask = 0;

	for (cd = clkdm->wkdep_srcs; cd && cd->clkdm_name; cd++) {
		if (!cd->clkdm)
			continue; /* only happens if data is erroneous */

		/* PRM accesses are slow, so minimize them */
		mask |= 1 << cd->clkdm->dep_bit;
		cd->wkdep_usecount = 0;
	}

	omap2_prm_clear_mod_reg_bits(mask, clkdm->pwrdm.ptr->prcm_offs,
				     PM_WKDEP);
	return 0;
}

