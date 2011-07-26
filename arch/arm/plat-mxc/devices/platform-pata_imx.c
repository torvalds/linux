/*
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <mach/hardware.h>
#include <mach/devices-common.h>

#define imx_pata_imx_data_entry_single(soc)				\
	{								\
		.iobase = soc ## _ATA_BASE_ADDR,			\
		.irq = soc ## _MXC_INT_ATA,				\
	}

#ifdef CONFIG_SOC_IMX51
const struct imx_pata_imx_data imx51_pata_imx_data __initconst =
	imx_pata_imx_data_entry_single(MX51);
#endif /* ifdef CONFIG_SOC_IMX51 */

struct platform_device *__init imx_add_pata_imx(
		const struct imx_pata_imx_data *data)
{
	struct resource res[] = {
		{
			.start = data->iobase,
			.end = data->iobase + SZ_16K - 1,
			.flags = IORESOURCE_MEM,
		},
		{
			.start = data->irq,
			.end = data->irq,
			.flags = IORESOURCE_IRQ,
		},
	};
	return imx_add_platform_device("pata_imx", -1,
			res, ARRAY_SIZE(res), NULL, 0);
}

