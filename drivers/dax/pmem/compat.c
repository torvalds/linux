// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2016 - 2018 Intel Corporation. All rights reserved. */
#include <linux/percpu-refcount.h>
#include <linux/memremap.h>
#include <linux/module.h>
#include <linux/pfn_t.h>
#include <linux/nd.h>
#include "../bus.h"

/* we need the private definitions to implement compat suport */
#include "../dax-private.h"

static int dax_pmem_compat_probe(struct device *dev)
{
	struct dev_dax *dev_dax = __dax_pmem_probe(dev, DEV_DAX_CLASS);
	int rc;

	if (IS_ERR(dev_dax))
		return PTR_ERR(dev_dax);

        if (!devres_open_group(&dev_dax->dev, dev_dax, GFP_KERNEL))
		return -ENOMEM;

	device_lock(&dev_dax->dev);
	rc = dev_dax_probe(dev_dax);
	device_unlock(&dev_dax->dev);

	devres_close_group(&dev_dax->dev, dev_dax);
	if (rc)
		devres_release_group(&dev_dax->dev, dev_dax);

	return rc;
}

static int dax_pmem_compat_release(struct device *dev, void *data)
{
	device_lock(dev);
	devres_release_group(dev, to_dev_dax(dev));
	device_unlock(dev);

	return 0;
}

static void dax_pmem_compat_remove(struct device *dev)
{
	device_for_each_child(dev, NULL, dax_pmem_compat_release);
}

static struct nd_device_driver dax_pmem_compat_driver = {
	.probe = dax_pmem_compat_probe,
	.remove = dax_pmem_compat_remove,
	.drv = {
		.name = "dax_pmem_compat",
	},
	.type = ND_DRIVER_DAX_PMEM,
};

static int __init dax_pmem_compat_init(void)
{
	return nd_driver_register(&dax_pmem_compat_driver);
}
module_init(dax_pmem_compat_init);

static void __exit dax_pmem_compat_exit(void)
{
	driver_unregister(&dax_pmem_compat_driver.drv);
}
module_exit(dax_pmem_compat_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Intel Corporation");
MODULE_ALIAS_ND_DEVICE(ND_DEVICE_DAX_PMEM);
