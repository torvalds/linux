/* linux/arch/arm/plat-s5p/dev-fimg2d.c
 *
 * Copyright (c) 2010 Samsung Electronics
 *
 * Base S5P FIMG2D resource and device definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <plat/fimg2d.h>
#include <plat/cpu.h>
#include <mach/map.h>

#define S5P_PA_FIMG2D_OFFSET	0x02000000
#define S5P_PA_FIMG2D_3X	(S5P_PA_FIMG2D+S5P_PA_FIMG2D_OFFSET)

static struct resource s5p_fimg2d_resource[] = {
	[0] = {
		.start	= S5P_PA_FIMG2D,
		.end	= S5P_PA_FIMG2D + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_2D,
		.end	= IRQ_2D,
		.flags	= IORESOURCE_IRQ,
	}
};

static u64 s5p_device_fimg2d_dmamask = 0xffffffffUL;

struct platform_device s5p_device_fimg2d = {
	.name		= "s5p-fimg2d",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s5p_fimg2d_resource),
	.resource	= s5p_fimg2d_resource,
	.dev		= {
		.dma_mask		= &s5p_device_fimg2d_dmamask,
		.coherent_dma_mask	= 0xffffffffUL,
	},
};
EXPORT_SYMBOL(s5p_device_fimg2d);

static struct fimg2d_platdata default_fimg2d_data __initdata = {
	.parent_clkname = "mout_g2d0",
	.clkname = "sclk_fimg2d",
	.gate_clkname = "fimg2d",
	.clkrate = 250 * 1000000,
};

void __init s5p_fimg2d_set_platdata(struct fimg2d_platdata *pd)
{
	struct fimg2d_platdata *npd;

	if (soc_is_exynos4210()) {
		s5p_fimg2d_resource[0].start = S5P_PA_FIMG2D_3X;
		s5p_fimg2d_resource[0].end = S5P_PA_FIMG2D_3X + SZ_4K - 1;
	}

	if (!pd)
		pd = &default_fimg2d_data;

	npd = kmemdup(pd, sizeof(*pd), GFP_KERNEL);
	if (!npd)
		printk(KERN_ERR "no memory for fimg2d platform data\n");
	else
		s5p_device_fimg2d.dev.platform_data = npd;
}
