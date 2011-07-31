/*
 * Copyright (C) 2009-2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <asm/sizes.h>
#include <mach/devices-common.h>

struct platform_device *__init imx_add_spi_imx(int id,
		resource_size_t iobase, resource_size_t iosize, int irq,
		const struct spi_imx_master *pdata)
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

	return imx_add_platform_device("spi_imx", id, res, ARRAY_SIZE(res),
			pdata, sizeof(*pdata));
}
