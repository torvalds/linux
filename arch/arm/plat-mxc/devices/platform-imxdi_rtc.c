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

#define imx_imxdi_rtc_data_entry_single(soc)				\
	{								\
		.iobase = soc ## _DRYICE_BASE_ADDR,			\
		.irq = soc ## _INT_DRYICE,				\
	}

#ifdef CONFIG_SOC_IMX25
const struct imx_imxdi_rtc_data imx25_imxdi_rtc_data __initconst =
	imx_imxdi_rtc_data_entry_single(MX25);
#endif /* ifdef CONFIG_SOC_IMX25 */

struct platform_device *__init imx_add_imxdi_rtc(
		const struct imx_imxdi_rtc_data *data)
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

	return imx_add_platform_device("imxdi_rtc", 0,
			res, ARRAY_SIZE(res), NULL, 0);
}
