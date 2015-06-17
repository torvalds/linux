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
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/sizes.h>
#include <linux/ndctl.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/nd.h>
#include "label.h"
#include "nd.h"

static int nvdimm_probe(struct device *dev)
{
	struct nvdimm_drvdata *ndd;
	int rc;

	ndd = kzalloc(sizeof(*ndd), GFP_KERNEL);
	if (!ndd)
		return -ENOMEM;

	dev_set_drvdata(dev, ndd);
	ndd->dpa.name = dev_name(dev);
	ndd->ns_current = -1;
	ndd->ns_next = -1;
	ndd->dpa.start = 0;
	ndd->dpa.end = -1;
	ndd->dev = dev;
	get_device(dev);
	kref_init(&ndd->kref);

	rc = nvdimm_init_nsarea(ndd);
	if (rc)
		goto err;

	rc = nvdimm_init_config_data(ndd);
	if (rc)
		goto err;

	dev_dbg(dev, "config data size: %d\n", ndd->nsarea.config_size);

	nvdimm_bus_lock(dev);
	ndd->ns_current = nd_label_validate(ndd);
	ndd->ns_next = nd_label_next_nsindex(ndd->ns_current);
	nd_label_copy(ndd, to_next_namespace_index(ndd),
			to_current_namespace_index(ndd));
	rc = nd_label_reserve_dpa(ndd);
	nvdimm_bus_unlock(dev);

	if (rc)
		goto err;

	return 0;

 err:
	put_ndd(ndd);
	return rc;
}

static int nvdimm_remove(struct device *dev)
{
	struct nvdimm_drvdata *ndd = dev_get_drvdata(dev);

	nvdimm_bus_lock(dev);
	dev_set_drvdata(dev, NULL);
	nvdimm_bus_unlock(dev);
	put_ndd(ndd);

	return 0;
}

static struct nd_device_driver nvdimm_driver = {
	.probe = nvdimm_probe,
	.remove = nvdimm_remove,
	.drv = {
		.name = "nvdimm",
	},
	.type = ND_DRIVER_DIMM,
};

int __init nvdimm_init(void)
{
	return nd_driver_register(&nvdimm_driver);
}

void nvdimm_exit(void)
{
	driver_unregister(&nvdimm_driver.drv);
}

MODULE_ALIAS_ND_DEVICE(ND_DEVICE_DIMM);
