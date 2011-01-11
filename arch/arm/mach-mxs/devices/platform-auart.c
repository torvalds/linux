/*
 * Copyright (C) 2010 Pengutronix
 * Sascha Hauer <s.hauer@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <asm/sizes.h>
#include <mach/mx28.h>
#include <mach/devices-common.h>

#define mxs_auart_data_entry_single(soc, _id)				\
	{								\
		.id = _id,						\
		.iobase = soc ## _AUART ## _id ## _BASE_ADDR,		\
		.irq = soc ## _INT_AUART ## _id,			\
	}

#define mxs_auart_data_entry(soc, _id)					\
	[_id] = mxs_auart_data_entry_single(soc, _id)

#ifdef CONFIG_SOC_IMX28
const struct mxs_auart_data mx28_auart_data[] __initconst = {
#define mx28_auart_data_entry(_id)					\
	mxs_auart_data_entry(MX28, _id)
	mx28_auart_data_entry(0),
	mx28_auart_data_entry(1),
	mx28_auart_data_entry(2),
	mx28_auart_data_entry(3),
	mx28_auart_data_entry(4),
};
#endif

struct platform_device *__init mxs_add_auart(
		const struct mxs_auart_data *data)
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

	return mxs_add_platform_device_dmamask("mxs-auart", data->id,
					res, ARRAY_SIZE(res), NULL, 0,
					DMA_BIT_MASK(32));
}

