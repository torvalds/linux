/*
 * Copyright (C) 2010-2011 Samsung Electronics Co., Ltd.
 *
 * S5P series device definition for TSI
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <mach/map.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>

/*TSI Interface*/
static u64 tsi_dma_mask = 0xffffffffUL;

static struct resource s3c_tsi_resource[] = {
	[0] = {
		.start = S5P_PA_TSI,
		.end   = S5P_PA_TSI + S5P_SZ_TSI - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_TSI,
		.end   = IRQ_TSI,
		.flags = IORESOURCE_IRQ,
	}
};

struct platform_device s3c_device_tsi = {
	.name	= "s3c-tsi",
	.id	= -1,
	.num_resources	= ARRAY_SIZE(s3c_tsi_resource),
	.resource	= s3c_tsi_resource,
	.dev	= {
		.dma_mask		= &tsi_dma_mask,
		.coherent_dma_mask	= 0xffffffffUL
	}


};
EXPORT_SYMBOL(s3c_device_tsi);
