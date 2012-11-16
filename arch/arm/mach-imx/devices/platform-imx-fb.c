/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <linux/dma-mapping.h>

#include "../hardware.h"
#include "devices-common.h"

#define imx_imx_fb_data_entry_single(soc, _devid, _size)		\
	{								\
		.devid = _devid,					\
		.iobase = soc ## _LCDC_BASE_ADDR,			\
		.iosize = _size,					\
		.irq = soc ## _INT_LCDC,				\
	}

#ifdef CONFIG_SOC_IMX1
const struct imx_imx_fb_data imx1_imx_fb_data __initconst =
	imx_imx_fb_data_entry_single(MX1, "imx1-fb", SZ_4K);
#endif /* ifdef CONFIG_SOC_IMX1 */

#ifdef CONFIG_SOC_IMX21
const struct imx_imx_fb_data imx21_imx_fb_data __initconst =
	imx_imx_fb_data_entry_single(MX21, "imx21-fb", SZ_4K);
#endif /* ifdef CONFIG_SOC_IMX21 */

#ifdef CONFIG_SOC_IMX25
const struct imx_imx_fb_data imx25_imx_fb_data __initconst =
	imx_imx_fb_data_entry_single(MX25, "imx21-fb", SZ_16K);
#endif /* ifdef CONFIG_SOC_IMX25 */

#ifdef CONFIG_SOC_IMX27
const struct imx_imx_fb_data imx27_imx_fb_data __initconst =
	imx_imx_fb_data_entry_single(MX27, "imx21-fb", SZ_4K);
#endif /* ifdef CONFIG_SOC_IMX27 */

struct platform_device *__init imx_add_imx_fb(
		const struct imx_imx_fb_data *data,
		const struct imx_fb_platform_data *pdata)
{
	struct resource res[] = {
		{
			.start = data->iobase,
			.end = data->iobase + data->iosize - 1,
			.flags = IORESOURCE_MEM,
		}, {
			.start = data->irq,
			.end = data->irq,
			.flags = IORESOURCE_IRQ,
		},
	};
	return imx_add_platform_device_dmamask("imx-fb", 0,
			res, ARRAY_SIZE(res),
			pdata, sizeof(*pdata), DMA_BIT_MASK(32));
}
