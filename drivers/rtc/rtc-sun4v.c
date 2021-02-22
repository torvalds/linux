// SPDX-License-Identifier: GPL-2.0
/* rtc-sun4v.c: Hypervisor based RTC for SUN4V systems.
 *
 * Author: David S. Miller
 *
 * Copyright (C) 2008 David S. Miller <davem@davemloft.net>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/rtc.h>
#include <linux/platform_device.h>

#include <asm/hypervisor.h>

static unsigned long hypervisor_get_time(void)
{
	unsigned long ret, time;
	int retries = 10000;

retry:
	ret = sun4v_tod_get(&time);
	if (ret == HV_EOK)
		return time;
	if (ret == HV_EWOULDBLOCK) {
		if (--retries > 0) {
			udelay(100);
			goto retry;
		}
		pr_warn("tod_get() timed out.\n");
		return 0;
	}
	pr_warn("tod_get() not supported.\n");
	return 0;
}

static int sun4v_read_time(struct device *dev, struct rtc_time *tm)
{
	rtc_time64_to_tm(hypervisor_get_time(), tm);
	return 0;
}

static int hypervisor_set_time(unsigned long secs)
{
	unsigned long ret;
	int retries = 10000;

retry:
	ret = sun4v_tod_set(secs);
	if (ret == HV_EOK)
		return 0;
	if (ret == HV_EWOULDBLOCK) {
		if (--retries > 0) {
			udelay(100);
			goto retry;
		}
		pr_warn("tod_set() timed out.\n");
		return -EAGAIN;
	}
	pr_warn("tod_set() not supported.\n");
	return -EOPNOTSUPP;
}

static int sun4v_set_time(struct device *dev, struct rtc_time *tm)
{
	return hypervisor_set_time(rtc_tm_to_time64(tm));
}

static const struct rtc_class_ops sun4v_rtc_ops = {
	.read_time	= sun4v_read_time,
	.set_time	= sun4v_set_time,
};

static int __init sun4v_rtc_probe(struct platform_device *pdev)
{
	struct rtc_device *rtc;

	rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	rtc->ops = &sun4v_rtc_ops;
	rtc->range_max = U64_MAX;
	platform_set_drvdata(pdev, rtc);

	return devm_rtc_register_device(rtc);
}

static struct platform_driver sun4v_rtc_driver = {
	.driver		= {
		.name	= "rtc-sun4v",
	},
};

builtin_platform_driver_probe(sun4v_rtc_driver, sun4v_rtc_probe);
