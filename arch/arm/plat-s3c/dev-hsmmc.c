/* linux/arch/arm/plat-s3c/dev-hsmmc.c
 *
 * Copyright (c) 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * S3C series device definition for hsmmc devices
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <mach/map.h>
#include <plat/devs.h>
#include <plat/cpu.h>

#define S3C_SZ_HSMMC	(0x1000)

static struct resource s3c_hsmmc_resource[] = {
	[0] = {
		.start = S3C_PA_HSMMC0,
		.end   = S3C_PA_HSMMC0 + S3C_SZ_HSMMC - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_HSMMC0,
		.end   = IRQ_HSMMC0,
		.flags = IORESOURCE_IRQ,
	}
};

static u64 s3c_device_hsmmc_dmamask = 0xffffffffUL;

struct platform_device s3c_device_hsmmc0 = {
	.name		  = "s3c-sdhci",
	.id		  = 0,
	.num_resources	  = ARRAY_SIZE(s3c_hsmmc_resource),
	.resource	  = s3c_hsmmc_resource,
	.dev              = {
		.dma_mask = &s3c_device_hsmmc_dmamask,
		.coherent_dma_mask = 0xffffffffUL
	}
};
