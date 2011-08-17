/*
 * arch/sh/kernel/cpu/shmobile/pm_runtime.c
 *
 * Runtime PM support code for SuperH Mobile
 *
 *  Copyright (C) 2009 Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <asm/hwblk.h>

static DEFINE_SPINLOCK(hwblk_lock);
static LIST_HEAD(hwblk_idle_list);
static struct work_struct hwblk_work;

extern struct hwblk_info *hwblk_info;

static void platform_pm_runtime_not_idle(struct platform_device *pdev)
{
	unsigned long flags;

	/* remove device from idle list */
	spin_lock_irqsave(&hwblk_lock, flags);
	if (test_bit(PDEV_ARCHDATA_FLAG_IDLE, &pdev->archdata.flags)) {
		list_del(&pdev->archdata.entry);
		__clear_bit(PDEV_ARCHDATA_FLAG_IDLE, &pdev->archdata.flags);
	}
	spin_unlock_irqrestore(&hwblk_lock, flags);
}

static int __platform_pm_runtime_resume(struct platform_device *pdev)
{
	struct device *d = &pdev->dev;
	struct pdev_archdata *ad = &pdev->archdata;
	int hwblk = ad->hwblk_id;
	int ret = -ENOSYS;

	dev_dbg(d, "__platform_pm_runtime_resume() [%d]\n", hwblk);

	if (d->driver) {
		hwblk_enable(hwblk_info, hwblk);
		ret = 0;

		if (test_bit(PDEV_ARCHDATA_FLAG_SUSP, &ad->flags)) {
			if (d->driver->pm && d->driver->pm->runtime_resume)
				ret = d->driver->pm->runtime_resume(d);

			if (!ret)
				clear_bit(PDEV_ARCHDATA_FLAG_SUSP, &ad->flags);
			else
				hwblk_disable(hwblk_info, hwblk);
		}
	}

	dev_dbg(d, "__platform_pm_runtime_resume() [%d] - returns %d\n",
		hwblk, ret);

	return ret;
}

static int __platform_pm_runtime_suspend(struct platform_device *pdev)
{
	struct device *d = &pdev->dev;
	struct pdev_archdata *ad = &pdev->archdata;
	int hwblk = ad->hwblk_id;
	int ret = -ENOSYS;

	dev_dbg(d, "__platform_pm_runtime_suspend() [%d]\n", hwblk);

	if (d->driver) {
		BUG_ON(!test_bit(PDEV_ARCHDATA_FLAG_IDLE, &ad->flags));
		ret = 0;

		if (d->driver->pm && d->driver->pm->runtime_suspend) {
			hwblk_enable(hwblk_info, hwblk);
			ret = d->driver->pm->runtime_suspend(d);
			hwblk_disable(hwblk_info, hwblk);
		}

		if (!ret) {
			set_bit(PDEV_ARCHDATA_FLAG_SUSP, &ad->flags);
			platform_pm_runtime_not_idle(pdev);
			hwblk_cnt_dec(hwblk_info, hwblk, HWBLK_CNT_IDLE);
		}
	}

	dev_dbg(d, "__platform_pm_runtime_suspend() [%d] - returns %d\n",
		hwblk, ret);

	return ret;
}

static void platform_pm_runtime_work(struct work_struct *work)
{
	struct platform_device *pdev;
	unsigned long flags;
	int ret;

	/* go through the idle list and suspend one device at a time */
	do {
		spin_lock_irqsave(&hwblk_lock, flags);
		if (list_empty(&hwblk_idle_list))
			pdev = NULL;
		else
			pdev = list_first_entry(&hwblk_idle_list,
						struct platform_device,
						archdata.entry);
		spin_unlock_irqrestore(&hwblk_lock, flags);

		if (pdev) {
			mutex_lock(&pdev->archdata.mutex);
			ret = __platform_pm_runtime_suspend(pdev);

			/* at this point the platform device may be:
			 * suspended: ret = 0, FLAG_SUSP set, clock stopped
			 * failed: ret < 0, FLAG_IDLE set, clock stopped
			 */
			mutex_unlock(&pdev->archdata.mutex);
		} else {
			ret = -ENODEV;
		}
	} while (!ret);
}

/* this function gets called from cpuidle context when all devices in the
 * main power domain are unused but some are counted as idle, ie the hwblk
 * counter values are (HWBLK_CNT_USAGE == 0) && (HWBLK_CNT_IDLE != 0)
 */
void platform_pm_runtime_suspend_idle(void)
{
	queue_work(pm_wq, &hwblk_work);
}

static int default_platform_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pdev_archdata *ad = &pdev->archdata;
	unsigned long flags;
	int hwblk = ad->hwblk_id;
	int ret = 0;

	dev_dbg(dev, "%s() [%d]\n", __func__, hwblk);

	/* ignore off-chip platform devices */
	if (!hwblk)
		goto out;

	/* interrupt context not allowed */
	might_sleep();

	/* catch misconfigured drivers not starting with resume */
	if (test_bit(PDEV_ARCHDATA_FLAG_INIT, &ad->flags)) {
		ret = -EINVAL;
		goto out;
	}

	/* serialize */
	mutex_lock(&ad->mutex);

	/* disable clock */
	hwblk_disable(hwblk_info, hwblk);

	/* put device on idle list */
	spin_lock_irqsave(&hwblk_lock, flags);
	list_add_tail(&ad->entry, &hwblk_idle_list);
	__set_bit(PDEV_ARCHDATA_FLAG_IDLE, &ad->flags);
	spin_unlock_irqrestore(&hwblk_lock, flags);

	/* increase idle count */
	hwblk_cnt_inc(hwblk_info, hwblk, HWBLK_CNT_IDLE);

	/* at this point the platform device is:
	 * idle: ret = 0, FLAG_IDLE set, clock stopped
	 */
	mutex_unlock(&ad->mutex);

out:
	dev_dbg(dev, "%s() [%d] returns %d\n",
		 __func__, hwblk, ret);

	return ret;
}

static int default_platform_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pdev_archdata *ad = &pdev->archdata;
	int hwblk = ad->hwblk_id;
	int ret = 0;

	dev_dbg(dev, "%s() [%d]\n", __func__, hwblk);

	/* ignore off-chip platform devices */
	if (!hwblk)
		goto out;

	/* interrupt context not allowed */
	might_sleep();

	/* serialize */
	mutex_lock(&ad->mutex);

	/* make sure device is removed from idle list */
	platform_pm_runtime_not_idle(pdev);

	/* decrease idle count */
	if (!test_bit(PDEV_ARCHDATA_FLAG_INIT, &pdev->archdata.flags) &&
	    !test_bit(PDEV_ARCHDATA_FLAG_SUSP, &pdev->archdata.flags))
		hwblk_cnt_dec(hwblk_info, hwblk, HWBLK_CNT_IDLE);

	/* resume the device if needed */
	ret = __platform_pm_runtime_resume(pdev);

	/* the driver has been initialized now, so clear the init flag */
	clear_bit(PDEV_ARCHDATA_FLAG_INIT, &pdev->archdata.flags);

	/* at this point the platform device may be:
	 * resumed: ret = 0, flags = 0, clock started
	 * failed: ret < 0, FLAG_SUSP set, clock stopped
	 */
	mutex_unlock(&ad->mutex);
out:
	dev_dbg(dev, "%s() [%d] returns %d\n",
		__func__, hwblk, ret);

	return ret;
}

static int default_platform_runtime_idle(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	int hwblk = pdev->archdata.hwblk_id;
	int ret = 0;

	dev_dbg(dev, "%s() [%d]\n", __func__, hwblk);

	/* ignore off-chip platform devices */
	if (!hwblk)
		goto out;

	/* interrupt context not allowed, use pm_runtime_put()! */
	might_sleep();

	/* suspend synchronously to disable clocks immediately */
	ret = pm_runtime_suspend(dev);
out:
	dev_dbg(dev, "%s() [%d] done!\n", __func__, hwblk);
	return ret;
}

static struct dev_pm_domain default_pm_domain = {
	.ops = {
		.runtime_suspend = default_platform_runtime_suspend,
		.runtime_resume = default_platform_runtime_resume,
		.runtime_idle = default_platform_runtime_idle,
		USE_PLATFORM_PM_SLEEP_OPS
	},
};

static int platform_bus_notify(struct notifier_block *nb,
			       unsigned long action, void *data)
{
	struct device *dev = data;
	struct platform_device *pdev = to_platform_device(dev);
	int hwblk = pdev->archdata.hwblk_id;

	/* ignore off-chip platform devices */
	if (!hwblk)
		return 0;

	switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
		INIT_LIST_HEAD(&pdev->archdata.entry);
		mutex_init(&pdev->archdata.mutex);
		/* platform devices without drivers should be disabled */
		hwblk_enable(hwblk_info, hwblk);
		hwblk_disable(hwblk_info, hwblk);
		/* make sure driver re-inits itself once */
		__set_bit(PDEV_ARCHDATA_FLAG_INIT, &pdev->archdata.flags);
		dev->pm_domain = &default_pm_domain;
		break;
	/* TODO: add BUS_NOTIFY_BIND_DRIVER and increase idle count */
	case BUS_NOTIFY_BOUND_DRIVER:
		/* keep track of number of devices in use per hwblk */
		hwblk_cnt_inc(hwblk_info, hwblk, HWBLK_CNT_DEVICES);
		break;
	case BUS_NOTIFY_UNBOUND_DRIVER:
		/* keep track of number of devices in use per hwblk */
		hwblk_cnt_dec(hwblk_info, hwblk, HWBLK_CNT_DEVICES);
		/* make sure driver re-inits itself once */
		__set_bit(PDEV_ARCHDATA_FLAG_INIT, &pdev->archdata.flags);
		break;
	case BUS_NOTIFY_DEL_DEVICE:
		dev->pm_domain = NULL;
		break;
	}
	return 0;
}

static struct notifier_block platform_bus_notifier = {
	.notifier_call = platform_bus_notify
};

static int __init sh_pm_runtime_init(void)
{
	INIT_WORK(&hwblk_work, platform_pm_runtime_work);

	bus_register_notifier(&platform_bus_type, &platform_bus_notifier);
	return 0;
}
core_initcall(sh_pm_runtime_init);
