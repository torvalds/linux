// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2021 Intel Corporation. All rights reserved. */
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/acpi.h>
#include "cxl.h"

static int cxl_acpi_probe(struct platform_device *pdev)
{
	struct cxl_port *root_port;
	struct device *host = &pdev->dev;

	root_port = devm_cxl_add_port(host, host, CXL_RESOURCE_NONE, NULL);
	if (IS_ERR(root_port))
		return PTR_ERR(root_port);
	dev_dbg(host, "add: %s\n", dev_name(&root_port->dev));

	return 0;
}

static const struct acpi_device_id cxl_acpi_ids[] = {
	{ "ACPI0017", 0 },
	{ "", 0 },
};
MODULE_DEVICE_TABLE(acpi, cxl_acpi_ids);

static struct platform_driver cxl_acpi_driver = {
	.probe = cxl_acpi_probe,
	.driver = {
		.name = KBUILD_MODNAME,
		.acpi_match_table = cxl_acpi_ids,
	},
};

module_platform_driver(cxl_acpi_driver);
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(CXL);
