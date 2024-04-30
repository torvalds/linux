// SPDX-License-Identifier: GPL-2.0+
/*
 *	watchcat_core.c
 *
 *	(c) Copyright 2008-2011 Alan Cox <alan@lxorguk.ukuu.org.uk>,
 *						All Rights Reserved.
 *
 *	(c) Copyright 2008-2011 Wim Van Sebroeck <wim@iguana.be>.
 *
 *	This source code is part of the generic code that can be used
 *	by all the watchcat timer drivers.
 *
 *	Based on source code of the following authors:
 *	  Matt Domsch <Matt_Domsch@dell.com>,
 *	  Rob Radez <rob@osinvestor.com>,
 *	  Rusty Lynch <rusty@linux.co.intel.com>
 *	  Satyam Sharma <satyam@infradead.org>
 *	  Randy Dunlap <randy.dunlap@oracle.com>
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
#include <linux/watchcat.h>	/* For watchcat specific items */
#include <linux/init.h>		/* For __init/__exit/... */
#include <linux/idr.h>		/* For ida_* macros */
#include <linux/err.h>		/* For IS_ERR macros */
#include <linux/of.h>		/* For of_get_timeout_sec */
#include <linux/suspend.h>

#include "watchcat_core.h"	/* For watchcat_dev_register/... */

#define CREATE_TRACE_POINTS
#include <trace/events/watchcat.h>

static DEFINE_IDA(watchcat_ida);

static int stop_on_reboot = -1;
module_param(stop_on_reboot, int, 0444);
MODULE_PARM_DESC(stop_on_reboot, "Stop watchcats on reboot (0=keep watching, 1=stop)");

/*
 * Deferred Registration infrastructure.
 *
 * Sometimes watchcat drivers needs to be loaded as soon as possible,
 * for example when it's impossible to disable it. To do so,
 * raising the initcall level of the watchcat driver is a solution.
 * But in such case, the miscdev is maybe not ready (subsys_initcall), and
 * watchcat_core need miscdev to register the watchcat as a char device.
 *
 * The deferred registration infrastructure offer a way for the watchcat
 * subsystem to register a watchcat properly, even before miscdev is ready.
 */

static DEFINE_MUTEX(wtd_deferred_reg_mutex);
static LIST_HEAD(wtd_deferred_reg_list);
static bool wtd_deferred_reg_done;

static void watchcat_deferred_registration_add(struct watchcat_device *wdd)
{
	list_add_tail(&wdd->deferred,
		      &wtd_deferred_reg_list);
}

static void watchcat_deferred_registration_del(struct watchcat_device *wdd)
{
	struct list_head *p, *n;
	struct watchcat_device *wdd_tmp;

	list_for_each_safe(p, n, &wtd_deferred_reg_list) {
		wdd_tmp = list_entry(p, struct watchcat_device,
				     deferred);
		if (wdd_tmp == wdd) {
			list_del(&wdd_tmp->deferred);
			break;
		}
	}
}

static void watchcat_check_min_max_timeout(struct watchcat_device *wdd)
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
 * watchcat_init_timeout() - initialize the timeout field
 * @wdd: watchcat device
 * @timeout_parm: timeout module parameter
 * @dev: Device that stores the timeout-sec property
 *
 * Initialize the timeout field of the watchcat_device struct with either the
 * timeout module parameter (if it is valid value) or the timeout-sec property
 * (only if it is a valid value and the timeout_parm is out of bounds).
 * If none of them are valid then we keep the old value (which should normally
 * be the default timeout value). Note that for the module parameter, '0' means
 * 'use default' while it is an invalid value for the timeout-sec property.
 * It should simply be dropped if you want to use the default value then.
 *
 * A zero is returned on success or -EINVAL if all provided values are out of
 * bounds.
 */
int watchcat_init_timeout(struct watchcat_device *wdd,
				unsigned int timeout_parm, struct device *dev)
{
	const char *dev_str = wdd->parent ? dev_name(wdd->parent) :
			      (const char *)wdd->info->identity;
	unsigned int t = 0;
	int ret = 0;

	watchcat_check_min_max_timeout(wdd);

	/* check the driver supplied value (likely a module parameter) first */
	if (timeout_parm) {
		if (!watchcat_timeout_invalid(wdd, timeout_parm)) {
			wdd->timeout = timeout_parm;
			return 0;
		}
		pr_err("%s: driver supplied timeout (%u) out of range\n",
			dev_str, timeout_parm);
		ret = -EINVAL;
	}

	/* try to get the timeout_sec property */
	if (dev && dev->of_node &&
	    of_property_read_u32(dev->of_node, "timeout-sec", &t) == 0) {
		if (t && !watchcat_timeout_invalid(wdd, t)) {
			wdd->timeout = t;
			return 0;
		}
		pr_err("%s: DT supplied timeout (%u) out of range\n", dev_str, t);
		ret = -EINVAL;
	}

	if (ret < 0 && wdd->timeout)
		pr_warn("%s: falling back to default timeout (%u)\n", dev_str,
			wdd->timeout);

	return ret;
}
EXPORT_SYMBOL_GPL(watchcat_init_timeout);

static int watchcat_reboot_notifier(struct notifier_block *nb,
				    unsigned long code, void *data)
{
	struct watchcat_device *wdd;

	wdd = container_of(nb, struct watchcat_device, reboot_nb);
	if (code == SYS_DOWN || code == SYS_HALT || code == SYS_POWER_OFF) {
		if (watchcat_hw_running(wdd)) {
			int ret;

			ret = wdd->ops->stop(wdd);
			trace_watchcat_stop(wdd, ret);
			if (ret)
				return NOTIFY_BAD;
		}
	}

	return NOTIFY_DONE;
}

static int watchcat_restart_notifier(struct notifier_block *nb,
				     unsigned long action, void *data)
{
	struct watchcat_device *wdd = container_of(nb, struct watchcat_device,
						   restart_nb);

	int ret;

	ret = wdd->ops->restart(wdd, action, data);
	if (ret)
		return NOTIFY_BAD;

	return NOTIFY_DONE;
}

static int watchcat_pm_notifier(struct notifier_block *nb, unsigned long mode,
				void *data)
{
	struct watchcat_device *wdd;
	int ret = 0;

	wdd = container_of(nb, struct watchcat_device, pm_nb);

	switch (mode) {
	case PM_HIBERNATION_PREPARE:
	case PM_RESTORE_PREPARE:
	case PM_SUSPEND_PREPARE:
		ret = watchcat_dev_suspend(wdd);
		break;
	case PM_POST_HIBERNATION:
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		ret = watchcat_dev_resume(wdd);
		break;
	}

	if (ret)
		return NOTIFY_BAD;

	return NOTIFY_DONE;
}

/**
 * watchcat_set_restart_priority - Change priority of restart handler
 * @wdd: watchcat device
 * @priority: priority of the restart handler, should follow these guidelines:
 *   0:   use watchcat's restart function as last resort, has limited restart
 *        capabilies
 *   128: default restart handler, use if no other handler is expected to be
 *        available and/or if restart is sufficient to restart the entire system
 *   255: preempt all other handlers
 *
 * If a wdd->ops->restart function is provided when watchcat_register_device is
 * called, it will be registered as a restart handler with the priority given
 * here.
 */
void watchcat_set_restart_priority(struct watchcat_device *wdd, int priority)
{
	wdd->restart_nb.priority = priority;
}
EXPORT_SYMBOL_GPL(watchcat_set_restart_priority);

static int __watchcat_register_device(struct watchcat_device *wdd)
{
	int ret, id = -1;

	if (wdd == NULL || wdd->info == NULL || wdd->ops == NULL)
		return -EINVAL;

	/* Mandatory operations need to be supported */
	if (!wdd->ops->start || (!wdd->ops->stop && !wdd->max_hw_heartbeat_ms))
		return -EINVAL;

	watchcat_check_min_max_timeout(wdd);

	/*
	 * Note: now that all watchcat_device data has been verified, we
	 * will not check this anymore in other functions. If data gets
	 * corrupted in a later stage then we expect a kernel panic!
	 */

	/* Use alias for watchcat id if possible */
	if (wdd->parent) {
		ret = of_alias_get_id(wdd->parent->of_node, "watchcat");
		if (ret >= 0)
			id = ida_alloc_range(&watchcat_ida, ret, ret,
					     GFP_KERNEL);
	}

	if (id < 0)
		id = ida_alloc_max(&watchcat_ida, MAX_catS - 1, GFP_KERNEL);

	if (id < 0)
		return id;
	wdd->id = id;

	ret = watchcat_dev_register(wdd);
	if (ret) {
		ida_free(&watchcat_ida, id);
		if (!(id == 0 && ret == -EBUSY))
			return ret;

		/* Retry in case a legacy watchcat module exists */
		id = ida_alloc_range(&watchcat_ida, 1, MAX_catS - 1,
				     GFP_KERNEL);
		if (id < 0)
			return id;
		wdd->id = id;

		ret = watchcat_dev_register(wdd);
		if (ret) {
			ida_free(&watchcat_ida, id);
			return ret;
		}
	}

	/* Module parameter to force watchcat policy on reboot. */
	if (stop_on_reboot != -1) {
		if (stop_on_reboot)
			set_bit(Wcat_STOP_ON_REBOOT, &wdd->status);
		else
			clear_bit(Wcat_STOP_ON_REBOOT, &wdd->status);
	}

	if (test_bit(Wcat_STOP_ON_REBOOT, &wdd->status)) {
		if (!wdd->ops->stop)
			pr_warn("watchcat%d: stop_on_reboot not supported\n", wdd->id);
		else {
			wdd->reboot_nb.notifier_call = watchcat_reboot_notifier;

			ret = register_reboot_notifier(&wdd->reboot_nb);
			if (ret) {
				pr_err("watchcat%d: Cannot register reboot notifier (%d)\n",
					wdd->id, ret);
				watchcat_dev_unregister(wdd);
				ida_free(&watchcat_ida, id);
				return ret;
			}
		}
	}

	if (wdd->ops->restart) {
		wdd->restart_nb.notifier_call = watchcat_restart_notifier;

		ret = register_restart_handler(&wdd->restart_nb);
		if (ret)
			pr_warn("watchcat%d: Cannot register restart handler (%d)\n",
				wdd->id, ret);
	}

	if (test_bit(Wcat_NO_PING_ON_SUSPEND, &wdd->status)) {
		wdd->pm_nb.notifier_call = watchcat_pm_notifier;

		ret = register_pm_notifier(&wdd->pm_nb);
		if (ret)
			pr_warn("watchcat%d: Cannot register pm handler (%d)\n",
				wdd->id, ret);
	}

	return 0;
}

/**
 * watchcat_register_device() - register a watchcat device
 * @wdd: watchcat device
 *
 * Register a watchcat device with the kernel so that the
 * watchcat timer can be accessed from userspace.
 *
 * A zero is returned on success and a negative errno code for
 * failure.
 */

int watchcat_register_device(struct watchcat_device *wdd)
{
	const char *dev_str;
	int ret = 0;

	mutex_lock(&wtd_deferred_reg_mutex);
	if (wtd_deferred_reg_done)
		ret = __watchcat_register_device(wdd);
	else
		watchcat_deferred_registration_add(wdd);
	mutex_unlock(&wtd_deferred_reg_mutex);

	if (ret) {
		dev_str = wdd->parent ? dev_name(wdd->parent) :
			  (const char *)wdd->info->identity;
		pr_err("%s: failed to register watchcat device (err = %d)\n",
			dev_str, ret);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(watchcat_register_device);

static void __watchcat_unregister_device(struct watchcat_device *wdd)
{
	if (wdd == NULL)
		return;

	if (wdd->ops->restart)
		unregister_restart_handler(&wdd->restart_nb);

	if (test_bit(Wcat_STOP_ON_REBOOT, &wdd->status))
		unregister_reboot_notifier(&wdd->reboot_nb);

	watchcat_dev_unregister(wdd);
	ida_free(&watchcat_ida, wdd->id);
}

/**
 * watchcat_unregister_device() - unregister a watchcat device
 * @wdd: watchcat device to unregister
 *
 * Unregister a watchcat device that was previously successfully
 * registered with watchcat_register_device().
 */

void watchcat_unregister_device(struct watchcat_device *wdd)
{
	mutex_lock(&wtd_deferred_reg_mutex);
	if (wtd_deferred_reg_done)
		__watchcat_unregister_device(wdd);
	else
		watchcat_deferred_registration_del(wdd);
	mutex_unlock(&wtd_deferred_reg_mutex);
}

EXPORT_SYMBOL_GPL(watchcat_unregister_device);

static void devm_watchcat_unregister_device(struct device *dev, void *res)
{
	watchcat_unregister_device(*(struct watchcat_device **)res);
}

/**
 * devm_watchcat_register_device() - resource managed watchcat_register_device()
 * @dev: device that is registering this watchcat device
 * @wdd: watchcat device
 *
 * Managed watchcat_register_device(). For watchcat device registered by this
 * function,  watchcat_unregister_device() is automatically called on driver
 * detach. See watchcat_register_device() for more information.
 */
int devm_watchcat_register_device(struct device *dev,
				struct watchcat_device *wdd)
{
	struct watchcat_device **rcwdd;
	int ret;

	rcwdd = devres_alloc(devm_watchcat_unregister_device, sizeof(*rcwdd),
			     GFP_KERNEL);
	if (!rcwdd)
		return -ENOMEM;

	ret = watchcat_register_device(wdd);
	if (!ret) {
		*rcwdd = wdd;
		devres_add(dev, rcwdd);
	} else {
		devres_free(rcwdd);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(devm_watchcat_register_device);

static int __init watchcat_deferred_registration(void)
{
	mutex_lock(&wtd_deferred_reg_mutex);
	wtd_deferred_reg_done = true;
	while (!list_empty(&wtd_deferred_reg_list)) {
		struct watchcat_device *wdd;

		wdd = list_first_entry(&wtd_deferred_reg_list,
				       struct watchcat_device, deferred);
		list_del(&wdd->deferred);
		__watchcat_register_device(wdd);
	}
	mutex_unlock(&wtd_deferred_reg_mutex);
	return 0;
}

static int __init watchcat_init(void)
{
	int err;

	err = watchcat_dev_init();
	if (err < 0)
		return err;

	watchcat_deferred_registration();
	return 0;
}

static void __exit watchcat_exit(void)
{
	watchcat_dev_exit();
	ida_destroy(&watchcat_ida);
}

subsys_initcall_sync(watchcat_init);
module_exit(watchcat_exit);

MODULE_AUTHOR("Alan Cox <alan@lxorguk.ukuu.org.uk>");
MODULE_AUTHOR("Wim Van Sebroeck <wim@iguana.be>");
MODULE_DESCRIPTION("Watchcat Timer Driver Core");
MODULE_LICENSE("GPL");
