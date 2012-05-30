/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
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

#define mxs_mxs_mmc_data_entry_single(soc, _devid, _id, hwid)		\
	{								\
		.devid = _devid,					\
		.id = _id,						\
		.iobase = soc ## _SSP ## hwid ## _BASE_ADDR,		\
		.dma = soc ## _DMA_SSP ## hwid,				\
		.irq_err = soc ## _INT_SSP ## hwid ## _ERROR,		\
		.irq_dma = soc ## _INT_SSP ## hwid ## _DMA,		\
	}

#define mxs_mxs_mmc_data_entry(soc, _devid, _id, hwid)			\
	[_id] = mxs_mxs_mmc_data_entry_single(soc, _devid, _id, hwid)


#ifdef CONFIG_SOC_IMX23
const struct mxs_mxs_mmc_data mx23_mxs_mmc_data[] __initconst = {
	mxs_mxs_mmc_data_entry(MX23, "imx23-mmc", 0, 1),
	mxs_mxs_mmc_data_entry(MX23, "imx23-mmc", 1, 2),
};
#endif

#ifdef CONFIG_SOC_IMX28
const struct mxs_mxs_mmc_data mx28_mxs_mmc_data[] __initconst = {
	mxs_mxs_mmc_data_entry(MX28, "imx28-mmc", 0, 0),
	mxs_mxs_mmc_data_entry(MX28, "imx28-mmc", 1, 1),
	mxs_mxs_mmc_data_entry(MX28, "imx28-mmc", 2, 2),
	mxs_mxs_mmc_data_entry(MX28, "imx28-mmc", 3, 3),
};
#endif

struct platform_device *__init mxs_add_mxs_mmc(
		const struct mxs_mxs_mmc_data *data,
		const struct mxs_mmc_platform_data *pdata)
{
	struct resource res[] = {
		{
			.start	= data->iobase,
			.end	= data->iobase + SZ_8K - 1,
			.flags	= IORESOURCE_MEM,
		}, {
			.start	= data->dma,
			.end	= data->dma,
			.flags	= IORESOURCE_DMA,
		}, {
			.start	= data->irq_err,
			.end	= data->irq_err,
			.flags	= IORESOURCE_IRQ,
		}, {
			.start	= data->irq_dma,
			.end	= data->irq_dma,
			.flags	= IORESOURCE_IRQ,
		},
	};

	return mxs_add_platform_device(data->devid, data->id,
			res, ARRAY_SIZE(res), pdata, sizeof(*pdata));
}
