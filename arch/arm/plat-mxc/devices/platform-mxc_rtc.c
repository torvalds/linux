/*
 * Copyright (C) 2010-2011 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <mach/hardware.h>
#include <mach/devices-common.h>

#define imx_mxc_rtc_data_entry_single(soc)				\
	{								\
		.iobase = soc ## _RTC_BASE_ADDR,			\
		.irq = soc ## _INT_RTC,					\
	}

#ifdef CONFIG_SOC_IMX31
const struct imx_mxc_rtc_data imx31_mxc_rtc_data __initconst =
	imx_mxc_rtc_data_entry_single(MX31);
#endif /* ifdef CONFIG_SOC_IMX31 */

#ifdef CONFIG_SOC_IMX35
const struct imx_mxc_rtc_data imx35_mxc_rtc_data __initconst =
	imx_mxc_rtc_data_entry_single(MX35);
#endif /* ifdef CONFIG_SOC_IMX35 */

struct platform_device *__init imx_add_mxc_rtc(
		const struct imx_mxc_rtc_data *data)
{
	struct resource res[] = {
		{
			.start = data->iobase,
			.end = data->iobase + SZ_16K - 1,
			.flags = IORESOURCE_MEM,
		}, {
			.start = data->irq,
			.end = data->irq,
			.flags = IORESOURCE_IRQ,
		},
	};

	return imx_add_platform_device("mxc_rtc", -1,
			res, ARRAY_SIZE(res), NULL, 0);
}
