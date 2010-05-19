/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/amba/bus.h>

#include <mach/hardware.h>
#include <mach/setup.h>

static struct nmk_gpio_platform_data u8500_gpio_data[] = {
	GPIO_DATA("GPIO-0-31", 0),
	GPIO_DATA("GPIO-32-63", 32), /* 37..63 not routed to pin */
	GPIO_DATA("GPIO-64-95", 64),
	GPIO_DATA("GPIO-96-127", 96), /* 98..127 not routed to pin */
	GPIO_DATA("GPIO-128-159", 128),
	GPIO_DATA("GPIO-160-191", 160), /* 172..191 not routed to pin */
	GPIO_DATA("GPIO-192-223", 192),
	GPIO_DATA("GPIO-224-255", 224), /* 231..255 not routed to pin */
	GPIO_DATA("GPIO-256-288", 256), /* 268..288 not routed to pin */
};

static struct resource u8500_gpio_resources[] = {
	GPIO_RESOURCE(0),
	GPIO_RESOURCE(1),
	GPIO_RESOURCE(2),
	GPIO_RESOURCE(3),
	GPIO_RESOURCE(4),
	GPIO_RESOURCE(5),
	GPIO_RESOURCE(6),
	GPIO_RESOURCE(7),
	GPIO_RESOURCE(8),
};

struct platform_device u8500_gpio_devs[] = {
	GPIO_DEVICE(0),
	GPIO_DEVICE(1),
	GPIO_DEVICE(2),
	GPIO_DEVICE(3),
	GPIO_DEVICE(4),
	GPIO_DEVICE(5),
	GPIO_DEVICE(6),
	GPIO_DEVICE(7),
	GPIO_DEVICE(8),
};

struct amba_device u8500_ssp0_device = {
	.dev = {
		.coherent_dma_mask = ~0,
		.init_name = "ssp0",
	},
	.res = {
		.start = U8500_SSP0_BASE,
		.end   = U8500_SSP0_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	.irq = {IRQ_SSP0, NO_IRQ },
	/* ST-Ericsson modified id */
	.periphid = SSP_PER_ID,
};

static struct resource u8500_i2c0_resources[] = {
	[0] = {
		.start	= U8500_I2C0_BASE,
		.end	= U8500_I2C0_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_I2C0,
		.end	= IRQ_I2C0,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device u8500_i2c0_device = {
	.name		= "nmk-i2c",
	.id		= 0,
	.resource	= u8500_i2c0_resources,
	.num_resources	= ARRAY_SIZE(u8500_i2c0_resources),
};

static struct resource u8500_i2c4_resources[] = {
	[0] = {
		.start	= U8500_I2C4_BASE,
		.end	= U8500_I2C4_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_I2C4,
		.end	= IRQ_I2C4,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device u8500_i2c4_device = {
	.name		= "nmk-i2c",
	.id		= 4,
	.resource	= u8500_i2c4_resources,
	.num_resources	= ARRAY_SIZE(u8500_i2c4_resources),
};
