/*
 *	watchdog_core.c
 *
 *	(c) Copyright 2008-2011 Alan Cox <alan@lxorguk.ukuu.org.uk>,
 *						All Rights Reserved.
 *
 *	(c) Copyright 2008-2011 Wim Van Sebroeck <wim@iguana.be>.
 *
 *	This source code is part of the generic code that can be used
 *	by all the watchdog timer drivers.
 *
 *	Based on source code of the following authors:
 *	  Matt Domsch <Matt_Domsch@dell.com>,
 *	  Rob Radez <rob@osinvestor.com>,
 *	  Rusty Lynch <rusty@linux.co.intel.com>
 *	  Satyam Sharma <satyam@infradead.org>
 *	  Randy Dunlap <randy.dunlap@oracle.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Neither Alan Cox, CymruNet Ltd., Wim Van Sebroeck nor Iguana vzw.
 *	admit liability nor provide warranty for any of this software.
 *	This material is provided "AS-IS" and at no charge.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>	/* For EXPORT_SYMBOL/module stuff/... */
#include <linux/types.h>	/* For standard types */
#include <linux/errno.h>	/* For the -ENODEV/... values */
#include <linux/kernel.h>	/* For printk/panic/... */
#include <linux/reboot.h>	/* For restart handler */
#include <linux/watchdog.h>	/* For watchdog specific items */
#include <linux/init.h>		/* For __init/__exit/... */
#include <linux/idr.h>		/* For ida_* macros */
#include <linux/err.h>		/* For IS_ERR macros */
#include <linux/of.h>		/* For of_get_timeout_sec */

#include "watchdog_core.h"	/* For watchdog_dev_register/... */

static DEFINE_IDA(watchdog_ida);

/*
 * Deferred Registration infrastructure.
 *
 * Sometimes watchdog drivers needs to be loaded as soon as possible,
 * for example when it's impossible to disable it. To do so,
 * raising the initcall level of the watchdog driver is a solution.
 * But in such case, the miscdev is maybe not ready (subsys_initcall), and
 * watchdog_core need miscdev to register the watchdog as a char device.
 *
 * The deferred registration infrastructure offer a way for the watchdog
 * subsystem to register a watchdog properly, even before miscdev is ready.
 */

static DEFINE_MUTEX(wtd_deferred_reg_mutex);
static LIST_HEAD(wtd_deferred_reg_list);
static bool wtd_deferred_reg_done;

static int watchdog_deferred_registration_add(struct watchdog_device *wdd)
{
	list_add_tail(&wdd->deferred,
		      &wtd_deferred_reg_list);
	return 0;
}

static void watchdog_deferred_registration_del(struct watchdog_device *wdd)
{
	struct list_head *p, *n;
	struct watchdog_device *wdd_tmp;

	list_for_each_safe(p, n, &wtd_deferred_reg_list) {
		wdd_tmp = list_entry(p, struct watchdog_device,
				     deferred);
		if (wdd_tmp == wdd) {
			list_del(&wdd_tmp->deferred);
			break;
		}
	}
}

static void watchdog_check_min_max_timeout(struct watchdog_device *wdd)
{
	/*
	 * Check that we have valid min and max timeout values, if
	 * not reset them both to 0 (=not used or unknown)
	 */
	if (!wdd->max_hw_heartbeat_ms && wdd->min_timeout > wdd->max_timeout) {
		pr_info("Invalid min and max timeout values, resetting to 0!\n");
		wdd->min_timeout = 0;
		wdd->max_timeout = 0;
	}
}

/**
 * watchdog_init_timeout() - initialize the timeout field
 * @timeout_parm: timeout module parameter
 * @dev: Device that stores the timeout-sec property
 *
 * Initialize the timeout field of the watchdog_device struct with either the
 * timeout module parameter (if it is valid value) or the timeout-sec property
 * (only if it is a valid value and the timeout_parm is out of bounds).
 * If none of them are valid then we keep the old value (which should normally
 * be the default timeout value).
 *
 * A zero is returned on success and -EINVAL for failure.
 */
int watchdog_init_timeout(struct watchdog_device *wdd,
				unsigned int timeout_parm, struct device *dev)
{
	unsigned int t = 0;
	int ret = 0;

	watchdog_check_min_max_timeout(wdd);

	/* try to get the timeout module parameter first */
	if (!watchdog_timeout_invalid(wdd, timeout_parm) && timeout_parm) {
		wdd->timeout = timeout_parm;
		return ret;
	}
	if (timeout_parm)
		ret = -EINVAL;

	/* try to get the timeout_sec property */
	if (dev == NULL || dev->of_node == NULL)
		return ret;
	of_property_read_u32(dev->of_node, "timeout-sec", &t);
	if (!watchdog_timeout_invalid(wdd, t) && t)
		wdd->timeout = t;
	else
		ret = -EINVAL;

	return ret;
}
EXPORT_SYMBOL_GPL(watchdog_init_timeout);

static int watchdog_reboot_notifier(struct notifier_block *nb,
				    unsigned long code, void *data)
{
	struct watchdog_device *wdd = container_of(nb, struct watchdog_device,
						   reboot_nb);

	if (code == SYS_DOWN || code == SYS_HALT) {
		if (watchdog_active(wdd)) {
			int ret;

			ret = wdd->ops->stop(wdd);
			if (ret)
				return NOTIFY_BAD;
		}
	}

	return NOTIFY_DONE;
}

static int watchdog_restart_notifier(struct notifier_block *nb,
				     unsigned long action, void *data)
{
	struct watchdog_device *wdd = container_of(nb, struct watchdog_device,
						   restart_nb);

	int ret;

	ret = wdd->ops->restart(wdd, action, data);
	if (ret)
		return NOTIFY_BAD;

	return NOTIFY_DONE;
}

/**
 * watchdog_set_restart_priority - Change priority of restart handler
 * @wdd: watchdog device
 * @priority: priority of the restart handler, should follow these guidelines:
 *   0:   use watchdog's restart function as last resort, has limited restart
 *        capabilies
 *   128: default restart handler, use if no other handler is expected to be
 *        available and/or if restart is sufficient to restart the entire system
 *   255: preempt all other handlers
 *
 * If a wdd->ops->restart function is provided when watchdog_register_device is
 * called, it will be registered as a restart handler with the priority given
 * here.
 */
void watchdog_set_restart_priority(struct watchdog_device *wdd, int priority)
{
	wdd->restart_nb.priority = priority;
}
EXPORT_SYMBOL_GPL(watchdog_set_restart_priority);

static int __watchdog_register_device(struct watchdog_device *wdd)
{
	int ret, id = -1;

	if (wdd == NULL || wdd->info == NULL || wdd->ops == NULL)
		return -EINVAL;

	/* Mandatory operations need to be supported */
	if (!wdd->ops->start || (!wdd->ops->stop && !wdd->max_hw_heartbeat_ms))
		return -EINVAL;

	watchdog_check_min_max_timeout(wdd);

	/*
	 * Note: now that all watchdog_device data has been verified, we
	 * will not check this anymore in other functions. If data gets
	 * corrupted in a later stage then we expect a kernel panic!
	 */

	/* Use alias for watchdog id if possible */
	if (wdd->parent) {
		ret = of_alias_get_id(wdd->parent->of_node, "watchdog");
		if (ret >= 0)
			id = ida_simple_get(&watchdog_ida, ret,
					    ret + 1, GFP_KERNEL);
	}

	if (id < 0)
		id = ida_simple_get(&watchdog_ida, 0, MAX_DOGS, GFP_KERNEL);

	if (id < 0)
		return id;
	wdd->id = id;

	ret = watchdog_dev_register(wdd);
	if (ret) {
		ida_simple_remove(&watchdog_ida, id);
		if (!(id == 0 && ret == -EBUSY))
			return ret;

		/* Retry in case a legacy watchdog module exists */
		id = ida_simple_get(&watchdog_ida, 1, MAX_DOGS, GFP_KERNEL);
		if (id < 0)
			return id;
		wdd->id = id;

		ret = watchdog_dev_register(wdd);
		if (ret) {
			ida_simple_remove(&watchdog_ida, id);
			return ret;
		}
	}

	if (test_bit(WDOG_STOP_ON_REBOOT, &wdd->status)) {
		wdd->reboot_nb.notifier_call = watchdog_reboot_notifier;

		ret = register_reboot_notifier(&wdd->reboot_nb);
		if (ret) {
			pr_err("watchdog%d: Cannot register reboot notifier (%d)\n",
			       wdd->id, ret);
			watchdog_dev_unregister(wdd);
			ida_simple_remove(&watchdog_ida, wdd->id);
			return ret;
		}
	}

	if (wdd->ops->restart) {
		wdd->restart_nb.notifier_call = watchdog_restart_notifier;

		ret = register_restart_handler(&wdd->restart_nb);
		if (ret)
			pr_warn("watchdog%d: Cannot register restart handler (%d)\n",
				wdd->id, ret);
	}

	return 0;
}

/**
 * watchdog_register_device() - register a watchdog device
 * @wdd: watchdog device
 *
 * Register a watchdog device with the kernel so that the
 * watchdog timer can be accessed from userspace.
 *
 * A zero is returned on success and a negative errno code for
 * failure.
 */

int watchdog_register_device(struct watchdog_device *wdd)
{
	int ret;

	mutex_lock(&wtd_deferred_reg_mutex);
	if (wtd_deferred_reg_done)
		ret = __watchdog_register_device(wdd);
	else
		ret = watchdog_deferred_registration_add(wdd);
	mutex_unlock(&wtd_deferred_reg_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(watchdog_register_device);

static void __watchdog_unregister_device(struct watchdog_device *wdd)
{
	if (wdd == NULL)
		return;

	if (wdd->ops->restart)
		unregister_restart_handler(&wdd->restart_nb);

	if (test_bit(WDOG_STOP_ON_REBOOT, &wdd->status))
		unregister_reboot_notifier(&wdd->reboot_nb);

	watchdog_dev_unregister(wdd);
	ida_simple_remove(&watchdog_ida, wdd->id);
}

/**
 * watchdog_unregister_device() - unregister a watchdog device
 * @wdd: watchdog device to unregister
 *
 * Unregister a watchdog device that was previously successfully
 * registered with watchdog_register_device().
 */

void watchdog_unregister_device(struct watchdog_device *wdd)
{
	mutex_lock(&wtd_deferred_reg_mutex);
	if (wtd_deferred_reg_done)
		__watchdog_unregister_device(wdd);
	else
		watchdog_deferred_registration_del(wdd);
	mutex_unlock(&wtd_deferred_reg_mutex);
}

EXPORT_SYMBOL_GPL(watchdog_unregister_device);

static void devm_watchdog_unregister_device(struct device *dev, void *res)
{
	watchdog_unregister_device(*(struct watchdog_device **)res);
}

/**
 * devm_watchdog_register_device() - resource managed watchdog_register_device()
 * @dev: device that is registering this watchdog device
 * @wdd: watchdog device
 *
 * Managed watchdog_register_device(). For watchdog device registered by this
 * function,  watchdog_unregister_device() is automatically called on driver
 * detach. See watchdog_register_device() for more information.
 */
int devm_watchdog_register_device(struct device *dev,
				struct watchdog_device *wdd)
{
	struct watchdog_device **rcwdd;
	int ret;

	rcwdd = devres_alloc(devm_watchdog_unregister_device, sizeof(*wdd),
			     GFP_KERNEL);
	if (!rcwdd)
		return -ENOMEM;

	ret = watchdog_register_device(wdd);
	if (!ret) {
		*rcwdd = wdd;
		devres_add(dev, rcwdd);
	} else {
		devres_free(rcwdd);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(devm_watchdog_register_device);

static int __init watchdog_deferred_registration(void)
{
	mutex_lock(&wtd_deferred_reg_mutex);
	wtd_deferred_reg_done = true;
	while (!list_empty(&wtd_deferred_reg_list)) {
		struct watchdog_device *wdd;

		wdd = list_first_entry(&wtd_deferred_reg_list,
				       struct watchdog_device, deferred);
		list_del(&wdd->deferred);
		__watchdog_register_device(wdd);
	}
	mutex_unlock(&wtd_deferred_reg_mutex);
	return 0;
}

static int __init watchdog_init(void)
{
	int err;

	err = watchdog_dev_init();
	if (err < 0)
		return err;

	watchdog_deferred_registration();
	return 0;
}

static void __exit watchdog_exit(void)
{
	watchdog_dev_exit();
	ida_destroy(&watchdog_ida);
}

subsys_initcall_sync(watchdog_init);
module_exit(watchdog_exit);

MODULE_AUTHOR("Alan Cox <alan@lxorguk.ukuu.org.uk>");
MODULE_AUTHOR("Wim Van Sebroeck <wim@iguana.be>");
MODULE_DESCRIPTION("WatchDog Timer Driver Core");
MODULE_LICENSE("GPL");
