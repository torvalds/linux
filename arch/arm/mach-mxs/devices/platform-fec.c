/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <asm/sizes.h>
#include <mach/mx28.h>
#include <mach/devices-common.h>

#define mxs_fec_data_entry_single(soc, _id)				\
	{								\
		.id = _id,						\
		.iobase = soc ## _ENET_MAC ## _id ## _BASE_ADDR,	\
		.irq = soc ## _INT_ENET_MAC ## _id,			\
	}

#define mxs_fec_data_entry(soc, _id)					\
	[_id] = mxs_fec_data_entry_single(soc, _id)

#ifdef CONFIG_SOC_IMX28
const struct mxs_fec_data mx28_fec_data[] __initconst = {
#define mx28_fec_data_entry(_id)					\
	mxs_fec_data_entry(MX28, _id)
	mx28_fec_data_entry(0),
	mx28_fec_data_entry(1),
};
#endif

struct platform_device *__init mxs_add_fec(
		const struct mxs_fec_data *data,
		const struct fec_platform_data *pdata)
{
	struct resource res[] = {
		{
			.start = data->iobase,
			.end = data->iobase + SZ_16K - 1,
			.flags = IORESOURCE_MEM,
		}, {
			.start = data->irq,
			.end = data->irq,
			.flags = IORESOURCE_IRQ,
		},
	};

	return mxs_add_platform_device_dmamask("imx28-fec", data->id,
			res, ARRAY_SIZE(res), pdata, sizeof(*pdata),
			DMA_BIT_MASK(32));
}
