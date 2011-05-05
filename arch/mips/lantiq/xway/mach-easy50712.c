/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2010 John Crispin <blogic@openwrt.org>
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/input.h>
#include <linux/phy.h>

#include <lantiq_soc.h>
#include <irq.h>

#include "../machtypes.h"
#include "devices.h"

static struct mtd_partition easy50712_partitions[] = {
	{
		.name	= "uboot",
		.offset	= 0x0,
		.size	= 0x10000,
	},
	{
		.name	= "uboot_env",
		.offset	= 0x10000,
		.size	= 0x10000,
	},
	{
		.name	= "linux",
		.offset	= 0x20000,
		.size	= 0xe0000,
	},
	{
		.name	= "rootfs",
		.offset	= 0x100000,
		.size	= 0x300000,
	},
};

static struct physmap_flash_data easy50712_flash_data = {
	.nr_parts	= ARRAY_SIZE(easy50712_partitions),
	.parts		= easy50712_partitions,
};

static struct ltq_pci_data ltq_pci_data = {
	.clock	= PCI_CLOCK_INT,
	.gpio	= PCI_GNT1 | PCI_REQ1,
	.irq	= {
		[14] = INT_NUM_IM0_IRL0 + 22,
	},
};

static struct ltq_eth_data ltq_eth_data = {
	.mii_mode = PHY_INTERFACE_MODE_MII,
};

static void __init easy50712_init(void)
{
	ltq_register_gpio_stp();
	ltq_register_nor(&easy50712_flash_data);
	ltq_register_pci(&ltq_pci_data);
	ltq_register_etop(&ltq_eth_data);
}

MIPS_MACHINE(LTQ_MACH_EASY50712,
	     "EASY50712",
	     "EASY50712 Eval Board",
	      easy50712_init);
