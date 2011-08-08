/* linux/arch/arm/plat-samsung/dev-asocdma.c
 *
 * Copyright (c) 2010 Samsung Electronics Co. Ltd
 *	Jaswinder Singh <jassi.brar@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <plat/devs.h>

static u64 audio_dmamask = DMA_BIT_MASK(32);

struct platform_device samsung_asoc_dma = {
	.name		  = "samsung-audio",
	.id		  = -1,
	.dev              = {
		.dma_mask = &audio_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	}
};
EXPORT_SYMBOL(samsung_asoc_dma);

struct platform_device samsung_asoc_idma = {
	.name		= "samsung-idma",
	.id		= -1,
	.dev		= {
		.dma_mask		= &audio_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	}
};
EXPORT_SYMBOL(samsung_asoc_idma);
