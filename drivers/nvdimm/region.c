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
#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/nd.h>
#include "nd-core.h"
#include "nd.h"

static int nd_region_probe(struct device *dev)
{
	int err, rc;
	static unsigned long once;
	struct nd_region_data *ndrd;
	struct nd_region *nd_region = to_nd_region(dev);

	if (nd_region->num_lanes > num_online_cpus()
			&& nd_region->num_lanes < num_possible_cpus()
			&& !test_and_set_bit(0, &once)) {
		dev_dbg(dev, "online cpus (%d) < concurrent i/o lanes (%d) < possible cpus (%d)\n",
				num_online_cpus(), nd_region->num_lanes,
				num_possible_cpus());
		dev_dbg(dev, "setting nr_cpus=%d may yield better libnvdimm device performance\n",
				nd_region->num_lanes);
	}

	rc = nd_region_activate(nd_region);
	if (rc)
		return rc;

	rc = nd_blk_region_init(nd_region);
	if (rc)
		return rc;

	if (is_memory(&nd_region->dev)) {
		struct resource ndr_res;

		if (devm_init_badblocks(dev, &nd_region->bb))
			return -ENODEV;
		nd_region->bb_state = sysfs_get_dirent(nd_region->dev.kobj.sd,
						       "badblocks");
		if (!nd_region->bb_state)
			dev_warn(&nd_region->dev,
					"'badblocks' notification disabled\n");
		ndr_res.start = nd_region->ndr_start;
		ndr_res.end = nd_region->ndr_start + nd_region->ndr_size - 1;
		nvdimm_badblocks_populate(nd_region, &nd_region->bb, &ndr_res);
	}

	rc = nd_region_register_namespaces(nd_region, &err);
	if (rc < 0)
		return rc;

	ndrd = dev_get_drvdata(dev);
	ndrd->ns_active = rc;
	ndrd->ns_count = rc + err;

	if (rc && err && rc == err)
		return -ENODEV;

	nd_region->btt_seed = nd_btt_create(nd_region);
	nd_region->pfn_seed = nd_pfn_create(nd_region);
	nd_region->dax_seed = nd_dax_create(nd_region);
	if (err == 0)
		return 0;

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

	device_for_each_child(dev, NULL, child_unregister);

	/* flush attribute readers and disable */
	nvdimm_bus_lock(dev);
	nd_region->ns_seed = NULL;
	nd_region->btt_seed = NULL;
	nd_region->pfn_seed = NULL;
	nd_region->dax_seed = NULL;
	dev_set_drvdata(dev, NULL);
	nvdimm_bus_unlock(dev);

	/*
	 * Note, this assumes device_lock() context to not race
	 * nd_region_notify()
	 */
	sysfs_put(nd_region->bb_state);
	nd_region->bb_state = NULL;

	return 0;
}

static int child_notify(struct device *dev, void *data)
{
	nd_device_notify(dev, *(enum nvdimm_event *) data);
	return 0;
}

static void nd_region_notify(struct device *dev, enum nvdimm_event event)
{
	if (event == NVDIMM_REVALIDATE_POISON) {
		struct nd_region *nd_region = to_nd_region(dev);
		struct resource res;

		if (is_memory(&nd_region->dev)) {
			res.start = nd_region->ndr_start;
			res.end = nd_region->ndr_start +
				nd_region->ndr_size - 1;
			nvdimm_badblocks_populate(nd_region,
					&nd_region->bb, &res);
			if (nd_region->bb_state)
				sysfs_notify_dirent(nd_region->bb_state);
		}
	}
	device_for_each_child(dev, &event, child_notify);
}

static struct nd_device_driver nd_region_driver = {
	.probe = nd_region_probe,
	.remove = nd_region_remove,
	.notify = nd_region_notify,
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
