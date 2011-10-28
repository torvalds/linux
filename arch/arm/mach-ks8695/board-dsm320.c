/*
 * arch/arm/mach-ks8695/board-dsm320.c
 *
 * DSM-320 D-Link Wireless Media Player, board support.
 *
 * Copyright 2008 Simtec Electronics
 *		  Daniel Silverstone <dsilvers@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/partitions.h>

#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/devices.h>
#include <mach/gpio.h>

#include "generic.h"

#ifdef CONFIG_PCI
static int dsm320_pci_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	switch (slot) {
	case 0:
		/* PCI-AHB bridge? */
		return KS8695_IRQ_EXTERN0;
	case 18:
		/* Mini PCI slot */
		return KS8695_IRQ_EXTERN2;
	case 20:
		/* RealMAGIC chip */
		return KS8695_IRQ_EXTERN0;
	}
	BUG();
}

static struct ks8695_pci_cfg __initdata dsm320_pci = {
	.mode		= KS8695_MODE_MINIPCI,
	.map_irq	= dsm320_pci_map_irq,
};

static void __init dsm320_register_pci(void)
{
	/* Initialise the GPIO lines for interrupt mode */
	/* RealMAGIC */
	ks8695_gpio_interrupt(KS8695_GPIO_0, IRQ_TYPE_LEVEL_LOW);
	/* MiniPCI Slot */
	ks8695_gpio_interrupt(KS8695_GPIO_2, IRQ_TYPE_LEVEL_LOW);

	ks8695_init_pci(&dsm320_pci);
}

#else
static inline void __init dsm320_register_pci(void) { }
#endif

static struct physmap_flash_data dsm320_nor_pdata = {
	.width		= 4,
	.nr_parts	= 0,
};

static struct resource dsm320_nor_resource[] = {
	[0] = {
		.start = SZ_32M, /* We expect the bootloader to map
				  * the flash here.
				  */
		.end   = SZ_32M + SZ_4M - 1,
		.flags = IORESOURCE_MEM,
	}
};

static struct platform_device dsm320_device_nor = {
	.name		= "physmap-flash",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(dsm320_nor_resource),
	.resource	= dsm320_nor_resource,
	.dev		= {
		.platform_data = &dsm320_nor_pdata,
	},
};

void __init dsm320_register_nor(void)
{
	int ret;

	ret = platform_device_register(&dsm320_device_nor);
	if (ret < 0)
		printk(KERN_ERR "failed to register physmap-flash device\n");
}

static void __init dsm320_init(void)
{
	/* GPIO registration */
	ks8695_register_gpios();

	/* PCI registration */
	dsm320_register_pci();

	/* Network device */
	ks8695_add_device_lan();	/* eth0 = LAN */

	/* NOR devices */
	dsm320_register_nor();
}

MACHINE_START(DSM320, "D-Link DSM-320 Wireless Media Player")
	/* Maintainer: Simtec Electronics. */
	.boot_params	= KS8695_SDRAM_PA + 0x100,
	.map_io		= ks8695_map_io,
	.init_irq	= ks8695_init_irq,
	.init_machine	= dsm320_init,
	.timer		= &ks8695_timer,
MACHINE_END
