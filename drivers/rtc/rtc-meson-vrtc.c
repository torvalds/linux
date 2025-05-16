// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/time64.h>

struct meson_vrtc_data {
	void __iomem *io_alarm;
	unsigned long alarm_time;
	bool enabled;
};

static int meson_vrtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct timespec64 time;

	dev_dbg(dev, "%s\n", __func__);
	ktime_get_real_ts64(&time);
	rtc_time64_to_tm(time.tv_sec, tm);

	return 0;
}

static void meson_vrtc_set_wakeup_time(struct meson_vrtc_data *vrtc,
				       unsigned long time)
{
	writel_relaxed(time, vrtc->io_alarm);
}

static int meson_vrtc_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct meson_vrtc_data *vrtc = dev_get_drvdata(dev);

	dev_dbg(dev, "%s: alarm->enabled=%d\n", __func__, alarm->enabled);
	if (alarm->enabled)
		vrtc->alarm_time = rtc_tm_to_time64(&alarm->time);
	else
		vrtc->alarm_time = 0;

	return 0;
}

static int meson_vrtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct meson_vrtc_data *vrtc = dev_get_drvdata(dev);

	vrtc->enabled = enabled;
	return 0;
}

static const struct rtc_class_ops meson_vrtc_ops = {
	.read_time = meson_vrtc_read_time,
	.set_alarm = meson_vrtc_set_alarm,
	.alarm_irq_enable = meson_vrtc_alarm_irq_enable,
};

static int meson_vrtc_probe(struct platform_device *pdev)
{
	struct meson_vrtc_data *vrtc;
	struct rtc_device *rtc;

	vrtc = devm_kzalloc(&pdev->dev, sizeof(*vrtc), GFP_KERNEL);
	if (!vrtc)
		return -ENOMEM;

	vrtc->io_alarm = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(vrtc->io_alarm))
		return PTR_ERR(vrtc->io_alarm);

	device_init_wakeup(&pdev->dev, true);

	platform_set_drvdata(pdev, vrtc);

	rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	rtc->ops = &meson_vrtc_ops;
	return devm_rtc_register_device(rtc);
}

static int __maybe_unused meson_vrtc_suspend(struct device *dev)
{
	struct meson_vrtc_data *vrtc = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);
	if (vrtc->alarm_time) {
		unsigned long local_time;
		long alarm_secs;
		struct timespec64 time;

		ktime_get_real_ts64(&time);
		local_time = time.tv_sec;

		dev_dbg(dev, "alarm_time = %lus, local_time=%lus\n",
			vrtc->alarm_time, local_time);
		alarm_secs = vrtc->alarm_time - local_time;
		if (alarm_secs > 0) {
			meson_vrtc_set_wakeup_time(vrtc, alarm_secs);
			dev_dbg(dev, "system will wakeup in %lds.\n",
				alarm_secs);
		} else {
			dev_err(dev, "alarm time already passed: %lds.\n",
				alarm_secs);
		}
	}

	return 0;
}

static int __maybe_unused meson_vrtc_resume(struct device *dev)
{
	struct meson_vrtc_data *vrtc = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);

	vrtc->alarm_time = 0;
	meson_vrtc_set_wakeup_time(vrtc, 0);
	return 0;
}

static SIMPLE_DEV_PM_OPS(meson_vrtc_pm_ops,
			 meson_vrtc_suspend, meson_vrtc_resume);

static const struct of_device_id meson_vrtc_dt_match[] = {
	{ .compatible = "amlogic,meson-vrtc"},
	{},
};
MODULE_DEVICE_TABLE(of, meson_vrtc_dt_match);

static struct platform_driver meson_vrtc_driver = {
	.probe = meson_vrtc_probe,
	.driver = {
		.name = "meson-vrtc",
		.of_match_table = meson_vrtc_dt_match,
		.pm = &meson_vrtc_pm_ops,
	},
};

module_platform_driver(meson_vrtc_driver);

MODULE_DESCRIPTION("Amlogic Virtual Wakeup RTC Timer driver");
MODULE_LICENSE("GPL");
