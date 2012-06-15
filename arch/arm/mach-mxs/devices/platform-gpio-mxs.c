/*
 * Copyright 2011 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <linux/compiler.h>
#include <linux/err.h>
#include <linux/init.h>

#include <mach/mx23.h>
#include <mach/mx28.h>
#include <mach/devices-common.h>

struct platform_device *__init mxs_add_gpio(
	char *name, int id, resource_size_t iobase, int irq)
{
	struct resource res[] = {
		{
			.start = iobase,
			.end = iobase + SZ_8K - 1,
			.flags = IORESOURCE_MEM,
		}, {
			.start = irq,
			.end = irq,
			.flags = IORESOURCE_IRQ,
		},
	};

	return platform_device_register_resndata(&mxs_apbh_bus,
			name, id, res, ARRAY_SIZE(res), NULL, 0);
}
