// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MPIC timer wakeup driver
 *
 * Copyright 2013 Freescale Semiconductor, Inc.
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
	time64_t interval = 0;

	mutex_lock(&sysfs_lock);
	if (fsl_wakeup->timer) {
		mpic_get_remain_time(fsl_wakeup->timer, &interval);
		interval++;
	}
	mutex_unlock(&sysfs_lock);

	return sprintf(buf, "%lld\n", interval);
}

static ssize_t fsl_timer_wakeup_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf,
				size_t count)
{
	time64_t interval;
	int ret;

	if (kstrtoll(buf, 0, &interval))
		return -EINVAL;

	guard(mutex)(&sysfs_lock);

	if (fsl_wakeup->timer) {
		disable_irq_wake(fsl_wakeup->timer->irq);
		mpic_free_timer(fsl_wakeup->timer);
		fsl_wakeup->timer = NULL;
	}

	if (!interval)
		return count;

	fsl_wakeup->timer = mpic_request_timer(fsl_mpic_timer_irq,
						fsl_wakeup, interval);
	if (!fsl_wakeup->timer)
		return -EINVAL;

	ret = enable_irq_wake(fsl_wakeup->timer->irq);
	if (ret) {
		mpic_free_timer(fsl_wakeup->timer);
		fsl_wakeup->timer = NULL;
		return ret;
	}

	mpic_start_timer(fsl_wakeup->timer);

	return count;
}

static struct device_attribute mpic_attributes = __ATTR(timer_wakeup, 0644,
			fsl_timer_wakeup_show, fsl_timer_wakeup_store);

static int __init fsl_wakeup_sys_init(void)
{
	struct device *dev_root;
	int ret = -EINVAL;

	fsl_wakeup = kzalloc(sizeof(struct fsl_mpic_timer_wakeup), GFP_KERNEL);
	if (!fsl_wakeup)
		return -ENOMEM;

	INIT_WORK(&fsl_wakeup->free_work, fsl_free_resource);

	dev_root = bus_get_dev_root(&mpic_subsys);
	if (dev_root) {
		ret = device_create_file(dev_root, &mpic_attributes);
		put_device(dev_root);
		if (ret)
			kfree(fsl_wakeup);
	}

	return ret;
}

static void __exit fsl_wakeup_sys_exit(void)
{
	struct device *dev_root;

	dev_root = bus_get_dev_root(&mpic_subsys);
	if (dev_root) {
		device_remove_file(dev_root, &mpic_attributes);
		put_device(dev_root);
	}

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
