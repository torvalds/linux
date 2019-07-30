// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2016 - 2018 Intel Corporation. All rights reserved. */
#include <linux/memremap.h>
#include <linux/module.h>
#include <linux/pfn_t.h>
#include "../../nvdimm/pfn.h"
#include "../../nvdimm/nd.h"
#include "../bus.h"

struct dev_dax *__dax_pmem_probe(struct device *dev, enum dev_dax_subsys subsys)
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
	struct nd_region *nd_region = to_nd_region(dev->parent);

	ndns = nvdimm_namespace_common_probe(dev);
	if (IS_ERR(ndns))
		return ERR_CAST(ndns);
	nsio = to_nd_namespace_io(&ndns->dev);

	/* parse the 'pfn' info block via ->rw_bytes */
	rc = devm_nsio_enable(dev, nsio);
	if (rc)
		return ERR_PTR(rc);
	rc = nvdimm_setup_pfn(nd_pfn, &pgmap);
	if (rc)
		return ERR_PTR(rc);
	devm_nsio_disable(dev, nsio);

	/* reserve the metadata area, device-dax will reserve the data */
	pfn_sb = nd_pfn->pfn_sb;
	offset = le64_to_cpu(pfn_sb->dataoff);
	if (!devm_request_mem_region(dev, nsio->res.start, offset,
				dev_name(&ndns->dev))) {
		dev_warn(dev, "could not reserve metadata\n");
		return ERR_PTR(-EBUSY);
	}

	rc = sscanf(dev_name(&ndns->dev), "namespace%d.%d", &region_id, &id);
	if (rc != 2)
		return ERR_PTR(-EINVAL);

	/* adjust the dax_region resource to the start of data */
	memcpy(&res, &pgmap.res, sizeof(res));
	res.start += offset;
	dax_region = alloc_dax_region(dev, region_id, &res,
			nd_region->target_node, le32_to_cpu(pfn_sb->align),
			PFN_DEV|PFN_MAP);
	if (!dax_region)
		return ERR_PTR(-ENOMEM);

	dev_dax = __devm_create_dev_dax(dax_region, id, &pgmap, subsys);

	/* child dev_dax instances now own the lifetime of the dax_region */
	dax_region_put(dax_region);

	return dev_dax;
}
EXPORT_SYMBOL_GPL(__dax_pmem_probe);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Intel Corporation");
