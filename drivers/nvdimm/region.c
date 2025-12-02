// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright(c) 2013-2015 Intel Corporation. All rights reserved.
 */
#include <linux/memregion.h>
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
	struct range range = {
		.start = nd_region->ndr_start,
		.end = nd_region->ndr_start + nd_region->ndr_size - 1,
	};

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

	if (devm_init_badblocks(dev, &nd_region->bb))
		return -ENODEV;
	nd_region->bb_state =
		sysfs_get_dirent(nd_region->dev.kobj.sd, "badblocks");
	if (!nd_region->bb_state)
		dev_warn(dev, "'badblocks' notification disabled\n");
	nvdimm_badblocks_populate(nd_region, &nd_region->bb, &range);

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
			err, str_plural(err));
	return 0;
}

static int child_unregister(struct device *dev, void *data)
{
	nd_device_unregister(dev, ND_SYNC);
	return 0;
}

static void nd_region_remove(struct device *dev)
{
	struct nd_region *nd_region = to_nd_region(dev);

	device_for_each_child(dev, NULL, child_unregister);

	/* flush attribute readers and disable */
	scoped_guard(nvdimm_bus, dev) {
		nd_region->ns_seed = NULL;
		nd_region->btt_seed = NULL;
		nd_region->pfn_seed = NULL;
		nd_region->dax_seed = NULL;
		dev_set_drvdata(dev, NULL);
	}

	/*
	 * Note, this assumes device_lock() context to not race
	 * nd_region_notify()
	 */
	sysfs_put(nd_region->bb_state);
	nd_region->bb_state = NULL;

	/*
	 * Try to flush caches here since a disabled region may be subject to
	 * secure erase while disabled, and previous dirty data should not be
	 * written back to a new instance of the region. This only matters on
	 * bare metal where security commands are available, so silent failure
	 * here is ok.
	 */
	if (cpu_cache_has_invalidate_memregion())
		cpu_cache_invalidate_memregion(IORES_DESC_PERSISTENT_MEMORY);
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

		if (is_memory(&nd_region->dev)) {
			struct range range = {
				.start = nd_region->ndr_start,
				.end = nd_region->ndr_start +
					nd_region->ndr_size - 1,
			};

			nvdimm_badblocks_populate(nd_region,
					&nd_region->bb, &range);
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
