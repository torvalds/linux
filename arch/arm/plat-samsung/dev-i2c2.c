/* linux/arch/arm/plat-s3c/dev-i2c2.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S3C series device definition for i2c device 2
 *
 * Based on plat-samsung/dev-i2c0.c
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

#include <plat/regs-iic.h>
#include <plat/iic.h>
#include <plat/devs.h>
#include <plat/cpu.h>

static struct resource s3c_i2c_resource[] = {
	[0] = {
		.start = S3C_PA_IIC2,
		.end   = S3C_PA_IIC2 + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_IIC2,
		.end   = IRQ_IIC2,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device s3c_device_i2c2 = {
	.name		  = "s3c2410-i2c",
	.id		  = 2,
	.num_resources	  = ARRAY_SIZE(s3c_i2c_resource),
	.resource	  = s3c_i2c_resource,
};

static struct s3c2410_platform_i2c default_i2c_data2 __initdata = {
	.flags		= 0,
	.bus_num	= 2,
	.slave_addr	= 0x10,
	.frequency	= 100*1000,
	.sda_delay	= 100,
};

void __init s3c_i2c2_set_platdata(struct s3c2410_platform_i2c *pd)
{
	struct s3c2410_platform_i2c *npd;

	if (!pd)
		pd = &default_i2c_data2;

	npd = kmemdup(pd, sizeof(struct s3c2410_platform_i2c), GFP_KERNEL);
	if (!npd)
		printk(KERN_ERR "%s: no memory for platform data\n", __func__);
	else if (!npd->cfg_gpio)
		npd->cfg_gpio = s3c_i2c2_cfg_gpio;

	s3c_device_i2c2.dev.platform_data = npd;
}
