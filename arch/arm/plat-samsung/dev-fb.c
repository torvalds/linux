/* linux/arch/arm/plat-s3c/dev-fb.c
 *
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * S3C series device definition for framebuffer device
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

#include <mach/irqs.h>
#include <mach/map.h>

#include <plat/fb.h>
#include <plat/devs.h>
#include <plat/cpu.h>

static struct resource s3c_fb_resource[] = {
	[0] = {
		.start = S3C_PA_FB,
		.end   = S3C_PA_FB + SZ_16K - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_LCD_VSYNC,
		.end   = IRQ_LCD_VSYNC,
		.flags = IORESOURCE_IRQ,
	},
	[2] = {
		.start = IRQ_LCD_FIFO,
		.end   = IRQ_LCD_FIFO,
		.flags = IORESOURCE_IRQ,
	},
	[3] = {
		.start = IRQ_LCD_SYSTEM,
		.end   = IRQ_LCD_SYSTEM,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device s3c_device_fb = {
	.name		  = "s3c-fb",
	.id		  = -1,
	.num_resources	  = ARRAY_SIZE(s3c_fb_resource),
	.resource	  = s3c_fb_resource,
	.dev.dma_mask	  = &s3c_device_fb.dev.coherent_dma_mask,
	.dev.coherent_dma_mask = 0xffffffffUL,
};

void __init s3c_fb_set_platdata(struct s3c_fb_platdata *pd)
{
	struct s3c_fb_platdata *npd;

	if (!pd) {
		printk(KERN_ERR "%s: no platform data\n", __func__);
		return;
	}

	npd = kmemdup(pd, sizeof(struct s3c_fb_platdata), GFP_KERNEL);
	if (!npd)
		printk(KERN_ERR "%s: no memory for platform data\n", __func__);

	s3c_device_fb.dev.platform_data = npd;
}
