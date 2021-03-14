// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2020 Intel Corporation. All rights reserved. */
#include <linux/device.h>
#include <linux/module.h>

/**
 * DOC: cxl bus
 *
 * The CXL bus provides namespace for control devices and a rendezvous
 * point for cross-device interleave coordination.
 */
struct bus_type cxl_bus_type = {
	.name = "cxl",
};
EXPORT_SYMBOL_GPL(cxl_bus_type);

static __init int cxl_bus_init(void)
{
	return bus_register(&cxl_bus_type);
}

static void cxl_bus_exit(void)
{
	bus_unregister(&cxl_bus_type);
}

module_init(cxl_bus_init);
module_exit(cxl_bus_exit);
MODULE_LICENSE("GPL v2");
