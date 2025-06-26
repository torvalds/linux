// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/base/power/wakeup.c - System wakeup events framework
 *
 * Copyright (c) 2010 Rafael J. Wysocki <rjw@sisk.pl>, Novell Inc.
 */
#define pr_fmt(fmt) "PM: " fmt

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <linux/capability.h>
#include <linux/export.h>
#include <linux/suspend.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/pm_wakeirq.h>
#include <trace/events/power.h>

#include "power.h"

#define list_for_each_entry_rcu_locked(pos, head, member) \
	list_for_each_entry_rcu(pos, head, member, \
		srcu_read_lock_held(&wakeup_srcu))
/*
 * If set, the suspend/hibernate code will abort transitions to a sleep state
 * if wakeup events are registered during or immediately before the transition.
 */
bool events_check_enabled __read_mostly;

/* First wakeup IRQ seen by the kernel in the last cycle. */
static unsigned int wakeup_irq[2] __read_mostly;
static DEFINE_RAW_SPINLOCK(wakeup_irq_lock);

/* If greater than 0 and the system is suspending, terminate the suspend. */
static atomic_t pm_abort_suspend __read_mostly;

/*
 * Combined counters of registered wakeup events and wakeup events in progress.
 * They need to be modified together atomically, so it's better to use one
 * atomic variable to hold them both.
 */
static atomic_t combined_event_count = ATOMIC_INIT(0);

#define IN_PROGRESS_BITS	(sizeof(int) * 4)
#define MAX_IN_PROGRESS		((1 << IN_PROGRESS_BITS) - 1)

static void split_counters(unsigned int *cnt, unsigned int *inpr)
{
	unsigned int comb = atomic_read(&combined_event_count);

	*cnt = (comb >> IN_PROGRESS_BITS);
	*inpr = comb & MAX_IN_PROGRESS;
}

/* A preserved old value of the events counter. */
static unsigned int saved_count;

static DEFINE_RAW_SPINLOCK(events_lock);

static void pm_wakeup_timer_fn(struct timer_list *t);

static LIST_HEAD(wakeup_sources);

static DECLARE_WAIT_QUEUE_HEAD(wakeup_count_wait_queue);

DEFINE_STATIC_SRCU(wakeup_srcu);

static struct wakeup_source deleted_ws = {
	.name = "deleted",
	.lock =  __SPIN_LOCK_UNLOCKED(deleted_ws.lock),
};

static DEFINE_IDA(wakeup_ida);

/**
 * wakeup_source_create - Create a struct wakeup_source object.
 * @name: Name of the new wakeup source.
 */
static struct wakeup_source *wakeup_source_create(const char *name)
{
	struct wakeup_source *ws;
	const char *ws_name;
	int id;

	ws = kzalloc(sizeof(*ws), GFP_KERNEL);
	if (!ws)
		goto err_ws;

	ws_name = kstrdup_const(name, GFP_KERNEL);
	if (!ws_name)
		goto err_name;
	ws->name = ws_name;

	id = ida_alloc(&wakeup_ida, GFP_KERNEL);
	if (id < 0)
		goto err_id;
	ws->id = id;

	return ws;

err_id:
	kfree_const(ws->name);
err_name:
	kfree(ws);
err_ws:
	return NULL;
}

/*
 * Record wakeup_source statistics being deleted into a dummy wakeup_source.
 */
static void wakeup_source_record(struct wakeup_source *ws)
{
	unsigned long flags;

	spin_lock_irqsave(&deleted_ws.lock, flags);

	if (ws->event_count) {
		deleted_ws.total_time =
			ktime_add(deleted_ws.total_time, ws->total_time);
		deleted_ws.prevent_sleep_time =
			ktime_add(deleted_ws.prevent_sleep_time,
				  ws->prevent_sleep_time);
		deleted_ws.max_time =
			ktime_compare(deleted_ws.max_time, ws->max_time) > 0 ?
				deleted_ws.max_time : ws->max_time;
		deleted_ws.event_count += ws->event_count;
		deleted_ws.active_count += ws->active_count;
		deleted_ws.relax_count += ws->relax_count;
		deleted_ws.expire_count += ws->expire_count;
		deleted_ws.wakeup_count += ws->wakeup_count;
	}

	spin_unlock_irqrestore(&deleted_ws.lock, flags);
}

static void wakeup_source_free(struct wakeup_source *ws)
{
	ida_free(&wakeup_ida, ws->id);
	kfree_const(ws->name);
	kfree(ws);
}

/**
 * wakeup_source_destroy - Destroy a struct wakeup_source object.
 * @ws: Wakeup source to destroy.
 *
 * Use only for wakeup source objects created with wakeup_source_create().
 */
static void wakeup_source_destroy(struct wakeup_source *ws)
{
	if (!ws)
		return;

	__pm_relax(ws);
	wakeup_source_record(ws);
	wakeup_source_free(ws);
}

/**
 * wakeup_source_add - Add given object to the list of wakeup sources.
 * @ws: Wakeup source object to add to the list.
 */
static void wakeup_source_add(struct wakeup_source *ws)
{
	unsigned long flags;

	if (WARN_ON(!ws))
		return;

	spin_lock_init(&ws->lock);
	timer_setup(&ws->timer, pm_wakeup_timer_fn, 0);
	ws->active = false;

	raw_spin_lock_irqsave(&events_lock, flags);
	list_add_rcu(&ws->entry, &wakeup_sources);
	raw_spin_unlock_irqrestore(&events_lock, flags);
}

/**
 * wakeup_source_remove - Remove given object from the wakeup sources list.
 * @ws: Wakeup source object to remove from the list.
 */
static void wakeup_source_remove(struct wakeup_source *ws)
{
	unsigned long flags;

	if (WARN_ON(!ws))
		return;

	raw_spin_lock_irqsave(&events_lock, flags);
	list_del_rcu(&ws->entry);
	raw_spin_unlock_irqrestore(&events_lock, flags);
	synchronize_srcu(&wakeup_srcu);

	timer_delete_sync(&ws->timer);
	/*
	 * Clear timer.function to make wakeup_source_not_registered() treat
	 * this wakeup source as not registered.
	 */
	ws->timer.function = NULL;
}

/**
 * wakeup_source_register - Create wakeup source and add it to the list.
 * @dev: Device this wakeup source is associated with (or NULL if virtual).
 * @name: Name of the wakeup source to register.
 */
struct wakeup_source *wakeup_source_register(struct device *dev,
					     const char *name)
{
	struct wakeup_source *ws;
	int ret;

	ws = wakeup_source_create(name);
	if (ws) {
		if (!dev || device_is_registered(dev)) {
			ret = wakeup_source_sysfs_add(dev, ws);
			if (ret) {
				wakeup_source_free(ws);
				return NULL;
			}
		}
		wakeup_source_add(ws);
	}
	return ws;
}
EXPORT_SYMBOL_GPL(wakeup_source_register);

/**
 * wakeup_source_unregister - Remove wakeup source from the list and remove it.
 * @ws: Wakeup source object to unregister.
 */
void wakeup_source_unregister(struct wakeup_source *ws)
{
	if (ws) {
		wakeup_source_remove(ws);
		if (ws->dev)
			wakeup_source_sysfs_remove(ws);

		wakeup_source_destroy(ws);
	}
}
EXPORT_SYMBOL_GPL(wakeup_source_unregister);

/**
 * wakeup_sources_read_lock - Lock wakeup source list for read.
 *
 * Returns an index of srcu lock for struct wakeup_srcu.
 * This index must be passed to the matching wakeup_sources_read_unlock().
 */
int wakeup_sources_read_lock(void)
{
	return srcu_read_lock(&wakeup_srcu);
}
EXPORT_SYMBOL_GPL(wakeup_sources_read_lock);

/**
 * wakeup_sources_read_unlock - Unlock wakeup source list.
 * @idx: return value from corresponding wakeup_sources_read_lock()
 */
void wakeup_sources_read_unlock(int idx)
{
	srcu_read_unlock(&wakeup_srcu, idx);
}
EXPORT_SYMBOL_GPL(wakeup_sources_read_unlock);

/**
 * wakeup_sources_walk_start - Begin a walk on wakeup source list
 *
 * Returns first object of the list of wakeup sources.
 *
 * Note that to be safe, wakeup sources list needs to be locked by calling
 * wakeup_source_read_lock() for this.
 */
struct wakeup_source *wakeup_sources_walk_start(void)
{
	struct list_head *ws_head = &wakeup_sources;

	return list_entry_rcu(ws_head->next, struct wakeup_source, entry);
}
EXPORT_SYMBOL_GPL(wakeup_sources_walk_start);

/**
 * wakeup_sources_walk_next - Get next wakeup source from the list
 * @ws: Previous wakeup source object
 *
 * Note that to be safe, wakeup sources list needs to be locked by calling
 * wakeup_source_read_lock() for this.
 */
struct wakeup_source *wakeup_sources_walk_next(struct wakeup_source *ws)
{
	struct list_head *ws_head = &wakeup_sources;

	return list_next_or_null_rcu(ws_head, &ws->entry,
				struct wakeup_source, entry);
}
EXPORT_SYMBOL_GPL(wakeup_sources_walk_next);

/**
 * device_wakeup_attach - Attach a wakeup source object to a device object.
 * @dev: Device to handle.
 * @ws: Wakeup source object to attach to @dev.
 *
 * This causes @dev to be treated as a wakeup device.
 */
static int device_wakeup_attach(struct device *dev, struct wakeup_source *ws)
{
	spin_lock_irq(&dev->power.lock);
	if (dev->power.wakeup) {
		spin_unlock_irq(&dev->power.lock);
		return -EEXIST;
	}
	dev->power.wakeup = ws;
	if (dev->power.wakeirq)
		device_wakeup_attach_irq(dev, dev->power.wakeirq);
	spin_unlock_irq(&dev->power.lock);
	return 0;
}

/**
 * device_wakeup_enable - Enable given device to be a wakeup source.
 * @dev: Device to handle.
 *
 * Create a wakeup source object, register it and attach it to @dev.
 */
int device_wakeup_enable(struct device *dev)
{
	struct wakeup_source *ws;
	int ret;

	if (!dev || !dev->power.can_wakeup)
		return -EINVAL;

	if (pm_sleep_transition_in_progress())
		dev_dbg(dev, "Suspicious %s() during system transition!\n", __func__);

	ws = wakeup_source_register(dev, dev_name(dev));
	if (!ws)
		return -ENOMEM;

	ret = device_wakeup_attach(dev, ws);
	if (ret)
		wakeup_source_unregister(ws);

	return ret;
}
EXPORT_SYMBOL_GPL(device_wakeup_enable);

/**
 * device_wakeup_attach_irq - Attach a wakeirq to a wakeup source
 * @dev: Device to handle
 * @wakeirq: Device specific wakeirq entry
 *
 * Attach a device wakeirq to the wakeup source so the device
 * wake IRQ can be configured automatically for suspend and
 * resume.
 *
 * Call under the device's power.lock lock.
 */
void device_wakeup_attach_irq(struct device *dev,
			     struct wake_irq *wakeirq)
{
	struct wakeup_source *ws;

	ws = dev->power.wakeup;
	if (!ws)
		return;

	if (ws->wakeirq)
		dev_err(dev, "Leftover wakeup IRQ found, overriding\n");

	ws->wakeirq = wakeirq;
}

/**
 * device_wakeup_detach_irq - Detach a wakeirq from a wakeup source
 * @dev: Device to handle
 *
 * Removes a device wakeirq from the wakeup source.
 *
 * Call under the device's power.lock lock.
 */
void device_wakeup_detach_irq(struct device *dev)
{
	struct wakeup_source *ws;

	ws = dev->power.wakeup;
	if (ws)
		ws->wakeirq = NULL;
}

/**
 * device_wakeup_arm_wake_irqs -
 *
 * Iterates over the list of device wakeirqs to arm them.
 */
void device_wakeup_arm_wake_irqs(void)
{
	struct wakeup_source *ws;
	int srcuidx;

	srcuidx = srcu_read_lock(&wakeup_srcu);
	list_for_each_entry_rcu_locked(ws, &wakeup_sources, entry)
		dev_pm_arm_wake_irq(ws->wakeirq);
	srcu_read_unlock(&wakeup_srcu, srcuidx);
}

/**
 * device_wakeup_disarm_wake_irqs -
 *
 * Iterates over the list of device wakeirqs to disarm them.
 */
void device_wakeup_disarm_wake_irqs(void)
{
	struct wakeup_source *ws;
	int srcuidx;

	srcuidx = srcu_read_lock(&wakeup_srcu);
	list_for_each_entry_rcu_locked(ws, &wakeup_sources, entry)
		dev_pm_disarm_wake_irq(ws->wakeirq);
	srcu_read_unlock(&wakeup_srcu, srcuidx);
}

/**
 * device_wakeup_detach - Detach a device's wakeup source object from it.
 * @dev: Device to detach the wakeup source object from.
 *
 * After it returns, @dev will not be treated as a wakeup device any more.
 */
static struct wakeup_source *device_wakeup_detach(struct device *dev)
{
	struct wakeup_source *ws;

	spin_lock_irq(&dev->power.lock);
	ws = dev->power.wakeup;
	dev->power.wakeup = NULL;
	spin_unlock_irq(&dev->power.lock);
	return ws;
}

/**
 * device_wakeup_disable - Do not regard a device as a wakeup source any more.
 * @dev: Device to handle.
 *
 * Detach the @dev's wakeup source object from it, unregister this wakeup source
 * object and destroy it.
 */
void device_wakeup_disable(struct device *dev)
{
	struct wakeup_source *ws;

	if (!dev || !dev->power.can_wakeup)
		return;

	ws = device_wakeup_detach(dev);
	wakeup_source_unregister(ws);
}
EXPORT_SYMBOL_GPL(device_wakeup_disable);

/**
 * device_set_wakeup_capable - Set/reset device wakeup capability flag.
 * @dev: Device to handle.
 * @capable: Whether or not @dev is capable of waking up the system from sleep.
 *
 * If @capable is set, set the @dev's power.can_wakeup flag and add its
 * wakeup-related attributes to sysfs.  Otherwise, unset the @dev's
 * power.can_wakeup flag and remove its wakeup-related attributes from sysfs.
 *
 * This function may sleep and it can't be called from any context where
 * sleeping is not allowed.
 */
void device_set_wakeup_capable(struct device *dev, bool capable)
{
	if (!!dev->power.can_wakeup == !!capable)
		return;

	dev->power.can_wakeup = capable;
	if (device_is_registered(dev) && !list_empty(&dev->power.entry)) {
		if (capable) {
			int ret = wakeup_sysfs_add(dev);

			if (ret)
				dev_info(dev, "Wakeup sysfs attributes not added\n");
		} else {
			wakeup_sysfs_remove(dev);
		}
	}
}
EXPORT_SYMBOL_GPL(device_set_wakeup_capable);

/**
 * device_set_wakeup_enable - Enable or disable a device to wake up the system.
 * @dev: Device to handle.
 * @enable: enable/disable flag
 */
int device_set_wakeup_enable(struct device *dev, bool enable)
{
	if (enable)
		return device_wakeup_enable(dev);

	device_wakeup_disable(dev);
	return 0;
}
EXPORT_SYMBOL_GPL(device_set_wakeup_enable);

/**
 * wakeup_source_not_registered - validate the given wakeup source.
 * @ws: Wakeup source to be validated.
 */
static bool wakeup_source_not_registered(struct wakeup_source *ws)
{
	/*
	 * Use timer struct to check if the given source is initialized
	 * by wakeup_source_add.
	 */
	return ws->timer.function != pm_wakeup_timer_fn;
}

/*
 * The functions below use the observation that each wakeup event starts a
 * period in which the system should not be suspended.  The moment this period
 * will end depends on how the wakeup event is going to be processed after being
 * detected and all of the possible cases can be divided into two distinct
 * groups.
 *
 * First, a wakeup event may be detected by the same functional unit that will
 * carry out the entire processing of it and possibly will pass it to user space
 * for further processing.  In that case the functional unit that has detected
 * the event may later "close" the "no suspend" period associated with it
 * directly as soon as it has been dealt with.  The pair of pm_stay_awake() and
 * pm_relax(), balanced with each other, is supposed to be used in such
 * situations.
 *
 * Second, a wakeup event may be detected by one functional unit and processed
 * by another one.  In that case the unit that has detected it cannot really
 * "close" the "no suspend" period associated with it, unless it knows in
 * advance what's going to happen to the event during processing.  This
 * knowledge, however, may not be available to it, so it can simply specify time
 * to wait before the system can be suspended and pass it as the second
 * argument of pm_wakeup_event().
 *
 * It is valid to call pm_relax() after pm_wakeup_event(), in which case the
 * "no suspend" period will be ended either by the pm_relax(), or by the timer
 * function executed when the timer expires, whichever comes first.
 */

/**
 * wakeup_source_activate - Mark given wakeup source as active.
 * @ws: Wakeup source to handle.
 *
 * Update the @ws' statistics and, if @ws has just been activated, notify the PM
 * core of the event by incrementing the counter of the wakeup events being
 * processed.
 */
static void wakeup_source_activate(struct wakeup_source *ws)
{
	unsigned int cec;

	if (WARN_ONCE(wakeup_source_not_registered(ws),
			"unregistered wakeup source\n"))
		return;

	ws->active = true;
	ws->active_count++;
	ws->last_time = ktime_get();
	if (ws->autosleep_enabled)
		ws->start_prevent_time = ws->last_time;

	/* Increment the counter of events in progress. */
	cec = atomic_inc_return(&combined_event_count);

	trace_wakeup_source_activate(ws->name, cec);
}

/**
 * wakeup_source_report_event - Report wakeup event using the given source.
 * @ws: Wakeup source to report the event for.
 * @hard: If set, abort suspends in progress and wake up from suspend-to-idle.
 */
static void wakeup_source_report_event(struct wakeup_source *ws, bool hard)
{
	ws->event_count++;
	/* This is racy, but the counter is approximate anyway. */
	if (events_check_enabled)
		ws->wakeup_count++;

	if (!ws->active)
		wakeup_source_activate(ws);

	if (hard)
		pm_system_wakeup();
}

/**
 * __pm_stay_awake - Notify the PM core of a wakeup event.
 * @ws: Wakeup source object associated with the source of the event.
 *
 * It is safe to call this function from interrupt context.
 */
void __pm_stay_awake(struct wakeup_source *ws)
{
	unsigned long flags;

	if (!ws)
		return;

	spin_lock_irqsave(&ws->lock, flags);

	wakeup_source_report_event(ws, false);
	timer_delete(&ws->timer);
	ws->timer_expires = 0;

	spin_unlock_irqrestore(&ws->lock, flags);
}
EXPORT_SYMBOL_GPL(__pm_stay_awake);

/**
 * pm_stay_awake - Notify the PM core that a wakeup event is being processed.
 * @dev: Device the wakeup event is related to.
 *
 * Notify the PM core of a wakeup event (signaled by @dev) by calling
 * __pm_stay_awake for the @dev's wakeup source object.
 *
 * Call this function after detecting of a wakeup event if pm_relax() is going
 * to be called directly after processing the event (and possibly passing it to
 * user space for further processing).
 */
void pm_stay_awake(struct device *dev)
{
	unsigned long flags;

	if (!dev)
		return;

	spin_lock_irqsave(&dev->power.lock, flags);
	__pm_stay_awake(dev->power.wakeup);
	spin_unlock_irqrestore(&dev->power.lock, flags);
}
EXPORT_SYMBOL_GPL(pm_stay_awake);

#ifdef CONFIG_PM_AUTOSLEEP
static void update_prevent_sleep_time(struct wakeup_source *ws, ktime_t now)
{
	ktime_t delta = ktime_sub(now, ws->start_prevent_time);
	ws->prevent_sleep_time = ktime_add(ws->prevent_sleep_time, delta);
}
#else
static inline void update_prevent_sleep_time(struct wakeup_source *ws,
					     ktime_t now) {}
#endif

/**
 * wakeup_source_deactivate - Mark given wakeup source as inactive.
 * @ws: Wakeup source to handle.
 *
 * Update the @ws' statistics and notify the PM core that the wakeup source has
 * become inactive by decrementing the counter of wakeup events being processed
 * and incrementing the counter of registered wakeup events.
 */
static void wakeup_source_deactivate(struct wakeup_source *ws)
{
	unsigned int cnt, inpr, cec;
	ktime_t duration;
	ktime_t now;

	ws->relax_count++;
	/*
	 * __pm_relax() may be called directly or from a timer function.
	 * If it is called directly right after the timer function has been
	 * started, but before the timer function calls __pm_relax(), it is
	 * possible that __pm_stay_awake() will be called in the meantime and
	 * will set ws->active.  Then, ws->active may be cleared immediately
	 * by the __pm_relax() called from the timer function, but in such a
	 * case ws->relax_count will be different from ws->active_count.
	 */
	if (ws->relax_count != ws->active_count) {
		ws->relax_count--;
		return;
	}

	ws->active = false;

	now = ktime_get();
	duration = ktime_sub(now, ws->last_time);
	ws->total_time = ktime_add(ws->total_time, duration);
	if (ktime_to_ns(duration) > ktime_to_ns(ws->max_time))
		ws->max_time = duration;

	ws->last_time = now;
	timer_delete(&ws->timer);
	ws->timer_expires = 0;

	if (ws->autosleep_enabled)
		update_prevent_sleep_time(ws, now);

	/*
	 * Increment the counter of registered wakeup events and decrement the
	 * counter of wakeup events in progress simultaneously.
	 */
	cec = atomic_add_return(MAX_IN_PROGRESS, &combined_event_count);
	trace_wakeup_source_deactivate(ws->name, cec);

	split_counters(&cnt, &inpr);
	if (!inpr && waitqueue_active(&wakeup_count_wait_queue))
		wake_up(&wakeup_count_wait_queue);
}

/**
 * __pm_relax - Notify the PM core that processing of a wakeup event has ended.
 * @ws: Wakeup source object associated with the source of the event.
 *
 * Call this function for wakeup events whose processing started with calling
 * __pm_stay_awake().
 *
 * It is safe to call it from interrupt context.
 */
void __pm_relax(struct wakeup_source *ws)
{
	unsigned long flags;

	if (!ws)
		return;

	spin_lock_irqsave(&ws->lock, flags);
	if (ws->active)
		wakeup_source_deactivate(ws);
	spin_unlock_irqrestore(&ws->lock, flags);
}
EXPORT_SYMBOL_GPL(__pm_relax);

/**
 * pm_relax - Notify the PM core that processing of a wakeup event has ended.
 * @dev: Device that signaled the event.
 *
 * Execute __pm_relax() for the @dev's wakeup source object.
 */
void pm_relax(struct device *dev)
{
	unsigned long flags;

	if (!dev)
		return;

	spin_lock_irqsave(&dev->power.lock, flags);
	__pm_relax(dev->power.wakeup);
	spin_unlock_irqrestore(&dev->power.lock, flags);
}
EXPORT_SYMBOL_GPL(pm_relax);

/**
 * pm_wakeup_timer_fn - Delayed finalization of a wakeup event.
 * @t: timer list
 *
 * Call wakeup_source_deactivate() for the wakeup source whose address is stored
 * in @data if it is currently active and its timer has not been canceled and
 * the expiration time of the timer is not in future.
 */
static void pm_wakeup_timer_fn(struct timer_list *t)
{
	struct wakeup_source *ws = timer_container_of(ws, t, timer);
	unsigned long flags;

	spin_lock_irqsave(&ws->lock, flags);

	if (ws->active && ws->timer_expires
	    && time_after_eq(jiffies, ws->timer_expires)) {
		wakeup_source_deactivate(ws);
		ws->expire_count++;
	}

	spin_unlock_irqrestore(&ws->lock, flags);
}

/**
 * pm_wakeup_ws_event - Notify the PM core of a wakeup event.
 * @ws: Wakeup source object associated with the event source.
 * @msec: Anticipated event processing time (in milliseconds).
 * @hard: If set, abort suspends in progress and wake up from suspend-to-idle.
 *
 * Notify the PM core of a wakeup event whose source is @ws that will take
 * approximately @msec milliseconds to be processed by the kernel.  If @ws is
 * not active, activate it.  If @msec is nonzero, set up the @ws' timer to
 * execute pm_wakeup_timer_fn() in future.
 *
 * It is safe to call this function from interrupt context.
 */
void pm_wakeup_ws_event(struct wakeup_source *ws, unsigned int msec, bool hard)
{
	unsigned long flags;
	unsigned long expires;

	if (!ws)
		return;

	spin_lock_irqsave(&ws->lock, flags);

	wakeup_source_report_event(ws, hard);

	if (!msec) {
		wakeup_source_deactivate(ws);
		goto unlock;
	}

	expires = jiffies + msecs_to_jiffies(msec);
	if (!expires)
		expires = 1;

	if (!ws->timer_expires || time_after(expires, ws->timer_expires)) {
		mod_timer(&ws->timer, expires);
		ws->timer_expires = expires;
	}

 unlock:
	spin_unlock_irqrestore(&ws->lock, flags);
}
EXPORT_SYMBOL_GPL(pm_wakeup_ws_event);

/**
 * pm_wakeup_dev_event - Notify the PM core of a wakeup event.
 * @dev: Device the wakeup event is related to.
 * @msec: Anticipated event processing time (in milliseconds).
 * @hard: If set, abort suspends in progress and wake up from suspend-to-idle.
 *
 * Call pm_wakeup_ws_event() for the @dev's wakeup source object.
 */
void pm_wakeup_dev_event(struct device *dev, unsigned int msec, bool hard)
{
	unsigned long flags;

	if (!dev)
		return;

	spin_lock_irqsave(&dev->power.lock, flags);
	pm_wakeup_ws_event(dev->power.wakeup, msec, hard);
	spin_unlock_irqrestore(&dev->power.lock, flags);
}
EXPORT_SYMBOL_GPL(pm_wakeup_dev_event);

void pm_print_active_wakeup_sources(void)
{
	struct wakeup_source *ws;
	int srcuidx, active = 0;
	struct wakeup_source *last_activity_ws = NULL;

	srcuidx = srcu_read_lock(&wakeup_srcu);
	list_for_each_entry_rcu_locked(ws, &wakeup_sources, entry) {
		if (ws->active) {
			pm_pr_dbg("active wakeup source: %s\n", ws->name);
			active = 1;
		} else if (!active &&
			   (!last_activity_ws ||
			    ktime_to_ns(ws->last_time) >
			    ktime_to_ns(last_activity_ws->last_time))) {
			last_activity_ws = ws;
		}
	}

	if (!active && last_activity_ws)
		pm_pr_dbg("last active wakeup source: %s\n",
			last_activity_ws->name);
	srcu_read_unlock(&wakeup_srcu, srcuidx);
}
EXPORT_SYMBOL_GPL(pm_print_active_wakeup_sources);

/**
 * pm_wakeup_pending - Check if power transition in progress should be aborted.
 *
 * Compare the current number of registered wakeup events with its preserved
 * value from the past and return true if new wakeup events have been registered
 * since the old value was stored.  Also return true if the current number of
 * wakeup events being processed is different from zero.
 */
bool pm_wakeup_pending(void)
{
	unsigned long flags;
	bool ret = false;

	raw_spin_lock_irqsave(&events_lock, flags);
	if (events_check_enabled) {
		unsigned int cnt, inpr;

		split_counters(&cnt, &inpr);
		ret = (cnt != saved_count || inpr > 0);
		events_check_enabled = !ret;
	}
	raw_spin_unlock_irqrestore(&events_lock, flags);

	if (ret) {
		pm_pr_dbg("Wakeup pending, aborting suspend\n");
		pm_print_active_wakeup_sources();
	}

	return ret || atomic_read(&pm_abort_suspend) > 0;
}
EXPORT_SYMBOL_GPL(pm_wakeup_pending);

void pm_system_wakeup(void)
{
	atomic_inc(&pm_abort_suspend);
	s2idle_wake();
}
EXPORT_SYMBOL_GPL(pm_system_wakeup);

void pm_system_cancel_wakeup(void)
{
	atomic_dec_if_positive(&pm_abort_suspend);
}

void pm_wakeup_clear(unsigned int irq_number)
{
	raw_spin_lock_irq(&wakeup_irq_lock);

	if (irq_number && wakeup_irq[0] == irq_number)
		wakeup_irq[0] = wakeup_irq[1];
	else
		wakeup_irq[0] = 0;

	wakeup_irq[1] = 0;

	raw_spin_unlock_irq(&wakeup_irq_lock);

	if (!irq_number)
		atomic_set(&pm_abort_suspend, 0);
}

void pm_system_irq_wakeup(unsigned int irq_number)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&wakeup_irq_lock, flags);

	if (wakeup_irq[0] == 0)
		wakeup_irq[0] = irq_number;
	else if (wakeup_irq[1] == 0)
		wakeup_irq[1] = irq_number;
	else
		irq_number = 0;

	pm_pr_dbg("Triggering wakeup from IRQ %d\n", irq_number);

	raw_spin_unlock_irqrestore(&wakeup_irq_lock, flags);

	if (irq_number)
		pm_system_wakeup();
}

unsigned int pm_wakeup_irq(void)
{
	return wakeup_irq[0];
}

/**
 * pm_get_wakeup_count - Read the number of registered wakeup events.
 * @count: Address to store the value at.
 * @block: Whether or not to block.
 *
 * Store the number of registered wakeup events at the address in @count.  If
 * @block is set, block until the current number of wakeup events being
 * processed is zero.
 *
 * Return 'false' if the current number of wakeup events being processed is
 * nonzero.  Otherwise return 'true'.
 */
bool pm_get_wakeup_count(unsigned int *count, bool block)
{
	unsigned int cnt, inpr;

	if (block) {
		DEFINE_WAIT(wait);

		for (;;) {
			prepare_to_wait(&wakeup_count_wait_queue, &wait,
					TASK_INTERRUPTIBLE);
			split_counters(&cnt, &inpr);
			if (inpr == 0 || signal_pending(current))
				break;
			pm_print_active_wakeup_sources();
			schedule();
		}
		finish_wait(&wakeup_count_wait_queue, &wait);
	}

	split_counters(&cnt, &inpr);
	*count = cnt;
	return !inpr;
}

/**
 * pm_save_wakeup_count - Save the current number of registered wakeup events.
 * @count: Value to compare with the current number of registered wakeup events.
 *
 * If @count is equal to the current number of registered wakeup events and the
 * current number of wakeup events being processed is zero, store @count as the
 * old number of registered wakeup events for pm_check_wakeup_events(), enable
 * wakeup events detection and return 'true'.  Otherwise disable wakeup events
 * detection and return 'false'.
 */
bool pm_save_wakeup_count(unsigned int count)
{
	unsigned int cnt, inpr;
	unsigned long flags;

	events_check_enabled = false;
	raw_spin_lock_irqsave(&events_lock, flags);
	split_counters(&cnt, &inpr);
	if (cnt == count && inpr == 0) {
		saved_count = count;
		events_check_enabled = true;
	}
	raw_spin_unlock_irqrestore(&events_lock, flags);
	return events_check_enabled;
}

#ifdef CONFIG_PM_AUTOSLEEP
/**
 * pm_wakep_autosleep_enabled - Modify autosleep_enabled for all wakeup sources.
 * @set: Whether to set or to clear the autosleep_enabled flags.
 */
void pm_wakep_autosleep_enabled(bool set)
{
	struct wakeup_source *ws;
	ktime_t now = ktime_get();
	int srcuidx;

	srcuidx = srcu_read_lock(&wakeup_srcu);
	list_for_each_entry_rcu_locked(ws, &wakeup_sources, entry) {
		spin_lock_irq(&ws->lock);
		if (ws->autosleep_enabled != set) {
			ws->autosleep_enabled = set;
			if (ws->active) {
				if (set)
					ws->start_prevent_time = now;
				else
					update_prevent_sleep_time(ws, now);
			}
		}
		spin_unlock_irq(&ws->lock);
	}
	srcu_read_unlock(&wakeup_srcu, srcuidx);
}
#endif /* CONFIG_PM_AUTOSLEEP */

/**
 * print_wakeup_source_stats - Print wakeup source statistics information.
 * @m: seq_file to print the statistics into.
 * @ws: Wakeup source object to print the statistics for.
 */
static int print_wakeup_source_stats(struct seq_file *m,
				     struct wakeup_source *ws)
{
	unsigned long flags;
	ktime_t total_time;
	ktime_t max_time;
	unsigned long active_count;
	ktime_t active_time;
	ktime_t prevent_sleep_time;

	spin_lock_irqsave(&ws->lock, flags);

	total_time = ws->total_time;
	max_time = ws->max_time;
	prevent_sleep_time = ws->prevent_sleep_time;
	active_count = ws->active_count;
	if (ws->active) {
		ktime_t now = ktime_get();

		active_time = ktime_sub(now, ws->last_time);
		total_time = ktime_add(total_time, active_time);
		if (active_time > max_time)
			max_time = active_time;

		if (ws->autosleep_enabled)
			prevent_sleep_time = ktime_add(prevent_sleep_time,
				ktime_sub(now, ws->start_prevent_time));
	} else {
		active_time = 0;
	}

	seq_printf(m, "%-12s\t%lu\t\t%lu\t\t%lu\t\t%lu\t\t%lld\t\t%lld\t\t%lld\t\t%lld\t\t%lld\n",
		   ws->name, active_count, ws->event_count,
		   ws->wakeup_count, ws->expire_count,
		   ktime_to_ms(active_time), ktime_to_ms(total_time),
		   ktime_to_ms(max_time), ktime_to_ms(ws->last_time),
		   ktime_to_ms(prevent_sleep_time));

	spin_unlock_irqrestore(&ws->lock, flags);

	return 0;
}

static void *wakeup_sources_stats_seq_start(struct seq_file *m,
					loff_t *pos)
{
	struct wakeup_source *ws;
	loff_t n = *pos;
	int *srcuidx = m->private;

	if (n == 0) {
		seq_puts(m, "name\t\tactive_count\tevent_count\twakeup_count\t"
			"expire_count\tactive_since\ttotal_time\tmax_time\t"
			"last_change\tprevent_suspend_time\n");
	}

	*srcuidx = srcu_read_lock(&wakeup_srcu);
	list_for_each_entry_rcu_locked(ws, &wakeup_sources, entry) {
		if (n-- <= 0)
			return ws;
	}

	return NULL;
}

static void *wakeup_sources_stats_seq_next(struct seq_file *m,
					void *v, loff_t *pos)
{
	struct wakeup_source *ws = v;
	struct wakeup_source *next_ws = NULL;

	++(*pos);

	list_for_each_entry_continue_rcu(ws, &wakeup_sources, entry) {
		next_ws = ws;
		break;
	}

	if (!next_ws)
		print_wakeup_source_stats(m, &deleted_ws);

	return next_ws;
}

static void wakeup_sources_stats_seq_stop(struct seq_file *m, void *v)
{
	int *srcuidx = m->private;

	srcu_read_unlock(&wakeup_srcu, *srcuidx);
}

/**
 * wakeup_sources_stats_seq_show - Print wakeup sources statistics information.
 * @m: seq_file to print the statistics into.
 * @v: wakeup_source of each iteration
 */
static int wakeup_sources_stats_seq_show(struct seq_file *m, void *v)
{
	struct wakeup_source *ws = v;

	print_wakeup_source_stats(m, ws);

	return 0;
}

static const struct seq_operations wakeup_sources_stats_seq_ops = {
	.start = wakeup_sources_stats_seq_start,
	.next  = wakeup_sources_stats_seq_next,
	.stop  = wakeup_sources_stats_seq_stop,
	.show  = wakeup_sources_stats_seq_show,
};

static int wakeup_sources_stats_open(struct inode *inode, struct file *file)
{
	return seq_open_private(file, &wakeup_sources_stats_seq_ops, sizeof(int));
}

static const struct file_operations wakeup_sources_stats_fops = {
	.owner = THIS_MODULE,
	.open = wakeup_sources_stats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release_private,
};

static int __init wakeup_sources_debugfs_init(void)
{
	debugfs_create_file("wakeup_sources", 0444, NULL, NULL,
			    &wakeup_sources_stats_fops);
	return 0;
}

postcore_initcall(wakeup_sources_debugfs_init);
