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
	struct range range;
	int rc, id, region_id;
	resource_size_t offset;
	struct nd_pfn_sb *pfn_sb;
	struct dev_dax *dev_dax;
	struct dev_dax_data data;
	struct nd_namespace_io *nsio;
	struct dax_region *dax_region;
	struct dev_pagemap pgmap = { };
	struct nd_namespace_common *ndns;
	struct nd_dax *nd_dax = to_nd_dax(dev);
	struct nd_pfn *nd_pfn = &nd_dax->nd_pfn;
	struct nd_region *nd_region = to_nd_region(dev->parent);

	ndns = nvdimm_namespace_common_probe(dev);
	if (IS_ERR(ndns))
		return ERR_CAST(ndns);

	/* parse the 'pfn' info block via ->rw_bytes */
	rc = devm_namespace_enable(dev, ndns, nd_info_block_reserve());
	if (rc)
		return ERR_PTR(rc);
	rc = nvdimm_setup_pfn(nd_pfn, &pgmap);
	if (rc)
		return ERR_PTR(rc);
	devm_namespace_disable(dev, ndns);

	/* reserve the metadata area, device-dax will reserve the data */
	pfn_sb = nd_pfn->pfn_sb;
	offset = le64_to_cpu(pfn_sb->dataoff);
	nsio = to_nd_namespace_io(&ndns->dev);
	if (!devm_request_mem_region(dev, nsio->res.start, offset,
				dev_name(&ndns->dev))) {
		dev_warn(dev, "could not reserve metadata\n");
		return ERR_PTR(-EBUSY);
	}

	rc = sscanf(dev_name(&ndns->dev), "namespace%d.%d", &region_id, &id);
	if (rc != 2)
		return ERR_PTR(-EINVAL);

	/* adjust the dax_region range to the start of data */
	range = pgmap.range;
	range.start += offset;
	dax_region = alloc_dax_region(dev, region_id, &range,
			nd_region->target_node, le32_to_cpu(pfn_sb->align),
			IORESOURCE_DAX_STATIC);
	if (!dax_region)
		return ERR_PTR(-ENOMEM);

	data = (struct dev_dax_data) {
		.dax_region = dax_region,
		.id = id,
		.pgmap = &pgmap,
		.subsys = subsys,
		.size = range_len(&range),
	};
	dev_dax = devm_create_dev_dax(&data);

	/* child dev_dax instances now own the lifetime of the dax_region */
	dax_region_put(dax_region);

	return dev_dax;
}
EXPORT_SYMBOL_GPL(__dax_pmem_probe);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Intel Corporation");
