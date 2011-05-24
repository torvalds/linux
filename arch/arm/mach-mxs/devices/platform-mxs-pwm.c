/*
 * Copyright (C) 2010 Pengutronix
 * Sascha Hauer <s.hauer@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <asm/sizes.h>
#include <mach/devices-common.h>

struct platform_device *__init mxs_add_mxs_pwm(resource_size_t iobase, int id)
{
	struct resource res = {
		.flags = IORESOURCE_MEM,
	};

	res.start = iobase + 0x10 + 0x20 * id;
	res.end = res.start + 0x1f;

	return mxs_add_platform_device("mxs-pwm", id, &res, 1, NULL, 0);
}
