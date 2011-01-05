/*
 * Copyright (C) 2009-2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * Copyright 2010 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <mach/mx23.h>
#include <mach/mx28.h>
#include <mach/devices-common.h>

#define mxs_duart_data_entry(soc)					\
	{								\
		.iobase = soc ## _DUART_BASE_ADDR,			\
		.irq = soc ## _INT_DUART,				\
	}

#ifdef CONFIG_SOC_IMX23
const struct mxs_duart_data mx23_duart_data __initconst =
	mxs_duart_data_entry(MX23);
#endif

#ifdef CONFIG_SOC_IMX28
const struct mxs_duart_data mx28_duart_data __initconst =
	mxs_duart_data_entry(MX28);
#endif

struct platform_device *__init mxs_add_duart(
		const struct mxs_duart_data *data)
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

	return mxs_add_platform_device("mxs-duart", 0, res, ARRAY_SIZE(res),
					NULL, 0);
}
