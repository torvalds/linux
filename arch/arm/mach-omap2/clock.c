/*
 *  linux/arch/arm/mach-omap2/clock.c
 *
 *  Copyright (C) 2005-2008 Texas Instruments, Inc.
 *  Copyright (C) 2004-2010 Nokia Corporation
 *
 *  Contacts:
 *  Richard Woodruff <r-woodruff2@ti.com>
 *  Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#undef DEBUG

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/regmap.h>
#include <linux/of_address.h>
#include <linux/bootmem.h>
#include <asm/cpu.h>

#include <trace/events/power.h>

#include "soc.h"
#include "clockdomain.h"
#include "clock.h"
#include "cm.h"
#include "cm2xxx.h"
#include "cm3xxx.h"
#include "cm-regbits-24xx.h"
#include "cm-regbits-34xx.h"
#include "common.h"

/*
 * MAX_MODULE_ENABLE_WAIT: maximum of number of microseconds to wait
 * for a module to indicate that it is no longer in idle
 */
#define MAX_MODULE_ENABLE_WAIT		100000

u16 cpu_mask;

/*
 * Clock features setup. Used instead of CPU type checks.
 */
struct ti_clk_features ti_clk_features;

/* DPLL valid Fint frequency band limits - from 34xx TRM Section 4.7.6.2 */
#define OMAP3430_DPLL_FINT_BAND1_MIN	750000
#define OMAP3430_DPLL_FINT_BAND1_MAX	2100000
#define OMAP3430_DPLL_FINT_BAND2_MIN	7500000
#define OMAP3430_DPLL_FINT_BAND2_MAX	21000000

/*
 * DPLL valid Fint frequency range for OMAP36xx and OMAP4xxx.
 * From device data manual section 4.3 "DPLL and DLL Specifications".
 */
#define OMAP3PLUS_DPLL_FINT_MIN		32000
#define OMAP3PLUS_DPLL_FINT_MAX		52000000

/*
 * clkdm_control: if true, then when a clock is enabled in the
 * hardware, its clockdomain will first be enabled; and when a clock
 * is disabled in the hardware, its clockdomain will be disabled
 * afterwards.
 */
static bool clkdm_control = true;

static LIST_HEAD(clk_hw_omap_clocks);

struct clk_iomap {
	struct regmap *regmap;
	void __iomem *mem;
};

static struct clk_iomap *clk_memmaps[CLK_MAX_MEMMAPS];

static void clk_memmap_writel(u32 val, void __iomem *reg)
{
	struct clk_omap_reg *r = (struct clk_omap_reg *)&reg;
	struct clk_iomap *io = clk_memmaps[r->index];

	if (io->regmap)
		regmap_write(io->regmap, r->offset, val);
	else
		writel_relaxed(val, io->mem + r->offset);
}

static u32 clk_memmap_readl(void __iomem *reg)
{
	u32 val;
	struct clk_omap_reg *r = (struct clk_omap_reg *)&reg;
	struct clk_iomap *io = clk_memmaps[r->index];

	if (io->regmap)
		regmap_read(io->regmap, r->offset, &val);
	else
		val = readl_relaxed(io->mem + r->offset);

	return val;
}

void omap2_clk_writel(u32 val, struct clk_hw_omap *clk, void __iomem *reg)
{
	if (WARN_ON_ONCE(!(clk->flags & MEMMAP_ADDRESSING)))
		writel_relaxed(val, reg);
	else
		clk_memmap_writel(val, reg);
}

u32 omap2_clk_readl(struct clk_hw_omap *clk, void __iomem *reg)
{
	if (WARN_ON_ONCE(!(clk->flags & MEMMAP_ADDRESSING)))
		return readl_relaxed(reg);
	else
		return clk_memmap_readl(reg);
}

static struct ti_clk_ll_ops omap_clk_ll_ops = {
	.clk_readl = clk_memmap_readl,
	.clk_writel = clk_memmap_writel,
};

/**
 * omap2_clk_provider_init - initialize a clock provider
 * @match_table: DT device table to match for devices to init
 * @np: device node pointer for the this clock provider
 * @index: index for the clock provider
 + @syscon: syscon regmap pointer
 * @mem: iomem pointer for the clock provider memory area, only used if
 *	 syscon is not provided
 *
 * Initializes a clock provider module (CM/PRM etc.), registering
 * the memory mapping at specified index and initializing the
 * low level driver infrastructure. Returns 0 in success.
 */
int __init omap2_clk_provider_init(struct device_node *np, int index,
				   struct regmap *syscon, void __iomem *mem)
{
	struct clk_iomap *io;

	ti_clk_ll_ops = &omap_clk_ll_ops;

	io = kzalloc(sizeof(*io), GFP_KERNEL);

	io->regmap = syscon;
	io->mem = mem;

	clk_memmaps[index] = io;

	ti_dt_clk_init_provider(np, index);

	return 0;
}

/**
 * omap2_clk_legacy_provider_init - initialize a legacy clock provider
 * @index: index for the clock provider
 * @mem: iomem pointer for the clock provider memory area
 *
 * Initializes a legacy clock provider memory mapping.
 */
void __init omap2_clk_legacy_provider_init(int index, void __iomem *mem)
{
	struct clk_iomap *io;

	ti_clk_ll_ops = &omap_clk_ll_ops;

	io = memblock_virt_alloc(sizeof(*io), 0);

	io->mem = mem;

	clk_memmaps[index] = io;
}

/*
 * OMAP2+ specific clock functions
 */

/* Private functions */


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

	omap_test_timeout(((omap2_clk_readl(clk, reg) & mask) == ena),
			  MAX_MODULE_ENABLE_WAIT, i);

	if (i < MAX_MODULE_ENABLE_WAIT)
		pr_debug("omap clock: module associated with clock %s ready after %d loops\n",
			 name, i);
	else
		pr_err("omap clock: module associated with clock %s didn't enable in %d tries\n",
		       name, MAX_MODULE_ENABLE_WAIT);

	return (i < MAX_MODULE_ENABLE_WAIT) ? 1 : 0;
};

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
		if (!(omap2_clk_readl(clk, companion_reg) & (1 << other_bit)))
			return;
	}

	clk->ops->find_idlest(clk, &idlest_reg, &idlest_bit, &idlest_val);
	r = cm_split_idlest_reg(idlest_reg, &prcm_mod, &idlest_reg_id);
	if (r) {
		/* IDLEST register not in the CM module */
		_wait_idlest_generic(clk, idlest_reg, (1 << idlest_bit),
				     idlest_val, __clk_get_name(clk->hw.clk));
	} else {
		omap_cm_wait_module_ready(0, prcm_mod, idlest_reg_id,
					  idlest_bit);
	};
}

/* Public functions */

/**
 * omap2_init_clk_clkdm - look up a clockdomain name, store pointer in clk
 * @clk: OMAP clock struct ptr to use
 *
 * Convert a clockdomain name stored in a struct clk 'clk' into a
 * clockdomain pointer, and save it into the struct clk.  Intended to be
 * called during clk_register().  No return value.
 */
void omap2_init_clk_clkdm(struct clk_hw *hw)
{
	struct clk_hw_omap *clk = to_clk_hw_omap(hw);
	struct clockdomain *clkdm;
	const char *clk_name;

	if (!clk->clkdm_name)
		return;

	clk_name = __clk_get_name(hw->clk);

	clkdm = clkdm_lookup(clk->clkdm_name);
	if (clkdm) {
		pr_debug("clock: associated clk %s to clkdm %s\n",
			 clk_name, clk->clkdm_name);
		clk->clkdm = clkdm;
	} else {
		pr_debug("clock: could not associate clk %s to clkdm %s\n",
			 clk_name, clk->clkdm_name);
	}
}

/**
 * omap2_clk_disable_clkdm_control - disable clkdm control on clk enable/disable
 *
 * Prevent the OMAP clock code from calling into the clockdomain code
 * when a hardware clock in that clockdomain is enabled or disabled.
 * Intended to be called at init time from omap*_clk_init().  No
 * return value.
 */
void __init omap2_clk_disable_clkdm_control(void)
{
	clkdm_control = false;
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
		void __iomem **idlest_reg, u8 *idlest_bit, u8 *idlest_val)
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
	*idlest_val = ti_clk_features.cm_idlest_val;
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

	clk = to_clk_hw_omap(hw);

	if (clkdm_control && clk->clkdm) {
		ret = clkdm_clk_enable(clk->clkdm, hw->clk);
		if (ret) {
			WARN(1, "%s: could not enable %s's clockdomain %s: %d\n",
			     __func__, __clk_get_name(hw->clk),
			     clk->clkdm->name, ret);
			return ret;
		}
	}

	if (unlikely(clk->enable_reg == NULL)) {
		pr_err("%s: %s missing enable_reg\n", __func__,
		       __clk_get_name(hw->clk));
		ret = -EINVAL;
		goto err;
	}

	/* FIXME should not have INVERT_ENABLE bit here */
	v = omap2_clk_readl(clk, clk->enable_reg);
	if (clk->flags & INVERT_ENABLE)
		v &= ~(1 << clk->enable_bit);
	else
		v |= (1 << clk->enable_bit);
	omap2_clk_writel(v, clk, clk->enable_reg);
	v = omap2_clk_readl(clk, clk->enable_reg); /* OCP barrier */

	if (clk->ops && clk->ops->find_idlest)
		_omap2_module_wait_ready(clk);

	return 0;

err:
	if (clkdm_control && clk->clkdm)
		clkdm_clk_disable(clk->clkdm, hw->clk);
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
	if (!clk->enable_reg) {
		/*
		 * 'independent' here refers to a clock which is not
		 * controlled by its parent.
		 */
		pr_err("%s: independent clock %s has no enable_reg\n",
		       __func__, __clk_get_name(hw->clk));
		return;
	}

	v = omap2_clk_readl(clk, clk->enable_reg);
	if (clk->flags & INVERT_ENABLE)
		v |= (1 << clk->enable_bit);
	else
		v &= ~(1 << clk->enable_bit);
	omap2_clk_writel(v, clk, clk->enable_reg);
	/* No OCP barrier needed here since it is a disable operation */

	if (clkdm_control && clk->clkdm)
		clkdm_clk_disable(clk->clkdm, hw->clk);
}

/**
 * omap2_clkops_enable_clkdm - increment usecount on clkdm of @hw
 * @hw: struct clk_hw * of the clock being enabled
 *
 * Increment the usecount of the clockdomain of the clock pointed to
 * by @hw; if the usecount is 1, the clockdomain will be "enabled."
 * Only needed for clocks that don't use omap2_dflt_clk_enable() as
 * their enable function pointer.  Passes along the return value of
 * clkdm_clk_enable(), -EINVAL if @hw is not associated with a
 * clockdomain, or 0 if clock framework-based clockdomain control is
 * not implemented.
 */
int omap2_clkops_enable_clkdm(struct clk_hw *hw)
{
	struct clk_hw_omap *clk;
	int ret = 0;

	clk = to_clk_hw_omap(hw);

	if (unlikely(!clk->clkdm)) {
		pr_err("%s: %s: no clkdm set ?!\n", __func__,
		       __clk_get_name(hw->clk));
		return -EINVAL;
	}

	if (unlikely(clk->enable_reg))
		pr_err("%s: %s: should use dflt_clk_enable ?!\n", __func__,
		       __clk_get_name(hw->clk));

	if (!clkdm_control) {
		pr_err("%s: %s: clkfw-based clockdomain control disabled ?!\n",
		       __func__, __clk_get_name(hw->clk));
		return 0;
	}

	ret = clkdm_clk_enable(clk->clkdm, hw->clk);
	WARN(ret, "%s: could not enable %s's clockdomain %s: %d\n",
	     __func__, __clk_get_name(hw->clk), clk->clkdm->name, ret);

	return ret;
}

/**
 * omap2_clkops_disable_clkdm - decrement usecount on clkdm of @hw
 * @hw: struct clk_hw * of the clock being disabled
 *
 * Decrement the usecount of the clockdomain of the clock pointed to
 * by @hw; if the usecount is 0, the clockdomain will be "disabled."
 * Only needed for clocks that don't use omap2_dflt_clk_disable() as their
 * disable function pointer.  No return value.
 */
void omap2_clkops_disable_clkdm(struct clk_hw *hw)
{
	struct clk_hw_omap *clk;

	clk = to_clk_hw_omap(hw);

	if (unlikely(!clk->clkdm)) {
		pr_err("%s: %s: no clkdm set ?!\n", __func__,
		       __clk_get_name(hw->clk));
		return;
	}

	if (unlikely(clk->enable_reg))
		pr_err("%s: %s: should use dflt_clk_disable ?!\n", __func__,
		       __clk_get_name(hw->clk));

	if (!clkdm_control) {
		pr_err("%s: %s: clkfw-based clockdomain control disabled ?!\n",
		       __func__, __clk_get_name(hw->clk));
		return;
	}

	clkdm_clk_disable(clk->clkdm, hw->clk);
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

	v = omap2_clk_readl(clk, clk->enable_reg);

	if (clk->flags & INVERT_ENABLE)
		v ^= BIT(clk->enable_bit);

	v &= BIT(clk->enable_bit);

	return v ? 1 : 0;
}

static int __initdata mpurate;

/*
 * By default we use the rate set by the bootloader.
 * You can override this with mpurate= cmdline option.
 */
static int __init omap_clk_setup(char *str)
{
	get_option(&str, &mpurate);

	if (!mpurate)
		return 1;

	if (mpurate < 1000)
		mpurate *= 1000000;

	return 1;
}
__setup("mpurate=", omap_clk_setup);

/**
 * omap2_init_clk_hw_omap_clocks - initialize an OMAP clock
 * @clk: struct clk * to initialize
 *
 * Add an OMAP clock @clk to the internal list of OMAP clocks.  Used
 * temporarily for autoidle handling, until this support can be
 * integrated into the common clock framework code in some way.  No
 * return value.
 */
void omap2_init_clk_hw_omap_clocks(struct clk *clk)
{
	struct clk_hw_omap *c;

	if (__clk_get_flags(clk) & CLK_IS_BASIC)
		return;

	c = to_clk_hw_omap(__clk_get_hw(clk));
	list_add(&c->node, &clk_hw_omap_clocks);
}

/**
 * omap2_clk_enable_autoidle_all - enable autoidle on all OMAP clocks that
 * support it
 *
 * Enable clock autoidle on all OMAP clocks that have allow_idle
 * function pointers associated with them.  This function is intended
 * to be temporary until support for this is added to the common clock
 * code.  Returns 0.
 */
int omap2_clk_enable_autoidle_all(void)
{
	struct clk_hw_omap *c;

	list_for_each_entry(c, &clk_hw_omap_clocks, node)
		if (c->ops && c->ops->allow_idle)
			c->ops->allow_idle(c);

	of_ti_clk_allow_autoidle_all();

	return 0;
}

/**
 * omap2_clk_disable_autoidle_all - disable autoidle on all OMAP clocks that
 * support it
 *
 * Disable clock autoidle on all OMAP clocks that have allow_idle
 * function pointers associated with them.  This function is intended
 * to be temporary until support for this is added to the common clock
 * code.  Returns 0.
 */
int omap2_clk_disable_autoidle_all(void)
{
	struct clk_hw_omap *c;

	list_for_each_entry(c, &clk_hw_omap_clocks, node)
		if (c->ops && c->ops->deny_idle)
			c->ops->deny_idle(c);

	of_ti_clk_deny_autoidle_all();

	return 0;
}

/**
 * omap2_clk_deny_idle - disable autoidle on an OMAP clock
 * @clk: struct clk * to disable autoidle for
 *
 * Disable autoidle on an OMAP clock.
 */
int omap2_clk_deny_idle(struct clk *clk)
{
	struct clk_hw_omap *c;

	if (__clk_get_flags(clk) & CLK_IS_BASIC)
		return -EINVAL;

	c = to_clk_hw_omap(__clk_get_hw(clk));
	if (c->ops && c->ops->deny_idle)
		c->ops->deny_idle(c);
	return 0;
}

/**
 * omap2_clk_allow_idle - enable autoidle on an OMAP clock
 * @clk: struct clk * to enable autoidle for
 *
 * Enable autoidle on an OMAP clock.
 */
int omap2_clk_allow_idle(struct clk *clk)
{
	struct clk_hw_omap *c;

	if (__clk_get_flags(clk) & CLK_IS_BASIC)
		return -EINVAL;

	c = to_clk_hw_omap(__clk_get_hw(clk));
	if (c->ops && c->ops->allow_idle)
		c->ops->allow_idle(c);
	return 0;
}

/**
 * omap2_clk_enable_init_clocks - prepare & enable a list of clocks
 * @clk_names: ptr to an array of strings of clock names to enable
 * @num_clocks: number of clock names in @clk_names
 *
 * Prepare and enable a list of clocks, named by @clk_names.  No
 * return value. XXX Deprecated; only needed until these clocks are
 * properly claimed and enabled by the drivers or core code that uses
 * them.  XXX What code disables & calls clk_put on these clocks?
 */
void omap2_clk_enable_init_clocks(const char **clk_names, u8 num_clocks)
{
	struct clk *init_clk;
	int i;

	for (i = 0; i < num_clocks; i++) {
		init_clk = clk_get(NULL, clk_names[i]);
		if (WARN(IS_ERR(init_clk), "could not find init clock %s\n",
				clk_names[i]))
			continue;
		clk_prepare_enable(init_clk);
	}
}

const struct clk_hw_omap_ops clkhwops_wait = {
	.find_idlest	= omap2_clk_dflt_find_idlest,
	.find_companion	= omap2_clk_dflt_find_companion,
};

/**
 * omap2_clk_switch_mpurate_at_boot - switch ARM MPU rate by boot-time argument
 * @mpurate_ck_name: clk name of the clock to change rate
 *
 * Change the ARM MPU clock rate to the rate specified on the command
 * line, if one was specified.  @mpurate_ck_name should be
 * "virt_prcm_set" on OMAP2xxx and "dpll1_ck" on OMAP34xx/OMAP36xx.
 * XXX Does not handle voltage scaling - on OMAP2xxx this is currently
 * handled by the virt_prcm_set clock, but this should be handled by
 * the OPP layer.  XXX This is intended to be handled by the OPP layer
 * code in the near future and should be removed from the clock code.
 * Returns -EINVAL if 'mpurate' is zero or if clk_set_rate() rejects
 * the rate, -ENOENT if the struct clk referred to by @mpurate_ck_name
 * cannot be found, or 0 upon success.
 */
int __init omap2_clk_switch_mpurate_at_boot(const char *mpurate_ck_name)
{
	struct clk *mpurate_ck;
	int r;

	if (!mpurate)
		return -EINVAL;

	mpurate_ck = clk_get(NULL, mpurate_ck_name);
	if (WARN(IS_ERR(mpurate_ck), "Failed to get %s.\n", mpurate_ck_name))
		return -ENOENT;

	r = clk_set_rate(mpurate_ck, mpurate);
	if (r < 0) {
		WARN(1, "clock: %s: unable to set MPU rate to %d: %d\n",
		     mpurate_ck_name, mpurate, r);
		clk_put(mpurate_ck);
		return -EINVAL;
	}

	calibrate_delay();
	clk_put(mpurate_ck);

	return 0;
}

/**
 * omap2_clk_print_new_rates - print summary of current clock tree rates
 * @hfclkin_ck_name: clk name for the off-chip HF oscillator
 * @core_ck_name: clk name for the on-chip CORE_CLK
 * @mpu_ck_name: clk name for the ARM MPU clock
 *
 * Prints a short message to the console with the HFCLKIN oscillator
 * rate, the rate of the CORE clock, and the rate of the ARM MPU clock.
 * Called by the boot-time MPU rate switching code.   XXX This is intended
 * to be handled by the OPP layer code in the near future and should be
 * removed from the clock code.  No return value.
 */
void __init omap2_clk_print_new_rates(const char *hfclkin_ck_name,
				      const char *core_ck_name,
				      const char *mpu_ck_name)
{
	struct clk *hfclkin_ck, *core_ck, *mpu_ck;
	unsigned long hfclkin_rate;

	mpu_ck = clk_get(NULL, mpu_ck_name);
	if (WARN(IS_ERR(mpu_ck), "clock: failed to get %s.\n", mpu_ck_name))
		return;

	core_ck = clk_get(NULL, core_ck_name);
	if (WARN(IS_ERR(core_ck), "clock: failed to get %s.\n", core_ck_name))
		return;

	hfclkin_ck = clk_get(NULL, hfclkin_ck_name);
	if (WARN(IS_ERR(hfclkin_ck), "Failed to get %s.\n", hfclkin_ck_name))
		return;

	hfclkin_rate = clk_get_rate(hfclkin_ck);

	pr_info("Switched to new clocking rate (Crystal/Core/MPU): %ld.%01ld/%ld/%ld MHz\n",
		(hfclkin_rate / 1000000), ((hfclkin_rate / 100000) % 10),
		(clk_get_rate(core_ck) / 1000000),
		(clk_get_rate(mpu_ck) / 1000000));
}

/**
 * ti_clk_init_features - init clock features struct for the SoC
 *
 * Initializes the clock features struct based on the SoC type.
 */
void __init ti_clk_init_features(void)
{
	/* Fint setup for DPLLs */
	if (cpu_is_omap3430()) {
		ti_clk_features.fint_min = OMAP3430_DPLL_FINT_BAND1_MIN;
		ti_clk_features.fint_max = OMAP3430_DPLL_FINT_BAND2_MAX;
		ti_clk_features.fint_band1_max = OMAP3430_DPLL_FINT_BAND1_MAX;
		ti_clk_features.fint_band2_min = OMAP3430_DPLL_FINT_BAND2_MIN;
	} else {
		ti_clk_features.fint_min = OMAP3PLUS_DPLL_FINT_MIN;
		ti_clk_features.fint_max = OMAP3PLUS_DPLL_FINT_MAX;
	}

	/* Bypass value setup for DPLLs */
	if (cpu_is_omap24xx()) {
		ti_clk_features.dpll_bypass_vals |=
			(1 << OMAP2XXX_EN_DPLL_LPBYPASS) |
			(1 << OMAP2XXX_EN_DPLL_FRBYPASS);
	} else if (cpu_is_omap34xx()) {
		ti_clk_features.dpll_bypass_vals |=
			(1 << OMAP3XXX_EN_DPLL_LPBYPASS) |
			(1 << OMAP3XXX_EN_DPLL_FRBYPASS);
	} else if (soc_is_am33xx() || cpu_is_omap44xx() || soc_is_am43xx() ||
		   soc_is_omap54xx() || soc_is_dra7xx()) {
		ti_clk_features.dpll_bypass_vals |=
			(1 << OMAP4XXX_EN_DPLL_LPBYPASS) |
			(1 << OMAP4XXX_EN_DPLL_FRBYPASS) |
			(1 << OMAP4XXX_EN_DPLL_MNBYPASS);
	}

	/* Jitter correction only available on OMAP343X */
	if (cpu_is_omap343x())
		ti_clk_features.flags |= TI_CLK_DPLL_HAS_FREQSEL;

	/* Idlest value for interface clocks.
	 * 24xx uses 0 to indicate not ready, and 1 to indicate ready.
	 * 34xx reverses this, just to keep us on our toes
	 * AM35xx uses both, depending on the module.
	 */
	if (cpu_is_omap24xx())
		ti_clk_features.cm_idlest_val = OMAP24XX_CM_IDLEST_VAL;
	else if (cpu_is_omap34xx())
		ti_clk_features.cm_idlest_val = OMAP34XX_CM_IDLEST_VAL;

	/* On OMAP3430 ES1.0, DPLL4 can't be re-programmed */
	if (omap_rev() == OMAP3430_REV_ES1_0)
		ti_clk_features.flags |= TI_CLK_DPLL4_DENY_REPROGRAM;
}
