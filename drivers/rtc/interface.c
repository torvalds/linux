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
#include <linux/log2.h>

int rtc_read_time(struct rtc_device *rtc, struct rtc_time *tm)
{
	int err;

	err = mutex_lock_interruptible(&rtc->ops_lock);
	if (err)
		return err;

	if (!rtc->ops)
		err = -ENODEV;
	else if (!rtc->ops->read_time)
		err = -EINVAL;
	else {
		memset(tm, 0, sizeof(struct rtc_time));
		err = rtc->ops->read_time(rtc->dev.parent, tm);
	}

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

	mutex_unlock(&rtc->ops_lock);
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
	}
	else
		err = -EINVAL;

	mutex_unlock(&rtc->ops_lock);

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

int rtc_read_alarm(struct rtc_device *rtc, struct rtc_wkalrm *alarm)
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
		if (!alarm->enabled)
			return 0;

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
	if (alarm->time.tm_mday == -1) {
		alarm->time.tm_mday = now.tm_mday;
		missing = day;
	}
	if (alarm->time.tm_mon == -1) {
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
		} while (rtc_valid_tm(&alarm->time) != 0);
		break;

	default:
		dev_warn(&rtc->dev, "alarm rollover not handled\n");
	}

done:
	return 0;
}
EXPORT_SYMBOL_GPL(rtc_read_alarm);

int rtc_set_alarm(struct rtc_device *rtc, struct rtc_wkalrm *alarm)
{
	int err;

	err = rtc_valid_tm(&alarm->time);
	if (err != 0)
		return err;

	err = mutex_lock_interruptible(&rtc->ops_lock);
	if (err)
		return err;

	if (!rtc->ops)
		err = -ENODEV;
	else if (!rtc->ops->set_alarm)
		err = -EINVAL;
	else
		err = rtc->ops->set_alarm(rtc->dev.parent, alarm);

	mutex_unlock(&rtc->ops_lock);
	return err;
}
EXPORT_SYMBOL_GPL(rtc_set_alarm);

int rtc_alarm_irq_enable(struct rtc_device *rtc, unsigned int enabled)
{
	int err = mutex_lock_interruptible(&rtc->ops_lock);
	if (err)
		return err;

	if (!rtc->ops)
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
		return rtc_dev_update_irq_enable_emul(rtc, enabled);
	}
#endif

	if (!rtc->ops)
		err = -ENODEV;
	else if (!rtc->ops->update_irq_enable)
		err = -EINVAL;
	else
		err = rtc->ops->update_irq_enable(rtc->dev.parent, enabled);

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
 * rtc_update_irq - report RTC periodic, alarm, and/or update irqs
 * @rtc: the rtc device
 * @num: how many irqs are being reported (usually one)
 * @events: mask of RTC_IRQF with one or more of RTC_PF, RTC_AF, RTC_UF
 * Context: in_interrupt(), irqs blocked
 */
void rtc_update_irq(struct rtc_device *rtc,
		unsigned long num, unsigned long events)
{
	spin_lock(&rtc->irq_lock);
	rtc->irq_data = (rtc->irq_data + (num << 8)) | events;
	spin_unlock(&rtc->irq_lock);

	spin_lock(&rtc->irq_task_lock);
	if (rtc->irq_task)
		rtc->irq_task->func(rtc->irq_task->private_data);
	spin_unlock(&rtc->irq_task_lock);

	wake_up_interruptible(&rtc->irq_queue);
	kill_fasync(&rtc->async_queue, SIGIO, POLL_IN);
}
EXPORT_SYMBOL_GPL(rtc_update_irq);

static int __rtc_match(struct device *dev, void *data)
{
	char *name = (char *)data;

	if (strcmp(dev_name(dev), name) == 0)
		return 1;
	return 0;
}

struct rtc_device *rtc_class_open(char *name)
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

	if (rtc->ops->irq_set_state == NULL)
		return -ENXIO;

	spin_lock_irqsave(&rtc->irq_task_lock, flags);
	if (rtc->irq_task != NULL && task == NULL)
		err = -EBUSY;
	if (rtc->irq_task != task)
		err = -EACCES;
	spin_unlock_irqrestore(&rtc->irq_task_lock, flags);

	if (err == 0)
		err = rtc->ops->irq_set_state(rtc->dev.parent, enabled);

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

	if (rtc->ops->irq_set_freq == NULL)
		return -ENXIO;

	spin_lock_irqsave(&rtc->irq_task_lock, flags);
	if (rtc->irq_task != NULL && task == NULL)
		err = -EBUSY;
	if (rtc->irq_task != task)
		err = -EACCES;
	spin_unlock_irqrestore(&rtc->irq_task_lock, flags);

	if (err == 0) {
		err = rtc->ops->irq_set_freq(rtc->dev.parent, freq);
		if (err == 0)
			rtc->irq_freq = freq;
	}
	return err;
}
EXPORT_SYMBOL_GPL(rtc_irq_set_freq);
