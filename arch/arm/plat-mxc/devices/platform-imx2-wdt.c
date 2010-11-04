/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <asm/sizes.h>
#include <mach/hardware.h>
#include <mach/devices-common.h>

#define imx_imx2_wdt_data_entry_single(soc)				\
	{								\
		.iobase = soc ## _WDOG_BASE_ADDR,			\
	}

#ifdef CONFIG_SOC_IMX21
const struct imx_imx2_wdt_data imx21_imx2_wdt_data __initconst =
	imx_imx2_wdt_data_entry_single(MX21);
#endif /* ifdef CONFIG_SOC_IMX21 */

#ifdef CONFIG_SOC_IMX27
const struct imx_imx2_wdt_data imx27_imx2_wdt_data __initconst =
	imx_imx2_wdt_data_entry_single(MX27);
#endif /* ifdef CONFIG_SOC_IMX27 */

struct platform_device *__init imx_add_imx2_wdt(
		const struct imx_imx2_wdt_data *data)
{
	struct resource res[] = {
		{
			.start = data->iobase,
			.end = data->iobase + SZ_4K - 1,
			.flags = IORESOURCE_MEM,
		},
	};
	return imx_add_platform_device("imx2-wdt", 0,
			res, ARRAY_SIZE(res), NULL, 0);
}
