/*
 * arch/arm/mach-ep93xx/gesbc9312.c
 * Glomation GESBC-9312-sx support.
 *
 * Copyright (C) 2006 Lennert Buytenhek <buytenh@wantstofly.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/mtd/physmap.h>
#include <linux/platform_device.h>
#include <asm/io.h>
#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

static struct physmap_flash_data gesbc9312_flash_data = {
	.width		= 4,
};

static struct resource gesbc9312_flash_resource = {
	.start		= 0x60000000,
	.end		= 0x607fffff,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device gesbc9312_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &gesbc9312_flash_data,
	},
	.num_resources	= 1,
	.resource	= &gesbc9312_flash_resource,
};

static struct ep93xx_eth_data gesbc9312_eth_data = {
	.phy_id			= 1,
};

static struct resource gesbc9312_eth_resource[] = {
	{
		.start	= EP93XX_ETHERNET_PHYS_BASE,
		.end	= EP93XX_ETHERNET_PHYS_BASE + 0xffff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_EP93XX_ETHERNET,
		.end	= IRQ_EP93XX_ETHERNET,
		.flags	= IORESOURCE_IRQ,
	}
};

static struct platform_device gesbc9312_eth_device = {
	.name		= "ep93xx-eth",
	.id		= -1,
	.dev		= {
		.platform_data	= &gesbc9312_eth_data,
	},
	.num_resources	= 2,
	.resource	= gesbc9312_eth_resource,
};

static void __init gesbc9312_init_machine(void)
{
	ep93xx_init_devices();
	platform_device_register(&gesbc9312_flash);
	platform_device_register(&gesbc9312_eth_device);
}

MACHINE_START(GESBC9312, "Glomation GESBC-9312-sx")
	/* Maintainer: Lennert Buytenhek <buytenh@wantstofly.org> */
	.phys_io	= EP93XX_APB_PHYS_BASE,
	.io_pg_offst	= ((EP93XX_APB_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= 0x00000100,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.timer		= &ep93xx_timer,
	.init_machine	= gesbc9312_init_machine,
MACHINE_END
