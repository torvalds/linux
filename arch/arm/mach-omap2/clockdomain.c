/*
 * OMAP2/3/4 clockdomain framework functions
 *
 * Copyright (C) 2008-2010 Texas Instruments, Inc.
 * Copyright (C) 2008-2010 Nokia Corporation
 *
 * Written by Paul Walmsley and Jouni Högander
 * Added OMAP4 specific support by Abhijit Pagare <abhijitpagare@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#undef DEBUG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/limits.h>
#include <linux/err.h>

#include <linux/io.h>

#include <linux/bitops.h>

#include "prm.h"
#include "prm-regbits-24xx.h"
#include "cm.h"

#include <plat/clock.h>
#include <plat/powerdomain.h>
#include <plat/clockdomain.h>
#include <plat/prcm.h>

/* clkdm_list contains all registered struct clockdomains */
static LIST_HEAD(clkdm_list);

/* array of clockdomain deps to be added/removed when clkdm in hwsup mode */
static struct clkdm_autodep *autodeps;


/* Private functions */

static struct clockdomain *_clkdm_lookup(const char *name)
{
	struct clockdomain *clkdm, *temp_clkdm;

	if (!name)
		return NULL;

	clkdm = NULL;

	list_for_each_entry(temp_clkdm, &clkdm_list, node) {
		if (!strcmp(name, temp_clkdm->name)) {
			clkdm = temp_clkdm;
			break;
		}
	}

	return clkdm;
}

/**
 * _clkdm_register - register a clockdomain
 * @clkdm: struct clockdomain * to register
 *
 * Adds a clockdomain to the internal clockdomain list.
 * Returns -EINVAL if given a null pointer, -EEXIST if a clockdomain is
 * already registered by the provided name, or 0 upon success.
 */
static int _clkdm_register(struct clockdomain *clkdm)
{
	struct powerdomain *pwrdm;

	if (!clkdm || !clkdm->name)
		return -EINVAL;

	if (!omap_chip_is(clkdm->omap_chip))
		return -EINVAL;

	pwrdm = pwrdm_lookup(clkdm->pwrdm.name);
	if (!pwrdm) {
		pr_err("clockdomain: %s: powerdomain %s does not exist\n",
			clkdm->name, clkdm->pwrdm.name);
		return -EINVAL;
	}
	clkdm->pwrdm.ptr = pwrdm;

	/* Verify that the clockdomain is not already registered */
	if (_clkdm_lookup(clkdm->name))
		return -EEXIST;

	list_add(&clkdm->node, &clkdm_list);

	pwrdm_add_clkdm(pwrdm, clkdm);

	pr_debug("clockdomain: registered %s\n", clkdm->name);

	return 0;
}

/* _clkdm_deps_lookup - look up the specified clockdomain in a clkdm list */
static struct clkdm_dep *_clkdm_deps_lookup(struct clockdomain *clkdm,
					    struct clkdm_dep *deps)
{
	struct clkdm_dep *cd;

	if (!clkdm || !deps || !omap_chip_is(clkdm->omap_chip))
		return ERR_PTR(-EINVAL);

	for (cd = deps; cd->clkdm_name; cd++) {
		if (!omap_chip_is(cd->omap_chip))
			continue;

		if (!cd->clkdm && cd->clkdm_name)
			cd->clkdm = _clkdm_lookup(cd->clkdm_name);

		if (cd->clkdm == clkdm)
			break;
	}

	if (!cd->clkdm_name)
		return ERR_PTR(-ENOENT);

	return cd;
}

/*
 * _autodep_lookup - resolve autodep clkdm names to clkdm pointers; store
 * @autodep: struct clkdm_autodep * to resolve
 *
 * Resolve autodep clockdomain names to clockdomain pointers via
 * clkdm_lookup() and store the pointers in the autodep structure.  An
 * "autodep" is a clockdomain sleep/wakeup dependency that is
 * automatically added and removed whenever clocks in the associated
 * clockdomain are enabled or disabled (respectively) when the
 * clockdomain is in hardware-supervised mode.	Meant to be called
 * once at clockdomain layer initialization, since these should remain
 * fixed for a particular architecture.  No return value.
 */
static void _autodep_lookup(struct clkdm_autodep *autodep)
{
	struct clockdomain *clkdm;

	if (!autodep)
		return;

	if (!omap_chip_is(autodep->omap_chip))
		return;

	clkdm = clkdm_lookup(autodep->clkdm.name);
	if (!clkdm) {
		pr_err("clockdomain: autodeps: clockdomain %s does not exist\n",
			 autodep->clkdm.name);
		clkdm = ERR_PTR(-ENOENT);
	}
	autodep->clkdm.ptr = clkdm;
}

/*
 * _clkdm_add_autodeps - add auto sleepdeps/wkdeps to clkdm upon clock enable
 * @clkdm: struct clockdomain *
 *
 * Add the "autodep" sleep & wakeup dependencies to clockdomain 'clkdm'
 * in hardware-supervised mode.  Meant to be called from clock framework
 * when a clock inside clockdomain 'clkdm' is enabled.	No return value.
 */
static void _clkdm_add_autodeps(struct clockdomain *clkdm)
{
	struct clkdm_autodep *autodep;

	if (!autodeps)
		return;

	for (autodep = autodeps; autodep->clkdm.ptr; autodep++) {
		if (IS_ERR(autodep->clkdm.ptr))
			continue;

		if (!omap_chip_is(autodep->omap_chip))
			continue;

		pr_debug("clockdomain: adding %s sleepdep/wkdep for "
			 "clkdm %s\n", autodep->clkdm.ptr->name,
			 clkdm->name);

		clkdm_add_sleepdep(clkdm, autodep->clkdm.ptr);
		clkdm_add_wkdep(clkdm, autodep->clkdm.ptr);
	}
}

/*
 * _clkdm_add_autodeps - remove auto sleepdeps/wkdeps from clkdm
 * @clkdm: struct clockdomain *
 *
 * Remove the "autodep" sleep & wakeup dependencies from clockdomain 'clkdm'
 * in hardware-supervised mode.  Meant to be called from clock framework
 * when a clock inside clockdomain 'clkdm' is disabled.  No return value.
 */
static void _clkdm_del_autodeps(struct clockdomain *clkdm)
{
	struct clkdm_autodep *autodep;

	if (!autodeps)
		return;

	for (autodep = autodeps; autodep->clkdm.ptr; autodep++) {
		if (IS_ERR(autodep->clkdm.ptr))
			continue;

		if (!omap_chip_is(autodep->omap_chip))
			continue;

		pr_debug("clockdomain: removing %s sleepdep/wkdep for "
			 "clkdm %s\n", autodep->clkdm.ptr->name,
			 clkdm->name);

		clkdm_del_sleepdep(clkdm, autodep->clkdm.ptr);
		clkdm_del_wkdep(clkdm, autodep->clkdm.ptr);
	}
}

/*
 * _omap2_clkdm_set_hwsup - set the hwsup idle transition bit
 * @clkdm: struct clockdomain *
 * @enable: int 0 to disable, 1 to enable
 *
 * Internal helper for actually switching the bit that controls hwsup
 * idle transitions for clkdm.
 */
static void _omap2_clkdm_set_hwsup(struct clockdomain *clkdm, int enable)
{
	u32 bits, v;

	if (cpu_is_omap24xx()) {
		if (enable)
			bits = OMAP24XX_CLKSTCTRL_ENABLE_AUTO;
		else
			bits = OMAP24XX_CLKSTCTRL_DISABLE_AUTO;
	} else if (cpu_is_omap34xx() || cpu_is_omap44xx()) {
		if (enable)
			bits = OMAP34XX_CLKSTCTRL_ENABLE_AUTO;
		else
			bits = OMAP34XX_CLKSTCTRL_DISABLE_AUTO;
	} else {
		BUG();
	}

	bits = bits << __ffs(clkdm->clktrctrl_mask);

	v = __raw_readl(clkdm->clkstctrl_reg);
	v &= ~(clkdm->clktrctrl_mask);
	v |= bits;
	__raw_writel(v, clkdm->clkstctrl_reg);

}

/**
 * _init_wkdep_usecount - initialize wkdep usecounts to match hardware
 * @clkdm: clockdomain to initialize wkdep usecounts
 *
 * Initialize the wakeup dependency usecount variables for clockdomain @clkdm.
 * If a wakeup dependency is present in the hardware, the usecount will be
 * set to 1; otherwise, it will be set to 0.  Software should clear all
 * software wakeup dependencies prior to calling this function if it wishes
 * to ensure that all usecounts start at 0.  No return value.
 */
static void _init_wkdep_usecount(struct clockdomain *clkdm)
{
	u32 v;
	struct clkdm_dep *cd;

	if (!clkdm->wkdep_srcs)
		return;

	for (cd = clkdm->wkdep_srcs; cd->clkdm_name; cd++) {
		if (!omap_chip_is(cd->omap_chip))
			continue;

		if (!cd->clkdm && cd->clkdm_name)
			cd->clkdm = _clkdm_lookup(cd->clkdm_name);

		if (!cd->clkdm) {
			WARN(!cd->clkdm, "clockdomain: %s: wkdep clkdm %s not "
			     "found\n", clkdm->name, cd->clkdm_name);
			continue;
		}

		v = prm_read_mod_bits_shift(clkdm->pwrdm.ptr->prcm_offs,
					    PM_WKDEP,
					    (1 << cd->clkdm->dep_bit));

		if (v)
			pr_debug("clockdomain: %s: wakeup dependency already "
				 "set to wake up when %s wakes\n",
				 clkdm->name, cd->clkdm->name);

		atomic_set(&cd->wkdep_usecount, (v) ? 1 : 0);
	}
}

/**
 * _init_sleepdep_usecount - initialize sleepdep usecounts to match hardware
 * @clkdm: clockdomain to initialize sleepdep usecounts
 *
 * Initialize the sleep dependency usecount variables for clockdomain @clkdm.
 * If a sleep dependency is present in the hardware, the usecount will be
 * set to 1; otherwise, it will be set to 0.  Software should clear all
 * software sleep dependencies prior to calling this function if it wishes
 * to ensure that all usecounts start at 0.  No return value.
 */
static void _init_sleepdep_usecount(struct clockdomain *clkdm)
{
	u32 v;
	struct clkdm_dep *cd;

	if (!cpu_is_omap34xx())
		return;

	if (!clkdm->sleepdep_srcs)
		return;

	for (cd = clkdm->sleepdep_srcs; cd->clkdm_name; cd++) {
		if (!omap_chip_is(cd->omap_chip))
			continue;

		if (!cd->clkdm && cd->clkdm_name)
			cd->clkdm = _clkdm_lookup(cd->clkdm_name);

		if (!cd->clkdm) {
			WARN(!cd->clkdm, "clockdomain: %s: sleepdep clkdm %s "
			     "not found\n", clkdm->name, cd->clkdm_name);
			continue;
		}

		v = prm_read_mod_bits_shift(clkdm->pwrdm.ptr->prcm_offs,
					    OMAP3430_CM_SLEEPDEP,
					    (1 << cd->clkdm->dep_bit));

		if (v)
			pr_debug("clockdomain: %s: sleep dependency already "
				 "set to prevent from idling until %s "
				 "idles\n", clkdm->name, cd->clkdm->name);

		atomic_set(&cd->sleepdep_usecount, (v) ? 1 : 0);
	}
};

/* Public functions */

/**
 * clkdm_init - set up the clockdomain layer
 * @clkdms: optional pointer to an array of clockdomains to register
 * @init_autodeps: optional pointer to an array of autodeps to register
 *
 * Set up internal state.  If a pointer to an array of clockdomains
 * @clkdms was supplied, loop through the list of clockdomains,
 * register all that are available on the current platform. Similarly,
 * if a pointer to an array of clockdomain autodependencies
 * @init_autodeps was provided, register those.  No return value.
 */
void clkdm_init(struct clockdomain **clkdms,
		struct clkdm_autodep *init_autodeps)
{
	struct clockdomain **c = NULL;
	struct clockdomain *clkdm;
	struct clkdm_autodep *autodep = NULL;

	if (clkdms)
		for (c = clkdms; *c; c++)
			_clkdm_register(*c);

	autodeps = init_autodeps;
	if (autodeps)
		for (autodep = autodeps; autodep->clkdm.ptr; autodep++)
			_autodep_lookup(autodep);

	/*
	 * Ensure that the *dep_usecount registers reflect the current
	 * state of the PRCM.
	 */
	list_for_each_entry(clkdm, &clkdm_list, node) {
		_init_wkdep_usecount(clkdm);
		_init_sleepdep_usecount(clkdm);
	}
}

/**
 * clkdm_lookup - look up a clockdomain by name, return a pointer
 * @name: name of clockdomain
 *
 * Find a registered clockdomain by its name @name.  Returns a pointer
 * to the struct clockdomain if found, or NULL otherwise.
 */
struct clockdomain *clkdm_lookup(const char *name)
{
	struct clockdomain *clkdm, *temp_clkdm;

	if (!name)
		return NULL;

	clkdm = NULL;

	list_for_each_entry(temp_clkdm, &clkdm_list, node) {
		if (!strcmp(name, temp_clkdm->name)) {
			clkdm = temp_clkdm;
			break;
		}
	}

	return clkdm;
}

/**
 * clkdm_for_each - call function on each registered clockdomain
 * @fn: callback function *
 *
 * Call the supplied function @fn for each registered clockdomain.
 * The callback function @fn can return anything but 0 to bail
 * out early from the iterator.  The callback function is called with
 * the clkdm_mutex held, so no clockdomain structure manipulation
 * functions should be called from the callback, although hardware
 * clockdomain control functions are fine.  Returns the last return
 * value of the callback function, which should be 0 for success or
 * anything else to indicate failure; or -EINVAL if the function pointer
 * is null.
 */
int clkdm_for_each(int (*fn)(struct clockdomain *clkdm, void *user),
			void *user)
{
	struct clockdomain *clkdm;
	int ret = 0;

	if (!fn)
		return -EINVAL;

	list_for_each_entry(clkdm, &clkdm_list, node) {
		ret = (*fn)(clkdm, user);
		if (ret)
			break;
	}

	return ret;
}


/**
 * clkdm_get_pwrdm - return a ptr to the pwrdm that this clkdm resides in
 * @clkdm: struct clockdomain *
 *
 * Return a pointer to the struct powerdomain that the specified clockdomain
 * @clkdm exists in, or returns NULL if @clkdm is NULL.
 */
struct powerdomain *clkdm_get_pwrdm(struct clockdomain *clkdm)
{
	if (!clkdm)
		return NULL;

	return clkdm->pwrdm.ptr;
}


/* Hardware clockdomain control */

/**
 * clkdm_add_wkdep - add a wakeup dependency from clkdm2 to clkdm1
 * @clkdm1: wake this struct clockdomain * up (dependent)
 * @clkdm2: when this struct clockdomain * wakes up (source)
 *
 * When the clockdomain represented by @clkdm2 wakes up, wake up
 * @clkdm1. Implemented in hardware on the OMAP, this feature is
 * designed to reduce wakeup latency of the dependent clockdomain @clkdm1.
 * Returns -EINVAL if presented with invalid clockdomain pointers,
 * -ENOENT if @clkdm2 cannot wake up clkdm1 in hardware, or 0 upon
 * success.
 */
int clkdm_add_wkdep(struct clockdomain *clkdm1, struct clockdomain *clkdm2)
{
	struct clkdm_dep *cd;

	if (!clkdm1 || !clkdm2)
		return -EINVAL;

	cd = _clkdm_deps_lookup(clkdm2, clkdm1->wkdep_srcs);
	if (IS_ERR(cd)) {
		pr_debug("clockdomain: hardware cannot set/clear wake up of "
			 "%s when %s wakes up\n", clkdm1->name, clkdm2->name);
		return PTR_ERR(cd);
	}

	if (atomic_inc_return(&cd->wkdep_usecount) == 1) {
		pr_debug("clockdomain: hardware will wake up %s when %s wakes "
			 "up\n", clkdm1->name, clkdm2->name);

		prm_set_mod_reg_bits((1 << clkdm2->dep_bit),
				     clkdm1->pwrdm.ptr->prcm_offs, PM_WKDEP);
	}

	return 0;
}

/**
 * clkdm_del_wkdep - remove a wakeup dependency from clkdm2 to clkdm1
 * @clkdm1: wake this struct clockdomain * up (dependent)
 * @clkdm2: when this struct clockdomain * wakes up (source)
 *
 * Remove a wakeup dependency causing @clkdm1 to wake up when @clkdm2
 * wakes up.  Returns -EINVAL if presented with invalid clockdomain
 * pointers, -ENOENT if @clkdm2 cannot wake up clkdm1 in hardware, or
 * 0 upon success.
 */
int clkdm_del_wkdep(struct clockdomain *clkdm1, struct clockdomain *clkdm2)
{
	struct clkdm_dep *cd;

	if (!clkdm1 || !clkdm2)
		return -EINVAL;

	cd = _clkdm_deps_lookup(clkdm2, clkdm1->wkdep_srcs);
	if (IS_ERR(cd)) {
		pr_debug("clockdomain: hardware cannot set/clear wake up of "
			 "%s when %s wakes up\n", clkdm1->name, clkdm2->name);
		return PTR_ERR(cd);
	}

	if (atomic_dec_return(&cd->wkdep_usecount) == 0) {
		pr_debug("clockdomain: hardware will no longer wake up %s "
			 "after %s wakes up\n", clkdm1->name, clkdm2->name);

		prm_clear_mod_reg_bits((1 << clkdm2->dep_bit),
				       clkdm1->pwrdm.ptr->prcm_offs, PM_WKDEP);
	}

	return 0;
}

/**
 * clkdm_read_wkdep - read wakeup dependency state from clkdm2 to clkdm1
 * @clkdm1: wake this struct clockdomain * up (dependent)
 * @clkdm2: when this struct clockdomain * wakes up (source)
 *
 * Return 1 if a hardware wakeup dependency exists wherein @clkdm1 will be
 * awoken when @clkdm2 wakes up; 0 if dependency is not set; -EINVAL
 * if either clockdomain pointer is invalid; or -ENOENT if the hardware
 * is incapable.
 *
 * REVISIT: Currently this function only represents software-controllable
 * wakeup dependencies.  Wakeup dependencies fixed in hardware are not
 * yet handled here.
 */
int clkdm_read_wkdep(struct clockdomain *clkdm1, struct clockdomain *clkdm2)
{
	struct clkdm_dep *cd;

	if (!clkdm1 || !clkdm2)
		return -EINVAL;

	cd = _clkdm_deps_lookup(clkdm2, clkdm1->wkdep_srcs);
	if (IS_ERR(cd)) {
		pr_debug("clockdomain: hardware cannot set/clear wake up of "
			 "%s when %s wakes up\n", clkdm1->name, clkdm2->name);
		return PTR_ERR(cd);
	}

	/* XXX It's faster to return the atomic wkdep_usecount */
	return prm_read_mod_bits_shift(clkdm1->pwrdm.ptr->prcm_offs, PM_WKDEP,
				       (1 << clkdm2->dep_bit));
}

/**
 * clkdm_clear_all_wkdeps - remove all wakeup dependencies from target clkdm
 * @clkdm: struct clockdomain * to remove all wakeup dependencies from
 *
 * Remove all inter-clockdomain wakeup dependencies that could cause
 * @clkdm to wake.  Intended to be used during boot to initialize the
 * PRCM to a known state, after all clockdomains are put into swsup idle
 * and woken up.  Returns -EINVAL if @clkdm pointer is invalid, or
 * 0 upon success.
 */
int clkdm_clear_all_wkdeps(struct clockdomain *clkdm)
{
	struct clkdm_dep *cd;
	u32 mask = 0;

	if (!clkdm)
		return -EINVAL;

	for (cd = clkdm->wkdep_srcs; cd && cd->clkdm_name; cd++) {
		if (!omap_chip_is(cd->omap_chip))
			continue;

		/* PRM accesses are slow, so minimize them */
		mask |= 1 << cd->clkdm->dep_bit;
		atomic_set(&cd->wkdep_usecount, 0);
	}

	prm_clear_mod_reg_bits(mask, clkdm->pwrdm.ptr->prcm_offs, PM_WKDEP);

	return 0;
}

/**
 * clkdm_add_sleepdep - add a sleep dependency from clkdm2 to clkdm1
 * @clkdm1: prevent this struct clockdomain * from sleeping (dependent)
 * @clkdm2: when this struct clockdomain * is active (source)
 *
 * Prevent @clkdm1 from automatically going inactive (and then to
 * retention or off) if @clkdm2 is active.  Returns -EINVAL if
 * presented with invalid clockdomain pointers or called on a machine
 * that does not support software-configurable hardware sleep
 * dependencies, -ENOENT if the specified dependency cannot be set in
 * hardware, or 0 upon success.
 */
int clkdm_add_sleepdep(struct clockdomain *clkdm1, struct clockdomain *clkdm2)
{
	struct clkdm_dep *cd;

	if (!cpu_is_omap34xx())
		return -EINVAL;

	if (!clkdm1 || !clkdm2)
		return -EINVAL;

	cd = _clkdm_deps_lookup(clkdm2, clkdm1->sleepdep_srcs);
	if (IS_ERR(cd)) {
		pr_debug("clockdomain: hardware cannot set/clear sleep "
			 "dependency affecting %s from %s\n", clkdm1->name,
			 clkdm2->name);
		return PTR_ERR(cd);
	}

	if (atomic_inc_return(&cd->sleepdep_usecount) == 1) {
		pr_debug("clockdomain: will prevent %s from sleeping if %s "
			 "is active\n", clkdm1->name, clkdm2->name);

		cm_set_mod_reg_bits((1 << clkdm2->dep_bit),
				    clkdm1->pwrdm.ptr->prcm_offs,
				    OMAP3430_CM_SLEEPDEP);
	}

	return 0;
}

/**
 * clkdm_del_sleepdep - remove a sleep dependency from clkdm2 to clkdm1
 * @clkdm1: prevent this struct clockdomain * from sleeping (dependent)
 * @clkdm2: when this struct clockdomain * is active (source)
 *
 * Allow @clkdm1 to automatically go inactive (and then to retention or
 * off), independent of the activity state of @clkdm2.  Returns -EINVAL
 * if presented with invalid clockdomain pointers or called on a machine
 * that does not support software-configurable hardware sleep dependencies,
 * -ENOENT if the specified dependency cannot be cleared in hardware, or
 * 0 upon success.
 */
int clkdm_del_sleepdep(struct clockdomain *clkdm1, struct clockdomain *clkdm2)
{
	struct clkdm_dep *cd;

	if (!cpu_is_omap34xx())
		return -EINVAL;

	if (!clkdm1 || !clkdm2)
		return -EINVAL;

	cd = _clkdm_deps_lookup(clkdm2, clkdm1->sleepdep_srcs);
	if (IS_ERR(cd)) {
		pr_debug("clockdomain: hardware cannot set/clear sleep "
			 "dependency affecting %s from %s\n", clkdm1->name,
			 clkdm2->name);
		return PTR_ERR(cd);
	}

	if (atomic_dec_return(&cd->sleepdep_usecount) == 0) {
		pr_debug("clockdomain: will no longer prevent %s from "
			 "sleeping if %s is active\n", clkdm1->name,
			 clkdm2->name);

		cm_clear_mod_reg_bits((1 << clkdm2->dep_bit),
				      clkdm1->pwrdm.ptr->prcm_offs,
				      OMAP3430_CM_SLEEPDEP);
	}

	return 0;
}

/**
 * clkdm_read_sleepdep - read sleep dependency state from clkdm2 to clkdm1
 * @clkdm1: prevent this struct clockdomain * from sleeping (dependent)
 * @clkdm2: when this struct clockdomain * is active (source)
 *
 * Return 1 if a hardware sleep dependency exists wherein @clkdm1 will
 * not be allowed to automatically go inactive if @clkdm2 is active;
 * 0 if @clkdm1's automatic power state inactivity transition is independent
 * of @clkdm2's; -EINVAL if either clockdomain pointer is invalid or called
 * on a machine that does not support software-configurable hardware sleep
 * dependencies; or -ENOENT if the hardware is incapable.
 *
 * REVISIT: Currently this function only represents software-controllable
 * sleep dependencies.	Sleep dependencies fixed in hardware are not
 * yet handled here.
 */
int clkdm_read_sleepdep(struct clockdomain *clkdm1, struct clockdomain *clkdm2)
{
	struct clkdm_dep *cd;

	if (!cpu_is_omap34xx())
		return -EINVAL;

	if (!clkdm1 || !clkdm2)
		return -EINVAL;

	cd = _clkdm_deps_lookup(clkdm2, clkdm1->sleepdep_srcs);
	if (IS_ERR(cd)) {
		pr_debug("clockdomain: hardware cannot set/clear sleep "
			 "dependency affecting %s from %s\n", clkdm1->name,
			 clkdm2->name);
		return PTR_ERR(cd);
	}

	/* XXX It's faster to return the atomic sleepdep_usecount */
	return prm_read_mod_bits_shift(clkdm1->pwrdm.ptr->prcm_offs,
				       OMAP3430_CM_SLEEPDEP,
				       (1 << clkdm2->dep_bit));
}

/**
 * clkdm_clear_all_sleepdeps - remove all sleep dependencies from target clkdm
 * @clkdm: struct clockdomain * to remove all sleep dependencies from
 *
 * Remove all inter-clockdomain sleep dependencies that could prevent
 * @clkdm from idling.  Intended to be used during boot to initialize the
 * PRCM to a known state, after all clockdomains are put into swsup idle
 * and woken up.  Returns -EINVAL if @clkdm pointer is invalid, or
 * 0 upon success.
 */
int clkdm_clear_all_sleepdeps(struct clockdomain *clkdm)
{
	struct clkdm_dep *cd;
	u32 mask = 0;

	if (!cpu_is_omap34xx())
		return -EINVAL;

	if (!clkdm)
		return -EINVAL;

	for (cd = clkdm->sleepdep_srcs; cd && cd->clkdm_name; cd++) {
		if (!omap_chip_is(cd->omap_chip))
			continue;

		/* PRM accesses are slow, so minimize them */
		mask |= 1 << cd->clkdm->dep_bit;
		atomic_set(&cd->sleepdep_usecount, 0);
	}

	prm_clear_mod_reg_bits(mask, clkdm->pwrdm.ptr->prcm_offs,
			       OMAP3430_CM_SLEEPDEP);

	return 0;
}

/**
 * omap2_clkdm_clktrctrl_read - read the clkdm's current state transition mode
 * @clkdm: struct clkdm * of a clockdomain
 *
 * Return the clockdomain @clkdm current state transition mode from the
 * corresponding domain CM_CLKSTCTRL register.	Returns -EINVAL if @clkdm
 * is NULL or the current mode upon success.
 */
static int omap2_clkdm_clktrctrl_read(struct clockdomain *clkdm)
{
	u32 v;

	if (!clkdm)
		return -EINVAL;

	v = __raw_readl(clkdm->clkstctrl_reg);
	v &= clkdm->clktrctrl_mask;
	v >>= __ffs(clkdm->clktrctrl_mask);

	return v;
}

/**
 * omap2_clkdm_sleep - force clockdomain sleep transition
 * @clkdm: struct clockdomain *
 *
 * Instruct the CM to force a sleep transition on the specified
 * clockdomain @clkdm.  Returns -EINVAL if @clkdm is NULL or if
 * clockdomain does not support software-initiated sleep; 0 upon
 * success.
 */
int omap2_clkdm_sleep(struct clockdomain *clkdm)
{
	if (!clkdm)
		return -EINVAL;

	if (!(clkdm->flags & CLKDM_CAN_FORCE_SLEEP)) {
		pr_debug("clockdomain: %s does not support forcing "
			 "sleep via software\n", clkdm->name);
		return -EINVAL;
	}

	pr_debug("clockdomain: forcing sleep on %s\n", clkdm->name);

	if (cpu_is_omap24xx()) {

		cm_set_mod_reg_bits(OMAP24XX_FORCESTATE_MASK,
			    clkdm->pwrdm.ptr->prcm_offs, OMAP2_PM_PWSTCTRL);

	} else if (cpu_is_omap34xx() || cpu_is_omap44xx()) {

		u32 bits = (OMAP34XX_CLKSTCTRL_FORCE_SLEEP <<
			 __ffs(clkdm->clktrctrl_mask));

		u32 v = __raw_readl(clkdm->clkstctrl_reg);
		v &= ~(clkdm->clktrctrl_mask);
		v |= bits;
		__raw_writel(v, clkdm->clkstctrl_reg);

	} else {
		BUG();
	};

	return 0;
}

/**
 * omap2_clkdm_wakeup - force clockdomain wakeup transition
 * @clkdm: struct clockdomain *
 *
 * Instruct the CM to force a wakeup transition on the specified
 * clockdomain @clkdm.  Returns -EINVAL if @clkdm is NULL or if the
 * clockdomain does not support software-controlled wakeup; 0 upon
 * success.
 */
int omap2_clkdm_wakeup(struct clockdomain *clkdm)
{
	if (!clkdm)
		return -EINVAL;

	if (!(clkdm->flags & CLKDM_CAN_FORCE_WAKEUP)) {
		pr_debug("clockdomain: %s does not support forcing "
			 "wakeup via software\n", clkdm->name);
		return -EINVAL;
	}

	pr_debug("clockdomain: forcing wakeup on %s\n", clkdm->name);

	if (cpu_is_omap24xx()) {

		cm_clear_mod_reg_bits(OMAP24XX_FORCESTATE_MASK,
			      clkdm->pwrdm.ptr->prcm_offs, OMAP2_PM_PWSTCTRL);

	} else if (cpu_is_omap34xx() || cpu_is_omap44xx()) {

		u32 bits = (OMAP34XX_CLKSTCTRL_FORCE_WAKEUP <<
			 __ffs(clkdm->clktrctrl_mask));

		u32 v = __raw_readl(clkdm->clkstctrl_reg);
		v &= ~(clkdm->clktrctrl_mask);
		v |= bits;
		__raw_writel(v, clkdm->clkstctrl_reg);

	} else {
		BUG();
	};

	return 0;
}

/**
 * omap2_clkdm_allow_idle - enable hwsup idle transitions for clkdm
 * @clkdm: struct clockdomain *
 *
 * Allow the hardware to automatically switch the clockdomain @clkdm into
 * active or idle states, as needed by downstream clocks.  If the
 * clockdomain has any downstream clocks enabled in the clock
 * framework, wkdep/sleepdep autodependencies are added; this is so
 * device drivers can read and write to the device.  No return value.
 */
void omap2_clkdm_allow_idle(struct clockdomain *clkdm)
{
	if (!clkdm)
		return;

	if (!(clkdm->flags & CLKDM_CAN_ENABLE_AUTO)) {
		pr_debug("clock: automatic idle transitions cannot be enabled "
			 "on clockdomain %s\n", clkdm->name);
		return;
	}

	pr_debug("clockdomain: enabling automatic idle transitions for %s\n",
		 clkdm->name);

	/*
	 * XXX This should be removed once TI adds wakeup/sleep
	 * dependency code and data for OMAP4.
	 */
	if (cpu_is_omap44xx()) {
		WARN_ONCE(1, "clockdomain: OMAP4 wakeup/sleep dependency "
			  "support is not yet implemented\n");
	} else {
		if (atomic_read(&clkdm->usecount) > 0)
			_clkdm_add_autodeps(clkdm);
	}

	_omap2_clkdm_set_hwsup(clkdm, 1);

	pwrdm_clkdm_state_switch(clkdm);
}

/**
 * omap2_clkdm_deny_idle - disable hwsup idle transitions for clkdm
 * @clkdm: struct clockdomain *
 *
 * Prevent the hardware from automatically switching the clockdomain
 * @clkdm into inactive or idle states.  If the clockdomain has
 * downstream clocks enabled in the clock framework, wkdep/sleepdep
 * autodependencies are removed.  No return value.
 */
void omap2_clkdm_deny_idle(struct clockdomain *clkdm)
{
	if (!clkdm)
		return;

	if (!(clkdm->flags & CLKDM_CAN_DISABLE_AUTO)) {
		pr_debug("clockdomain: automatic idle transitions cannot be "
			 "disabled on %s\n", clkdm->name);
		return;
	}

	pr_debug("clockdomain: disabling automatic idle transitions for %s\n",
		 clkdm->name);

	_omap2_clkdm_set_hwsup(clkdm, 0);

	/*
	 * XXX This should be removed once TI adds wakeup/sleep
	 * dependency code and data for OMAP4.
	 */
	if (cpu_is_omap44xx()) {
		WARN_ONCE(1, "clockdomain: OMAP4 wakeup/sleep dependency "
			  "support is not yet implemented\n");
	} else {
		if (atomic_read(&clkdm->usecount) > 0)
			_clkdm_del_autodeps(clkdm);
	}
}


/* Clockdomain-to-clock framework interface code */

/**
 * omap2_clkdm_clk_enable - add an enabled downstream clock to this clkdm
 * @clkdm: struct clockdomain *
 * @clk: struct clk * of the enabled downstream clock
 *
 * Increment the usecount of the clockdomain @clkdm and ensure that it
 * is awake before @clk is enabled.  Intended to be called by
 * clk_enable() code.  If the clockdomain is in software-supervised
 * idle mode, force the clockdomain to wake.  If the clockdomain is in
 * hardware-supervised idle mode, add clkdm-pwrdm autodependencies, to
 * ensure that devices in the clockdomain can be read from/written to
 * by on-chip processors.  Returns -EINVAL if passed null pointers;
 * returns 0 upon success or if the clockdomain is in hwsup idle mode.
 */
int omap2_clkdm_clk_enable(struct clockdomain *clkdm, struct clk *clk)
{
	int v;

	/*
	 * XXX Rewrite this code to maintain a list of enabled
	 * downstream clocks for debugging purposes?
	 */

	if (!clkdm || !clk)
		return -EINVAL;

	if (atomic_inc_return(&clkdm->usecount) > 1)
		return 0;

	/* Clockdomain now has one enabled downstream clock */

	pr_debug("clockdomain: clkdm %s: clk %s now enabled\n", clkdm->name,
		 clk->name);

	if (!clkdm->clkstctrl_reg)
		return 0;

	v = omap2_clkdm_clktrctrl_read(clkdm);

	if ((cpu_is_omap34xx() && v == OMAP34XX_CLKSTCTRL_ENABLE_AUTO) ||
	    (cpu_is_omap24xx() && v == OMAP24XX_CLKSTCTRL_ENABLE_AUTO)) {
		/* Disable HW transitions when we are changing deps */
		_omap2_clkdm_set_hwsup(clkdm, 0);
		_clkdm_add_autodeps(clkdm);
		_omap2_clkdm_set_hwsup(clkdm, 1);
	} else {
		omap2_clkdm_wakeup(clkdm);
	}

	pwrdm_wait_transition(clkdm->pwrdm.ptr);
	pwrdm_clkdm_state_switch(clkdm);

	return 0;
}

/**
 * omap2_clkdm_clk_disable - remove an enabled downstream clock from this clkdm
 * @clkdm: struct clockdomain *
 * @clk: struct clk * of the disabled downstream clock
 *
 * Decrement the usecount of this clockdomain @clkdm when @clk is
 * disabled.  Intended to be called by clk_disable() code.  If the
 * clockdomain usecount goes to 0, put the clockdomain to sleep
 * (software-supervised mode) or remove the clkdm autodependencies
 * (hardware-supervised mode).  Returns -EINVAL if passed null
 * pointers; -ERANGE if the @clkdm usecount underflows and debugging
 * is enabled; or returns 0 upon success or if the clockdomain is in
 * hwsup idle mode.
 */
int omap2_clkdm_clk_disable(struct clockdomain *clkdm, struct clk *clk)
{
	int v;

	/*
	 * XXX Rewrite this code to maintain a list of enabled
	 * downstream clocks for debugging purposes?
	 */

	if (!clkdm || !clk)
		return -EINVAL;

#ifdef DEBUG
	if (atomic_read(&clkdm->usecount) == 0) {
		WARN_ON(1); /* underflow */
		return -ERANGE;
	}
#endif

	if (atomic_dec_return(&clkdm->usecount) > 0)
		return 0;

	/* All downstream clocks of this clockdomain are now disabled */

	pr_debug("clockdomain: clkdm %s: clk %s now disabled\n", clkdm->name,
		 clk->name);

	if (!clkdm->clkstctrl_reg)
		return 0;

	v = omap2_clkdm_clktrctrl_read(clkdm);

	if ((cpu_is_omap34xx() && v == OMAP34XX_CLKSTCTRL_ENABLE_AUTO) ||
	    (cpu_is_omap24xx() && v == OMAP24XX_CLKSTCTRL_ENABLE_AUTO)) {
		/* Disable HW transitions when we are changing deps */
		_omap2_clkdm_set_hwsup(clkdm, 0);
		_clkdm_del_autodeps(clkdm);
		_omap2_clkdm_set_hwsup(clkdm, 1);
	} else {
		omap2_clkdm_sleep(clkdm);
	}

	pwrdm_clkdm_state_switch(clkdm);

	return 0;
}

