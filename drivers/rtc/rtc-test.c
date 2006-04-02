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

static struct platform_device *test0 = NULL, *test1 = NULL;

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

static int test_rtc_read_time(struct device *dev,
	struct rtc_time *tm)
{
	rtc_time_to_tm(get_seconds(), tm);
	return 0;
}

static int test_rtc_set_time(struct device *dev,
	struct rtc_time *tm)
{
	return 0;
}

static int test_rtc_set_mmss(struct device *dev, unsigned long secs)
{
	return 0;
}

static int test_rtc_proc(struct device *dev, struct seq_file *seq)
{
	struct platform_device *plat_dev = to_platform_device(dev);

	seq_printf(seq, "24hr\t\t: yes\n");
	seq_printf(seq, "test\t\t: yes\n");
	seq_printf(seq, "id\t\t: %d\n", plat_dev->id);

	return 0;
}

static int test_rtc_ioctl(struct device *dev, unsigned int cmd,
	unsigned long arg)
{
	/* We do support interrupts, they're generated
	 * using the sysfs interface.
	 */
	switch (cmd) {
	case RTC_PIE_ON:
	case RTC_PIE_OFF:
	case RTC_UIE_ON:
	case RTC_UIE_OFF:
	case RTC_AIE_ON:
	case RTC_AIE_OFF:
		return 0;

	default:
		return -EINVAL;
	}
}

static struct rtc_class_ops test_rtc_ops = {
	.proc = test_rtc_proc,
	.read_time = test_rtc_read_time,
	.set_time = test_rtc_set_time,
	.read_alarm = test_rtc_read_alarm,
	.set_alarm = test_rtc_set_alarm,
	.set_mmss = test_rtc_set_mmss,
	.ioctl = test_rtc_ioctl,
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
	struct platform_device *plat_dev = to_platform_device(dev);
	struct rtc_device *rtc = platform_get_drvdata(plat_dev);

	retval = count;
	if (strncmp(buf, "tick", 4) == 0)
		rtc_update_irq(&rtc->class_dev, 1, RTC_PF | RTC_IRQF);
	else if (strncmp(buf, "alarm", 5) == 0)
		rtc_update_irq(&rtc->class_dev, 1, RTC_AF | RTC_IRQF);
	else if (strncmp(buf, "update", 6) == 0)
		rtc_update_irq(&rtc->class_dev, 1, RTC_UF | RTC_IRQF);
	else
		retval = -EINVAL;

	return retval;
}
static DEVICE_ATTR(irq, S_IRUGO | S_IWUSR, test_irq_show, test_irq_store);

static int test_probe(struct platform_device *plat_dev)
{
	int err;
	struct rtc_device *rtc = rtc_device_register("test", &plat_dev->dev,
						&test_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc)) {
		err = PTR_ERR(rtc);
		dev_err(&plat_dev->dev,
			"unable to register the class device\n");
		return err;
	}
	device_create_file(&plat_dev->dev, &dev_attr_irq);

	platform_set_drvdata(plat_dev, rtc);

	return 0;
}

static int __devexit test_remove(struct platform_device *plat_dev)
{
	struct rtc_device *rtc = platform_get_drvdata(plat_dev);

	rtc_device_unregister(rtc);
	device_remove_file(&plat_dev->dev, &dev_attr_irq);

	return 0;
}

static struct platform_driver test_drv = {
	.probe	= test_probe,
	.remove = __devexit_p(test_remove),
	.driver = {
		.name = "rtc-test",
		.owner = THIS_MODULE,
	},
};

static int __init test_init(void)
{
	int err;

	if ((err = platform_driver_register(&test_drv)))
		return err;

	if ((test0 = platform_device_alloc("rtc-test", 0)) == NULL) {
		err = -ENOMEM;
		goto exit_driver_unregister;
	}

	if ((test1 = platform_device_alloc("rtc-test", 1)) == NULL) {
		err = -ENOMEM;
		goto exit_free_test0;
	}

	if ((err = platform_device_add(test0)))
		goto exit_free_test1;

	if ((err = platform_device_add(test1)))
		goto exit_device_unregister;

	return 0;

exit_device_unregister:
	platform_device_unregister(test0);

exit_free_test1:
	platform_device_put(test1);

exit_free_test0:
	platform_device_put(test0);

exit_driver_unregister:
	platform_driver_unregister(&test_drv);
	return err;
}

static void __exit test_exit(void)
{
	platform_device_unregister(test0);
	platform_device_unregister(test1);
	platform_driver_unregister(&test_drv);
}

MODULE_AUTHOR("Alessandro Zummo <a.zummo@towertech.it>");
MODULE_DESCRIPTION("RTC test driver/device");
MODULE_LICENSE("GPL");

module_init(test_init);
module_exit(test_exit);
