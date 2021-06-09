// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2021 Intel Corporation. All rights reserved. */
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/acpi.h>
#include "cxl.h"

static struct acpi_device *to_cxl_host_bridge(struct device *dev)
{
	struct acpi_device *adev = to_acpi_device(dev);

	if (strcmp(acpi_device_hid(adev), "ACPI0016") == 0)
		return adev;
	return NULL;
}

static int add_host_bridge_dport(struct device *match, void *arg)
{
	int rc;
	acpi_status status;
	unsigned long long uid;
	struct cxl_port *root_port = arg;
	struct device *host = root_port->dev.parent;
	struct acpi_device *bridge = to_cxl_host_bridge(match);

	if (!bridge)
		return 0;

	status = acpi_evaluate_integer(bridge->handle, METHOD_NAME__UID, NULL,
				       &uid);
	if (status != AE_OK) {
		dev_err(host, "unable to retrieve _UID of %s\n",
			dev_name(match));
		return -ENODEV;
	}

	rc = cxl_add_dport(root_port, match, uid, CXL_RESOURCE_NONE);
	if (rc) {
		dev_err(host, "failed to add downstream port: %s\n",
			dev_name(match));
		return rc;
	}
	dev_dbg(host, "add dport%llu: %s\n", uid, dev_name(match));
	return 0;
}

static int cxl_acpi_probe(struct platform_device *pdev)
{
	struct cxl_port *root_port;
	struct device *host = &pdev->dev;
	struct acpi_device *adev = ACPI_COMPANION(host);

	root_port = devm_cxl_add_port(host, host, CXL_RESOURCE_NONE, NULL);
	if (IS_ERR(root_port))
		return PTR_ERR(root_port);
	dev_dbg(host, "add: %s\n", dev_name(&root_port->dev));

	return bus_for_each_dev(adev->dev.bus, NULL, root_port,
				add_host_bridge_dport);
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
