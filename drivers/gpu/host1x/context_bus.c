// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, NVIDIA Corporation.
 */

#include <linux/device.h>
#include <linux/of.h>

struct bus_type host1x_context_device_bus_type = {
	.name = "host1x-context",
};
EXPORT_SYMBOL_GPL(host1x_context_device_bus_type);

static int __init host1x_context_device_bus_init(void)
{
	int err;

	err = bus_register(&host1x_context_device_bus_type);
	if (err < 0) {
		pr_err("bus type registration failed: %d\n", err);
		return err;
	}

	return 0;
}
postcore_initcall(host1x_context_device_bus_init);
