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
#include "dax.h"

struct dax_pmem {
	struct device *dev;
	struct percpu_ref ref;
	struct completion cmp;
};

static struct dax_pmem *to_dax_pmem(struct percpu_ref *ref)
{
	return container_of(ref, struct dax_pmem, ref);
}

static void dax_pmem_percpu_release(struct percpu_ref *ref)
{
	struct dax_pmem *dax_pmem = to_dax_pmem(ref);

	dev_dbg(dax_pmem->dev, "%s\n", __func__);
	complete(&dax_pmem->cmp);
}

static void dax_pmem_percpu_exit(void *data)
{
	struct percpu_ref *ref = data;
	struct dax_pmem *dax_pmem = to_dax_pmem(ref);

	dev_dbg(dax_pmem->dev, "%s\n", __func__);
	percpu_ref_exit(ref);
}

static void dax_pmem_percpu_kill(void *data)
{
	struct percpu_ref *ref = data;
	struct dax_pmem *dax_pmem = to_dax_pmem(ref);

	dev_dbg(dax_pmem->dev, "%s\n", __func__);
	percpu_ref_kill(ref);
	wait_for_completion(&dax_pmem->cmp);
}

static int dax_pmem_probe(struct device *dev)
{
	int rc;
	void *addr;
	struct resource res;
	struct dax_dev *dax_dev;
	struct nd_pfn_sb *pfn_sb;
	struct dax_pmem *dax_pmem;
	struct nd_region *nd_region;
	struct nd_namespace_io *nsio;
	struct dax_region *dax_region;
	struct nd_namespace_common *ndns;
	struct nd_dax *nd_dax = to_nd_dax(dev);
	struct nd_pfn *nd_pfn = &nd_dax->nd_pfn;
	struct vmem_altmap __altmap, *altmap = NULL;

	ndns = nvdimm_namespace_common_probe(dev);
	if (IS_ERR(ndns))
		return PTR_ERR(ndns);
	nsio = to_nd_namespace_io(&ndns->dev);

	/* parse the 'pfn' info block via ->rw_bytes */
	rc = devm_nsio_enable(dev, nsio);
	if (rc)
		return rc;
	altmap = nvdimm_setup_pfn(nd_pfn, &res, &__altmap);
	if (IS_ERR(altmap))
		return PTR_ERR(altmap);
	devm_nsio_disable(dev, nsio);

	pfn_sb = nd_pfn->pfn_sb;

	if (!devm_request_mem_region(dev, nsio->res.start,
				resource_size(&nsio->res), dev_name(dev))) {
		dev_warn(dev, "could not reserve region %pR\n", &nsio->res);
		return -EBUSY;
	}

	dax_pmem = devm_kzalloc(dev, sizeof(*dax_pmem), GFP_KERNEL);
	if (!dax_pmem)
		return -ENOMEM;

	dax_pmem->dev = dev;
	init_completion(&dax_pmem->cmp);
	rc = percpu_ref_init(&dax_pmem->ref, dax_pmem_percpu_release, 0,
			GFP_KERNEL);
	if (rc)
		return rc;

	rc = devm_add_action_or_reset(dev, dax_pmem_percpu_exit,
							&dax_pmem->ref);
	if (rc)
		return rc;

	addr = devm_memremap_pages(dev, &res, &dax_pmem->ref, altmap);
	if (IS_ERR(addr))
		return PTR_ERR(addr);

	rc = devm_add_action_or_reset(dev, dax_pmem_percpu_kill,
							&dax_pmem->ref);
	if (rc)
		return rc;

	/* adjust the dax_region resource to the start of data */
	res.start += le64_to_cpu(pfn_sb->dataoff);

	nd_region = to_nd_region(dev->parent);
	dax_region = alloc_dax_region(dev, nd_region->id, &res,
			le32_to_cpu(pfn_sb->align), addr, PFN_DEV|PFN_MAP);
	if (!dax_region)
		return -ENOMEM;

	/* TODO: support for subdividing a dax region... */
	dax_dev = devm_create_dax_dev(dax_region, &res, 1);

	/* child dax_dev instances now own the lifetime of the dax_region */
	dax_region_put(dax_region);

	return PTR_ERR_OR_ZERO(dax_dev);
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
MODULE_ALIAS_ND_DEVICE(ND_DEVICE_DAX_PMEM);
