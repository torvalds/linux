/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <mach/hardware.h>
#include <mach/devices-common.h>

#define imx_imx_ssi_data_entry(soc, _id, _hwid, _size)			\
	[_id] = {							\
		.id = _id,						\
		.iobase = soc ## _SSI ## _hwid ## _BASE_ADDR,		\
		.iosize = _size,					\
		.irq = soc ## _INT_SSI ## _hwid,			\
		.dmatx0 = soc ## _DMA_REQ_SSI ## _hwid ## _TX0,		\
		.dmarx0 = soc ## _DMA_REQ_SSI ## _hwid ## _RX0,		\
		.dmatx1 = soc ## _DMA_REQ_SSI ## _hwid ## _TX1,		\
		.dmarx1 = soc ## _DMA_REQ_SSI ## _hwid ## _RX1,		\
	}

#ifdef CONFIG_SOC_IMX21
const struct imx_imx_ssi_data imx21_imx_ssi_data[] __initconst = {
#define imx21_imx_ssi_data_entry(_id, _hwid)				\
	imx_imx_ssi_data_entry(MX21, _id, _hwid, SZ_4K)
	imx21_imx_ssi_data_entry(0, 1),
	imx21_imx_ssi_data_entry(1, 2),
};
#endif /* ifdef CONFIG_SOC_IMX21 */

#ifdef CONFIG_SOC_IMX25
const struct imx_imx_ssi_data imx25_imx_ssi_data[] __initconst = {
#define imx25_imx_ssi_data_entry(_id, _hwid)				\
	imx_imx_ssi_data_entry(MX25, _id, _hwid, SZ_4K)
	imx25_imx_ssi_data_entry(0, 1),
	imx25_imx_ssi_data_entry(1, 2),
};
#endif /* ifdef CONFIG_SOC_IMX25 */

#ifdef CONFIG_SOC_IMX27
const struct imx_imx_ssi_data imx27_imx_ssi_data[] __initconst = {
#define imx27_imx_ssi_data_entry(_id, _hwid)				\
	imx_imx_ssi_data_entry(MX27, _id, _hwid, SZ_4K)
	imx27_imx_ssi_data_entry(0, 1),
	imx27_imx_ssi_data_entry(1, 2),
};
#endif /* ifdef CONFIG_SOC_IMX27 */

#ifdef CONFIG_SOC_IMX31
const struct imx_imx_ssi_data imx31_imx_ssi_data[] __initconst = {
#define imx31_imx_ssi_data_entry(_id, _hwid)				\
	imx_imx_ssi_data_entry(MX31, _id, _hwid, SZ_4K)
	imx31_imx_ssi_data_entry(0, 1),
	imx31_imx_ssi_data_entry(1, 2),
};
#endif /* ifdef CONFIG_SOC_IMX31 */

#ifdef CONFIG_SOC_IMX35
const struct imx_imx_ssi_data imx35_imx_ssi_data[] __initconst = {
#define imx35_imx_ssi_data_entry(_id, _hwid)				\
	imx_imx_ssi_data_entry(MX35, _id, _hwid, SZ_4K)
	imx35_imx_ssi_data_entry(0, 1),
	imx35_imx_ssi_data_entry(1, 2),
};
#endif /* ifdef CONFIG_SOC_IMX35 */

#ifdef CONFIG_SOC_IMX51
const struct imx_imx_ssi_data imx51_imx_ssi_data[] __initconst = {
#define imx51_imx_ssi_data_entry(_id, _hwid)				\
	imx_imx_ssi_data_entry(MX51, _id, _hwid, SZ_4K)
	imx51_imx_ssi_data_entry(0, 1),
	imx51_imx_ssi_data_entry(1, 2),
};
#endif /* ifdef CONFIG_SOC_IMX51 */

struct platform_device *__init imx_add_imx_ssi(
		const struct imx_imx_ssi_data *data,
		const struct imx_ssi_platform_data *pdata)
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
#define DMARES(_name) {							\
	.name = #_name,							\
	.start = data->dma ## _name,					\
	.end = data->dma ## _name,					\
	.flags = IORESOURCE_DMA,					\
}
		DMARES(tx0),
		DMARES(rx0),
		DMARES(tx1),
		DMARES(rx1),
	};

	return imx_add_platform_device("imx-ssi", data->id,
			res, ARRAY_SIZE(res),
			pdata, sizeof(*pdata));
}
