/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include "../hardware.h"
#include "devices-common.h"

#define imx_mx2_emmaprp_data_entry_single(soc)				\
	{								\
		.iobase = soc ## _EMMAPRP_BASE_ADDR,			\
		.iosize = SZ_256,					\
		.irq = soc ## _INT_EMMAPRP,				\
	}

#ifdef CONFIG_SOC_IMX27
const struct imx_mx2_emma_data imx27_mx2_emmaprp_data __initconst =
	imx_mx2_emmaprp_data_entry_single(MX27);
#endif /* ifdef CONFIG_SOC_IMX27 */

struct platform_device *__init imx_add_mx2_emmaprp(
		const struct imx_mx2_emma_data *data)
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
	return imx_add_platform_device_dmamask("m2m-emmaprp", 0,
			res, 2, NULL, 0, DMA_BIT_MASK(32));
}
