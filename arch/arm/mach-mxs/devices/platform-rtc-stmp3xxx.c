/*
 * Copyright (C) 2011 Pengutronix, Wolfram Sang <w.sang@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <asm/sizes.h>
#include <mach/mx23.h>
#include <mach/mx28.h>
#include <mach/devices-common.h>

#ifdef CONFIG_SOC_IMX23
struct platform_device *__init mx23_add_rtc_stmp3xxx(void)
{
	struct resource res[] = {
		{
			.start = MX23_RTC_BASE_ADDR,
			.end = MX23_RTC_BASE_ADDR + SZ_8K - 1,
			.flags = IORESOURCE_MEM,
		}, {
			.start = MX23_INT_RTC_ALARM,
			.end = MX23_INT_RTC_ALARM,
			.flags = IORESOURCE_IRQ,
		},
	};

	return mxs_add_platform_device("stmp3xxx-rtc", 0, res, ARRAY_SIZE(res),
					NULL, 0);
}
#endif /* CONFIG_SOC_IMX23 */

#ifdef CONFIG_SOC_IMX28
struct platform_device *__init mx28_add_rtc_stmp3xxx(void)
{
	struct resource res[] = {
		{
			.start = MX28_RTC_BASE_ADDR,
			.end = MX28_RTC_BASE_ADDR + SZ_8K - 1,
			.flags = IORESOURCE_MEM,
		}, {
			.start = MX28_INT_RTC_ALARM,
			.end = MX28_INT_RTC_ALARM,
			.flags = IORESOURCE_IRQ,
		},
	};

	return mxs_add_platform_device("stmp3xxx-rtc", 0, res, ARRAY_SIZE(res),
					NULL, 0);
}
#endif /* CONFIG_SOC_IMX28 */
