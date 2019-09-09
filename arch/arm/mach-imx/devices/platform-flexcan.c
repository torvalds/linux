// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2010 Pengutronix, Marc Kleine-Budde <kernel@pengutronix.de>
 */
#include "../hardware.h"
#include "devices-common.h"

#define imx_flexcan_data_entry_single(soc, _id, _hwid, _size)		\
	{								\
		.id = _id,						\
		.iobase = soc ## _CAN ## _hwid ## _BASE_ADDR,		\
		.iosize = _size,					\
		.irq = soc ## _INT_CAN ## _hwid,			\
	}

#define imx_flexcan_data_entry(soc, _id, _hwid, _size)			\
	[_id] = imx_flexcan_data_entry_single(soc, _id, _hwid, _size)

#ifdef CONFIG_SOC_IMX35
const struct imx_flexcan_data imx35_flexcan_data[] __initconst = {
#define imx35_flexcan_data_entry(_id, _hwid)				\
	imx_flexcan_data_entry(MX35, _id, _hwid, SZ_16K)
	imx35_flexcan_data_entry(0, 1),
	imx35_flexcan_data_entry(1, 2),
};
#endif /* ifdef CONFIG_SOC_IMX35 */

struct platform_device *__init imx_add_flexcan(
		const struct imx_flexcan_data *data)
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

	return imx_add_platform_device("flexcan", data->id,
			res, ARRAY_SIZE(res), NULL, 0);
}
