/*
 * OMAP powerdomain control
 *
 * Copyright (C) 2007-2008 Texas Instruments, Inc.
 * Copyright (C) 2007-2008 Nokia Corporation
 *
 * Written by Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifdef CONFIG_OMAP_DEBUG_POWERDOMAIN
# define DEBUG
#endif

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>

#include <asm/atomic.h>

#include "cm.h"
#include "cm-regbits-34xx.h"
#include "prm.h"
#include "prm-regbits-34xx.h"

#include <mach/cpu.h>
#include <mach/powerdomain.h>
#include <mach/clockdomain.h>

#include "pm.h"

enum {
	PWRDM_STATE_NOW = 0,
	PWRDM_STATE_PREV,
};

/* pwrdm_list contains all registered struct powerdomains */
static LIST_HEAD(pwrdm_list);

/*
 * pwrdm_rwlock protects pwrdm_list add and del ops - also reused to
 * protect pwrdm_clkdms[] during clkdm add/del ops
 */
static DEFINE_RWLOCK(pwrdm_rwlock);


/* Private functions */

static u32 prm_read_mod_bits_shift(s16 domain, s16 idx, u32 mask)
{
	u32 v;

	v = prm_read_mod_reg(domain, idx);
	v &= mask;
	v >>= __ffs(mask);

	return v;
}

static struct powerdomain *_pwrdm_lookup(const char *name)
{
	struct powerdomain *pwrdm, *temp_pwrdm;

	pwrdm = NULL;

	list_for_each_entry(temp_pwrdm, &pwrdm_list, node) {
		if (!strcmp(name, temp_pwrdm->name)) {
			pwrdm = temp_pwrdm;
			break;
		}
	}

	return pwrdm;
}

/* _pwrdm_deps_lookup - look up the specified powerdomain in a pwrdm list */
static struct powerdomain *_pwrdm_deps_lookup(struct powerdomain *pwrdm,
					      struct pwrdm_dep *deps)
{
	struct pwrdm_dep *pd;

	if (!pwrdm || !deps || !omap_chip_is(pwrdm->omap_chip))
		return ERR_PTR(-EINVAL);

	for (pd = deps; pd->pwrdm_name; pd++) {

		if (!omap_chip_is(pd->omap_chip))
			continue;

		if (!pd->pwrdm && pd->pwrdm_name)
			pd->pwrdm = pwrdm_lookup(pd->pwrdm_name);

		if (pd->pwrdm == pwrdm)
			break;

	}

	if (!pd->pwrdm_name)
		return ERR_PTR(-ENOENT);

	return pd->pwrdm;
}

static int _pwrdm_state_switch(struct powerdomain *pwrdm, int flag)
{

	int prev;
	int state;

	if (pwrdm == NULL)
		return -EINVAL;

	state = pwrdm_read_pwrst(pwrdm);

	switch (flag) {
	case PWRDM_STATE_NOW:
		prev = pwrdm->state;
		break;
	case PWRDM_STATE_PREV:
		prev = pwrdm_read_prev_pwrst(pwrdm);
		if (pwrdm->state != prev)
			pwrdm->state_counter[prev]++;
		break;
	default:
		return -EINVAL;
	}

	if (state != prev)
		pwrdm->state_counter[state]++;

	pm_dbg_update_time(pwrdm, prev);

	pwrdm->state = state;

	return 0;
}

static int _pwrdm_pre_transition_cb(struct powerdomain *pwrdm, void *unused)
{
	pwrdm_clear_all_prev_pwrst(pwrdm);
	_pwrdm_state_switch(pwrdm, PWRDM_STATE_NOW);
	return 0;
}

static int _pwrdm_post_transition_cb(struct powerdomain *pwrdm, void *unused)
{
	_pwrdm_state_switch(pwrdm, PWRDM_STATE_PREV);
	return 0;
}

static __init void _pwrdm_setup(struct powerdomain *pwrdm)
{
	int i;

	for (i = 0; i < 4; i++)
		pwrdm->state_counter[i] = 0;

	pwrdm_wait_transition(pwrdm);
	pwrdm->state = pwrdm_read_pwrst(pwrdm);
	pwrdm->state_counter[pwrdm->state] = 1;

}

/* Public functions */

/**
 * pwrdm_init - set up the powerdomain layer
 *
 * Loop through the list of powerdomains, registering all that are
 * available on the current CPU. If pwrdm_list is supplied and not
 * null, all of the referenced powerdomains will be registered.  No
 * return value.
 */
void pwrdm_init(struct powerdomain **pwrdm_list)
{
	struct powerdomain **p = NULL;

	if (pwrdm_list) {
		for (p = pwrdm_list; *p; p++) {
			pwrdm_register(*p);
			_pwrdm_setup(*p);
		}
	}
}

/**
 * pwrdm_register - register a powerdomain
 * @pwrdm: struct powerdomain * to register
 *
 * Adds a powerdomain to the internal powerdomain list.  Returns
 * -EINVAL if given a null pointer, -EEXIST if a powerdomain is
 * already registered by the provided name, or 0 upon success.
 */
int pwrdm_register(struct powerdomain *pwrdm)
{
	unsigned long flags;
	int ret = -EINVAL;

	if (!pwrdm)
		return -EINVAL;

	if (!omap_chip_is(pwrdm->omap_chip))
		return -EINVAL;

	write_lock_irqsave(&pwrdm_rwlock, flags);
	if (_pwrdm_lookup(pwrdm->name)) {
		ret = -EEXIST;
		goto pr_unlock;
	}

	list_add(&pwrdm->node, &pwrdm_list);

	pr_debug("powerdomain: registered %s\n", pwrdm->name);
	ret = 0;

pr_unlock:
	write_unlock_irqrestore(&pwrdm_rwlock, flags);

	return ret;
}

/**
 * pwrdm_unregister - unregister a powerdomain
 * @pwrdm: struct powerdomain * to unregister
 *
 * Removes a powerdomain from the internal powerdomain list.  Returns
 * -EINVAL if pwrdm argument is NULL.
 */
int pwrdm_unregister(struct powerdomain *pwrdm)
{
	unsigned long flags;

	if (!pwrdm)
		return -EINVAL;

	write_lock_irqsave(&pwrdm_rwlock, flags);
	list_del(&pwrdm->node);
	write_unlock_irqrestore(&pwrdm_rwlock, flags);

	pr_debug("powerdomain: unregistered %s\n", pwrdm->name);

	return 0;
}

/**
 * pwrdm_lookup - look up a powerdomain by name, return a pointer
 * @name: name of powerdomain
 *
 * Find a registered powerdomain by its name.  Returns a pointer to the
 * struct powerdomain if found, or NULL otherwise.
 */
struct powerdomain *pwrdm_lookup(const char *name)
{
	struct powerdomain *pwrdm;
	unsigned long flags;

	if (!name)
		return NULL;

	read_lock_irqsave(&pwrdm_rwlock, flags);
	pwrdm = _pwrdm_lookup(name);
	read_unlock_irqrestore(&pwrdm_rwlock, flags);

	return pwrdm;
}

/**
 * pwrdm_for_each_nolock - call function on each registered clockdomain
 * @fn: callback function *
 *
 * Call the supplied function for each registered powerdomain.  The
 * callback function can return anything but 0 to bail out early from
 * the iterator.  Returns the last return value of the callback function, which
 * should be 0 for success or anything else to indicate failure; or -EINVAL if
 * the function pointer is null.
 */
int pwrdm_for_each_nolock(int (*fn)(struct powerdomain *pwrdm, void *user),
				void *user)
{
	struct powerdomain *temp_pwrdm;
	int ret = 0;

	if (!fn)
		return -EINVAL;

	list_for_each_entry(temp_pwrdm, &pwrdm_list, node) {
		ret = (*fn)(temp_pwrdm, user);
		if (ret)
			break;
	}

	return ret;
}

/**
 * pwrdm_for_each - call function on each registered clockdomain
 * @fn: callback function *
 *
 * This function is the same as 'pwrdm_for_each_nolock()', but keeps the
 * &pwrdm_rwlock locked for reading, so no powerdomain structure manipulation
 * functions should be called from the callback, although hardware powerdomain
 * control functions are fine.
 */
int pwrdm_for_each(int (*fn)(struct powerdomain *pwrdm, void *user),
			void *user)
{
	unsigned long flags;
	int ret;

	read_lock_irqsave(&pwrdm_rwlock, flags);
	ret = pwrdm_for_each_nolock(fn, user);
	read_unlock_irqrestore(&pwrdm_rwlock, flags);

	return ret;
}

/**
 * pwrdm_add_clkdm - add a clockdomain to a powerdomain
 * @pwrdm: struct powerdomain * to add the clockdomain to
 * @clkdm: struct clockdomain * to associate with a powerdomain
 *
 * Associate the clockdomain 'clkdm' with a powerdomain 'pwrdm'.  This
 * enables the use of pwrdm_for_each_clkdm().  Returns -EINVAL if
 * presented with invalid pointers; -ENOMEM if memory could not be allocated;
 * or 0 upon success.
 */
int pwrdm_add_clkdm(struct powerdomain *pwrdm, struct clockdomain *clkdm)
{
	unsigned long flags;
	int i;
	int ret = -EINVAL;

	if (!pwrdm || !clkdm)
		return -EINVAL;

	pr_debug("powerdomain: associating clockdomain %s with powerdomain "
		 "%s\n", clkdm->name, pwrdm->name);

	write_lock_irqsave(&pwrdm_rwlock, flags);

	for (i = 0; i < PWRDM_MAX_CLKDMS; i++) {
		if (!pwrdm->pwrdm_clkdms[i])
			break;
#ifdef DEBUG
		if (pwrdm->pwrdm_clkdms[i] == clkdm) {
			ret = -EINVAL;
			goto pac_exit;
		}
#endif
	}

	if (i == PWRDM_MAX_CLKDMS) {
		pr_debug("powerdomain: increase PWRDM_MAX_CLKDMS for "
			 "pwrdm %s clkdm %s\n", pwrdm->name, clkdm->name);
		WARN_ON(1);
		ret = -ENOMEM;
		goto pac_exit;
	}

	pwrdm->pwrdm_clkdms[i] = clkdm;

	ret = 0;

pac_exit:
	write_unlock_irqrestore(&pwrdm_rwlock, flags);

	return ret;
}

/**
 * pwrdm_del_clkdm - remove a clockdomain from a powerdomain
 * @pwrdm: struct powerdomain * to add the clockdomain to
 * @clkdm: struct clockdomain * to associate with a powerdomain
 *
 * Dissociate the clockdomain 'clkdm' from the powerdomain
 * 'pwrdm'. Returns -EINVAL if presented with invalid pointers;
 * -ENOENT if the clkdm was not associated with the powerdomain, or 0
 * upon success.
 */
int pwrdm_del_clkdm(struct powerdomain *pwrdm, struct clockdomain *clkdm)
{
	unsigned long flags;
	int ret = -EINVAL;
	int i;

	if (!pwrdm || !clkdm)
		return -EINVAL;

	pr_debug("powerdomain: dissociating clockdomain %s from powerdomain "
		 "%s\n", clkdm->name, pwrdm->name);

	write_lock_irqsave(&pwrdm_rwlock, flags);

	for (i = 0; i < PWRDM_MAX_CLKDMS; i++)
		if (pwrdm->pwrdm_clkdms[i] == clkdm)
			break;

	if (i == PWRDM_MAX_CLKDMS) {
		pr_debug("powerdomain: clkdm %s not associated with pwrdm "
			 "%s ?!\n", clkdm->name, pwrdm->name);
		ret = -ENOENT;
		goto pdc_exit;
	}

	pwrdm->pwrdm_clkdms[i] = NULL;

	ret = 0;

pdc_exit:
	write_unlock_irqrestore(&pwrdm_rwlock, flags);

	return ret;
}

/**
 * pwrdm_for_each_clkdm - call function on each clkdm in a pwrdm
 * @pwrdm: struct powerdomain * to iterate over
 * @fn: callback function *
 *
 * Call the supplied function for each clockdomain in the powerdomain
 * 'pwrdm'.  The callback function can return anything but 0 to bail
 * out early from the iterator.  The callback function is called with
 * the pwrdm_rwlock held for reading, so no powerdomain structure
 * manipulation functions should be called from the callback, although
 * hardware powerdomain control functions are fine.  Returns -EINVAL
 * if presented with invalid pointers; or passes along the last return
 * value of the callback function, which should be 0 for success or
 * anything else to indicate failure.
 */
int pwrdm_for_each_clkdm(struct powerdomain *pwrdm,
			 int (*fn)(struct powerdomain *pwrdm,
				   struct clockdomain *clkdm))
{
	unsigned long flags;
	int ret = 0;
	int i;

	if (!fn)
		return -EINVAL;

	read_lock_irqsave(&pwrdm_rwlock, flags);

	for (i = 0; i < PWRDM_MAX_CLKDMS && !ret; i++)
		ret = (*fn)(pwrdm, pwrdm->pwrdm_clkdms[i]);

	read_unlock_irqrestore(&pwrdm_rwlock, flags);

	return ret;
}


/**
 * pwrdm_add_wkdep - add a wakeup dependency from pwrdm2 to pwrdm1
 * @pwrdm1: wake this struct powerdomain * up (dependent)
 * @pwrdm2: when this struct powerdomain * wakes up (source)
 *
 * When the powerdomain represented by pwrdm2 wakes up (due to an
 * interrupt), wake up pwrdm1.	Implemented in hardware on the OMAP,
 * this feature is designed to reduce wakeup latency of the dependent
 * powerdomain.  Returns -EINVAL if presented with invalid powerdomain
 * pointers, -ENOENT if pwrdm2 cannot wake up pwrdm1 in hardware, or
 * 0 upon success.
 */
int pwrdm_add_wkdep(struct powerdomain *pwrdm1, struct powerdomain *pwrdm2)
{
	struct powerdomain *p;

	if (!pwrdm1)
		return -EINVAL;

	p = _pwrdm_deps_lookup(pwrdm2, pwrdm1->wkdep_srcs);
	if (IS_ERR(p)) {
		pr_debug("powerdomain: hardware cannot set/clear wake up of "
			 "%s when %s wakes up\n", pwrdm1->name, pwrdm2->name);
		return IS_ERR(p);
	}

	pr_debug("powerdomain: hardware will wake up %s when %s wakes up\n",
		 pwrdm1->name, pwrdm2->name);

	prm_set_mod_reg_bits((1 << pwrdm2->dep_bit),
			     pwrdm1->prcm_offs, PM_WKDEP);

	return 0;
}

/**
 * pwrdm_del_wkdep - remove a wakeup dependency from pwrdm2 to pwrdm1
 * @pwrdm1: wake this struct powerdomain * up (dependent)
 * @pwrdm2: when this struct powerdomain * wakes up (source)
 *
 * Remove a wakeup dependency that causes pwrdm1 to wake up when pwrdm2
 * wakes up.  Returns -EINVAL if presented with invalid powerdomain
 * pointers, -ENOENT if pwrdm2 cannot wake up pwrdm1 in hardware, or
 * 0 upon success.
 */
int pwrdm_del_wkdep(struct powerdomain *pwrdm1, struct powerdomain *pwrdm2)
{
	struct powerdomain *p;

	if (!pwrdm1)
		return -EINVAL;

	p = _pwrdm_deps_lookup(pwrdm2, pwrdm1->wkdep_srcs);
	if (IS_ERR(p)) {
		pr_debug("powerdomain: hardware cannot set/clear wake up of "
			 "%s when %s wakes up\n", pwrdm1->name, pwrdm2->name);
		return IS_ERR(p);
	}

	pr_debug("powerdomain: hardware will no longer wake up %s after %s "
		 "wakes up\n", pwrdm1->name, pwrdm2->name);

	prm_clear_mod_reg_bits((1 << pwrdm2->dep_bit),
			       pwrdm1->prcm_offs, PM_WKDEP);

	return 0;
}

/**
 * pwrdm_read_wkdep - read wakeup dependency state from pwrdm2 to pwrdm1
 * @pwrdm1: wake this struct powerdomain * up (dependent)
 * @pwrdm2: when this struct powerdomain * wakes up (source)
 *
 * Return 1 if a hardware wakeup dependency exists wherein pwrdm1 will be
 * awoken when pwrdm2 wakes up; 0 if dependency is not set; -EINVAL
 * if either powerdomain pointer is invalid; or -ENOENT if the hardware
 * is incapable.
 *
 * REVISIT: Currently this function only represents software-controllable
 * wakeup dependencies.  Wakeup dependencies fixed in hardware are not
 * yet handled here.
 */
int pwrdm_read_wkdep(struct powerdomain *pwrdm1, struct powerdomain *pwrdm2)
{
	struct powerdomain *p;

	if (!pwrdm1)
		return -EINVAL;

	p = _pwrdm_deps_lookup(pwrdm2, pwrdm1->wkdep_srcs);
	if (IS_ERR(p)) {
		pr_debug("powerdomain: hardware cannot set/clear wake up of "
			 "%s when %s wakes up\n", pwrdm1->name, pwrdm2->name);
		return IS_ERR(p);
	}

	return prm_read_mod_bits_shift(pwrdm1->prcm_offs, PM_WKDEP,
					(1 << pwrdm2->dep_bit));
}

/**
 * pwrdm_add_sleepdep - add a sleep dependency from pwrdm2 to pwrdm1
 * @pwrdm1: prevent this struct powerdomain * from sleeping (dependent)
 * @pwrdm2: when this struct powerdomain * is active (source)
 *
 * Prevent pwrdm1 from automatically going inactive (and then to
 * retention or off) if pwrdm2 is still active.	 Returns -EINVAL if
 * presented with invalid powerdomain pointers or called on a machine
 * that does not support software-configurable hardware sleep dependencies,
 * -ENOENT if the specified dependency cannot be set in hardware, or
 * 0 upon success.
 */
int pwrdm_add_sleepdep(struct powerdomain *pwrdm1, struct powerdomain *pwrdm2)
{
	struct powerdomain *p;

	if (!pwrdm1)
		return -EINVAL;

	if (!cpu_is_omap34xx())
		return -EINVAL;

	p = _pwrdm_deps_lookup(pwrdm2, pwrdm1->sleepdep_srcs);
	if (IS_ERR(p)) {
		pr_debug("powerdomain: hardware cannot set/clear sleep "
			 "dependency affecting %s from %s\n", pwrdm1->name,
			 pwrdm2->name);
		return IS_ERR(p);
	}

	pr_debug("powerdomain: will prevent %s from sleeping if %s is active\n",
		 pwrdm1->name, pwrdm2->name);

	cm_set_mod_reg_bits((1 << pwrdm2->dep_bit),
			    pwrdm1->prcm_offs, OMAP3430_CM_SLEEPDEP);

	return 0;
}

/**
 * pwrdm_del_sleepdep - remove a sleep dependency from pwrdm2 to pwrdm1
 * @pwrdm1: prevent this struct powerdomain * from sleeping (dependent)
 * @pwrdm2: when this struct powerdomain * is active (source)
 *
 * Allow pwrdm1 to automatically go inactive (and then to retention or
 * off), independent of the activity state of pwrdm2.  Returns -EINVAL
 * if presented with invalid powerdomain pointers or called on a machine
 * that does not support software-configurable hardware sleep dependencies,
 * -ENOENT if the specified dependency cannot be cleared in hardware, or
 * 0 upon success.
 */
int pwrdm_del_sleepdep(struct powerdomain *pwrdm1, struct powerdomain *pwrdm2)
{
	struct powerdomain *p;

	if (!pwrdm1)
		return -EINVAL;

	if (!cpu_is_omap34xx())
		return -EINVAL;

	p = _pwrdm_deps_lookup(pwrdm2, pwrdm1->sleepdep_srcs);
	if (IS_ERR(p)) {
		pr_debug("powerdomain: hardware cannot set/clear sleep "
			 "dependency affecting %s from %s\n", pwrdm1->name,
			 pwrdm2->name);
		return IS_ERR(p);
	}

	pr_debug("powerdomain: will no longer prevent %s from sleeping if "
		 "%s is active\n", pwrdm1->name, pwrdm2->name);

	cm_clear_mod_reg_bits((1 << pwrdm2->dep_bit),
			      pwrdm1->prcm_offs, OMAP3430_CM_SLEEPDEP);

	return 0;
}

/**
 * pwrdm_read_sleepdep - read sleep dependency state from pwrdm2 to pwrdm1
 * @pwrdm1: prevent this struct powerdomain * from sleeping (dependent)
 * @pwrdm2: when this struct powerdomain * is active (source)
 *
 * Return 1 if a hardware sleep dependency exists wherein pwrdm1 will
 * not be allowed to automatically go inactive if pwrdm2 is active;
 * 0 if pwrdm1's automatic power state inactivity transition is independent
 * of pwrdm2's; -EINVAL if either powerdomain pointer is invalid or called
 * on a machine that does not support software-configurable hardware sleep
 * dependencies; or -ENOENT if the hardware is incapable.
 *
 * REVISIT: Currently this function only represents software-controllable
 * sleep dependencies.	Sleep dependencies fixed in hardware are not
 * yet handled here.
 */
int pwrdm_read_sleepdep(struct powerdomain *pwrdm1, struct powerdomain *pwrdm2)
{
	struct powerdomain *p;

	if (!pwrdm1)
		return -EINVAL;

	if (!cpu_is_omap34xx())
		return -EINVAL;

	p = _pwrdm_deps_lookup(pwrdm2, pwrdm1->sleepdep_srcs);
	if (IS_ERR(p)) {
		pr_debug("powerdomain: hardware cannot set/clear sleep "
			 "dependency affecting %s from %s\n", pwrdm1->name,
			 pwrdm2->name);
		return IS_ERR(p);
	}

	return prm_read_mod_bits_shift(pwrdm1->prcm_offs, OMAP3430_CM_SLEEPDEP,
					(1 << pwrdm2->dep_bit));
}

/**
 * pwrdm_get_mem_bank_count - get number of memory banks in this powerdomain
 * @pwrdm: struct powerdomain *
 *
 * Return the number of controllable memory banks in powerdomain pwrdm,
 * starting with 1.  Returns -EINVAL if the powerdomain pointer is null.
 */
int pwrdm_get_mem_bank_count(struct powerdomain *pwrdm)
{
	if (!pwrdm)
		return -EINVAL;

	return pwrdm->banks;
}

/**
 * pwrdm_set_next_pwrst - set next powerdomain power state
 * @pwrdm: struct powerdomain * to set
 * @pwrst: one of the PWRDM_POWER_* macros
 *
 * Set the powerdomain pwrdm's next power state to pwrst.  The powerdomain
 * may not enter this state immediately if the preconditions for this state
 * have not been satisfied.  Returns -EINVAL if the powerdomain pointer is
 * null or if the power state is invalid for the powerdomin, or returns 0
 * upon success.
 */
int pwrdm_set_next_pwrst(struct powerdomain *pwrdm, u8 pwrst)
{
	if (!pwrdm)
		return -EINVAL;

	if (!(pwrdm->pwrsts & (1 << pwrst)))
		return -EINVAL;

	pr_debug("powerdomain: setting next powerstate for %s to %0x\n",
		 pwrdm->name, pwrst);

	prm_rmw_mod_reg_bits(OMAP_POWERSTATE_MASK,
			     (pwrst << OMAP_POWERSTATE_SHIFT),
			     pwrdm->prcm_offs, PM_PWSTCTRL);

	return 0;
}

/**
 * pwrdm_read_next_pwrst - get next powerdomain power state
 * @pwrdm: struct powerdomain * to get power state
 *
 * Return the powerdomain pwrdm's next power state.  Returns -EINVAL
 * if the powerdomain pointer is null or returns the next power state
 * upon success.
 */
int pwrdm_read_next_pwrst(struct powerdomain *pwrdm)
{
	if (!pwrdm)
		return -EINVAL;

	return prm_read_mod_bits_shift(pwrdm->prcm_offs, PM_PWSTCTRL,
					OMAP_POWERSTATE_MASK);
}

/**
 * pwrdm_read_pwrst - get current powerdomain power state
 * @pwrdm: struct powerdomain * to get power state
 *
 * Return the powerdomain pwrdm's current power state.	Returns -EINVAL
 * if the powerdomain pointer is null or returns the current power state
 * upon success.
 */
int pwrdm_read_pwrst(struct powerdomain *pwrdm)
{
	if (!pwrdm)
		return -EINVAL;

	return prm_read_mod_bits_shift(pwrdm->prcm_offs, PM_PWSTST,
					OMAP_POWERSTATEST_MASK);
}

/**
 * pwrdm_read_prev_pwrst - get previous powerdomain power state
 * @pwrdm: struct powerdomain * to get previous power state
 *
 * Return the powerdomain pwrdm's previous power state.  Returns -EINVAL
 * if the powerdomain pointer is null or returns the previous power state
 * upon success.
 */
int pwrdm_read_prev_pwrst(struct powerdomain *pwrdm)
{
	if (!pwrdm)
		return -EINVAL;

	return prm_read_mod_bits_shift(pwrdm->prcm_offs, OMAP3430_PM_PREPWSTST,
					OMAP3430_LASTPOWERSTATEENTERED_MASK);
}

/**
 * pwrdm_set_logic_retst - set powerdomain logic power state upon retention
 * @pwrdm: struct powerdomain * to set
 * @pwrst: one of the PWRDM_POWER_* macros
 *
 * Set the next power state that the logic portion of the powerdomain
 * pwrdm will enter when the powerdomain enters retention.  This will
 * be either RETENTION or OFF, if supported.  Returns -EINVAL if the
 * powerdomain pointer is null or the target power state is not not
 * supported, or returns 0 upon success.
 */
int pwrdm_set_logic_retst(struct powerdomain *pwrdm, u8 pwrst)
{
	if (!pwrdm)
		return -EINVAL;

	if (!(pwrdm->pwrsts_logic_ret & (1 << pwrst)))
		return -EINVAL;

	pr_debug("powerdomain: setting next logic powerstate for %s to %0x\n",
		 pwrdm->name, pwrst);

	/*
	 * The register bit names below may not correspond to the
	 * actual names of the bits in each powerdomain's register,
	 * but the type of value returned is the same for each
	 * powerdomain.
	 */
	prm_rmw_mod_reg_bits(OMAP3430_LOGICL1CACHERETSTATE,
			     (pwrst << __ffs(OMAP3430_LOGICL1CACHERETSTATE)),
			     pwrdm->prcm_offs, PM_PWSTCTRL);

	return 0;
}

/**
 * pwrdm_set_mem_onst - set memory power state while powerdomain ON
 * @pwrdm: struct powerdomain * to set
 * @bank: memory bank number to set (0-3)
 * @pwrst: one of the PWRDM_POWER_* macros
 *
 * Set the next power state that memory bank x of the powerdomain
 * pwrdm will enter when the powerdomain enters the ON state.  Bank
 * will be a number from 0 to 3, and represents different types of
 * memory, depending on the powerdomain.  Returns -EINVAL if the
 * powerdomain pointer is null or the target power state is not not
 * supported for this memory bank, -EEXIST if the target memory bank
 * does not exist or is not controllable, or returns 0 upon success.
 */
int pwrdm_set_mem_onst(struct powerdomain *pwrdm, u8 bank, u8 pwrst)
{
	u32 m;

	if (!pwrdm)
		return -EINVAL;

	if (pwrdm->banks < (bank + 1))
		return -EEXIST;

	if (!(pwrdm->pwrsts_mem_on[bank] & (1 << pwrst)))
		return -EINVAL;

	pr_debug("powerdomain: setting next memory powerstate for domain %s "
		 "bank %0x while pwrdm-ON to %0x\n", pwrdm->name, bank, pwrst);

	/*
	 * The register bit names below may not correspond to the
	 * actual names of the bits in each powerdomain's register,
	 * but the type of value returned is the same for each
	 * powerdomain.
	 */
	switch (bank) {
	case 0:
		m = OMAP3430_SHAREDL1CACHEFLATONSTATE_MASK;
		break;
	case 1:
		m = OMAP3430_L1FLATMEMONSTATE_MASK;
		break;
	case 2:
		m = OMAP3430_SHAREDL2CACHEFLATONSTATE_MASK;
		break;
	case 3:
		m = OMAP3430_L2FLATMEMONSTATE_MASK;
		break;
	default:
		WARN_ON(1); /* should never happen */
		return -EEXIST;
	}

	prm_rmw_mod_reg_bits(m, (pwrst << __ffs(m)),
			     pwrdm->prcm_offs, PM_PWSTCTRL);

	return 0;
}

/**
 * pwrdm_set_mem_retst - set memory power state while powerdomain in RET
 * @pwrdm: struct powerdomain * to set
 * @bank: memory bank number to set (0-3)
 * @pwrst: one of the PWRDM_POWER_* macros
 *
 * Set the next power state that memory bank x of the powerdomain
 * pwrdm will enter when the powerdomain enters the RETENTION state.
 * Bank will be a number from 0 to 3, and represents different types
 * of memory, depending on the powerdomain.  pwrst will be either
 * RETENTION or OFF, if supported. Returns -EINVAL if the powerdomain
 * pointer is null or the target power state is not not supported for
 * this memory bank, -EEXIST if the target memory bank does not exist
 * or is not controllable, or returns 0 upon success.
 */
int pwrdm_set_mem_retst(struct powerdomain *pwrdm, u8 bank, u8 pwrst)
{
	u32 m;

	if (!pwrdm)
		return -EINVAL;

	if (pwrdm->banks < (bank + 1))
		return -EEXIST;

	if (!(pwrdm->pwrsts_mem_ret[bank] & (1 << pwrst)))
		return -EINVAL;

	pr_debug("powerdomain: setting next memory powerstate for domain %s "
		 "bank %0x while pwrdm-RET to %0x\n", pwrdm->name, bank, pwrst);

	/*
	 * The register bit names below may not correspond to the
	 * actual names of the bits in each powerdomain's register,
	 * but the type of value returned is the same for each
	 * powerdomain.
	 */
	switch (bank) {
	case 0:
		m = OMAP3430_SHAREDL1CACHEFLATRETSTATE;
		break;
	case 1:
		m = OMAP3430_L1FLATMEMRETSTATE;
		break;
	case 2:
		m = OMAP3430_SHAREDL2CACHEFLATRETSTATE;
		break;
	case 3:
		m = OMAP3430_L2FLATMEMRETSTATE;
		break;
	default:
		WARN_ON(1); /* should never happen */
		return -EEXIST;
	}

	prm_rmw_mod_reg_bits(m, (pwrst << __ffs(m)), pwrdm->prcm_offs,
			     PM_PWSTCTRL);

	return 0;
}

/**
 * pwrdm_read_logic_pwrst - get current powerdomain logic retention power state
 * @pwrdm: struct powerdomain * to get current logic retention power state
 *
 * Return the current power state that the logic portion of
 * powerdomain pwrdm will enter
 * Returns -EINVAL if the powerdomain pointer is null or returns the
 * current logic retention power state upon success.
 */
int pwrdm_read_logic_pwrst(struct powerdomain *pwrdm)
{
	if (!pwrdm)
		return -EINVAL;

	return prm_read_mod_bits_shift(pwrdm->prcm_offs, PM_PWSTST,
					OMAP3430_LOGICSTATEST);
}

/**
 * pwrdm_read_prev_logic_pwrst - get previous powerdomain logic power state
 * @pwrdm: struct powerdomain * to get previous logic power state
 *
 * Return the powerdomain pwrdm's logic power state.  Returns -EINVAL
 * if the powerdomain pointer is null or returns the previous logic
 * power state upon success.
 */
int pwrdm_read_prev_logic_pwrst(struct powerdomain *pwrdm)
{
	if (!pwrdm)
		return -EINVAL;

	/*
	 * The register bit names below may not correspond to the
	 * actual names of the bits in each powerdomain's register,
	 * but the type of value returned is the same for each
	 * powerdomain.
	 */
	return prm_read_mod_bits_shift(pwrdm->prcm_offs, OMAP3430_PM_PREPWSTST,
					OMAP3430_LASTLOGICSTATEENTERED);
}

/**
 * pwrdm_read_mem_pwrst - get current memory bank power state
 * @pwrdm: struct powerdomain * to get current memory bank power state
 * @bank: memory bank number (0-3)
 *
 * Return the powerdomain pwrdm's current memory power state for bank
 * x.  Returns -EINVAL if the powerdomain pointer is null, -EEXIST if
 * the target memory bank does not exist or is not controllable, or
 * returns the current memory power state upon success.
 */
int pwrdm_read_mem_pwrst(struct powerdomain *pwrdm, u8 bank)
{
	u32 m;

	if (!pwrdm)
		return -EINVAL;

	if (pwrdm->banks < (bank + 1))
		return -EEXIST;

	/*
	 * The register bit names below may not correspond to the
	 * actual names of the bits in each powerdomain's register,
	 * but the type of value returned is the same for each
	 * powerdomain.
	 */
	switch (bank) {
	case 0:
		m = OMAP3430_SHAREDL1CACHEFLATSTATEST_MASK;
		break;
	case 1:
		m = OMAP3430_L1FLATMEMSTATEST_MASK;
		break;
	case 2:
		m = OMAP3430_SHAREDL2CACHEFLATSTATEST_MASK;
		break;
	case 3:
		m = OMAP3430_L2FLATMEMSTATEST_MASK;
		break;
	default:
		WARN_ON(1); /* should never happen */
		return -EEXIST;
	}

	return prm_read_mod_bits_shift(pwrdm->prcm_offs, PM_PWSTST, m);
}

/**
 * pwrdm_read_prev_mem_pwrst - get previous memory bank power state
 * @pwrdm: struct powerdomain * to get previous memory bank power state
 * @bank: memory bank number (0-3)
 *
 * Return the powerdomain pwrdm's previous memory power state for bank
 * x.  Returns -EINVAL if the powerdomain pointer is null, -EEXIST if
 * the target memory bank does not exist or is not controllable, or
 * returns the previous memory power state upon success.
 */
int pwrdm_read_prev_mem_pwrst(struct powerdomain *pwrdm, u8 bank)
{
	u32 m;

	if (!pwrdm)
		return -EINVAL;

	if (pwrdm->banks < (bank + 1))
		return -EEXIST;

	/*
	 * The register bit names below may not correspond to the
	 * actual names of the bits in each powerdomain's register,
	 * but the type of value returned is the same for each
	 * powerdomain.
	 */
	switch (bank) {
	case 0:
		m = OMAP3430_LASTMEM1STATEENTERED_MASK;
		break;
	case 1:
		m = OMAP3430_LASTMEM2STATEENTERED_MASK;
		break;
	case 2:
		m = OMAP3430_LASTSHAREDL2CACHEFLATSTATEENTERED_MASK;
		break;
	case 3:
		m = OMAP3430_LASTL2FLATMEMSTATEENTERED_MASK;
		break;
	default:
		WARN_ON(1); /* should never happen */
		return -EEXIST;
	}

	return prm_read_mod_bits_shift(pwrdm->prcm_offs,
					OMAP3430_PM_PREPWSTST, m);
}

/**
 * pwrdm_clear_all_prev_pwrst - clear previous powerstate register for a pwrdm
 * @pwrdm: struct powerdomain * to clear
 *
 * Clear the powerdomain's previous power state register.  Clears the
 * entire register, including logic and memory bank previous power states.
 * Returns -EINVAL if the powerdomain pointer is null, or returns 0 upon
 * success.
 */
int pwrdm_clear_all_prev_pwrst(struct powerdomain *pwrdm)
{
	if (!pwrdm)
		return -EINVAL;

	/*
	 * XXX should get the powerdomain's current state here;
	 * warn & fail if it is not ON.
	 */

	pr_debug("powerdomain: clearing previous power state reg for %s\n",
		 pwrdm->name);

	prm_write_mod_reg(0, pwrdm->prcm_offs, OMAP3430_PM_PREPWSTST);

	return 0;
}

/**
 * pwrdm_enable_hdwr_sar - enable automatic hardware SAR for a pwrdm
 * @pwrdm: struct powerdomain *
 *
 * Enable automatic context save-and-restore upon power state change
 * for some devices in a powerdomain.  Warning: this only affects a
 * subset of devices in a powerdomain; check the TRM closely.  Returns
 * -EINVAL if the powerdomain pointer is null or if the powerdomain
 * does not support automatic save-and-restore, or returns 0 upon
 * success.
 */
int pwrdm_enable_hdwr_sar(struct powerdomain *pwrdm)
{
	if (!pwrdm)
		return -EINVAL;

	if (!(pwrdm->flags & PWRDM_HAS_HDWR_SAR))
		return -EINVAL;

	pr_debug("powerdomain: %s: setting SAVEANDRESTORE bit\n",
		 pwrdm->name);

	prm_rmw_mod_reg_bits(0, 1 << OMAP3430ES2_SAVEANDRESTORE_SHIFT,
			     pwrdm->prcm_offs, PM_PWSTCTRL);

	return 0;
}

/**
 * pwrdm_disable_hdwr_sar - disable automatic hardware SAR for a pwrdm
 * @pwrdm: struct powerdomain *
 *
 * Disable automatic context save-and-restore upon power state change
 * for some devices in a powerdomain.  Warning: this only affects a
 * subset of devices in a powerdomain; check the TRM closely.  Returns
 * -EINVAL if the powerdomain pointer is null or if the powerdomain
 * does not support automatic save-and-restore, or returns 0 upon
 * success.
 */
int pwrdm_disable_hdwr_sar(struct powerdomain *pwrdm)
{
	if (!pwrdm)
		return -EINVAL;

	if (!(pwrdm->flags & PWRDM_HAS_HDWR_SAR))
		return -EINVAL;

	pr_debug("powerdomain: %s: clearing SAVEANDRESTORE bit\n",
		 pwrdm->name);

	prm_rmw_mod_reg_bits(1 << OMAP3430ES2_SAVEANDRESTORE_SHIFT, 0,
			     pwrdm->prcm_offs, PM_PWSTCTRL);

	return 0;
}

/**
 * pwrdm_has_hdwr_sar - test whether powerdomain supports hardware SAR
 * @pwrdm: struct powerdomain *
 *
 * Returns 1 if powerdomain 'pwrdm' supports hardware save-and-restore
 * for some devices, or 0 if it does not.
 */
bool pwrdm_has_hdwr_sar(struct powerdomain *pwrdm)
{
	return (pwrdm && pwrdm->flags & PWRDM_HAS_HDWR_SAR) ? 1 : 0;
}

/**
 * pwrdm_wait_transition - wait for powerdomain power transition to finish
 * @pwrdm: struct powerdomain * to wait for
 *
 * If the powerdomain pwrdm is in the process of a state transition,
 * spin until it completes the power transition, or until an iteration
 * bailout value is reached. Returns -EINVAL if the powerdomain
 * pointer is null, -EAGAIN if the bailout value was reached, or
 * returns 0 upon success.
 */
int pwrdm_wait_transition(struct powerdomain *pwrdm)
{
	u32 c = 0;

	if (!pwrdm)
		return -EINVAL;

	/*
	 * REVISIT: pwrdm_wait_transition() may be better implemented
	 * via a callback and a periodic timer check -- how long do we expect
	 * powerdomain transitions to take?
	 */

	/* XXX Is this udelay() value meaningful? */
	while ((prm_read_mod_reg(pwrdm->prcm_offs, PM_PWSTST) &
		OMAP_INTRANSITION) &&
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

int pwrdm_state_switch(struct powerdomain *pwrdm)
{
	return _pwrdm_state_switch(pwrdm, PWRDM_STATE_NOW);
}

int pwrdm_clkdm_state_switch(struct clockdomain *clkdm)
{
	if (clkdm != NULL && clkdm->pwrdm.ptr != NULL) {
		pwrdm_wait_transition(clkdm->pwrdm.ptr);
		return pwrdm_state_switch(clkdm->pwrdm.ptr);
	}

	return -EINVAL;
}
int pwrdm_clk_state_switch(struct clk *clk)
{
	if (clk != NULL && clk->clkdm != NULL)
		return pwrdm_clkdm_state_switch(clk->clkdm);
	return -EINVAL;
}

int pwrdm_pre_transition(void)
{
	pwrdm_for_each(_pwrdm_pre_transition_cb, NULL);
	return 0;
}

int pwrdm_post_transition(void)
{
	pwrdm_for_each(_pwrdm_post_transition_cb, NULL);
	return 0;
}

