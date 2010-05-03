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
#include <linux/amba/bus.h>

#include <mach/hardware.h>
#include <mach/setup.h>

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
