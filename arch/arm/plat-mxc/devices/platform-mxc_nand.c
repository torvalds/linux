/*
 * Copyright (C) 2009-2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <asm/sizes.h>
#include <mach/hardware.h>
#include <mach/devices-common.h>

#define imx_mxc_nand_data_entry_single(soc, _size)			\
	{								\
		.iobase = soc ## _NFC_BASE_ADDR,			\
		.iosize = _size,					\
		.irq = soc ## _INT_NFC					\
	}

#ifdef CONFIG_SOC_IMX21
const struct imx_mxc_nand_data imx21_mxc_nand_data __initconst =
	imx_mxc_nand_data_entry_single(MX21, SZ_4K);
#endif /* ifdef CONFIG_SOC_IMX21 */

#ifdef CONFIG_ARCH_MX25
const struct imx_mxc_nand_data imx25_mxc_nand_data __initconst =
	imx_mxc_nand_data_entry_single(MX25, SZ_8K);
#endif /* ifdef CONFIG_ARCH_MX25 */

#ifdef CONFIG_SOC_IMX27
const struct imx_mxc_nand_data imx27_mxc_nand_data __initconst =
	imx_mxc_nand_data_entry_single(MX27, SZ_4K);
#endif /* ifdef CONFIG_SOC_IMX27 */

#ifdef CONFIG_ARCH_MX31
const struct imx_mxc_nand_data imx31_mxc_nand_data __initconst =
	imx_mxc_nand_data_entry_single(MX31, SZ_4K);
#endif

#ifdef CONFIG_ARCH_MX35
const struct imx_mxc_nand_data imx35_mxc_nand_data __initconst =
	imx_mxc_nand_data_entry_single(MX35, SZ_8K);
#endif

struct platform_device *__init imx_add_mxc_nand(
		const struct imx_mxc_nand_data *data,
		const struct mxc_nand_platform_data *pdata)
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
	return imx_add_platform_device("mxc_nand", 0, res, ARRAY_SIZE(res),
			pdata, sizeof(*pdata));
}
