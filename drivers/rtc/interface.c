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

int rtc_read_time(struct class_device *class_dev, struct rtc_time *tm)
{
	int err;
	struct rtc_device *rtc = to_rtc_device(class_dev);

	err = mutex_lock_interruptible(&rtc->ops_lock);
	if (err)
		return -EBUSY;

	if (!rtc->ops)
		err = -ENODEV;
	else if (!rtc->ops->read_time)
		err = -EINVAL;
	else {
		memset(tm, 0, sizeof(struct rtc_time));
		err = rtc->ops->read_time(class_dev->dev, tm);
	}

	mutex_unlock(&rtc->ops_lock);
	return err;
}
EXPORT_SYMBOL_GPL(rtc_read_time);

int rtc_set_time(struct class_device *class_dev, struct rtc_time *tm)
{
	int err;
	struct rtc_device *rtc = to_rtc_device(class_dev);

	err = rtc_valid_tm(tm);
	if (err != 0)
		return err;

	err = mutex_lock_interruptible(&rtc->ops_lock);
	if (err)
		return -EBUSY;

	if (!rtc->ops)
		err = -ENODEV;
	else if (!rtc->ops->set_time)
		err = -EINVAL;
	else
		err = rtc->ops->set_time(class_dev->dev, tm);

	mutex_unlock(&rtc->ops_lock);
	return err;
}
EXPORT_SYMBOL_GPL(rtc_set_time);

int rtc_set_mmss(struct class_device *class_dev, unsigned long secs)
{
	int err;
	struct rtc_device *rtc = to_rtc_device(class_dev);

	err = mutex_lock_interruptible(&rtc->ops_lock);
	if (err)
		return -EBUSY;

	if (!rtc->ops)
		err = -ENODEV;
	else if (rtc->ops->set_mmss)
		err = rtc->ops->set_mmss(class_dev->dev, secs);
	else if (rtc->ops->read_time && rtc->ops->set_time) {
		struct rtc_time new, old;

		err = rtc->ops->read_time(class_dev->dev, &old);
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
				err = rtc->ops->set_time(class_dev->dev, &new);
		}
	}
	else
		err = -EINVAL;

	mutex_unlock(&rtc->ops_lock);

	return err;
}
EXPORT_SYMBOL_GPL(rtc_set_mmss);

int rtc_read_alarm(struct class_device *class_dev, struct rtc_wkalrm *alarm)
{
	int err;
	struct rtc_device *rtc = to_rtc_device(class_dev);

	err = mutex_lock_interruptible(&rtc->ops_lock);
	if (err)
		return -EBUSY;

	if (rtc->ops == NULL)
		err = -ENODEV;
	else if (!rtc->ops->read_alarm)
		err = -EINVAL;
	else {
		memset(alarm, 0, sizeof(struct rtc_wkalrm));
		err = rtc->ops->read_alarm(class_dev->dev, alarm);
	}

	mutex_unlock(&rtc->ops_lock);
	return err;
}
EXPORT_SYMBOL_GPL(rtc_read_alarm);

int rtc_set_alarm(struct class_device *class_dev, struct rtc_wkalrm *alarm)
{
	int err;
	struct rtc_device *rtc = to_rtc_device(class_dev);

	err = mutex_lock_interruptible(&rtc->ops_lock);
	if (err)
		return -EBUSY;

	if (!rtc->ops)
		err = -ENODEV;
	else if (!rtc->ops->set_alarm)
		err = -EINVAL;
	else
		err = rtc->ops->set_alarm(class_dev->dev, alarm);

	mutex_unlock(&rtc->ops_lock);
	return err;
}
EXPORT_SYMBOL_GPL(rtc_set_alarm);

void rtc_update_irq(struct class_device *class_dev,
		unsigned long num, unsigned long events)
{
	struct rtc_device *rtc = to_rtc_device(class_dev);

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

struct class_device *rtc_class_open(char *name)
{
	struct class_device *class_dev = NULL,
				*class_dev_tmp;

	down(&rtc_class->sem);
	list_for_each_entry(class_dev_tmp, &rtc_class->children, node) {
		if (strncmp(class_dev_tmp->class_id, name, BUS_ID_SIZE) == 0) {
			class_dev = class_dev_tmp;
			break;
		}
	}

	if (class_dev) {
		if (!try_module_get(to_rtc_device(class_dev)->owner))
			class_dev = NULL;
	}
	up(&rtc_class->sem);

	return class_dev;
}
EXPORT_SYMBOL_GPL(rtc_class_open);

void rtc_class_close(struct class_device *class_dev)
{
	module_put(to_rtc_device(class_dev)->owner);
}
EXPORT_SYMBOL_GPL(rtc_class_close);

int rtc_irq_register(struct class_device *class_dev, struct rtc_task *task)
{
	int retval = -EBUSY;
	struct rtc_device *rtc = to_rtc_device(class_dev);

	if (task == NULL || task->func == NULL)
		return -EINVAL;

	spin_lock(&rtc->irq_task_lock);
	if (rtc->irq_task == NULL) {
		rtc->irq_task = task;
		retval = 0;
	}
	spin_unlock(&rtc->irq_task_lock);

	return retval;
}
EXPORT_SYMBOL_GPL(rtc_irq_register);

void rtc_irq_unregister(struct class_device *class_dev, struct rtc_task *task)
{
	struct rtc_device *rtc = to_rtc_device(class_dev);

	spin_lock(&rtc->irq_task_lock);
	if (rtc->irq_task == task)
		rtc->irq_task = NULL;
	spin_unlock(&rtc->irq_task_lock);
}
EXPORT_SYMBOL_GPL(rtc_irq_unregister);

int rtc_irq_set_state(struct class_device *class_dev, struct rtc_task *task, int enabled)
{
	int err = 0;
	unsigned long flags;
	struct rtc_device *rtc = to_rtc_device(class_dev);

	spin_lock_irqsave(&rtc->irq_task_lock, flags);
	if (rtc->irq_task != task)
		err = -ENXIO;
	spin_unlock_irqrestore(&rtc->irq_task_lock, flags);

	if (err == 0)
		err = rtc->ops->irq_set_state(class_dev->dev, enabled);

	return err;
}
EXPORT_SYMBOL_GPL(rtc_irq_set_state);

int rtc_irq_set_freq(struct class_device *class_dev, struct rtc_task *task, int freq)
{
	int err = 0, tmp = 0;
	unsigned long flags;
	struct rtc_device *rtc = to_rtc_device(class_dev);

	/* allowed range is 2-8192 */
	if (freq < 2 || freq > 8192)
		return -EINVAL;
/*
	FIXME: this does not belong here, will move where appropriate
	at a later stage. It cannot hurt right now, trust me :)
	if ((freq > rtc_max_user_freq) && (!capable(CAP_SYS_RESOURCE)))
		return -EACCES;
*/
	/* check if freq is a power of 2 */
	while (freq > (1 << tmp))
		tmp++;

	if (freq != (1 << tmp))
		return -EINVAL;

	spin_lock_irqsave(&rtc->irq_task_lock, flags);
	if (rtc->irq_task != task)
		err = -ENXIO;
	spin_unlock_irqrestore(&rtc->irq_task_lock, flags);

	if (err == 0) {
		err = rtc->ops->irq_set_freq(class_dev->dev, freq);
		if (err == 0)
			rtc->irq_freq = freq;
	}
	return err;
}
