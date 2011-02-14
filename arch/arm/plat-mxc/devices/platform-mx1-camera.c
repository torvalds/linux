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

#define imx_mx1_camera_data_entry_single(soc, _size)			\
	{								\
		.iobase = soc ## _CSI ## _BASE_ADDR,			\
		.iosize = _size,					\
		.irq = soc ## _INT_CSI,					\
	}

#ifdef CONFIG_SOC_IMX1
const struct imx_mx1_camera_data imx1_mx1_camera_data __initconst =
	imx_mx1_camera_data_entry_single(MX1, 10);
#endif /* ifdef CONFIG_SOC_IMX1 */

struct platform_device *__init imx_add_mx1_camera(
		const struct imx_mx1_camera_data *data,
		const struct mx1_camera_pdata *pdata)
{
	struct resource res[] = {
		{
			.start = data->iobase,
			.end = data->iobase + data->iosize - 1,
			.flags = IORESOURCE_MEM,
		}, {
			.start = data->irq,
			.end = data->irq,
			.flags = IORESOURCE_IRQ,
		},
	};
	return imx_add_platform_device_dmamask("mx1-camera", 0,
			res, ARRAY_SIZE(res),
			pdata, sizeof(*pdata), DMA_BIT_MASK(32));
}
