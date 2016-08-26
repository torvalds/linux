/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/platform_device.h>

#define LEDFLAGS(bits, shift)		\
	((bits << 8) | (shift << 8))

#define LEDBITS(id, shift, bits)	\
	.name = id #shift,		\
	.flags = LEDFLAGS(bits, shift)

static struct led_info led_data_info[] = {
	{ LEDBITS("bit", 0, 1) },
	{ LEDBITS("bit", 1, 1) },
	{ LEDBITS("bit", 2, 1) },
	{ LEDBITS("bit", 3, 1) },
	{ LEDBITS("bit", 4, 1) },
	{ LEDBITS("bit", 5, 1) },
	{ LEDBITS("bit", 6, 1) },
	{ LEDBITS("bit", 7, 1) },
	{ LEDBITS("all", 0, 8) },
};

static struct led_platform_data led_data = {
	.num_leds	= ARRAY_SIZE(led_data_info),
	.leds		= led_data_info
};

static struct resource pled_resources[] = {
	{
		.start	= 0x1f000210,
		.end	= 0x1f000217,
		.flags	= IORESOURCE_MEM
	}
};

static struct platform_device pled_device = {
	.name			= "sead3::pled",
	.id			= 0,
	.dev			= {
		.platform_data	= &led_data,
	},
	.num_resources		= ARRAY_SIZE(pled_resources),
	.resource		= pled_resources
};


static struct resource fled_resources[] = {
	{
		.start			= 0x1f000218,
		.end			= 0x1f00021f,
		.flags			= IORESOURCE_MEM
	}
};

static struct platform_device fled_device = {
	.name			= "sead3::fled",
	.id			= 0,
	.dev			= {
		.platform_data	= &led_data,
	},
	.num_resources		= ARRAY_SIZE(fled_resources),
	.resource		= fled_resources
};

static struct platform_device sead3_led_device = {
        .name   = "sead3-led",
        .id     = -1,
};

static struct platform_device *sead3_platform_devices[] __initdata = {
	&pled_device,
	&fled_device,
	&sead3_led_device,
};

static int __init sead3_platforms_device_init(void)
{
	return platform_add_devices(sead3_platform_devices,
				    ARRAY_SIZE(sead3_platform_devices));
}

device_initcall(sead3_platforms_device_init);
