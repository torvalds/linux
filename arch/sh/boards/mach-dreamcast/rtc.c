// SPDX-License-Identifier: GPL-2.0
/*
 * arch/sh/boards/dreamcast/rtc.c
 *
 * Dreamcast AICA RTC routines.
 *
 * Copyright (c) 2001, 2002 M. R. Brown <mrbrown@0xd6.org>
 * Copyright (c) 2002 Paul Mundt <lethal@chaoticdreams.org>
 */

#include <linux/time.h>
#include <linux/rtc.h>
#include <linux/io.h>
#include <linux/platform_device.h>

/* The AICA RTC has an Epoch of 1/1/1950, so we must subtract 20 years (in
   seconds) to get the standard Unix Epoch when getting the time, and add
   20 years when setting the time. */
#define TWENTY_YEARS ((20 * 365LU + 5) * 86400)

/* The AICA RTC is represented by a 32-bit seconds counter stored in 2 16-bit
   registers.*/
#define AICA_RTC_SECS_H		0xa0710000
#define AICA_RTC_SECS_L		0xa0710004

/**
 * aica_rtc_gettimeofday - Get the time from the AICA RTC
 * @dev: the RTC device (ignored)
 * @tm: pointer to resulting RTC time structure
 *
 * Grabs the current RTC seconds counter and adjusts it to the Unix Epoch.
 */
static int aica_rtc_gettimeofday(struct device *dev, struct rtc_time *tm)
{
	unsigned long val1, val2;
	time64_t t;

	do {
		val1 = ((__raw_readl(AICA_RTC_SECS_H) & 0xffff) << 16) |
			(__raw_readl(AICA_RTC_SECS_L) & 0xffff);

		val2 = ((__raw_readl(AICA_RTC_SECS_H) & 0xffff) << 16) |
			(__raw_readl(AICA_RTC_SECS_L) & 0xffff);
	} while (val1 != val2);

	/* normalize to 1970..2106 time range */
	t = (u32)(val1 - TWENTY_YEARS);

	rtc_time64_to_tm(t, tm);

	return 0;
}

/**
 * aica_rtc_settimeofday - Set the AICA RTC to the current time
 * @dev: the RTC device (ignored)
 * @tm: pointer to new RTC time structure
 *
 * Adjusts the given @tv to the AICA Epoch and sets the RTC seconds counter.
 */
static int aica_rtc_settimeofday(struct device *dev, struct rtc_time *tm)
{
	unsigned long val1, val2;
	time64_t secs = rtc_tm_to_time64(tm);
	u32 adj = secs + TWENTY_YEARS;

	do {
		__raw_writel((adj & 0xffff0000) >> 16, AICA_RTC_SECS_H);
		__raw_writel((adj & 0xffff), AICA_RTC_SECS_L);

		val1 = ((__raw_readl(AICA_RTC_SECS_H) & 0xffff) << 16) |
			(__raw_readl(AICA_RTC_SECS_L) & 0xffff);

		val2 = ((__raw_readl(AICA_RTC_SECS_H) & 0xffff) << 16) |
			(__raw_readl(AICA_RTC_SECS_L) & 0xffff);
	} while (val1 != val2);

	return 0;
}

static const struct rtc_class_ops rtc_generic_ops = {
	.read_time = aica_rtc_gettimeofday,
	.set_time = aica_rtc_settimeofday,
};

static int __init aica_time_init(void)
{
	struct platform_device *pdev;

	pdev = platform_device_register_data(NULL, "rtc-generic", -1,
					     &rtc_generic_ops,
					     sizeof(rtc_generic_ops));

	return PTR_ERR_OR_ZERO(pdev);
}
arch_initcall(aica_time_init);
