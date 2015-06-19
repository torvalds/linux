/*
 * Copyright (c) 2015, Christoph Hellwig.
 * Copyright (c) 2015, Intel Corporation.
 */
#include <linux/platform_device.h>
#include <linux/libnvdimm.h>
#include <linux/module.h>
#include <asm/e820.h>

static void e820_pmem_release(struct device *dev)
{
	struct nvdimm_bus *nvdimm_bus = dev->platform_data;

	if (nvdimm_bus)
		nvdimm_bus_unregister(nvdimm_bus);
}

static struct platform_device e820_pmem = {
	.name = "e820_pmem",
	.id = -1,
	.dev = {
		.release = e820_pmem_release,
	},
};

static const struct attribute_group *e820_pmem_attribute_groups[] = {
	&nvdimm_bus_attribute_group,
	NULL,
};

static const struct attribute_group *e820_pmem_region_attribute_groups[] = {
	&nd_region_attribute_group,
	&nd_device_attribute_group,
	NULL,
};

static __init int register_e820_pmem(void)
{
	static struct nvdimm_bus_descriptor nd_desc;
	struct device *dev = &e820_pmem.dev;
	struct nvdimm_bus *nvdimm_bus;
	int rc, i;

	rc = platform_device_register(&e820_pmem);
	if (rc)
		return rc;

	nd_desc.attr_groups = e820_pmem_attribute_groups;
	nd_desc.provider_name = "e820";
	nvdimm_bus = nvdimm_bus_register(dev, &nd_desc);
	if (!nvdimm_bus)
		goto err;
	dev->platform_data = nvdimm_bus;

	for (i = 0; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];
		struct resource res = {
			.flags	= IORESOURCE_MEM,
			.start	= ei->addr,
			.end	= ei->addr + ei->size - 1,
		};
		struct nd_region_desc ndr_desc;

		if (ei->type != E820_PRAM)
			continue;

		memset(&ndr_desc, 0, sizeof(ndr_desc));
		ndr_desc.res = &res;
		ndr_desc.attr_groups = e820_pmem_region_attribute_groups;
		ndr_desc.numa_node = NUMA_NO_NODE;
		if (!nvdimm_pmem_region_create(nvdimm_bus, &ndr_desc))
			goto err;
	}

	return 0;

 err:
	dev_err(dev, "failed to register legacy persistent memory ranges\n");
	platform_device_unregister(&e820_pmem);
	return -ENXIO;
}
device_initcall(register_e820_pmem);
