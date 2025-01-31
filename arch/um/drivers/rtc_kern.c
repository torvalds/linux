// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Intel Corporation
 * Author: Johannes Berg <johannes@sipsolutions.net>
 */
#include <linux/platform_device.h>
#include <linux/time-internal.h>
#include <linux/suspend.h>
#include <linux/err.h>
#include <linux/rtc.h>
#include <kern_util.h>
#include <irq_kern.h>
#include <os.h>
#include "rtc.h"

static time64_t uml_rtc_alarm_time;
static bool uml_rtc_alarm_enabled;
static struct rtc_device *uml_rtc;
static int uml_rtc_irq_fd, uml_rtc_irq;

#ifdef CONFIG_UML_TIME_TRAVEL_SUPPORT

static void uml_rtc_time_travel_alarm(struct time_travel_event *ev)
{
	uml_rtc_send_timetravel_alarm();
}

static struct time_travel_event uml_rtc_alarm_event = {
	.fn = uml_rtc_time_travel_alarm,
};
#endif

static int uml_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct timespec64 ts;

	/* Use this to get correct time in time-travel mode */
	read_persistent_clock64(&ts);
	rtc_time64_to_tm(timespec64_to_ktime(ts) / NSEC_PER_SEC, tm);

	return 0;
}

static int uml_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	rtc_time64_to_tm(uml_rtc_alarm_time, &alrm->time);
	alrm->enabled = uml_rtc_alarm_enabled;

	return 0;
}

static int uml_rtc_alarm_irq_enable(struct device *dev, unsigned int enable)
{
	struct timespec64 ts;
	unsigned long long secs;

	if (!enable && !uml_rtc_alarm_enabled)
		return 0;

	uml_rtc_alarm_enabled = enable;

	read_persistent_clock64(&ts);
	secs = uml_rtc_alarm_time - ts.tv_sec;

	if (time_travel_mode == TT_MODE_OFF) {
		if (!enable) {
			uml_rtc_disable_alarm();
			return 0;
		}

		/* enable or update */
		return uml_rtc_enable_alarm(secs);
	} else {
		time_travel_del_event(&uml_rtc_alarm_event);

		if (enable)
			time_travel_add_event_rel(&uml_rtc_alarm_event,
						  secs * NSEC_PER_SEC -
						  ts.tv_nsec);
	}

	return 0;
}

static int uml_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	uml_rtc_alarm_irq_enable(dev, 0);
	uml_rtc_alarm_time = rtc_tm_to_time64(&alrm->time);
	uml_rtc_alarm_irq_enable(dev, alrm->enabled);

	return 0;
}

static const struct rtc_class_ops uml_rtc_ops = {
	.read_time = uml_rtc_read_time,
	.read_alarm = uml_rtc_read_alarm,
	.alarm_irq_enable = uml_rtc_alarm_irq_enable,
	.set_alarm = uml_rtc_set_alarm,
};

static irqreturn_t uml_rtc_interrupt(int irq, void *data)
{
	unsigned long long c = 0;

	/* alarm triggered, it's now off */
	uml_rtc_alarm_enabled = false;

	os_read_file(uml_rtc_irq_fd, &c, sizeof(c));
	WARN_ON(c == 0);

	pm_system_wakeup();
	rtc_update_irq(uml_rtc, 1, RTC_IRQF | RTC_AF);

	return IRQ_HANDLED;
}

static int uml_rtc_setup(void)
{
	int err;

	err = uml_rtc_start(time_travel_mode != TT_MODE_OFF);
	if (WARN(err < 0, "err = %d\n", err))
		return err;

	uml_rtc_irq_fd = err;

	err = um_request_irq(UM_IRQ_ALLOC, uml_rtc_irq_fd, IRQ_READ,
			     uml_rtc_interrupt, 0, "rtc", NULL);
	if (err < 0) {
		uml_rtc_stop(time_travel_mode != TT_MODE_OFF);
		return err;
	}

	irq_set_irq_wake(err, 1);

	uml_rtc_irq = err;
	return 0;
}

static void uml_rtc_cleanup(void)
{
	um_free_irq(uml_rtc_irq, NULL);
	uml_rtc_stop(time_travel_mode != TT_MODE_OFF);
}

static int uml_rtc_probe(struct platform_device *pdev)
{
	int err;

	err = uml_rtc_setup();
	if (err)
		return err;

	uml_rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(uml_rtc)) {
		err = PTR_ERR(uml_rtc);
		goto cleanup;
	}

	uml_rtc->ops = &uml_rtc_ops;

	device_init_wakeup(&pdev->dev, 1);

	err = devm_rtc_register_device(uml_rtc);
	if (err)
		goto cleanup;

	return 0;
cleanup:
	uml_rtc_cleanup();
	return err;
}

static void uml_rtc_remove(struct platform_device *pdev)
{
	device_init_wakeup(&pdev->dev, 0);
	uml_rtc_cleanup();
}

static struct platform_driver uml_rtc_driver = {
	.probe = uml_rtc_probe,
	.remove = uml_rtc_remove,
	.driver = {
		.name = "uml-rtc",
	},
};

static int __init uml_rtc_init(void)
{
	struct platform_device *pdev;
	int err;

	err = platform_driver_register(&uml_rtc_driver);
	if (err)
		return err;

	pdev = platform_device_alloc("uml-rtc", 0);
	if (!pdev) {
		err = -ENOMEM;
		goto unregister;
	}

	err = platform_device_add(pdev);
	if (err)
		goto unregister;
	return 0;

unregister:
	platform_device_put(pdev);
	platform_driver_unregister(&uml_rtc_driver);
	return err;
}
device_initcall(uml_rtc_init);
