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

struct genpd_lock_ops {
	void (*lock)(struct generic_pm_domain *genpd);
	void (*lock_nested)(struct generic_pm_domain *genpd, int depth);
	int (*lock_interruptible)(struct generic_pm_domain *genpd);
	void (*unlock)(struct generic_pm_domain *genpd);
};

static void genpd_lock_mtx(struct generic_pm_domain *genpd)
{
	mutex_lock(&genpd->mlock);
}

static void genpd_lock_nested_mtx(struct generic_pm_domain *genpd,
					int depth)
{
	mutex_lock_nested(&genpd->mlock, depth);
}

static int genpd_lock_interruptible_mtx(struct generic_pm_domain *genpd)
{
	return mutex_lock_interruptible(&genpd->mlock);
}

static void genpd_unlock_mtx(struct generic_pm_domain *genpd)
{
	return mutex_unlock(&genpd->mlock);
}

static const struct genpd_lock_ops genpd_mtx_ops = {
	.lock = genpd_lock_mtx,
	.lock_nested = genpd_lock_nested_mtx,
	.lock_interruptible = genpd_lock_interruptible_mtx,
	.unlock = genpd_unlock_mtx,
};

static void genpd_lock_spin(struct generic_pm_domain *genpd)
	__acquires(&genpd->slock)
{
	unsigned long flags;

	spin_lock_irqsave(&genpd->slock, flags);
	genpd->lock_flags = flags;
}

static void genpd_lock_nested_spin(struct generic_pm_domain *genpd,
					int depth)
	__acquires(&genpd->slock)
{
	unsigned long flags;

	spin_lock_irqsave_nested(&genpd->slock, flags, depth);
	genpd->lock_flags = flags;
}

static int genpd_lock_interruptible_spin(struct generic_pm_domain *genpd)
	__acquires(&genpd->slock)
{
	unsigned long flags;

	spin_lock_irqsave(&genpd->slock, flags);
	genpd->lock_flags = flags;
	return 0;
}

static void genpd_unlock_spin(struct generic_pm_domain *genpd)
	__releases(&genpd->slock)
{
	spin_unlock_irqrestore(&genpd->slock, genpd->lock_flags);
}

static const struct genpd_lock_ops genpd_spin_ops = {
	.lock = genpd_lock_spin,
	.lock_nested = genpd_lock_nested_spin,
	.lock_interruptible = genpd_lock_interruptible_spin,
	.unlock = genpd_unlock_spin,
};

#define genpd_lock(p)			p->lock_ops->lock(p)
#define genpd_lock_nested(p, d)		p->lock_ops->lock_nested(p, d)
#define genpd_lock_interruptible(p)	p->lock_ops->lock_interruptible(p)
#define genpd_unlock(p)			p->lock_ops->unlock(p)

#define genpd_status_on(genpd)		(genpd->status == GPD_STATE_ACTIVE)
#define genpd_is_irq_safe(genpd)	(genpd->flags & GENPD_FLAG_IRQ_SAFE)
#define genpd_is_always_on(genpd)	(genpd->flags & GENPD_FLAG_ALWAYS_ON)
#define genpd_is_active_wakeup(genpd)	(genpd->flags & GENPD_FLAG_ACTIVE_WAKEUP)

static inline bool irq_safe_dev_in_no_sleep_domain(struct device *dev,
		const struct generic_pm_domain *genpd)
{
	bool ret;

	ret = pm_runtime_is_irq_safe(dev) && !genpd_is_irq_safe(genpd);

	/*
	 * Warn once if an IRQ safe device is attached to a no sleep domain, as
	 * to indicate a suboptimal configuration for PM. For an always on
	 * domain this isn't case, thus don't warn.
	 */
	if (ret && !genpd_is_always_on(genpd))
		dev_warn_once(dev, "PM domain %s will not be powered off\n",
				genpd->name);

	return ret;
}

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

static int genpd_stop_dev(const struct generic_pm_domain *genpd,
			  struct device *dev)
{
	return GENPD_DEV_CALLBACK(genpd, int, stop, dev);
}

static int genpd_start_dev(const struct generic_pm_domain *genpd,
			   struct device *dev)
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

#ifdef CONFIG_DEBUG_FS
static void genpd_update_accounting(struct generic_pm_domain *genpd)
{
	ktime_t delta, now;

	now = ktime_get();
	delta = ktime_sub(now, genpd->accounting_time);

	/*
	 * If genpd->status is active, it means we are just
	 * out of off and so update the idle time and vice
	 * versa.
	 */
	if (genpd->status == GPD_STATE_ACTIVE) {
		int state_idx = genpd->state_idx;

		genpd->states[state_idx].idle_time =
			ktime_add(genpd->states[state_idx].idle_time, delta);
	} else {
		genpd->on_time = ktime_add(genpd->on_time, delta);
	}

	genpd->accounting_time = now;
}
#else
static inline void genpd_update_accounting(struct generic_pm_domain *genpd) {}
#endif

/**
 * dev_pm_genpd_set_performance_state- Set performance state of device's power
 * domain.
 *
 * @dev: Device for which the performance-state needs to be set.
 * @state: Target performance state of the device. This can be set as 0 when the
 *	   device doesn't have any performance state constraints left (And so
 *	   the device wouldn't participate anymore to find the target
 *	   performance state of the genpd).
 *
 * It is assumed that the users guarantee that the genpd wouldn't be detached
 * while this routine is getting called.
 *
 * Returns 0 on success and negative error values on failures.
 */
int dev_pm_genpd_set_performance_state(struct device *dev, unsigned int state)
{
	struct generic_pm_domain *genpd;
	struct generic_pm_domain_data *gpd_data, *pd_data;
	struct pm_domain_data *pdd;
	unsigned int prev;
	int ret = 0;

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -ENODEV;

	if (unlikely(!genpd->set_performance_state))
		return -EINVAL;

	if (unlikely(!dev->power.subsys_data ||
		     !dev->power.subsys_data->domain_data)) {
		WARN_ON(1);
		return -EINVAL;
	}

	genpd_lock(genpd);

	gpd_data = to_gpd_data(dev->power.subsys_data->domain_data);
	prev = gpd_data->performance_state;
	gpd_data->performance_state = state;

	/* New requested state is same as Max requested state */
	if (state == genpd->performance_state)
		goto unlock;

	/* New requested state is higher than Max requested state */
	if (state > genpd->performance_state)
		goto update_state;

	/* Traverse all devices within the domain */
	list_for_each_entry(pdd, &genpd->dev_list, list_node) {
		pd_data = to_gpd_data(pdd);

		if (pd_data->performance_state > state)
			state = pd_data->performance_state;
	}

	if (state == genpd->performance_state)
		goto unlock;

	/*
	 * We aren't propagating performance state changes of a subdomain to its
	 * masters as we don't have hardware that needs it. Over that, the
	 * performance states of subdomain and its masters may not have
	 * one-to-one mapping and would require additional information. We can
	 * get back to this once we have hardware that needs it. For that
	 * reason, we don't have to consider performance state of the subdomains
	 * of genpd here.
	 */

update_state:
	if (genpd_status_on(genpd)) {
		ret = genpd->set_performance_state(genpd, state);
		if (ret) {
			gpd_data->performance_state = prev;
			goto unlock;
		}
	}

	genpd->performance_state = state;

unlock:
	genpd_unlock(genpd);

	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_genpd_set_performance_state);

static int _genpd_power_on(struct generic_pm_domain *genpd, bool timed)
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

	if (unlikely(genpd->set_performance_state)) {
		ret = genpd->set_performance_state(genpd, genpd->performance_state);
		if (ret) {
			pr_warn("%s: Failed to set performance state %d (%d)\n",
				genpd->name, genpd->performance_state, ret);
		}
	}

	if (elapsed_ns <= genpd->states[state_idx].power_on_latency_ns)
		return ret;

	genpd->states[state_idx].power_on_latency_ns = elapsed_ns;
	genpd->max_off_time_changed = true;
	pr_debug("%s: Power-%s latency exceeded, new value %lld ns\n",
		 genpd->name, "on", elapsed_ns);

	return ret;
}

static int _genpd_power_off(struct generic_pm_domain *genpd, bool timed)
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
 * genpd_queue_power_off_work - Queue up the execution of genpd_power_off().
 * @genpd: PM domain to power off.
 *
 * Queue up the execution of genpd_power_off() unless it's already been done
 * before.
 */
static void genpd_queue_power_off_work(struct generic_pm_domain *genpd)
{
	queue_work(pm_wq, &genpd->power_off_work);
}

/**
 * genpd_power_off - Remove power from a given PM domain.
 * @genpd: PM domain to power down.
 * @one_dev_on: If invoked from genpd's ->runtime_suspend|resume() callback, the
 * RPM status of the releated device is in an intermediate state, not yet turned
 * into RPM_SUSPENDED. This means genpd_power_off() must allow one device to not
 * be RPM_SUSPENDED, while it tries to power off the PM domain.
 *
 * If all of the @genpd's devices have been suspended and all of its subdomains
 * have been powered down, remove power from @genpd.
 */
static int genpd_power_off(struct generic_pm_domain *genpd, bool one_dev_on,
			   unsigned int depth)
{
	struct pm_domain_data *pdd;
	struct gpd_link *link;
	unsigned int not_suspended = 0;

	/*
	 * Do not try to power off the domain in the following situations:
	 * (1) The domain is already in the "power off" state.
	 * (2) System suspend is in progress.
	 */
	if (!genpd_status_on(genpd) || genpd->prepared_count > 0)
		return 0;

	/*
	 * Abort power off for the PM domain in the following situations:
	 * (1) The domain is configured as always on.
	 * (2) When the domain has a subdomain being powered on.
	 */
	if (genpd_is_always_on(genpd) || atomic_read(&genpd->sd_count) > 0)
		return -EBUSY;

	list_for_each_entry(pdd, &genpd->dev_list, list_node) {
		enum pm_qos_flags_status stat;

		stat = dev_pm_qos_flags(pdd->dev,
					PM_QOS_FLAG_NO_POWER_OFF
						| PM_QOS_FLAG_REMOTE_WAKEUP);
		if (stat > PM_QOS_FLAGS_NONE)
			return -EBUSY;

		/*
		 * Do not allow PM domain to be powered off, when an IRQ safe
		 * device is part of a non-IRQ safe domain.
		 */
		if (!pm_runtime_suspended(pdd->dev) ||
			irq_safe_dev_in_no_sleep_domain(pdd->dev, genpd))
			not_suspended++;
	}

	if (not_suspended > 1 || (not_suspended == 1 && !one_dev_on))
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
		 * managed to call genpd_power_on() for the master yet after
		 * incrementing it.  In that case genpd_power_on() will wait
		 * for us to drop the lock, so we can call .power_off() and let
		 * the genpd_power_on() restore power for us (this shouldn't
		 * happen very often).
		 */
		ret = _genpd_power_off(genpd, true);
		if (ret)
			return ret;
	}

	genpd->status = GPD_STATE_POWER_OFF;
	genpd_update_accounting(genpd);

	list_for_each_entry(link, &genpd->slave_links, slave_node) {
		genpd_sd_counter_dec(link->master);
		genpd_lock_nested(link->master, depth + 1);
		genpd_power_off(link->master, false, depth + 1);
		genpd_unlock(link->master);
	}

	return 0;
}

/**
 * genpd_power_on - Restore power to a given PM domain and its masters.
 * @genpd: PM domain to power up.
 * @depth: nesting count for lockdep.
 *
 * Restore power to @genpd and all of its masters so that it is possible to
 * resume a device belonging to it.
 */
static int genpd_power_on(struct generic_pm_domain *genpd, unsigned int depth)
{
	struct gpd_link *link;
	int ret = 0;

	if (genpd_status_on(genpd))
		return 0;

	/*
	 * The list is guaranteed not to change while the loop below is being
	 * executed, unless one of the masters' .power_on() callbacks fiddles
	 * with it.
	 */
	list_for_each_entry(link, &genpd->slave_links, slave_node) {
		struct generic_pm_domain *master = link->master;

		genpd_sd_counter_inc(master);

		genpd_lock_nested(master, depth + 1);
		ret = genpd_power_on(master, depth + 1);
		genpd_unlock(master);

		if (ret) {
			genpd_sd_counter_dec(master);
			goto err;
		}
	}

	ret = _genpd_power_on(genpd, true);
	if (ret)
		goto err;

	genpd->status = GPD_STATE_ACTIVE;
	genpd_update_accounting(genpd);

	return 0;

 err:
	list_for_each_entry_continue_reverse(link,
					&genpd->slave_links,
					slave_node) {
		genpd_sd_counter_dec(link->master);
		genpd_lock_nested(link->master, depth + 1);
		genpd_power_off(link->master, false, depth + 1);
		genpd_unlock(link->master);
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
		if (pdd) {
			to_gpd_data(pdd)->td.constraint_changed = true;
			genpd = dev_to_genpd(dev);
		} else {
			genpd = ERR_PTR(-ENODATA);
		}

		spin_unlock_irq(&dev->power.lock);

		if (!IS_ERR(genpd)) {
			genpd_lock(genpd);
			genpd->max_off_time_changed = true;
			genpd_unlock(genpd);
		}

		dev = dev->parent;
		if (!dev || dev->power.ignore_children)
			break;
	}

	return NOTIFY_DONE;
}

/**
 * genpd_power_off_work_fn - Power off PM domain whose subdomain count is 0.
 * @work: Work structure used for scheduling the execution of this function.
 */
static void genpd_power_off_work_fn(struct work_struct *work)
{
	struct generic_pm_domain *genpd;

	genpd = container_of(work, struct generic_pm_domain, power_off_work);

	genpd_lock(genpd);
	genpd_power_off(genpd, false, 0);
	genpd_unlock(genpd);
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
	time_start = 0;
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
	 * If power.irq_safe is set, this routine may be run with
	 * IRQs disabled, so suspend only if the PM domain also is irq_safe.
	 */
	if (irq_safe_dev_in_no_sleep_domain(dev, genpd))
		return 0;

	genpd_lock(genpd);
	genpd_power_off(genpd, true, 0);
	genpd_unlock(genpd);

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

	/*
	 * As we don't power off a non IRQ safe domain, which holds
	 * an IRQ safe device, we don't need to restore power to it.
	 */
	if (irq_safe_dev_in_no_sleep_domain(dev, genpd)) {
		timed = false;
		goto out;
	}

	genpd_lock(genpd);
	ret = genpd_power_on(genpd, 0);
	genpd_unlock(genpd);

	if (ret)
		return ret;

 out:
	/* Measure resume latency. */
	time_start = 0;
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
	if (!pm_runtime_is_irq_safe(dev) ||
		(pm_runtime_is_irq_safe(dev) && genpd_is_irq_safe(genpd))) {
		genpd_lock(genpd);
		genpd_power_off(genpd, true, 0);
		genpd_unlock(genpd);
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
 * genpd_power_off_unused - Power off all PM domains with no devices in use.
 */
static int __init genpd_power_off_unused(void)
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
late_initcall(genpd_power_off_unused);

#if defined(CONFIG_PM_SLEEP) || defined(CONFIG_PM_GENERIC_DOMAINS_OF)

static bool genpd_present(const struct generic_pm_domain *genpd)
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

/**
 * genpd_sync_power_off - Synchronously power off a PM domain and its masters.
 * @genpd: PM domain to power off, if possible.
 * @use_lock: use the lock.
 * @depth: nesting count for lockdep.
 *
 * Check if the given PM domain can be powered off (during system suspend or
 * hibernation) and do that if so.  Also, in that case propagate to its masters.
 *
 * This function is only called in "noirq" and "syscore" stages of system power
 * transitions. The "noirq" callbacks may be executed asynchronously, thus in
 * these cases the lock must be held.
 */
static void genpd_sync_power_off(struct generic_pm_domain *genpd, bool use_lock,
				 unsigned int depth)
{
	struct gpd_link *link;

	if (!genpd_status_on(genpd) || genpd_is_always_on(genpd))
		return;

	if (genpd->suspended_count != genpd->device_count
	    || atomic_read(&genpd->sd_count) > 0)
		return;

	/* Choose the deepest state when suspending */
	genpd->state_idx = genpd->state_count - 1;
	if (_genpd_power_off(genpd, false))
		return;

	genpd->status = GPD_STATE_POWER_OFF;

	list_for_each_entry(link, &genpd->slave_links, slave_node) {
		genpd_sd_counter_dec(link->master);

		if (use_lock)
			genpd_lock_nested(link->master, depth + 1);

		genpd_sync_power_off(link->master, use_lock, depth + 1);

		if (use_lock)
			genpd_unlock(link->master);
	}
}

/**
 * genpd_sync_power_on - Synchronously power on a PM domain and its masters.
 * @genpd: PM domain to power on.
 * @use_lock: use the lock.
 * @depth: nesting count for lockdep.
 *
 * This function is only called in "noirq" and "syscore" stages of system power
 * transitions. The "noirq" callbacks may be executed asynchronously, thus in
 * these cases the lock must be held.
 */
static void genpd_sync_power_on(struct generic_pm_domain *genpd, bool use_lock,
				unsigned int depth)
{
	struct gpd_link *link;

	if (genpd_status_on(genpd))
		return;

	list_for_each_entry(link, &genpd->slave_links, slave_node) {
		genpd_sd_counter_inc(link->master);

		if (use_lock)
			genpd_lock_nested(link->master, depth + 1);

		genpd_sync_power_on(link->master, use_lock, depth + 1);

		if (use_lock)
			genpd_unlock(link->master);
	}

	_genpd_power_on(genpd, false);

	genpd->status = GPD_STATE_ACTIVE;
}

/**
 * resume_needed - Check whether to resume a device before system suspend.
 * @dev: Device to check.
 * @genpd: PM domain the device belongs to.
 *
 * There are two cases in which a device that can wake up the system from sleep
 * states should be resumed by genpd_prepare(): (1) if the device is enabled
 * to wake up the system and it has to remain active for this purpose while the
 * system is in the sleep state and (2) if the device is not enabled to wake up
 * the system from sleep states and it generally doesn't generate wakeup signals
 * by itself (those signals are generated on its behalf by other parts of the
 * system).  In the latter case it may be necessary to reconfigure the device's
 * wakeup settings during system suspend, because it may have been set up to
 * signal remote wakeup from the system's working state as needed by runtime PM.
 * Return 'true' in either of the above cases.
 */
static bool resume_needed(struct device *dev,
			  const struct generic_pm_domain *genpd)
{
	bool active_wakeup;

	if (!device_can_wakeup(dev))
		return false;

	active_wakeup = genpd_is_active_wakeup(genpd);
	return device_may_wakeup(dev) ? active_wakeup : !active_wakeup;
}

/**
 * genpd_prepare - Start power transition of a device in a PM domain.
 * @dev: Device to start the transition of.
 *
 * Start a power transition of a device (during a system-wide power transition)
 * under the assumption that its pm_domain field points to the domain member of
 * an object of type struct generic_pm_domain representing a PM domain
 * consisting of I/O devices.
 */
static int genpd_prepare(struct device *dev)
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

	genpd_lock(genpd);

	if (genpd->prepared_count++ == 0)
		genpd->suspended_count = 0;

	genpd_unlock(genpd);

	ret = pm_generic_prepare(dev);
	if (ret < 0) {
		genpd_lock(genpd);

		genpd->prepared_count--;

		genpd_unlock(genpd);
	}

	/* Never return 1, as genpd don't cope with the direct_complete path. */
	return ret >= 0 ? 0 : ret;
}

/**
 * genpd_finish_suspend - Completion of suspend or hibernation of device in an
 *   I/O pm domain.
 * @dev: Device to suspend.
 * @poweroff: Specifies if this is a poweroff_noirq or suspend_noirq callback.
 *
 * Stop the device and remove power from the domain if all devices in it have
 * been stopped.
 */
static int genpd_finish_suspend(struct device *dev, bool poweroff)
{
	struct generic_pm_domain *genpd;
	int ret;

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	if (dev->power.wakeup_path && genpd_is_active_wakeup(genpd))
		return 0;

	if (poweroff)
		ret = pm_generic_poweroff_noirq(dev);
	else
		ret = pm_generic_suspend_noirq(dev);
	if (ret)
		return ret;

	if (genpd->dev_ops.stop && genpd->dev_ops.start) {
		ret = pm_runtime_force_suspend(dev);
		if (ret)
			return ret;
	}

	genpd_lock(genpd);
	genpd->suspended_count++;
	genpd_sync_power_off(genpd, true, 0);
	genpd_unlock(genpd);

	return 0;
}

/**
 * genpd_suspend_noirq - Completion of suspend of device in an I/O PM domain.
 * @dev: Device to suspend.
 *
 * Stop the device and remove power from the domain if all devices in it have
 * been stopped.
 */
static int genpd_suspend_noirq(struct device *dev)
{
	dev_dbg(dev, "%s()\n", __func__);

	return genpd_finish_suspend(dev, false);
}

/**
 * genpd_resume_noirq - Start of resume of device in an I/O PM domain.
 * @dev: Device to resume.
 *
 * Restore power to the device's PM domain, if necessary, and start the device.
 */
static int genpd_resume_noirq(struct device *dev)
{
	struct generic_pm_domain *genpd;
	int ret = 0;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	if (dev->power.wakeup_path && genpd_is_active_wakeup(genpd))
		return 0;

	genpd_lock(genpd);
	genpd_sync_power_on(genpd, true, 0);
	genpd->suspended_count--;
	genpd_unlock(genpd);

	if (genpd->dev_ops.stop && genpd->dev_ops.start)
		ret = pm_runtime_force_resume(dev);

	ret = pm_generic_resume_noirq(dev);
	if (ret)
		return ret;

	return ret;
}

/**
 * genpd_freeze_noirq - Completion of freezing a device in an I/O PM domain.
 * @dev: Device to freeze.
 *
 * Carry out a late freeze of a device under the assumption that its
 * pm_domain field points to the domain member of an object of type
 * struct generic_pm_domain representing a power domain consisting of I/O
 * devices.
 */
static int genpd_freeze_noirq(struct device *dev)
{
	const struct generic_pm_domain *genpd;
	int ret = 0;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	ret = pm_generic_freeze_noirq(dev);
	if (ret)
		return ret;

	if (genpd->dev_ops.stop && genpd->dev_ops.start)
		ret = pm_runtime_force_suspend(dev);

	return ret;
}

/**
 * genpd_thaw_noirq - Early thaw of device in an I/O PM domain.
 * @dev: Device to thaw.
 *
 * Start the device, unless power has been removed from the domain already
 * before the system transition.
 */
static int genpd_thaw_noirq(struct device *dev)
{
	const struct generic_pm_domain *genpd;
	int ret = 0;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	if (genpd->dev_ops.stop && genpd->dev_ops.start) {
		ret = pm_runtime_force_resume(dev);
		if (ret)
			return ret;
	}

	return pm_generic_thaw_noirq(dev);
}

/**
 * genpd_poweroff_noirq - Completion of hibernation of device in an
 *   I/O PM domain.
 * @dev: Device to poweroff.
 *
 * Stop the device and remove power from the domain if all devices in it have
 * been stopped.
 */
static int genpd_poweroff_noirq(struct device *dev)
{
	dev_dbg(dev, "%s()\n", __func__);

	return genpd_finish_suspend(dev, true);
}

/**
 * genpd_restore_noirq - Start of restore of device in an I/O PM domain.
 * @dev: Device to resume.
 *
 * Make sure the domain will be in the same power state as before the
 * hibernation the system is resuming from and start the device if necessary.
 */
static int genpd_restore_noirq(struct device *dev)
{
	struct generic_pm_domain *genpd;
	int ret = 0;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return -EINVAL;

	/*
	 * At this point suspended_count == 0 means we are being run for the
	 * first time for the given domain in the present cycle.
	 */
	genpd_lock(genpd);
	if (genpd->suspended_count++ == 0)
		/*
		 * The boot kernel might put the domain into arbitrary state,
		 * so make it appear as powered off to genpd_sync_power_on(),
		 * so that it tries to power it on in case it was really off.
		 */
		genpd->status = GPD_STATE_POWER_OFF;

	genpd_sync_power_on(genpd, true, 0);
	genpd_unlock(genpd);

	if (genpd->dev_ops.stop && genpd->dev_ops.start) {
		ret = pm_runtime_force_resume(dev);
		if (ret)
			return ret;
	}

	return pm_generic_restore_noirq(dev);
}

/**
 * genpd_complete - Complete power transition of a device in a power domain.
 * @dev: Device to complete the transition of.
 *
 * Complete a power transition of a device (during a system-wide power
 * transition) under the assumption that its pm_domain field points to the
 * domain member of an object of type struct generic_pm_domain representing
 * a power domain consisting of I/O devices.
 */
static void genpd_complete(struct device *dev)
{
	struct generic_pm_domain *genpd;

	dev_dbg(dev, "%s()\n", __func__);

	genpd = dev_to_genpd(dev);
	if (IS_ERR(genpd))
		return;

	pm_generic_complete(dev);

	genpd_lock(genpd);

	genpd->prepared_count--;
	if (!genpd->prepared_count)
		genpd_queue_power_off_work(genpd);

	genpd_unlock(genpd);
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
	if (!genpd_present(genpd))
		return;

	if (suspend) {
		genpd->suspended_count++;
		genpd_sync_power_off(genpd, false, 0);
	} else {
		genpd_sync_power_on(genpd, false, 0);
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

#define genpd_prepare		NULL
#define genpd_suspend_noirq	NULL
#define genpd_resume_noirq	NULL
#define genpd_freeze_noirq	NULL
#define genpd_thaw_noirq	NULL
#define genpd_poweroff_noirq	NULL
#define genpd_restore_noirq	NULL
#define genpd_complete		NULL

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
	gpd_data->td.effective_constraint_ns = 0;
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

	genpd_lock(genpd);

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
	genpd_unlock(genpd);

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

	genpd_lock(genpd);

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

	genpd_unlock(genpd);

	genpd_free_dev_data(dev, gpd_data);

	return 0;

 out:
	genpd_unlock(genpd);
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

	/*
	 * If the domain can be powered on/off in an IRQ safe
	 * context, ensure that the subdomain can also be
	 * powered on/off in that context.
	 */
	if (!genpd_is_irq_safe(genpd) && genpd_is_irq_safe(subdomain)) {
		WARN(1, "Parent %s of subdomain %s must be IRQ safe\n",
				genpd->name, subdomain->name);
		return -EINVAL;
	}

	link = kzalloc(sizeof(*link), GFP_KERNEL);
	if (!link)
		return -ENOMEM;

	genpd_lock(subdomain);
	genpd_lock_nested(genpd, SINGLE_DEPTH_NESTING);

	if (!genpd_status_on(genpd) && genpd_status_on(subdomain)) {
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
	if (genpd_status_on(subdomain))
		genpd_sd_counter_inc(genpd);

 out:
	genpd_unlock(genpd);
	genpd_unlock(subdomain);
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

	genpd_lock(subdomain);
	genpd_lock_nested(genpd, SINGLE_DEPTH_NESTING);

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
		if (genpd_status_on(subdomain))
			genpd_sd_counter_dec(genpd);

		ret = 0;
		break;
	}

out:
	genpd_unlock(genpd);
	genpd_unlock(subdomain);

	return ret;
}
EXPORT_SYMBOL_GPL(pm_genpd_remove_subdomain);

static int genpd_set_default_power_state(struct generic_pm_domain *genpd)
{
	struct genpd_power_state *state;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	genpd->states = state;
	genpd->state_count = 1;
	genpd->free = state;

	return 0;
}

static void genpd_lock_init(struct generic_pm_domain *genpd)
{
	if (genpd->flags & GENPD_FLAG_IRQ_SAFE) {
		spin_lock_init(&genpd->slock);
		genpd->lock_ops = &genpd_spin_ops;
	} else {
		mutex_init(&genpd->mlock);
		genpd->lock_ops = &genpd_mtx_ops;
	}
}

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
	int ret;

	if (IS_ERR_OR_NULL(genpd))
		return -EINVAL;

	INIT_LIST_HEAD(&genpd->master_links);
	INIT_LIST_HEAD(&genpd->slave_links);
	INIT_LIST_HEAD(&genpd->dev_list);
	genpd_lock_init(genpd);
	genpd->gov = gov;
	INIT_WORK(&genpd->power_off_work, genpd_power_off_work_fn);
	atomic_set(&genpd->sd_count, 0);
	genpd->status = is_off ? GPD_STATE_POWER_OFF : GPD_STATE_ACTIVE;
	genpd->device_count = 0;
	genpd->max_off_time_ns = -1;
	genpd->max_off_time_changed = true;
	genpd->provider = NULL;
	genpd->has_provider = false;
	genpd->accounting_time = ktime_get();
	genpd->domain.ops.runtime_suspend = genpd_runtime_suspend;
	genpd->domain.ops.runtime_resume = genpd_runtime_resume;
	genpd->domain.ops.prepare = genpd_prepare;
	genpd->domain.ops.suspend_noirq = genpd_suspend_noirq;
	genpd->domain.ops.resume_noirq = genpd_resume_noirq;
	genpd->domain.ops.freeze_noirq = genpd_freeze_noirq;
	genpd->domain.ops.thaw_noirq = genpd_thaw_noirq;
	genpd->domain.ops.poweroff_noirq = genpd_poweroff_noirq;
	genpd->domain.ops.restore_noirq = genpd_restore_noirq;
	genpd->domain.ops.complete = genpd_complete;

	if (genpd->flags & GENPD_FLAG_PM_CLK) {
		genpd->dev_ops.stop = pm_clk_suspend;
		genpd->dev_ops.start = pm_clk_resume;
	}

	/* Always-on domains must be powered on at initialization. */
	if (genpd_is_always_on(genpd) && !genpd_status_on(genpd))
		return -EINVAL;

	/* Use only one "off" state if there were no states declared */
	if (genpd->state_count == 0) {
		ret = genpd_set_default_power_state(genpd);
		if (ret)
			return ret;
	}

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

	genpd_lock(genpd);

	if (genpd->has_provider) {
		genpd_unlock(genpd);
		pr_err("Provider present, unable to remove %s\n", genpd->name);
		return -EBUSY;
	}

	if (!list_empty(&genpd->master_links) || genpd->device_count) {
		genpd_unlock(genpd);
		pr_err("%s: unable to remove %s\n", __func__, genpd->name);
		return -EBUSY;
	}

	list_for_each_entry_safe(link, l, &genpd->slave_links, slave_node) {
		list_del(&link->master_node);
		list_del(&link->slave_node);
		kfree(link);
	}

	list_del(&genpd->gpd_list_node);
	genpd_unlock(genpd);
	cancel_work_sync(&genpd->power_off_work);
	kfree(genpd->free);
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
	pr_debug("Added domain provider from %pOF\n", np);

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

	if (genpd_present(genpd)) {
		ret = genpd_add_provider(np, genpd_xlate_simple, genpd);
		if (!ret) {
			genpd->provider = &np->fwnode;
			genpd->has_provider = true;
		}
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

	if (!data->xlate)
		data->xlate = genpd_xlate_onecell;

	for (i = 0; i < data->num_domains; i++) {
		if (!data->domains[i])
			continue;
		if (!genpd_present(data->domains[i]))
			goto error;

		data->domains[i]->provider = &np->fwnode;
		data->domains[i]->has_provider = true;
	}

	ret = genpd_add_provider(np, data->xlate, data);
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
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to add to PM domain %s: %d",
				pd->name, ret);
		goto out;
	}

	dev->pm_domain->detach = genpd_dev_pm_detach;
	dev->pm_domain->sync = genpd_dev_pm_sync;

	genpd_lock(pd);
	ret = genpd_power_on(pd, 0);
	genpd_unlock(pd);
out:
	return ret ? -EPROBE_DEFER : 0;
}
EXPORT_SYMBOL_GPL(genpd_dev_pm_attach);

static const struct of_device_id idle_state_match[] = {
	{ .compatible = "domain-idle-state", },
	{ }
};

static int genpd_parse_state(struct genpd_power_state *genpd_state,
				    struct device_node *state_node)
{
	int err;
	u32 residency;
	u32 entry_latency, exit_latency;

	err = of_property_read_u32(state_node, "entry-latency-us",
						&entry_latency);
	if (err) {
		pr_debug(" * %pOF missing entry-latency-us property\n",
						state_node);
		return -EINVAL;
	}

	err = of_property_read_u32(state_node, "exit-latency-us",
						&exit_latency);
	if (err) {
		pr_debug(" * %pOF missing exit-latency-us property\n",
						state_node);
		return -EINVAL;
	}

	err = of_property_read_u32(state_node, "min-residency-us", &residency);
	if (!err)
		genpd_state->residency_ns = 1000 * residency;

	genpd_state->power_on_latency_ns = 1000 * exit_latency;
	genpd_state->power_off_latency_ns = 1000 * entry_latency;
	genpd_state->fwnode = &state_node->fwnode;

	return 0;
}

/**
 * of_genpd_parse_idle_states: Return array of idle states for the genpd.
 *
 * @dn: The genpd device node
 * @states: The pointer to which the state array will be saved.
 * @n: The count of elements in the array returned from this function.
 *
 * Returns the device states parsed from the OF node. The memory for the states
 * is allocated by this function and is the responsibility of the caller to
 * free the memory after use.
 */
int of_genpd_parse_idle_states(struct device_node *dn,
			struct genpd_power_state **states, int *n)
{
	struct genpd_power_state *st;
	struct device_node *np;
	int i = 0;
	int err, ret;
	int count;
	struct of_phandle_iterator it;
	const struct of_device_id *match_id;

	count = of_count_phandle_with_args(dn, "domain-idle-states", NULL);
	if (count <= 0)
		return -EINVAL;

	st = kcalloc(count, sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	/* Loop over the phandles until all the requested entry is found */
	of_for_each_phandle(&it, err, dn, "domain-idle-states", NULL, 0) {
		np = it.node;
		match_id = of_match_node(idle_state_match, np);
		if (!match_id)
			continue;
		ret = genpd_parse_state(&st[i++], np);
		if (ret) {
			pr_err
			("Parsing idle state node %pOF failed with err %d\n",
							np, ret);
			of_node_put(np);
			kfree(st);
			return ret;
		}
	}

	*n = i;
	if (!i)
		kfree(st);
	else
		*states = st;

	return 0;
}
EXPORT_SYMBOL_GPL(of_genpd_parse_idle_states);

#endif /* CONFIG_PM_GENERIC_DOMAINS_OF */


/***        debugfs support        ***/

#ifdef CONFIG_DEBUG_FS
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/kobject.h>
static struct dentry *genpd_debugfs_dir;

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

static int genpd_summary_one(struct seq_file *s,
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

	ret = genpd_lock_interruptible(genpd);
	if (ret)
		return -ERESTARTSYS;

	if (WARN_ON(genpd->status >= ARRAY_SIZE(status_lookup)))
		goto exit;
	if (!genpd_status_on(genpd))
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
		kobj_path = kobject_get_path(&pm_data->dev->kobj,
				genpd_is_irq_safe(genpd) ?
				GFP_ATOMIC : GFP_KERNEL);
		if (kobj_path == NULL)
			continue;

		seq_printf(s, "\n    %-50s  ", kobj_path);
		rtpm_status_str(s, pm_data->dev);
		kfree(kobj_path);
	}

	seq_puts(s, "\n");
exit:
	genpd_unlock(genpd);

	return 0;
}

static int genpd_summary_show(struct seq_file *s, void *data)
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
		ret = genpd_summary_one(s, genpd);
		if (ret)
			break;
	}
	mutex_unlock(&gpd_list_lock);

	return ret;
}

static int genpd_status_show(struct seq_file *s, void *data)
{
	static const char * const status_lookup[] = {
		[GPD_STATE_ACTIVE] = "on",
		[GPD_STATE_POWER_OFF] = "off"
	};

	struct generic_pm_domain *genpd = s->private;
	int ret = 0;

	ret = genpd_lock_interruptible(genpd);
	if (ret)
		return -ERESTARTSYS;

	if (WARN_ON_ONCE(genpd->status >= ARRAY_SIZE(status_lookup)))
		goto exit;

	if (genpd->status == GPD_STATE_POWER_OFF)
		seq_printf(s, "%s-%u\n", status_lookup[genpd->status],
			genpd->state_idx);
	else
		seq_printf(s, "%s\n", status_lookup[genpd->status]);
exit:
	genpd_unlock(genpd);
	return ret;
}

static int genpd_sub_domains_show(struct seq_file *s, void *data)
{
	struct generic_pm_domain *genpd = s->private;
	struct gpd_link *link;
	int ret = 0;

	ret = genpd_lock_interruptible(genpd);
	if (ret)
		return -ERESTARTSYS;

	list_for_each_entry(link, &genpd->master_links, master_node)
		seq_printf(s, "%s\n", link->slave->name);

	genpd_unlock(genpd);
	return ret;
}

static int genpd_idle_states_show(struct seq_file *s, void *data)
{
	struct generic_pm_domain *genpd = s->private;
	unsigned int i;
	int ret = 0;

	ret = genpd_lock_interruptible(genpd);
	if (ret)
		return -ERESTARTSYS;

	seq_puts(s, "State          Time Spent(ms)\n");

	for (i = 0; i < genpd->state_count; i++) {
		ktime_t delta = 0;
		s64 msecs;

		if ((genpd->status == GPD_STATE_POWER_OFF) &&
				(genpd->state_idx == i))
			delta = ktime_sub(ktime_get(), genpd->accounting_time);

		msecs = ktime_to_ms(
			ktime_add(genpd->states[i].idle_time, delta));
		seq_printf(s, "S%-13i %lld\n", i, msecs);
	}

	genpd_unlock(genpd);
	return ret;
}

static int genpd_active_time_show(struct seq_file *s, void *data)
{
	struct generic_pm_domain *genpd = s->private;
	ktime_t delta = 0;
	int ret = 0;

	ret = genpd_lock_interruptible(genpd);
	if (ret)
		return -ERESTARTSYS;

	if (genpd->status == GPD_STATE_ACTIVE)
		delta = ktime_sub(ktime_get(), genpd->accounting_time);

	seq_printf(s, "%lld ms\n", ktime_to_ms(
				ktime_add(genpd->on_time, delta)));

	genpd_unlock(genpd);
	return ret;
}

static int genpd_total_idle_time_show(struct seq_file *s, void *data)
{
	struct generic_pm_domain *genpd = s->private;
	ktime_t delta = 0, total = 0;
	unsigned int i;
	int ret = 0;

	ret = genpd_lock_interruptible(genpd);
	if (ret)
		return -ERESTARTSYS;

	for (i = 0; i < genpd->state_count; i++) {

		if ((genpd->status == GPD_STATE_POWER_OFF) &&
				(genpd->state_idx == i))
			delta = ktime_sub(ktime_get(), genpd->accounting_time);

		total = ktime_add(total, genpd->states[i].idle_time);
	}
	total = ktime_add(total, delta);

	seq_printf(s, "%lld ms\n", ktime_to_ms(total));

	genpd_unlock(genpd);
	return ret;
}


static int genpd_devices_show(struct seq_file *s, void *data)
{
	struct generic_pm_domain *genpd = s->private;
	struct pm_domain_data *pm_data;
	const char *kobj_path;
	int ret = 0;

	ret = genpd_lock_interruptible(genpd);
	if (ret)
		return -ERESTARTSYS;

	list_for_each_entry(pm_data, &genpd->dev_list, list_node) {
		kobj_path = kobject_get_path(&pm_data->dev->kobj,
				genpd_is_irq_safe(genpd) ?
				GFP_ATOMIC : GFP_KERNEL);
		if (kobj_path == NULL)
			continue;

		seq_printf(s, "%s\n", kobj_path);
		kfree(kobj_path);
	}

	genpd_unlock(genpd);
	return ret;
}

#define define_genpd_open_function(name) \
static int genpd_##name##_open(struct inode *inode, struct file *file) \
{ \
	return single_open(file, genpd_##name##_show, inode->i_private); \
}

define_genpd_open_function(summary);
define_genpd_open_function(status);
define_genpd_open_function(sub_domains);
define_genpd_open_function(idle_states);
define_genpd_open_function(active_time);
define_genpd_open_function(total_idle_time);
define_genpd_open_function(devices);

#define define_genpd_debugfs_fops(name) \
static const struct file_operations genpd_##name##_fops = { \
	.open = genpd_##name##_open, \
	.read = seq_read, \
	.llseek = seq_lseek, \
	.release = single_release, \
}

define_genpd_debugfs_fops(summary);
define_genpd_debugfs_fops(status);
define_genpd_debugfs_fops(sub_domains);
define_genpd_debugfs_fops(idle_states);
define_genpd_debugfs_fops(active_time);
define_genpd_debugfs_fops(total_idle_time);
define_genpd_debugfs_fops(devices);

static int __init genpd_debug_init(void)
{
	struct dentry *d;
	struct generic_pm_domain *genpd;

	genpd_debugfs_dir = debugfs_create_dir("pm_genpd", NULL);

	if (!genpd_debugfs_dir)
		return -ENOMEM;

	d = debugfs_create_file("pm_genpd_summary", S_IRUGO,
			genpd_debugfs_dir, NULL, &genpd_summary_fops);
	if (!d)
		return -ENOMEM;

	list_for_each_entry(genpd, &gpd_list, gpd_list_node) {
		d = debugfs_create_dir(genpd->name, genpd_debugfs_dir);
		if (!d)
			return -ENOMEM;

		debugfs_create_file("current_state", 0444,
				d, genpd, &genpd_status_fops);
		debugfs_create_file("sub_domains", 0444,
				d, genpd, &genpd_sub_domains_fops);
		debugfs_create_file("idle_states", 0444,
				d, genpd, &genpd_idle_states_fops);
		debugfs_create_file("active_time", 0444,
				d, genpd, &genpd_active_time_fops);
		debugfs_create_file("total_idle_time", 0444,
				d, genpd, &genpd_total_idle_time_fops);
		debugfs_create_file("devices", 0444,
				d, genpd, &genpd_devices_fops);
	}

	return 0;
}
late_initcall(genpd_debug_init);

static void __exit genpd_debug_exit(void)
{
	debugfs_remove_recursive(genpd_debugfs_dir);
}
__exitcall(genpd_debug_exit);
#endif /* CONFIG_DEBUG_FS */
