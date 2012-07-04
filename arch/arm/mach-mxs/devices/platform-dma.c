/*
 * Copyright 2011 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <linux/compiler.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/init.h>

#include <mach/mx23.h>
#include <mach/mx28.h>
#include <mach/devices-common.h>

struct platform_device *__init mxs_add_dma(const char *devid,
						resource_size_t base)
{
	struct resource res[] = {
		{
			.start = base,
			.end = base + SZ_8K - 1,
			.flags = IORESOURCE_MEM,
		}
	};

	return mxs_add_platform_device_dmamask(devid, -1,
				res, ARRAY_SIZE(res), NULL, 0,
				DMA_BIT_MASK(32));
}
