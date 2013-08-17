/*
 * MIMC200 board-specific flash initialization
 *
 * Copyright (C) 2008 Mercury IMC Ltd
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

static struct smc_timing flash_timing __initdata = {
	.ncs_read_setup		= 0,
	.nrd_setup		= 15,
	.ncs_write_setup	= 0,
	.nwe_setup		= 0,

	.ncs_read_pulse		= 115,
	.nrd_pulse		= 110,
	.ncs_write_pulse	= 60,
	.nwe_pulse		= 60,

	.read_cycle		= 115,
	.write_cycle		= 100,
};

static struct smc_config flash_config __initdata = {
	.bus_width		= 2,
	.nrd_controlled		= 1,
	.nwe_controlled		= 1,
	.byte_write		= 1,
};

/* system flash definition */

static struct mtd_partition flash_parts_system[] = {
	{
		.name           = "u-boot",
		.offset         = 0x00000000,
		.size           = 0x00020000,           /* 128 KiB */
		.mask_flags     = MTD_WRITEABLE,
	},
	{
		.name           = "root",
		.offset         = 0x00020000,
		.size           = 0x007c0000,
	},
	{
		.name		= "splash",
		.offset		= 0x007e0000,
		.size		= 0x00010000,		/* 64KiB */
	},
	{
		.name           = "env",
		.offset         = 0x007f0000,
		.size           = 0x00010000,
		.mask_flags     = MTD_WRITEABLE,
	},
};

static struct physmap_flash_data flash_system = {
	.width		= 2,
	.nr_parts	= ARRAY_SIZE(flash_parts_system),
	.parts		= flash_parts_system,
};

static struct resource flash_resource_system = {
	.start		= 0x00000000,
	.end		= 0x007fffff,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device flash_device_system = {
	.name		= "physmap-flash",
	.id		= 0,
	.resource	= &flash_resource_system,
	.num_resources	= 1,
	.dev		= {
		.platform_data = &flash_system,
	},
};

/* data flash definition */

static struct mtd_partition flash_parts_data[] = {
	{
		.name		= "data",
		.offset		= 0x00000000,
		.size		= 0x00800000,
	},
};

static struct physmap_flash_data flash_data = {
	.width		= 2,
	.nr_parts	= ARRAY_SIZE(flash_parts_data),
	.parts		= flash_parts_data,
};

static struct resource flash_resource_data = {
	.start		= 0x08000000,
	.end		= 0x087fffff,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device flash_device_data = {
	.name		= "physmap-flash",
	.id		= 1,
	.resource	= &flash_resource_data,
	.num_resources	= 1,
	.dev		= {
		.platform_data = &flash_data,
	},
};

/* This needs to be called after the SMC has been initialized */
static int __init mimc200_flash_init(void)
{
	int ret;

	smc_set_timing(&flash_config, &flash_timing);
	ret = smc_set_configuration(0, &flash_config);
	if (ret < 0) {
		printk(KERN_ERR "mimc200: failed to set 'System' NOR flash timing\n");
		return ret;
	}
	ret = smc_set_configuration(1, &flash_config);
	if (ret < 0) {
		printk(KERN_ERR "mimc200: failed to set 'Data' NOR flash timing\n");
		return ret;
	}

	platform_device_register(&flash_device_system);
	platform_device_register(&flash_device_data);

	return 0;
}
device_initcall(mimc200_flash_init);
