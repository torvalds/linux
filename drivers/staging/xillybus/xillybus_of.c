/*
 * linux/drivers/misc/xillybus_of.c
 *
 * Copyright 2011 Xillybus Ltd, http://xillybus.com
 *
 * Driver for the Xillybus FPGA/host framework using Open Firmware.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the smems of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include "xillybus.h"

MODULE_DESCRIPTION("Xillybus driver for Open Firmware");
MODULE_AUTHOR("Eli Billauer, Xillybus Ltd.");
MODULE_VERSION("1.06");
MODULE_ALIAS("xillybus_of");
MODULE_LICENSE("GPL v2");

static const char xillyname[] = "xillybus_of";

/* Match table for of_platform binding */
static struct of_device_id xillybus_of_match[] = {
	{ .compatible = "xlnx,xillybus-1.00.a", },
	{}
};

MODULE_DEVICE_TABLE(of, xillybus_of_match);

static void xilly_dma_sync_single_for_cpu_of(struct xilly_endpoint *ep,
					     dma_addr_t dma_handle,
					     size_t size,
					     int direction)
{
	dma_sync_single_for_cpu(ep->dev, dma_handle, size, direction);
}

static void xilly_dma_sync_single_for_device_of(struct xilly_endpoint *ep,
						dma_addr_t dma_handle,
						size_t size,
						int direction)
{
	dma_sync_single_for_device(ep->dev, dma_handle, size, direction);
}

static dma_addr_t xilly_map_single_of(struct xilly_cleanup *mem,
				      struct xilly_endpoint *ep,
				      void *ptr,
				      size_t size,
				      int direction
	)
{

	dma_addr_t addr = 0;
	struct xilly_dma *this;

	this = kmalloc(sizeof(struct xilly_dma), GFP_KERNEL);
	if (!this)
		return 0;

	addr = dma_map_single(ep->dev, ptr, size, direction);
	this->direction = direction;

	if (dma_mapping_error(ep->dev, addr)) {
		kfree(this);
		return 0;
	}

	this->dma_addr = addr;
	this->dev = ep->dev;
	this->size = size;

	list_add_tail(&this->node, &mem->to_unmap);

	return addr;
}

static void xilly_unmap_single_of(struct xilly_dma *entry)
{
	dma_unmap_single(entry->dev,
			 entry->dma_addr,
			 entry->size,
			 entry->direction);
}

static struct xilly_endpoint_hardware of_hw = {
	.owner = THIS_MODULE,
	.hw_sync_sgl_for_cpu = xilly_dma_sync_single_for_cpu_of,
	.hw_sync_sgl_for_device = xilly_dma_sync_single_for_device_of,
	.map_single = xilly_map_single_of,
	.unmap_single = xilly_unmap_single_of
};

static int xilly_drv_probe(struct platform_device *op)
{
	struct device *dev = &op->dev;
	struct xilly_endpoint *endpoint;
	int rc = 0;
	int irq;

	endpoint = xillybus_init_endpoint(NULL, dev, &of_hw);

	if (!endpoint)
		return -ENOMEM;

	dev_set_drvdata(dev, endpoint);

	rc = of_address_to_resource(dev->of_node, 0, &endpoint->res);
	if (rc) {
		dev_warn(endpoint->dev,
			 "Failed to obtain device tree resource\n");
		goto failed_request_regions;
	}

	if  (!request_mem_region(endpoint->res.start,
				 resource_size(&endpoint->res), xillyname)) {
		dev_err(endpoint->dev,
			"request_mem_region failed. Aborting.\n");
		rc = -EBUSY;
		goto failed_request_regions;
	}

	endpoint->registers = of_iomap(dev->of_node, 0);

	if (!endpoint->registers) {
		dev_err(endpoint->dev,
			"Failed to map I/O memory. Aborting.\n");
		goto failed_iomap0;
	}

	irq = irq_of_parse_and_map(dev->of_node, 0);

	rc = request_irq(irq, xillybus_isr, 0, xillyname, endpoint);

	if (rc) {
		dev_err(endpoint->dev,
			"Failed to register IRQ handler. Aborting.\n");
		rc = -ENODEV;
		goto failed_register_irq;
	}

	rc = xillybus_endpoint_discovery(endpoint);

	if (!rc)
		return 0;

	free_irq(irq, endpoint);

failed_register_irq:
	iounmap(endpoint->registers);
failed_iomap0:
	release_mem_region(endpoint->res.start,
			   resource_size(&endpoint->res));

failed_request_regions:
	xillybus_do_cleanup(&endpoint->cleanup, endpoint);

	kfree(endpoint);
	return rc;
}

static int xilly_drv_remove(struct platform_device *op)
{
	struct device *dev = &op->dev;
	struct xilly_endpoint *endpoint = dev_get_drvdata(dev);
	int irq = irq_of_parse_and_map(dev->of_node, 0);

	xillybus_endpoint_remove(endpoint);

	free_irq(irq, endpoint);

	iounmap(endpoint->registers);
	release_mem_region(endpoint->res.start,
			   resource_size(&endpoint->res));

	xillybus_do_cleanup(&endpoint->cleanup, endpoint);

	kfree(endpoint);

	return 0;
}

static struct platform_driver xillybus_platform_driver = {
	.probe = xilly_drv_probe,
	.remove = xilly_drv_remove,
	.driver = {
		.name = xillyname,
		.owner = THIS_MODULE,
		.of_match_table = xillybus_of_match,
	},
};

module_platform_driver(xillybus_platform_driver);
