/*
 * Copyright (C) 2010 Pengutronix, Wolfram Sang <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */

#include <linux/platform_data/mmc-esdhc-imx.h>

#include "../hardware.h"
#include "devices-common.h"

#define imx_sdhci_esdhc_imx_data_entry_single(soc, _devid, _id, hwid) \
	{								\
		.devid = _devid,					\
		.id = _id,						\
		.iobase = soc ## _ESDHC ## hwid ## _BASE_ADDR,	\
		.irq = soc ## _INT_ESDHC ## hwid,			\
	}

#define imx_sdhci_esdhc_imx_data_entry(soc, devid, id, hwid)	\
	[id] = imx_sdhci_esdhc_imx_data_entry_single(soc, devid, id, hwid)

#ifdef CONFIG_SOC_IMX35
const struct imx_sdhci_esdhc_imx_data
imx35_sdhci_esdhc_imx_data[] __initconst = {
#define imx35_sdhci_esdhc_imx_data_entry(_id, _hwid)			\
	imx_sdhci_esdhc_imx_data_entry(MX35, "sdhci-esdhc-imx35", _id, _hwid)
	imx35_sdhci_esdhc_imx_data_entry(0, 1),
	imx35_sdhci_esdhc_imx_data_entry(1, 2),
	imx35_sdhci_esdhc_imx_data_entry(2, 3),
};
#endif /* ifdef CONFIG_SOC_IMX35 */

static const struct esdhc_platform_data default_esdhc_pdata __initconst = {
	.wp_type = ESDHC_WP_NONE,
	.cd_type = ESDHC_CD_NONE,
};

struct platform_device *__init imx_add_sdhci_esdhc_imx(
		const struct imx_sdhci_esdhc_imx_data *data,
		const struct esdhc_platform_data *pdata)
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

	/*
	 * If machine does not provide pdata, use the default one
	 * which means no WP/CD support
	 */
	if (!pdata)
		pdata = &default_esdhc_pdata;

	return imx_add_platform_device_dmamask(data->devid, data->id, res,
			ARRAY_SIZE(res), pdata, sizeof(*pdata),
			DMA_BIT_MASK(32));
}
