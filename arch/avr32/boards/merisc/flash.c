/*
 * Merisc board-specific flash initialization
 *
 * Copyright (C) 2008 Martinsson Elektronik AB
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <mach/smc.h>

/* Will be translated to units of 14.3 ns, rounded up */
static struct smc_timing flash_timing __initdata = {
	.ncs_read_setup		= 1 * 14,
	.nrd_setup		= 5 * 14,
	.ncs_write_setup	= 1 * 14,
	.nwe_setup		= 2 * 14,

	.ncs_read_pulse		= 12 * 14,
	.nrd_pulse		= 7 * 14,
	.ncs_write_pulse	= 8 * 14,
	.nwe_pulse		= 4 * 14,

	.read_cycle		= 14 * 14,
	.write_cycle		= 10 * 14,
};

static struct smc_config flash_config __initdata = {
	.bus_width	= 2,
	.nrd_controlled	= 1,
	.nwe_controlled	= 1,
	.byte_write	= 1,
	.tdf_cycles	= 3,
};

static struct mtd_partition flash_0_parts[] = {
	{
		.name		= "boot",
		.offset		= 0x00000000,
		.size		= 0x00060000,
		.mask_flags	= 0,
	},
	{
		.name		= "kernel",
		.offset		= 0x00060000,
		.size		= 0x00200000,
		.mask_flags	= 0,
	},
	{
		.name		= "root",
		.offset		= 0x00260000,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= 0,
	},
};

static struct mtd_partition flash_1_parts[] = {
	{
		.name		= "2ndflash",
		.offset		= 0x00000000,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= 0,
	},
};

static struct physmap_flash_data flash_data[] = {
	{
		.width		= 2,
		.nr_parts	= ARRAY_SIZE(flash_0_parts),
		.parts		= flash_0_parts,
	},
	{
		.width		= 2,
		.nr_parts	= ARRAY_SIZE(flash_1_parts),
		.parts		= flash_1_parts,
	}
};

static struct resource flash_resource[] = {
	{
		.start		= 0x00000000,
		.end		= 0x03ffffff,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= 0x04000000,
		.end		= 0x07ffffff,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device flash_device[] = {
	{
		.name		= "physmap-flash",
		.id		= 0,
		.resource	= &flash_resource[0],
		.num_resources	= 1,
		.dev		= {
			.platform_data	= &flash_data[0],
		},
	},
	{
		.name		= "physmap-flash",
		.id		= 1,
		.resource	= &flash_resource[1],
		.num_resources	= 1,
		.dev		= {
			.platform_data	= &flash_data[1],
		},
	},
};

static int __init merisc_flash_init(void)
{
	int ret;
	smc_set_timing(&flash_config, &flash_timing);

	ret = smc_set_configuration(0, &flash_config);
	if (ret < 0) {
		printk(KERN_ERR "Merisc: failed to set NOR flash timing #0\n");
		return ret;
	}

	ret = smc_set_configuration(4, &flash_config);
	if (ret < 0) {
		printk(KERN_ERR "Merisc: failed to set NOR flash timing #1\n");
		return ret;
	}

	platform_device_register(&flash_device[0]);
	platform_device_register(&flash_device[1]);
	return 0;
}
device_initcall(merisc_flash_init);
