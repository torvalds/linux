/*
 *  linux/arch/arm/mach-pxa/colibri-pxa270.c
 *
 *  Support for Toradex PXA270 based Colibri module
 *  Daniel Mack <daniel@caiaq.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/sysdev.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/gpio.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/sizes.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/flash.h>

#include <mach/pxa27x.h>
#include <mach/colibri.h>

#include "generic.h"
#include "devices.h"

/*
 * GPIO configuration
 */
static mfp_cfg_t colibri_pxa270_pin_config[] __initdata = {
	GPIO78_nCS_2,	/* Ethernet CS */
	GPIO114_GPIO,	/* Ethernet IRQ */
};

/*
 * NOR flash
 */
static struct mtd_partition colibri_partitions[] = {
	{
		.name =		"Bootloader",
		.offset =	0x00000000,
		.size =		0x00040000,
		.mask_flags =	MTD_WRITEABLE  /* force read-only */
	}, {
		.name =		"Kernel",
		.offset =	0x00040000,
		.size =		0x00400000,
		.mask_flags =	0
	}, {
		.name =		"Rootfs",
		.offset =	0x00440000,
		.size =		MTDPART_SIZ_FULL,
		.mask_flags =	0
	}
};

static struct physmap_flash_data colibri_flash_data[] = {
	{
		.width		= 4,			/* bankwidth in bytes */
		.parts		= colibri_partitions,
		.nr_parts	= ARRAY_SIZE(colibri_partitions)
	}
};

static struct resource colibri_pxa270_flash_resource = {
	.start	= PXA_CS0_PHYS,
	.end	= PXA_CS0_PHYS + SZ_32M - 1,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device colibri_pxa270_flash_device = {
	.name	= "physmap-flash",
	.id	= 0,
	.dev 	= {
		.platform_data = colibri_flash_data,
	},
	.resource = &colibri_pxa270_flash_resource,
	.num_resources = 1,
};

/*
 * DM9000 Ethernet
 */
#if defined(CONFIG_DM9000)
static struct resource dm9000_resources[] = {
	[0] = {
		.start	= COLIBRI_PXA270_ETH_PHYS,
		.end	= COLIBRI_PXA270_ETH_PHYS + 3,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= COLIBRI_PXA270_ETH_PHYS + 4,
		.end	= COLIBRI_PXA270_ETH_PHYS + 4 + 500,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= COLIBRI_PXA270_ETH_IRQ,
		.end	= COLIBRI_PXA270_ETH_IRQ,
		.flags	= IORESOURCE_IRQ | IRQF_TRIGGER_RISING,
	},
};

static struct platform_device dm9000_device = {
	.name		= "dm9000",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(dm9000_resources),
	.resource	= dm9000_resources,
};
#endif /* CONFIG_DM9000 */

static struct platform_device *colibri_pxa270_devices[] __initdata = {
	&colibri_pxa270_flash_device,
#if defined(CONFIG_DM9000)
	&dm9000_device,
#endif
};

static void __init colibri_pxa270_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa270_pin_config));
	platform_add_devices(ARRAY_AND_SIZE(colibri_pxa270_devices));
}

MACHINE_START(COLIBRI, "Toradex Colibri PXA270")
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params	= COLIBRI_SDRAM_BASE + 0x100,
	.init_machine	= colibri_pxa270_init,
	.map_io		= pxa_map_io,
	.init_irq	= pxa27x_init_irq,
	.timer		= &pxa_timer,
MACHINE_END

