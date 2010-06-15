/*
 * Copyright (C) 2009-2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/init.h>

struct platform_device *imx_add_platform_device(const char *name, int id,
		const struct resource *res, unsigned int num_resources,
		const void *data, size_t size_data);
