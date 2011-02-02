/*
 * Copyright (C) 2010, 2011 Pengutronix,
 *                          Marc Kleine-Budde <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <asm/sizes.h>
#include <mach/mx28.h>
#include <mach/devices-common.h>

#define mxs_flexcan_data_entry_single(soc, _id, _hwid, _size)		\
	{								\
		.id = _id,						\
		.iobase = soc ## _CAN ## _hwid ## _BASE_ADDR,		\
		.iosize = _size,					\
		.irq = soc ## _INT_CAN ## _hwid,			\
	}

#define mxs_flexcan_data_entry(soc, _id, _hwid, _size)			\
	[_id] = mxs_flexcan_data_entry_single(soc, _id, _hwid, _size)

#ifdef CONFIG_SOC_IMX28
const struct mxs_flexcan_data mx28_flexcan_data[] __initconst = {
#define mx28_flexcan_data_entry(_id, _hwid)				\
	mxs_flexcan_data_entry_single(MX28, _id, _hwid, SZ_8K)
	mx28_flexcan_data_entry(0, 0),
	mx28_flexcan_data_entry(1, 1),
};
#endif /* ifdef CONFIG_SOC_IMX28 */

struct platform_device *__init mxs_add_flexcan(
		const struct mxs_flexcan_data *data,
		const struct flexcan_platform_data *pdata)
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

	return mxs_add_platform_device("flexcan", data->id,
			res, ARRAY_SIZE(res), pdata, sizeof(*pdata));
}
