/*
 * Copyright (C) 2010 Pengutronix, Wolfram Sang <w.sang@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */

#include <mach/devices-common.h>
#include <mach/esdhc.h>

struct platform_device *__init imx_add_esdhc(int id,
		resource_size_t iobase, resource_size_t iosize,
		resource_size_t irq,
		const struct esdhc_platform_data *pdata)
{
	struct resource res[] = {
		{
			.start = iobase,
			.end = iobase + iosize - 1,
			.flags = IORESOURCE_MEM,
		}, {
			.start = irq,
			.end = irq,
			.flags = IORESOURCE_IRQ,
		},
	};

	return imx_add_platform_device("sdhci-esdhc-imx", id, res,
			ARRAY_SIZE(res), pdata, sizeof(*pdata));
}
