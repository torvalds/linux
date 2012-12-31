/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5P series device definition for i2c for hdmiphy device
 *
 * Based on plat-samsung/dev-i2c7.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/platform_device.h>

#include <mach/irqs.h>
#include <mach/map.h>
#include <mach/iic-hdmiphy.h>

#include <plat/regs-iic.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/iic.h>

static struct resource s5p_i2c_resource[] = {
	[0] = {
		.start = S5P_PA_IIC_HDMIPHY,
		.end   = S5P_PA_IIC_HDMIPHY + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_IIC_HDMIPHY,
		.end   = IRQ_IIC_HDMIPHY,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device s5p_device_i2c_hdmiphy = {
	.name		  = "s3c2440-hdmiphy-i2c",
	.id		  = -1,
	.num_resources	  = ARRAY_SIZE(s5p_i2c_resource),
	.resource	  = s5p_i2c_resource,
};
EXPORT_SYMBOL(s5p_device_i2c_hdmiphy);

void __init s5p_i2c_hdmiphy_set_platdata(struct s3c2410_platform_i2c *pd)
{
	struct s3c2410_platform_i2c *npd;

	if (!pd) {
		pd = &default_i2c_data;
		pd->bus_num = S5P_IIC_HDMIPHY_BUS_NUM;
	}

	npd = s3c_set_platdata(pd, sizeof(struct s3c2410_platform_i2c),
			       &s5p_device_i2c_hdmiphy);
}
EXPORT_SYMBOL(s5p_i2c_hdmiphy_set_platdata);
