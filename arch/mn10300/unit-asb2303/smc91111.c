/* ASB2303 initialisation
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include <asm/io.h>
#include <asm/timex.h>
#include <asm/processor.h>
#include <asm/intctl-regs.h>
#include <asm/unit/smc91111.h>

static struct resource smc91c111_resources[] = {
	[0] = {
		.start		= SMC91111_BASE,
		.end		= SMC91111_BASE_END,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= SMC91111_IRQ,
		.end		= SMC91111_IRQ,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device smc91c111_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91c111_resources),
	.resource	= smc91c111_resources,
};

/*
 * add platform devices
 */
static int __init unit_device_init(void)
{
	platform_device_register(&smc91c111_device);
	return 0;
}

device_initcall(unit_device_init);
