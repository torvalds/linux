// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Intel Corporation. All rights reserved. */
#include <linux/module.h>
#include <linux/dax.h>

#include "../cxl/cxl.h"
#include "bus.h"

static int cxl_dax_region_probe(struct device *dev)
{
	struct cxl_dax_region *cxlr_dax = to_cxl_dax_region(dev);
	int nid = phys_to_target_node(cxlr_dax->hpa_range.start);
	struct cxl_region *cxlr = cxlr_dax->cxlr;
	struct dax_region *dax_region;
	struct dev_dax_data data;

	if (nid == NUMA_NO_NODE)
		nid = memory_add_physaddr_to_nid(cxlr_dax->hpa_range.start);

	dax_region = alloc_dax_region(dev, cxlr->id, &cxlr_dax->hpa_range, nid,
				      PMD_SIZE, IORESOURCE_DAX_KMEM);
	if (!dax_region)
		return -ENOMEM;

	data = (struct dev_dax_data) {
		.dax_region = dax_region,
		.id = -1,
		.size = range_len(&cxlr_dax->hpa_range),
		.memmap_on_memory = true,
	};

	return PTR_ERR_OR_ZERO(devm_create_dev_dax(&data));
}

static struct cxl_driver cxl_dax_region_driver = {
	.name = "cxl_dax_region",
	.probe = cxl_dax_region_probe,
	.id = CXL_DEVICE_DAX_REGION,
	.drv = {
		.suppress_bind_attrs = true,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};

static void cxl_dax_region_driver_register(struct work_struct *work)
{
	dax_hmem_flush_work();
	cxl_driver_register(&cxl_dax_region_driver);
}

static DECLARE_WORK(cxl_dax_region_driver_work, cxl_dax_region_driver_register);

static int __init cxl_dax_region_init(void)
{
	/*
	 * Need to resolve a race with dax_hmem wanting to drive regions
	 * instead of CXL
	 */
	queue_work(system_long_wq, &cxl_dax_region_driver_work);
	return 0;
}
module_init(cxl_dax_region_init);

static void __exit cxl_dax_region_exit(void)
{
	flush_work(&cxl_dax_region_driver_work);
	cxl_driver_unregister(&cxl_dax_region_driver);
}
module_exit(cxl_dax_region_exit);

MODULE_ALIAS_CXL(CXL_DEVICE_DAX_REGION);
MODULE_DESCRIPTION("CXL DAX: direct access to CXL regions");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Intel Corporation");
MODULE_IMPORT_NS("CXL");
