/*
 * OMAP2/3 clockdomain framework functions
 *
 * Copyright (C) 2008 Texas Instruments, Inc.
 * Copyright (C) 2008 Nokia Corporation
 *
 * Written by Paul Walmsley and Jouni Högander
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifdef CONFIG_OMAP_DEBUG_CLOCKDOMAIN
#  define DEBUG
#endif

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

#include <mach/clock.h>

#include "prm.h"
#include "prm-regbits-24xx.h"
#include "cm.h"

#include <mach/powerdomain.h>
#include <mach/clockdomain.h>

/* clkdm_list contains all registered struct clockdomains */
static LIST_HEAD(clkdm_list);

/* clkdm_mutex protects clkdm_list add and del ops */
static DEFINE_MUTEX(clkdm_mutex);

/* array of powerdomain deps to be added/removed when clkdm in hwsup mode */
static struct clkdm_pwrdm_autodep *autodeps;


/* Private functions */

/*
 * _autodep_lookup - resolve autodep pwrdm names to pwrdm pointers; store
 * @autodep: struct clkdm_pwrdm_autodep * to resolve
 *
 * Resolve autodep powerdomain names to powerdomain pointers via
 * pwrdm_lookup() and store the pointers in the autodep structure.  An
 * "autodep" is a powerdomain sleep/wakeup dependency that is
 * automatically added and removed whenever clocks in the associated
 * clockdomain are enabled or disabled (respectively) when the
 * clockdomain is in hardware-supervised mode.	Meant to be called
 * once at clockdomain layer initialization, since these should remain
 * fixed for a particular architecture.  No return value.
 */
static void _autodep_lookup(struct clkdm_pwrdm_autodep *autodep)
{
	struct powerdomain *pwrdm;

	if (!autodep)
		return;

	if (!omap_chip_is(autodep->omap_chip))
		return;

	pwrdm = pwrdm_lookup(autodep->pwrdm.name);
	if (!pwrdm) {
		pr_err("clockdomain: autodeps: powerdomain %s does not exist\n",
			 autodep->pwrdm.name);
		pwrdm = ERR_PTR(-ENOENT);
	}
	autodep->pwrdm.ptr = pwrdm;
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
	struct clkdm_pwrdm_autodep *autodep;

	for (autodep = autodeps; autodep->pwrdm.ptr; autodep++) {
		if (IS_ERR(autodep->pwrdm.ptr))
			continue;

		if (!omap_chip_is(autodep->omap_chip))
			continue;

		pr_debug("clockdomain: adding %s sleepdep/wkdep for "
			 "pwrdm %s\n", autodep->pwrdm.ptr->name,
			 clkdm->pwrdm.ptr->name);

		pwrdm_add_sleepdep(clkdm->pwrdm.ptr, autodep->pwrdm.ptr);
		pwrdm_add_wkdep(clkdm->pwrdm.ptr, autodep->pwrdm.ptr);
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
	struct clkdm_pwrdm_autodep *autodep;

	for (autodep = autodeps; autodep->pwrdm.ptr; autodep++) {
		if (IS_ERR(autodep->pwrdm.ptr))
			continue;

		if (!omap_chip_is(autodep->omap_chip))
			continue;

		pr_debug("clockdomain: removing %s sleepdep/wkdep for "
			 "pwrdm %s\n", autodep->pwrdm.ptr->name,
			 clkdm->pwrdm.ptr->name);

		pwrdm_del_sleepdep(clkdm->pwrdm.ptr, autodep->pwrdm.ptr);
		pwrdm_del_wkdep(clkdm->pwrdm.ptr, autodep->pwrdm.ptr);
	}
}


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


/* Public functions */

/**
 * clkdm_init - set up the clockdomain layer
 * @clkdms: optional pointer to an array of clockdomains to register
 * @init_autodeps: optional pointer to an array of autodeps to register
 *
 * Set up internal state.  If a pointer to an array of clockdomains
 * was supplied, loop through the list of clockdomains, register all
 * that are available on the current platform.	Similarly, if a
 * pointer to an array of clockdomain-powerdomain autodependencies was
 * provided, register those.  No return value.
 */
void clkdm_init(struct clockdomain **clkdms,
		struct clkdm_pwrdm_autodep *init_autodeps)
{
	struct clockdomain **c = NULL;
	struct clkdm_pwrdm_autodep *autodep = NULL;

	if (clkdms)
		for (c = clkdms; *c; c++)
			clkdm_register(*c);

	autodeps = init_autodeps;
	if (autodeps)
		for (autodep = autodeps; autodep->pwrdm.ptr; autodep++)
			_autodep_lookup(autodep);
}

/**
 * clkdm_register - register a clockdomain
 * @clkdm: struct clockdomain * to register
 *
 * Adds a clockdomain to the internal clockdomain list.
 * Returns -EINVAL if given a null pointer, -EEXIST if a clockdomain is
 * already registered by the provided name, or 0 upon success.
 */
int clkdm_register(struct clockdomain *clkdm)
{
	int ret = -EINVAL;
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

	mutex_lock(&clkdm_mutex);
	/* Verify that the clockdomain is not already registered */
	if (_clkdm_lookup(clkdm->name)) {
		ret = -EEXIST;
		goto cr_unlock;
	}

	list_add(&clkdm->node, &clkdm_list);

	pwrdm_add_clkdm(pwrdm, clkdm);

	pr_debug("clockdomain: registered %s\n", clkdm->name);
	ret = 0;

cr_unlock:
	mutex_unlock(&clkdm_mutex);

	return ret;
}

/**
 * clkdm_unregister - unregister a clockdomain
 * @clkdm: struct clockdomain * to unregister
 *
 * Removes a clockdomain from the internal clockdomain list.  Returns
 * -EINVAL if clkdm argument is NULL.
 */
int clkdm_unregister(struct clockdomain *clkdm)
{
	if (!clkdm)
		return -EINVAL;

	pwrdm_del_clkdm(clkdm->pwrdm.ptr, clkdm);

	mutex_lock(&clkdm_mutex);
	list_del(&clkdm->node);
	mutex_unlock(&clkdm_mutex);

	pr_debug("clockdomain: unregistered %s\n", clkdm->name);

	return 0;
}

/**
 * clkdm_lookup - look up a clockdomain by name, return a pointer
 * @name: name of clockdomain
 *
 * Find a registered clockdomain by its name.  Returns a pointer to the
 * struct clockdomain if found, or NULL otherwise.
 */
struct clockdomain *clkdm_lookup(const char *name)
{
	struct clockdomain *clkdm, *temp_clkdm;

	if (!name)
		return NULL;

	clkdm = NULL;

	mutex_lock(&clkdm_mutex);
	list_for_each_entry(temp_clkdm, &clkdm_list, node) {
		if (!strcmp(name, temp_clkdm->name)) {
			clkdm = temp_clkdm;
			break;
		}
	}
	mutex_unlock(&clkdm_mutex);

	return clkdm;
}

/**
 * clkdm_for_each - call function on each registered clockdomain
 * @fn: callback function *
 *
 * Call the supplied function for each registered clockdomain.
 * The callback function can return anything but 0 to bail
 * out early from the iterator.  The callback function is called with
 * the clkdm_mutex held, so no clockdomain structure manipulation
 * functions should be called from the callback, although hardware
 * clockdomain control functions are fine.  Returns the last return
 * value of the callback function, which should be 0 for success or
 * anything else to indicate failure; or -EINVAL if the function pointer
 * is null.
 */
int clkdm_for_each(int (*fn)(struct clockdomain *clkdm))
{
	struct clockdomain *clkdm;
	int ret = 0;

	if (!fn)
		return -EINVAL;

	mutex_lock(&clkdm_mutex);
	list_for_each_entry(clkdm, &clkdm_list, node) {
		ret = (*fn)(clkdm);
		if (ret)
			break;
	}
	mutex_unlock(&clkdm_mutex);

	return ret;
}


/**
 * clkdm_get_pwrdm - return a ptr to the pwrdm that this clkdm resides in
 * @clkdm: struct clockdomain *
 *
 * Return a pointer to the struct powerdomain that the specified clockdomain
 * 'clkdm' exists in, or returns NULL if clkdm argument is NULL.
 */
struct powerdomain *clkdm_get_pwrdm(struct clockdomain *clkdm)
{
	if (!clkdm)
		return NULL;

	return clkdm->pwrdm.ptr;
}


/* Hardware clockdomain control */

/**
 * omap2_clkdm_clktrctrl_read - read the clkdm's current state transition mode
 * @clk: struct clk * of a clockdomain
 *
 * Return the clockdomain's current state transition mode from the
 * corresponding domain CM_CLKSTCTRL register.	Returns -EINVAL if clk
 * is NULL or the current mode upon success.
 */
static int omap2_clkdm_clktrctrl_read(struct clockdomain *clkdm)
{
	u32 v;

	if (!clkdm)
		return -EINVAL;

	v = cm_read_mod_reg(clkdm->pwrdm.ptr->prcm_offs, CM_CLKSTCTRL);
	v &= clkdm->clktrctrl_mask;
	v >>= __ffs(clkdm->clktrctrl_mask);

	return v;
}

/**
 * omap2_clkdm_sleep - force clockdomain sleep transition
 * @clkdm: struct clockdomain *
 *
 * Instruct the CM to force a sleep transition on the specified
 * clockdomain 'clkdm'.  Returns -EINVAL if clk is NULL or if
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

		cm_set_mod_reg_bits(OMAP24XX_FORCESTATE,
				    clkdm->pwrdm.ptr->prcm_offs, PM_PWSTCTRL);

	} else if (cpu_is_omap34xx()) {

		u32 v = (OMAP34XX_CLKSTCTRL_FORCE_SLEEP <<
			 __ffs(clkdm->clktrctrl_mask));

		cm_rmw_mod_reg_bits(clkdm->clktrctrl_mask, v,
				    clkdm->pwrdm.ptr->prcm_offs, CM_CLKSTCTRL);

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
 * clockdomain 'clkdm'.  Returns -EINVAL if clkdm is NULL or if the
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

		cm_clear_mod_reg_bits(OMAP24XX_FORCESTATE,
				      clkdm->pwrdm.ptr->prcm_offs, PM_PWSTCTRL);

	} else if (cpu_is_omap34xx()) {

		u32 v = (OMAP34XX_CLKSTCTRL_FORCE_WAKEUP <<
			 __ffs(clkdm->clktrctrl_mask));

		cm_rmw_mod_reg_bits(clkdm->clktrctrl_mask, v,
				    clkdm->pwrdm.ptr->prcm_offs, CM_CLKSTCTRL);

	} else {
		BUG();
	};

	return 0;
}

/**
 * omap2_clkdm_allow_idle - enable hwsup idle transitions for clkdm
 * @clkdm: struct clockdomain *
 *
 * Allow the hardware to automatically switch the clockdomain into
 * active or idle states, as needed by downstream clocks.  If the
 * clockdomain has any downstream clocks enabled in the clock
 * framework, wkdep/sleepdep autodependencies are added; this is so
 * device drivers can read and write to the device.  No return value.
 */
void omap2_clkdm_allow_idle(struct clockdomain *clkdm)
{
	u32 v;

	if (!clkdm)
		return;

	if (!(clkdm->flags & CLKDM_CAN_ENABLE_AUTO)) {
		pr_debug("clock: automatic idle transitions cannot be enabled "
			 "on clockdomain %s\n", clkdm->name);
		return;
	}

	pr_debug("clockdomain: enabling automatic idle transitions for %s\n",
		 clkdm->name);

	if (atomic_read(&clkdm->usecount) > 0)
		_clkdm_add_autodeps(clkdm);

	if (cpu_is_omap24xx())
		v = OMAP24XX_CLKSTCTRL_ENABLE_AUTO;
	else if (cpu_is_omap34xx())
		v = OMAP34XX_CLKSTCTRL_ENABLE_AUTO;
	else
		BUG();


	cm_rmw_mod_reg_bits(clkdm->clktrctrl_mask,
			    v << __ffs(clkdm->clktrctrl_mask),
			    clkdm->pwrdm.ptr->prcm_offs,
			    CM_CLKSTCTRL);
}

/**
 * omap2_clkdm_deny_idle - disable hwsup idle transitions for clkdm
 * @clkdm: struct clockdomain *
 *
 * Prevent the hardware from automatically switching the clockdomain
 * into inactive or idle states.  If the clockdomain has downstream
 * clocks enabled in the clock framework, wkdep/sleepdep
 * autodependencies are removed.  No return value.
 */
void omap2_clkdm_deny_idle(struct clockdomain *clkdm)
{
	u32 v;

	if (!clkdm)
		return;

	if (!(clkdm->flags & CLKDM_CAN_DISABLE_AUTO)) {
		pr_debug("clockdomain: automatic idle transitions cannot be "
			 "disabled on %s\n", clkdm->name);
		return;
	}

	pr_debug("clockdomain: disabling automatic idle transitions for %s\n",
		 clkdm->name);

	if (cpu_is_omap24xx())
		v = OMAP24XX_CLKSTCTRL_DISABLE_AUTO;
	else if (cpu_is_omap34xx())
		v = OMAP34XX_CLKSTCTRL_DISABLE_AUTO;
	else
		BUG();

	cm_rmw_mod_reg_bits(clkdm->clktrctrl_mask,
			    v << __ffs(clkdm->clktrctrl_mask),
			    clkdm->pwrdm.ptr->prcm_offs, CM_CLKSTCTRL);

	if (atomic_read(&clkdm->usecount) > 0)
		_clkdm_del_autodeps(clkdm);
}


/* Clockdomain-to-clock framework interface code */

/**
 * omap2_clkdm_clk_enable - add an enabled downstream clock to this clkdm
 * @clkdm: struct clockdomain *
 * @clk: struct clk * of the enabled downstream clock
 *
 * Increment the usecount of this clockdomain 'clkdm' and ensure that
 * it is awake.  Intended to be called by clk_enable() code.  If the
 * clockdomain is in software-supervised idle mode, force the
 * clockdomain to wake.  If the clockdomain is in hardware-supervised
 * idle mode, add clkdm-pwrdm autodependencies, to ensure that devices
 * in the clockdomain can be read from/written to by on-chip processors.
 * Returns -EINVAL if passed null pointers; returns 0 upon success or
 * if the clockdomain is in hwsup idle mode.
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

	v = omap2_clkdm_clktrctrl_read(clkdm);

	if ((cpu_is_omap34xx() && v == OMAP34XX_CLKSTCTRL_ENABLE_AUTO) ||
	    (cpu_is_omap24xx() && v == OMAP24XX_CLKSTCTRL_ENABLE_AUTO))
		_clkdm_add_autodeps(clkdm);
	else
		omap2_clkdm_wakeup(clkdm);

	pwrdm_wait_transition(clkdm->pwrdm.ptr);

	return 0;
}

/**
 * omap2_clkdm_clk_disable - remove an enabled downstream clock from this clkdm
 * @clkdm: struct clockdomain *
 * @clk: struct clk * of the disabled downstream clock
 *
 * Decrement the usecount of this clockdomain 'clkdm'. Intended to be
 * called by clk_disable() code.  If the usecount goes to 0, put the
 * clockdomain to sleep (software-supervised mode) or remove the
 * clkdm-pwrdm autodependencies (hardware-supervised mode).  Returns
 * -EINVAL if passed null pointers; -ERANGE if the clkdm usecount
 * underflows and debugging is enabled; or returns 0 upon success or
 * if the clockdomain is in hwsup idle mode.
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

	v = omap2_clkdm_clktrctrl_read(clkdm);

	if ((cpu_is_omap34xx() && v == OMAP34XX_CLKSTCTRL_ENABLE_AUTO) ||
	    (cpu_is_omap24xx() && v == OMAP24XX_CLKSTCTRL_ENABLE_AUTO))
		_clkdm_del_autodeps(clkdm);
	else
		omap2_clkdm_sleep(clkdm);

	return 0;
}

