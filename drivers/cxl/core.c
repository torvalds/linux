// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2020 Intel Corporation. All rights reserved. */
#include <linux/device.h>
#include <linux/module.h>

/**
 * DOC: cxl core
 *
 * The CXL core provides a sysfs hierarchy for control devices and a rendezvous
 * point for cross-device interleave coordination through cxl ports.
 */

struct bus_type cxl_bus_type = {
	.name = "cxl",
};
EXPORT_SYMBOL_GPL(cxl_bus_type);

static __init int cxl_core_init(void)
{
	return bus_register(&cxl_bus_type);
}

static void cxl_core_exit(void)
{
	bus_unregister(&cxl_bus_type);
}

module_init(cxl_core_init);
module_exit(cxl_core_exit);
MODULE_LICENSE("GPL v2");
