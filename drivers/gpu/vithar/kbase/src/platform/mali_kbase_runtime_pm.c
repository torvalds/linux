/* drivers/gpu/vithar/kbase/src/platform/mali_kbase_runtime_pm.c
 *
 * Copyright 2011 by S.LSI. Samsung Electronics Inc.
 * San#24, Nongseo-Dong, Giheung-Gu, Yongin, Korea
 *
 * Samsung SoC Mali-T604 runtime pm driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software FoundatIon.
 */

/**
 * @file mali_kbase_runtime_pm.c
 * Runtime PM
 */

#include <osk/mali_osk.h>
#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_kbase_uku.h>
#include <kbase/src/common/mali_kbase_mem.h>
#include <kbase/src/common/mali_midg_regmap.h>
#include <kbase/src/linux/mali_kbase_mem_linux.h>
#include <uk/mali_ukk.h>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/miscdevice.h>
#include <linux/list.h>
#include <linux/semaphore.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/timer.h>
#include <kbase/src/platform/mali_kbase_platform.h>
#include <linux/pm_runtime.h>


/** Indicator to use suspended power off scheme.
 *
 * if SUSPENDED_OFF is defined, power gating to mali-t604 is RUNTIME_PM_RUNTIME_DELAY_TIME delayed.
 */
#define SUSPENDED_OFF
#define RUNTIME_PM_RUNTIME_DELAY_TIME 50
//static struct timer_list runtime_pm_timer;

static void kbase_device_runtime_timer_callback(unsigned long data)
{
#ifdef SUSPENDED_OFF
	struct kbase_device *kbdev;
	kbdev = dev_get_drvdata((struct device *)data);

	kbase_pm_wait_for_power_down(kbdev);
	kbase_platform_cmu_pmu_control((struct device *)data, 0);
#endif
}

void kbase_device_runtime_init_timer(struct device *dev)
{
#ifdef SUSPENDED_OFF
	struct kbase_device *kbdev;
	kbdev = dev_get_drvdata(dev);

	init_timer(&kbdev->runtime_pm_timer);

	kbdev->runtime_pm_timer.expires = jiffies + RUNTIME_PM_RUNTIME_DELAY_TIME;
	kbdev->runtime_pm_timer.data = (unsigned long)dev;
	kbdev->runtime_pm_timer.function = kbase_device_runtime_timer_callback;

	add_timer(&kbdev->runtime_pm_timer);
#endif
}

/** Suspend callback from the OS.
 *
 * This is called by Linux runtime PM when the device should suspend.
 *
 * @param dev	The device to suspend
 *
 * @return A standard Linux error code
 */
int kbase_device_runtime_suspend(struct device *dev)
{
#ifdef SUSPENDED_OFF
	struct kbase_device *kbdev;
	kbdev = dev_get_drvdata(dev);

	mod_timer(&kbdev->runtime_pm_timer, jiffies + RUNTIME_PM_RUNTIME_DELAY_TIME);
	return 0;
#else
	return kbase_platform_cmu_pmu_control(dev, 0);
#endif
}

/** Resume callback from the OS.
 *
 * This is called by Linux runtime PM when the device should resume from suspension.
 *
 * @param dev	The device to resume
 *
 * @return A standard Linux error code
 */
int kbase_device_runtime_resume(struct device *dev)
{
#ifdef SUSPENDED_OFF
	struct kbase_device *kbdev;
	kbdev = dev_get_drvdata(dev);

	del_timer_sync(&kbdev->runtime_pm_timer);
#endif
	return kbase_platform_cmu_pmu_control(dev, 1);
}

/** Disable runtime pm
 *
 * @param dev	The device to enable rpm
 *
 * @return A standard Linux error code
 */
void kbase_device_runtime_disable(struct device *dev)
{
    pm_runtime_disable(dev);
}

/** Initialize runtiem pm fields in given device 
 *
 * @param dev	The device to initialize
 *
 * @return A standard Linux error code
 */
static void kbase_device_runtime_init(struct device *dev)
{
	pm_suspend_ignore_children(dev, true);
	pm_runtime_enable(dev);
}

static int rp_started = 1;
void kbase_device_runtime_get_sync(struct device *dev)
{
	int result;

	if(rp_started)
	{
		kbase_device_runtime_init(dev);
		rp_started = 0;
	}

	result = pm_runtime_get_sync(dev);
	//OSK_PRINT_ERROR(OSK_BASE_PM, "get_sync, usage_count=%d  \n", atomic_read(&dev->power.usage_count));
	if(result < 0)
		OSK_PRINT_ERROR(OSK_BASE_PM, "pm_runtime_get_sync failed (%d)\n", result);
}

void kbase_device_runtime_put_sync(struct device *dev)
{
	int result;

	if(rp_started)
	{
		return;
	}

	result = pm_runtime_put_sync(dev);
	//OSK_PRINT_ERROR(OSK_BASE_PM, "put_sync, usage_count=%d  \n", atomic_read(&dev->power.usage_count));
	if(result < 0)
		OSK_PRINT_ERROR(OSK_BASE_PM, "pm_runtime_put_sync failed (%d)\n", result);

}
