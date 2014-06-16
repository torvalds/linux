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
#include <linux/err.h>
#include "xillybus.h"

MODULE_DESCRIPTION("Xillybus driver for Open Firmware");
MODULE_AUTHOR("Eli Billauer, Xillybus Ltd.");
MODULE_VERSION("1.06");
MODULE_ALIAS("xillybus_of");
MODULE_LICENSE("GPL v2");

static const char xillyname[] = "xillybus_of";

/* Match table for of_platform binding */
static struct of_device_id xillybus_of_match[] = {
	{ .compatible = "xillybus,xillybus-1.00.a", },
	{ .compatible = "xlnx,xillybus-1.00.a", }, /* Deprecated */
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

static void xilly_dma_sync_single_nop(struct xilly_endpoint *ep,
				      dma_addr_t dma_handle,
				      size_t size,
				      int direction)
{
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

static struct xilly_endpoint_hardware of_hw_coherent = {
	.owner = THIS_MODULE,
	.hw_sync_sgl_for_cpu = xilly_dma_sync_single_nop,
	.hw_sync_sgl_for_device = xilly_dma_sync_single_nop,
	.map_single = xilly_map_single_of,
	.unmap_single = xilly_unmap_single_of
};

static int xilly_drv_probe(struct platform_device *op)
{
	struct device *dev = &op->dev;
	struct xilly_endpoint *endpoint;
	int rc = 0;
	int irq;
	struct resource res;
	struct xilly_endpoint_hardware *ephw = &of_hw;

	if (of_property_read_bool(dev->of_node, "dma-coherent"))
		ephw = &of_hw_coherent;

	endpoint = xillybus_init_endpoint(NULL, dev, ephw);

	if (!endpoint)
		return -ENOMEM;

	dev_set_drvdata(dev, endpoint);

	rc = of_address_to_resource(dev->of_node, 0, &res);
	if (rc) {
		dev_warn(endpoint->dev,
			 "Failed to obtain device tree resource\n");
		return rc;
	}

	endpoint->registers = devm_ioremap_resource(dev, &res);

	if (IS_ERR(endpoint->registers))
		return PTR_ERR(endpoint->registers);

	irq = irq_of_parse_and_map(dev->of_node, 0);

	rc = devm_request_irq(dev, irq, xillybus_isr, 0, xillyname, endpoint);

	if (rc) {
		dev_err(endpoint->dev,
			"Failed to register IRQ handler. Aborting.\n");
		return -ENODEV;
	}

	rc = xillybus_endpoint_discovery(endpoint);

	if (!rc)
		return 0;

	xillybus_do_cleanup(&endpoint->cleanup, endpoint);

	return rc;
}

static int xilly_drv_remove(struct platform_device *op)
{
	struct device *dev = &op->dev;
	struct xilly_endpoint *endpoint = dev_get_drvdata(dev);

	xillybus_endpoint_remove(endpoint);

	xillybus_do_cleanup(&endpoint->cleanup, endpoint);

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
