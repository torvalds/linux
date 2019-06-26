/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <linux/dma-mapping.h>
#include <linux/sizes.h>

#include "../hardware.h"
#include "devices-common.h"

#define imx_fec_data_entry_single(soc, _devid)				\
	{								\
		.devid = _devid,					\
		.iobase = soc ## _FEC_BASE_ADDR,			\
		.irq = soc ## _INT_FEC,					\
	}

#ifdef CONFIG_SOC_IMX27
const struct imx_fec_data imx27_fec_data __initconst =
	imx_fec_data_entry_single(MX27, "imx27-fec");
#endif /* ifdef CONFIG_SOC_IMX27 */

#ifdef CONFIG_SOC_IMX35
/* i.mx35 has the i.mx27 type fec */
const struct imx_fec_data imx35_fec_data __initconst =
	imx_fec_data_entry_single(MX35, "imx27-fec");
#endif

struct platform_device *__init imx_add_fec(
		const struct imx_fec_data *data,
		const struct fec_platform_data *pdata)
{
	struct resource res[] = {
		{
			.start = data->iobase,
			.end = data->iobase + SZ_4K - 1,
			.flags = IORESOURCE_MEM,
		}, {
			.start = data->irq,
			.end = data->irq,
			.flags = IORESOURCE_IRQ,
		},
	};

	return imx_add_platform_device_dmamask(data->devid, 0,
			res, ARRAY_SIZE(res),
			pdata, sizeof(*pdata), DMA_BIT_MASK(32));
}
