/*
 * Copyright(c) 2013-2015 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#include <linux/module.h>
#include <linux/device.h>
#include <linux/nd.h>
#include "nd.h"

static int nd_region_probe(struct device *dev)
{
	int err;
	struct nd_region_namespaces *num_ns;
	struct nd_region *nd_region = to_nd_region(dev);
	int rc = nd_region_register_namespaces(nd_region, &err);

	num_ns = devm_kzalloc(dev, sizeof(*num_ns), GFP_KERNEL);
	if (!num_ns)
		return -ENOMEM;

	if (rc < 0)
		return rc;

	num_ns->active = rc;
	num_ns->count = rc + err;
	dev_set_drvdata(dev, num_ns);

	if (err == 0)
		return 0;

	if (rc == err)
		return -ENODEV;

	/*
	 * Given multiple namespaces per region, we do not want to
	 * disable all the successfully registered peer namespaces upon
	 * a single registration failure.  If userspace is missing a
	 * namespace that it expects it can disable/re-enable the region
	 * to retry discovery after correcting the failure.
	 * <regionX>/namespaces returns the current
	 * "<async-registered>/<total>" namespace count.
	 */
	dev_err(dev, "failed to register %d namespace%s, continuing...\n",
			err, err == 1 ? "" : "s");
	return 0;
}

static int child_unregister(struct device *dev, void *data)
{
	nd_device_unregister(dev, ND_SYNC);
	return 0;
}

static int nd_region_remove(struct device *dev)
{
	struct nd_region *nd_region = to_nd_region(dev);

	/* flush attribute readers and disable */
	nvdimm_bus_lock(dev);
	nd_region->ns_seed = NULL;
	dev_set_drvdata(dev, NULL);
	nvdimm_bus_unlock(dev);

	device_for_each_child(dev, NULL, child_unregister);
	return 0;
}

static struct nd_device_driver nd_region_driver = {
	.probe = nd_region_probe,
	.remove = nd_region_remove,
	.drv = {
		.name = "nd_region",
	},
	.type = ND_DRIVER_REGION_BLK | ND_DRIVER_REGION_PMEM,
};

int __init nd_region_init(void)
{
	return nd_driver_register(&nd_region_driver);
}

void nd_region_exit(void)
{
	driver_unregister(&nd_region_driver.drv);
}

MODULE_ALIAS_ND_DEVICE(ND_DEVICE_REGION_PMEM);
MODULE_ALIAS_ND_DEVICE(ND_DEVICE_REGION_BLK);
