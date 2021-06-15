// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2021 Intel Corporation. All rights reserved. */
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/pci.h>
#include "cxl.h"

struct cxl_walk_context {
	struct device *dev;
	struct pci_bus *root;
	struct cxl_port *port;
	int error;
	int count;
};

static int match_add_root_ports(struct pci_dev *pdev, void *data)
{
	struct cxl_walk_context *ctx = data;
	struct pci_bus *root_bus = ctx->root;
	struct cxl_port *port = ctx->port;
	int type = pci_pcie_type(pdev);
	struct device *dev = ctx->dev;
	u32 lnkcap, port_num;
	int rc;

	if (pdev->bus != root_bus)
		return 0;
	if (!pci_is_pcie(pdev))
		return 0;
	if (type != PCI_EXP_TYPE_ROOT_PORT)
		return 0;
	if (pci_read_config_dword(pdev, pci_pcie_cap(pdev) + PCI_EXP_LNKCAP,
				  &lnkcap) != PCIBIOS_SUCCESSFUL)
		return 0;

	/* TODO walk DVSEC to find component register base */
	port_num = FIELD_GET(PCI_EXP_LNKCAP_PN, lnkcap);
	rc = cxl_add_dport(port, &pdev->dev, port_num, CXL_RESOURCE_NONE);
	if (rc) {
		ctx->error = rc;
		return rc;
	}
	ctx->count++;

	dev_dbg(dev, "add dport%d: %s\n", port_num, dev_name(&pdev->dev));

	return 0;
}

static struct acpi_device *to_cxl_host_bridge(struct device *dev)
{
	struct acpi_device *adev = to_acpi_device(dev);

	if (strcmp(acpi_device_hid(adev), "ACPI0016") == 0)
		return adev;
	return NULL;
}

/*
 * A host bridge is a dport to a CFMWS decode and it is a uport to the
 * dport (PCIe Root Ports) in the host bridge.
 */
static int add_host_bridge_uport(struct device *match, void *arg)
{
	struct acpi_device *bridge = to_cxl_host_bridge(match);
	struct cxl_port *root_port = arg;
	struct device *host = root_port->dev.parent;
	struct acpi_pci_root *pci_root;
	struct cxl_walk_context ctx;
	struct cxl_decoder *cxld;
	struct cxl_port *port;

	if (!bridge)
		return 0;

	pci_root = acpi_pci_find_root(bridge->handle);
	if (!pci_root)
		return -ENXIO;

	/* TODO: fold in CEDT.CHBS retrieval */
	port = devm_cxl_add_port(host, match, CXL_RESOURCE_NONE, root_port);
	if (IS_ERR(port))
		return PTR_ERR(port);
	dev_dbg(host, "%s: add: %s\n", dev_name(match), dev_name(&port->dev));

	ctx = (struct cxl_walk_context){
		.dev = host,
		.root = pci_root->bus,
		.port = port,
	};
	pci_walk_bus(pci_root->bus, match_add_root_ports, &ctx);

	if (ctx.count == 0)
		return -ENODEV;
	if (ctx.error)
		return ctx.error;

	/* TODO: Scan CHBCR for HDM Decoder resources */

	/*
	 * In the single-port host-bridge case there are no HDM decoders
	 * in the CHBCR and a 1:1 passthrough decode is implied.
	 */
	if (ctx.count == 1) {
		cxld = devm_cxl_add_passthrough_decoder(host, port);
		if (IS_ERR(cxld))
			return PTR_ERR(cxld);

		dev_dbg(host, "add: %s\n", dev_name(&cxld->dev));
	}

	return 0;
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

static int add_root_nvdimm_bridge(struct device *match, void *data)
{
	struct cxl_decoder *cxld;
	struct cxl_port *root_port = data;
	struct cxl_nvdimm_bridge *cxl_nvb;
	struct device *host = root_port->dev.parent;

	if (!is_root_decoder(match))
		return 0;

	cxld = to_cxl_decoder(match);
	if (!(cxld->flags & CXL_DECODER_F_PMEM))
		return 0;

	cxl_nvb = devm_cxl_add_nvdimm_bridge(host, root_port);
	if (IS_ERR(cxl_nvb)) {
		dev_dbg(host, "failed to register pmem\n");
		return PTR_ERR(cxl_nvb);
	}
	dev_dbg(host, "%s: add: %s\n", dev_name(&root_port->dev),
		dev_name(&cxl_nvb->dev));
	return 1;
}

static int cxl_acpi_probe(struct platform_device *pdev)
{
	int rc;
	struct cxl_port *root_port;
	struct device *host = &pdev->dev;
	struct acpi_device *adev = ACPI_COMPANION(host);

	root_port = devm_cxl_add_port(host, host, CXL_RESOURCE_NONE, NULL);
	if (IS_ERR(root_port))
		return PTR_ERR(root_port);
	dev_dbg(host, "add: %s\n", dev_name(&root_port->dev));

	rc = bus_for_each_dev(adev->dev.bus, NULL, root_port,
			      add_host_bridge_dport);
	if (rc)
		return rc;

	/*
	 * Root level scanned with host-bridge as dports, now scan host-bridges
	 * for their role as CXL uports to their CXL-capable PCIe Root Ports.
	 */
	rc = bus_for_each_dev(adev->dev.bus, NULL, root_port,
			      add_host_bridge_uport);
	if (rc)
		return rc;

	if (IS_ENABLED(CONFIG_CXL_PMEM))
		rc = device_for_each_child(&root_port->dev, root_port,
					   add_root_nvdimm_bridge);
	if (rc < 0)
		return rc;
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
