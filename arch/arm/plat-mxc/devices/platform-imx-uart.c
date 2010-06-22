/*
 * Copyright (C) 2009-2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <mach/devices-common.h>

struct platform_device *__init imx_add_imx_uart_3irq(int id,
		resource_size_t iobase, resource_size_t iosize,
		resource_size_t irqrx, resource_size_t irqtx,
		resource_size_t irqrts,
		const struct imxuart_platform_data *pdata)
{
	struct resource res[] = {
		{
			.start = iobase,
			.end = iobase + iosize - 1,
			.flags = IORESOURCE_MEM,
		}, {
			.start = irqrx,
			.end = irqrx,
			.flags = IORESOURCE_IRQ,
		}, {
			.start = irqtx,
			.end = irqtx,
			.flags = IORESOURCE_IRQ,
		}, {
			.start = irqrts,
			.end = irqrx,
			.flags = IORESOURCE_IRQ,
		},
	};

	return imx_add_platform_device("imx-uart", id, res, ARRAY_SIZE(res),
			pdata, sizeof(*pdata));
}

struct platform_device *__init imx_add_imx_uart_1irq(int id,
		resource_size_t iobase, resource_size_t iosize,
		resource_size_t irq,
		const struct imxuart_platform_data *pdata)
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

	return imx_add_platform_device("imx-uart", id, res, ARRAY_SIZE(res),
			pdata, sizeof(*pdata));
}
