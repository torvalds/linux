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
#include "device-dax.h"

struct dax_pmem {
	struct device *dev;
	struct percpu_ref ref;
	struct dev_pagemap pgmap;
	struct completion cmp;
};

static struct dax_pmem *to_dax_pmem(struct percpu_ref *ref)
{
	return container_of(ref, struct dax_pmem, ref);
}

static void dax_pmem_percpu_release(struct percpu_ref *ref)
{
	struct dax_pmem *dax_pmem = to_dax_pmem(ref);

	dev_dbg(dax_pmem->dev, "trace\n");
	complete(&dax_pmem->cmp);
}

static void dax_pmem_percpu_exit(void *data)
{
	struct percpu_ref *ref = data;
	struct dax_pmem *dax_pmem = to_dax_pmem(ref);

	dev_dbg(dax_pmem->dev, "trace\n");
	wait_for_completion(&dax_pmem->cmp);
	percpu_ref_exit(ref);
}

static void dax_pmem_percpu_kill(void *data)
{
	struct percpu_ref *ref = data;
	struct dax_pmem *dax_pmem = to_dax_pmem(ref);

	dev_dbg(dax_pmem->dev, "trace\n");
	percpu_ref_kill(ref);
}

static int dax_pmem_probe(struct device *dev)
{
	void *addr;
	struct resource res;
	int rc, id, region_id;
	struct nd_pfn_sb *pfn_sb;
	struct dev_dax *dev_dax;
	struct dax_pmem *dax_pmem;
	struct nd_namespace_io *nsio;
	struct dax_region *dax_region;
	struct nd_namespace_common *ndns;
	struct nd_dax *nd_dax = to_nd_dax(dev);
	struct nd_pfn *nd_pfn = &nd_dax->nd_pfn;

	ndns = nvdimm_namespace_common_probe(dev);
	if (IS_ERR(ndns))
		return PTR_ERR(ndns);
	nsio = to_nd_namespace_io(&ndns->dev);

	dax_pmem = devm_kzalloc(dev, sizeof(*dax_pmem), GFP_KERNEL);
	if (!dax_pmem)
		return -ENOMEM;

	/* parse the 'pfn' info block via ->rw_bytes */
	rc = devm_nsio_enable(dev, nsio);
	if (rc)
		return rc;
	rc = nvdimm_setup_pfn(nd_pfn, &dax_pmem->pgmap);
	if (rc)
		return rc;
	devm_nsio_disable(dev, nsio);

	pfn_sb = nd_pfn->pfn_sb;

	if (!devm_request_mem_region(dev, nsio->res.start,
				resource_size(&nsio->res),
				dev_name(&ndns->dev))) {
		dev_warn(dev, "could not reserve region %pR\n", &nsio->res);
		return -EBUSY;
	}

	dax_pmem->dev = dev;
	init_completion(&dax_pmem->cmp);
	rc = percpu_ref_init(&dax_pmem->ref, dax_pmem_percpu_release, 0,
			GFP_KERNEL);
	if (rc)
		return rc;

	rc = devm_add_action(dev, dax_pmem_percpu_exit, &dax_pmem->ref);
	if (rc) {
		percpu_ref_exit(&dax_pmem->ref);
		return rc;
	}

	dax_pmem->pgmap.ref = &dax_pmem->ref;
	addr = devm_memremap_pages(dev, &dax_pmem->pgmap);
	if (IS_ERR(addr)) {
		devm_remove_action(dev, dax_pmem_percpu_exit, &dax_pmem->ref);
		percpu_ref_exit(&dax_pmem->ref);
		return PTR_ERR(addr);
	}

	rc = devm_add_action_or_reset(dev, dax_pmem_percpu_kill,
							&dax_pmem->ref);
	if (rc)
		return rc;

	/* adjust the dax_region resource to the start of data */
	memcpy(&res, &dax_pmem->pgmap.res, sizeof(res));
	res.start += le64_to_cpu(pfn_sb->dataoff);

	rc = sscanf(dev_name(&ndns->dev), "namespace%d.%d", &region_id, &id);
	if (rc != 2)
		return -EINVAL;

	dax_region = alloc_dax_region(dev, region_id, &res,
			le32_to_cpu(pfn_sb->align), addr, PFN_DEV|PFN_MAP);
	if (!dax_region)
		return -ENOMEM;

	/* TODO: support for subdividing a dax region... */
	dev_dax = devm_create_dev_dax(dax_region, id, &res, 1);

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
