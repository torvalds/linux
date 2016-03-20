/* rtc-generic: RTC driver using the generic RTC abstraction
 *
 * Copyright (C) 2008 Kyle McMartin <kyle@mcmartin.ca>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

#if defined(CONFIG_M68K) || defined(CONFIG_PARISC) || \
    defined(CONFIG_PPC) || defined(CONFIG_SUPERH32)
#include <asm/rtc.h>

static int generic_get_time(struct device *dev, struct rtc_time *tm)
{
	unsigned int ret = get_rtc_time(tm);

	if (ret & RTC_BATT_BAD)
		return -EOPNOTSUPP;

	return rtc_valid_tm(tm);
}

static int generic_set_time(struct device *dev, struct rtc_time *tm)
{
	if (set_rtc_time(tm) < 0)
		return -EOPNOTSUPP;

	return 0;
}

static const struct rtc_class_ops generic_rtc_ops = {
	.read_time = generic_get_time,
	.set_time = generic_set_time,
};
#else
#define generic_rtc_ops *(struct rtc_class_ops*)NULL
#endif

static int __init generic_rtc_probe(struct platform_device *dev)
{
	struct rtc_device *rtc;
	const struct rtc_class_ops *ops;

	ops = dev_get_platdata(&dev->dev);
	if (!ops)
		ops = &generic_rtc_ops;

	rtc = devm_rtc_device_register(&dev->dev, "rtc-generic",
					ops, THIS_MODULE);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	platform_set_drvdata(dev, rtc);

	return 0;
}

static struct platform_driver generic_rtc_driver = {
	.driver = {
		.name = "rtc-generic",
	},
};

module_platform_driver_probe(generic_rtc_driver, generic_rtc_probe);

MODULE_AUTHOR("Kyle McMartin <kyle@mcmartin.ca>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Generic RTC driver");
MODULE_ALIAS("platform:rtc-generic");
