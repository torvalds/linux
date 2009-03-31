/* rtc-parisc: RTC for HP PA-RISC firmware
 *
 * Copyright (C) 2008 Kyle McMartin <kyle@mcmartin.ca>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

#include <asm/rtc.h>

static int parisc_get_time(struct device *dev, struct rtc_time *tm)
{
	unsigned long ret;

	ret = get_rtc_time(tm);

	if (ret & RTC_BATT_BAD)
		return -EOPNOTSUPP;

	return rtc_valid_tm(tm);
}

static int parisc_set_time(struct device *dev, struct rtc_time *tm)
{
	int ret;

	ret = set_rtc_time(tm);

	if (ret < 0)
		return -EOPNOTSUPP;

	return 0;
}

static const struct rtc_class_ops parisc_rtc_ops = {
	.read_time = parisc_get_time,
	.set_time = parisc_set_time,
};

static int __init parisc_rtc_probe(struct platform_device *dev)
{
	struct rtc_device *p;

	p = rtc_device_register("rtc-parisc", &dev->dev, &parisc_rtc_ops,
				THIS_MODULE);
	if (IS_ERR(p)) {
		int err = PTR_ERR(p);
		return err;
	}

	platform_set_drvdata(dev, p);

	return 0;
}

static int __exit parisc_rtc_remove(struct platform_device *dev)
{
	struct rtc_device *p = platform_get_drvdata(dev);

	rtc_device_unregister(p);

	return 0;
}

static struct platform_driver parisc_rtc_driver = {
	.driver = {
		.name = "rtc-parisc",
		.owner = THIS_MODULE,
	},
	.probe = parisc_rtc_probe,
	.remove = __devexit_p(parisc_rtc_remove),
};

static int __init parisc_rtc_init(void)
{
	return platform_driver_probe(&parisc_rtc_driver, parisc_rtc_probe);
}

static void __exit parisc_rtc_fini(void)
{
	platform_driver_unregister(&parisc_rtc_driver);
}

module_init(parisc_rtc_init);
module_exit(parisc_rtc_fini);

MODULE_AUTHOR("Kyle McMartin <kyle@mcmartin.ca>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HP PA-RISC RTC driver");
