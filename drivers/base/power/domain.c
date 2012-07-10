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
#include <linux/pm_qos.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/suspend.h>
#include <linux/export.h>

#define GENPD_DEV_CALLBACK(genpd, type, callback, dev)		\
({								\
	type (*__routine)(struct device *__d); 			\
	type __ret = (type)0;					\
								\
	__routine = genpd->dev_ops.callback; 			\
	if (__routine) {					\
		__ret = __routine(dev); 			\
	} else {						\
		__routine = dev_gpd_data(dev)->ops.callback;	\
		if (__routine) 					\
			__ret = __routine(dev);			\
	}							\
	__ret;							\
})

#define GENPD_DEV_TIMED_CALLBACK(genpd, type, callback, dev, field, name)	\
({										\
	ktime_t __start = ktime_get();						\
	type __retval = GENPD_DEV_CALLBACK(genpd, type, callback, dev);		\
	s64 __elapsed = ktime_to_ns(ktime_sub(ktime_get(), __start));		\
	struct gpd_timing_data *__td = &dev_gpd_data(dev)->td;			\
	if (!__retval && __elapsed > __td->field) {				\
		__td->field = __elapsed;					\
		dev_warn(dev, name " latency exceeded, new value %lld ns\n",	\
			__elapsed);						\
		genpd->max_off_time_changed = true;				\
		__td->constraint_changed = true;				\
	}									\
	__retval;								\
})

static LIST_HEAD(gpd_list);
static DEFINE_MUTEX(gpd_list_lock);

#ifdef CONFIG_PM

struct generic_pm_domain *dev_to_genpd(struct device *dev)
{
	if (IS_ERR_OR_NULL(dev->pm_domain))
		return ERR_PTR(-EINVAL);

	return pd_to_genpd(dev->pm_domain);
}

static int genpd_stop_dev(struct generic_pm_domain *genpd, struct device *dev)
{
	return GENPD_DEV_TIMED_CALLBACK(genpd, int, stop, dev,
					stop_latency_ns, "stop");
}

static int genpd_start_dev(struct generic_pm_domain *genpd, struct device *dev)
{
	return GENPD_DEV_TIMED_CALLBACK(genpd, int, start, dev,
					start_latency_ns, "start");
}

static int genpd_save_dev(struct generic_pm_domain *genpd, struct device *dev)
{
	return GENPD_DEV_TIMED_CALLBACK(genpd, int, save_state, dev,
					save_state_latency_ns, "state save");
}

static int genpd_restore_dev(struct generic_pm_domain *genpd, struct device *dev)
{
	return GENPD_DEV_TIMED_CALLBACK(genpd, int, restore_state, dev,
					restore_state_latency_ns,
					"state restore");
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
	smp_mb__after_atomic_inc();
}

static void genpd_acquire_lock(struct generic_pm_domain *genpd)
{
	DEFINE_WAIT(wait);

	mutex_lock(&genpd->lock);
	/*
	 * Wait for the domain to transition into either the active,
	 * or the power off state.
	 */
	for (;;) {
		prepare_to_wait(&genpd->status_wait_queue, &wait,
				TASK_UNINTERRUPTIBLE);
		if (genpd->status == GPD_STATE_ACTIVE
		    || genpd->status == GPD_STATE_POWER_OFF)
			break;
		mutex_unlock(&genpd->lock);

		schedule();

		mutex_lock(&genpd->lock);
	}
	finish_wait(&genpd->status_wait_queue, &wait);
}

static void genpd_release_lock(struct generic_pm_domain *genpd)
{
	mutex_unlock(&genpd->lock);
}

static void genpd_set_active(struct generic_pm_domain *genpd)
{
	if (genpd->resume_count == 0)
		genpd->status = GPD_STATE_ACTIVE;
}

static void genpd_recalc_cpu_exit_latency(struct generic_pm_domain *genpd)
{
	s64 usecs64;

	if (!genpd->cpu_data)
		return;

	usecs64 = genpd->power_on_latency_ns;
	do_div(usecs64, NSEC_PER_USEC);
	usecs64 += genpd->cpu_data->saved_exit_latency;
	genpd->cpu_data->idle_state->exit_latency = usecs64;
}

/**
 * __pm_genpd_poweron - Restore power to a given PM domain and its masters.
 * @genpd: PM domain to power up.
 *
 * Restore power to @genpd and all of its masters so that it is possible to
 * resume a device belonging to it.
 */
static int __pm_genpd_poweron(struct generic_pm_domain *genpd)
	__releases(&genpd->lock) __acquires(&genpd->lock)
{
	struct gpd_link *link;
	DEFINE_WAIT(wait);
	int ret = 0;

	/* If the domain's master is being waited for, we have to wait too. */
	for (;;) {
		prepare_to_wait(&genpd->status_wait_queue, &wait,
				TASK_UNINTERRUPTIBLE);
		if (genpd->status != GPD_STATE_WAIT_MASTER)
			break;
		mutex_unlock(&genpd->lock);

		schedule();

		mutex_lock(&genpd->lock);
	}
	finish_wait(&genpd->status_wait_queue, &wait);

	if (genpd->status == GPD_STATE_ACTIVE
	    || (genpd->prepared_count > 0 && genpd->suspend_power_off))
		return 0;

	if (genpd->status != GPD_STATE_POWER_OFF) {
		genpd_set_active(genpd);
		return 0;
	}

	if (genpd->cpu_data) {
		cpuidle_pause_and_lock();
		genpd->cpu_data->idle_state->disabled = true;
		cpuidle_resume_and_unlock();
		goto out;
	}

	/*
	 * The list is guaranteed not to change while the loop below is being
	 * executed, unless one of the masters' .power_on() callbacks fiddles
	 * with it.
	 */
	list_for_each_entry(link, &genpd->slave_links, slave_node) {
		genpd_sd_counter_inc(link->master);
		genpd->status = GPD_STATE_WAIT_MASTER;

		mutex_unlock(&genpd->lock);

		ret = pm_genpd_poweron(link->master);

		mutex_lock(&genpd->lock);

		/*
		 * The "wait for parent" status is guaranteed not to change
		 * while the master is powering on.
		 */
		genpd->status = GPD_STATE_POWER_OFF;
		wake_up_all(&genpd->status_wait_queue);
		if (ret) {
			genpd_sd_counter_dec(link->master);
			goto err;
		}
	}

	if (genpd->power_on) {
		ktime_t time_start = ktime_get();
		s64 elapsed_ns;

		ret = genpd->power_on(genpd);
		if (ret)
			goto err;

		elapsed_ns = ktime_to_ns(ktime_sub(ktime_get(), time_start));
		if (elapsed_ns > genpd->power_on_latency_ns) {
			genpd->power_on_latency_ns = elapsed_ns;
			genpd->max_off_time_changed = true;
			genpd_recalc_cpu_exit_latency(genpd);
			if (genpd->name)
				pr_warning("%s: Power-on latency exceeded, "
					"new value %lld ns\n", genpd->name,
					elapsed_ns);
		}
	}

 out:
	genpd_set_active(genpd);

	return 0;

 err:
	list_for_each_entry_continue_reverse(link, &genpd->slave_links, slave_node)
		genpd_sd_counter_dec(link->master);

	return ret;
}

/**
 * pm_genpd_poweron - Restore power to a given PM domain and its masters.
 * @genpd: PM domain to power up.
 */
int pm_genpd_poweron(struct generic_pm_domain *genpd)
{
	int ret;

	mutex_lock(&genpd->lock);
	ret = __pm_genpd_poweron(genpd);
	mutex_unlock(&genpd->lock);
	return ret;
}

#endif /* CONFIG_PM */

#ifdef CONFIG_PM_RUNTIME

static int genpd_dev_pm_qos_notifier(struct notifier_block *nb,
				     unsigned long val, void *ptr)
{
	struct generic_pm_domain_data *gpd_data;
	struct device *dev;

	gpd_data = container_of(nb, struct generic_pm_domain_data, nb);

	mutex_lock(&gpd_data->lock);
	dev = gpd_data->base.dev;
	if (!dev) {
		mutex_unlock(&gpd_data->lock);
		return NOTIFY_DONE;
	}
	mutex_unlock(&gpd_data->lock);

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
 * __pm_genpd_save_device - Save the pre-suspend state of a device.
 * @pdd: Domain data of the device to save the state of.
 * @genpd: PM domain the device belongs to.
 */
static int __pm_genpd_save_device(struct pm_domain_data *pdd,
				  struct generic_pm_domain *genpd)
	__releases(&genpd->lock) __acquires(&genpd->lock)
{
	struct generic_pm_domain_data *gpd_data = to_gpd_data(pdd);
	struct device *dev = pdd->dev;
	int ret = 0;

	if (gpd_data->need_restore)
		return 0;

	mutex_unlock(&genpd->lock);

	genpd_start_dev(genpd, dev);
	ret = genpd_save_dev(genpd, dev);
	genpd_stop_dev(genpd, dev);

	mutex_lock(&genpd->lock);

	if (!ret)
		gpd_data->need_restore = true;

	return ret;
}

/**
 * __pm_genpd_restore_device - Restore the pre-suspend state of a device.
 * @pdd: Domain data of the device to restore the state of.
 * @genpd: PM domain the device belongs to.
 */
static void __pm_genpd_restore_device(struct pm_domain_data *pdd,
				      struct generic_pm_domain *genpd)
	__releases(&genpd->lock) __acquires(&genpd->lock)
{
	struct generic_pm_domain_data *gpd_data = to_gpd_data(pdd);
	struct device *dev = pdd->dev;
	bool need_restore = gpd_data->need_restore;

	gpd_data->need_restore = false;
	mutex_unlock(&genpd->lock);

	genpd_start_dev(genpd, dev);
	if (need_restore)
		genpd_restore_dev(genpd, dev);

	mutex_lock(&genpd->lock);
}

/**
 * genpd_abort_poweroff - Check if a PM domain power off should be aborted.
 * @genpd: PM domain to check.
 *
 * Return true if a PM domain's status changed to GPD_STATE_ACTIVE during
 * a "power off" operation, which means that a "power on" has occured in the
 * meantime, or if its resume_count field is different from zero, which means
 * that one of its devices has been resumed in the meantime.
 */
static bool genpd_abort_poweroff(struct generic_pm_domain *genpd)
{
	return genpd->status == GPD_STATE_WAIT_MASTER
		|| genpd->status == GPD_STATE_ACTIVE || genpd->resume_count > 0;
}

/**
 * genpd_queue_power_off_work - Queue up the execution of pm_genpd_poweroff().
 * @genpd: PM domait to power off.
 *
 * Queue up the execution of pm_genpd_poweroff() unless it's already been done
 * before.
 */
void genpd_queue_power_off_work(struct generic_pm_domain *genpd)
{
	if (!work_pending(&genpd->power_off_work))
		queue_work(pm_wq, &genpd->power_off_work);
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
	__releases(&genpd->lock) __acquires(&genpd->lock)
{
	struct pm_domain_data *pdd;
	struct gpd_link *link;
	unsigned int not_suspended;
	int ret = 0;

 start:
	/*
	 * Do not try to power off the domain in the following situations:
	 * (1) The domain is already in the "power off" state.
	 * (2) The domain is waiting for its master to power up.
	 * (3) One of the domain's devices is being resumed right now.
	 * (4) System suspend is in progress.
	 */
	if (genpd->status == GPD_STATE_POWER_OFF
	    || genpd->status == GPD_STATE_WAIT_MASTER
	    || genpd->resume_count > 0 || genpd->prepared_count > 0)
		return 0;

	if (atomic_read(&genpd->sd_count) > 0)
		return -EBUSY;

	not_suspended = 0;
	list_for_each_entry(pdd, &genpd->dev_list, list_node)
		if (pdd->dev->driver && (!pm_runtime_suspended(pdd->dev)
		    || pdd->dev->power.irq_safe || to_gpd_data(pdd)->always_on))
			not_suspended++;

	if (not_suspended > genpd->in_progress)
		return -EBUSY;

	if (genpd->poweroff_task) {
		/*
		 * Another instance of pm_genpd_poweroff() is executing
		 * callbacks, so tell it to start over and return.
		 */
		genpd->status = GPD_STATE_REPEAT;
		return 0;
	}

	if (genpd->gov && genpd->gov->power_down_ok) {
		if (!genpd->gov->power_down_ok(&genpd->domain))
			return -EAGAIN;
	}

	genpd->status = GPD_STATE_BUSY;
	genpd->poweroff_task = current;

	list_for_each_entry_reverse(pdd, &genpd->dev_list, list_node) {
		ret = atomic_read(&genpd->sd_count) == 0 ?
			__pm_genpd_save_device(pdd, genpd) : -EBUSY;

		if (genpd_abort_poweroff(genpd))
			goto out;

		if (ret) {
			genpd_set_active(genpd);
			goto out;
		}

		if (genpd->status == GPD_STATE_REPEAT) {
			genpd->poweroff_task = NULL;
			goto start;
		}
	}

	if (genpd->cpu_data) {
		/*
		 * If cpu_data is set, cpuidle should turn the domain off when
		 * the CPU in it is idle.  In that case we don't decrement the
		 * subdomain counts of the master domains, so that power is not
		 * removed from the current domain prematurely as a result of
		 * cutting off the masters' power.
		 */
		genpd->status = GPD_STATE_POWER_OFF;
		cpuidle_pause_and_lock();
		genpd->cpu_data->idle_state->disabled = false;
		cpuidle_resume_and_unlock();
		goto out;
	}

	if (genpd->power_off) {
		ktime_t time_start;
		s64 elapsed_ns;

		if (atomic_read(&genpd->sd_count) > 0) {
			ret = -EBUSY;
			goto out;
		}

		time_start = ktime_get();

		/*
		 * If sd_count > 0 at this point, one of the subdomains hasn't
		 * managed to call pm_genpd_poweron() for the master yet after
		 * incrementing it.  In that case pm_genpd_poweron() will wait
		 * for us to drop the lock, so we can call .power_off() and let
		 * the pm_genpd_poweron() restore power for us (this shouldn't
		 * happen very often).
		 */
		ret = genpd->power_off(genpd);
		if (ret == -EBUSY) {
			genpd_set_active(genpd);
			goto out;
		}

		elapsed_ns = ktime_to_ns(ktime_sub(ktime_get(), time_start));
		if (elapsed_ns > genpd->power_off_latency_ns) {
			genpd->power_off_latency_ns = elapsed_ns;
			genpd->max_off_time_changed = true;
			if (genpd->name)
				pr_warning("%s: Power-off latency exceeded, "
					"new value %lld ns\n", genpd->name,
					elapsed_ns);
		}
	}

	genpd->status = GPD_STATE_POWER_OFF;

	list_for_each_entry(link, &genpd->slave_links, slave_node) {
		genpd_sd_counter_dec(link->master);
		genpd_queue_power_off_work(link->master);
	}

 out:
	genpd->poweroff_task = NULL;
	wake_up_all(&genpd->status_wait_queue);
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

	genpd_acquire_lock(genpd);
	pm_genpd_poweroff(genpd);
	genpd_release_lock(genpd);
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
	bool (*stop_ok)(struct device *__dev);
	int ret;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	might_sleep_if(!genpd->dev_irq_safe);

	if (dev_gpd_data(dev)->always_on)
		return -EBUSY;

	stop_ok = genpd->gov ? genpd->gov->stop_ok : NULL;
	if (stop_ok && !stop_ok(dev))
		return -EBUSY;

	ret = genpd_stop_dev(genpd, dev);
	if (ret)
		return ret;

	/*
	 * If power.irq_safe is set, this routine will be run with interrupts
	 * off, so it can't use mutexes.
	 */
	if (dev->power.irq_safe)
		return 0;

	mutex_lock(&genpd->lock);
	genpd->in_progress++;
	pm_genpd_poweroff(genpd);
	genpd->in_progress--;
	mutex_unlock(&genpd->lock);

	return 0;
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
	DEFINE_WAIT(wait);
	int ret;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	might_sleep_if(!genpd->dev_irq_safe);

	/* If power.irq_safe, the PM domain is never powered off. */
	if (dev->power.irq_safe)
		return genpd_start_dev(genpd, dev);

	mutex_lock(&genpd->lock);
	ret = __pm_genpd_poweron(genpd);
	if (ret) {
		mutex_unlock(&genpd->lock);
		return ret;
	}
	genpd->status = GPD_STATE_BUSY;
	genpd->resume_count++;
	for (;;) {
		prepare_to_wait(&genpd->status_wait_queue, &wait,
				TASK_UNINTERRUPTIBLE);
		/*
		 * If current is the powering off task, we have been called
		 * reentrantly from one of the device callbacks, so we should
		 * not wait.
		 */
		if (!genpd->poweroff_task || genpd->poweroff_task == current)
			break;
		mutex_unlock(&genpd->lock);

		schedule();

		mutex_lock(&genpd->lock);
	}
	finish_wait(&genpd->status_wait_queue, &wait);
	__pm_genpd_restore_device(dev->power.subsys_data->domain_data, genpd);
	genpd->resume_count--;
	genpd_set_active(genpd);
	wake_up_all(&genpd->status_wait_queue);
	mutex_unlock(&genpd->lock);

	return 0;
}

/**
 * pm_genpd_poweroff_unused - Power off all PM domains with no devices in use.
 */
void pm_genpd_poweroff_unused(void)
{
	struct generic_pm_domain *genpd;

	mutex_lock(&gpd_list_lock);

	list_for_each_entry(genpd, &gpd_list, gpd_list_node)
		genpd_queue_power_off_work(genpd);

	mutex_unlock(&gpd_list_lock);
}

#else

static inline int genpd_dev_pm_qos_notifier(struct notifier_block *nb,
					    unsigned long val, void *ptr)
{
	return NOTIFY_DONE;
}

static inline void genpd_power_off_work_fn(struct work_struct *work) {}

#define pm_genpd_runtime_suspend	NULL
#define pm_genpd_runtime_resume		NULL

#endif /* CONFIG_PM_RUNTIME */

#ifdef CONFIG_PM_SLEEP

static bool genpd_dev_active_wakeup(struct generic_pm_domain *genpd,
				    struct device *dev)
{
	return GENPD_DEV_CALLBACK(genpd, bool, active_wakeup, dev);
}

static int genpd_suspend_dev(struct generic_pm_domain *genpd, struct device *dev)
{
	return GENPD_DEV_CALLBACK(genpd, int, suspend, dev);
}

static int genpd_suspend_late(struct generic_pm_domain *genpd, struct device *dev)
{
	return GENPD_DEV_CALLBACK(genpd, int, suspend_late, dev);
}

static int genpd_resume_early(struct generic_pm_domain *genpd, struct device *dev)
{
	return GENPD_DEV_CALLBACK(genpd, int, resume_early, dev);
}

static int genpd_resume_dev(struct generic_pm_domain *genpd, struct device *dev)
{
	return GENPD_DEV_CALLBACK(genpd, int, resume, dev);
}

static int genpd_freeze_dev(struct generic_pm_domain *genpd, struct device *dev)
{
	return GENPD_DEV_CALLBACK(genpd, int, freeze, dev);
}

static int genpd_freeze_late(struct generic_pm_domain *genpd, struct device *dev)
{
	return GENPD_DEV_CALLBACK(genpd, int, freeze_late, dev);
}

static int genpd_thaw_early(struct generic_pm_domain *genpd, struct device *dev)
{
	return GENPD_DEV_CALLBACK(genpd, int, thaw_early, dev);
}

static int genpd_thaw_dev(struct generic_pm_domain *genpd, struct device *dev)
{
	return GENPD_DEV_CALLBACK(genpd, int, thaw, dev);
}

/**
 * pm_genpd_sync_poweroff - Synchronously power off a PM domain and its masters.
 * @genpd: PM domain to power off, if possible.
 *
 * Check if the given PM domain can be powered off (during system suspend or
 * hibernation) and do that if so.  Also, in that case propagate to its masters.
 *
 * This function is only called in "noirq" stages of system power transitions,
 * so it need not acquire locks (all of the "noirq" callbacks are executed
 * sequentially, so it is guaranteed that it will never run twice in parallel).
 */
static void pm_genpd_sync_poweroff(struct generic_pm_domain *genpd)
{
	struct gpd_link *link;

	if (genpd->status == GPD_STATE_POWER_OFF)
		return;

	if (genpd->suspended_count != genpd->device_count
	    || atomic_read(&genpd->sd_count) > 0)
		return;

	if (genpd->power_off)
		genpd->power_off(genpd);

	genpd->status = GPD_STATE_POWER_OFF;

	list_for_each_entry(link, &genpd->slave_links, slave_node) {
		genpd_sd_counter_dec(link->master);
		pm_genpd_sync_poweroff(link->master);
	}
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
	pm_runtime_get_noresume(dev);
	if (pm_runtime_barrier(dev) && device_may_wakeup(dev))
		pm_wakeup_event(dev, 0);

	if (pm_wakeup_pending()) {
		pm_runtime_put_sync(dev);
		return -EBUSY;
	}

	if (resume_needed(dev, genpd))
		pm_runtime_resume(dev);

	genpd_acquire_lock(genpd);

	if (genpd->prepared_count++ == 0) {
		genpd->suspended_count = 0;
		genpd->suspend_power_off = genpd->status == GPD_STATE_POWER_OFF;
	}

	genpd_release_lock(genpd);

	if (genpd->suspend_power_off) {
		pm_runtime_put_noidle(dev);
		return 0;
	}

	/*
	 * The PM domain must be in the GPD_STATE_ACTIVE state at this point,
	 * so pm_genpd_poweron() will return immediately, but if the device
	 * is suspended (e.g. it's been stopped by genpd_stop_dev()), we need
	 * to make it operational.
	 */
	pm_runtime_resume(dev);
	__pm_runtime_disable(dev, false);

	ret = pm_generic_prepare(dev);
	if (ret) {
		mutex_lock(&genpd->lock);

		if (--genpd->prepared_count == 0)
			genpd->suspend_power_off = false;

		mutex_unlock(&genpd->lock);
		pm_runtime_enable(dev);
	}

	pm_runtime_put_sync(dev);
	return ret;
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

	return genpd->suspend_power_off ? 0 : genpd_suspend_dev(genpd, dev);
}

/**
 * pm_genpd_suspend_late - Late suspend of a device from an I/O PM domain.
 * @dev: Device to suspend.
 *
 * Carry out a late suspend of a device under the assumption that its
 * pm_domain field points to the domain member of an object of type
 * struct generic_pm_domain representing a PM domain consisting of I/O devices.
 */
static int pm_genpd_suspend_late(struct device *dev)
{
	struct generic_pm_domain *genpd;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	return genpd->suspend_power_off ? 0 : genpd_suspend_late(genpd, dev);
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

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	if (genpd->suspend_power_off || dev_gpd_data(dev)->always_on
	    || (dev->power.wakeup_path && genpd_dev_active_wakeup(genpd, dev)))
		return 0;

	genpd_stop_dev(genpd, dev);

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
 * pm_genpd_resume_noirq - Start of resume of device in an I/O PM domain.
 * @dev: Device to resume.
 *
 * Restore power to the device's PM domain, if necessary, and start the device.
 */
static int pm_genpd_resume_noirq(struct device *dev)
{
	struct generic_pm_domain *genpd;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	if (genpd->suspend_power_off || dev_gpd_data(dev)->always_on
	    || (dev->power.wakeup_path && genpd_dev_active_wakeup(genpd, dev)))
		return 0;

	/*
	 * Since all of the "noirq" callbacks are executed sequentially, it is
	 * guaranteed that this function will never run twice in parallel for
	 * the same PM domain, so it is not necessary to use locking here.
	 */
	pm_genpd_poweron(genpd);
	genpd->suspended_count--;

	return genpd_start_dev(genpd, dev);
}

/**
 * pm_genpd_resume_early - Early resume of a device in an I/O PM domain.
 * @dev: Device to resume.
 *
 * Carry out an early resume of a device under the assumption that its
 * pm_domain field points to the domain member of an object of type
 * struct generic_pm_domain representing a power domain consisting of I/O
 * devices.
 */
static int pm_genpd_resume_early(struct device *dev)
{
	struct generic_pm_domain *genpd;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	return genpd->suspend_power_off ? 0 : genpd_resume_early(genpd, dev);
}

/**
 * pm_genpd_resume - Resume of device in an I/O PM domain.
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

	return genpd->suspend_power_off ? 0 : genpd_resume_dev(genpd, dev);
}

/**
 * pm_genpd_freeze - Freezing a device in an I/O PM domain.
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

	return genpd->suspend_power_off ? 0 : genpd_freeze_dev(genpd, dev);
}

/**
 * pm_genpd_freeze_late - Late freeze of a device in an I/O PM domain.
 * @dev: Device to freeze.
 *
 * Carry out a late freeze of a device under the assumption that its
 * pm_domain field points to the domain member of an object of type
 * struct generic_pm_domain representing a power domain consisting of I/O
 * devices.
 */
static int pm_genpd_freeze_late(struct device *dev)
{
	struct generic_pm_domain *genpd;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	return genpd->suspend_power_off ? 0 : genpd_freeze_late(genpd, dev);
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

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	return genpd->suspend_power_off || dev_gpd_data(dev)->always_on ?
		0 : genpd_stop_dev(genpd, dev);
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

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	return genpd->suspend_power_off || dev_gpd_data(dev)->always_on ?
		0 : genpd_start_dev(genpd, dev);
}

/**
 * pm_genpd_thaw_early - Early thaw of device in an I/O PM domain.
 * @dev: Device to thaw.
 *
 * Carry out an early thaw of a device under the assumption that its
 * pm_domain field points to the domain member of an object of type
 * struct generic_pm_domain representing a power domain consisting of I/O
 * devices.
 */
static int pm_genpd_thaw_early(struct device *dev)
{
	struct generic_pm_domain *genpd;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	return genpd->suspend_power_off ? 0 : genpd_thaw_early(genpd, dev);
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

	return genpd->suspend_power_off ? 0 : genpd_thaw_dev(genpd, dev);
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
	if (genpd->suspended_count++ == 0) {
		/*
		 * The boot kernel might put the domain into arbitrary state,
		 * so make it appear as powered off to pm_genpd_poweron(), so
		 * that it tries to power it on in case it was really off.
		 */
		genpd->status = GPD_STATE_POWER_OFF;
		if (genpd->suspend_power_off) {
			/*
			 * If the domain was off before the hibernation, make
			 * sure it will be off going forward.
			 */
			if (genpd->power_off)
				genpd->power_off(genpd);

			return 0;
		}
	}

	if (genpd->suspend_power_off)
		return 0;

	pm_genpd_poweron(genpd);

	return dev_gpd_data(dev)->always_on ? 0 : genpd_start_dev(genpd, dev);
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
#define pm_genpd_suspend_late		NULL
#define pm_genpd_suspend_noirq		NULL
#define pm_genpd_resume_early		NULL
#define pm_genpd_resume_noirq		NULL
#define pm_genpd_resume			NULL
#define pm_genpd_freeze			NULL
#define pm_genpd_freeze_late		NULL
#define pm_genpd_freeze_noirq		NULL
#define pm_genpd_thaw_early		NULL
#define pm_genpd_thaw_noirq		NULL
#define pm_genpd_thaw			NULL
#define pm_genpd_restore_noirq		NULL
#define pm_genpd_complete		NULL

#endif /* CONFIG_PM_SLEEP */

static struct generic_pm_domain_data *__pm_genpd_alloc_dev_data(struct device *dev)
{
	struct generic_pm_domain_data *gpd_data;

	gpd_data = kzalloc(sizeof(*gpd_data), GFP_KERNEL);
	if (!gpd_data)
		return NULL;

	mutex_init(&gpd_data->lock);
	gpd_data->nb.notifier_call = genpd_dev_pm_qos_notifier;
	dev_pm_qos_add_notifier(dev, &gpd_data->nb);
	return gpd_data;
}

static void __pm_genpd_free_dev_data(struct device *dev,
				     struct generic_pm_domain_data *gpd_data)
{
	dev_pm_qos_remove_notifier(dev, &gpd_data->nb);
	kfree(gpd_data);
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
	struct generic_pm_domain_data *gpd_data_new, *gpd_data = NULL;
	struct pm_domain_data *pdd;
	int ret = 0;

	dev_dbg(dev, "%s()\n", __func__);

	if (IS_ERR_OR_NULL(genpd) || IS_ERR_OR_NULL(dev))
		return -EINVAL;

	gpd_data_new = __pm_genpd_alloc_dev_data(dev);
	if (!gpd_data_new)
		return -ENOMEM;

	genpd_acquire_lock(genpd);

	if (genpd->prepared_count > 0) {
		ret = -EAGAIN;
		goto out;
	}

	list_for_each_entry(pdd, &genpd->dev_list, list_node)
		if (pdd->dev == dev) {
			ret = -EINVAL;
			goto out;
		}

	ret = dev_pm_get_subsys_data(dev);
	if (ret)
		goto out;

	genpd->device_count++;
	genpd->max_off_time_changed = true;

	spin_lock_irq(&dev->power.lock);

	dev->pm_domain = &genpd->domain;
	if (dev->power.subsys_data->domain_data) {
		gpd_data = to_gpd_data(dev->power.subsys_data->domain_data);
	} else {
		gpd_data = gpd_data_new;
		dev->power.subsys_data->domain_data = &gpd_data->base;
	}
	gpd_data->refcount++;
	if (td)
		gpd_data->td = *td;

	spin_unlock_irq(&dev->power.lock);

	mutex_lock(&gpd_data->lock);
	gpd_data->base.dev = dev;
	list_add_tail(&gpd_data->base.list_node, &genpd->dev_list);
	gpd_data->need_restore = genpd->status == GPD_STATE_POWER_OFF;
	gpd_data->td.constraint_changed = true;
	gpd_data->td.effective_constraint_ns = -1;
	mutex_unlock(&gpd_data->lock);

 out:
	genpd_release_lock(genpd);

	if (gpd_data != gpd_data_new)
		__pm_genpd_free_dev_data(dev, gpd_data_new);

	return ret;
}

/**
 * __pm_genpd_of_add_device - Add a device to an I/O PM domain.
 * @genpd_node: Device tree node pointer representing a PM domain to which the
 *   the device is added to.
 * @dev: Device to be added.
 * @td: Set of PM QoS timing parameters to attach to the device.
 */
int __pm_genpd_of_add_device(struct device_node *genpd_node, struct device *dev,
			     struct gpd_timing_data *td)
{
	struct generic_pm_domain *genpd = NULL, *gpd;

	dev_dbg(dev, "%s()\n", __func__);

	if (IS_ERR_OR_NULL(genpd_node) || IS_ERR_OR_NULL(dev))
		return -EINVAL;

	mutex_lock(&gpd_list_lock);
	list_for_each_entry(gpd, &gpd_list, gpd_list_node) {
		if (gpd->of_node == genpd_node) {
			genpd = gpd;
			break;
		}
	}
	mutex_unlock(&gpd_list_lock);

	if (!genpd)
		return -EINVAL;

	return __pm_genpd_add_device(genpd, dev, td);
}

/**
 * pm_genpd_remove_device - Remove a device from an I/O PM domain.
 * @genpd: PM domain to remove the device from.
 * @dev: Device to be removed.
 */
int pm_genpd_remove_device(struct generic_pm_domain *genpd,
			   struct device *dev)
{
	struct generic_pm_domain_data *gpd_data;
	struct pm_domain_data *pdd;
	bool remove = false;
	int ret = 0;

	dev_dbg(dev, "%s()\n", __func__);

	if (IS_ERR_OR_NULL(genpd) || IS_ERR_OR_NULL(dev)
	    ||  IS_ERR_OR_NULL(dev->pm_domain)
	    ||  pd_to_genpd(dev->pm_domain) != genpd)
		return -EINVAL;

	genpd_acquire_lock(genpd);

	if (genpd->prepared_count > 0) {
		ret = -EAGAIN;
		goto out;
	}

	genpd->device_count--;
	genpd->max_off_time_changed = true;

	spin_lock_irq(&dev->power.lock);

	dev->pm_domain = NULL;
	pdd = dev->power.subsys_data->domain_data;
	list_del_init(&pdd->list_node);
	gpd_data = to_gpd_data(pdd);
	if (--gpd_data->refcount == 0) {
		dev->power.subsys_data->domain_data = NULL;
		remove = true;
	}

	spin_unlock_irq(&dev->power.lock);

	mutex_lock(&gpd_data->lock);
	pdd->dev = NULL;
	mutex_unlock(&gpd_data->lock);

	genpd_release_lock(genpd);

	dev_pm_put_subsys_data(dev);
	if (remove)
		__pm_genpd_free_dev_data(dev, gpd_data);

	return 0;

 out:
	genpd_release_lock(genpd);

	return ret;
}

/**
 * pm_genpd_dev_always_on - Set/unset the "always on" flag for a given device.
 * @dev: Device to set/unset the flag for.
 * @val: The new value of the device's "always on" flag.
 */
void pm_genpd_dev_always_on(struct device *dev, bool val)
{
	struct pm_subsys_data *psd;
	unsigned long flags;

	spin_lock_irqsave(&dev->power.lock, flags);

	psd = dev_to_psd(dev);
	if (psd && psd->domain_data)
		to_gpd_data(psd->domain_data)->always_on = val;

	spin_unlock_irqrestore(&dev->power.lock, flags);
}
EXPORT_SYMBOL_GPL(pm_genpd_dev_always_on);

/**
 * pm_genpd_dev_need_restore - Set/unset the device's "need restore" flag.
 * @dev: Device to set/unset the flag for.
 * @val: The new value of the device's "need restore" flag.
 */
void pm_genpd_dev_need_restore(struct device *dev, bool val)
{
	struct pm_subsys_data *psd;
	unsigned long flags;

	spin_lock_irqsave(&dev->power.lock, flags);

	psd = dev_to_psd(dev);
	if (psd && psd->domain_data)
		to_gpd_data(psd->domain_data)->need_restore = val;

	spin_unlock_irqrestore(&dev->power.lock, flags);
}
EXPORT_SYMBOL_GPL(pm_genpd_dev_need_restore);

/**
 * pm_genpd_add_subdomain - Add a subdomain to an I/O PM domain.
 * @genpd: Master PM domain to add the subdomain to.
 * @subdomain: Subdomain to be added.
 */
int pm_genpd_add_subdomain(struct generic_pm_domain *genpd,
			   struct generic_pm_domain *subdomain)
{
	struct gpd_link *link;
	int ret = 0;

	if (IS_ERR_OR_NULL(genpd) || IS_ERR_OR_NULL(subdomain))
		return -EINVAL;

 start:
	genpd_acquire_lock(genpd);
	mutex_lock_nested(&subdomain->lock, SINGLE_DEPTH_NESTING);

	if (subdomain->status != GPD_STATE_POWER_OFF
	    && subdomain->status != GPD_STATE_ACTIVE) {
		mutex_unlock(&subdomain->lock);
		genpd_release_lock(genpd);
		goto start;
	}

	if (genpd->status == GPD_STATE_POWER_OFF
	    &&  subdomain->status != GPD_STATE_POWER_OFF) {
		ret = -EINVAL;
		goto out;
	}

	list_for_each_entry(link, &genpd->master_links, master_node) {
		if (link->slave == subdomain && link->master == genpd) {
			ret = -EINVAL;
			goto out;
		}
	}

	link = kzalloc(sizeof(*link), GFP_KERNEL);
	if (!link) {
		ret = -ENOMEM;
		goto out;
	}
	link->master = genpd;
	list_add_tail(&link->master_node, &genpd->master_links);
	link->slave = subdomain;
	list_add_tail(&link->slave_node, &subdomain->slave_links);
	if (subdomain->status != GPD_STATE_POWER_OFF)
		genpd_sd_counter_inc(genpd);

 out:
	mutex_unlock(&subdomain->lock);
	genpd_release_lock(genpd);

	return ret;
}

/**
 * pm_genpd_remove_subdomain - Remove a subdomain from an I/O PM domain.
 * @genpd: Master PM domain to remove the subdomain from.
 * @subdomain: Subdomain to be removed.
 */
int pm_genpd_remove_subdomain(struct generic_pm_domain *genpd,
			      struct generic_pm_domain *subdomain)
{
	struct gpd_link *link;
	int ret = -EINVAL;

	if (IS_ERR_OR_NULL(genpd) || IS_ERR_OR_NULL(subdomain))
		return -EINVAL;

 start:
	genpd_acquire_lock(genpd);

	list_for_each_entry(link, &genpd->master_links, master_node) {
		if (link->slave != subdomain)
			continue;

		mutex_lock_nested(&subdomain->lock, SINGLE_DEPTH_NESTING);

		if (subdomain->status != GPD_STATE_POWER_OFF
		    && subdomain->status != GPD_STATE_ACTIVE) {
			mutex_unlock(&subdomain->lock);
			genpd_release_lock(genpd);
			goto start;
		}

		list_del(&link->master_node);
		list_del(&link->slave_node);
		kfree(link);
		if (subdomain->status != GPD_STATE_POWER_OFF)
			genpd_sd_counter_dec(genpd);

		mutex_unlock(&subdomain->lock);

		ret = 0;
		break;
	}

	genpd_release_lock(genpd);

	return ret;
}

/**
 * pm_genpd_add_callbacks - Add PM domain callbacks to a given device.
 * @dev: Device to add the callbacks to.
 * @ops: Set of callbacks to add.
 * @td: Timing data to add to the device along with the callbacks (optional).
 *
 * Every call to this routine should be balanced with a call to
 * __pm_genpd_remove_callbacks() and they must not be nested.
 */
int pm_genpd_add_callbacks(struct device *dev, struct gpd_dev_ops *ops,
			   struct gpd_timing_data *td)
{
	struct generic_pm_domain_data *gpd_data_new, *gpd_data = NULL;
	int ret = 0;

	if (!(dev && ops))
		return -EINVAL;

	gpd_data_new = __pm_genpd_alloc_dev_data(dev);
	if (!gpd_data_new)
		return -ENOMEM;

	pm_runtime_disable(dev);
	device_pm_lock();

	ret = dev_pm_get_subsys_data(dev);
	if (ret)
		goto out;

	spin_lock_irq(&dev->power.lock);

	if (dev->power.subsys_data->domain_data) {
		gpd_data = to_gpd_data(dev->power.subsys_data->domain_data);
	} else {
		gpd_data = gpd_data_new;
		dev->power.subsys_data->domain_data = &gpd_data->base;
	}
	gpd_data->refcount++;
	gpd_data->ops = *ops;
	if (td)
		gpd_data->td = *td;

	spin_unlock_irq(&dev->power.lock);

 out:
	device_pm_unlock();
	pm_runtime_enable(dev);

	if (gpd_data != gpd_data_new)
		__pm_genpd_free_dev_data(dev, gpd_data_new);

	return ret;
}
EXPORT_SYMBOL_GPL(pm_genpd_add_callbacks);

/**
 * __pm_genpd_remove_callbacks - Remove PM domain callbacks from a given device.
 * @dev: Device to remove the callbacks from.
 * @clear_td: If set, clear the device's timing data too.
 *
 * This routine can only be called after pm_genpd_add_callbacks().
 */
int __pm_genpd_remove_callbacks(struct device *dev, bool clear_td)
{
	struct generic_pm_domain_data *gpd_data = NULL;
	bool remove = false;
	int ret = 0;

	if (!(dev && dev->power.subsys_data))
		return -EINVAL;

	pm_runtime_disable(dev);
	device_pm_lock();

	spin_lock_irq(&dev->power.lock);

	if (dev->power.subsys_data->domain_data) {
		gpd_data = to_gpd_data(dev->power.subsys_data->domain_data);
		gpd_data->ops = (struct gpd_dev_ops){ 0 };
		if (clear_td)
			gpd_data->td = (struct gpd_timing_data){ 0 };

		if (--gpd_data->refcount == 0) {
			dev->power.subsys_data->domain_data = NULL;
			remove = true;
		}
	} else {
		ret = -EINVAL;
	}

	spin_unlock_irq(&dev->power.lock);

	device_pm_unlock();
	pm_runtime_enable(dev);

	if (ret)
		return ret;

	dev_pm_put_subsys_data(dev);
	if (remove)
		__pm_genpd_free_dev_data(dev, gpd_data);

	return 0;
}
EXPORT_SYMBOL_GPL(__pm_genpd_remove_callbacks);

int genpd_attach_cpuidle(struct generic_pm_domain *genpd, int state)
{
	struct cpuidle_driver *cpuidle_drv;
	struct gpd_cpu_data *cpu_data;
	struct cpuidle_state *idle_state;
	int ret = 0;

	if (IS_ERR_OR_NULL(genpd) || state < 0)
		return -EINVAL;

	genpd_acquire_lock(genpd);

	if (genpd->cpu_data) {
		ret = -EEXIST;
		goto out;
	}
	cpu_data = kzalloc(sizeof(*cpu_data), GFP_KERNEL);
	if (!cpu_data) {
		ret = -ENOMEM;
		goto out;
	}
	cpuidle_drv = cpuidle_driver_ref();
	if (!cpuidle_drv) {
		ret = -ENODEV;
		goto out;
	}
	if (cpuidle_drv->state_count <= state) {
		ret = -EINVAL;
		goto err;
	}
	idle_state = &cpuidle_drv->states[state];
	if (!idle_state->disabled) {
		ret = -EAGAIN;
		goto err;
	}
	cpu_data->idle_state = idle_state;
	cpu_data->saved_exit_latency = idle_state->exit_latency;
	genpd->cpu_data = cpu_data;
	genpd_recalc_cpu_exit_latency(genpd);

 out:
	genpd_release_lock(genpd);
	return ret;

 err:
	cpuidle_driver_unref();
	goto out;
}

int genpd_detach_cpuidle(struct generic_pm_domain *genpd)
{
	struct gpd_cpu_data *cpu_data;
	struct cpuidle_state *idle_state;
	int ret = 0;

	if (IS_ERR_OR_NULL(genpd))
		return -EINVAL;

	genpd_acquire_lock(genpd);

	cpu_data = genpd->cpu_data;
	if (!cpu_data) {
		ret = -ENODEV;
		goto out;
	}
	idle_state = cpu_data->idle_state;
	if (!idle_state->disabled) {
		ret = -EAGAIN;
		goto out;
	}
	idle_state->exit_latency = cpu_data->saved_exit_latency;
	cpuidle_driver_unref();
	genpd->cpu_data = NULL;
	kfree(cpu_data);

 out:
	genpd_release_lock(genpd);
	return ret;
}

/* Default device callbacks for generic PM domains. */

/**
 * pm_genpd_default_save_state - Default "save device state" for PM domians.
 * @dev: Device to handle.
 */
static int pm_genpd_default_save_state(struct device *dev)
{
	int (*cb)(struct device *__dev);

	cb = dev_gpd_data(dev)->ops.save_state;
	if (cb)
		return cb(dev);

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
 * pm_genpd_default_restore_state - Default PM domians "restore device state".
 * @dev: Device to handle.
 */
static int pm_genpd_default_restore_state(struct device *dev)
{
	int (*cb)(struct device *__dev);

	cb = dev_gpd_data(dev)->ops.restore_state;
	if (cb)
		return cb(dev);

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

#ifdef CONFIG_PM_SLEEP

/**
 * pm_genpd_default_suspend - Default "device suspend" for PM domians.
 * @dev: Device to handle.
 */
static int pm_genpd_default_suspend(struct device *dev)
{
	int (*cb)(struct device *__dev) = dev_gpd_data(dev)->ops.suspend;

	return cb ? cb(dev) : pm_generic_suspend(dev);
}

/**
 * pm_genpd_default_suspend_late - Default "late device suspend" for PM domians.
 * @dev: Device to handle.
 */
static int pm_genpd_default_suspend_late(struct device *dev)
{
	int (*cb)(struct device *__dev) = dev_gpd_data(dev)->ops.suspend_late;

	return cb ? cb(dev) : pm_generic_suspend_late(dev);
}

/**
 * pm_genpd_default_resume_early - Default "early device resume" for PM domians.
 * @dev: Device to handle.
 */
static int pm_genpd_default_resume_early(struct device *dev)
{
	int (*cb)(struct device *__dev) = dev_gpd_data(dev)->ops.resume_early;

	return cb ? cb(dev) : pm_generic_resume_early(dev);
}

/**
 * pm_genpd_default_resume - Default "device resume" for PM domians.
 * @dev: Device to handle.
 */
static int pm_genpd_default_resume(struct device *dev)
{
	int (*cb)(struct device *__dev) = dev_gpd_data(dev)->ops.resume;

	return cb ? cb(dev) : pm_generic_resume(dev);
}

/**
 * pm_genpd_default_freeze - Default "device freeze" for PM domians.
 * @dev: Device to handle.
 */
static int pm_genpd_default_freeze(struct device *dev)
{
	int (*cb)(struct device *__dev) = dev_gpd_data(dev)->ops.freeze;

	return cb ? cb(dev) : pm_generic_freeze(dev);
}

/**
 * pm_genpd_default_freeze_late - Default "late device freeze" for PM domians.
 * @dev: Device to handle.
 */
static int pm_genpd_default_freeze_late(struct device *dev)
{
	int (*cb)(struct device *__dev) = dev_gpd_data(dev)->ops.freeze_late;

	return cb ? cb(dev) : pm_generic_freeze_late(dev);
}

/**
 * pm_genpd_default_thaw_early - Default "early device thaw" for PM domians.
 * @dev: Device to handle.
 */
static int pm_genpd_default_thaw_early(struct device *dev)
{
	int (*cb)(struct device *__dev) = dev_gpd_data(dev)->ops.thaw_early;

	return cb ? cb(dev) : pm_generic_thaw_early(dev);
}

/**
 * pm_genpd_default_thaw - Default "device thaw" for PM domians.
 * @dev: Device to handle.
 */
static int pm_genpd_default_thaw(struct device *dev)
{
	int (*cb)(struct device *__dev) = dev_gpd_data(dev)->ops.thaw;

	return cb ? cb(dev) : pm_generic_thaw(dev);
}

#else /* !CONFIG_PM_SLEEP */

#define pm_genpd_default_suspend	NULL
#define pm_genpd_default_suspend_late	NULL
#define pm_genpd_default_resume_early	NULL
#define pm_genpd_default_resume		NULL
#define pm_genpd_default_freeze		NULL
#define pm_genpd_default_freeze_late	NULL
#define pm_genpd_default_thaw_early	NULL
#define pm_genpd_default_thaw		NULL

#endif /* !CONFIG_PM_SLEEP */

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

	INIT_LIST_HEAD(&genpd->master_links);
	INIT_LIST_HEAD(&genpd->slave_links);
	INIT_LIST_HEAD(&genpd->dev_list);
	mutex_init(&genpd->lock);
	genpd->gov = gov;
	INIT_WORK(&genpd->power_off_work, genpd_power_off_work_fn);
	genpd->in_progress = 0;
	atomic_set(&genpd->sd_count, 0);
	genpd->status = is_off ? GPD_STATE_POWER_OFF : GPD_STATE_ACTIVE;
	init_waitqueue_head(&genpd->status_wait_queue);
	genpd->poweroff_task = NULL;
	genpd->resume_count = 0;
	genpd->device_count = 0;
	genpd->max_off_time_ns = -1;
	genpd->max_off_time_changed = true;
	genpd->domain.ops.runtime_suspend = pm_genpd_runtime_suspend;
	genpd->domain.ops.runtime_resume = pm_genpd_runtime_resume;
	genpd->domain.ops.runtime_idle = pm_generic_runtime_idle;
	genpd->domain.ops.prepare = pm_genpd_prepare;
	genpd->domain.ops.suspend = pm_genpd_suspend;
	genpd->domain.ops.suspend_late = pm_genpd_suspend_late;
	genpd->domain.ops.suspend_noirq = pm_genpd_suspend_noirq;
	genpd->domain.ops.resume_noirq = pm_genpd_resume_noirq;
	genpd->domain.ops.resume_early = pm_genpd_resume_early;
	genpd->domain.ops.resume = pm_genpd_resume;
	genpd->domain.ops.freeze = pm_genpd_freeze;
	genpd->domain.ops.freeze_late = pm_genpd_freeze_late;
	genpd->domain.ops.freeze_noirq = pm_genpd_freeze_noirq;
	genpd->domain.ops.thaw_noirq = pm_genpd_thaw_noirq;
	genpd->domain.ops.thaw_early = pm_genpd_thaw_early;
	genpd->domain.ops.thaw = pm_genpd_thaw;
	genpd->domain.ops.poweroff = pm_genpd_suspend;
	genpd->domain.ops.poweroff_late = pm_genpd_suspend_late;
	genpd->domain.ops.poweroff_noirq = pm_genpd_suspend_noirq;
	genpd->domain.ops.restore_noirq = pm_genpd_restore_noirq;
	genpd->domain.ops.restore_early = pm_genpd_resume_early;
	genpd->domain.ops.restore = pm_genpd_resume;
	genpd->domain.ops.complete = pm_genpd_complete;
	genpd->dev_ops.save_state = pm_genpd_default_save_state;
	genpd->dev_ops.restore_state = pm_genpd_default_restore_state;
	genpd->dev_ops.suspend = pm_genpd_default_suspend;
	genpd->dev_ops.suspend_late = pm_genpd_default_suspend_late;
	genpd->dev_ops.resume_early = pm_genpd_default_resume_early;
	genpd->dev_ops.resume = pm_genpd_default_resume;
	genpd->dev_ops.freeze = pm_genpd_default_freeze;
	genpd->dev_ops.freeze_late = pm_genpd_default_freeze_late;
	genpd->dev_ops.thaw_early = pm_genpd_default_thaw_early;
	genpd->dev_ops.thaw = pm_genpd_default_thaw;
	mutex_lock(&gpd_list_lock);
	list_add(&genpd->gpd_list_node, &gpd_list);
	mutex_unlock(&gpd_list_lock);
}
