/*
 * drivers/base/power/domain.c - Common code related to device power domains.
 *
 * Copyright (C) 2011 Rafael J. Wysocki <rjw@sisk.pl>, Renesas Electronics Corp.
 *
 * This file is released under the GPLv2.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <linux/slab.h>
#include <linux/err.h>

#ifdef CONFIG_PM

static struct generic_pm_domain *dev_to_genpd(struct device *dev)
{
	if (IS_ERR_OR_NULL(dev->pm_domain))
		return ERR_PTR(-EINVAL);

	return pd_to_genpd(dev->pm_domain);
}

static void genpd_sd_counter_dec(struct generic_pm_domain *genpd)
{
	if (!WARN_ON(genpd->sd_count == 0))
			genpd->sd_count--;
}

/**
 * pm_genpd_poweron - Restore power to a given PM domain and its parents.
 * @genpd: PM domain to power up.
 *
 * Restore power to @genpd and all of its parents so that it is possible to
 * resume a device belonging to it.
 */
int pm_genpd_poweron(struct generic_pm_domain *genpd)
{
	int ret = 0;

 start:
	if (genpd->parent)
		mutex_lock(&genpd->parent->lock);
	mutex_lock_nested(&genpd->lock, SINGLE_DEPTH_NESTING);

	if (!genpd->power_is_off
	    || (genpd->prepared_count > 0 && genpd->suspend_power_off))
		goto out;

	if (genpd->parent && genpd->parent->power_is_off) {
		mutex_unlock(&genpd->lock);
		mutex_unlock(&genpd->parent->lock);

		ret = pm_genpd_poweron(genpd->parent);
		if (ret)
			return ret;

		goto start;
	}

	if (genpd->power_on) {
		int ret = genpd->power_on(genpd);
		if (ret)
			goto out;
	}

	genpd->power_is_off = false;
	if (genpd->parent)
		genpd->parent->sd_count++;

 out:
	mutex_unlock(&genpd->lock);
	if (genpd->parent)
		mutex_unlock(&genpd->parent->lock);

	return ret;
}

#endif /* CONFIG_PM */

#ifdef CONFIG_PM_RUNTIME

/**
 * __pm_genpd_save_device - Save the pre-suspend state of a device.
 * @dle: Device list entry of the device to save the state of.
 * @genpd: PM domain the device belongs to.
 */
static int __pm_genpd_save_device(struct dev_list_entry *dle,
				  struct generic_pm_domain *genpd)
{
	struct device *dev = dle->dev;
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (dle->need_restore)
		return 0;

	if (drv && drv->pm && drv->pm->runtime_suspend) {
		if (genpd->start_device)
			genpd->start_device(dev);

		ret = drv->pm->runtime_suspend(dev);

		if (genpd->stop_device)
			genpd->stop_device(dev);
	}

	if (!ret)
		dle->need_restore = true;

	return ret;
}

/**
 * __pm_genpd_restore_device - Restore the pre-suspend state of a device.
 * @dle: Device list entry of the device to restore the state of.
 * @genpd: PM domain the device belongs to.
 */
static void __pm_genpd_restore_device(struct dev_list_entry *dle,
				      struct generic_pm_domain *genpd)
{
	struct device *dev = dle->dev;
	struct device_driver *drv = dev->driver;

	if (!dle->need_restore)
		return;

	if (drv && drv->pm && drv->pm->runtime_resume) {
		if (genpd->start_device)
			genpd->start_device(dev);

		drv->pm->runtime_resume(dev);

		if (genpd->stop_device)
			genpd->stop_device(dev);
	}

	dle->need_restore = false;
}

/**
 * pm_genpd_poweroff - Remove power from a given PM domain.
 * @genpd: PM domain to power down.
 *
 * If all of the @genpd's devices have been suspended and all of its subdomains
 * have been powered down, run the runtime suspend callbacks provided by all of
 * the @genpd's devices' drivers and remove power from @genpd.
 */
static int pm_genpd_poweroff(struct generic_pm_domain *genpd)
{
	struct generic_pm_domain *parent;
	struct dev_list_entry *dle;
	unsigned int not_suspended;
	int ret;

	if (genpd->power_is_off || genpd->prepared_count > 0)
		return 0;

	if (genpd->sd_count > 0)
		return -EBUSY;

	not_suspended = 0;
	list_for_each_entry(dle, &genpd->dev_list, node)
		if (dle->dev->driver && !pm_runtime_suspended(dle->dev))
			not_suspended++;

	if (not_suspended > genpd->in_progress)
		return -EBUSY;

	if (genpd->gov && genpd->gov->power_down_ok) {
		if (!genpd->gov->power_down_ok(&genpd->domain))
			return -EAGAIN;
	}

	list_for_each_entry_reverse(dle, &genpd->dev_list, node) {
		ret = __pm_genpd_save_device(dle, genpd);
		if (ret)
			goto err_dev;
	}

	if (genpd->power_off)
		genpd->power_off(genpd);

	genpd->power_is_off = true;

	parent = genpd->parent;
	if (parent) {
		genpd_sd_counter_dec(parent);
		if (parent->sd_count == 0)
			queue_work(pm_wq, &parent->power_off_work);
	}

	return 0;

 err_dev:
	list_for_each_entry_continue(dle, &genpd->dev_list, node)
		__pm_genpd_restore_device(dle, genpd);

	return ret;
}

/**
 * genpd_power_off_work_fn - Power off PM domain whose subdomain count is 0.
 * @work: Work structure used for scheduling the execution of this function.
 */
static void genpd_power_off_work_fn(struct work_struct *work)
{
	struct generic_pm_domain *genpd;

	genpd = container_of(work, struct generic_pm_domain, power_off_work);

	if (genpd->parent)
		mutex_lock(&genpd->parent->lock);
	mutex_lock_nested(&genpd->lock, SINGLE_DEPTH_NESTING);
	pm_genpd_poweroff(genpd);
	mutex_unlock(&genpd->lock);
	if (genpd->parent)
		mutex_unlock(&genpd->parent->lock);
}

/**
 * pm_genpd_runtime_suspend - Suspend a device belonging to I/O PM domain.
 * @dev: Device to suspend.
 *
 * Carry out a runtime suspend of a device under the assumption that its
 * pm_domain field points to the domain member of an object of type
 * struct generic_pm_domain representing a PM domain consisting of I/O devices.
 */
static int pm_genpd_runtime_suspend(struct device *dev)
{
	struct generic_pm_domain *genpd;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	if (genpd->parent)
		mutex_lock(&genpd->parent->lock);
	mutex_lock_nested(&genpd->lock, SINGLE_DEPTH_NESTING);

	if (genpd->stop_device) {
		int ret = genpd->stop_device(dev);
		if (ret)
			goto out;
	}
	genpd->in_progress++;
	pm_genpd_poweroff(genpd);
	genpd->in_progress--;

 out:
	mutex_unlock(&genpd->lock);
	if (genpd->parent)
		mutex_unlock(&genpd->parent->lock);

	return 0;
}

/**
 * __pm_genpd_runtime_resume - Resume a device belonging to I/O PM domain.
 * @dev: Device to resume.
 * @genpd: PM domain the device belongs to.
 */
static void __pm_genpd_runtime_resume(struct device *dev,
				      struct generic_pm_domain *genpd)
{
	struct dev_list_entry *dle;

	list_for_each_entry(dle, &genpd->dev_list, node) {
		if (dle->dev == dev) {
			__pm_genpd_restore_device(dle, genpd);
			break;
		}
	}

	if (genpd->start_device)
		genpd->start_device(dev);
}

/**
 * pm_genpd_runtime_resume - Resume a device belonging to I/O PM domain.
 * @dev: Device to resume.
 *
 * Carry out a runtime resume of a device under the assumption that its
 * pm_domain field points to the domain member of an object of type
 * struct generic_pm_domain representing a PM domain consisting of I/O devices.
 */
static int pm_genpd_runtime_resume(struct device *dev)
{
	struct generic_pm_domain *genpd;
	int ret;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	ret = pm_genpd_poweron(genpd);
	if (ret)
		return ret;

	mutex_lock(&genpd->lock);
	__pm_genpd_runtime_resume(dev, genpd);
	mutex_unlock(&genpd->lock);

	return 0;
}

#else

static inline void genpd_power_off_work_fn(struct work_struct *work) {}
static inline void __pm_genpd_runtime_resume(struct device *dev,
					     struct generic_pm_domain *genpd) {}

#define pm_genpd_runtime_suspend	NULL
#define pm_genpd_runtime_resume		NULL

#endif /* CONFIG_PM_RUNTIME */

#ifdef CONFIG_PM_SLEEP

/**
 * pm_genpd_sync_poweroff - Synchronously power off a PM domain and its parents.
 * @genpd: PM domain to power off, if possible.
 *
 * Check if the given PM domain can be powered off (during system suspend or
 * hibernation) and do that if so.  Also, in that case propagate to its parent.
 *
 * This function is only called in "noirq" stages of system power transitions,
 * so it need not acquire locks (all of the "noirq" callbacks are executed
 * sequentially, so it is guaranteed that it will never run twice in parallel).
 */
static void pm_genpd_sync_poweroff(struct generic_pm_domain *genpd)
{
	struct generic_pm_domain *parent = genpd->parent;

	if (genpd->power_is_off)
		return;

	if (genpd->suspended_count != genpd->device_count || genpd->sd_count > 0)
		return;

	if (genpd->power_off)
		genpd->power_off(genpd);

	genpd->power_is_off = true;
	if (parent) {
		genpd_sd_counter_dec(parent);
		pm_genpd_sync_poweroff(parent);
	}
}

/**
 * pm_genpd_prepare - Start power transition of a device in a PM domain.
 * @dev: Device to start the transition of.
 *
 * Start a power transition of a device (during a system-wide power transition)
 * under the assumption that its pm_domain field points to the domain member of
 * an object of type struct generic_pm_domain representing a PM domain
 * consisting of I/O devices.
 */
static int pm_genpd_prepare(struct device *dev)
{
	struct generic_pm_domain *genpd;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	mutex_lock(&genpd->lock);

	if (genpd->prepared_count++ == 0)
		genpd->suspend_power_off = genpd->power_is_off;

	if (genpd->suspend_power_off) {
		mutex_unlock(&genpd->lock);
		return 0;
	}

	/*
	 * If the device is in the (runtime) "suspended" state, call
	 * .start_device() for it, if defined.
	 */
	if (pm_runtime_suspended(dev))
		__pm_genpd_runtime_resume(dev, genpd);

	/*
	 * Do not check if runtime resume is pending at this point, because it
	 * has been taken care of already and if pm_genpd_poweron() ran at this
	 * point as a result of the check, it would deadlock.
	 */
	__pm_runtime_disable(dev, false);

	mutex_unlock(&genpd->lock);

	return pm_generic_prepare(dev);
}

/**
 * pm_genpd_suspend - Suspend a device belonging to an I/O PM domain.
 * @dev: Device to suspend.
 *
 * Suspend a device under the assumption that its pm_domain field points to the
 * domain member of an object of type struct generic_pm_domain representing
 * a PM domain consisting of I/O devices.
 */
static int pm_genpd_suspend(struct device *dev)
{
	struct generic_pm_domain *genpd;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	return genpd->suspend_power_off ? 0 : pm_generic_suspend(dev);
}

/**
 * pm_genpd_suspend_noirq - Late suspend of a device from an I/O PM domain.
 * @dev: Device to suspend.
 *
 * Carry out a late suspend of a device under the assumption that its
 * pm_domain field points to the domain member of an object of type
 * struct generic_pm_domain representing a PM domain consisting of I/O devices.
 */
static int pm_genpd_suspend_noirq(struct device *dev)
{
	struct generic_pm_domain *genpd;
	int ret;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	if (genpd->suspend_power_off)
		return 0;

	ret = pm_generic_suspend_noirq(dev);
	if (ret)
		return ret;

	if (device_may_wakeup(dev)
	    && genpd->active_wakeup && genpd->active_wakeup(dev))
		return 0;

	if (genpd->stop_device)
		genpd->stop_device(dev);

	/*
	 * Since all of the "noirq" callbacks are executed sequentially, it is
	 * guaranteed that this function will never run twice in parallel for
	 * the same PM domain, so it is not necessary to use locking here.
	 */
	genpd->suspended_count++;
	pm_genpd_sync_poweroff(genpd);

	return 0;
}

/**
 * pm_genpd_resume_noirq - Early resume of a device from an I/O power domain.
 * @dev: Device to resume.
 *
 * Carry out an early resume of a device under the assumption that its
 * pm_domain field points to the domain member of an object of type
 * struct generic_pm_domain representing a power domain consisting of I/O
 * devices.
 */
static int pm_genpd_resume_noirq(struct device *dev)
{
	struct generic_pm_domain *genpd;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	if (genpd->suspend_power_off)
		return 0;

	/*
	 * Since all of the "noirq" callbacks are executed sequentially, it is
	 * guaranteed that this function will never run twice in parallel for
	 * the same PM domain, so it is not necessary to use locking here.
	 */
	pm_genpd_poweron(genpd);
	genpd->suspended_count--;
	if (genpd->start_device)
		genpd->start_device(dev);

	return pm_generic_resume_noirq(dev);
}

/**
 * pm_genpd_resume - Resume a device belonging to an I/O power domain.
 * @dev: Device to resume.
 *
 * Resume a device under the assumption that its pm_domain field points to the
 * domain member of an object of type struct generic_pm_domain representing
 * a power domain consisting of I/O devices.
 */
static int pm_genpd_resume(struct device *dev)
{
	struct generic_pm_domain *genpd;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	return genpd->suspend_power_off ? 0 : pm_generic_resume(dev);
}

/**
 * pm_genpd_freeze - Freeze a device belonging to an I/O power domain.
 * @dev: Device to freeze.
 *
 * Freeze a device under the assumption that its pm_domain field points to the
 * domain member of an object of type struct generic_pm_domain representing
 * a power domain consisting of I/O devices.
 */
static int pm_genpd_freeze(struct device *dev)
{
	struct generic_pm_domain *genpd;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	return genpd->suspend_power_off ? 0 : pm_generic_freeze(dev);
}

/**
 * pm_genpd_freeze_noirq - Late freeze of a device from an I/O power domain.
 * @dev: Device to freeze.
 *
 * Carry out a late freeze of a device under the assumption that its
 * pm_domain field points to the domain member of an object of type
 * struct generic_pm_domain representing a power domain consisting of I/O
 * devices.
 */
static int pm_genpd_freeze_noirq(struct device *dev)
{
	struct generic_pm_domain *genpd;
	int ret;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	if (genpd->suspend_power_off)
		return 0;

	ret = pm_generic_freeze_noirq(dev);
	if (ret)
		return ret;

	if (genpd->stop_device)
		genpd->stop_device(dev);

	return 0;
}

/**
 * pm_genpd_thaw_noirq - Early thaw of a device from an I/O power domain.
 * @dev: Device to thaw.
 *
 * Carry out an early thaw of a device under the assumption that its
 * pm_domain field points to the domain member of an object of type
 * struct generic_pm_domain representing a power domain consisting of I/O
 * devices.
 */
static int pm_genpd_thaw_noirq(struct device *dev)
{
	struct generic_pm_domain *genpd;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	if (genpd->suspend_power_off)
		return 0;

	if (genpd->start_device)
		genpd->start_device(dev);

	return pm_generic_thaw_noirq(dev);
}

/**
 * pm_genpd_thaw - Thaw a device belonging to an I/O power domain.
 * @dev: Device to thaw.
 *
 * Thaw a device under the assumption that its pm_domain field points to the
 * domain member of an object of type struct generic_pm_domain representing
 * a power domain consisting of I/O devices.
 */
static int pm_genpd_thaw(struct device *dev)
{
	struct generic_pm_domain *genpd;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	return genpd->suspend_power_off ? 0 : pm_generic_thaw(dev);
}

/**
 * pm_genpd_dev_poweroff - Power off a device belonging to an I/O PM domain.
 * @dev: Device to suspend.
 *
 * Power off a device under the assumption that its pm_domain field points to
 * the domain member of an object of type struct generic_pm_domain representing
 * a PM domain consisting of I/O devices.
 */
static int pm_genpd_dev_poweroff(struct device *dev)
{
	struct generic_pm_domain *genpd;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	return genpd->suspend_power_off ? 0 : pm_generic_poweroff(dev);
}

/**
 * pm_genpd_dev_poweroff_noirq - Late power off of a device from a PM domain.
 * @dev: Device to suspend.
 *
 * Carry out a late powering off of a device under the assumption that its
 * pm_domain field points to the domain member of an object of type
 * struct generic_pm_domain representing a PM domain consisting of I/O devices.
 */
static int pm_genpd_dev_poweroff_noirq(struct device *dev)
{
	struct generic_pm_domain *genpd;
	int ret;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	if (genpd->suspend_power_off)
		return 0;

	ret = pm_generic_poweroff_noirq(dev);
	if (ret)
		return ret;

	if (device_may_wakeup(dev)
	    && genpd->active_wakeup && genpd->active_wakeup(dev))
		return 0;

	if (genpd->stop_device)
		genpd->stop_device(dev);

	/*
	 * Since all of the "noirq" callbacks are executed sequentially, it is
	 * guaranteed that this function will never run twice in parallel for
	 * the same PM domain, so it is not necessary to use locking here.
	 */
	genpd->suspended_count++;
	pm_genpd_sync_poweroff(genpd);

	return 0;
}

/**
 * pm_genpd_restore_noirq - Early restore of a device from an I/O power domain.
 * @dev: Device to resume.
 *
 * Carry out an early restore of a device under the assumption that its
 * pm_domain field points to the domain member of an object of type
 * struct generic_pm_domain representing a power domain consisting of I/O
 * devices.
 */
static int pm_genpd_restore_noirq(struct device *dev)
{
	struct generic_pm_domain *genpd;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	/*
	 * Since all of the "noirq" callbacks are executed sequentially, it is
	 * guaranteed that this function will never run twice in parallel for
	 * the same PM domain, so it is not necessary to use locking here.
	 */
	genpd->power_is_off = true;
	if (genpd->suspend_power_off) {
		/*
		 * The boot kernel might put the domain into the power on state,
		 * so make sure it really is powered off.
		 */
		if (genpd->power_off)
			genpd->power_off(genpd);
		return 0;
	}

	pm_genpd_poweron(genpd);
	genpd->suspended_count--;
	if (genpd->start_device)
		genpd->start_device(dev);

	return pm_generic_restore_noirq(dev);
}

/**
 * pm_genpd_restore - Restore a device belonging to an I/O power domain.
 * @dev: Device to resume.
 *
 * Restore a device under the assumption that its pm_domain field points to the
 * domain member of an object of type struct generic_pm_domain representing
 * a power domain consisting of I/O devices.
 */
static int pm_genpd_restore(struct device *dev)
{
	struct generic_pm_domain *genpd;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	return genpd->suspend_power_off ? 0 : pm_generic_restore(dev);
}

/**
 * pm_genpd_complete - Complete power transition of a device in a power domain.
 * @dev: Device to complete the transition of.
 *
 * Complete a power transition of a device (during a system-wide power
 * transition) under the assumption that its pm_domain field points to the
 * domain member of an object of type struct generic_pm_domain representing
 * a power domain consisting of I/O devices.
 */
static void pm_genpd_complete(struct device *dev)
{
	struct generic_pm_domain *genpd;
	bool run_complete;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return;

	mutex_lock(&genpd->lock);

	run_complete = !genpd->suspend_power_off;
	if (--genpd->prepared_count == 0)
		genpd->suspend_power_off = false;

	mutex_unlock(&genpd->lock);

	if (run_complete) {
		pm_generic_complete(dev);
		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);
		pm_runtime_idle(dev);
	}
}

#else

#define pm_genpd_prepare		NULL
#define pm_genpd_suspend		NULL
#define pm_genpd_suspend_noirq		NULL
#define pm_genpd_resume_noirq		NULL
#define pm_genpd_resume			NULL
#define pm_genpd_freeze			NULL
#define pm_genpd_freeze_noirq		NULL
#define pm_genpd_thaw_noirq		NULL
#define pm_genpd_thaw			NULL
#define pm_genpd_dev_poweroff_noirq	NULL
#define pm_genpd_dev_poweroff		NULL
#define pm_genpd_restore_noirq		NULL
#define pm_genpd_restore		NULL
#define pm_genpd_complete		NULL

#endif /* CONFIG_PM_SLEEP */

/**
 * pm_genpd_add_device - Add a device to an I/O PM domain.
 * @genpd: PM domain to add the device to.
 * @dev: Device to be added.
 */
int pm_genpd_add_device(struct generic_pm_domain *genpd, struct device *dev)
{
	struct dev_list_entry *dle;
	int ret = 0;

	dev_dbg(dev, "%s()\n", __func__);

	if (IS_ERR_OR_NULL(genpd) || IS_ERR_OR_NULL(dev))
		return -EINVAL;

	mutex_lock(&genpd->lock);

	if (genpd->power_is_off) {
		ret = -EINVAL;
		goto out;
	}

	if (genpd->prepared_count > 0) {
		ret = -EAGAIN;
		goto out;
	}

	list_for_each_entry(dle, &genpd->dev_list, node)
		if (dle->dev == dev) {
			ret = -EINVAL;
			goto out;
		}

	dle = kzalloc(sizeof(*dle), GFP_KERNEL);
	if (!dle) {
		ret = -ENOMEM;
		goto out;
	}

	dle->dev = dev;
	dle->need_restore = false;
	list_add_tail(&dle->node, &genpd->dev_list);
	genpd->device_count++;

	spin_lock_irq(&dev->power.lock);
	dev->pm_domain = &genpd->domain;
	spin_unlock_irq(&dev->power.lock);

 out:
	mutex_unlock(&genpd->lock);

	return ret;
}

/**
 * pm_genpd_remove_device - Remove a device from an I/O PM domain.
 * @genpd: PM domain to remove the device from.
 * @dev: Device to be removed.
 */
int pm_genpd_remove_device(struct generic_pm_domain *genpd,
			   struct device *dev)
{
	struct dev_list_entry *dle;
	int ret = -EINVAL;

	dev_dbg(dev, "%s()\n", __func__);

	if (IS_ERR_OR_NULL(genpd) || IS_ERR_OR_NULL(dev))
		return -EINVAL;

	mutex_lock(&genpd->lock);

	if (genpd->prepared_count > 0) {
		ret = -EAGAIN;
		goto out;
	}

	list_for_each_entry(dle, &genpd->dev_list, node) {
		if (dle->dev != dev)
			continue;

		spin_lock_irq(&dev->power.lock);
		dev->pm_domain = NULL;
		spin_unlock_irq(&dev->power.lock);

		genpd->device_count--;
		list_del(&dle->node);
		kfree(dle);

		ret = 0;
		break;
	}

 out:
	mutex_unlock(&genpd->lock);

	return ret;
}

/**
 * pm_genpd_add_subdomain - Add a subdomain to an I/O PM domain.
 * @genpd: Master PM domain to add the subdomain to.
 * @new_subdomain: Subdomain to be added.
 */
int pm_genpd_add_subdomain(struct generic_pm_domain *genpd,
			   struct generic_pm_domain *new_subdomain)
{
	struct generic_pm_domain *subdomain;
	int ret = 0;

	if (IS_ERR_OR_NULL(genpd) || IS_ERR_OR_NULL(new_subdomain))
		return -EINVAL;

	mutex_lock(&genpd->lock);

	if (genpd->power_is_off && !new_subdomain->power_is_off) {
		ret = -EINVAL;
		goto out;
	}

	list_for_each_entry(subdomain, &genpd->sd_list, sd_node) {
		if (subdomain == new_subdomain) {
			ret = -EINVAL;
			goto out;
		}
	}

	mutex_lock_nested(&new_subdomain->lock, SINGLE_DEPTH_NESTING);

	list_add_tail(&new_subdomain->sd_node, &genpd->sd_list);
	new_subdomain->parent = genpd;
	if (!subdomain->power_is_off)
		genpd->sd_count++;

	mutex_unlock(&new_subdomain->lock);

 out:
	mutex_unlock(&genpd->lock);

	return ret;
}

/**
 * pm_genpd_remove_subdomain - Remove a subdomain from an I/O PM domain.
 * @genpd: Master PM domain to remove the subdomain from.
 * @target: Subdomain to be removed.
 */
int pm_genpd_remove_subdomain(struct generic_pm_domain *genpd,
			      struct generic_pm_domain *target)
{
	struct generic_pm_domain *subdomain;
	int ret = -EINVAL;

	if (IS_ERR_OR_NULL(genpd) || IS_ERR_OR_NULL(target))
		return -EINVAL;

	mutex_lock(&genpd->lock);

	list_for_each_entry(subdomain, &genpd->sd_list, sd_node) {
		if (subdomain != target)
			continue;

		mutex_lock_nested(&subdomain->lock, SINGLE_DEPTH_NESTING);

		list_del(&subdomain->sd_node);
		subdomain->parent = NULL;
		if (!subdomain->power_is_off)
			genpd_sd_counter_dec(genpd);

		mutex_unlock(&subdomain->lock);

		ret = 0;
		break;
	}

	mutex_unlock(&genpd->lock);

	return ret;
}

/**
 * pm_genpd_init - Initialize a generic I/O PM domain object.
 * @genpd: PM domain object to initialize.
 * @gov: PM domain governor to associate with the domain (may be NULL).
 * @is_off: Initial value of the domain's power_is_off field.
 */
void pm_genpd_init(struct generic_pm_domain *genpd,
		   struct dev_power_governor *gov, bool is_off)
{
	if (IS_ERR_OR_NULL(genpd))
		return;

	INIT_LIST_HEAD(&genpd->sd_node);
	genpd->parent = NULL;
	INIT_LIST_HEAD(&genpd->dev_list);
	INIT_LIST_HEAD(&genpd->sd_list);
	mutex_init(&genpd->lock);
	genpd->gov = gov;
	INIT_WORK(&genpd->power_off_work, genpd_power_off_work_fn);
	genpd->in_progress = 0;
	genpd->sd_count = 0;
	genpd->power_is_off = is_off;
	genpd->device_count = 0;
	genpd->suspended_count = 0;
	genpd->domain.ops.runtime_suspend = pm_genpd_runtime_suspend;
	genpd->domain.ops.runtime_resume = pm_genpd_runtime_resume;
	genpd->domain.ops.runtime_idle = pm_generic_runtime_idle;
	genpd->domain.ops.prepare = pm_genpd_prepare;
	genpd->domain.ops.suspend = pm_genpd_suspend;
	genpd->domain.ops.suspend_noirq = pm_genpd_suspend_noirq;
	genpd->domain.ops.resume_noirq = pm_genpd_resume_noirq;
	genpd->domain.ops.resume = pm_genpd_resume;
	genpd->domain.ops.freeze = pm_genpd_freeze;
	genpd->domain.ops.freeze_noirq = pm_genpd_freeze_noirq;
	genpd->domain.ops.thaw_noirq = pm_genpd_thaw_noirq;
	genpd->domain.ops.thaw = pm_genpd_thaw;
	genpd->domain.ops.poweroff = pm_genpd_dev_poweroff;
	genpd->domain.ops.poweroff_noirq = pm_genpd_dev_poweroff_noirq;
	genpd->domain.ops.restore_noirq = pm_genpd_restore_noirq;
	genpd->domain.ops.restore = pm_genpd_restore;
	genpd->domain.ops.complete = pm_genpd_complete;
}
