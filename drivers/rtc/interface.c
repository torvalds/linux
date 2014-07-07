/*
 * RTC subsystem, interface functions
 *
 * Copyright (C) 2005 Tower Technologies
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * based on arch/arm/common/rtctime.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/rtc.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/log2.h>
#include <linux/workqueue.h>

static int rtc_timer_enqueue(struct rtc_device *rtc, struct rtc_timer *timer);
static void rtc_timer_remove(struct rtc_device *rtc, struct rtc_timer *timer);

static int __rtc_read_time(struct rtc_device *rtc, struct rtc_time *tm)
{
	int err;
	if (!rtc->ops)
		err = -ENODEV;
	else if (!rtc->ops->read_time)
		err = -EINVAL;
	else {
		memset(tm, 0, sizeof(struct rtc_time));
		err = rtc->ops->read_time(rtc->dev.parent, tm);
	}
	return err;
}

int rtc_read_time(struct rtc_device *rtc, struct rtc_time *tm)
{
	int err;

	err = mutex_lock_interruptible(&rtc->ops_lock);
	if (err)
		return err;

	err = __rtc_read_time(rtc, tm);
	mutex_unlock(&rtc->ops_lock);
	return err;
}
EXPORT_SYMBOL_GPL(rtc_read_time);

int rtc_set_time(struct rtc_device *rtc, struct rtc_time *tm)
{
	int err;

	err = rtc_valid_tm(tm);
	if (err != 0)
		return err;

	err = mutex_lock_interruptible(&rtc->ops_lock);
	if (err)
		return err;

	if (!rtc->ops)
		err = -ENODEV;
	else if (rtc->ops->set_time)
		err = rtc->ops->set_time(rtc->dev.parent, tm);
	else if (rtc->ops->set_mmss) {
		unsigned long secs;
		err = rtc_tm_to_time(tm, &secs);
		if (err == 0)
			err = rtc->ops->set_mmss(rtc->dev.parent, secs);
	} else
		err = -EINVAL;

	pm_stay_awake(rtc->dev.parent);
	mutex_unlock(&rtc->ops_lock);
	/* A timer might have just expired */
	schedule_work(&rtc->irqwork);
	return err;
}
EXPORT_SYMBOL_GPL(rtc_set_time);

int rtc_set_mmss(struct rtc_device *rtc, unsigned long secs)
{
	int err;

	err = mutex_lock_interruptible(&rtc->ops_lock);
	if (err)
		return err;

	if (!rtc->ops)
		err = -ENODEV;
	else if (rtc->ops->set_mmss)
		err = rtc->ops->set_mmss(rtc->dev.parent, secs);
	else if (rtc->ops->read_time && rtc->ops->set_time) {
		struct rtc_time new, old;

		err = rtc->ops->read_time(rtc->dev.parent, &old);
		if (err == 0) {
			rtc_time_to_tm(secs, &new);

			/*
			 * avoid writing when we're going to change the day of
			 * the month. We will retry in the next minute. This
			 * basically means that if the RTC must not drift
			 * by more than 1 minute in 11 minutes.
			 */
			if (!((old.tm_hour == 23 && old.tm_min == 59) ||
				(new.tm_hour == 23 && new.tm_min == 59)))
				err = rtc->ops->set_time(rtc->dev.parent,
						&new);
		}
	} else {
		err = -EINVAL;
	}

	pm_stay_awake(rtc->dev.parent);
	mutex_unlock(&rtc->ops_lock);
	/* A timer might have just expired */
	schedule_work(&rtc->irqwork);

	return err;
}
EXPORT_SYMBOL_GPL(rtc_set_mmss);

static int rtc_read_alarm_internal(struct rtc_device *rtc, struct rtc_wkalrm *alarm)
{
	int err;

	err = mutex_lock_interruptible(&rtc->ops_lock);
	if (err)
		return err;

	if (rtc->ops == NULL)
		err = -ENODEV;
	else if (!rtc->ops->read_alarm)
		err = -EINVAL;
	else {
		memset(alarm, 0, sizeof(struct rtc_wkalrm));
		err = rtc->ops->read_alarm(rtc->dev.parent, alarm);
	}

	mutex_unlock(&rtc->ops_lock);
	return err;
}

int __rtc_read_alarm(struct rtc_device *rtc, struct rtc_wkalrm *alarm)
{
	int err;
	struct rtc_time before, now;
	int first_time = 1;
	unsigned long t_now, t_alm;
	enum { none, day, month, year } missing = none;
	unsigned days;

	/* The lower level RTC driver may return -1 in some fields,
	 * creating invalid alarm->time values, for reasons like:
	 *
	 *   - The hardware may not be capable of filling them in;
	 *     many alarms match only on time-of-day fields, not
	 *     day/month/year calendar data.
	 *
	 *   - Some hardware uses illegal values as "wildcard" match
	 *     values, which non-Linux firmware (like a BIOS) may try
	 *     to set up as e.g. "alarm 15 minutes after each hour".
	 *     Linux uses only oneshot alarms.
	 *
	 * When we see that here, we deal with it by using values from
	 * a current RTC timestamp for any missing (-1) values.  The
	 * RTC driver prevents "periodic alarm" modes.
	 *
	 * But this can be racey, because some fields of the RTC timestamp
	 * may have wrapped in the interval since we read the RTC alarm,
	 * which would lead to us inserting inconsistent values in place
	 * of the -1 fields.
	 *
	 * Reading the alarm and timestamp in the reverse sequence
	 * would have the same race condition, and not solve the issue.
	 *
	 * So, we must first read the RTC timestamp,
	 * then read the RTC alarm value,
	 * and then read a second RTC timestamp.
	 *
	 * If any fields of the second timestamp have changed
	 * when compared with the first timestamp, then we know
	 * our timestamp may be inconsistent with that used by
	 * the low-level rtc_read_alarm_internal() function.
	 *
	 * So, when the two timestamps disagree, we just loop and do
	 * the process again to get a fully consistent set of values.
	 *
	 * This could all instead be done in the lower level driver,
	 * but since more than one lower level RTC implementation needs it,
	 * then it's probably best best to do it here instead of there..
	 */

	/* Get the "before" timestamp */
	err = rtc_read_time(rtc, &before);
	if (err < 0)
		return err;
	do {
		if (!first_time)
			memcpy(&before, &now, sizeof(struct rtc_time));
		first_time = 0;

		/* get the RTC alarm values, which may be incomplete */
		err = rtc_read_alarm_internal(rtc, alarm);
		if (err)
			return err;

		/* full-function RTCs won't have such missing fields */
		if (rtc_valid_tm(&alarm->time) == 0)
			return 0;

		/* get the "after" timestamp, to detect wrapped fields */
		err = rtc_read_time(rtc, &now);
		if (err < 0)
			return err;

		/* note that tm_sec is a "don't care" value here: */
	} while (   before.tm_min   != now.tm_min
		 || before.tm_hour  != now.tm_hour
		 || before.tm_mon   != now.tm_mon
		 || before.tm_year  != now.tm_year);

	/* Fill in the missing alarm fields using the timestamp; we
	 * know there's at least one since alarm->time is invalid.
	 */
	if (alarm->time.tm_sec == -1)
		alarm->time.tm_sec = now.tm_sec;
	if (alarm->time.tm_min == -1)
		alarm->time.tm_min = now.tm_min;
	if (alarm->time.tm_hour == -1)
		alarm->time.tm_hour = now.tm_hour;

	/* For simplicity, only support date rollover for now */
	if (alarm->time.tm_mday < 1 || alarm->time.tm_mday > 31) {
		alarm->time.tm_mday = now.tm_mday;
		missing = day;
	}
	if ((unsigned)alarm->time.tm_mon >= 12) {
		alarm->time.tm_mon = now.tm_mon;
		if (missing == none)
			missing = month;
	}
	if (alarm->time.tm_year == -1) {
		alarm->time.tm_year = now.tm_year;
		if (missing == none)
			missing = year;
	}

	/* with luck, no rollover is needed */
	rtc_tm_to_time(&now, &t_now);
	rtc_tm_to_time(&alarm->time, &t_alm);
	if (t_now < t_alm)
		goto done;

	switch (missing) {

	/* 24 hour rollover ... if it's now 10am Monday, an alarm that
	 * that will trigger at 5am will do so at 5am Tuesday, which
	 * could also be in the next month or year.  This is a common
	 * case, especially for PCs.
	 */
	case day:
		dev_dbg(&rtc->dev, "alarm rollover: %s\n", "day");
		t_alm += 24 * 60 * 60;
		rtc_time_to_tm(t_alm, &alarm->time);
		break;

	/* Month rollover ... if it's the 31th, an alarm on the 3rd will
	 * be next month.  An alarm matching on the 30th, 29th, or 28th
	 * may end up in the month after that!  Many newer PCs support
	 * this type of alarm.
	 */
	case month:
		dev_dbg(&rtc->dev, "alarm rollover: %s\n", "month");
		do {
			if (alarm->time.tm_mon < 11)
				alarm->time.tm_mon++;
			else {
				alarm->time.tm_mon = 0;
				alarm->time.tm_year++;
			}
			days = rtc_month_days(alarm->time.tm_mon,
					alarm->time.tm_year);
		} while (days < alarm->time.tm_mday);
		break;

	/* Year rollover ... easy except for leap years! */
	case year:
		dev_dbg(&rtc->dev, "alarm rollover: %s\n", "year");
		do {
			alarm->time.tm_year++;
		} while (!is_leap_year(alarm->time.tm_year + 1900)
			&& rtc_valid_tm(&alarm->time) != 0);
		break;

	default:
		dev_warn(&rtc->dev, "alarm rollover not handled\n");
	}

done:
	err = rtc_valid_tm(&alarm->time);

	if (err) {
		dev_warn(&rtc->dev, "invalid alarm value: %d-%d-%d %d:%d:%d\n",
			alarm->time.tm_year + 1900, alarm->time.tm_mon + 1,
			alarm->time.tm_mday, alarm->time.tm_hour, alarm->time.tm_min,
			alarm->time.tm_sec);
	}

	return err;
}

int rtc_read_alarm(struct rtc_device *rtc, struct rtc_wkalrm *alarm)
{
	int err;

	err = mutex_lock_interruptible(&rtc->ops_lock);
	if (err)
		return err;
	if (rtc->ops == NULL)
		err = -ENODEV;
	else if (!rtc->ops->read_alarm)
		err = -EINVAL;
	else {
		memset(alarm, 0, sizeof(struct rtc_wkalrm));
		alarm->enabled = rtc->aie_timer.enabled;
		alarm->time = rtc_ktime_to_tm(rtc->aie_timer.node.expires);
	}
	mutex_unlock(&rtc->ops_lock);

	return err;
}
EXPORT_SYMBOL_GPL(rtc_read_alarm);

static int __rtc_set_alarm(struct rtc_device *rtc, struct rtc_wkalrm *alarm)
{
	struct rtc_time tm;
	long now, scheduled;
	int err;

	err = rtc_valid_tm(&alarm->time);
	if (err)
		return err;
	rtc_tm_to_time(&alarm->time, &scheduled);

	/* Make sure we're not setting alarms in the past */
	err = __rtc_read_time(rtc, &tm);
	rtc_tm_to_time(&tm, &now);
	if (scheduled <= now)
		return -ETIME;
	/*
	 * XXX - We just checked to make sure the alarm time is not
	 * in the past, but there is still a race window where if
	 * the is alarm set for the next second and the second ticks
	 * over right here, before we set the alarm.
	 */

	if (!rtc->ops)
		err = -ENODEV;
	else if (!rtc->ops->set_alarm)
		err = -EINVAL;
	else
		err = rtc->ops->set_alarm(rtc->dev.parent, alarm);

	return err;
}

int rtc_set_alarm(struct rtc_device *rtc, struct rtc_wkalrm *alarm)
{
	int err;

	err = rtc_valid_tm(&alarm->time);
	if (err != 0)
		return err;

	err = mutex_lock_interruptible(&rtc->ops_lock);
	if (err)
		return err;
	if (rtc->aie_timer.enabled)
		rtc_timer_remove(rtc, &rtc->aie_timer);

	rtc->aie_timer.node.expires = rtc_tm_to_ktime(alarm->time);
	rtc->aie_timer.period = ktime_set(0, 0);
	if (alarm->enabled)
		err = rtc_timer_enqueue(rtc, &rtc->aie_timer);

	mutex_unlock(&rtc->ops_lock);
	return err;
}
EXPORT_SYMBOL_GPL(rtc_set_alarm);

/* Called once per device from rtc_device_register */
int rtc_initialize_alarm(struct rtc_device *rtc, struct rtc_wkalrm *alarm)
{
	int err;
	struct rtc_time now;

	err = rtc_valid_tm(&alarm->time);
	if (err != 0)
		return err;

	err = rtc_read_time(rtc, &now);
	if (err)
		return err;

	err = mutex_lock_interruptible(&rtc->ops_lock);
	if (err)
		return err;

	rtc->aie_timer.node.expires = rtc_tm_to_ktime(alarm->time);
	rtc->aie_timer.period = ktime_set(0, 0);

	/* Alarm has to be enabled & in the futrure for us to enqueue it */
	if (alarm->enabled && (rtc_tm_to_ktime(now).tv64 <
			 rtc->aie_timer.node.expires.tv64)) {

		rtc->aie_timer.enabled = 1;
		timerqueue_add(&rtc->timerqueue, &rtc->aie_timer.node);
	}
	mutex_unlock(&rtc->ops_lock);
	return err;
}
EXPORT_SYMBOL_GPL(rtc_initialize_alarm);



int rtc_alarm_irq_enable(struct rtc_device *rtc, unsigned int enabled)
{
	int err = mutex_lock_interruptible(&rtc->ops_lock);
	if (err)
		return err;

	if (rtc->aie_timer.enabled != enabled) {
		if (enabled)
			err = rtc_timer_enqueue(rtc, &rtc->aie_timer);
		else
			rtc_timer_remove(rtc, &rtc->aie_timer);
	}

	if (err)
		/* nothing */;
	else if (!rtc->ops)
		err = -ENODEV;
	else if (!rtc->ops->alarm_irq_enable)
		err = -EINVAL;
	else
		err = rtc->ops->alarm_irq_enable(rtc->dev.parent, enabled);

	mutex_unlock(&rtc->ops_lock);
	return err;
}
EXPORT_SYMBOL_GPL(rtc_alarm_irq_enable);

int rtc_update_irq_enable(struct rtc_device *rtc, unsigned int enabled)
{
	int err = mutex_lock_interruptible(&rtc->ops_lock);
	if (err)
		return err;

#ifdef CONFIG_RTC_INTF_DEV_UIE_EMUL
	if (enabled == 0 && rtc->uie_irq_active) {
		mutex_unlock(&rtc->ops_lock);
		return rtc_dev_update_irq_enable_emul(rtc, 0);
	}
#endif
	/* make sure we're changing state */
	if (rtc->uie_rtctimer.enabled == enabled)
		goto out;

	if (rtc->uie_unsupported) {
		err = -EINVAL;
		goto out;
	}

	if (enabled) {
		struct rtc_time tm;
		ktime_t now, onesec;

		__rtc_read_time(rtc, &tm);
		onesec = ktime_set(1, 0);
		now = rtc_tm_to_ktime(tm);
		rtc->uie_rtctimer.node.expires = ktime_add(now, onesec);
		rtc->uie_rtctimer.period = ktime_set(1, 0);
		err = rtc_timer_enqueue(rtc, &rtc->uie_rtctimer);
	} else
		rtc_timer_remove(rtc, &rtc->uie_rtctimer);

out:
	mutex_unlock(&rtc->ops_lock);
#ifdef CONFIG_RTC_INTF_DEV_UIE_EMUL
	/*
	 * Enable emulation if the driver did not provide
	 * the update_irq_enable function pointer or if returned
	 * -EINVAL to signal that it has been configured without
	 * interrupts or that are not available at the moment.
	 */
	if (err == -EINVAL)
		err = rtc_dev_update_irq_enable_emul(rtc, enabled);
#endif
	return err;

}
EXPORT_SYMBOL_GPL(rtc_update_irq_enable);


/**
 * rtc_handle_legacy_irq - AIE, UIE and PIE event hook
 * @rtc: pointer to the rtc device
 *
 * This function is called when an AIE, UIE or PIE mode interrupt
 * has occurred (or been emulated).
 *
 * Triggers the registered irq_task function callback.
 */
void rtc_handle_legacy_irq(struct rtc_device *rtc, int num, int mode)
{
	unsigned long flags;

	/* mark one irq of the appropriate mode */
	spin_lock_irqsave(&rtc->irq_lock, flags);
	rtc->irq_data = (rtc->irq_data + (num << 8)) | (RTC_IRQF|mode);
	spin_unlock_irqrestore(&rtc->irq_lock, flags);

	/* call the task func */
	spin_lock_irqsave(&rtc->irq_task_lock, flags);
	if (rtc->irq_task)
		rtc->irq_task->func(rtc->irq_task->private_data);
	spin_unlock_irqrestore(&rtc->irq_task_lock, flags);

	wake_up_interruptible(&rtc->irq_queue);
	kill_fasync(&rtc->async_queue, SIGIO, POLL_IN);
}


/**
 * rtc_aie_update_irq - AIE mode rtctimer hook
 * @private: pointer to the rtc_device
 *
 * This functions is called when the aie_timer expires.
 */
void rtc_aie_update_irq(void *private)
{
	struct rtc_device *rtc = (struct rtc_device *)private;
	rtc_handle_legacy_irq(rtc, 1, RTC_AF);
}


/**
 * rtc_uie_update_irq - UIE mode rtctimer hook
 * @private: pointer to the rtc_device
 *
 * This functions is called when the uie_timer expires.
 */
void rtc_uie_update_irq(void *private)
{
	struct rtc_device *rtc = (struct rtc_device *)private;
	rtc_handle_legacy_irq(rtc, 1,  RTC_UF);
}


/**
 * rtc_pie_update_irq - PIE mode hrtimer hook
 * @timer: pointer to the pie mode hrtimer
 *
 * This function is used to emulate PIE mode interrupts
 * using an hrtimer. This function is called when the periodic
 * hrtimer expires.
 */
enum hrtimer_restart rtc_pie_update_irq(struct hrtimer *timer)
{
	struct rtc_device *rtc;
	ktime_t period;
	int count;
	rtc = container_of(timer, struct rtc_device, pie_timer);

	period = ktime_set(0, NSEC_PER_SEC/rtc->irq_freq);
	count = hrtimer_forward_now(timer, period);

	rtc_handle_legacy_irq(rtc, count, RTC_PF);

	return HRTIMER_RESTART;
}

/**
 * rtc_update_irq - Triggered when a RTC interrupt occurs.
 * @rtc: the rtc device
 * @num: how many irqs are being reported (usually one)
 * @events: mask of RTC_IRQF with one or more of RTC_PF, RTC_AF, RTC_UF
 * Context: any
 */
void rtc_update_irq(struct rtc_device *rtc,
		unsigned long num, unsigned long events)
{
	if (unlikely(IS_ERR_OR_NULL(rtc)))
		return;

	pm_stay_awake(rtc->dev.parent);
	schedule_work(&rtc->irqwork);
}
EXPORT_SYMBOL_GPL(rtc_update_irq);

static int __rtc_match(struct device *dev, const void *data)
{
	const char *name = data;

	if (strcmp(dev_name(dev), name) == 0)
		return 1;
	return 0;
}

struct rtc_device *rtc_class_open(const char *name)
{
	struct device *dev;
	struct rtc_device *rtc = NULL;

	dev = class_find_device(rtc_class, NULL, name, __rtc_match);
	if (dev)
		rtc = to_rtc_device(dev);

	if (rtc) {
		if (!try_module_get(rtc->owner)) {
			put_device(dev);
			rtc = NULL;
		}
	}

	return rtc;
}
EXPORT_SYMBOL_GPL(rtc_class_open);

void rtc_class_close(struct rtc_device *rtc)
{
	module_put(rtc->owner);
	put_device(&rtc->dev);
}
EXPORT_SYMBOL_GPL(rtc_class_close);

int rtc_irq_register(struct rtc_device *rtc, struct rtc_task *task)
{
	int retval = -EBUSY;

	if (task == NULL || task->func == NULL)
		return -EINVAL;

	/* Cannot register while the char dev is in use */
	if (test_and_set_bit_lock(RTC_DEV_BUSY, &rtc->flags))
		return -EBUSY;

	spin_lock_irq(&rtc->irq_task_lock);
	if (rtc->irq_task == NULL) {
		rtc->irq_task = task;
		retval = 0;
	}
	spin_unlock_irq(&rtc->irq_task_lock);

	clear_bit_unlock(RTC_DEV_BUSY, &rtc->flags);

	return retval;
}
EXPORT_SYMBOL_GPL(rtc_irq_register);

void rtc_irq_unregister(struct rtc_device *rtc, struct rtc_task *task)
{
	spin_lock_irq(&rtc->irq_task_lock);
	if (rtc->irq_task == task)
		rtc->irq_task = NULL;
	spin_unlock_irq(&rtc->irq_task_lock);
}
EXPORT_SYMBOL_GPL(rtc_irq_unregister);

static int rtc_update_hrtimer(struct rtc_device *rtc, int enabled)
{
	/*
	 * We always cancel the timer here first, because otherwise
	 * we could run into BUG_ON(timer->state != HRTIMER_STATE_CALLBACK);
	 * when we manage to start the timer before the callback
	 * returns HRTIMER_RESTART.
	 *
	 * We cannot use hrtimer_cancel() here as a running callback
	 * could be blocked on rtc->irq_task_lock and hrtimer_cancel()
	 * would spin forever.
	 */
	if (hrtimer_try_to_cancel(&rtc->pie_timer) < 0)
		return -1;

	if (enabled) {
		ktime_t period = ktime_set(0, NSEC_PER_SEC / rtc->irq_freq);

		hrtimer_start(&rtc->pie_timer, period, HRTIMER_MODE_REL);
	}
	return 0;
}

/**
 * rtc_irq_set_state - enable/disable 2^N Hz periodic IRQs
 * @rtc: the rtc device
 * @task: currently registered with rtc_irq_register()
 * @enabled: true to enable periodic IRQs
 * Context: any
 *
 * Note that rtc_irq_set_freq() should previously have been used to
 * specify the desired frequency of periodic IRQ task->func() callbacks.
 */
int rtc_irq_set_state(struct rtc_device *rtc, struct rtc_task *task, int enabled)
{
	int err = 0;
	unsigned long flags;

retry:
	spin_lock_irqsave(&rtc->irq_task_lock, flags);
	if (rtc->irq_task != NULL && task == NULL)
		err = -EBUSY;
	else if (rtc->irq_task != task)
		err = -EACCES;
	else {
		if (rtc_update_hrtimer(rtc, enabled) < 0) {
			spin_unlock_irqrestore(&rtc->irq_task_lock, flags);
			cpu_relax();
			goto retry;
		}
		rtc->pie_enabled = enabled;
	}
	spin_unlock_irqrestore(&rtc->irq_task_lock, flags);
	return err;
}
EXPORT_SYMBOL_GPL(rtc_irq_set_state);

/**
 * rtc_irq_set_freq - set 2^N Hz periodic IRQ frequency for IRQ
 * @rtc: the rtc device
 * @task: currently registered with rtc_irq_register()
 * @freq: positive frequency with which task->func() will be called
 * Context: any
 *
 * Note that rtc_irq_set_state() is used to enable or disable the
 * periodic IRQs.
 */
int rtc_irq_set_freq(struct rtc_device *rtc, struct rtc_task *task, int freq)
{
	int err = 0;
	unsigned long flags;

	if (freq <= 0 || freq > RTC_MAX_FREQ)
		return -EINVAL;
retry:
	spin_lock_irqsave(&rtc->irq_task_lock, flags);
	if (rtc->irq_task != NULL && task == NULL)
		err = -EBUSY;
	else if (rtc->irq_task != task)
		err = -EACCES;
	else {
		rtc->irq_freq = freq;
		if (rtc->pie_enabled && rtc_update_hrtimer(rtc, 1) < 0) {
			spin_unlock_irqrestore(&rtc->irq_task_lock, flags);
			cpu_relax();
			goto retry;
		}
	}
	spin_unlock_irqrestore(&rtc->irq_task_lock, flags);
	return err;
}
EXPORT_SYMBOL_GPL(rtc_irq_set_freq);

/**
 * rtc_timer_enqueue - Adds a rtc_timer to the rtc_device timerqueue
 * @rtc rtc device
 * @timer timer being added.
 *
 * Enqueues a timer onto the rtc devices timerqueue and sets
 * the next alarm event appropriately.
 *
 * Sets the enabled bit on the added timer.
 *
 * Must hold ops_lock for proper serialization of timerqueue
 */
static int rtc_timer_enqueue(struct rtc_device *rtc, struct rtc_timer *timer)
{
	timer->enabled = 1;
	timerqueue_add(&rtc->timerqueue, &timer->node);
	if (&timer->node == timerqueue_getnext(&rtc->timerqueue)) {
		struct rtc_wkalrm alarm;
		int err;
		alarm.time = rtc_ktime_to_tm(timer->node.expires);
		alarm.enabled = 1;
		err = __rtc_set_alarm(rtc, &alarm);
		if (err == -ETIME) {
			pm_stay_awake(rtc->dev.parent);
			schedule_work(&rtc->irqwork);
		} else if (err) {
			timerqueue_del(&rtc->timerqueue, &timer->node);
			timer->enabled = 0;
			return err;
		}
	}
	return 0;
}

static void rtc_alarm_disable(struct rtc_device *rtc)
{
	if (!rtc->ops || !rtc->ops->alarm_irq_enable)
		return;

	rtc->ops->alarm_irq_enable(rtc->dev.parent, false);
}

/**
 * rtc_timer_remove - Removes a rtc_timer from the rtc_device timerqueue
 * @rtc rtc device
 * @timer timer being removed.
 *
 * Removes a timer onto the rtc devices timerqueue and sets
 * the next alarm event appropriately.
 *
 * Clears the enabled bit on the removed timer.
 *
 * Must hold ops_lock for proper serialization of timerqueue
 */
static void rtc_timer_remove(struct rtc_device *rtc, struct rtc_timer *timer)
{
	struct timerqueue_node *next = timerqueue_getnext(&rtc->timerqueue);
	timerqueue_del(&rtc->timerqueue, &timer->node);
	timer->enabled = 0;
	if (next == &timer->node) {
		struct rtc_wkalrm alarm;
		int err;
		next = timerqueue_getnext(&rtc->timerqueue);
		if (!next) {
			rtc_alarm_disable(rtc);
			return;
		}
		alarm.time = rtc_ktime_to_tm(next->expires);
		alarm.enabled = 1;
		err = __rtc_set_alarm(rtc, &alarm);
		if (err == -ETIME) {
			pm_stay_awake(rtc->dev.parent);
			schedule_work(&rtc->irqwork);
		}
	}
}

/**
 * rtc_timer_do_work - Expires rtc timers
 * @rtc rtc device
 * @timer timer being removed.
 *
 * Expires rtc timers. Reprograms next alarm event if needed.
 * Called via worktask.
 *
 * Serializes access to timerqueue via ops_lock mutex
 */
void rtc_timer_do_work(struct work_struct *work)
{
	struct rtc_timer *timer;
	struct timerqueue_node *next;
	ktime_t now;
	struct rtc_time tm;

	struct rtc_device *rtc =
		container_of(work, struct rtc_device, irqwork);

	mutex_lock(&rtc->ops_lock);
again:
	__rtc_read_time(rtc, &tm);
	now = rtc_tm_to_ktime(tm);
	while ((next = timerqueue_getnext(&rtc->timerqueue))) {
		if (next->expires.tv64 > now.tv64)
			break;

		/* expire timer */
		timer = container_of(next, struct rtc_timer, node);
		timerqueue_del(&rtc->timerqueue, &timer->node);
		timer->enabled = 0;
		if (timer->task.func)
			timer->task.func(timer->task.private_data);

		/* Re-add/fwd periodic timers */
		if (ktime_to_ns(timer->period)) {
			timer->node.expires = ktime_add(timer->node.expires,
							timer->period);
			timer->enabled = 1;
			timerqueue_add(&rtc->timerqueue, &timer->node);
		}
	}

	/* Set next alarm */
	if (next) {
		struct rtc_wkalrm alarm;
		int err;
		alarm.time = rtc_ktime_to_tm(next->expires);
		alarm.enabled = 1;
		err = __rtc_set_alarm(rtc, &alarm);
		if (err == -ETIME)
			goto again;
	} else
		rtc_alarm_disable(rtc);

	pm_relax(rtc->dev.parent);
	mutex_unlock(&rtc->ops_lock);
}


/* rtc_timer_init - Initializes an rtc_timer
 * @timer: timer to be intiialized
 * @f: function pointer to be called when timer fires
 * @data: private data passed to function pointer
 *
 * Kernel interface to initializing an rtc_timer.
 */
void rtc_timer_init(struct rtc_timer *timer, void (*f)(void *p), void *data)
{
	timerqueue_init(&timer->node);
	timer->enabled = 0;
	timer->task.func = f;
	timer->task.private_data = data;
}

/* rtc_timer_start - Sets an rtc_timer to fire in the future
 * @ rtc: rtc device to be used
 * @ timer: timer being set
 * @ expires: time at which to expire the timer
 * @ period: period that the timer will recur
 *
 * Kernel interface to set an rtc_timer
 */
int rtc_timer_start(struct rtc_device *rtc, struct rtc_timer *timer,
			ktime_t expires, ktime_t period)
{
	int ret = 0;
	mutex_lock(&rtc->ops_lock);
	if (timer->enabled)
		rtc_timer_remove(rtc, timer);

	timer->node.expires = expires;
	timer->period = period;

	ret = rtc_timer_enqueue(rtc, timer);

	mutex_unlock(&rtc->ops_lock);
	return ret;
}

/* rtc_timer_cancel - Stops an rtc_timer
 * @ rtc: rtc device to be used
 * @ timer: timer being set
 *
 * Kernel interface to cancel an rtc_timer
 */
int rtc_timer_cancel(struct rtc_device *rtc, struct rtc_timer *timer)
{
	int ret = 0;
	mutex_lock(&rtc->ops_lock);
	if (timer->enabled)
		rtc_timer_remove(rtc, timer);
	mutex_unlock(&rtc->ops_lock);
	return ret;
}


