/* linux/arch/arm/plat-s5p/dev-fimd0.c
 *
 * Copyright (c) 2009-2011 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com
 *
 * Core file for Samsung Display Controller (FIMD) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/gfp.h>
#include <linux/dma-mapping.h>

#include <mach/irqs.h>
#include <mach/map.h>

#include <plat/fb.h>
#include <plat/devs.h>
#include <plat/cpu.h>

static struct resource s5p_fimd0_resource[] = {
	[0] = {
		.start  = S5P_PA_FIMD0,
		.end    = S5P_PA_FIMD0 + SZ_32K - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = IRQ_FIMD0_VSYNC,
		.end    = IRQ_FIMD0_VSYNC,
		.flags  = IORESOURCE_IRQ,
	},
	[2] = {
		.start  = IRQ_FIMD0_FIFO,
		.end    = IRQ_FIMD0_FIFO,
		.flags  = IORESOURCE_IRQ,
	},
	[3] = {
		.start  = IRQ_FIMD0_SYSTEM,
		.end    = IRQ_FIMD0_SYSTEM,
		.flags  = IORESOURCE_IRQ,
	},
};

static u64 fimd0_dmamask = DMA_BIT_MASK(32);

struct platform_device s5p_device_fimd0 = {
	.name           = "s5p-fb",
	.id             = 0,
	.num_resources  = ARRAY_SIZE(s5p_fimd0_resource),
	.resource       = s5p_fimd0_resource,
	.dev            = {
		.dma_mask               = &fimd0_dmamask,
		.coherent_dma_mask      = DMA_BIT_MASK(32),
	},
};

void __init s5p_fimd0_set_platdata(struct s3c_fb_platdata *pd)
{
	s3c_set_platdata(pd, sizeof(struct s3c_fb_platdata),
			&s5p_device_fimd0);
}
