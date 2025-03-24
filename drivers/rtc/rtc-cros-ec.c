// SPDX-License-Identifier: GPL-2.0
// RTC driver for ChromeOS Embedded Controller.
//
// Copyright (C) 2017 Google, Inc.
// Author: Stephen Barber <smbarber@chromium.org>

#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/slab.h>

#define DRV_NAME	"cros-ec-rtc"

#define SECS_PER_DAY	(24 * 60 * 60)

/**
 * struct cros_ec_rtc - Driver data for EC RTC
 *
 * @cros_ec: Pointer to EC device
 * @rtc: Pointer to RTC device
 * @notifier: Notifier info for responding to EC events
 * @saved_alarm: Alarm to restore when interrupts are reenabled
 */
struct cros_ec_rtc {
	struct cros_ec_device *cros_ec;
	struct rtc_device *rtc;
	struct notifier_block notifier;
	u32 saved_alarm;
};

static int cros_ec_rtc_get(struct cros_ec_device *cros_ec, u32 command,
			   u32 *response)
{
	int ret;
	struct {
		struct cros_ec_command msg;
		struct ec_response_rtc data;
	} __packed msg;

	memset(&msg, 0, sizeof(msg));
	msg.msg.command = command;
	msg.msg.insize = sizeof(msg.data);

	ret = cros_ec_cmd_xfer_status(cros_ec, &msg.msg);
	if (ret < 0)
		return ret;

	*response = msg.data.time;

	return 0;
}

static int cros_ec_rtc_set(struct cros_ec_device *cros_ec, u32 command,
			   u32 param)
{
	int ret;
	struct {
		struct cros_ec_command msg;
		struct ec_response_rtc data;
	} __packed msg;

	memset(&msg, 0, sizeof(msg));
	msg.msg.command = command;
	msg.msg.outsize = sizeof(msg.data);
	msg.data.time = param;

	ret = cros_ec_cmd_xfer_status(cros_ec, &msg.msg);
	if (ret < 0)
		return ret;
	return 0;
}

/* Read the current time from the EC. */
static int cros_ec_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct cros_ec_rtc *cros_ec_rtc = dev_get_drvdata(dev);
	struct cros_ec_device *cros_ec = cros_ec_rtc->cros_ec;
	int ret;
	u32 time;

	ret = cros_ec_rtc_get(cros_ec, EC_CMD_RTC_GET_VALUE, &time);
	if (ret) {
		dev_err(dev, "error getting time: %d\n", ret);
		return ret;
	}

	rtc_time64_to_tm(time, tm);

	return 0;
}

/* Set the current EC time. */
static int cros_ec_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct cros_ec_rtc *cros_ec_rtc = dev_get_drvdata(dev);
	struct cros_ec_device *cros_ec = cros_ec_rtc->cros_ec;
	int ret;
	time64_t time = rtc_tm_to_time64(tm);

	ret = cros_ec_rtc_set(cros_ec, EC_CMD_RTC_SET_VALUE, (u32)time);
	if (ret < 0) {
		dev_err(dev, "error setting time: %d\n", ret);
		return ret;
	}

	return 0;
}

/* Read alarm time from RTC. */
static int cros_ec_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct cros_ec_rtc *cros_ec_rtc = dev_get_drvdata(dev);
	struct cros_ec_device *cros_ec = cros_ec_rtc->cros_ec;
	int ret;
	u32 current_time, alarm_offset;

	/*
	 * The EC host command for getting the alarm is relative (i.e. 5
	 * seconds from now) whereas rtc_wkalrm is absolute. Get the current
	 * RTC time first so we can calculate the relative time.
	 */
	ret = cros_ec_rtc_get(cros_ec, EC_CMD_RTC_GET_VALUE, &current_time);
	if (ret < 0) {
		dev_err(dev, "error getting time: %d\n", ret);
		return ret;
	}

	ret = cros_ec_rtc_get(cros_ec, EC_CMD_RTC_GET_ALARM, &alarm_offset);
	if (ret < 0) {
		dev_err(dev, "error getting alarm: %d\n", ret);
		return ret;
	}

	rtc_time64_to_tm(current_time + alarm_offset, &alrm->time);

	return 0;
}

/* Set the EC's RTC alarm. */
static int cros_ec_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct cros_ec_rtc *cros_ec_rtc = dev_get_drvdata(dev);
	struct cros_ec_device *cros_ec = cros_ec_rtc->cros_ec;
	int ret;
	time64_t alarm_time;
	u32 current_time, alarm_offset;

	/*
	 * The EC host command for setting the alarm is relative
	 * (i.e. 5 seconds from now) whereas rtc_wkalrm is absolute.
	 * Get the current RTC time first so we can calculate the
	 * relative time.
	 */
	ret = cros_ec_rtc_get(cros_ec, EC_CMD_RTC_GET_VALUE, &current_time);
	if (ret < 0) {
		dev_err(dev, "error getting time: %d\n", ret);
		return ret;
	}

	alarm_time = rtc_tm_to_time64(&alrm->time);

	if (alarm_time < 0 || alarm_time > U32_MAX)
		return -EINVAL;

	if (!alrm->enabled) {
		/*
		 * If the alarm is being disabled, send an alarm
		 * clear command.
		 */
		alarm_offset = EC_RTC_ALARM_CLEAR;
		cros_ec_rtc->saved_alarm = (u32)alarm_time;
	} else {
		/* Don't set an alarm in the past. */
		if ((u32)alarm_time <= current_time)
			return -ETIME;

		alarm_offset = (u32)alarm_time - current_time;
	}

	ret = cros_ec_rtc_set(cros_ec, EC_CMD_RTC_SET_ALARM, alarm_offset);
	if (ret < 0) {
		dev_err(dev, "error setting alarm in %u seconds: %d\n",
			alarm_offset, ret);
		/*
		 * The EC code returns -EINVAL if the alarm time is too
		 * far in the future. Convert it to the expected error code.
		 */
		if (ret == -EINVAL)
			ret = -ERANGE;
		return ret;
	}

	return 0;
}

static int cros_ec_rtc_alarm_irq_enable(struct device *dev,
					unsigned int enabled)
{
	struct cros_ec_rtc *cros_ec_rtc = dev_get_drvdata(dev);
	struct cros_ec_device *cros_ec = cros_ec_rtc->cros_ec;
	int ret;
	u32 current_time, alarm_offset, alarm_value;

	ret = cros_ec_rtc_get(cros_ec, EC_CMD_RTC_GET_VALUE, &current_time);
	if (ret < 0) {
		dev_err(dev, "error getting time: %d\n", ret);
		return ret;
	}

	if (enabled) {
		/* Restore saved alarm if it's still in the future. */
		if (cros_ec_rtc->saved_alarm < current_time)
			alarm_offset = EC_RTC_ALARM_CLEAR;
		else
			alarm_offset = cros_ec_rtc->saved_alarm - current_time;

		ret = cros_ec_rtc_set(cros_ec, EC_CMD_RTC_SET_ALARM,
				      alarm_offset);
		if (ret < 0) {
			dev_err(dev, "error restoring alarm: %d\n", ret);
			return ret;
		}
	} else {
		/* Disable alarm, saving the old alarm value. */
		ret = cros_ec_rtc_get(cros_ec, EC_CMD_RTC_GET_ALARM,
				      &alarm_offset);
		if (ret < 0) {
			dev_err(dev, "error saving alarm: %d\n", ret);
			return ret;
		}

		alarm_value = current_time + alarm_offset;

		/*
		 * If the current EC alarm is already past, we don't want
		 * to set an alarm when we go through the alarm irq enable
		 * path.
		 */
		if (alarm_value < current_time)
			cros_ec_rtc->saved_alarm = EC_RTC_ALARM_CLEAR;
		else
			cros_ec_rtc->saved_alarm = alarm_value;

		alarm_offset = EC_RTC_ALARM_CLEAR;
		ret = cros_ec_rtc_set(cros_ec, EC_CMD_RTC_SET_ALARM,
				      alarm_offset);
		if (ret < 0) {
			dev_err(dev, "error disabling alarm: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int cros_ec_rtc_event(struct notifier_block *nb,
			     unsigned long queued_during_suspend,
			     void *_notify)
{
	struct cros_ec_rtc *cros_ec_rtc;
	struct rtc_device *rtc;
	struct cros_ec_device *cros_ec;
	u32 host_event;

	cros_ec_rtc = container_of(nb, struct cros_ec_rtc, notifier);
	rtc = cros_ec_rtc->rtc;
	cros_ec = cros_ec_rtc->cros_ec;

	host_event = cros_ec_get_host_event(cros_ec);
	if (host_event & EC_HOST_EVENT_MASK(EC_HOST_EVENT_RTC)) {
		rtc_update_irq(rtc, 1, RTC_IRQF | RTC_AF);
		return NOTIFY_OK;
	} else {
		return NOTIFY_DONE;
	}
}

static const struct rtc_class_ops cros_ec_rtc_ops = {
	.read_time = cros_ec_rtc_read_time,
	.set_time = cros_ec_rtc_set_time,
	.read_alarm = cros_ec_rtc_read_alarm,
	.set_alarm = cros_ec_rtc_set_alarm,
	.alarm_irq_enable = cros_ec_rtc_alarm_irq_enable,
};

#ifdef CONFIG_PM_SLEEP
static int cros_ec_rtc_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct cros_ec_rtc *cros_ec_rtc = dev_get_drvdata(&pdev->dev);

	if (device_may_wakeup(dev))
		return enable_irq_wake(cros_ec_rtc->cros_ec->irq);

	return 0;
}

static int cros_ec_rtc_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct cros_ec_rtc *cros_ec_rtc = dev_get_drvdata(&pdev->dev);

	if (device_may_wakeup(dev))
		return disable_irq_wake(cros_ec_rtc->cros_ec->irq);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(cros_ec_rtc_pm_ops, cros_ec_rtc_suspend,
			 cros_ec_rtc_resume);

static int cros_ec_rtc_probe(struct platform_device *pdev)
{
	struct cros_ec_dev *ec_dev = dev_get_drvdata(pdev->dev.parent);
	struct cros_ec_device *cros_ec = ec_dev->ec_dev;
	struct cros_ec_rtc *cros_ec_rtc;
	struct rtc_time tm;
	int ret;

	cros_ec_rtc = devm_kzalloc(&pdev->dev, sizeof(*cros_ec_rtc),
				   GFP_KERNEL);
	if (!cros_ec_rtc)
		return -ENOMEM;

	platform_set_drvdata(pdev, cros_ec_rtc);
	cros_ec_rtc->cros_ec = cros_ec;

	/* Get initial time */
	ret = cros_ec_rtc_read_time(&pdev->dev, &tm);
	if (ret) {
		dev_err(&pdev->dev, "failed to read RTC time\n");
		return ret;
	}

	ret = device_init_wakeup(&pdev->dev, true);
	if (ret) {
		dev_err(&pdev->dev, "failed to initialize wakeup\n");
		return ret;
	}

	cros_ec_rtc->rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(cros_ec_rtc->rtc))
		return PTR_ERR(cros_ec_rtc->rtc);

	cros_ec_rtc->rtc->ops = &cros_ec_rtc_ops;
	cros_ec_rtc->rtc->range_max = U32_MAX;

	/*
	 * The RTC on some older Chromebooks can only handle alarms less than
	 * 24 hours in the future. The only way to find out is to try to set an
	 * alarm further in the future. If that fails, assume that the RTC
	 * connected to the EC can only handle less than 24 hours of alarm
	 * window.
	 */
	ret = cros_ec_rtc_set(cros_ec, EC_CMD_RTC_SET_ALARM, SECS_PER_DAY * 2);
	if (ret == -EINVAL)
		cros_ec_rtc->rtc->alarm_offset_max = SECS_PER_DAY - 1;

	(void)cros_ec_rtc_set(cros_ec, EC_CMD_RTC_SET_ALARM,
			      EC_RTC_ALARM_CLEAR);

	ret = devm_rtc_register_device(cros_ec_rtc->rtc);
	if (ret)
		return ret;

	/* Get RTC events from the EC. */
	cros_ec_rtc->notifier.notifier_call = cros_ec_rtc_event;
	ret = blocking_notifier_chain_register(&cros_ec->event_notifier,
					       &cros_ec_rtc->notifier);
	if (ret) {
		dev_err(&pdev->dev, "failed to register notifier\n");
		return ret;
	}

	return 0;
}

static void cros_ec_rtc_remove(struct platform_device *pdev)
{
	struct cros_ec_rtc *cros_ec_rtc = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	int ret;

	ret = blocking_notifier_chain_unregister(
				&cros_ec_rtc->cros_ec->event_notifier,
				&cros_ec_rtc->notifier);
	if (ret)
		dev_err(dev, "failed to unregister notifier\n");
}

static const struct platform_device_id cros_ec_rtc_id[] = {
	{ DRV_NAME, 0 },
	{}
};
MODULE_DEVICE_TABLE(platform, cros_ec_rtc_id);

static struct platform_driver cros_ec_rtc_driver = {
	.probe = cros_ec_rtc_probe,
	.remove = cros_ec_rtc_remove,
	.driver = {
		.name = DRV_NAME,
		.pm = &cros_ec_rtc_pm_ops,
	},
	.id_table = cros_ec_rtc_id,
};

module_platform_driver(cros_ec_rtc_driver);

MODULE_DESCRIPTION("RTC driver for Chrome OS ECs");
MODULE_AUTHOR("Stephen Barber <smbarber@chromium.org>");
MODULE_LICENSE("GPL v2");
