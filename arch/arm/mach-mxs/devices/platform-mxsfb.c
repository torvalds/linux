/*
 * Copyright (C) 2011 Pengutronix, Sascha Hauer <s.hauer@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <linux/dma-mapping.h>
#include <asm/sizes.h>
#include <mach/mx23.h>
#include <mach/mx28.h>
#include <mach/devices-common.h>
#include <linux/mxsfb.h>

#ifdef CONFIG_SOC_IMX23
struct platform_device *__init mx23_add_mxsfb(
		const struct mxsfb_platform_data *pdata)
{
	struct resource res[] = {
		{
			.start = MX23_LCDIF_BASE_ADDR,
			.end = MX23_LCDIF_BASE_ADDR + SZ_8K - 1,
			.flags = IORESOURCE_MEM,
		},
	};

	return mxs_add_platform_device_dmamask("imx23-fb", -1,
			res, ARRAY_SIZE(res), pdata, sizeof(*pdata), DMA_BIT_MASK(32));
}
#endif /* ifdef CONFIG_SOC_IMX23 */

#ifdef CONFIG_SOC_IMX28
struct platform_device *__init mx28_add_mxsfb(
		const struct mxsfb_platform_data *pdata)
{
	struct resource res[] = {
		{
			.start = MX28_LCDIF_BASE_ADDR,
			.end = MX28_LCDIF_BASE_ADDR + SZ_8K - 1,
			.flags = IORESOURCE_MEM,
		},
	};

	return mxs_add_platform_device_dmamask("imx28-fb", -1,
			res, ARRAY_SIZE(res), pdata, sizeof(*pdata), DMA_BIT_MASK(32));
}
#endif /* ifdef CONFIG_SOC_IMX28 */
