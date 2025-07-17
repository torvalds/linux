// SPDX-License-Identifier: GPL-2.0
/*
 * RTC subsystem, base class
 *
 * Copyright (C) 2005 Tower Technologies
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * class skeleton from drivers/hwmon/hwmon.c
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/of.h>
#include <linux/rtc.h>
#include <linux/kdev_t.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include "rtc-core.h"

static DEFINE_IDA(rtc_ida);

static void rtc_device_release(struct device *dev)
{
	struct rtc_device *rtc = to_rtc_device(dev);
	struct timerqueue_head *head = &rtc->timerqueue;
	struct timerqueue_node *node;

	mutex_lock(&rtc->ops_lock);
	while ((node = timerqueue_getnext(head)))
		timerqueue_del(head, node);
	mutex_unlock(&rtc->ops_lock);

	cancel_work_sync(&rtc->irqwork);

	ida_free(&rtc_ida, rtc->id);
	mutex_destroy(&rtc->ops_lock);
	kfree(rtc);
}

#ifdef CONFIG_RTC_HCTOSYS_DEVICE
/* Result of the last RTC to system clock attempt. */
int rtc_hctosys_ret = -ENODEV;

/* IMPORTANT: the RTC only stores whole seconds. It is arbitrary
 * whether it stores the most close value or the value with partial
 * seconds truncated. However, it is important that we use it to store
 * the truncated value. This is because otherwise it is necessary,
 * in an rtc sync function, to read both xtime.tv_sec and
 * xtime.tv_nsec. On some processors (i.e. ARM), an atomic read
 * of >32bits is not possible. So storing the most close value would
 * slow down the sync API. So here we have the truncated value and
 * the best guess is to add 0.5s.
 */

static void rtc_hctosys(struct rtc_device *rtc)
{
	int err;
	struct rtc_time tm;
	struct timespec64 tv64 = {
		.tv_nsec = NSEC_PER_SEC >> 1,
	};

	err = rtc_read_time(rtc, &tm);
	if (err) {
		dev_err(rtc->dev.parent,
			"hctosys: unable to read the hardware clock\n");
		goto err_read;
	}

	tv64.tv_sec = rtc_tm_to_time64(&tm);

#if BITS_PER_LONG == 32
	if (tv64.tv_sec > INT_MAX) {
		err = -ERANGE;
		goto err_read;
	}
#endif

	err = do_settimeofday64(&tv64);

	dev_info(rtc->dev.parent, "setting system clock to %ptR UTC (%lld)\n",
		 &tm, (long long)tv64.tv_sec);

err_read:
	rtc_hctosys_ret = err;
}
#endif

#if defined(CONFIG_PM_SLEEP) && defined(CONFIG_RTC_HCTOSYS_DEVICE)
/*
 * On suspend(), measure the delta between one RTC and the
 * system's wall clock; restore it on resume().
 */

static struct timespec64 old_rtc, old_system, old_delta;

static int rtc_suspend(struct device *dev)
{
	struct rtc_device	*rtc = to_rtc_device(dev);
	struct rtc_time		tm;
	struct timespec64	delta, delta_delta;
	int err;

	if (timekeeping_rtc_skipsuspend())
		return 0;

	if (strcmp(dev_name(&rtc->dev), CONFIG_RTC_HCTOSYS_DEVICE) != 0)
		return 0;

	/* snapshot the current RTC and system time at suspend*/
	err = rtc_read_time(rtc, &tm);
	if (err < 0) {
		pr_debug("%s:  fail to read rtc time\n", dev_name(&rtc->dev));
		return 0;
	}

	ktime_get_real_ts64(&old_system);
	old_rtc.tv_sec = rtc_tm_to_time64(&tm);

	/*
	 * To avoid drift caused by repeated suspend/resumes,
	 * which each can add ~1 second drift error,
	 * try to compensate so the difference in system time
	 * and rtc time stays close to constant.
	 */
	delta = timespec64_sub(old_system, old_rtc);
	delta_delta = timespec64_sub(delta, old_delta);
	if (delta_delta.tv_sec < -2 || delta_delta.tv_sec >= 2) {
		/*
		 * if delta_delta is too large, assume time correction
		 * has occurred and set old_delta to the current delta.
		 */
		old_delta = delta;
	} else {
		/* Otherwise try to adjust old_system to compensate */
		old_system = timespec64_sub(old_system, delta_delta);
	}

	return 0;
}

static int rtc_resume(struct device *dev)
{
	struct rtc_device	*rtc = to_rtc_device(dev);
	struct rtc_time		tm;
	struct timespec64	new_system, new_rtc;
	struct timespec64	sleep_time;
	int err;

	if (timekeeping_rtc_skipresume())
		return 0;

	rtc_hctosys_ret = -ENODEV;
	if (strcmp(dev_name(&rtc->dev), CONFIG_RTC_HCTOSYS_DEVICE) != 0)
		return 0;

	/* snapshot the current rtc and system time at resume */
	ktime_get_real_ts64(&new_system);
	err = rtc_read_time(rtc, &tm);
	if (err < 0) {
		pr_debug("%s:  fail to read rtc time\n", dev_name(&rtc->dev));
		return 0;
	}

	new_rtc.tv_sec = rtc_tm_to_time64(&tm);
	new_rtc.tv_nsec = 0;

	if (new_rtc.tv_sec < old_rtc.tv_sec) {
		pr_debug("%s:  time travel!\n", dev_name(&rtc->dev));
		return 0;
	}

	/* calculate the RTC time delta (sleep time)*/
	sleep_time = timespec64_sub(new_rtc, old_rtc);

	/*
	 * Since these RTC suspend/resume handlers are not called
	 * at the very end of suspend or the start of resume,
	 * some run-time may pass on either sides of the sleep time
	 * so subtract kernel run-time between rtc_suspend to rtc_resume
	 * to keep things accurate.
	 */
	sleep_time = timespec64_sub(sleep_time,
				    timespec64_sub(new_system, old_system));

	if (sleep_time.tv_sec >= 0)
		timekeeping_inject_sleeptime64(&sleep_time);
	rtc_hctosys_ret = 0;
	return 0;
}

static SIMPLE_DEV_PM_OPS(rtc_class_dev_pm_ops, rtc_suspend, rtc_resume);
#define RTC_CLASS_DEV_PM_OPS	(&rtc_class_dev_pm_ops)
#else
#define RTC_CLASS_DEV_PM_OPS	NULL
#endif

const struct class rtc_class = {
	.name = "rtc",
	.pm = RTC_CLASS_DEV_PM_OPS,
};

/* Ensure the caller will set the id before releasing the device */
static struct rtc_device *rtc_allocate_device(void)
{
	struct rtc_device *rtc;

	rtc = kzalloc(sizeof(*rtc), GFP_KERNEL);
	if (!rtc)
		return NULL;

	device_initialize(&rtc->dev);

	/*
	 * Drivers can revise this default after allocating the device.
	 * The default is what most RTCs do: Increment seconds exactly one
	 * second after the write happened. This adds a default transport
	 * time of 5ms which is at least halfways close to reality.
	 */
	rtc->set_offset_nsec = NSEC_PER_SEC + 5 * NSEC_PER_MSEC;

	rtc->irq_freq = 1;
	rtc->max_user_freq = 64;
	rtc->dev.class = &rtc_class;
	rtc->dev.groups = rtc_get_dev_attribute_groups();
	rtc->dev.release = rtc_device_release;

	mutex_init(&rtc->ops_lock);
	spin_lock_init(&rtc->irq_lock);
	init_waitqueue_head(&rtc->irq_queue);

	/* Init timerqueue */
	timerqueue_init_head(&rtc->timerqueue);
	INIT_WORK(&rtc->irqwork, rtc_timer_do_work);
	/* Init aie timer */
	rtc_timer_init(&rtc->aie_timer, rtc_aie_update_irq, rtc);
	/* Init uie timer */
	rtc_timer_init(&rtc->uie_rtctimer, rtc_uie_update_irq, rtc);
	/* Init pie timer */
	hrtimer_setup(&rtc->pie_timer, rtc_pie_update_irq, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	rtc->pie_enabled = 0;

	set_bit(RTC_FEATURE_ALARM, rtc->features);
	set_bit(RTC_FEATURE_UPDATE_INTERRUPT, rtc->features);

	return rtc;
}

static int rtc_device_get_id(struct device *dev)
{
	int of_id = -1, id = -1;

	if (dev->of_node)
		of_id = of_alias_get_id(dev->of_node, "rtc");
	else if (dev->parent && dev->parent->of_node)
		of_id = of_alias_get_id(dev->parent->of_node, "rtc");

	if (of_id >= 0) {
		id = ida_alloc_range(&rtc_ida, of_id, of_id, GFP_KERNEL);
		if (id < 0)
			dev_warn(dev, "/aliases ID %d not available\n", of_id);
	}

	if (id < 0)
		id = ida_alloc(&rtc_ida, GFP_KERNEL);

	return id;
}

static void rtc_device_get_offset(struct rtc_device *rtc)
{
	time64_t range_secs;
	u32 start_year;
	int ret;

	/*
	 * If RTC driver did not implement the range of RTC hardware device,
	 * then we can not expand the RTC range by adding or subtracting one
	 * offset.
	 */
	if (rtc->range_min == rtc->range_max)
		return;

	ret = device_property_read_u32(rtc->dev.parent, "start-year",
				       &start_year);
	if (!ret) {
		rtc->start_secs = mktime64(start_year, 1, 1, 0, 0, 0);
		rtc->set_start_time = true;
	}

	/*
	 * If user did not implement the start time for RTC driver, then no
	 * need to expand the RTC range.
	 */
	if (!rtc->set_start_time)
		return;

	range_secs = rtc->range_max - rtc->range_min + 1;

	/*
	 * If the start_secs is larger than the maximum seconds (rtc->range_max)
	 * supported by RTC hardware or the maximum seconds of new expanded
	 * range (start_secs + rtc->range_max - rtc->range_min) is less than
	 * rtc->range_min, which means the minimum seconds (rtc->range_min) of
	 * RTC hardware will be mapped to start_secs by adding one offset, so
	 * the offset seconds calculation formula should be:
	 * rtc->offset_secs = rtc->start_secs - rtc->range_min;
	 *
	 * If the start_secs is larger than the minimum seconds (rtc->range_min)
	 * supported by RTC hardware, then there is one region is overlapped
	 * between the original RTC hardware range and the new expanded range,
	 * and this overlapped region do not need to be mapped into the new
	 * expanded range due to it is valid for RTC device. So the minimum
	 * seconds of RTC hardware (rtc->range_min) should be mapped to
	 * rtc->range_max + 1, then the offset seconds formula should be:
	 * rtc->offset_secs = rtc->range_max - rtc->range_min + 1;
	 *
	 * If the start_secs is less than the minimum seconds (rtc->range_min),
	 * which is similar to case 2. So the start_secs should be mapped to
	 * start_secs + rtc->range_max - rtc->range_min + 1, then the
	 * offset seconds formula should be:
	 * rtc->offset_secs = -(rtc->range_max - rtc->range_min + 1);
	 *
	 * Otherwise the offset seconds should be 0.
	 */
	if ((rtc->start_secs >= 0 && rtc->start_secs > rtc->range_max) ||
	    rtc->start_secs + range_secs - 1 < rtc->range_min)
		rtc->offset_secs = rtc->start_secs - rtc->range_min;
	else if (rtc->start_secs > rtc->range_min)
		rtc->offset_secs = range_secs;
	else if (rtc->start_secs < rtc->range_min)
		rtc->offset_secs = -range_secs;
	else
		rtc->offset_secs = 0;
}

static void devm_rtc_unregister_device(void *data)
{
	struct rtc_device *rtc = data;

	mutex_lock(&rtc->ops_lock);
	/*
	 * Remove innards of this RTC, then disable it, before
	 * letting any rtc_class_open() users access it again
	 */
	rtc_proc_del_device(rtc);
	if (!test_bit(RTC_NO_CDEV, &rtc->flags))
		cdev_device_del(&rtc->char_dev, &rtc->dev);
	rtc->ops = NULL;
	mutex_unlock(&rtc->ops_lock);
}

static void devm_rtc_release_device(void *res)
{
	struct rtc_device *rtc = res;

	put_device(&rtc->dev);
}

struct rtc_device *devm_rtc_allocate_device(struct device *dev)
{
	struct rtc_device *rtc;
	int id, err;

	id = rtc_device_get_id(dev);
	if (id < 0)
		return ERR_PTR(id);

	rtc = rtc_allocate_device();
	if (!rtc) {
		ida_free(&rtc_ida, id);
		return ERR_PTR(-ENOMEM);
	}

	rtc->id = id;
	rtc->dev.parent = dev;
	err = devm_add_action_or_reset(dev, devm_rtc_release_device, rtc);
	if (err)
		return ERR_PTR(err);

	err = dev_set_name(&rtc->dev, "rtc%d", id);
	if (err)
		return ERR_PTR(err);

	return rtc;
}
EXPORT_SYMBOL_GPL(devm_rtc_allocate_device);

int __devm_rtc_register_device(struct module *owner, struct rtc_device *rtc)
{
	struct rtc_wkalrm alrm;
	int err;

	if (!rtc->ops) {
		dev_dbg(&rtc->dev, "no ops set\n");
		return -EINVAL;
	}

	if (!rtc->ops->set_alarm)
		clear_bit(RTC_FEATURE_ALARM, rtc->features);

	if (rtc->ops->set_offset)
		set_bit(RTC_FEATURE_CORRECTION, rtc->features);

	rtc->owner = owner;
	rtc_device_get_offset(rtc);

	/* Check to see if there is an ALARM already set in hw */
	err = __rtc_read_alarm(rtc, &alrm);
	if (!err && !rtc_valid_tm(&alrm.time))
		rtc_initialize_alarm(rtc, &alrm);

	rtc_dev_prepare(rtc);

	err = cdev_device_add(&rtc->char_dev, &rtc->dev);
	if (err) {
		set_bit(RTC_NO_CDEV, &rtc->flags);
		dev_warn(rtc->dev.parent, "failed to add char device %d:%d\n",
			 MAJOR(rtc->dev.devt), rtc->id);
	} else {
		dev_dbg(rtc->dev.parent, "char device (%d:%d)\n",
			MAJOR(rtc->dev.devt), rtc->id);
	}

	rtc_proc_add_device(rtc);

	dev_info(rtc->dev.parent, "registered as %s\n",
		 dev_name(&rtc->dev));

#ifdef CONFIG_RTC_HCTOSYS_DEVICE
	if (!strcmp(dev_name(&rtc->dev), CONFIG_RTC_HCTOSYS_DEVICE))
		rtc_hctosys(rtc);
#endif

	return devm_add_action_or_reset(rtc->dev.parent,
					devm_rtc_unregister_device, rtc);
}
EXPORT_SYMBOL_GPL(__devm_rtc_register_device);

/**
 * devm_rtc_device_register - resource managed rtc_device_register()
 * @dev: the device to register
 * @name: the name of the device (unused)
 * @ops: the rtc operations structure
 * @owner: the module owner
 *
 * @return a struct rtc on success, or an ERR_PTR on error
 *
 * Managed rtc_device_register(). The rtc_device returned from this function
 * are automatically freed on driver detach.
 * This function is deprecated, use devm_rtc_allocate_device and
 * rtc_register_device instead
 */
struct rtc_device *devm_rtc_device_register(struct device *dev,
					    const char *name,
					    const struct rtc_class_ops *ops,
					    struct module *owner)
{
	struct rtc_device *rtc;
	int err;

	rtc = devm_rtc_allocate_device(dev);
	if (IS_ERR(rtc))
		return rtc;

	rtc->ops = ops;

	err = __devm_rtc_register_device(owner, rtc);
	if (err)
		return ERR_PTR(err);

	return rtc;
}
EXPORT_SYMBOL_GPL(devm_rtc_device_register);

static int __init rtc_init(void)
{
	int err;

	err = class_register(&rtc_class);
	if (err)
		return err;

	rtc_dev_init();

	return 0;
}
subsys_initcall(rtc_init);
