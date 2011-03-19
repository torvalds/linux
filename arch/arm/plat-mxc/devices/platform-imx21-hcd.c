/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <mach/hardware.h>
#include <mach/devices-common.h>

#define imx_imx21_hcd_data_entry_single(soc)				\
	{								\
		.iobase = soc ## _USBOTG_BASE_ADDR,			\
		.irq = soc ## _INT_USBHOST,				\
	}

#ifdef CONFIG_SOC_IMX21
const struct imx_imx21_hcd_data imx21_imx21_hcd_data __initconst =
	imx_imx21_hcd_data_entry_single(MX21);
#endif /* ifdef CONFIG_SOC_IMX21 */

struct platform_device *__init imx_add_imx21_hcd(
		const struct imx_imx21_hcd_data *data,
		const struct mx21_usbh_platform_data *pdata)
{
	struct resource res[] = {
		{
			.start = data->iobase,
			.end = data->iobase + SZ_8K - 1,
			.flags = IORESOURCE_MEM,
		}, {
			.start = data->irq,
			.end = data->irq,
			.flags = IORESOURCE_IRQ,
		},
	};
	return imx_add_platform_device_dmamask("imx21-hcd", 0,
			res, ARRAY_SIZE(res),
			pdata, sizeof(*pdata), DMA_BIT_MASK(32));
}
