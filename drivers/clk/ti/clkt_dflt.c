/*
 * Default clock type
 *
 * Copyright (C) 2005-2008, 2015 Texas Instruments, Inc.
 * Copyright (C) 2004-2010 Nokia Corporation
 *
 * Contacts:
 * Richard Woodruff <r-woodruff2@ti.com>
 * Paul Walmsley
 * Tero Kristo <t-kristo@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/clk/ti.h>
#include <linux/delay.h>

#include "clock.h"

/*
 * MAX_MODULE_ENABLE_WAIT: maximum of number of microseconds to wait
 * for a module to indicate that it is no longer in idle
 */
#define MAX_MODULE_ENABLE_WAIT		100000

/*
 * CM module register offsets, used for calculating the companion
 * register addresses.
 */
#define CM_FCLKEN			0x0000
#define CM_ICLKEN			0x0010

/**
 * _wait_idlest_generic - wait for a module to leave the idle state
 * @clk: module clock to wait for (needed for register offsets)
 * @reg: virtual address of module IDLEST register
 * @mask: value to mask against to determine if the module is active
 * @idlest: idle state indicator (0 or 1) for the clock
 * @name: name of the clock (for printk)
 *
 * Wait for a module to leave idle, where its idle-status register is
 * not inside the CM module.  Returns 1 if the module left idle
 * promptly, or 0 if the module did not leave idle before the timeout
 * elapsed.  XXX Deprecated - should be moved into drivers for the
 * individual IP block that the IDLEST register exists in.
 */
static int _wait_idlest_generic(struct clk_hw_omap *clk, void __iomem *reg,
				u32 mask, u8 idlest, const char *name)
{
	int i = 0, ena = 0;

	ena = (idlest) ? 0 : mask;

	/* Wait until module enters enabled state */
	for (i = 0; i < MAX_MODULE_ENABLE_WAIT; i++) {
		if ((ti_clk_ll_ops->clk_readl(reg) & mask) == ena)
			break;
		udelay(1);
	}

	if (i < MAX_MODULE_ENABLE_WAIT)
		pr_debug("omap clock: module associated with clock %s ready after %d loops\n",
			 name, i);
	else
		pr_err("omap clock: module associated with clock %s didn't enable in %d tries\n",
		       name, MAX_MODULE_ENABLE_WAIT);

	return (i < MAX_MODULE_ENABLE_WAIT) ? 1 : 0;
}

/**
 * _omap2_module_wait_ready - wait for an OMAP module to leave IDLE
 * @clk: struct clk * belonging to the module
 *
 * If the necessary clocks for the OMAP hardware IP block that
 * corresponds to clock @clk are enabled, then wait for the module to
 * indicate readiness (i.e., to leave IDLE).  This code does not
 * belong in the clock code and will be moved in the medium term to
 * module-dependent code.  No return value.
 */
static void _omap2_module_wait_ready(struct clk_hw_omap *clk)
{
	void __iomem *companion_reg, *idlest_reg;
	u8 other_bit, idlest_bit, idlest_val, idlest_reg_id;
	s16 prcm_mod;
	int r;

	/* Not all modules have multiple clocks that their IDLEST depends on */
	if (clk->ops->find_companion) {
		clk->ops->find_companion(clk, &companion_reg, &other_bit);
		if (!(ti_clk_ll_ops->clk_readl(companion_reg) &
		      (1 << other_bit)))
			return;
	}

	clk->ops->find_idlest(clk, &idlest_reg, &idlest_bit, &idlest_val);
	r = ti_clk_ll_ops->cm_split_idlest_reg(idlest_reg, &prcm_mod,
					       &idlest_reg_id);
	if (r) {
		/* IDLEST register not in the CM module */
		_wait_idlest_generic(clk, idlest_reg, (1 << idlest_bit),
				     idlest_val, clk_hw_get_name(&clk->hw));
	} else {
		ti_clk_ll_ops->cm_wait_module_ready(0, prcm_mod, idlest_reg_id,
						    idlest_bit);
	}
}

/**
 * omap2_clk_dflt_find_companion - find companion clock to @clk
 * @clk: struct clk * to find the companion clock of
 * @other_reg: void __iomem ** to return the companion clock CM_*CLKEN va in
 * @other_bit: u8 ** to return the companion clock bit shift in
 *
 * Note: We don't need special code here for INVERT_ENABLE for the
 * time being since INVERT_ENABLE only applies to clocks enabled by
 * CM_CLKEN_PLL
 *
 * Convert CM_ICLKEN* <-> CM_FCLKEN*.  This conversion assumes it's
 * just a matter of XORing the bits.
 *
 * Some clocks don't have companion clocks.  For example, modules with
 * only an interface clock (such as MAILBOXES) don't have a companion
 * clock.  Right now, this code relies on the hardware exporting a bit
 * in the correct companion register that indicates that the
 * nonexistent 'companion clock' is active.  Future patches will
 * associate this type of code with per-module data structures to
 * avoid this issue, and remove the casts.  No return value.
 */
void omap2_clk_dflt_find_companion(struct clk_hw_omap *clk,
				   void __iomem **other_reg, u8 *other_bit)
{
	u32 r;

	/*
	 * Convert CM_ICLKEN* <-> CM_FCLKEN*.  This conversion assumes
	 * it's just a matter of XORing the bits.
	 */
	r = ((__force u32)clk->enable_reg ^ (CM_FCLKEN ^ CM_ICLKEN));

	*other_reg = (__force void __iomem *)r;
	*other_bit = clk->enable_bit;
}

/**
 * omap2_clk_dflt_find_idlest - find CM_IDLEST reg va, bit shift for @clk
 * @clk: struct clk * to find IDLEST info for
 * @idlest_reg: void __iomem ** to return the CM_IDLEST va in
 * @idlest_bit: u8 * to return the CM_IDLEST bit shift in
 * @idlest_val: u8 * to return the idle status indicator
 *
 * Return the CM_IDLEST register address and bit shift corresponding
 * to the module that "owns" this clock.  This default code assumes
 * that the CM_IDLEST bit shift is the CM_*CLKEN bit shift, and that
 * the IDLEST register address ID corresponds to the CM_*CLKEN
 * register address ID (e.g., that CM_FCLKEN2 corresponds to
 * CM_IDLEST2).  This is not true for all modules.  No return value.
 */
void omap2_clk_dflt_find_idlest(struct clk_hw_omap *clk,
				void __iomem **idlest_reg, u8 *idlest_bit,
				u8 *idlest_val)
{
	u32 r;

	r = (((__force u32)clk->enable_reg & ~0xf0) | 0x20);
	*idlest_reg = (__force void __iomem *)r;
	*idlest_bit = clk->enable_bit;

	/*
	 * 24xx uses 0 to indicate not ready, and 1 to indicate ready.
	 * 34xx reverses this, just to keep us on our toes
	 * AM35xx uses both, depending on the module.
	 */
	*idlest_val = ti_clk_get_features()->cm_idlest_val;
}

/**
 * omap2_dflt_clk_enable - enable a clock in the hardware
 * @hw: struct clk_hw * of the clock to enable
 *
 * Enable the clock @hw in the hardware.  We first call into the OMAP
 * clockdomain code to "enable" the corresponding clockdomain if this
 * is the first enabled user of the clockdomain.  Then program the
 * hardware to enable the clock.  Then wait for the IP block that uses
 * this clock to leave idle (if applicable).  Returns the error value
 * from clkdm_clk_enable() if it terminated with an error, or -EINVAL
 * if @hw has a null clock enable_reg, or zero upon success.
 */
int omap2_dflt_clk_enable(struct clk_hw *hw)
{
	struct clk_hw_omap *clk;
	u32 v;
	int ret = 0;
	bool clkdm_control;

	if (ti_clk_get_features()->flags & TI_CLK_DISABLE_CLKDM_CONTROL)
		clkdm_control = false;
	else
		clkdm_control = true;

	clk = to_clk_hw_omap(hw);

	if (clkdm_control && clk->clkdm) {
		ret = ti_clk_ll_ops->clkdm_clk_enable(clk->clkdm, hw->clk);
		if (ret) {
			WARN(1,
			     "%s: could not enable %s's clockdomain %s: %d\n",
			     __func__, clk_hw_get_name(hw),
			     clk->clkdm_name, ret);
			return ret;
		}
	}

	if (IS_ERR(clk->enable_reg)) {
		pr_err("%s: %s missing enable_reg\n", __func__,
		       clk_hw_get_name(hw));
		ret = -EINVAL;
		goto err;
	}

	/* FIXME should not have INVERT_ENABLE bit here */
	v = ti_clk_ll_ops->clk_readl(clk->enable_reg);
	if (clk->flags & INVERT_ENABLE)
		v &= ~(1 << clk->enable_bit);
	else
		v |= (1 << clk->enable_bit);
	ti_clk_ll_ops->clk_writel(v, clk->enable_reg);
	v = ti_clk_ll_ops->clk_readl(clk->enable_reg); /* OCP barrier */

	if (clk->ops && clk->ops->find_idlest)
		_omap2_module_wait_ready(clk);

	return 0;

err:
	if (clkdm_control && clk->clkdm)
		ti_clk_ll_ops->clkdm_clk_disable(clk->clkdm, hw->clk);
	return ret;
}

/**
 * omap2_dflt_clk_disable - disable a clock in the hardware
 * @hw: struct clk_hw * of the clock to disable
 *
 * Disable the clock @hw in the hardware, and call into the OMAP
 * clockdomain code to "disable" the corresponding clockdomain if all
 * clocks/hwmods in that clockdomain are now disabled.  No return
 * value.
 */
void omap2_dflt_clk_disable(struct clk_hw *hw)
{
	struct clk_hw_omap *clk;
	u32 v;

	clk = to_clk_hw_omap(hw);
	if (IS_ERR(clk->enable_reg)) {
		/*
		 * 'independent' here refers to a clock which is not
		 * controlled by its parent.
		 */
		pr_err("%s: independent clock %s has no enable_reg\n",
		       __func__, clk_hw_get_name(hw));
		return;
	}

	v = ti_clk_ll_ops->clk_readl(clk->enable_reg);
	if (clk->flags & INVERT_ENABLE)
		v |= (1 << clk->enable_bit);
	else
		v &= ~(1 << clk->enable_bit);
	ti_clk_ll_ops->clk_writel(v, clk->enable_reg);
	/* No OCP barrier needed here since it is a disable operation */

	if (!(ti_clk_get_features()->flags & TI_CLK_DISABLE_CLKDM_CONTROL) &&
	    clk->clkdm)
		ti_clk_ll_ops->clkdm_clk_disable(clk->clkdm, hw->clk);
}

/**
 * omap2_dflt_clk_is_enabled - is clock enabled in the hardware?
 * @hw: struct clk_hw * to check
 *
 * Return 1 if the clock represented by @hw is enabled in the
 * hardware, or 0 otherwise.  Intended for use in the struct
 * clk_ops.is_enabled function pointer.
 */
int omap2_dflt_clk_is_enabled(struct clk_hw *hw)
{
	struct clk_hw_omap *clk = to_clk_hw_omap(hw);
	u32 v;

	v = ti_clk_ll_ops->clk_readl(clk->enable_reg);

	if (clk->flags & INVERT_ENABLE)
		v ^= BIT(clk->enable_bit);

	v &= BIT(clk->enable_bit);

	return v ? 1 : 0;
}

const struct clk_hw_omap_ops clkhwops_wait = {
	.find_idlest	= omap2_clk_dflt_find_idlest,
	.find_companion	= omap2_clk_dflt_find_companion,
};
