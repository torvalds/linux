// SPDX-License-Identifier: GPL-2.0
/*
 * Generic DFL driver for Userspace I/O devicess
 *
 * Copyright (C) 2021 Intel Corporation, Inc.
 */
#include <linux/dfl.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/uio_driver.h>

#define DRIVER_NAME "uio_dfl"

static int uio_dfl_probe(struct dfl_device *ddev)
{
	struct resource *r = &ddev->mmio_res;
	struct device *dev = &ddev->dev;
	struct uio_info *uioinfo;
	struct uio_mem *uiomem;
	int ret;

	uioinfo = devm_kzalloc(dev, sizeof(struct uio_info), GFP_KERNEL);
	if (!uioinfo)
		return -ENOMEM;

	uioinfo->name = DRIVER_NAME;
	uioinfo->version = "0";

	uiomem = &uioinfo->mem[0];
	uiomem->memtype = UIO_MEM_PHYS;
	uiomem->addr = r->start & PAGE_MASK;
	uiomem->offs = r->start & ~PAGE_MASK;
	uiomem->size = (uiomem->offs + resource_size(r)
			+ PAGE_SIZE - 1) & PAGE_MASK;
	uiomem->name = r->name;

	/* Irq is yet to be supported */
	uioinfo->irq = UIO_IRQ_NONE;

	ret = devm_uio_register_device(dev, uioinfo);
	if (ret)
		dev_err(dev, "unable to register uio device\n");

	return ret;
}

#define FME_FEATURE_ID_ETH_GROUP	0x10

static const struct dfl_device_id uio_dfl_ids[] = {
	{ FME_ID, FME_FEATURE_ID_ETH_GROUP },
	{ }
};
MODULE_DEVICE_TABLE(dfl, uio_dfl_ids);

static struct dfl_driver uio_dfl_driver = {
	.drv = {
		.name = DRIVER_NAME,
	},
	.id_table	= uio_dfl_ids,
	.probe		= uio_dfl_probe,
};
module_dfl_driver(uio_dfl_driver);

MODULE_DESCRIPTION("Generic DFL driver for Userspace I/O devices");
MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
