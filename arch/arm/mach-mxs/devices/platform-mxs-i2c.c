/*
 * Copyright (C) 2011 Pengutronix
 * Wolfram Sang <w.sang@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <asm/sizes.h>
#include <mach/mx28.h>
#include <mach/devices-common.h>

#define mxs_i2c_data_entry_single(soc, _id)				\
	{								\
		.id = _id,						\
		.iobase = soc ## _I2C ## _id ## _BASE_ADDR,		\
		.errirq = soc ## _INT_I2C ## _id ## _ERROR,		\
		.dmairq = soc ## _INT_I2C ## _id ## _DMA,		\
	}

#define mxs_i2c_data_entry(soc, _id)					\
	[_id] = mxs_i2c_data_entry_single(soc, _id)

#ifdef CONFIG_SOC_IMX28
const struct mxs_mxs_i2c_data mx28_mxs_i2c_data[] __initconst = {
	mxs_i2c_data_entry(MX28, 0),
	mxs_i2c_data_entry(MX28, 1),
};
#endif

struct platform_device *__init mxs_add_mxs_i2c(
		const struct mxs_mxs_i2c_data *data)
{
	struct resource res[] = {
		{
			.start = data->iobase,
			.end = data->iobase + SZ_8K - 1,
			.flags = IORESOURCE_MEM,
		}, {
			.start = data->errirq,
			.end = data->errirq,
			.flags = IORESOURCE_IRQ,
		}, {
			.start = data->dmairq,
			.end = data->dmairq,
			.flags = IORESOURCE_IRQ,
		},
	};

	return mxs_add_platform_device("mxs-i2c", data->id, res,
					ARRAY_SIZE(res), NULL, 0);
}
