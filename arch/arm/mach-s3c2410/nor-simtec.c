/* linux/arch/arm/mach-s3c2410/nor-simtec.c
 *
 * Copyright (c) 2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Simtec NOR mapping
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/partitions.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/map.h>
#include <mach/bast-map.h>
#include <mach/bast-cpld.h>


static void simtec_nor_vpp(struct map_info *map, int vpp)
{
	unsigned int val;
	unsigned long flags;

	local_irq_save(flags);
	val = __raw_readb(BAST_VA_CTRL3);

	printk(KERN_DEBUG "%s(%d)\n", __func__, vpp);

	if (vpp)
		val |= BAST_CPLD_CTRL3_ROMWEN;
	else
		val &= ~BAST_CPLD_CTRL3_ROMWEN;

	__raw_writeb(val, BAST_VA_CTRL3);
	local_irq_restore(flags);
}

struct physmap_flash_data simtec_nor_pdata = {
	.width		= 2,
	.set_vpp	= simtec_nor_vpp,
	.nr_parts	= 0,
};

static struct resource simtec_nor_resource[] = {
	[0] = {
		.start = S3C2410_CS1 + 0x4000000,
		.end   = S3C2410_CS1 + 0x4000000 + SZ_8M - 1,
		.flags = IORESOURCE_MEM,
	}
};

static struct platform_device simtec_device_nor = {
	.name		= "physmap-flash",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(simtec_nor_resource),
	.resource	= simtec_nor_resource,
	.dev		= {
		.platform_data = &simtec_nor_pdata,
	},
};

void __init nor_simtec_init(void)
{
	int ret;

	ret = platform_device_register(&simtec_device_nor);
	if (ret < 0)
		printk(KERN_ERR "failed to register physmap-flash device\n");
	else
		simtec_nor_vpp(NULL, 1);
}
