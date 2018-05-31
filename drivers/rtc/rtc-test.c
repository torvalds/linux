/*
 * An RTC test device/driver
 * Copyright (C) 2005 Tower Technologies
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/rtc.h>
#include <linux/platform_device.h>

#define MAX_RTC_TEST 3

struct rtc_test_data {
	struct rtc_device *rtc;
	time64_t offset;
};

struct platform_device *pdev[MAX_RTC_TEST];

static int test_rtc_read_alarm(struct device *dev,
	struct rtc_wkalrm *alrm)
{
	return 0;
}

static int test_rtc_set_alarm(struct device *dev,
	struct rtc_wkalrm *alrm)
{
	return 0;
}

static int test_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct rtc_test_data *rtd = dev_get_drvdata(dev);

	rtc_time64_to_tm(ktime_get_real_seconds() + rtd->offset, tm);

	return 0;
}

static int test_rtc_set_mmss64(struct device *dev, time64_t secs)
{
	struct rtc_test_data *rtd = dev_get_drvdata(dev);

	rtd->offset = secs - ktime_get_real_seconds();

	return 0;
}

static int test_rtc_alarm_irq_enable(struct device *dev, unsigned int enable)
{
	return 0;
}

static const struct rtc_class_ops test_rtc_ops = {
	.read_time = test_rtc_read_time,
	.read_alarm = test_rtc_read_alarm,
	.set_alarm = test_rtc_set_alarm,
	.set_mmss64 = test_rtc_set_mmss64,
	.alarm_irq_enable = test_rtc_alarm_irq_enable,
};

static ssize_t test_irq_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", 42);
}
static ssize_t test_irq_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int retval;
	struct rtc_device *rtc = dev_get_drvdata(dev);

	retval = count;
	if (strncmp(buf, "tick", 4) == 0 && rtc->pie_enabled)
		rtc_update_irq(rtc, 1, RTC_PF | RTC_IRQF);
	else if (strncmp(buf, "alarm", 5) == 0) {
		struct rtc_wkalrm alrm;
		int err = rtc_read_alarm(rtc, &alrm);

		if (!err && alrm.enabled)
			rtc_update_irq(rtc, 1, RTC_AF | RTC_IRQF);

	} else if (strncmp(buf, "update", 6) == 0 && rtc->uie_rtctimer.enabled)
		rtc_update_irq(rtc, 1, RTC_UF | RTC_IRQF);
	else
		retval = -EINVAL;

	return retval;
}
static DEVICE_ATTR(irq, S_IRUGO | S_IWUSR, test_irq_show, test_irq_store);

static int test_probe(struct platform_device *plat_dev)
{
	struct rtc_test_data *rtd;

	rtd = devm_kzalloc(&plat_dev->dev, sizeof(*rtd), GFP_KERNEL);
	if (!rtd)
		return -ENOMEM;

	platform_set_drvdata(plat_dev, rtd);

	rtd->rtc = devm_rtc_device_register(&plat_dev->dev, "test",
					    &test_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtd->rtc))
		return PTR_ERR(rtd->rtc);

	return 0;
}

static int test_remove(struct platform_device *plat_dev)
{
	device_remove_file(&plat_dev->dev, &dev_attr_irq);

	return 0;
}

static struct platform_driver test_driver = {
	.probe	= test_probe,
	.remove = test_remove,
	.driver = {
		.name = "rtc-test",
	},
};

static int __init test_init(void)
{
	int i, err;

	if ((err = platform_driver_register(&test_driver)))
		return err;

	err = -ENOMEM;
	for (i = 0; i < MAX_RTC_TEST; i++) {
		pdev[i] = platform_device_alloc("rtc-test", i);
		if (!pdev[i])
			goto exit_free_mem;
	}

	for (i = 0; i < MAX_RTC_TEST; i++) {
		err = platform_device_add(pdev[i]);
		if (err)
			goto exit_device_del;
	}

	return 0;

exit_device_del:
	for (; i > 0; i--)
		platform_device_del(pdev[i - 1]);

exit_free_mem:
	for (i = 0; i < MAX_RTC_TEST; i++)
		platform_device_put(pdev[i]);

	platform_driver_unregister(&test_driver);
	return err;
}

static void __exit test_exit(void)
{
	int i;

	for (i = 0; i < MAX_RTC_TEST; i++)
		platform_device_unregister(pdev[i]);

	platform_driver_unregister(&test_driver);
}

MODULE_AUTHOR("Alessandro Zummo <a.zummo@towertech.it>");
MODULE_DESCRIPTION("RTC test driver/device");
MODULE_LICENSE("GPL");

module_init(test_init);
module_exit(test_exit);
