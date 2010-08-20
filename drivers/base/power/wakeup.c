/*
 * drivers/base/power/wakeup.c - System wakeup events framework
 *
 * Copyright (c) 2010 Rafael J. Wysocki <rjw@sisk.pl>, Novell Inc.
 *
 * This file is released under the GPLv2.
 */

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/capability.h>
#include <linux/suspend.h>
#include <linux/pm.h>

/*
 * If set, the suspend/hibernate code will abort transitions to a sleep state
 * if wakeup events are registered during or immediately before the transition.
 */
bool events_check_enabled;

/* The counter of registered wakeup events. */
static unsigned long event_count;
/* A preserved old value of event_count. */
static unsigned long saved_event_count;
/* The counter of wakeup events being processed. */
static unsigned long events_in_progress;

static DEFINE_SPINLOCK(events_lock);

static void pm_wakeup_timer_fn(unsigned long data);

static DEFINE_TIMER(events_timer, pm_wakeup_timer_fn, 0, 0);
static unsigned long events_timer_expires;

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
 */

/**
 * pm_stay_awake - Notify the PM core that a wakeup event is being processed.
 * @dev: Device the wakeup event is related to.
 *
 * Notify the PM core of a wakeup event (signaled by @dev) by incrementing the
 * counter of wakeup events being processed.  If @dev is not NULL, the counter
 * of wakeup events related to @dev is incremented too.
 *
 * Call this function after detecting of a wakeup event if pm_relax() is going
 * to be called directly after processing the event (and possibly passing it to
 * user space for further processing).
 *
 * It is safe to call this function from interrupt context.
 */
void pm_stay_awake(struct device *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&events_lock, flags);
	if (dev)
		dev->power.wakeup_count++;

	events_in_progress++;
	spin_unlock_irqrestore(&events_lock, flags);
}

/**
 * pm_relax - Notify the PM core that processing of a wakeup event has ended.
 *
 * Notify the PM core that a wakeup event has been processed by decrementing
 * the counter of wakeup events being processed and incrementing the counter
 * of registered wakeup events.
 *
 * Call this function for wakeup events whose processing started with calling
 * pm_stay_awake().
 *
 * It is safe to call it from interrupt context.
 */
void pm_relax(void)
{
	unsigned long flags;

	spin_lock_irqsave(&events_lock, flags);
	if (events_in_progress) {
		events_in_progress--;
		event_count++;
	}
	spin_unlock_irqrestore(&events_lock, flags);
}

/**
 * pm_wakeup_timer_fn - Delayed finalization of a wakeup event.
 *
 * Decrease the counter of wakeup events being processed after it was increased
 * by pm_wakeup_event().
 */
static void pm_wakeup_timer_fn(unsigned long data)
{
	unsigned long flags;

	spin_lock_irqsave(&events_lock, flags);
	if (events_timer_expires
	    && time_before_eq(events_timer_expires, jiffies)) {
		events_in_progress--;
		events_timer_expires = 0;
	}
	spin_unlock_irqrestore(&events_lock, flags);
}

/**
 * pm_wakeup_event - Notify the PM core of a wakeup event.
 * @dev: Device the wakeup event is related to.
 * @msec: Anticipated event processing time (in milliseconds).
 *
 * Notify the PM core of a wakeup event (signaled by @dev) that will take
 * approximately @msec milliseconds to be processed by the kernel.  Increment
 * the counter of registered wakeup events and (if @msec is nonzero) set up
 * the wakeup events timer to execute pm_wakeup_timer_fn() in future (if the
 * timer has not been set up already, increment the counter of wakeup events
 * being processed).  If @dev is not NULL, the counter of wakeup events related
 * to @dev is incremented too.
 *
 * It is safe to call this function from interrupt context.
 */
void pm_wakeup_event(struct device *dev, unsigned int msec)
{
	unsigned long flags;

	spin_lock_irqsave(&events_lock, flags);
	event_count++;
	if (dev)
		dev->power.wakeup_count++;

	if (msec) {
		unsigned long expires;

		expires = jiffies + msecs_to_jiffies(msec);
		if (!expires)
			expires = 1;

		if (!events_timer_expires
		    || time_after(expires, events_timer_expires)) {
			if (!events_timer_expires)
				events_in_progress++;

			mod_timer(&events_timer, expires);
			events_timer_expires = expires;
		}
	}
	spin_unlock_irqrestore(&events_lock, flags);
}

/**
 * pm_check_wakeup_events - Check for new wakeup events.
 *
 * Compare the current number of registered wakeup events with its preserved
 * value from the past to check if new wakeup events have been registered since
 * the old value was stored.  Check if the current number of wakeup events being
 * processed is zero.
 */
bool pm_check_wakeup_events(void)
{
	unsigned long flags;
	bool ret = true;

	spin_lock_irqsave(&events_lock, flags);
	if (events_check_enabled) {
		ret = (event_count == saved_event_count) && !events_in_progress;
		events_check_enabled = ret;
	}
	spin_unlock_irqrestore(&events_lock, flags);
	return ret;
}

/**
 * pm_get_wakeup_count - Read the number of registered wakeup events.
 * @count: Address to store the value at.
 *
 * Store the number of registered wakeup events at the address in @count.  Block
 * if the current number of wakeup events being processed is nonzero.
 *
 * Return false if the wait for the number of wakeup events being processed to
 * drop down to zero has been interrupted by a signal (and the current number
 * of wakeup events being processed is still nonzero).  Otherwise return true.
 */
bool pm_get_wakeup_count(unsigned long *count)
{
	bool ret;

	spin_lock_irq(&events_lock);
	if (capable(CAP_SYS_ADMIN))
		events_check_enabled = false;

	while (events_in_progress && !signal_pending(current)) {
		spin_unlock_irq(&events_lock);

		schedule_timeout_interruptible(msecs_to_jiffies(100));

		spin_lock_irq(&events_lock);
	}
	*count = event_count;
	ret = !events_in_progress;
	spin_unlock_irq(&events_lock);
	return ret;
}

/**
 * pm_save_wakeup_count - Save the current number of registered wakeup events.
 * @count: Value to compare with the current number of registered wakeup events.
 *
 * If @count is equal to the current number of registered wakeup events and the
 * current number of wakeup events being processed is zero, store @count as the
 * old number of registered wakeup events to be used by pm_check_wakeup_events()
 * and return true.  Otherwise return false.
 */
bool pm_save_wakeup_count(unsigned long count)
{
	bool ret = false;

	spin_lock_irq(&events_lock);
	if (count == event_count && !events_in_progress) {
		saved_event_count = count;
		events_check_enabled = true;
		ret = true;
	}
	spin_unlock_irq(&events_lock);
	return ret;
}
