/*
 * Copyright (C) 2006 IBM Corporation
 *
 * Implements device information for i8253 timer chip
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation
 */

#include <linux/platform_device.h>

static __init int add_pcspkr(void)
{
	struct platform_device *pd;
	int ret;

	pd = platform_device_alloc("pcspkr", -1);
	if (!pd)
		return -ENOMEM;

	ret = platform_device_add(pd);
	if (ret)
		platform_device_put(pd);

	return ret;
}
device_initcall(add_pcspkr);
