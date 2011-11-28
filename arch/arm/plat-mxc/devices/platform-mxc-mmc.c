/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <linux/dma-mapping.h>
#include <mach/hardware.h>
#include <mach/devices-common.h>

#define imx_mxc_mmc_data_entry_single(soc, _id, _hwid, _size)		\
	{								\
		.id = _id,						\
		.iobase = soc ## _SDHC ## _hwid ## _BASE_ADDR,		\
		.iosize = _size,					\
		.irq = soc ## _INT_SDHC ## _hwid,			\
		.dmareq = soc ## _DMA_REQ_SDHC ## _hwid,		\
	}
#define imx_mxc_mmc_data_entry(soc, _id, _hwid, _size)			\
	[_id] = imx_mxc_mmc_data_entry_single(soc, _id, _hwid, _size)

#ifdef CONFIG_SOC_IMX21
const struct imx_mxc_mmc_data imx21_mxc_mmc_data[] __initconst = {
#define imx21_mxc_mmc_data_entry(_id, _hwid)				\
	imx_mxc_mmc_data_entry(MX21, _id, _hwid, SZ_4K)
	imx21_mxc_mmc_data_entry(0, 1),
	imx21_mxc_mmc_data_entry(1, 2),
};
#endif /* ifdef CONFIG_SOC_IMX21 */

#ifdef CONFIG_SOC_IMX27
const struct imx_mxc_mmc_data imx27_mxc_mmc_data[] __initconst = {
#define imx27_mxc_mmc_data_entry(_id, _hwid)				\
	imx_mxc_mmc_data_entry(MX27, _id, _hwid, SZ_4K)
	imx27_mxc_mmc_data_entry(0, 1),
	imx27_mxc_mmc_data_entry(1, 2),
};
#endif /* ifdef CONFIG_SOC_IMX27 */

#ifdef CONFIG_SOC_IMX31
const struct imx_mxc_mmc_data imx31_mxc_mmc_data[] __initconst = {
#define imx31_mxc_mmc_data_entry(_id, _hwid)				\
	imx_mxc_mmc_data_entry(MX31, _id, _hwid, SZ_16K)
	imx31_mxc_mmc_data_entry(0, 1),
	imx31_mxc_mmc_data_entry(1, 2),
};
#endif /* ifdef CONFIG_SOC_IMX31 */

struct platform_device *__init imx_add_mxc_mmc(
		const struct imx_mxc_mmc_data *data,
		const struct imxmmc_platform_data *pdata)
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
			.start = data->dmareq,
			.end = data->dmareq,
			.flags = IORESOURCE_DMA,
		},
	};
	return imx_add_platform_device_dmamask("mxc-mmc", data->id,
			res, ARRAY_SIZE(res),
			pdata, sizeof(*pdata), DMA_BIT_MASK(32));
}
