/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <asm/sizes.h>

#include "../hardware.h"
#include "devices-common.h"

#define imx_imx2_wdt_data_entry_single(soc, _id, _hwid, _size)		\
	{								\
		.id = _id,						\
		.iobase = soc ## _WDOG ## _hwid ## _BASE_ADDR,		\
		.iosize = _size,					\
	}
#define imx_imx2_wdt_data_entry(soc, _id, _hwid, _size)			\
	[_id] = imx_imx2_wdt_data_entry_single(soc, _id, _hwid, _size)

#ifdef CONFIG_SOC_IMX21
const struct imx_imx2_wdt_data imx21_imx2_wdt_data __initconst =
	imx_imx2_wdt_data_entry_single(MX21, 0, , SZ_4K);
#endif /* ifdef CONFIG_SOC_IMX21 */

#ifdef CONFIG_SOC_IMX27
const struct imx_imx2_wdt_data imx27_imx2_wdt_data __initconst =
	imx_imx2_wdt_data_entry_single(MX27, 0, , SZ_4K);
#endif /* ifdef CONFIG_SOC_IMX27 */

#ifdef CONFIG_SOC_IMX31
const struct imx_imx2_wdt_data imx31_imx2_wdt_data __initconst =
	imx_imx2_wdt_data_entry_single(MX31, 0, , SZ_16K);
#endif /* ifdef CONFIG_SOC_IMX31 */

#ifdef CONFIG_SOC_IMX35
const struct imx_imx2_wdt_data imx35_imx2_wdt_data __initconst =
	imx_imx2_wdt_data_entry_single(MX35, 0, , SZ_16K);
#endif /* ifdef CONFIG_SOC_IMX35 */

struct platform_device *__init imx_add_imx2_wdt(
		const struct imx_imx2_wdt_data *data)
{
	struct resource res[] = {
		{
			.start = data->iobase,
			.end = data->iobase + data->iosize - 1,
			.flags = IORESOURCE_MEM,
		},
	};
	return imx_add_platform_device("imx2-wdt", data->id,
			res, ARRAY_SIZE(res), NULL, 0);
}
