/*
 * Copyright (c) 2015, Christoph Hellwig.
 * Copyright (c) 2015, Intel Corporation.
 */
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/ioport.h>

static int found(u64 start, u64 end, void *data)
{
	return 1;
}

static __init int register_e820_pmem(void)
{
	struct platform_device *pdev;
	int rc;

	rc = walk_iomem_res_desc(IORES_DESC_PERSISTENT_MEMORY_LEGACY,
				 IORESOURCE_MEM, 0, -1, NULL, found);
	if (rc <= 0)
		return 0;

	/*
	 * See drivers/nvdimm/e820.c for the implementation, this is
	 * simply here to trigger the module to load on demand.
	 */
	pdev = platform_device_alloc("e820_pmem", -1);
	return platform_device_add(pdev);
}
device_initcall(register_e820_pmem);
