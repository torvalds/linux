/*
 * Copyright (c) 2015, Christoph Hellwig.
 * Copyright (c) 2015, Intel Corporation.
 */
#include <linux/platform_device.h>
#include <linux/module.h>

static __init int register_e820_pmem(void)
{
	struct platform_device *pdev;

	/*
	 * See drivers/nvdimm/e820.c for the implementation, this is
	 * simply here to trigger the module to load on demand.
	 */
	pdev = platform_device_alloc("e820_pmem", -1);
	return platform_device_add(pdev);
}
device_initcall(register_e820_pmem);
