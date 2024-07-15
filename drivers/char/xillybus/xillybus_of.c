// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/drivers/misc/xillybus_of.c
 *
 * Copyright 2011 Xillybus Ltd, http://xillybus.com
 *
 * Driver for the Xillybus FPGA/host framework using Open Firmware.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/err.h>
#include "xillybus.h"

MODULE_DESCRIPTION("Xillybus driver for Open Firmware");
MODULE_AUTHOR("Eli Billauer, Xillybus Ltd.");
MODULE_ALIAS("xillybus_of");
MODULE_LICENSE("GPL v2");

static const char xillyname[] = "xillybus_of";

/* Match table for of_platform binding */
static const struct of_device_id xillybus_of_match[] = {
	{ .compatible = "xillybus,xillybus-1.00.a", },
	{ .compatible = "xlnx,xillybus-1.00.a", }, /* Deprecated */
	{}
};

MODULE_DEVICE_TABLE(of, xillybus_of_match);

static int xilly_drv_probe(struct platform_device *op)
{
	struct device *dev = &op->dev;
	struct xilly_endpoint *endpoint;
	int rc;
	int irq;

	endpoint = xillybus_init_endpoint(dev);

	if (!endpoint)
		return -ENOMEM;

	dev_set_drvdata(dev, endpoint);

	endpoint->owner = THIS_MODULE;

	endpoint->registers = devm_platform_ioremap_resource(op, 0);
	if (IS_ERR(endpoint->registers))
		return PTR_ERR(endpoint->registers);

	irq = platform_get_irq(op, 0);

	rc = devm_request_irq(dev, irq, xillybus_isr, 0, xillyname, endpoint);

	if (rc) {
		dev_err(endpoint->dev,
			"Failed to register IRQ handler. Aborting.\n");
		return -ENODEV;
	}

	return xillybus_endpoint_discovery(endpoint);
}

static void xilly_drv_remove(struct platform_device *op)
{
	struct device *dev = &op->dev;
	struct xilly_endpoint *endpoint = dev_get_drvdata(dev);

	xillybus_endpoint_remove(endpoint);
}

static struct platform_driver xillybus_platform_driver = {
	.probe = xilly_drv_probe,
	.remove_new = xilly_drv_remove,
	.driver = {
		.name = xillyname,
		.of_match_table = xillybus_of_match,
	},
};

module_platform_driver(xillybus_platform_driver);
