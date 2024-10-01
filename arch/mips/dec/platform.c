// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	DEC platform devices.
 *
 *	Copyright (c) 2014  Maciej W. Rozycki
 */

#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/mc146818rtc.h>
#include <linux/platform_device.h>

static struct resource dec_rtc_resources[] = {
	{
		.name = "rtc",
		.flags = IORESOURCE_MEM,
	},
};

static struct cmos_rtc_board_info dec_rtc_info = {
	.flags = CMOS_RTC_FLAGS_NOFREQ,
	.address_space = 64,
};

static struct platform_device dec_rtc_device = {
	.name = "rtc_cmos",
	.id = PLATFORM_DEVID_NONE,
	.dev.platform_data = &dec_rtc_info,
	.resource = dec_rtc_resources,
	.num_resources = ARRAY_SIZE(dec_rtc_resources),
};

static int __init dec_add_devices(void)
{
	dec_rtc_resources[0].start = RTC_PORT(0);
	dec_rtc_resources[0].end = RTC_PORT(0) + dec_kn_slot_size - 1;
	return platform_device_register(&dec_rtc_device);
}

device_initcall(dec_add_devices);
