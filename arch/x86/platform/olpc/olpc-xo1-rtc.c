// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Support for OLPC XO-1 Real Time Clock (RTC)
 *
 * Copyright (C) 2011 One Laptop per Child
 */

#include <linux/mc146818rtc.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/of.h>

#include <asm/msr.h>
#include <asm/olpc.h>
#include <asm/x86_init.h>

static void rtc_wake_on(struct device *dev)
{
	olpc_xo1_pm_wakeup_set(CS5536_PM_RTC);
}

static void rtc_wake_off(struct device *dev)
{
	olpc_xo1_pm_wakeup_clear(CS5536_PM_RTC);
}

static struct resource rtc_platform_resource[] = {
	[0] = {
		.start	= RTC_PORT(0),
		.end	= RTC_PORT(1),
		.flags	= IORESOURCE_IO,
	},
	[1] = {
		.start	= RTC_IRQ,
		.end	= RTC_IRQ,
		.flags	= IORESOURCE_IRQ,
	}
};

static struct cmos_rtc_board_info rtc_info = {
	.rtc_day_alarm = 0,
	.rtc_mon_alarm = 0,
	.rtc_century = 0,
	.wake_on = rtc_wake_on,
	.wake_off = rtc_wake_off,
};

static struct platform_device xo1_rtc_device = {
	.name = "rtc_cmos",
	.id = -1,
	.num_resources = ARRAY_SIZE(rtc_platform_resource),
	.dev.platform_data = &rtc_info,
	.resource = rtc_platform_resource,
};

static int __init xo1_rtc_init(void)
{
	int r;
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, "olpc,xo1-rtc");
	if (!node)
		return 0;
	of_node_put(node);

	pr_info("olpc-xo1-rtc: Initializing OLPC XO-1 RTC\n");
	rdmsrl(MSR_RTC_DOMA_OFFSET, rtc_info.rtc_day_alarm);
	rdmsrl(MSR_RTC_MONA_OFFSET, rtc_info.rtc_mon_alarm);
	rdmsrl(MSR_RTC_CEN_OFFSET, rtc_info.rtc_century);

	r = platform_device_register(&xo1_rtc_device);
	if (r)
		return r;

	x86_platform.legacy.rtc = 0;

	device_init_wakeup(&xo1_rtc_device.dev, 1);
	return 0;
}
arch_initcall(xo1_rtc_init);
