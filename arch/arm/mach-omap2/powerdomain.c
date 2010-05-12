/*
 * OMAP powerdomain control
 *
 * Copyright (C) 2007-2008 Texas Instruments, Inc.
 * Copyright (C) 2007-2009 Nokia Corporation
 *
 * Written by Paul Walmsley
 *
 * Added OMAP4 specific support by Abhijit Pagare <abhijitpagare@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#undef DEBUG

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
#include "cm-regbits-44xx.h"
#include "prm.h"
#include "prm-regbits-34xx.h"
#include "prm-regbits-44xx.h"

#include <plat/cpu.h>
#include <plat/powerdomain.h>
#include <plat/clockdomain.h>
#include <plat/prcm.h>

#include "pm.h"

enum {
	PWRDM_STATE_NOW = 0,
	PWRDM_STATE_PREV,
};

/* Variable holding value of the CPU dependent PWRSTCTRL Register Offset */
static u16 pwrstctrl_reg_offs;

/* Variable holding value of the CPU dependent PWRSTST Register Offset */
static u16 pwrstst_reg_offs;

/* OMAP3 and OMAP4 specific register bit initialisations
 * Notice that the names here are not according to each power
 * domain but the bit mapping used applies to all of them
 */

/* OMAP3 and OMAP4 Memory Onstate Masks (common across all power domains) */
#define OMAP_MEM0_ONSTATE_MASK OMAP3430_SHAREDL1CACHEFLATONSTATE_MASK
#define OMAP_MEM1_ONSTATE_MASK OMAP3430_L1FLATMEMONSTATE_MASK
#define OMAP_MEM2_ONSTATE_MASK OMAP3430_SHAREDL2CACHEFLATONSTATE_MASK
#define OMAP_MEM3_ONSTATE_MASK OMAP3430_L2FLATMEMONSTATE_MASK
#define OMAP_MEM4_ONSTATE_MASK OMAP4430_OCP_NRET_BANK_ONSTATE_MASK

/* OMAP3 and OMAP4 Memory Retstate Masks (common across all power domains) */
#define OMAP_MEM0_RETSTATE_MASK OMAP3430_SHAREDL1CACHEFLATRETSTATE
#define OMAP_MEM1_RETSTATE_MASK OMAP3430_L1FLATMEMRETSTATE
#define OMAP_MEM2_RETSTATE_MASK OMAP3430_SHAREDL2CACHEFLATRETSTATE
#define OMAP_MEM3_RETSTATE_MASK OMAP3430_L2FLATMEMRETSTATE
#define OMAP_MEM4_RETSTATE_MASK OMAP4430_OCP_NRET_BANK_RETSTATE_MASK

/* OMAP3 and OMAP4 Memory Status bits */
#define OMAP_MEM0_STATEST_MASK OMAP3430_SHAREDL1CACHEFLATSTATEST_MASK
#define OMAP_MEM1_STATEST_MASK OMAP3430_L1FLATMEMSTATEST_MASK
#define OMAP_MEM2_STATEST_MASK OMAP3430_SHAREDL2CACHEFLATSTATEST_MASK
#define OMAP_MEM3_STATEST_MASK OMAP3430_L2FLATMEMSTATEST_MASK
#define OMAP_MEM4_STATEST_MASK OMAP4430_OCP_NRET_BANK_STATEST_MASK

/* pwrdm_list contains all registered struct powerdomains */
static LIST_HEAD(pwrdm_list);

/* Private functions */

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

/**
 * _pwrdm_register - register a powerdomain
 * @pwrdm: struct powerdomain * to register
 *
 * Adds a powerdomain to the internal powerdomain list.  Returns
 * -EINVAL if given a null pointer, -EEXIST if a powerdomain is
 * already registered by the provided name, or 0 upon success.
 */
static int _pwrdm_register(struct powerdomain *pwrdm)
{
	int i;

	if (!pwrdm)
		return -EINVAL;

	if (!omap_chip_is(pwrdm->omap_chip))
		return -EINVAL;

	if (_pwrdm_lookup(pwrdm->name))
		return -EEXIST;

	list_add(&pwrdm->node, &pwrdm_list);

	/* Initialize the powerdomain's state counter */
	for (i = 0; i < PWRDM_MAX_PWRSTS; i++)
		pwrdm->state_counter[i] = 0;

	pwrdm->ret_logic_off_counter = 0;
	for (i = 0; i < pwrdm->banks; i++)
		pwrdm->ret_mem_off_counter[i] = 0;

	pwrdm_wait_transition(pwrdm);
	pwrdm->state = pwrdm_read_pwrst(pwrdm);
	pwrdm->state_counter[pwrdm->state] = 1;

	pr_debug("powerdomain: registered %s\n", pwrdm->name);

	return 0;
}

static void _update_logic_membank_counters(struct powerdomain *pwrdm)
{
	int i;
	u8 prev_logic_pwrst, prev_mem_pwrst;

	prev_logic_pwrst = pwrdm_read_prev_logic_pwrst(pwrdm);
	if ((pwrdm->pwrsts_logic_ret == PWRSTS_OFF_RET) &&
	    (prev_logic_pwrst == PWRDM_POWER_OFF))
		pwrdm->ret_logic_off_counter++;

	for (i = 0; i < pwrdm->banks; i++) {
		prev_mem_pwrst = pwrdm_read_prev_mem_pwrst(pwrdm, i);

		if ((pwrdm->pwrsts_mem_ret[i] == PWRSTS_OFF_RET) &&
		    (prev_mem_pwrst == PWRDM_POWER_OFF))
			pwrdm->ret_mem_off_counter[i]++;
	}
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
		if (prev == PWRDM_POWER_RET)
			_update_logic_membank_counters(pwrdm);
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

/* Public functions */

/**
 * pwrdm_init - set up the powerdomain layer
 * @pwrdm_list: array of struct powerdomain pointers to register
 *
 * Loop through the array of powerdomains @pwrdm_list, registering all
 * that are available on the current CPU. If pwrdm_list is supplied
 * and not null, all of the referenced powerdomains will be
 * registered.  No return value.  XXX pwrdm_list is not really a
 * "list"; it is an array.  Rename appropriately.
 */
void pwrdm_init(struct powerdomain **pwrdm_list)
{
	struct powerdomain **p = NULL;

	if (cpu_is_omap24xx() || cpu_is_omap34xx()) {
		pwrstctrl_reg_offs = OMAP2_PM_PWSTCTRL;
		pwrstst_reg_offs = OMAP2_PM_PWSTST;
	} else if (cpu_is_omap44xx()) {
		pwrstctrl_reg_offs = OMAP4_PM_PWSTCTRL;
		pwrstst_reg_offs = OMAP4_PM_PWSTST;
	} else {
		printk(KERN_ERR "Power Domain struct not supported for " \
							"this CPU\n");
		return;
	}

	if (pwrdm_list) {
		for (p = pwrdm_list; *p; p++)
			_pwrdm_register(*p);
	}
}

/**
 * pwrdm_lookup - look up a powerdomain by name, return a pointer
 * @name: name of powerdomain
 *
 * Find a registered powerdomain by its name @name.  Returns a pointer
 * to the struct powerdomain if found, or NULL otherwise.
 */
struct powerdomain *pwrdm_lookup(const char *name)
{
	struct powerdomain *pwrdm;

	if (!name)
		return NULL;

	pwrdm = _pwrdm_lookup(name);

	return pwrdm;
}

/**
 * pwrdm_for_each - call function on each registered clockdomain
 * @fn: callback function *
 *
 * Call the supplied function @fn for each registered powerdomain.
 * The callback function @fn can return anything but 0 to bail out
 * early from the iterator.  Returns the last return value of the
 * callback function, which should be 0 for success or anything else
 * to indicate failure; or -EINVAL if the function pointer is null.
 */
int pwrdm_for_each(int (*fn)(struct powerdomain *pwrdm, void *user),
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
 * pwrdm_add_clkdm - add a clockdomain to a powerdomain
 * @pwrdm: struct powerdomain * to add the clockdomain to
 * @clkdm: struct clockdomain * to associate with a powerdomain
 *
 * Associate the clockdomain @clkdm with a powerdomain @pwrdm.  This
 * enables the use of pwrdm_for_each_clkdm().  Returns -EINVAL if
 * presented with invalid pointers; -ENOMEM if memory could not be allocated;
 * or 0 upon success.
 */
int pwrdm_add_clkdm(struct powerdomain *pwrdm, struct clockdomain *clkdm)
{
	int i;
	int ret = -EINVAL;

	if (!pwrdm || !clkdm)
		return -EINVAL;

	pr_debug("powerdomain: associating clockdomain %s with powerdomain "
		 "%s\n", clkdm->name, pwrdm->name);

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
	return ret;
}

/**
 * pwrdm_del_clkdm - remove a clockdomain from a powerdomain
 * @pwrdm: struct powerdomain * to add the clockdomain to
 * @clkdm: struct clockdomain * to associate with a powerdomain
 *
 * Dissociate the clockdomain @clkdm from the powerdomain
 * @pwrdm. Returns -EINVAL if presented with invalid pointers; -ENOENT
 * if @clkdm was not associated with the powerdomain, or 0 upon
 * success.
 */
int pwrdm_del_clkdm(struct powerdomain *pwrdm, struct clockdomain *clkdm)
{
	int ret = -EINVAL;
	int i;

	if (!pwrdm || !clkdm)
		return -EINVAL;

	pr_debug("powerdomain: dissociating clockdomain %s from powerdomain "
		 "%s\n", clkdm->name, pwrdm->name);

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
	return ret;
}

/**
 * pwrdm_for_each_clkdm - call function on each clkdm in a pwrdm
 * @pwrdm: struct powerdomain * to iterate over
 * @fn: callback function *
 *
 * Call the supplied function @fn for each clockdomain in the powerdomain
 * @pwrdm.  The callback function can return anything but 0 to bail
 * out early from the iterator.  Returns -EINVAL if presented with
 * invalid pointers; or passes along the last return value of the
 * callback function, which should be 0 for success or anything else
 * to indicate failure.
 */
int pwrdm_for_each_clkdm(struct powerdomain *pwrdm,
			 int (*fn)(struct powerdomain *pwrdm,
				   struct clockdomain *clkdm))
{
	int ret = 0;
	int i;

	if (!fn)
		return -EINVAL;

	for (i = 0; i < PWRDM_MAX_CLKDMS && !ret; i++)
		ret = (*fn)(pwrdm, pwrdm->pwrdm_clkdms[i]);

	return ret;
}

/**
 * pwrdm_get_mem_bank_count - get number of memory banks in this powerdomain
 * @pwrdm: struct powerdomain *
 *
 * Return the number of controllable memory banks in powerdomain @pwrdm,
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
 * Set the powerdomain @pwrdm's next power state to @pwrst.  The powerdomain
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
			     pwrdm->prcm_offs, pwrstctrl_reg_offs);

	return 0;
}

/**
 * pwrdm_read_next_pwrst - get next powerdomain power state
 * @pwrdm: struct powerdomain * to get power state
 *
 * Return the powerdomain @pwrdm's next power state.  Returns -EINVAL
 * if the powerdomain pointer is null or returns the next power state
 * upon success.
 */
int pwrdm_read_next_pwrst(struct powerdomain *pwrdm)
{
	if (!pwrdm)
		return -EINVAL;

	return prm_read_mod_bits_shift(pwrdm->prcm_offs,
				 pwrstctrl_reg_offs, OMAP_POWERSTATE_MASK);
}

/**
 * pwrdm_read_pwrst - get current powerdomain power state
 * @pwrdm: struct powerdomain * to get power state
 *
 * Return the powerdomain @pwrdm's current power state.	Returns -EINVAL
 * if the powerdomain pointer is null or returns the current power state
 * upon success.
 */
int pwrdm_read_pwrst(struct powerdomain *pwrdm)
{
	if (!pwrdm)
		return -EINVAL;

	return prm_read_mod_bits_shift(pwrdm->prcm_offs,
				 pwrstst_reg_offs, OMAP_POWERSTATEST_MASK);
}

/**
 * pwrdm_read_prev_pwrst - get previous powerdomain power state
 * @pwrdm: struct powerdomain * to get previous power state
 *
 * Return the powerdomain @pwrdm's previous power state.  Returns -EINVAL
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
 * Set the next power state @pwrst that the logic portion of the
 * powerdomain @pwrdm will enter when the powerdomain enters retention.
 * This will be either RETENTION or OFF, if supported.  Returns
 * -EINVAL if the powerdomain pointer is null or the target power
 * state is not not supported, or returns 0 upon success.
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
				 pwrdm->prcm_offs, pwrstctrl_reg_offs);

	return 0;
}

/**
 * pwrdm_set_mem_onst - set memory power state while powerdomain ON
 * @pwrdm: struct powerdomain * to set
 * @bank: memory bank number to set (0-3)
 * @pwrst: one of the PWRDM_POWER_* macros
 *
 * Set the next power state @pwrst that memory bank @bank of the
 * powerdomain @pwrdm will enter when the powerdomain enters the ON
 * state.  @bank will be a number from 0 to 3, and represents different
 * types of memory, depending on the powerdomain.  Returns -EINVAL if
 * the powerdomain pointer is null or the target power state is not
 * not supported for this memory bank, -EEXIST if the target memory
 * bank does not exist or is not controllable, or returns 0 upon
 * success.
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
		m = OMAP_MEM0_ONSTATE_MASK;
		break;
	case 1:
		m = OMAP_MEM1_ONSTATE_MASK;
		break;
	case 2:
		m = OMAP_MEM2_ONSTATE_MASK;
		break;
	case 3:
		m = OMAP_MEM3_ONSTATE_MASK;
		break;
	case 4:
		m = OMAP_MEM4_ONSTATE_MASK;
		break;
	default:
		WARN_ON(1); /* should never happen */
		return -EEXIST;
	}

	prm_rmw_mod_reg_bits(m, (pwrst << __ffs(m)),
			     pwrdm->prcm_offs, pwrstctrl_reg_offs);

	return 0;
}

/**
 * pwrdm_set_mem_retst - set memory power state while powerdomain in RET
 * @pwrdm: struct powerdomain * to set
 * @bank: memory bank number to set (0-3)
 * @pwrst: one of the PWRDM_POWER_* macros
 *
 * Set the next power state @pwrst that memory bank @bank of the
 * powerdomain @pwrdm will enter when the powerdomain enters the
 * RETENTION state.  Bank will be a number from 0 to 3, and represents
 * different types of memory, depending on the powerdomain.  @pwrst
 * will be either RETENTION or OFF, if supported.  Returns -EINVAL if
 * the powerdomain pointer is null or the target power state is not
 * not supported for this memory bank, -EEXIST if the target memory
 * bank does not exist or is not controllable, or returns 0 upon
 * success.
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
		m = OMAP_MEM0_RETSTATE_MASK;
		break;
	case 1:
		m = OMAP_MEM1_RETSTATE_MASK;
		break;
	case 2:
		m = OMAP_MEM2_RETSTATE_MASK;
		break;
	case 3:
		m = OMAP_MEM3_RETSTATE_MASK;
		break;
	case 4:
		m = OMAP_MEM4_RETSTATE_MASK;
		break;
	default:
		WARN_ON(1); /* should never happen */
		return -EEXIST;
	}

	prm_rmw_mod_reg_bits(m, (pwrst << __ffs(m)), pwrdm->prcm_offs,
			     pwrstctrl_reg_offs);

	return 0;
}

/**
 * pwrdm_read_logic_pwrst - get current powerdomain logic retention power state
 * @pwrdm: struct powerdomain * to get current logic retention power state
 *
 * Return the power state that the logic portion of powerdomain @pwrdm
 * will enter when the powerdomain enters retention.  Returns -EINVAL
 * if the powerdomain pointer is null or returns the logic retention
 * power state upon success.
 */
int pwrdm_read_logic_pwrst(struct powerdomain *pwrdm)
{
	if (!pwrdm)
		return -EINVAL;

	return prm_read_mod_bits_shift(pwrdm->prcm_offs,
				 pwrstst_reg_offs, OMAP3430_LOGICSTATEST);
}

/**
 * pwrdm_read_prev_logic_pwrst - get previous powerdomain logic power state
 * @pwrdm: struct powerdomain * to get previous logic power state
 *
 * Return the powerdomain @pwrdm's previous logic power state.  Returns
 * -EINVAL if the powerdomain pointer is null or returns the previous
 * logic power state upon success.
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
 * pwrdm_read_logic_retst - get next powerdomain logic power state
 * @pwrdm: struct powerdomain * to get next logic power state
 *
 * Return the powerdomain pwrdm's logic power state.  Returns -EINVAL
 * if the powerdomain pointer is null or returns the next logic
 * power state upon success.
 */
int pwrdm_read_logic_retst(struct powerdomain *pwrdm)
{
	if (!pwrdm)
		return -EINVAL;

	/*
	 * The register bit names below may not correspond to the
	 * actual names of the bits in each powerdomain's register,
	 * but the type of value returned is the same for each
	 * powerdomain.
	 */
	return prm_read_mod_bits_shift(pwrdm->prcm_offs, pwrstctrl_reg_offs,
					OMAP3430_LOGICSTATEST);
}

/**
 * pwrdm_read_mem_pwrst - get current memory bank power state
 * @pwrdm: struct powerdomain * to get current memory bank power state
 * @bank: memory bank number (0-3)
 *
 * Return the powerdomain @pwrdm's current memory power state for bank
 * @bank.  Returns -EINVAL if the powerdomain pointer is null, -EEXIST if
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

	if (pwrdm->flags & PWRDM_HAS_MPU_QUIRK)
		bank = 1;

	/*
	 * The register bit names below may not correspond to the
	 * actual names of the bits in each powerdomain's register,
	 * but the type of value returned is the same for each
	 * powerdomain.
	 */
	switch (bank) {
	case 0:
		m = OMAP_MEM0_STATEST_MASK;
		break;
	case 1:
		m = OMAP_MEM1_STATEST_MASK;
		break;
	case 2:
		m = OMAP_MEM2_STATEST_MASK;
		break;
	case 3:
		m = OMAP_MEM3_STATEST_MASK;
		break;
	case 4:
		m = OMAP_MEM4_STATEST_MASK;
		break;
	default:
		WARN_ON(1); /* should never happen */
		return -EEXIST;
	}

	return prm_read_mod_bits_shift(pwrdm->prcm_offs,
					 pwrstst_reg_offs, m);
}

/**
 * pwrdm_read_prev_mem_pwrst - get previous memory bank power state
 * @pwrdm: struct powerdomain * to get previous memory bank power state
 * @bank: memory bank number (0-3)
 *
 * Return the powerdomain @pwrdm's previous memory power state for
 * bank @bank.  Returns -EINVAL if the powerdomain pointer is null,
 * -EEXIST if the target memory bank does not exist or is not
 * controllable, or returns the previous memory power state upon
 * success.
 */
int pwrdm_read_prev_mem_pwrst(struct powerdomain *pwrdm, u8 bank)
{
	u32 m;

	if (!pwrdm)
		return -EINVAL;

	if (pwrdm->banks < (bank + 1))
		return -EEXIST;

	if (pwrdm->flags & PWRDM_HAS_MPU_QUIRK)
		bank = 1;

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
 * pwrdm_read_mem_retst - get next memory bank power state
 * @pwrdm: struct powerdomain * to get mext memory bank power state
 * @bank: memory bank number (0-3)
 *
 * Return the powerdomain pwrdm's next memory power state for bank
 * x.  Returns -EINVAL if the powerdomain pointer is null, -EEXIST if
 * the target memory bank does not exist or is not controllable, or
 * returns the next memory power state upon success.
 */
int pwrdm_read_mem_retst(struct powerdomain *pwrdm, u8 bank)
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
		m = OMAP_MEM0_RETSTATE_MASK;
		break;
	case 1:
		m = OMAP_MEM1_RETSTATE_MASK;
		break;
	case 2:
		m = OMAP_MEM2_RETSTATE_MASK;
		break;
	case 3:
		m = OMAP_MEM3_RETSTATE_MASK;
		break;
	case 4:
		m = OMAP_MEM4_RETSTATE_MASK;
	default:
		WARN_ON(1); /* should never happen */
		return -EEXIST;
	}

	return prm_read_mod_bits_shift(pwrdm->prcm_offs,
					pwrstctrl_reg_offs, m);
}

/**
 * pwrdm_clear_all_prev_pwrst - clear previous powerstate register for a pwrdm
 * @pwrdm: struct powerdomain * to clear
 *
 * Clear the powerdomain's previous power state register @pwrdm.
 * Clears the entire register, including logic and memory bank
 * previous power states.  Returns -EINVAL if the powerdomain pointer
 * is null, or returns 0 upon success.
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
 * for some devices in the powerdomain @pwrdm.  Warning: this only
 * affects a subset of devices in a powerdomain; check the TRM
 * closely.  Returns -EINVAL if the powerdomain pointer is null or if
 * the powerdomain does not support automatic save-and-restore, or
 * returns 0 upon success.
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
			     pwrdm->prcm_offs, pwrstctrl_reg_offs);

	return 0;
}

/**
 * pwrdm_disable_hdwr_sar - disable automatic hardware SAR for a pwrdm
 * @pwrdm: struct powerdomain *
 *
 * Disable automatic context save-and-restore upon power state change
 * for some devices in the powerdomain @pwrdm.  Warning: this only
 * affects a subset of devices in a powerdomain; check the TRM
 * closely.  Returns -EINVAL if the powerdomain pointer is null or if
 * the powerdomain does not support automatic save-and-restore, or
 * returns 0 upon success.
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
			     pwrdm->prcm_offs, pwrstctrl_reg_offs);

	return 0;
}

/**
 * pwrdm_has_hdwr_sar - test whether powerdomain supports hardware SAR
 * @pwrdm: struct powerdomain *
 *
 * Returns 1 if powerdomain @pwrdm supports hardware save-and-restore
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
 * If the powerdomain @pwrdm is in the process of a state transition,
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
	while ((prm_read_mod_reg(pwrdm->prcm_offs, pwrstst_reg_offs) &
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

