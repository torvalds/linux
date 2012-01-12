/*
 * Copyright 2011 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <linux/compiler.h>
#include <linux/err.h>
#include <linux/init.h>

#include <mach/mx23.h>
#include <mach/mx28.h>
#include <mach/devices-common.h>

#define mxs_saif_data_entry_single(soc, _id)				\
	{								\
		.id = _id,						\
		.iobase = soc ## _SAIF ## _id ## _BASE_ADDR,		\
		.irq = soc ## _INT_SAIF ## _id,				\
		.dma = soc ## _DMA_SAIF ## _id,				\
		.dmairq = soc ## _INT_SAIF ## _id ##_DMA,		\
	}

#define mxs_saif_data_entry(soc, _id)					\
	[_id] = mxs_saif_data_entry_single(soc, _id)

#ifdef CONFIG_SOC_IMX28
const struct mxs_saif_data mx28_saif_data[] __initconst = {
	mxs_saif_data_entry(MX28, 0),
	mxs_saif_data_entry(MX28, 1),
};
#endif

struct platform_device *__init mxs_add_saif(const struct mxs_saif_data *data,
				const struct mxs_saif_platform_data *pdata)
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
		}, {
			.start = data->dma,
			.end = data->dma,
			.flags = IORESOURCE_DMA,
		}, {
			.start = data->dmairq,
			.end = data->dmairq,
			.flags = IORESOURCE_IRQ,
		},

	};

	return mxs_add_platform_device("mxs-saif", data->id, res,
				ARRAY_SIZE(res), pdata, sizeof(*pdata));
}
