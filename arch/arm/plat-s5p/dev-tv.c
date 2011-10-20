/* linux/arch/arm/plat-s5p/dev-tv.c
 *
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Author: Tomasz Stanislawski <t.stanislaws@samsung.com>
 *
 * S5P series device definition for TV device
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/dma-mapping.h>

#include <mach/irqs.h>
#include <mach/map.h>

#include <plat/devs.h>

/* HDMI interface */
static struct resource s5p_hdmi_resources[] = {
	[0] = {
		.start	= S5P_PA_HDMI,
		.end	= S5P_PA_HDMI + SZ_1M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_HDMI,
		.end	= IRQ_HDMI,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device s5p_device_hdmi = {
	.name		= "s5p-hdmi",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s5p_hdmi_resources),
	.resource	= s5p_hdmi_resources,
};
EXPORT_SYMBOL(s5p_device_hdmi);

/* SDO interface */
static struct resource s5p_sdo_resources[] = {
	[0] = {
		.start	= S5P_PA_SDO,
		.end	= S5P_PA_SDO + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_SDO,
		.end	= IRQ_SDO,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device s5p_device_sdo = {
	.name		= "s5p-sdo",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s5p_sdo_resources),
	.resource	= s5p_sdo_resources,
};
EXPORT_SYMBOL(s5p_device_sdo);

/* MIXER */
static struct resource s5p_mixer_resources[] = {
	[0] = {
		.start	= S5P_PA_MIXER,
		.end	= S5P_PA_MIXER + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
		.name	= "mxr"
	},
	[1] = {
		.start	= S5P_PA_VP,
		.end	= S5P_PA_VP + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
		.name	= "vp"
	},
	[2] = {
		.start	= IRQ_MIXER,
		.end	= IRQ_MIXER,
		.flags	= IORESOURCE_IRQ,
		.name	= "irq"
	}
};

static u64 s5p_tv_dmamask = DMA_BIT_MASK(32);

struct platform_device s5p_device_mixer = {
	.name		= "s5p-mixer",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s5p_mixer_resources),
	.resource	= s5p_mixer_resources,
	.dev		= {
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.dma_mask = &s5p_tv_dmamask,
	}
};
EXPORT_SYMBOL(s5p_device_mixer);
