/* linux/arch/arm/plat-samsung/dev-dp.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com
 *
 * Base Samsung SoC DP resource and device definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <video/s5p-dp.h>
#include <mach/map.h>
#include <plat/devs.h>
#include <plat/dp.h>

static struct resource s5p_dp_resource[] = {
	[0] = {
		.start	= S5P_PA_DP,
		.end	= S5P_PA_DP + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_DP,
		.end	= IRQ_DP,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 s5p_dp_dma_mask = DMA_BIT_MASK(32);

struct platform_device s5p_device_dp = {
	.name		= "s5p-dp",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s5p_dp_resource),
	.resource	= s5p_dp_resource,
	.dev		= {
		.dma_mask		= &s5p_dp_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

void __init s5p_dp_set_platdata(struct s5p_dp_platdata *pd)
{
	s3c_set_platdata(pd, sizeof(struct s5p_dp_platdata),
			&s5p_device_dp);
}
