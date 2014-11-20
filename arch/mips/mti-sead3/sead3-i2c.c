/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/init.h>
#include <linux/platform_device.h>

struct resource sead3_i2c_resources[] = {
	{
		.start	= 0x805200,
		.end	= 0x8053ff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device sead3_i2c_device = {
	.name		= "sead3-i2c",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(sead3_i2c_resources),
	.resource	= sead3_i2c_resources,
};

static int __init sead3_i2c_init(void)
{
	return platform_device_register(&sead3_i2c_device);
}

device_initcall(sead3_i2c_init);
