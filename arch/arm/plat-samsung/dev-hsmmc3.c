/* linux/arch/arm/plat-samsung/dev-hsmmc3.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Copyright (c) 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * Based on arch/arm/plat-samsung/dev-hsmmc1.c
 *
 * Samsung device definition for hsmmc device 3
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/mmc/host.h>

#include <mach/map.h>
#include <plat/sdhci.h>
#include <plat/devs.h>

#define S3C_SZ_HSMMC	(0x1000)

static struct resource s3c_hsmmc3_resource[] = {
	[0] = {
		.start	= S3C_PA_HSMMC3,
		.end	= S3C_PA_HSMMC3 + S3C_SZ_HSMMC - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_HSMMC3,
		.end	= IRQ_HSMMC3,
		.flags	= IORESOURCE_IRQ,
	}
};

static u64 s3c_device_hsmmc3_dmamask = 0xffffffffUL;

struct s3c_sdhci_platdata s3c_hsmmc3_def_platdata = {
	.max_width	= 4,
	.host_caps	= (MMC_CAP_4_BIT_DATA |
			   MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED),
	.clk_type	= S3C_SDHCI_CLK_DIV_INTERNAL,
};

struct platform_device s3c_device_hsmmc3 = {
	.name		= "s3c-sdhci",
	.id		= 3,
	.num_resources	= ARRAY_SIZE(s3c_hsmmc3_resource),
	.resource	= s3c_hsmmc3_resource,
	.dev		= {
		.dma_mask		= &s3c_device_hsmmc3_dmamask,
		.coherent_dma_mask	= 0xffffffffUL,
		.platform_data		= &s3c_hsmmc3_def_platdata,
	},
};

void s3c_sdhci3_set_platdata(struct s3c_sdhci_platdata *pd)
{
	s3c_sdhci_set_platdata(pd, &s3c_hsmmc3_def_platdata);
}
