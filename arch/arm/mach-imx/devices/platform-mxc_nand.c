/*
 * Copyright (C) 2009-2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <asm/sizes.h>

#include "../hardware.h"
#include "devices-common.h"

#define imx_mxc_nand_data_entry_single(soc, _devid, _size)		\
	{								\
		.devid = _devid,					\
		.iobase = soc ## _NFC_BASE_ADDR,			\
		.iosize = _size,					\
		.irq = soc ## _INT_NFC					\
	}

#define imx_mxc_nandv3_data_entry_single(soc, _devid, _size)		\
	{								\
		.devid = _devid,					\
		.id = -1,						\
		.iobase = soc ## _NFC_BASE_ADDR,			\
		.iosize = _size,					\
		.axibase = soc ## _NFC_AXI_BASE_ADDR,			\
		.irq = soc ## _INT_NFC					\
	}

#ifdef CONFIG_SOC_IMX21
const struct imx_mxc_nand_data imx21_mxc_nand_data __initconst =
	imx_mxc_nand_data_entry_single(MX21, "imx21-nand", SZ_4K);
#endif /* ifdef CONFIG_SOC_IMX21 */

#ifdef CONFIG_SOC_IMX25
const struct imx_mxc_nand_data imx25_mxc_nand_data __initconst =
	imx_mxc_nand_data_entry_single(MX25, "imx25-nand", SZ_8K);
#endif /* ifdef CONFIG_SOC_IMX25 */

#ifdef CONFIG_SOC_IMX27
const struct imx_mxc_nand_data imx27_mxc_nand_data __initconst =
	imx_mxc_nand_data_entry_single(MX27, "imx27-nand", SZ_4K);
#endif /* ifdef CONFIG_SOC_IMX27 */

#ifdef CONFIG_SOC_IMX31
const struct imx_mxc_nand_data imx31_mxc_nand_data __initconst =
	imx_mxc_nand_data_entry_single(MX31, "imx27-nand", SZ_4K);
#endif

#ifdef CONFIG_SOC_IMX35
const struct imx_mxc_nand_data imx35_mxc_nand_data __initconst =
	imx_mxc_nand_data_entry_single(MX35, "imx25-nand", SZ_8K);
#endif

struct platform_device *__init imx_add_mxc_nand(
		const struct imx_mxc_nand_data *data,
		const struct mxc_nand_platform_data *pdata)
{
	/* AXI has to come first, that's how the mxc_nand driver expect it */
	struct resource res[] = {
		{
			.start = data->iobase,
			.end = data->iobase + data->iosize - 1,
			.flags = IORESOURCE_MEM,
		}, {
			.start = data->irq,
			.end = data->irq,
			.flags = IORESOURCE_IRQ,
		}, {
			.start = data->axibase,
			.end = data->axibase + SZ_16K - 1,
			.flags = IORESOURCE_MEM,
		},
	};
	return imx_add_platform_device(data->devid, data->id,
			res, ARRAY_SIZE(res) - !data->axibase,
			pdata, sizeof(*pdata));
}
