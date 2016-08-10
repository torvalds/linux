/*
 * Copyright 2011 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2011 Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include "devices-common.h"
#include "../common.h"

struct platform_device *__init mxc_register_gpio(char *name, int id,
	resource_size_t iobase, resource_size_t iosize, int irq, int irq_high)
{
	struct resource res[] = {
		{
			.start = iobase,
			.end = iobase + iosize - 1,
			.flags = IORESOURCE_MEM,
		}, {
			.start = irq,
			.end = irq,
			.flags = IORESOURCE_IRQ,
		}, {
			.start = irq_high,
			.end = irq_high,
			.flags = IORESOURCE_IRQ,
		},
	};

	return platform_device_register_resndata(&mxc_aips_bus,
			name, id, res, ARRAY_SIZE(res), NULL, 0);
}
