/*
 * Copyright(c) 2016 Intel Corporation. All rights reserved.
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
#include <linux/percpu-refcount.h>
#include <linux/memremap.h>
#include <linux/module.h>
#include <linux/pfn_t.h>
#include "../nvdimm/pfn.h"
#include "../nvdimm/nd.h"
#include "bus.h"

static int dax_pmem_probe(struct device *dev)
{
	struct resource res;
	int rc, id, region_id;
	resource_size_t offset;
	struct nd_pfn_sb *pfn_sb;
	struct dev_dax *dev_dax;
	struct nd_namespace_io *nsio;
	struct dax_region *dax_region;
	struct dev_pagemap pgmap = { 0 };
	struct nd_namespace_common *ndns;
	struct nd_dax *nd_dax = to_nd_dax(dev);
	struct nd_pfn *nd_pfn = &nd_dax->nd_pfn;

	ndns = nvdimm_namespace_common_probe(dev);
	if (IS_ERR(ndns))
		return PTR_ERR(ndns);
	nsio = to_nd_namespace_io(&ndns->dev);

	/* parse the 'pfn' info block via ->rw_bytes */
	rc = devm_nsio_enable(dev, nsio);
	if (rc)
		return rc;
	rc = nvdimm_setup_pfn(nd_pfn, &pgmap);
	if (rc)
		return rc;
	devm_nsio_disable(dev, nsio);

	/* reserve the metadata area, device-dax will reserve the data */
        pfn_sb = nd_pfn->pfn_sb;
	offset = le64_to_cpu(pfn_sb->dataoff);
	if (!devm_request_mem_region(dev, nsio->res.start, offset,
				dev_name(&ndns->dev))) {
                dev_warn(dev, "could not reserve metadata\n");
                return -EBUSY;
        }

	rc = sscanf(dev_name(&ndns->dev), "namespace%d.%d", &region_id, &id);
	if (rc != 2)
		return -EINVAL;

	/* adjust the dax_region resource to the start of data */
	memcpy(&res, &pgmap.res, sizeof(res));
	res.start += offset;
	dax_region = alloc_dax_region(dev, region_id, &res,
			le32_to_cpu(pfn_sb->align), PFN_DEV|PFN_MAP);
	if (!dax_region)
		return -ENOMEM;

	dev_dax = devm_create_dev_dax(dax_region, id, &pgmap);

	/* child dev_dax instances now own the lifetime of the dax_region */
	dax_region_put(dax_region);

	return PTR_ERR_OR_ZERO(dev_dax);
}

static struct nd_device_driver dax_pmem_driver = {
	.probe = dax_pmem_probe,
	.drv = {
		.name = "dax_pmem",
	},
	.type = ND_DRIVER_DAX_PMEM,
};

module_nd_driver(dax_pmem_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Intel Corporation");
MODULE_ALIAS_ND_DEVICE(ND_DEVICE_DAX_PMEM);
