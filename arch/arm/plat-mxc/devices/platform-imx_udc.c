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

#define imx_imx_udc_data_entry_single(soc, _size)			\
	{								\
		.iobase = soc ## _USBD_BASE_ADDR,			\
		.iosize = _size,					\
		.irq0 = soc ## _INT_USBD0,				\
		.irq1 = soc ## _INT_USBD1,				\
		.irq2 = soc ## _INT_USBD2,				\
		.irq3 = soc ## _INT_USBD3,				\
		.irq4 = soc ## _INT_USBD4,				\
		.irq5 = soc ## _INT_USBD5,				\
		.irq6 = soc ## _INT_USBD6,				\
	}

#define imx_imx_udc_data_entry(soc, _size)				\
	[_id] = imx_imx_udc_data_entry_single(soc, _size)

#ifdef CONFIG_SOC_IMX1
const struct imx_imx_udc_data imx1_imx_udc_data __initconst =
	imx_imx_udc_data_entry_single(MX1, SZ_4K);
#endif /* ifdef CONFIG_SOC_IMX1 */

struct platform_device *__init imx_add_imx_udc(
		const struct imx_imx_udc_data *data,
		const struct imxusb_platform_data *pdata)
{
	struct resource res[] = {
		{
			.start = data->iobase,
			.end = data->iobase + data->iosize - 1,
			.flags = IORESOURCE_MEM,
		}, {
			.start = data->irq0,
			.end = data->irq0,
			.flags = IORESOURCE_IRQ,
		}, {
			.start = data->irq1,
			.end = data->irq1,
			.flags = IORESOURCE_IRQ,
		}, {
			.start = data->irq2,
			.end = data->irq2,
			.flags = IORESOURCE_IRQ,
		}, {
			.start = data->irq3,
			.end = data->irq3,
			.flags = IORESOURCE_IRQ,
		}, {
			.start = data->irq4,
			.end = data->irq4,
			.flags = IORESOURCE_IRQ,
		}, {
			.start = data->irq5,
			.end = data->irq5,
			.flags = IORESOURCE_IRQ,
		}, {
			.start = data->irq6,
			.end = data->irq6,
			.flags = IORESOURCE_IRQ,
		},
	};

	return imx_add_platform_device("imx_udc", 0,
			res, ARRAY_SIZE(res), pdata, sizeof(*pdata));
}
