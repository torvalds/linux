/*
 * drivers/base/power/domain.c - Common code related to device power domains.
 *
 * Copyright (C) 2011 Rafael J. Wysocki <rjw@sisk.pl>, Renesas Electronics Corp.
 *
 * This file is released under the GPLv2.
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <linux/pm_qos.h>
#include <linux/pm_clock.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/suspend.h>
#include <linux/export.h>

#include "power.h"

#define GENPD_RETRY_MAX_MS	250		/* Approximate */

#define GENPD_DEV_CALLBACK(genpd, type, callback, dev)		\
({								\
	type (*__routine)(struct device *__d); 			\
	type __ret = (type)0;					\
								\
	__routine = genpd->dev_ops.callback; 			\
	if (__routine) {					\
		__ret = __routine(dev); 			\
	}							\
	__ret;							\
})

static LIST_HEAD(gpd_list);
static DEFINE_MUTEX(gpd_list_lock);

/*
 * Get the generic PM domain for a particular struct device.
 * This validates the struct device pointer, the PM domain pointer,
 * and checks that the PM domain pointer is a real generic PM domain.
 * Any failure results in NULL being returned.
 */
static struct generic_pm_domain *genpd_lookup_dev(struct device *dev)
{
	struct generic_pm_domain *genpd = NULL, *gpd;

	if (IS_ERR_OR_NULL(dev) || IS_ERR_OR_NULL(dev->pm_domain))
		return NULL;

	mutex_lock(&gpd_list_lock);
	list_for_each_entry(gpd, &gpd_list, gpd_list_node) {
		if (&gpd->domain == dev->pm_domain) {
			genpd = gpd;
			break;
		}
	}
	mutex_unlock(&gpd_list_lock);

	return genpd;
}

/*
 * This should only be used where we are certain that the pm_domain
 * attached to the device is a genpd domain.
 */
static struct generic_pm_domain *dev_to_genpd(struct device *dev)
{
	if (IS_ERR_OR_NULL(dev->pm_domain))
		return ERR_PTR(-EINVAL);

	return pd_to_genpd(dev->pm_domain);
}

static int genpd_stop_dev(struct generic_pm_domain *genpd, struct device *dev)
{
	return GENPD_DEV_CALLBACK(genpd, int, stop, dev);
}

static int genpd_start_dev(struct generic_pm_domain *genpd, struct device *dev)
{
	return GENPD_DEV_CALLBACK(genpd, int, start, dev);
}

static bool genpd_sd_counter_dec(struct generic_pm_domain *genpd)
{
	bool ret = false;

	if (!WARN_ON(atomic_read(&genpd->sd_count) == 0))
		ret = !!atomic_dec_and_test(&genpd->sd_count);

	return ret;
}

static void genpd_sd_counter_inc(struct generic_pm_domain *genpd)
{
	atomic_inc(&genpd->sd_count);
	smp_mb__after_atomic();
}

static int genpd_power_on(struct generic_pm_domain *genpd, bool timed)
{
	unsigned int state_idx = genpd->state_idx;
	ktime_t time_start;
	s64 elapsed_ns;
	int ret;

	if (!genpd->power_on)
		return 0;

	if (!timed)
		return genpd->power_on(genpd);

	time_start = ktime_get();
	ret = genpd->power_on(genpd);
	if (ret)
		return ret;

	elapsed_ns = ktime_to_ns(ktime_sub(ktime_get(), time_start));
	if (elapsed_ns <= genpd->states[state_idx].power_on_latency_ns)
		return ret;

	genpd->states[state_idx].power_on_latency_ns = elapsed_ns;
	genpd->max_off_time_changed = true;
	pr_debug("%s: Power-%s latency exceeded, new value %lld ns\n",
		 genpd->name, "on", elapsed_ns);

	return ret;
}

static int genpd_power_off(struct generic_pm_domain *genpd, bool timed)
{
	unsigned int state_idx = genpd->state_idx;
	ktime_t time_start;
	s64 elapsed_ns;
	int ret;

	if (!genpd->power_off)
		return 0;

	if (!timed)
		return genpd->power_off(genpd);

	time_start = ktime_get();
	ret = genpd->power_off(genpd);
	if (ret == -EBUSY)
		return ret;

	elapsed_ns = ktime_to_ns(ktime_sub(ktime_get(), time_start));
	if (elapsed_ns <= genpd->states[state_idx].power_off_latency_ns)
		return ret;

	genpd->states[state_idx].power_off_latency_ns = elapsed_ns;
	genpd->max_off_time_changed = true;
	pr_debug("%s: Power-%s latency exceeded, new value %lld ns\n",
		 genpd->name, "off", elapsed_ns);

	return ret;
}

/**
 * genpd_queue_power_off_work - Queue up the execution of genpd_poweroff().
 * @genpd: PM domain to power off.
 *
 * Queue up the execution of genpd_poweroff() unless it's already been done
 * before.
 */
static void genpd_queue_power_off_work(struct generic_pm_domain *genpd)
{
	queue_work(pm_wq, &genpd->power_off_work);
}

/**
 * genpd_poweron - Restore power to a given PM domain and its masters.
 * @genpd: PM domain to power up.
 * @depth: nesting count for lockdep.
 *
 * Restore power to @genpd and all of its masters so that it is possible to
 * resume a device belonging to it.
 */
static int genpd_poweron(struct generic_pm_domain *genpd, unsigned int depth)
{
	struct gpd_link *link;
	int ret = 0;

	if (genpd->status == GPD_STATE_ACTIVE)
		return 0;

	/*
	 * The list is guaranteed not to change while the loop below is being
	 * executed, unless one of the masters' .power_on() callbacks fiddles
	 * with it.
	 */
	list_for_each_entry(link, &genpd->slave_links, slave_node) {
		struct generic_pm_domain *master = link->master;

		genpd_sd_counter_inc(master);

		mutex_lock_nested(&master->lock, depth + 1);
		ret = genpd_poweron(master, depth + 1);
		mutex_unlock(&master->lock);

		if (ret) {
			genpd_sd_counter_dec(master);
			goto err;
		}
	}

	ret = genpd_power_on(genpd, true);
	if (ret)
		goto err;

	genpd->status = GPD_STATE_ACTIVE;
	return 0;

 err:
	list_for_each_entry_continue_reverse(link,
					&genpd->slave_links,
					slave_node) {
		genpd_sd_counter_dec(link->master);
		genpd_queue_power_off_work(link->master);
	}

	return ret;
}

static int genpd_dev_pm_qos_notifier(struct notifier_block *nb,
				     unsigned long val, void *ptr)
{
	struct generic_pm_domain_data *gpd_data;
	struct device *dev;

	gpd_data = container_of(nb, struct generic_pm_domain_data, nb);
	dev = gpd_data->base.dev;

	for (;;) {
		struct generic_pm_domain *genpd;
		struct pm_domain_data *pdd;

		spin_lock_irq(&dev->power.lock);

		pdd = dev->power.subsys_data ?
				dev->power.subsys_data->domain_data : NULL;
		if (pdd && pdd->dev) {
			to_gpd_data(pdd)->td.constraint_changed = true;
			genpd = dev_to_genpd(dev);
		} else {
			genpd = ERR_PTR(-ENODATA);
		}

		spin_unlock_irq(&dev->power.lock);

		if (!IS_ERR(genpd)) {
			mutex_lock(&genpd->lock);
			genpd->max_off_time_changed = true;
			mutex_unlock(&genpd->lock);
		}

		dev = dev->parent;
		if (!dev || dev->power.ignore_children)
			break;
	}

	return NOTIFY_DONE;
}

/**
 * genpd_poweroff - Remove power from a given PM domain.
 * @genpd: PM domain to power down.
 * @is_async: PM domain is powered down from a scheduled work
 *
 * If all of the @genpd's devices have been suspended and all of its subdomains
 * have been powered down, remove power from @genpd.
 */
static int genpd_poweroff(struct generic_pm_domain *genpd, bool is_async)
{
	struct pm_domain_data *pdd;
	struct gpd_link *link;
	unsigned int not_suspended = 0;

	/*
	 * Do not try to power off the domain in the following situations:
	 * (1) The domain is already in the "power off" state.
	 * (2) System suspend is in progress.
	 */
	if (genpd->status == GPD_STATE_POWER_OFF
	    || genpd->prepared_count > 0)
		return 0;

	if (atomic_read(&genpd->sd_count) > 0)
		return -EBUSY;

	list_for_each_entry(pdd, &genpd->dev_list, list_node) {
		enum pm_qos_flags_status stat;

		stat = dev_pm_qos_flags(pdd->dev,
					PM_QOS_FLAG_NO_POWER_OFF
						| PM_QOS_FLAG_REMOTE_WAKEUP);
		if (stat > PM_QOS_FLAGS_NONE)
			return -EBUSY;

		if (!pm_runtime_suspended(pdd->dev) || pdd->dev->power.irq_safe)
			not_suspended++;
	}

	if (not_suspended > 1 || (not_suspended == 1 && is_async))
		return -EBUSY;

	if (genpd->gov && genpd->gov->power_down_ok) {
		if (!genpd->gov->power_down_ok(&genpd->domain))
			return -EAGAIN;
	}

	if (genpd->power_off) {
		int ret;

		if (atomic_read(&genpd->sd_count) > 0)
			return -EBUSY;

		/*
		 * If sd_count > 0 at this point, one of the subdomains hasn't
		 * managed to call genpd_poweron() for the master yet after
		 * incrementing it.  In that case genpd_poweron() will wait
		 * for us to drop the lock, so we can call .power_off() and let
		 * the genpd_poweron() restore power for us (this shouldn't
		 * happen very often).
		 */
		ret = genpd_power_off(genpd, true);
		if (ret)
			return ret;
	}

	genpd->status = GPD_STATE_POWER_OFF;

	list_for_each_entry(link, &genpd->slave_links, slave_node) {
		genpd_sd_counter_dec(link->master);
		genpd_queue_power_off_work(link->master);
	}

	return 0;
}

/**
 * genpd_power_off_work_fn - Power off PM domain whose subdomain count is 0.
 * @work: Work structure used for scheduling the execution of this function.
 */
static void genpd_power_off_work_fn(struct work_struct *work)
{
	struct generic_pm_domain *genpd;

	genpd = container_of(work, struct generic_pm_domain, power_off_work);

	mutex_lock(&genpd->lock);
	genpd_poweroff(genpd, true);
	mutex_unlock(&genpd->lock);
}

/**
 * __genpd_runtime_suspend - walk the hierarchy of ->runtime_suspend() callbacks
 * @dev: Device to handle.
 */
static int __genpd_runtime_suspend(struct device *dev)
{
	int (*cb)(struct device *__dev);

	if (dev->type && dev->type->pm)
		cb = dev->type->pm->runtime_suspend;
	else if (dev->class && dev->class->pm)
		cb = dev->class->pm->runtime_suspend;
	else if (dev->bus && dev->bus->pm)
		cb = dev->bus->pm->runtime_suspend;
	else
		cb = NULL;

	if (!cb && dev->driver && dev->driver->pm)
		cb = dev->driver->pm->runtime_suspend;

	return cb ? cb(dev) : 0;
}

/**
 * __genpd_runtime_resume - walk the hierarchy of ->runtime_resume() callbacks
 * @dev: Device to handle.
 */
static int __genpd_runtime_resume(struct device *dev)
{
	int (*cb)(struct device *__dev);

	if (dev->type && dev->type->pm)
		cb = dev->type->pm->runtime_resume;
	else if (dev->class && dev->class->pm)
		cb = dev->class->pm->runtime_resume;
	else if (dev->bus && dev->bus->pm)
		cb = dev->bus->pm->runtime_resume;
	else
		cb = NULL;

	if (!cb && dev->driver && dev->driver->pm)
		cb = dev->driver->pm->runtime_resume;

	return cb ? cb(dev) : 0;
}

/**
 * genpd_runtime_suspend - Suspend a device belonging to I/O PM domain.
 * @dev: Device to suspend.
 *
 * Carry out a runtime suspend of a device under the assumption that its
 * pm_domain field points to the domain member of an object of type
 * struct generic_pm_domain representing a PM domain consisting of I/O devices.
 */
static int genpd_runtime_suspend(struct device *dev)
{
	struct generic_pm_domain *genpd;
	bool (*suspend_ok)(struct device *__dev);
	struct gpd_timing_data *td = &dev_gpd_data(dev)->td;
	bool runtime_pm = pm_runtime_enabled(dev);
	ktime_t time_start;
	s64 elapsed_ns;
	int ret;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	/*
	 * A runtime PM centric subsystem/driver may re-use the runtime PM
	 * callbacks for other purposes than runtime PM. In those scenarios
	 * runtime PM is disabled. Under these circumstances, we shall skip
	 * validating/measuring the PM QoS latency.
	 */
	suspend_ok = genpd->gov ? genpd->gov->suspend_ok : NULL;
	if (runtime_pm && suspend_ok && !suspend_ok(dev))
		return -EBUSY;

	/* Measure suspend latency. */
	if (runtime_pm)
		time_start = ktime_get();

	ret = __genpd_runtime_suspend(dev);
	if (ret)
		return ret;

	ret = genpd_stop_dev(genpd, dev);
	if (ret) {
		__genpd_runtime_resume(dev);
		return ret;
	}

	/* Update suspend latency value if the measured time exceeds it. */
	if (runtime_pm) {
		elapsed_ns = ktime_to_ns(ktime_sub(ktime_get(), time_start));
		if (elapsed_ns > td->suspend_latency_ns) {
			td->suspend_latency_ns = elapsed_ns;
			dev_dbg(dev, "suspend latency exceeded, %lld ns\n",
				elapsed_ns);
			genpd->max_off_time_changed = true;
			td->constraint_changed = true;
		}
	}

	/*
	 * If power.irq_safe is set, this routine will be run with interrupts
	 * off, so it can't use mutexes.
	 */
	if (dev->power.irq_safe)
		return 0;

	mutex_lock(&genpd->lock);
	genpd_poweroff(genpd, false);
	mutex_unlock(&genpd->lock);

	return 0;
}

/**
 * genpd_runtime_resume - Resume a device belonging to I/O PM domain.
 * @dev: Device to resume.
 *
 * Carry out a runtime resume of a device under the assumption that its
 * pm_domain field points to the domain member of an object of type
 * struct generic_pm_domain representing a PM domain consisting of I/O devices.
 */
static int genpd_runtime_resume(struct device *dev)
{
	struct generic_pm_domain *genpd;
	struct gpd_timing_data *td = &dev_gpd_data(dev)->td;
	bool runtime_pm = pm_runtime_enabled(dev);
	ktime_t time_start;
	s64 elapsed_ns;
	int ret;
	bool timed = true;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	/* If power.irq_safe, the PM domain is never powered off. */
	if (dev->power.irq_safe) {
		timed = false;
		goto out;
	}

	mutex_lock(&genpd->lock);
	ret = genpd_poweron(genpd, 0);
	mutex_unlock(&genpd->lock);

	if (ret)
		return ret;

 out:
	/* Measure resume latency. */
	if (timed && runtime_pm)
		time_start = ktime_get();

	ret = genpd_start_dev(genpd, dev);
	if (ret)
		goto err_poweroff;

	ret = __genpd_runtime_resume(dev);
	if (ret)
		goto err_stop;

	/* Update resume latency value if the measured time exceeds it. */
	if (timed && runtime_pm) {
		elapsed_ns = ktime_to_ns(ktime_sub(ktime_get(), time_start));
		if (elapsed_ns > td->resume_latency_ns) {
			td->resume_latency_ns = elapsed_ns;
			dev_dbg(dev, "resume latency exceeded, %lld ns\n",
				elapsed_ns);
			genpd->max_off_time_changed = true;
			td->constraint_changed = true;
		}
	}

	return 0;

err_stop:
	genpd_stop_dev(genpd, dev);
err_poweroff:
	if (!dev->power.irq_safe) {
		mutex_lock(&genpd->lock);
		genpd_poweroff(genpd, 0);
		mutex_unlock(&genpd->lock);
	}

	return ret;
}

static bool pd_ignore_unused;
static int __init pd_ignore_unused_setup(char *__unused)
{
	pd_ignore_unused = true;
	return 1;
}
__setup("pd_ignore_unused", pd_ignore_unused_setup);

/**
 * genpd_poweroff_unused - Power off all PM domains with no devices in use.
 */
static int __init genpd_poweroff_unused(void)
{
	struct generic_pm_domain *genpd;

	if (pd_ignore_unused) {
		pr_warn("genpd: Not disabling unused power domains\n");
		return 0;
	}

	mutex_lock(&gpd_list_lock);

	list_for_each_entry(genpd, &gpd_list, gpd_list_node)
		genpd_queue_power_off_work(genpd);

	mutex_unlock(&gpd_list_lock);

	return 0;
}
late_initcall(genpd_poweroff_unused);

#if defined(CONFIG_PM_SLEEP) || defined(CONFIG_PM_GENERIC_DOMAINS_OF)

/**
 * pm_genpd_present - Check if the given PM domain has been initialized.
 * @genpd: PM domain to check.
 */
static bool pm_genpd_present(const struct generic_pm_domain *genpd)
{
	const struct generic_pm_domain *gpd;

	if (IS_ERR_OR_NULL(genpd))
		return false;

	list_for_each_entry(gpd, &gpd_list, gpd_list_node)
		if (gpd == genpd)
			return true;

	return false;
}

#endif

#ifdef CONFIG_PM_SLEEP

static bool genpd_dev_active_wakeup(struct generic_pm_domain *genpd,
				    struct device *dev)
{
	return GENPD_DEV_CALLBACK(genpd, bool, active_wakeup, dev);
}

/**
 * genpd_sync_poweroff - Synchronously power off a PM domain and its masters.
 * @genpd: PM domain to power off, if possible.
 *
 * Check if the given PM domain can be powered off (during system suspend or
 * hibernation) and do that if so.  Also, in that case propagate to its masters.
 *
 * This function is only called in "noirq" and "syscore" stages of system power
 * transitions, so it need not acquire locks (all of the "noirq" callbacks are
 * executed sequentially, so it is guaranteed that it will never run twice in
 * parallel).
 */
static void genpd_sync_poweroff(struct generic_pm_domain *genpd)
{
	struct gpd_link *link;

	if (genpd->status == GPD_STATE_POWER_OFF)
		return;

	if (genpd->suspended_count != genpd->device_count
	    || atomic_read(&genpd->sd_count) > 0)
		return;

	/* Choose the deepest state when suspending */
	genpd->state_idx = genpd->state_count - 1;
	genpd_power_off(genpd, false);

	genpd->status = GPD_STATE_POWER_OFF;

	list_for_each_entry(link, &genpd->slave_links, slave_node) {
		genpd_sd_counter_dec(link->master);
		genpd_sync_poweroff(link->master);
	}
}

/**
 * genpd_sync_poweron - Synchronously power on a PM domain and its masters.
 * @genpd: PM domain to power on.
 *
 * This function is only called in "noirq" and "syscore" stages of system power
 * transitions, so it need not acquire locks (all of the "noirq" callbacks are
 * executed sequentially, so it is guaranteed that it will never run twice in
 * parallel).
 */
static void genpd_sync_poweron(struct generic_pm_domain *genpd)
{
	struct gpd_link *link;

	if (genpd->status == GPD_STATE_ACTIVE)
		return;

	list_for_each_entry(link, &genpd->slave_links, slave_node) {
		genpd_sync_poweron(link->master);
		genpd_sd_counter_inc(link->master);
	}

	genpd_power_on(genpd, false);

	genpd->status = GPD_STATE_ACTIVE;
}

/**
 * resume_needed - Check whether to resume a device before system suspend.
 * @dev: Device to check.
 * @genpd: PM domain the device belongs to.
 *
 * There are two cases in which a device that can wake up the system from sleep
 * states should be resumed by pm_genpd_prepare(): (1) if the device is enabled
 * to wake up the system and it has to remain active for this purpose while the
 * system is in the sleep state and (2) if the device is not enabled to wake up
 * the system from sleep states and it generally doesn't generate wakeup signals
 * by itself (those signals are generated on its behalf by other parts of the
 * system).  In the latter case it may be necessary to reconfigure the device's
 * wakeup settings during system suspend, because it may have been set up to
 * signal remote wakeup from the system's working state as needed by runtime PM.
 * Return 'true' in either of the above cases.
 */
static bool resume_needed(struct device *dev, struct generic_pm_domain *genpd)
{
	bool active_wakeup;

	if (!device_can_wakeup(dev))
		return false;

	active_wakeup = genpd_dev_active_wakeup(genpd, dev);
	return device_may_wakeup(dev) ? active_wakeup : !active_wakeup;
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
	int ret;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	/*
	 * If a wakeup request is pending for the device, it should be woken up
	 * at this point and a system wakeup event should be reported if it's
	 * set up to wake up the system from sleep states.
	 */
	if (resume_needed(dev, genpd))
		pm_runtime_resume(dev);

	mutex_lock(&genpd->lock);

	if (genpd->prepared_count++ == 0)
		genpd->suspended_count = 0;

	mutex_unlock(&genpd->lock);

	ret = pm_generic_prepare(dev);
	if (ret) {
		mutex_lock(&genpd->lock);

		genpd->prepared_count--;

		mutex_unlock(&genpd->lock);
	}

	return ret;
}

/**
 * pm_genpd_suspend_noirq - Completion of suspend of device in an I/O PM domain.
 * @dev: Device to suspend.
 *
 * Stop the device and remove power from the domain if all devices in it have
 * been stopped.
 */
static int pm_genpd_suspend_noirq(struct device *dev)
{
	struct generic_pm_domain *genpd;
	int ret;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	if (dev->power.wakeup_path && genpd_dev_active_wakeup(genpd, dev))
		return 0;

	if (genpd->dev_ops.stop && genpd->dev_ops.start) {
		ret = pm_runtime_force_suspend(dev);
		if (ret)
			return ret;
	}

	/*
	 * Since all of the "noirq" callbacks are executed sequentially, it is
	 * guaranteed that this function will never run twice in parallel for
	 * the same PM domain, so it is not necessary to use locking here.
	 */
	genpd->suspended_count++;
	genpd_sync_poweroff(genpd);

	return 0;
}

/**
 * pm_genpd_resume_noirq - Start of resume of device in an I/O PM domain.
 * @dev: Device to resume.
 *
 * Restore power to the device's PM domain, if necessary, and start the device.
 */
static int pm_genpd_resume_noirq(struct device *dev)
{
	struct generic_pm_domain *genpd;
	int ret = 0;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	if (dev->power.wakeup_path && genpd_dev_active_wakeup(genpd, dev))
		return 0;

	/*
	 * Since all of the "noirq" callbacks are executed sequentially, it is
	 * guaranteed that this function will never run twice in parallel for
	 * the same PM domain, so it is not necessary to use locking here.
	 */
	genpd_sync_poweron(genpd);
	genpd->suspended_count--;

	if (genpd->dev_ops.stop && genpd->dev_ops.start)
		ret = pm_runtime_force_resume(dev);

	return ret;
}

/**
 * pm_genpd_freeze_noirq - Completion of freezing a device in an I/O PM domain.
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
	int ret = 0;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	if (genpd->dev_ops.stop && genpd->dev_ops.start)
		ret = pm_runtime_force_suspend(dev);

	return ret;
}

/**
 * pm_genpd_thaw_noirq - Early thaw of device in an I/O PM domain.
 * @dev: Device to thaw.
 *
 * Start the device, unless power has been removed from the domain already
 * before the system transition.
 */
static int pm_genpd_thaw_noirq(struct device *dev)
{
	struct generic_pm_domain *genpd;
	int ret = 0;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	if (genpd->dev_ops.stop && genpd->dev_ops.start)
		ret = pm_runtime_force_resume(dev);

	return ret;
}

/**
 * pm_genpd_restore_noirq - Start of restore of device in an I/O PM domain.
 * @dev: Device to resume.
 *
 * Make sure the domain will be in the same power state as before the
 * hibernation the system is resuming from and start the device if necessary.
 */
static int pm_genpd_restore_noirq(struct device *dev)
{
	struct generic_pm_domain *genpd;
	int ret = 0;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	/*
	 * Since all of the "noirq" callbacks are executed sequentially, it is
	 * guaranteed that this function will never run twice in parallel for
	 * the same PM domain, so it is not necessary to use locking here.
	 *
	 * At this point suspended_count == 0 means we are being run for the
	 * first time for the given domain in the present cycle.
	 */
	if (genpd->suspended_count++ == 0)
		/*
		 * The boot kernel might put the domain into arbitrary state,
		 * so make it appear as powered off to genpd_sync_poweron(),
		 * so that it tries to power it on in case it was really off.
		 */
		genpd->status = GPD_STATE_POWER_OFF;

	genpd_sync_poweron(genpd);

	if (genpd->dev_ops.stop && genpd->dev_ops.start)
		ret = pm_runtime_force_resume(dev);

	return ret;
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

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return;

	pm_generic_complete(dev);

	mutex_lock(&genpd->lock);

	genpd->prepared_count--;
	if (!genpd->prepared_count)
		genpd_queue_power_off_work(genpd);

	mutex_unlock(&genpd->lock);
}

/**
 * genpd_syscore_switch - Switch power during system core suspend or resume.
 * @dev: Device that normally is marked as "always on" to switch power for.
 *
 * This routine may only be called during the system core (syscore) suspend or
 * resume phase for devices whose "always on" flags are set.
 */
static void genpd_syscore_switch(struct device *dev, bool suspend)
{
	struct generic_pm_domain *genpd;

	genpd = dev_to_genpd(dev);
	if (!pm_genpd_present(genpd))
		return;

	if (suspend) {
		genpd->suspended_count++;
		genpd_sync_poweroff(genpd);
	} else {
		genpd_sync_poweron(genpd);
		genpd->suspended_count--;
	}
}

void pm_genpd_syscore_poweroff(struct device *dev)
{
	genpd_syscore_switch(dev, true);
}
EXPORT_SYMBOL_GPL(pm_genpd_syscore_poweroff);

void pm_genpd_syscore_poweron(struct device *dev)
{
	genpd_syscore_switch(dev, false);
}
EXPORT_SYMBOL_GPL(pm_genpd_syscore_poweron);

#else /* !CONFIG_PM_SLEEP */

#define pm_genpd_prepare		NULL
#define pm_genpd_suspend_noirq		NULL
#define pm_genpd_resume_noirq		NULL
#define pm_genpd_freeze_noirq		NULL
#define pm_genpd_thaw_noirq		NULL
#define pm_genpd_restore_noirq		NULL
#define pm_genpd_complete		NULL

#endif /* CONFIG_PM_SLEEP */

static struct generic_pm_domain_data *genpd_alloc_dev_data(struct device *dev,
					struct generic_pm_domain *genpd,
					struct gpd_timing_data *td)
{
	struct generic_pm_domain_data *gpd_data;
	int ret;

	ret = dev_pm_get_subsys_data(dev);
	if (ret)
		return ERR_PTR(ret);

	gpd_data = kzalloc(sizeof(*gpd_data), GFP_KERNEL);
	if (!gpd_data) {
		ret = -ENOMEM;
		goto err_put;
	}

	if (td)
		gpd_data->td = *td;

	gpd_data->base.dev = dev;
	gpd_data->td.constraint_changed = true;
	gpd_data->td.effective_constraint_ns = -1;
	gpd_data->nb.notifier_call = genpd_dev_pm_qos_notifier;

	spin_lock_irq(&dev->power.lock);

	if (dev->power.subsys_data->domain_data) {
		ret = -EINVAL;
		goto err_free;
	}

	dev->power.subsys_data->domain_data = &gpd_data->base;

	spin_unlock_irq(&dev->power.lock);

	return gpd_data;

 err_free:
	spin_unlock_irq(&dev->power.lock);
	kfree(gpd_data);
 err_put:
	dev_pm_put_subsys_data(dev);
	return ERR_PTR(ret);
}

static void genpd_free_dev_data(struct device *dev,
				struct generic_pm_domain_data *gpd_data)
{
	spin_lock_irq(&dev->power.lock);

	dev->power.subsys_data->domain_data = NULL;

	spin_unlock_irq(&dev->power.lock);

	kfree(gpd_data);
	dev_pm_put_subsys_data(dev);
}

static int genpd_add_device(struct generic_pm_domain *genpd, struct device *dev,
			    struct gpd_timing_data *td)
{
	struct generic_pm_domain_data *gpd_data;
	int ret = 0;

	dev_dbg(dev, "%s()\n", __func__);

	if (IS_ERR_OR_NULL(genpd) || IS_ERR_OR_NULL(dev))
		return -EINVAL;

	gpd_data = genpd_alloc_dev_data(dev, genpd, td);
	if (IS_ERR(gpd_data))
		return PTR_ERR(gpd_data);

	mutex_lock(&genpd->lock);

	if (genpd->prepared_count > 0) {
		ret = -EAGAIN;
		goto out;
	}

	ret = genpd->attach_dev ? genpd->attach_dev(genpd, dev) : 0;
	if (ret)
		goto out;

	dev_pm_domain_set(dev, &genpd->domain);

	genpd->device_count++;
	genpd->max_off_time_changed = true;

	list_add_tail(&gpd_data->base.list_node, &genpd->dev_list);

 out:
	mutex_unlock(&genpd->lock);

	if (ret)
		genpd_free_dev_data(dev, gpd_data);
	else
		dev_pm_qos_add_notifier(dev, &gpd_data->nb);

	return ret;
}

/**
 * __pm_genpd_add_device - Add a device to an I/O PM domain.
 * @genpd: PM domain to add the device to.
 * @dev: Device to be added.
 * @td: Set of PM QoS timing parameters to attach to the device.
 */
int __pm_genpd_add_device(struct generic_pm_domain *genpd, struct device *dev,
			  struct gpd_timing_data *td)
{
	int ret;

	mutex_lock(&gpd_list_lock);
	ret = genpd_add_device(genpd, dev, td);
	mutex_unlock(&gpd_list_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(__pm_genpd_add_device);

static int genpd_remove_device(struct generic_pm_domain *genpd,
			       struct device *dev)
{
	struct generic_pm_domain_data *gpd_data;
	struct pm_domain_data *pdd;
	int ret = 0;

	dev_dbg(dev, "%s()\n", __func__);

	pdd = dev->power.subsys_data->domain_data;
	gpd_data = to_gpd_data(pdd);
	dev_pm_qos_remove_notifier(dev, &gpd_data->nb);

	mutex_lock(&genpd->lock);

	if (genpd->prepared_count > 0) {
		ret = -EAGAIN;
		goto out;
	}

	genpd->device_count--;
	genpd->max_off_time_changed = true;

	if (genpd->detach_dev)
		genpd->detach_dev(genpd, dev);

	dev_pm_domain_set(dev, NULL);

	list_del_init(&pdd->list_node);

	mutex_unlock(&genpd->lock);

	genpd_free_dev_data(dev, gpd_data);

	return 0;

 out:
	mutex_unlock(&genpd->lock);
	dev_pm_qos_add_notifier(dev, &gpd_data->nb);

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
	if (!genpd || genpd != genpd_lookup_dev(dev))
		return -EINVAL;

	return genpd_remove_device(genpd, dev);
}
EXPORT_SYMBOL_GPL(pm_genpd_remove_device);

static int genpd_add_subdomain(struct generic_pm_domain *genpd,
			       struct generic_pm_domain *subdomain)
{
	struct gpd_link *link, *itr;
	int ret = 0;

	if (IS_ERR_OR_NULL(genpd) || IS_ERR_OR_NULL(subdomain)
	    || genpd == subdomain)
		return -EINVAL;

	link = kzalloc(sizeof(*link), GFP_KERNEL);
	if (!link)
		return -ENOMEM;

	mutex_lock(&subdomain->lock);
	mutex_lock_nested(&genpd->lock, SINGLE_DEPTH_NESTING);

	if (genpd->status == GPD_STATE_POWER_OFF
	    &&  subdomain->status != GPD_STATE_POWER_OFF) {
		ret = -EINVAL;
		goto out;
	}

	list_for_each_entry(itr, &genpd->master_links, master_node) {
		if (itr->slave == subdomain && itr->master == genpd) {
			ret = -EINVAL;
			goto out;
		}
	}

	link->master = genpd;
	list_add_tail(&link->master_node, &genpd->master_links);
	link->slave = subdomain;
	list_add_tail(&link->slave_node, &subdomain->slave_links);
	if (subdomain->status != GPD_STATE_POWER_OFF)
		genpd_sd_counter_inc(genpd);

 out:
	mutex_unlock(&genpd->lock);
	mutex_unlock(&subdomain->lock);
	if (ret)
		kfree(link);
	return ret;
}

/**
 * pm_genpd_add_subdomain - Add a subdomain to an I/O PM domain.
 * @genpd: Master PM domain to add the subdomain to.
 * @subdomain: Subdomain to be added.
 */
int pm_genpd_add_subdomain(struct generic_pm_domain *genpd,
			   struct generic_pm_domain *subdomain)
{
	int ret;

	mutex_lock(&gpd_list_lock);
	ret = genpd_add_subdomain(genpd, subdomain);
	mutex_unlock(&gpd_list_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(pm_genpd_add_subdomain);

/**
 * pm_genpd_remove_subdomain - Remove a subdomain from an I/O PM domain.
 * @genpd: Master PM domain to remove the subdomain from.
 * @subdomain: Subdomain to be removed.
 */
int pm_genpd_remove_subdomain(struct generic_pm_domain *genpd,
			      struct generic_pm_domain *subdomain)
{
	struct gpd_link *l, *link;
	int ret = -EINVAL;

	if (IS_ERR_OR_NULL(genpd) || IS_ERR_OR_NULL(subdomain))
		return -EINVAL;

	mutex_lock(&subdomain->lock);
	mutex_lock_nested(&genpd->lock, SINGLE_DEPTH_NESTING);

	if (!list_empty(&subdomain->master_links) || subdomain->device_count) {
		pr_warn("%s: unable to remove subdomain %s\n", genpd->name,
			subdomain->name);
		ret = -EBUSY;
		goto out;
	}

	list_for_each_entry_safe(link, l, &genpd->master_links, master_node) {
		if (link->slave != subdomain)
			continue;

		list_del(&link->master_node);
		list_del(&link->slave_node);
		kfree(link);
		if (subdomain->status != GPD_STATE_POWER_OFF)
			genpd_sd_counter_dec(genpd);

		ret = 0;
		break;
	}

out:
	mutex_unlock(&genpd->lock);
	mutex_unlock(&subdomain->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(pm_genpd_remove_subdomain);

/**
 * pm_genpd_init - Initialize a generic I/O PM domain object.
 * @genpd: PM domain object to initialize.
 * @gov: PM domain governor to associate with the domain (may be NULL).
 * @is_off: Initial value of the domain's power_is_off field.
 *
 * Returns 0 on successful initialization, else a negative error code.
 */
int pm_genpd_init(struct generic_pm_domain *genpd,
		  struct dev_power_governor *gov, bool is_off)
{
	if (IS_ERR_OR_NULL(genpd))
		return -EINVAL;

	INIT_LIST_HEAD(&genpd->master_links);
	INIT_LIST_HEAD(&genpd->slave_links);
	INIT_LIST_HEAD(&genpd->dev_list);
	mutex_init(&genpd->lock);
	genpd->gov = gov;
	INIT_WORK(&genpd->power_off_work, genpd_power_off_work_fn);
	atomic_set(&genpd->sd_count, 0);
	genpd->status = is_off ? GPD_STATE_POWER_OFF : GPD_STATE_ACTIVE;
	genpd->device_count = 0;
	genpd->max_off_time_ns = -1;
	genpd->max_off_time_changed = true;
	genpd->provider = NULL;
	genpd->has_provider = false;
	genpd->domain.ops.runtime_suspend = genpd_runtime_suspend;
	genpd->domain.ops.runtime_resume = genpd_runtime_resume;
	genpd->domain.ops.prepare = pm_genpd_prepare;
	genpd->domain.ops.suspend_noirq = pm_genpd_suspend_noirq;
	genpd->domain.ops.resume_noirq = pm_genpd_resume_noirq;
	genpd->domain.ops.freeze_noirq = pm_genpd_freeze_noirq;
	genpd->domain.ops.thaw_noirq = pm_genpd_thaw_noirq;
	genpd->domain.ops.poweroff_noirq = pm_genpd_suspend_noirq;
	genpd->domain.ops.restore_noirq = pm_genpd_restore_noirq;
	genpd->domain.ops.complete = pm_genpd_complete;

	if (genpd->flags & GENPD_FLAG_PM_CLK) {
		genpd->dev_ops.stop = pm_clk_suspend;
		genpd->dev_ops.start = pm_clk_resume;
	}

	if (genpd->state_idx >= GENPD_MAX_NUM_STATES) {
		pr_warn("Initial state index out of bounds.\n");
		genpd->state_idx = GENPD_MAX_NUM_STATES - 1;
	}

	if (genpd->state_count > GENPD_MAX_NUM_STATES) {
		pr_warn("Limiting states to  %d\n", GENPD_MAX_NUM_STATES);
		genpd->state_count = GENPD_MAX_NUM_STATES;
	}

	/* Use only one "off" state if there were no states declared */
	if (genpd->state_count == 0)
		genpd->state_count = 1;

	mutex_lock(&gpd_list_lock);
	list_add(&genpd->gpd_list_node, &gpd_list);
	mutex_unlock(&gpd_list_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(pm_genpd_init);

static int genpd_remove(struct generic_pm_domain *genpd)
{
	struct gpd_link *l, *link;

	if (IS_ERR_OR_NULL(genpd))
		return -EINVAL;

	mutex_lock(&genpd->lock);

	if (genpd->has_provider) {
		mutex_unlock(&genpd->lock);
		pr_err("Provider present, unable to remove %s\n", genpd->name);
		return -EBUSY;
	}

	if (!list_empty(&genpd->master_links) || genpd->device_count) {
		mutex_unlock(&genpd->lock);
		pr_err("%s: unable to remove %s\n", __func__, genpd->name);
		return -EBUSY;
	}

	list_for_each_entry_safe(link, l, &genpd->slave_links, slave_node) {
		list_del(&link->master_node);
		list_del(&link->slave_node);
		kfree(link);
	}

	list_del(&genpd->gpd_list_node);
	mutex_unlock(&genpd->lock);
	cancel_work_sync(&genpd->power_off_work);
	pr_debug("%s: removed %s\n", __func__, genpd->name);

	return 0;
}

/**
 * pm_genpd_remove - Remove a generic I/O PM domain
 * @genpd: Pointer to PM domain that is to be removed.
 *
 * To remove the PM domain, this function:
 *  - Removes the PM domain as a subdomain to any parent domains,
 *    if it was added.
 *  - Removes the PM domain from the list of registered PM domains.
 *
 * The PM domain will only be removed, if the associated provider has
 * been removed, it is not a parent to any other PM domain and has no
 * devices associated with it.
 */
int pm_genpd_remove(struct generic_pm_domain *genpd)
{
	int ret;

	mutex_lock(&gpd_list_lock);
	ret = genpd_remove(genpd);
	mutex_unlock(&gpd_list_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(pm_genpd_remove);

#ifdef CONFIG_PM_GENERIC_DOMAINS_OF

typedef struct generic_pm_domain *(*genpd_xlate_t)(struct of_phandle_args *args,
						   void *data);

/*
 * Device Tree based PM domain providers.
 *
 * The code below implements generic device tree based PM domain providers that
 * bind device tree nodes with generic PM domains registered in the system.
 *
 * Any driver that registers generic PM domains and needs to support binding of
 * devices to these domains is supposed to register a PM domain provider, which
 * maps a PM domain specifier retrieved from the device tree to a PM domain.
 *
 * Two simple mapping functions have been provided for convenience:
 *  - genpd_xlate_simple() for 1:1 device tree node to PM domain mapping.
 *  - genpd_xlate_onecell() for mapping of multiple PM domains per node by
 *    index.
 */

/**
 * struct of_genpd_provider - PM domain provider registration structure
 * @link: Entry in global list of PM domain providers
 * @node: Pointer to device tree node of PM domain provider
 * @xlate: Provider-specific xlate callback mapping a set of specifier cells
 *         into a PM domain.
 * @data: context pointer to be passed into @xlate callback
 */
struct of_genpd_provider {
	struct list_head link;
	struct device_node *node;
	genpd_xlate_t xlate;
	void *data;
};

/* List of registered PM domain providers. */
static LIST_HEAD(of_genpd_providers);
/* Mutex to protect the list above. */
static DEFINE_MUTEX(of_genpd_mutex);

/**
 * genpd_xlate_simple() - Xlate function for direct node-domain mapping
 * @genpdspec: OF phandle args to map into a PM domain
 * @data: xlate function private data - pointer to struct generic_pm_domain
 *
 * This is a generic xlate function that can be used to model PM domains that
 * have their own device tree nodes. The private data of xlate function needs
 * to be a valid pointer to struct generic_pm_domain.
 */
static struct generic_pm_domain *genpd_xlate_simple(
					struct of_phandle_args *genpdspec,
					void *data)
{
	if (genpdspec->args_count != 0)
		return ERR_PTR(-EINVAL);
	return data;
}

/**
 * genpd_xlate_onecell() - Xlate function using a single index.
 * @genpdspec: OF phandle args to map into a PM domain
 * @data: xlate function private data - pointer to struct genpd_onecell_data
 *
 * This is a generic xlate function that can be used to model simple PM domain
 * controllers that have one device tree node and provide multiple PM domains.
 * A single cell is used as an index into an array of PM domains specified in
 * the genpd_onecell_data struct when registering the provider.
 */
static struct generic_pm_domain *genpd_xlate_onecell(
					struct of_phandle_args *genpdspec,
					void *data)
{
	struct genpd_onecell_data *genpd_data = data;
	unsigned int idx = genpdspec->args[0];

	if (genpdspec->args_count != 1)
		return ERR_PTR(-EINVAL);

	if (idx >= genpd_data->num_domains) {
		pr_err("%s: invalid domain index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	if (!genpd_data->domains[idx])
		return ERR_PTR(-ENOENT);

	return genpd_data->domains[idx];
}

/**
 * genpd_add_provider() - Register a PM domain provider for a node
 * @np: Device node pointer associated with the PM domain provider.
 * @xlate: Callback for decoding PM domain from phandle arguments.
 * @data: Context pointer for @xlate callback.
 */
static int genpd_add_provider(struct device_node *np, genpd_xlate_t xlate,
			      void *data)
{
	struct of_genpd_provider *cp;

	cp = kzalloc(sizeof(*cp), GFP_KERNEL);
	if (!cp)
		return -ENOMEM;

	cp->node = of_node_get(np);
	cp->data = data;
	cp->xlate = xlate;

	mutex_lock(&of_genpd_mutex);
	list_add(&cp->link, &of_genpd_providers);
	mutex_unlock(&of_genpd_mutex);
	pr_debug("Added domain provider from %s\n", np->full_name);

	return 0;
}

/**
 * of_genpd_add_provider_simple() - Register a simple PM domain provider
 * @np: Device node pointer associated with the PM domain provider.
 * @genpd: Pointer to PM domain associated with the PM domain provider.
 */
int of_genpd_add_provider_simple(struct device_node *np,
				 struct generic_pm_domain *genpd)
{
	int ret = -EINVAL;

	if (!np || !genpd)
		return -EINVAL;

	mutex_lock(&gpd_list_lock);

	if (pm_genpd_present(genpd))
		ret = genpd_add_provider(np, genpd_xlate_simple, genpd);

	if (!ret) {
		genpd->provider = &np->fwnode;
		genpd->has_provider = true;
	}

	mutex_unlock(&gpd_list_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(of_genpd_add_provider_simple);

/**
 * of_genpd_add_provider_onecell() - Register a onecell PM domain provider
 * @np: Device node pointer associated with the PM domain provider.
 * @data: Pointer to the data associated with the PM domain provider.
 */
int of_genpd_add_provider_onecell(struct device_node *np,
				  struct genpd_onecell_data *data)
{
	unsigned int i;
	int ret = -EINVAL;

	if (!np || !data)
		return -EINVAL;

	mutex_lock(&gpd_list_lock);

	for (i = 0; i < data->num_domains; i++) {
		if (!data->domains[i])
			continue;
		if (!pm_genpd_present(data->domains[i]))
			goto error;

		data->domains[i]->provider = &np->fwnode;
		data->domains[i]->has_provider = true;
	}

	ret = genpd_add_provider(np, genpd_xlate_onecell, data);
	if (ret < 0)
		goto error;

	mutex_unlock(&gpd_list_lock);

	return 0;

error:
	while (i--) {
		if (!data->domains[i])
			continue;
		data->domains[i]->provider = NULL;
		data->domains[i]->has_provider = false;
	}

	mutex_unlock(&gpd_list_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(of_genpd_add_provider_onecell);

/**
 * of_genpd_del_provider() - Remove a previously registered PM domain provider
 * @np: Device node pointer associated with the PM domain provider
 */
void of_genpd_del_provider(struct device_node *np)
{
	struct of_genpd_provider *cp, *tmp;
	struct generic_pm_domain *gpd;

	mutex_lock(&gpd_list_lock);
	mutex_lock(&of_genpd_mutex);
	list_for_each_entry_safe(cp, tmp, &of_genpd_providers, link) {
		if (cp->node == np) {
			/*
			 * For each PM domain associated with the
			 * provider, set the 'has_provider' to false
			 * so that the PM domain can be safely removed.
			 */
			list_for_each_entry(gpd, &gpd_list, gpd_list_node)
				if (gpd->provider == &np->fwnode)
					gpd->has_provider = false;

			list_del(&cp->link);
			of_node_put(cp->node);
			kfree(cp);
			break;
		}
	}
	mutex_unlock(&of_genpd_mutex);
	mutex_unlock(&gpd_list_lock);
}
EXPORT_SYMBOL_GPL(of_genpd_del_provider);

/**
 * genpd_get_from_provider() - Look-up PM domain
 * @genpdspec: OF phandle args to use for look-up
 *
 * Looks for a PM domain provider under the node specified by @genpdspec and if
 * found, uses xlate function of the provider to map phandle args to a PM
 * domain.
 *
 * Returns a valid pointer to struct generic_pm_domain on success or ERR_PTR()
 * on failure.
 */
static struct generic_pm_domain *genpd_get_from_provider(
					struct of_phandle_args *genpdspec)
{
	struct generic_pm_domain *genpd = ERR_PTR(-ENOENT);
	struct of_genpd_provider *provider;

	if (!genpdspec)
		return ERR_PTR(-EINVAL);

	mutex_lock(&of_genpd_mutex);

	/* Check if we have such a provider in our array */
	list_for_each_entry(provider, &of_genpd_providers, link) {
		if (provider->node == genpdspec->np)
			genpd = provider->xlate(genpdspec, provider->data);
		if (!IS_ERR(genpd))
			break;
	}

	mutex_unlock(&of_genpd_mutex);

	return genpd;
}

/**
 * of_genpd_add_device() - Add a device to an I/O PM domain
 * @genpdspec: OF phandle args to use for look-up PM domain
 * @dev: Device to be added.
 *
 * Looks-up an I/O PM domain based upon phandle args provided and adds
 * the device to the PM domain. Returns a negative error code on failure.
 */
int of_genpd_add_device(struct of_phandle_args *genpdspec, struct device *dev)
{
	struct generic_pm_domain *genpd;
	int ret;

	mutex_lock(&gpd_list_lock);

	genpd = genpd_get_from_provider(genpdspec);
	if (IS_ERR(genpd)) {
		ret = PTR_ERR(genpd);
		goto out;
	}

	ret = genpd_add_device(genpd, dev, NULL);

out:
	mutex_unlock(&gpd_list_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(of_genpd_add_device);

/**
 * of_genpd_add_subdomain - Add a subdomain to an I/O PM domain.
 * @parent_spec: OF phandle args to use for parent PM domain look-up
 * @subdomain_spec: OF phandle args to use for subdomain look-up
 *
 * Looks-up a parent PM domain and subdomain based upon phandle args
 * provided and adds the subdomain to the parent PM domain. Returns a
 * negative error code on failure.
 */
int of_genpd_add_subdomain(struct of_phandle_args *parent_spec,
			   struct of_phandle_args *subdomain_spec)
{
	struct generic_pm_domain *parent, *subdomain;
	int ret;

	mutex_lock(&gpd_list_lock);

	parent = genpd_get_from_provider(parent_spec);
	if (IS_ERR(parent)) {
		ret = PTR_ERR(parent);
		goto out;
	}

	subdomain = genpd_get_from_provider(subdomain_spec);
	if (IS_ERR(subdomain)) {
		ret = PTR_ERR(subdomain);
		goto out;
	}

	ret = genpd_add_subdomain(parent, subdomain);

out:
	mutex_unlock(&gpd_list_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(of_genpd_add_subdomain);

/**
 * of_genpd_remove_last - Remove the last PM domain registered for a provider
 * @provider: Pointer to device structure associated with provider
 *
 * Find the last PM domain that was added by a particular provider and
 * remove this PM domain from the list of PM domains. The provider is
 * identified by the 'provider' device structure that is passed. The PM
 * domain will only be removed, if the provider associated with domain
 * has been removed.
 *
 * Returns a valid pointer to struct generic_pm_domain on success or
 * ERR_PTR() on failure.
 */
struct generic_pm_domain *of_genpd_remove_last(struct device_node *np)
{
	struct generic_pm_domain *gpd, *tmp, *genpd = ERR_PTR(-ENOENT);
	int ret;

	if (IS_ERR_OR_NULL(np))
		return ERR_PTR(-EINVAL);

	mutex_lock(&gpd_list_lock);
	list_for_each_entry_safe(gpd, tmp, &gpd_list, gpd_list_node) {
		if (gpd->provider == &np->fwnode) {
			ret = genpd_remove(gpd);
			genpd = ret ? ERR_PTR(ret) : gpd;
			break;
		}
	}
	mutex_unlock(&gpd_list_lock);

	return genpd;
}
EXPORT_SYMBOL_GPL(of_genpd_remove_last);

/**
 * genpd_dev_pm_detach - Detach a device from its PM domain.
 * @dev: Device to detach.
 * @power_off: Currently not used
 *
 * Try to locate a corresponding generic PM domain, which the device was
 * attached to previously. If such is found, the device is detached from it.
 */
static void genpd_dev_pm_detach(struct device *dev, bool power_off)
{
	struct generic_pm_domain *pd;
	unsigned int i;
	int ret = 0;

	pd = dev_to_genpd(dev);
	if (IS_ERR(pd))
		return;

	dev_dbg(dev, "removing from PM domain %s\n", pd->name);

	for (i = 1; i < GENPD_RETRY_MAX_MS; i <<= 1) {
		ret = genpd_remove_device(pd, dev);
		if (ret != -EAGAIN)
			break;

		mdelay(i);
		cond_resched();
	}

	if (ret < 0) {
		dev_err(dev, "failed to remove from PM domain %s: %d",
			pd->name, ret);
		return;
	}

	/* Check if PM domain can be powered off after removing this device. */
	genpd_queue_power_off_work(pd);
}

static void genpd_dev_pm_sync(struct device *dev)
{
	struct generic_pm_domain *pd;

	pd = dev_to_genpd(dev);
	if (IS_ERR(pd))
		return;

	genpd_queue_power_off_work(pd);
}

/**
 * genpd_dev_pm_attach - Attach a device to its PM domain using DT.
 * @dev: Device to attach.
 *
 * Parse device's OF node to find a PM domain specifier. If such is found,
 * attaches the device to retrieved pm_domain ops.
 *
 * Both generic and legacy Samsung-specific DT bindings are supported to keep
 * backwards compatibility with existing DTBs.
 *
 * Returns 0 on successfully attached PM domain or negative error code. Note
 * that if a power-domain exists for the device, but it cannot be found or
 * turned on, then return -EPROBE_DEFER to ensure that the device is not
 * probed and to re-try again later.
 */
int genpd_dev_pm_attach(struct device *dev)
{
	struct of_phandle_args pd_args;
	struct generic_pm_domain *pd;
	unsigned int i;
	int ret;

	if (!dev->of_node)
		return -ENODEV;

	if (dev->pm_domain)
		return -EEXIST;

	ret = of_parse_phandle_with_args(dev->of_node, "power-domains",
					"#power-domain-cells", 0, &pd_args);
	if (ret < 0) {
		if (ret != -ENOENT)
			return ret;

		/*
		 * Try legacy Samsung-specific bindings
		 * (for backwards compatibility of DT ABI)
		 */
		pd_args.args_count = 0;
		pd_args.np = of_parse_phandle(dev->of_node,
						"samsung,power-domain", 0);
		if (!pd_args.np)
			return -ENOENT;
	}

	mutex_lock(&gpd_list_lock);
	pd = genpd_get_from_provider(&pd_args);
	of_node_put(pd_args.np);
	if (IS_ERR(pd)) {
		mutex_unlock(&gpd_list_lock);
		dev_dbg(dev, "%s() failed to find PM domain: %ld\n",
			__func__, PTR_ERR(pd));
		return -EPROBE_DEFER;
	}

	dev_dbg(dev, "adding to PM domain %s\n", pd->name);

	for (i = 1; i < GENPD_RETRY_MAX_MS; i <<= 1) {
		ret = genpd_add_device(pd, dev, NULL);
		if (ret != -EAGAIN)
			break;

		mdelay(i);
		cond_resched();
	}
	mutex_unlock(&gpd_list_lock);

	if (ret < 0) {
		dev_err(dev, "failed to add to PM domain %s: %d",
			pd->name, ret);
		goto out;
	}

	dev->pm_domain->detach = genpd_dev_pm_detach;
	dev->pm_domain->sync = genpd_dev_pm_sync;

	mutex_lock(&pd->lock);
	ret = genpd_poweron(pd, 0);
	mutex_unlock(&pd->lock);
out:
	return ret ? -EPROBE_DEFER : 0;
}
EXPORT_SYMBOL_GPL(genpd_dev_pm_attach);
#endif /* CONFIG_PM_GENERIC_DOMAINS_OF */


/***        debugfs support        ***/

#ifdef CONFIG_DEBUG_FS
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/kobject.h>
static struct dentry *pm_genpd_debugfs_dir;

/*
 * TODO: This function is a slightly modified version of rtpm_status_show
 * from sysfs.c, so generalize it.
 */
static void rtpm_status_str(struct seq_file *s, struct device *dev)
{
	static const char * const status_lookup[] = {
		[RPM_ACTIVE] = "active",
		[RPM_RESUMING] = "resuming",
		[RPM_SUSPENDED] = "suspended",
		[RPM_SUSPENDING] = "suspending"
	};
	const char *p = "";

	if (dev->power.runtime_error)
		p = "error";
	else if (dev->power.disable_depth)
		p = "unsupported";
	else if (dev->power.runtime_status < ARRAY_SIZE(status_lookup))
		p = status_lookup[dev->power.runtime_status];
	else
		WARN_ON(1);

	seq_puts(s, p);
}

static int pm_genpd_summary_one(struct seq_file *s,
				struct generic_pm_domain *genpd)
{
	static const char * const status_lookup[] = {
		[GPD_STATE_ACTIVE] = "on",
		[GPD_STATE_POWER_OFF] = "off"
	};
	struct pm_domain_data *pm_data;
	const char *kobj_path;
	struct gpd_link *link;
	char state[16];
	int ret;

	ret = mutex_lock_interruptible(&genpd->lock);
	if (ret)
		return -ERESTARTSYS;

	if (WARN_ON(genpd->status >= ARRAY_SIZE(status_lookup)))
		goto exit;
	if (genpd->status == GPD_STATE_POWER_OFF)
		snprintf(state, sizeof(state), "%s-%u",
			 status_lookup[genpd->status], genpd->state_idx);
	else
		snprintf(state, sizeof(state), "%s",
			 status_lookup[genpd->status]);
	seq_printf(s, "%-30s  %-15s ", genpd->name, state);

	/*
	 * Modifications on the list require holding locks on both
	 * master and slave, so we are safe.
	 * Also genpd->name is immutable.
	 */
	list_for_each_entry(link, &genpd->master_links, master_node) {
		seq_printf(s, "%s", link->slave->name);
		if (!list_is_last(&link->master_node, &genpd->master_links))
			seq_puts(s, ", ");
	}

	list_for_each_entry(pm_data, &genpd->dev_list, list_node) {
		kobj_path = kobject_get_path(&pm_data->dev->kobj, GFP_KERNEL);
		if (kobj_path == NULL)
			continue;

		seq_printf(s, "\n    %-50s  ", kobj_path);
		rtpm_status_str(s, pm_data->dev);
		kfree(kobj_path);
	}

	seq_puts(s, "\n");
exit:
	mutex_unlock(&genpd->lock);

	return 0;
}

static int pm_genpd_summary_show(struct seq_file *s, void *data)
{
	struct generic_pm_domain *genpd;
	int ret = 0;

	seq_puts(s, "domain                          status          slaves\n");
	seq_puts(s, "    /device                                             runtime status\n");
	seq_puts(s, "----------------------------------------------------------------------\n");

	ret = mutex_lock_interruptible(&gpd_list_lock);
	if (ret)
		return -ERESTARTSYS;

	list_for_each_entry(genpd, &gpd_list, gpd_list_node) {
		ret = pm_genpd_summary_one(s, genpd);
		if (ret)
			break;
	}
	mutex_unlock(&gpd_list_lock);

	return ret;
}

static int pm_genpd_summary_open(struct inode *inode, struct file *file)
{
	return single_open(file, pm_genpd_summary_show, NULL);
}

static const struct file_operations pm_genpd_summary_fops = {
	.open = pm_genpd_summary_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init pm_genpd_debug_init(void)
{
	struct dentry *d;

	pm_genpd_debugfs_dir = debugfs_create_dir("pm_genpd", NULL);

	if (!pm_genpd_debugfs_dir)
		return -ENOMEM;

	d = debugfs_create_file("pm_genpd_summary", S_IRUGO,
			pm_genpd_debugfs_dir, NULL, &pm_genpd_summary_fops);
	if (!d)
		return -ENOMEM;

	return 0;
}
late_initcall(pm_genpd_debug_init);

static void __exit pm_genpd_debug_exit(void)
{
	debugfs_remove_recursive(pm_genpd_debugfs_dir);
}
__exitcall(pm_genpd_debug_exit);
#endif /* CONFIG_DEBUG_FS */
