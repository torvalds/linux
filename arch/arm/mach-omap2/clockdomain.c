/*
 * OMAP2/3/4 clockdomain framework functions
 *
 * Copyright (C) 2008-2011 Texas Instruments, Inc.
 * Copyright (C) 2008-2011 Nokia Corporation
 *
 * Written by Paul Walmsley and Jouni Högander
 * Added OMAP4 specific support by Abhijit Pagare <abhijitpagare@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#undef DEBUG

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/limits.h>
#include <linux/err.h>
#include <linux/clk-provider.h>

#include <linux/io.h>

#include <linux/bitops.h>

#include "soc.h"
#include "clock.h"
#include "clockdomain.h"

/* clkdm_list contains all registered struct clockdomains */
static LIST_HEAD(clkdm_list);

/* array of clockdomain deps to be added/removed when clkdm in hwsup mode */
static struct clkdm_autodep *autodeps;

static struct clkdm_ops *arch_clkdm;

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

	spin_lock_init(&clkdm->lock);

	pr_debug("clockdomain: registered %s\n", clkdm->name);

	return 0;
}

/* _clkdm_deps_lookup - look up the specified clockdomain in a clkdm list */
static struct clkdm_dep *_clkdm_deps_lookup(struct clockdomain *clkdm,
					    struct clkdm_dep *deps)
{
	struct clkdm_dep *cd;

	if (!clkdm || !deps)
		return ERR_PTR(-EINVAL);

	for (cd = deps; cd->clkdm_name; cd++) {
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
 *
 * XXX autodeps are deprecated and should be removed at the earliest
 * opportunity
 */
static void _autodep_lookup(struct clkdm_autodep *autodep)
{
	struct clockdomain *clkdm;

	if (!autodep)
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
 *
 * XXX autodeps are deprecated and should be removed at the earliest
 * opportunity
 */
void _clkdm_add_autodeps(struct clockdomain *clkdm)
{
	struct clkdm_autodep *autodep;

	if (!autodeps || clkdm->flags & CLKDM_NO_AUTODEPS)
		return;

	for (autodep = autodeps; autodep->clkdm.ptr; autodep++) {
		if (IS_ERR(autodep->clkdm.ptr))
			continue;

		pr_debug("clockdomain: %s: adding %s sleepdep/wkdep\n",
			 clkdm->name, autodep->clkdm.ptr->name);

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
 *
 * XXX autodeps are deprecated and should be removed at the earliest
 * opportunity
 */
void _clkdm_del_autodeps(struct clockdomain *clkdm)
{
	struct clkdm_autodep *autodep;

	if (!autodeps || clkdm->flags & CLKDM_NO_AUTODEPS)
		return;

	for (autodep = autodeps; autodep->clkdm.ptr; autodep++) {
		if (IS_ERR(autodep->clkdm.ptr))
			continue;

		pr_debug("clockdomain: %s: removing %s sleepdep/wkdep\n",
			 clkdm->name, autodep->clkdm.ptr->name);

		clkdm_del_sleepdep(clkdm, autodep->clkdm.ptr);
		clkdm_del_wkdep(clkdm, autodep->clkdm.ptr);
	}
}

/**
 * _resolve_clkdm_deps() - resolve clkdm_names in @clkdm_deps to clkdms
 * @clkdm: clockdomain that we are resolving dependencies for
 * @clkdm_deps: ptr to array of struct clkdm_deps to resolve
 *
 * Iterates through @clkdm_deps, looking up the struct clockdomain named by
 * clkdm_name and storing the clockdomain pointer in the struct clkdm_dep.
 * No return value.
 */
static void _resolve_clkdm_deps(struct clockdomain *clkdm,
				struct clkdm_dep *clkdm_deps)
{
	struct clkdm_dep *cd;

	for (cd = clkdm_deps; cd && cd->clkdm_name; cd++) {
		if (cd->clkdm)
			continue;
		cd->clkdm = _clkdm_lookup(cd->clkdm_name);

		WARN(!cd->clkdm, "clockdomain: %s: could not find clkdm %s while resolving dependencies - should never happen",
		     clkdm->name, cd->clkdm_name);
	}
}

/* Public functions */

/**
 * clkdm_register_platform_funcs - register clockdomain implementation fns
 * @co: func pointers for arch specific implementations
 *
 * Register the list of function pointers used to implement the
 * clockdomain functions on different OMAP SoCs.  Should be called
 * before any other clkdm_register*() function.  Returns -EINVAL if
 * @co is null, -EEXIST if platform functions have already been
 * registered, or 0 upon success.
 */
int clkdm_register_platform_funcs(struct clkdm_ops *co)
{
	if (!co)
		return -EINVAL;

	if (arch_clkdm)
		return -EEXIST;

	arch_clkdm = co;

	return 0;
};

/**
 * clkdm_register_clkdms - register SoC clockdomains
 * @cs: pointer to an array of struct clockdomain to register
 *
 * Register the clockdomains available on a particular OMAP SoC.  Must
 * be called after clkdm_register_platform_funcs().  May be called
 * multiple times.  Returns -EACCES if called before
 * clkdm_register_platform_funcs(); -EINVAL if the argument @cs is
 * null; or 0 upon success.
 */
int clkdm_register_clkdms(struct clockdomain **cs)
{
	struct clockdomain **c = NULL;

	if (!arch_clkdm)
		return -EACCES;

	if (!cs)
		return -EINVAL;

	for (c = cs; *c; c++)
		_clkdm_register(*c);

	return 0;
}

/**
 * clkdm_register_autodeps - register autodeps (if required)
 * @ia: pointer to a static array of struct clkdm_autodep to register
 *
 * Register clockdomain "automatic dependencies."  These are
 * clockdomain wakeup and sleep dependencies that are automatically
 * added whenever the first clock inside a clockdomain is enabled, and
 * removed whenever the last clock inside a clockdomain is disabled.
 * These are currently only used on OMAP3 devices, and are deprecated,
 * since they waste energy.  However, until the OMAP2/3 IP block
 * enable/disable sequence can be converted to match the OMAP4
 * sequence, they are needed.
 *
 * Must be called only after all of the SoC clockdomains are
 * registered, since the function will resolve autodep clockdomain
 * names into clockdomain pointers.
 *
 * The struct clkdm_autodep @ia array must be static, as this function
 * does not copy the array elements.
 *
 * Returns -EACCES if called before any clockdomains have been
 * registered, -EINVAL if called with a null @ia argument, -EEXIST if
 * autodeps have already been registered, or 0 upon success.
 */
int clkdm_register_autodeps(struct clkdm_autodep *ia)
{
	struct clkdm_autodep *a = NULL;

	if (list_empty(&clkdm_list))
		return -EACCES;

	if (!ia)
		return -EINVAL;

	if (autodeps)
		return -EEXIST;

	autodeps = ia;
	for (a = autodeps; a->clkdm.ptr; a++)
		_autodep_lookup(a);

	return 0;
}

/**
 * clkdm_complete_init - set up the clockdomain layer
 *
 * Put all clockdomains into software-supervised mode; PM code should
 * later enable hardware-supervised mode as appropriate.  Must be
 * called after clkdm_register_clkdms().  Returns -EACCES if called
 * before clkdm_register_clkdms(), or 0 upon success.
 */
int clkdm_complete_init(void)
{
	struct clockdomain *clkdm;

	if (list_empty(&clkdm_list))
		return -EACCES;

	list_for_each_entry(clkdm, &clkdm_list, node) {
		if (clkdm->flags & CLKDM_CAN_FORCE_WAKEUP)
			clkdm_wakeup(clkdm);
		else if (clkdm->flags & CLKDM_CAN_DISABLE_AUTO)
			clkdm_deny_idle(clkdm);

		_resolve_clkdm_deps(clkdm, clkdm->wkdep_srcs);
		clkdm_clear_all_wkdeps(clkdm);

		_resolve_clkdm_deps(clkdm, clkdm->sleepdep_srcs);
		clkdm_clear_all_sleepdeps(clkdm);
	}

	return 0;
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
	int ret = 0;

	if (!clkdm1 || !clkdm2)
		return -EINVAL;

	cd = _clkdm_deps_lookup(clkdm2, clkdm1->wkdep_srcs);
	if (IS_ERR(cd))
		ret = PTR_ERR(cd);

	if (!arch_clkdm || !arch_clkdm->clkdm_add_wkdep)
		ret = -EINVAL;

	if (ret) {
		pr_debug("clockdomain: hardware cannot set/clear wake up of %s when %s wakes up\n",
			 clkdm1->name, clkdm2->name);
		return ret;
	}

	if (atomic_inc_return(&cd->wkdep_usecount) == 1) {
		pr_debug("clockdomain: hardware will wake up %s when %s wakes up\n",
			 clkdm1->name, clkdm2->name);

		ret = arch_clkdm->clkdm_add_wkdep(clkdm1, clkdm2);
	}

	return ret;
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
	int ret = 0;

	if (!clkdm1 || !clkdm2)
		return -EINVAL;

	cd = _clkdm_deps_lookup(clkdm2, clkdm1->wkdep_srcs);
	if (IS_ERR(cd))
		ret = PTR_ERR(cd);

	if (!arch_clkdm || !arch_clkdm->clkdm_del_wkdep)
		ret = -EINVAL;

	if (ret) {
		pr_debug("clockdomain: hardware cannot set/clear wake up of %s when %s wakes up\n",
			 clkdm1->name, clkdm2->name);
		return ret;
	}

	if (atomic_dec_return(&cd->wkdep_usecount) == 0) {
		pr_debug("clockdomain: hardware will no longer wake up %s after %s wakes up\n",
			 clkdm1->name, clkdm2->name);

		ret = arch_clkdm->clkdm_del_wkdep(clkdm1, clkdm2);
	}

	return ret;
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
	int ret = 0;

	if (!clkdm1 || !clkdm2)
		return -EINVAL;

	cd = _clkdm_deps_lookup(clkdm2, clkdm1->wkdep_srcs);
	if (IS_ERR(cd))
		ret = PTR_ERR(cd);

	if (!arch_clkdm || !arch_clkdm->clkdm_read_wkdep)
		ret = -EINVAL;

	if (ret) {
		pr_debug("clockdomain: hardware cannot set/clear wake up of %s when %s wakes up\n",
			 clkdm1->name, clkdm2->name);
		return ret;
	}

	/* XXX It's faster to return the atomic wkdep_usecount */
	return arch_clkdm->clkdm_read_wkdep(clkdm1, clkdm2);
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
	if (!clkdm)
		return -EINVAL;

	if (!arch_clkdm || !arch_clkdm->clkdm_clear_all_wkdeps)
		return -EINVAL;

	return arch_clkdm->clkdm_clear_all_wkdeps(clkdm);
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
	int ret = 0;

	if (!clkdm1 || !clkdm2)
		return -EINVAL;

	cd = _clkdm_deps_lookup(clkdm2, clkdm1->sleepdep_srcs);
	if (IS_ERR(cd))
		ret = PTR_ERR(cd);

	if (!arch_clkdm || !arch_clkdm->clkdm_add_sleepdep)
		ret = -EINVAL;

	if (ret) {
		pr_debug("clockdomain: hardware cannot set/clear sleep dependency affecting %s from %s\n",
			 clkdm1->name, clkdm2->name);
		return ret;
	}

	if (atomic_inc_return(&cd->sleepdep_usecount) == 1) {
		pr_debug("clockdomain: will prevent %s from sleeping if %s is active\n",
			 clkdm1->name, clkdm2->name);

		ret = arch_clkdm->clkdm_add_sleepdep(clkdm1, clkdm2);
	}

	return ret;
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
	int ret = 0;

	if (!clkdm1 || !clkdm2)
		return -EINVAL;

	cd = _clkdm_deps_lookup(clkdm2, clkdm1->sleepdep_srcs);
	if (IS_ERR(cd))
		ret = PTR_ERR(cd);

	if (!arch_clkdm || !arch_clkdm->clkdm_del_sleepdep)
		ret = -EINVAL;

	if (ret) {
		pr_debug("clockdomain: hardware cannot set/clear sleep dependency affecting %s from %s\n",
			 clkdm1->name, clkdm2->name);
		return ret;
	}

	if (atomic_dec_return(&cd->sleepdep_usecount) == 0) {
		pr_debug("clockdomain: will no longer prevent %s from sleeping if %s is active\n",
			 clkdm1->name, clkdm2->name);

		ret = arch_clkdm->clkdm_del_sleepdep(clkdm1, clkdm2);
	}

	return ret;
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
	int ret = 0;

	if (!clkdm1 || !clkdm2)
		return -EINVAL;

	cd = _clkdm_deps_lookup(clkdm2, clkdm1->sleepdep_srcs);
	if (IS_ERR(cd))
		ret = PTR_ERR(cd);

	if (!arch_clkdm || !arch_clkdm->clkdm_read_sleepdep)
		ret = -EINVAL;

	if (ret) {
		pr_debug("clockdomain: hardware cannot set/clear sleep dependency affecting %s from %s\n",
			 clkdm1->name, clkdm2->name);
		return ret;
	}

	/* XXX It's faster to return the atomic sleepdep_usecount */
	return arch_clkdm->clkdm_read_sleepdep(clkdm1, clkdm2);
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
	if (!clkdm)
		return -EINVAL;

	if (!arch_clkdm || !arch_clkdm->clkdm_clear_all_sleepdeps)
		return -EINVAL;

	return arch_clkdm->clkdm_clear_all_sleepdeps(clkdm);
}

/**
 * clkdm_sleep - force clockdomain sleep transition
 * @clkdm: struct clockdomain *
 *
 * Instruct the CM to force a sleep transition on the specified
 * clockdomain @clkdm.  Returns -EINVAL if @clkdm is NULL or if
 * clockdomain does not support software-initiated sleep; 0 upon
 * success.
 */
int clkdm_sleep(struct clockdomain *clkdm)
{
	int ret;
	unsigned long flags;

	if (!clkdm)
		return -EINVAL;

	if (!(clkdm->flags & CLKDM_CAN_FORCE_SLEEP)) {
		pr_debug("clockdomain: %s does not support forcing sleep via software\n",
			 clkdm->name);
		return -EINVAL;
	}

	if (!arch_clkdm || !arch_clkdm->clkdm_sleep)
		return -EINVAL;

	pr_debug("clockdomain: forcing sleep on %s\n", clkdm->name);

	spin_lock_irqsave(&clkdm->lock, flags);
	clkdm->_flags &= ~_CLKDM_FLAG_HWSUP_ENABLED;
	ret = arch_clkdm->clkdm_sleep(clkdm);
	spin_unlock_irqrestore(&clkdm->lock, flags);
	return ret;
}

/**
 * clkdm_wakeup - force clockdomain wakeup transition
 * @clkdm: struct clockdomain *
 *
 * Instruct the CM to force a wakeup transition on the specified
 * clockdomain @clkdm.  Returns -EINVAL if @clkdm is NULL or if the
 * clockdomain does not support software-controlled wakeup; 0 upon
 * success.
 */
int clkdm_wakeup(struct clockdomain *clkdm)
{
	int ret;
	unsigned long flags;

	if (!clkdm)
		return -EINVAL;

	if (!(clkdm->flags & CLKDM_CAN_FORCE_WAKEUP)) {
		pr_debug("clockdomain: %s does not support forcing wakeup via software\n",
			 clkdm->name);
		return -EINVAL;
	}

	if (!arch_clkdm || !arch_clkdm->clkdm_wakeup)
		return -EINVAL;

	pr_debug("clockdomain: forcing wakeup on %s\n", clkdm->name);

	spin_lock_irqsave(&clkdm->lock, flags);
	clkdm->_flags &= ~_CLKDM_FLAG_HWSUP_ENABLED;
	ret = arch_clkdm->clkdm_wakeup(clkdm);
	ret |= pwrdm_state_switch(clkdm->pwrdm.ptr);
	spin_unlock_irqrestore(&clkdm->lock, flags);
	return ret;
}

/**
 * clkdm_allow_idle - enable hwsup idle transitions for clkdm
 * @clkdm: struct clockdomain *
 *
 * Allow the hardware to automatically switch the clockdomain @clkdm into
 * active or idle states, as needed by downstream clocks.  If the
 * clockdomain has any downstream clocks enabled in the clock
 * framework, wkdep/sleepdep autodependencies are added; this is so
 * device drivers can read and write to the device.  No return value.
 */
void clkdm_allow_idle(struct clockdomain *clkdm)
{
	unsigned long flags;

	if (!clkdm)
		return;

	if (!(clkdm->flags & CLKDM_CAN_ENABLE_AUTO)) {
		pr_debug("clock: %s: automatic idle transitions cannot be enabled\n",
			 clkdm->name);
		return;
	}

	if (!arch_clkdm || !arch_clkdm->clkdm_allow_idle)
		return;

	pr_debug("clockdomain: enabling automatic idle transitions for %s\n",
		 clkdm->name);

	spin_lock_irqsave(&clkdm->lock, flags);
	clkdm->_flags |= _CLKDM_FLAG_HWSUP_ENABLED;
	arch_clkdm->clkdm_allow_idle(clkdm);
	pwrdm_state_switch(clkdm->pwrdm.ptr);
	spin_unlock_irqrestore(&clkdm->lock, flags);
}

/**
 * clkdm_deny_idle - disable hwsup idle transitions for clkdm
 * @clkdm: struct clockdomain *
 *
 * Prevent the hardware from automatically switching the clockdomain
 * @clkdm into inactive or idle states.  If the clockdomain has
 * downstream clocks enabled in the clock framework, wkdep/sleepdep
 * autodependencies are removed.  No return value.
 */
void clkdm_deny_idle(struct clockdomain *clkdm)
{
	unsigned long flags;

	if (!clkdm)
		return;

	if (!(clkdm->flags & CLKDM_CAN_DISABLE_AUTO)) {
		pr_debug("clockdomain: %s: automatic idle transitions cannot be disabled\n",
			 clkdm->name);
		return;
	}

	if (!arch_clkdm || !arch_clkdm->clkdm_deny_idle)
		return;

	pr_debug("clockdomain: disabling automatic idle transitions for %s\n",
		 clkdm->name);

	spin_lock_irqsave(&clkdm->lock, flags);
	clkdm->_flags &= ~_CLKDM_FLAG_HWSUP_ENABLED;
	arch_clkdm->clkdm_deny_idle(clkdm);
	pwrdm_state_switch(clkdm->pwrdm.ptr);
	spin_unlock_irqrestore(&clkdm->lock, flags);
}

/**
 * clkdm_in_hwsup - is clockdomain @clkdm have hardware-supervised idle enabled?
 * @clkdm: struct clockdomain *
 *
 * Returns true if clockdomain @clkdm currently has
 * hardware-supervised idle enabled, or false if it does not or if
 * @clkdm is NULL.  It is only valid to call this function after
 * clkdm_init() has been called.  This function does not actually read
 * bits from the hardware; it instead tests an in-memory flag that is
 * changed whenever the clockdomain code changes the auto-idle mode.
 */
bool clkdm_in_hwsup(struct clockdomain *clkdm)
{
	bool ret;
	unsigned long flags;

	if (!clkdm)
		return false;

	spin_lock_irqsave(&clkdm->lock, flags);
	ret = (clkdm->_flags & _CLKDM_FLAG_HWSUP_ENABLED) ? true : false;
	spin_unlock_irqrestore(&clkdm->lock, flags);

	return ret;
}

/**
 * clkdm_missing_idle_reporting - can @clkdm enter autoidle even if in use?
 * @clkdm: struct clockdomain *
 *
 * Returns true if clockdomain @clkdm has the
 * CLKDM_MISSING_IDLE_REPORTING flag set, or false if not or @clkdm is
 * null.  More information is available in the documentation for the
 * CLKDM_MISSING_IDLE_REPORTING macro.
 */
bool clkdm_missing_idle_reporting(struct clockdomain *clkdm)
{
	if (!clkdm)
		return false;

	return (clkdm->flags & CLKDM_MISSING_IDLE_REPORTING) ? true : false;
}

/* Clockdomain-to-clock/hwmod framework interface code */

static int _clkdm_clk_hwmod_enable(struct clockdomain *clkdm)
{
	unsigned long flags;

	if (!clkdm || !arch_clkdm || !arch_clkdm->clkdm_clk_enable)
		return -EINVAL;

	spin_lock_irqsave(&clkdm->lock, flags);

	/*
	 * For arch's with no autodeps, clkcm_clk_enable
	 * should be called for every clock instance or hwmod that is
	 * enabled, so the clkdm can be force woken up.
	 */
	if ((atomic_inc_return(&clkdm->usecount) > 1) && autodeps) {
		spin_unlock_irqrestore(&clkdm->lock, flags);
		return 0;
	}

	arch_clkdm->clkdm_clk_enable(clkdm);
	pwrdm_state_switch(clkdm->pwrdm.ptr);
	spin_unlock_irqrestore(&clkdm->lock, flags);

	pr_debug("clockdomain: %s: enabled\n", clkdm->name);

	return 0;
}

/**
 * clkdm_clk_enable - add an enabled downstream clock to this clkdm
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
int clkdm_clk_enable(struct clockdomain *clkdm, struct clk *clk)
{
	/*
	 * XXX Rewrite this code to maintain a list of enabled
	 * downstream clocks for debugging purposes?
	 */

	if (!clk)
		return -EINVAL;

	return _clkdm_clk_hwmod_enable(clkdm);
}

/**
 * clkdm_clk_disable - remove an enabled downstream clock from this clkdm
 * @clkdm: struct clockdomain *
 * @clk: struct clk * of the disabled downstream clock
 *
 * Decrement the usecount of this clockdomain @clkdm when @clk is
 * disabled.  Intended to be called by clk_disable() code.  If the
 * clockdomain usecount goes to 0, put the clockdomain to sleep
 * (software-supervised mode) or remove the clkdm autodependencies
 * (hardware-supervised mode).  Returns -EINVAL if passed null
 * pointers; -ERANGE if the @clkdm usecount underflows; or returns 0
 * upon success or if the clockdomain is in hwsup idle mode.
 */
int clkdm_clk_disable(struct clockdomain *clkdm, struct clk *clk)
{
	unsigned long flags;

	if (!clkdm || !clk || !arch_clkdm || !arch_clkdm->clkdm_clk_disable)
		return -EINVAL;

	spin_lock_irqsave(&clkdm->lock, flags);

#ifdef CONFIG_COMMON_CLK
	/* corner case: disabling unused clocks */
	if (__clk_get_enable_count(clk) == 0)
		goto ccd_exit;
#endif

	if (atomic_read(&clkdm->usecount) == 0) {
		spin_unlock_irqrestore(&clkdm->lock, flags);
		WARN_ON(1); /* underflow */
		return -ERANGE;
	}

	if (atomic_dec_return(&clkdm->usecount) > 0) {
		spin_unlock_irqrestore(&clkdm->lock, flags);
		return 0;
	}

	arch_clkdm->clkdm_clk_disable(clkdm);
	pwrdm_state_switch(clkdm->pwrdm.ptr);

	pr_debug("clockdomain: %s: disabled\n", clkdm->name);

#ifdef CONFIG_COMMON_CLK
ccd_exit:
#endif
	spin_unlock_irqrestore(&clkdm->lock, flags);

	return 0;
}

/**
 * clkdm_hwmod_enable - add an enabled downstream hwmod to this clkdm
 * @clkdm: struct clockdomain *
 * @oh: struct omap_hwmod * of the enabled downstream hwmod
 *
 * Increment the usecount of the clockdomain @clkdm and ensure that it
 * is awake before @oh is enabled. Intended to be called by
 * module_enable() code.
 * If the clockdomain is in software-supervised idle mode, force the
 * clockdomain to wake.  If the clockdomain is in hardware-supervised idle
 * mode, add clkdm-pwrdm autodependencies, to ensure that devices in the
 * clockdomain can be read from/written to by on-chip processors.
 * Returns -EINVAL if passed null pointers;
 * returns 0 upon success or if the clockdomain is in hwsup idle mode.
 */
int clkdm_hwmod_enable(struct clockdomain *clkdm, struct omap_hwmod *oh)
{
	/* The clkdm attribute does not exist yet prior OMAP4 */
	if (cpu_is_omap24xx() || cpu_is_omap34xx())
		return 0;

	/*
	 * XXX Rewrite this code to maintain a list of enabled
	 * downstream hwmods for debugging purposes?
	 */

	if (!oh)
		return -EINVAL;

	return _clkdm_clk_hwmod_enable(clkdm);
}

/**
 * clkdm_hwmod_disable - remove an enabled downstream hwmod from this clkdm
 * @clkdm: struct clockdomain *
 * @oh: struct omap_hwmod * of the disabled downstream hwmod
 *
 * Decrement the usecount of this clockdomain @clkdm when @oh is
 * disabled. Intended to be called by module_disable() code.
 * If the clockdomain usecount goes to 0, put the clockdomain to sleep
 * (software-supervised mode) or remove the clkdm autodependencies
 * (hardware-supervised mode).
 * Returns -EINVAL if passed null pointers; -ERANGE if the @clkdm usecount
 * underflows; or returns 0 upon success or if the clockdomain is in hwsup
 * idle mode.
 */
int clkdm_hwmod_disable(struct clockdomain *clkdm, struct omap_hwmod *oh)
{
	unsigned long flags;

	/* The clkdm attribute does not exist yet prior OMAP4 */
	if (cpu_is_omap24xx() || cpu_is_omap34xx())
		return 0;

	/*
	 * XXX Rewrite this code to maintain a list of enabled
	 * downstream hwmods for debugging purposes?
	 */

	if (!clkdm || !oh || !arch_clkdm || !arch_clkdm->clkdm_clk_disable)
		return -EINVAL;

	spin_lock_irqsave(&clkdm->lock, flags);

	if (atomic_read(&clkdm->usecount) == 0) {
		spin_unlock_irqrestore(&clkdm->lock, flags);
		WARN_ON(1); /* underflow */
		return -ERANGE;
	}

	if (atomic_dec_return(&clkdm->usecount) > 0) {
		spin_unlock_irqrestore(&clkdm->lock, flags);
		return 0;
	}

	arch_clkdm->clkdm_clk_disable(clkdm);
	pwrdm_state_switch(clkdm->pwrdm.ptr);
	spin_unlock_irqrestore(&clkdm->lock, flags);

	pr_debug("clockdomain: %s: disabled\n", clkdm->name);

	return 0;
}

