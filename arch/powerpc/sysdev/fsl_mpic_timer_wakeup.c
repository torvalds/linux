/*
 * MPIC timer wakeup driver
 *
 * Copyright 2013 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/device.h>

#include <asm/mpic_timer.h>
#include <asm/mpic.h>

struct fsl_mpic_timer_wakeup {
	struct mpic_timer *timer;
	struct work_struct free_work;
};

static struct fsl_mpic_timer_wakeup *fsl_wakeup;
static DEFINE_MUTEX(sysfs_lock);

static void fsl_free_resource(struct work_struct *ws)
{
	struct fsl_mpic_timer_wakeup *wakeup =
		container_of(ws, struct fsl_mpic_timer_wakeup, free_work);

	mutex_lock(&sysfs_lock);

	if (wakeup->timer) {
		disable_irq_wake(wakeup->timer->irq);
		mpic_free_timer(wakeup->timer);
	}

	wakeup->timer = NULL;
	mutex_unlock(&sysfs_lock);
}

static irqreturn_t fsl_mpic_timer_irq(int irq, void *dev_id)
{
	struct fsl_mpic_timer_wakeup *wakeup = dev_id;

	schedule_work(&wakeup->free_work);

	return wakeup->timer ? IRQ_HANDLED : IRQ_NONE;
}

static ssize_t fsl_timer_wakeup_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct timeval interval;
	int val = 0;

	mutex_lock(&sysfs_lock);
	if (fsl_wakeup->timer) {
		mpic_get_remain_time(fsl_wakeup->timer, &interval);
		val = interval.tv_sec + 1;
	}
	mutex_unlock(&sysfs_lock);

	return sprintf(buf, "%d\n", val);
}

static ssize_t fsl_timer_wakeup_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf,
				size_t count)
{
	struct timeval interval;
	int ret;

	interval.tv_usec = 0;
	if (kstrtol(buf, 0, &interval.tv_sec))
		return -EINVAL;

	mutex_lock(&sysfs_lock);

	if (fsl_wakeup->timer) {
		disable_irq_wake(fsl_wakeup->timer->irq);
		mpic_free_timer(fsl_wakeup->timer);
		fsl_wakeup->timer = NULL;
	}

	if (!interval.tv_sec) {
		mutex_unlock(&sysfs_lock);
		return count;
	}

	fsl_wakeup->timer = mpic_request_timer(fsl_mpic_timer_irq,
						fsl_wakeup, &interval);
	if (!fsl_wakeup->timer) {
		mutex_unlock(&sysfs_lock);
		return -EINVAL;
	}

	ret = enable_irq_wake(fsl_wakeup->timer->irq);
	if (ret) {
		mpic_free_timer(fsl_wakeup->timer);
		fsl_wakeup->timer = NULL;
		mutex_unlock(&sysfs_lock);

		return ret;
	}

	mpic_start_timer(fsl_wakeup->timer);

	mutex_unlock(&sysfs_lock);

	return count;
}

static struct device_attribute mpic_attributes = __ATTR(timer_wakeup, 0644,
			fsl_timer_wakeup_show, fsl_timer_wakeup_store);

static int __init fsl_wakeup_sys_init(void)
{
	int ret;

	fsl_wakeup = kzalloc(sizeof(struct fsl_mpic_timer_wakeup), GFP_KERNEL);
	if (!fsl_wakeup)
		return -ENOMEM;

	INIT_WORK(&fsl_wakeup->free_work, fsl_free_resource);

	ret = device_create_file(mpic_subsys.dev_root, &mpic_attributes);
	if (ret)
		kfree(fsl_wakeup);

	return ret;
}

static void __exit fsl_wakeup_sys_exit(void)
{
	device_remove_file(mpic_subsys.dev_root, &mpic_attributes);

	mutex_lock(&sysfs_lock);

	if (fsl_wakeup->timer) {
		disable_irq_wake(fsl_wakeup->timer->irq);
		mpic_free_timer(fsl_wakeup->timer);
	}

	kfree(fsl_wakeup);

	mutex_unlock(&sysfs_lock);
}

module_init(fsl_wakeup_sys_init);
module_exit(fsl_wakeup_sys_exit);

MODULE_DESCRIPTION("Freescale MPIC global timer wakeup driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Wang Dongsheng <dongsheng.wang@freescale.com>");
