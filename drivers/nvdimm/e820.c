// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015, Christoph Hellwig.
 * Copyright (c) 2015, Intel Corporation.
 */
#include <linux/platform_device.h>
#include <linux/memory_hotplug.h>
#include <linux/libnvdimm.h>
#include <linux/module.h>

static int e820_pmem_remove(struct platform_device *pdev)
{
	struct nvdimm_bus *nvdimm_bus = platform_get_drvdata(pdev);

	nvdimm_bus_unregister(nvdimm_bus);
	return 0;
}

#ifdef CONFIG_MEMORY_HOTPLUG
static int e820_range_to_nid(resource_size_t addr)
{
	return memory_add_physaddr_to_nid(addr);
}
#else
static int e820_range_to_nid(resource_size_t addr)
{
	return NUMA_NO_NODE;
}
#endif

static int e820_register_one(struct resource *res, void *data)
{
	struct nd_region_desc ndr_desc;
	struct nvdimm_bus *nvdimm_bus = data;

	memset(&ndr_desc, 0, sizeof(ndr_desc));
	ndr_desc.res = res;
	ndr_desc.numa_node = e820_range_to_nid(res->start);
	ndr_desc.target_node = ndr_desc.numa_node;
	set_bit(ND_REGION_PAGEMAP, &ndr_desc.flags);
	if (!nvdimm_pmem_region_create(nvdimm_bus, &ndr_desc))
		return -ENXIO;
	return 0;
}

static int e820_pmem_probe(struct platform_device *pdev)
{
	static struct nvdimm_bus_descriptor nd_desc;
	struct device *dev = &pdev->dev;
	struct nvdimm_bus *nvdimm_bus;
	int rc = -ENXIO;

	nd_desc.provider_name = "e820";
	nd_desc.module = THIS_MODULE;
	nvdimm_bus = nvdimm_bus_register(dev, &nd_desc);
	if (!nvdimm_bus)
		goto err;
	platform_set_drvdata(pdev, nvdimm_bus);

	rc = walk_iomem_res_desc(IORES_DESC_PERSISTENT_MEMORY_LEGACY,
			IORESOURCE_MEM, 0, -1, nvdimm_bus, e820_register_one);
	if (rc)
		goto err;
	return 0;
err:
	nvdimm_bus_unregister(nvdimm_bus);
	dev_err(dev, "failed to register legacy persistent memory ranges\n");
	return rc;
}

static struct platform_driver e820_pmem_driver = {
	.probe = e820_pmem_probe,
	.remove = e820_pmem_remove,
	.driver = {
		.name = "e820_pmem",
	},
};

module_platform_driver(e820_pmem_driver);

MODULE_ALIAS("platform:e820_pmem*");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Intel Corporation");
