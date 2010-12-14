/*
 * Copyright (C) 2009-2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <mach/hardware.h>
#include <mach/devices-common.h>

#define imx_imx_i2c_data_entry_single(soc, _id, _hwid, _size)		\
	{								\
		.id = _id,						\
		.iobase = soc ## _I2C ## _hwid ## _BASE_ADDR,		\
		.iosize = _size,					\
		.irq = soc ## _INT_I2C ## _hwid,			\
	}

#define imx_imx_i2c_data_entry(soc, _id, _hwid, _size)			\
	[_id] = imx_imx_i2c_data_entry_single(soc, _id, _hwid, _size)

#ifdef CONFIG_SOC_IMX1
const struct imx_imx_i2c_data imx1_imx_i2c_data __initconst =
	imx_imx_i2c_data_entry_single(MX1, 0, , SZ_4K);
#endif /* ifdef CONFIG_SOC_IMX1 */

#ifdef CONFIG_SOC_IMX21
const struct imx_imx_i2c_data imx21_imx_i2c_data __initconst =
	imx_imx_i2c_data_entry_single(MX21, 0, , SZ_4K);
#endif /* ifdef CONFIG_SOC_IMX21 */

#ifdef CONFIG_ARCH_MX25
const struct imx_imx_i2c_data imx25_imx_i2c_data[] __initconst = {
#define imx25_imx_i2c_data_entry(_id, _hwid)				\
	imx_imx_i2c_data_entry(MX25, _id, _hwid, SZ_16K)
	imx25_imx_i2c_data_entry(0, 1),
	imx25_imx_i2c_data_entry(1, 2),
	imx25_imx_i2c_data_entry(2, 3),
};
#endif /* ifdef CONFIG_ARCH_MX25 */

#ifdef CONFIG_SOC_IMX27
const struct imx_imx_i2c_data imx27_imx_i2c_data[] __initconst = {
#define imx27_imx_i2c_data_entry(_id, _hwid)				\
	imx_imx_i2c_data_entry(MX27, _id, _hwid, SZ_4K)
	imx27_imx_i2c_data_entry(0, 1),
	imx27_imx_i2c_data_entry(1, 2),
};
#endif /* ifdef CONFIG_SOC_IMX27 */

#ifdef CONFIG_ARCH_MX31
const struct imx_imx_i2c_data imx31_imx_i2c_data[] __initconst = {
#define imx31_imx_i2c_data_entry(_id, _hwid)				\
	imx_imx_i2c_data_entry(MX31, _id, _hwid, SZ_4K)
	imx31_imx_i2c_data_entry(0, 1),
	imx31_imx_i2c_data_entry(1, 2),
	imx31_imx_i2c_data_entry(2, 3),
};
#endif /* ifdef CONFIG_ARCH_MX31 */

#ifdef CONFIG_ARCH_MX35
const struct imx_imx_i2c_data imx35_imx_i2c_data[] __initconst = {
#define imx35_imx_i2c_data_entry(_id, _hwid)				\
	imx_imx_i2c_data_entry(MX35, _id, _hwid, SZ_4K)
	imx35_imx_i2c_data_entry(0, 1),
	imx35_imx_i2c_data_entry(1, 2),
	imx35_imx_i2c_data_entry(2, 3),
};
#endif /* ifdef CONFIG_ARCH_MX35 */

#ifdef CONFIG_ARCH_MX51
const struct imx_imx_i2c_data imx51_imx_i2c_data[] __initconst = {
#define imx51_imx_i2c_data_entry(_id, _hwid)				\
	imx_imx_i2c_data_entry(MX51, _id, _hwid, SZ_4K)
	imx51_imx_i2c_data_entry(0, 1),
	imx51_imx_i2c_data_entry(1, 2),
};
#endif /* ifdef CONFIG_ARCH_MX51 */

struct platform_device *__init imx_add_imx_i2c(
		const struct imx_imx_i2c_data *data,
		const struct imxi2c_platform_data *pdata)
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

	return imx_add_platform_device("imx-i2c", data->id,
			res, ARRAY_SIZE(res),
			pdata, sizeof(*pdata));
}
