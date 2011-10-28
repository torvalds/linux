/* Specification for the SMSC911x NIC
 *
 * Copyright (C) 2006 Matsushita Electric Industrial Co., Ltd.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/smsc911x.h>
#include <unit/smsc911x.h>

static struct smsc911x_platform_config smsc911x_config = {
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_OPEN_DRAIN,
	.flags		= SMSC911X_USE_32BIT,
};

static struct resource smsc911x_resources[] = {
	[0] = {
		.start	= SMSC911X_BASE,
		.end	= SMSC911X_BASE_END,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= SMSC911X_IRQ,
		.end	= SMSC911X_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device smsc911x_device = {
	.name		= "smsc911x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smsc911x_resources),
	.resource	= smsc911x_resources,
	.dev		= {
		.platform_data = &smsc911x_config,
	}
};

/*
 * add platform devices
 */
static int __init unit_device_init(void)
{
	platform_device_register(&smsc911x_device);
	return 0;
}

device_initcall(unit_device_init);
