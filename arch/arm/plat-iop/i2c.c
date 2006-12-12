/*
 * arch/arm/plat-iop/i2c.c
 *
 * Author: Nicolas Pitre <nico@cam.org>
 * Copyright (C) 2001 MontaVista Software, Inc.
 * Copyright (C) 2004 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/serial_core.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/mach/map.h>
#include <asm/setup.h>
#include <asm/system.h>
#include <asm/memory.h>
#include <asm/hardware.h>
#include <asm/hardware/iop3xx.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#ifdef CONFIG_ARCH_IOP32X
#define IRQ_IOP3XX_I2C_0	IRQ_IOP32X_I2C_0
#define IRQ_IOP3XX_I2C_1	IRQ_IOP32X_I2C_1
#endif
#ifdef CONFIG_ARCH_IOP33X
#define IRQ_IOP3XX_I2C_0	IRQ_IOP33X_I2C_0
#define IRQ_IOP3XX_I2C_1	IRQ_IOP33X_I2C_1
#endif

static struct resource iop3xx_i2c0_resources[] = {
	[0] = {
		.start	= 0xfffff680,
		.end	= 0xfffff697,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_IOP3XX_I2C_0,
		.end	= IRQ_IOP3XX_I2C_0,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device iop3xx_i2c0_device = {
	.name		= "IOP3xx-I2C",
	.id		= 0,
	.num_resources	= 2,
	.resource	= iop3xx_i2c0_resources,
};


static struct resource iop3xx_i2c1_resources[] = {
	[0] = {
		.start	= 0xfffff6a0,
		.end	= 0xfffff6b7,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_IOP3XX_I2C_1,
		.end	= IRQ_IOP3XX_I2C_1,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device iop3xx_i2c1_device = {
	.name		= "IOP3xx-I2C",
	.id		= 1,
	.num_resources	= 2,
	.resource	= iop3xx_i2c1_resources,
};
