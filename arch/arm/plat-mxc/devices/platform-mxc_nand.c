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

static struct platform_device *__init imx_add_mxc_nand(resource_size_t iobase,
		int irq, const struct mxc_nand_platform_data *pdata,
		resource_size_t iosize)
{
	static int id = 0;
	
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

	return imx_add_platform_device("mxc_nand", id++, res, ARRAY_SIZE(res),
			pdata, sizeof(*pdata));
}

struct platform_device *__init imx_add_mxc_nand_v1(resource_size_t iobase,
		int irq, const struct mxc_nand_platform_data *pdata)
{
	return imx_add_mxc_nand(iobase, irq, pdata, SZ_4K);
}

struct platform_device *__init imx_add_mxc_nand_v21(resource_size_t iobase,
		int irq, const struct mxc_nand_platform_data *pdata)
{
	return imx_add_mxc_nand(iobase, irq, pdata, SZ_8K);
}
