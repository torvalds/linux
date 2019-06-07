// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2016 - 2018 Intel Corporation. All rights reserved. */
#include <linux/percpu-refcount.h>
#include <linux/memremap.h>
#include <linux/module.h>
#include <linux/pfn_t.h>
#include <linux/nd.h>
#include "../bus.h"

static int dax_pmem_probe(struct device *dev)
{
	return PTR_ERR_OR_ZERO(__dax_pmem_probe(dev, DEV_DAX_BUS));
}

static struct nd_device_driver dax_pmem_driver = {
	.probe = dax_pmem_probe,
	.drv = {
		.name = "dax_pmem",
	},
	.type = ND_DRIVER_DAX_PMEM,
};

static int __init dax_pmem_init(void)
{
	return nd_driver_register(&dax_pmem_driver);
}
module_init(dax_pmem_init);

static void __exit dax_pmem_exit(void)
{
	driver_unregister(&dax_pmem_driver.drv);
}
module_exit(dax_pmem_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Intel Corporation");
#if !IS_ENABLED(CONFIG_DEV_DAX_PMEM_COMPAT)
/* For compat builds, don't load this module by default */
MODULE_ALIAS_ND_DEVICE(ND_DEVICE_DAX_PMEM);
#endif
