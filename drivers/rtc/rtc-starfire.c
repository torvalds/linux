/* rtc-starfire.c: Starfire platform RTC driver.
 *
 * Author: David S. Miller
 * License: GPL
 *
 * Copyright (C) 2008 David S. Miller <davem@davemloft.net>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/rtc.h>
#include <linux/platform_device.h>

#include <asm/oplib.h>

static u32 starfire_get_time(void)
{
	static char obp_gettod[32];
	static u32 unix_tod;

	sprintf(obp_gettod, "h# %08x unix-gettod",
		(unsigned int) (long) &unix_tod);
	prom_feval(obp_gettod);

	return unix_tod;
}

static int starfire_read_time(struct device *dev, struct rtc_time *tm)
{
	rtc_time64_to_tm(starfire_get_time(), tm);
	return 0;
}

static const struct rtc_class_ops starfire_rtc_ops = {
	.read_time	= starfire_read_time,
};

static int __init starfire_rtc_probe(struct platform_device *pdev)
{
	struct rtc_device *rtc;

	rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	rtc->ops = &starfire_rtc_ops;
	rtc->range_max = U32_MAX;

	platform_set_drvdata(pdev, rtc);

	return rtc_register_device(rtc);
}

static struct platform_driver starfire_rtc_driver = {
	.driver		= {
		.name	= "rtc-starfire",
	},
};

builtin_platform_driver_probe(starfire_rtc_driver, starfire_rtc_probe);
