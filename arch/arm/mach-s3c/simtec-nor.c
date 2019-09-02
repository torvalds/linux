// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2008 Simtec Electronics
//	http://armlinux.simtec.co.uk/
//	Ben Dooks <ben@simtec.co.uk>
//
// Simtec NOR mapping

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

#include "map.h"

#include "bast.h"
#include "simtec.h"

static void simtec_nor_vpp(struct platform_device *pdev, int vpp)
{
	unsigned int val;

	val = __raw_readb(BAST_VA_CTRL3);

	printk(KERN_DEBUG "%s(%d)\n", __func__, vpp);

	if (vpp)
		val |= BAST_CPLD_CTRL3_ROMWEN;
	else
		val &= ~BAST_CPLD_CTRL3_ROMWEN;

	__raw_writeb(val, BAST_VA_CTRL3);
}

static struct physmap_flash_data simtec_nor_pdata = {
	.width		= 2,
	.set_vpp	= simtec_nor_vpp,
	.nr_parts	= 0,
};

static struct resource simtec_nor_resource[] = {
	[0] = DEFINE_RES_MEM(S3C2410_CS1 + 0x4000000, SZ_8M),
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
