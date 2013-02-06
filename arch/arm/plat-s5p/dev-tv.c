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

#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/hdmi.h>
#include <plat/tvout.h>

/* HDMI interface */
static struct resource s5p_hdmi_resources[] = {
	[0] = {
		.start	= S5P_PA_HDMI,
		.end	= S5P_PA_HDMI + SZ_1M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_TVOUT_HPD,
		.end	= IRQ_TVOUT_HPD,
		.flags	= IORESOURCE_IRQ,
		.name	= "external_irq"
	},
	[2] = {
		.start	= IRQ_HDMI,
		.end	= IRQ_HDMI,
		.flags	= IORESOURCE_IRQ,
		.name	= "internal_irq"
	},
};

struct platform_device s5p_device_hdmi = {
	.name		= "s5p-hdmi",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s5p_hdmi_resources),
	.resource	= s5p_hdmi_resources,
};
EXPORT_SYMBOL(s5p_device_hdmi);

void __init s5p_hdmi_set_platdata(struct s5p_hdmi_platdata *pd)
{
	struct s5p_hdmi_platdata *npd;

	npd = s3c_set_platdata(pd, sizeof(struct s5p_hdmi_platdata),
			&s5p_device_hdmi);
	if (!npd)
		return;

	if (soc_is_s5pv210() || soc_is_exynos4210())
		npd->is_v13 = true;

	if (!npd->cfg_hpd)
		npd->cfg_hpd = s5p_hdmi_cfg_hpd;
	if (!npd->get_hpd)
		npd->get_hpd = s5p_hdmi_get_hpd;
}

/* MIXER */
#if defined(CONFIG_ARCH_EXYNOS4)
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
#else
static struct resource s5p_mixer_resources[] = {
	[0] = {
		.start	= S5P_PA_MIXER,
		.end	= S5P_PA_MIXER + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
		.name	= "mxr"
	},
	[2] = {
		.start	= IRQ_MIXER,
		.end	= IRQ_MIXER,
		.flags	= IORESOURCE_IRQ,
		.name	= "irq"
	}
};
#endif

struct platform_device s5p_device_mixer = {
	.name		= "s5p-mixer",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s5p_mixer_resources),
	.resource	= s5p_mixer_resources,
	.dev		= {
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.dma_mask = &s5p_device_mixer.dev.coherent_dma_mask,
	}
};
EXPORT_SYMBOL(s5p_device_mixer);

#if defined(CONFIG_ARCH_EXYNOS4)
/* HDMI interface */
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
	.dev		= {
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.dma_mask = &s5p_device_sdo.dev.coherent_dma_mask,
	}
};
EXPORT_SYMBOL(s5p_device_sdo);
#endif

/* CEC */
static struct resource s5p_cec_resources[] = {
	[0] = {
		.start  = S5P_PA_HDMI_CEC,
		.end    = S5P_PA_HDMI_CEC + S5P_SZ_HDMI_CEC - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = IRQ_CEC,
		.end    = IRQ_CEC,
		.flags  = IORESOURCE_IRQ,
	},
};

struct platform_device s5p_device_cec = {
	.name           = "s5p-tvout-cec",
	.id             = -1,
	.num_resources  = ARRAY_SIZE(s5p_cec_resources),
	.resource       = s5p_cec_resources,
};
EXPORT_SYMBOL(s5p_device_cec);


void __init s5p_hdmi_cec_set_platdata(struct s5p_platform_cec *pd)
{
	struct s5p_platform_cec *npd;

	npd = kmemdup(pd, sizeof(struct s5p_platform_cec), GFP_KERNEL);
	if (!npd)
		printk(KERN_ERR "%s: no memory for platform data\n", __func__);
	else {
		if (!npd->cfg_gpio)
			npd->cfg_gpio = s5p_cec_cfg_gpio;

		s5p_device_cec.dev.platform_data = npd;
	}
}
