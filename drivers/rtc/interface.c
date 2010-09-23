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
#include <linux/log2.h>
#include <linux/workqueue.h>

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

int rtc_read_alarm(struct rtc_device *rtc, struct rtc_wkalrm *alarm)
{
	int err;

	err = mutex_lock_interruptible(&rtc->ops_lock);
	if (err)
		return err;
	alarm->enabled = rtc->aie_timer.enabled;
	if (alarm->enabled)
		alarm->time = rtc_ktime_to_tm(rtc->aie_timer.node.expires);
	mutex_unlock(&rtc->ops_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(rtc_read_alarm);

int __rtc_set_alarm(struct rtc_device *rtc, struct rtc_wkalrm *alarm)
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
	if (rtc->aie_timer.enabled) {
		rtctimer_remove(rtc, &rtc->aie_timer);
		rtc->aie_timer.enabled = 0;
	}
	rtc->aie_timer.node.expires = rtc_tm_to_ktime(alarm->time);
	rtc->aie_timer.period = ktime_set(0, 0);
	if (alarm->enabled) {
		rtc->aie_timer.enabled = 1;
		rtctimer_enqueue(rtc, &rtc->aie_timer);
	}
	mutex_unlock(&rtc->ops_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(rtc_set_alarm);

int rtc_alarm_irq_enable(struct rtc_device *rtc, unsigned int enabled)
{
	int err = mutex_lock_interruptible(&rtc->ops_lock);
	if (err)
		return err;

	if (rtc->aie_timer.enabled != enabled) {
		if (enabled) {
			rtc->aie_timer.enabled = 1;
			rtctimer_enqueue(rtc, &rtc->aie_timer);
		} else {
			rtctimer_remove(rtc, &rtc->aie_timer);
			rtc->aie_timer.enabled = 0;
		}
	}

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

	/* make sure we're changing state */
	if (rtc->uie_rtctimer.enabled == enabled)
		goto out;

	if (enabled) {
		struct rtc_time tm;
		ktime_t now, onesec;

		__rtc_read_time(rtc, &tm);
		onesec = ktime_set(1, 0);
		now = rtc_tm_to_ktime(tm);
		rtc->uie_rtctimer.node.expires = ktime_add(now, onesec);
		rtc->uie_rtctimer.period = ktime_set(1, 0);
		rtc->uie_rtctimer.enabled = 1;
		rtctimer_enqueue(rtc, &rtc->uie_rtctimer);
	} else {
		rtctimer_remove(rtc, &rtc->uie_rtctimer);
		rtc->uie_rtctimer.enabled = 0;
	}

out:
	mutex_unlock(&rtc->ops_lock);
	return err;

}
EXPORT_SYMBOL_GPL(rtc_update_irq_enable);


/**
 * rtc_handle_legacy_irq - AIE, UIE and PIE event hook
 * @rtc: pointer to the rtc device
 *
 * This function is called when an AIE, UIE or PIE mode interrupt
 * has occured (or been emulated).
 *
 * Triggers the registered irq_task function callback.
 */
static void rtc_handle_legacy_irq(struct rtc_device *rtc, int num, int mode)
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
	schedule_work(&rtc->irqwork);
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

	spin_lock_irqsave(&rtc->irq_task_lock, flags);
	if (rtc->irq_task != NULL && task == NULL)
		err = -EBUSY;
	if (rtc->irq_task != task)
		err = -EACCES;

	if (enabled) {
		ktime_t period = ktime_set(0, NSEC_PER_SEC/rtc->irq_freq);
		hrtimer_start(&rtc->pie_timer, period, HRTIMER_MODE_REL);
	} else {
		hrtimer_cancel(&rtc->pie_timer);
	}
	rtc->pie_enabled = enabled;
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

	spin_lock_irqsave(&rtc->irq_task_lock, flags);
	if (rtc->irq_task != NULL && task == NULL)
		err = -EBUSY;
	if (rtc->irq_task != task)
		err = -EACCES;
	if (err == 0) {
		rtc->irq_freq = freq;
		if (rtc->pie_enabled) {
			ktime_t period;
			hrtimer_cancel(&rtc->pie_timer);
			period = ktime_set(0, NSEC_PER_SEC/rtc->irq_freq);
			hrtimer_start(&rtc->pie_timer, period,
					HRTIMER_MODE_REL);
		}
	}
	spin_unlock_irqrestore(&rtc->irq_task_lock, flags);
	return err;
}
EXPORT_SYMBOL_GPL(rtc_irq_set_freq);

/**
 * rtctimer_enqueue - Adds a rtc_timer to the rtc_device timerqueue
 * @rtc rtc device
 * @timer timer being added.
 *
 * Enqueues a timer onto the rtc devices timerqueue and sets
 * the next alarm event appropriately.
 *
 * Must hold ops_lock for proper serialization of timerqueue
 */
void rtctimer_enqueue(struct rtc_device *rtc, struct rtc_timer *timer)
{
	timerqueue_add(&rtc->timerqueue, &timer->node);
	if (&timer->node == timerqueue_getnext(&rtc->timerqueue)) {
		struct rtc_wkalrm alarm;
		int err;
		alarm.time = rtc_ktime_to_tm(timer->node.expires);
		alarm.enabled = 1;
		err = __rtc_set_alarm(rtc, &alarm);
		if (err == -ETIME)
			schedule_work(&rtc->irqwork);
	}
}

/**
 * rtctimer_remove - Removes a rtc_timer from the rtc_device timerqueue
 * @rtc rtc device
 * @timer timer being removed.
 *
 * Removes a timer onto the rtc devices timerqueue and sets
 * the next alarm event appropriately.
 *
 * Must hold ops_lock for proper serialization of timerqueue
 */
void rtctimer_remove(struct rtc_device *rtc, struct rtc_timer *timer)
{
	struct timerqueue_node *next = timerqueue_getnext(&rtc->timerqueue);
	timerqueue_del(&rtc->timerqueue, &timer->node);

	if (next == &timer->node) {
		struct rtc_wkalrm alarm;
		int err;
		next = timerqueue_getnext(&rtc->timerqueue);
		if (!next)
			return;
		alarm.time = rtc_ktime_to_tm(next->expires);
		alarm.enabled = 1;
		err = __rtc_set_alarm(rtc, &alarm);
		if (err == -ETIME)
			schedule_work(&rtc->irqwork);
	}
}

/**
 * rtctimer_do_work - Expires rtc timers
 * @rtc rtc device
 * @timer timer being removed.
 *
 * Expires rtc timers. Reprograms next alarm event if needed.
 * Called via worktask.
 *
 * Serializes access to timerqueue via ops_lock mutex
 */
void rtctimer_do_work(struct work_struct *work)
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
	}

	mutex_unlock(&rtc->ops_lock);
}


/* rtctimer_init - Initializes an rtc_timer
 * @timer: timer to be intiialized
 * @f: function pointer to be called when timer fires
 * @data: private data passed to function pointer
 *
 * Kernel interface to initializing an rtc_timer.
 */
void rtctimer_init(struct rtc_timer *timer, void (*f)(void* p), void* data)
{
	timerqueue_init(&timer->node);
	timer->enabled = 0;
	timer->task.func = f;
	timer->task.private_data = data;
}

/* rtctimer_start - Sets an rtc_timer to fire in the future
 * @ rtc: rtc device to be used
 * @ timer: timer being set
 * @ expires: time at which to expire the timer
 * @ period: period that the timer will recur
 *
 * Kernel interface to set an rtc_timer
 */
int rtctimer_start(struct rtc_device *rtc, struct rtc_timer* timer,
			ktime_t expires, ktime_t period)
{
	int ret = 0;
	mutex_lock(&rtc->ops_lock);
	if (timer->enabled)
		rtctimer_remove(rtc, timer);

	timer->node.expires = expires;
	timer->period = period;

	timer->enabled = 1;
	rtctimer_enqueue(rtc, timer);

	mutex_unlock(&rtc->ops_lock);
	return ret;
}

/* rtctimer_cancel - Stops an rtc_timer
 * @ rtc: rtc device to be used
 * @ timer: timer being set
 *
 * Kernel interface to cancel an rtc_timer
 */
int rtctimer_cancel(struct rtc_device *rtc, struct rtc_timer* timer)
{
	int ret = 0;
	mutex_lock(&rtc->ops_lock);
	if (timer->enabled)
		rtctimer_remove(rtc, timer);
	timer->enabled = 0;
	mutex_unlock(&rtc->ops_lock);
	return ret;
}


