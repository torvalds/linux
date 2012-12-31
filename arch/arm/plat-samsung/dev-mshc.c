/* linux/arch/arm/plat-samsung/dev-mshc.c
 *
 * Copyright (c) 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * Based on arch/arm/plat-samsung/dev-hsmmc1.c
 *
 * Device definition for mshc devices
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/mmc/host.h>

#include <mach/map.h>
#include <plat/mshci.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/exynos4.h>

#define S5P_SZ_MSHC	(0x1000)

static struct resource s3c_mshci_resource[] = {
	[0] = {
		.start = EXYNOS_PA_DWMCI,
		.end   = EXYNOS_PA_DWMCI + S5P_SZ_MSHC - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_DWMCI,
		.end   = IRQ_DWMCI,
		.flags = IORESOURCE_IRQ,
	}
};

static u64 s3c_device_hsmmc_dmamask = 0xffffffffUL;

struct s3c_mshci_platdata s3c_mshci_def_platdata = {
	.max_width	= 4,
	.host_caps	= (MMC_CAP_4_BIT_DATA |
			   MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED),
};

struct platform_device s3c_device_mshci = {
	.name		= "dw_mmc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s3c_mshci_resource),
	.resource	= s3c_mshci_resource,
	.dev		= {
		.dma_mask		= &s3c_device_hsmmc_dmamask,
		.coherent_dma_mask	= 0xffffffffUL,
		.platform_data		= &s3c_mshci_def_platdata,
	},
};


void s3c_mshci_set_platdata(struct s3c_mshci_platdata *pd)
{
	struct s3c_mshci_platdata *set = &s3c_mshci_def_platdata;

	set->cd_type = pd->cd_type;
	set->ext_cd_init = pd->ext_cd_init;
	set->ext_cd_cleanup = pd->ext_cd_cleanup;
	set->ext_cd_gpio = pd->ext_cd_gpio;
	set->ext_cd_gpio_invert = pd->ext_cd_gpio_invert;
	set->wp_gpio = pd->wp_gpio;
	set->has_wp_gpio = pd->has_wp_gpio;
	set->int_power_gpio = pd->int_power_gpio;
	if(pd->fifo_depth)
		set->fifo_depth = pd->fifo_depth;
	else
		set->fifo_depth = 0x20; /* exynos4210 size. */

	if (pd->max_width)
		set->max_width = pd->max_width;
	if (pd->host_caps)
		set->host_caps |= pd->host_caps;
	if (pd->host_caps2)
		set->host_caps2 |= pd->host_caps2;
	if (soc_is_exynos4210()) {
		if (pd->host_caps && samsung_rev() < EXYNOS4210_REV_1_1) {
			printk(KERN_INFO "MSHC: This exynos4 is EVT1.0. "
				"Disable DDR R/W for eMMC.\n");
			set->host_caps &= ~(MMC_CAP_1_8V_DDR |
						MMC_CAP_UHS_DDR50);
		}
	}
	if (pd->cfg_gpio)
		set->cfg_gpio = pd->cfg_gpio;
	if (pd->cfg_card)
		set->cfg_card = pd->cfg_card;
	if (pd->cfg_ddr)
		set->cfg_ddr = pd->cfg_ddr;
	if (pd->init_card)
		set->init_card = pd->init_card;
}
