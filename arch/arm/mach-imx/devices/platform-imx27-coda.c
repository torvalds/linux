/*
 * Copyright (C) 2012 Vista Silicon
 * Javier Martin <javier.martin@vista-silicon.com>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */

#include "../hardware.h"
#include "devices-common.h"

#ifdef CONFIG_SOC_IMX27
const struct imx_imx27_coda_data imx27_coda_data __initconst = {
	.iobase = MX27_VPU_BASE_ADDR,
	.iosize = SZ_512,
	.irq = MX27_INT_VPU,
};
#endif

struct platform_device *__init imx_add_imx27_coda(
		const struct imx_imx27_coda_data *data)
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
	return imx_add_platform_device_dmamask("coda-imx27", 0, res, 2, NULL,
					0, DMA_BIT_MASK(32));
}
