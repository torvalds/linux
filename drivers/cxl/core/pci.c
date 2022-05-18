// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2021 Intel Corporation. All rights reserved. */
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <cxlpci.h>
#include <cxlmem.h>
#include <cxl.h>
#include "core.h"

/**
 * DOC: cxl core pci
 *
 * Compute Express Link protocols are layered on top of PCIe. CXL core provides
 * a set of helpers for CXL interactions which occur via PCIe.
 */

static unsigned short media_ready_timeout = 60;
module_param(media_ready_timeout, ushort, 0644);
MODULE_PARM_DESC(media_ready_timeout, "seconds to wait for media ready");

struct cxl_walk_context {
	struct pci_bus *bus;
	struct cxl_port *port;
	int type;
	int error;
	int count;
};

static int match_add_dports(struct pci_dev *pdev, void *data)
{
	struct cxl_walk_context *ctx = data;
	struct cxl_port *port = ctx->port;
	int type = pci_pcie_type(pdev);
	struct cxl_register_map map;
	struct cxl_dport *dport;
	u32 lnkcap, port_num;
	int rc;

	if (pdev->bus != ctx->bus)
		return 0;
	if (!pci_is_pcie(pdev))
		return 0;
	if (type != ctx->type)
		return 0;
	if (pci_read_config_dword(pdev, pci_pcie_cap(pdev) + PCI_EXP_LNKCAP,
				  &lnkcap))
		return 0;

	rc = cxl_find_regblock(pdev, CXL_REGLOC_RBI_COMPONENT, &map);
	if (rc)
		dev_dbg(&port->dev, "failed to find component registers\n");

	port_num = FIELD_GET(PCI_EXP_LNKCAP_PN, lnkcap);
	dport = devm_cxl_add_dport(port, &pdev->dev, port_num,
				   cxl_regmap_to_base(pdev, &map));
	if (IS_ERR(dport)) {
		ctx->error = PTR_ERR(dport);
		return PTR_ERR(dport);
	}
	ctx->count++;

	dev_dbg(&port->dev, "add dport%d: %s\n", port_num, dev_name(&pdev->dev));

	return 0;
}

/**
 * devm_cxl_port_enumerate_dports - enumerate downstream ports of the upstream port
 * @port: cxl_port whose ->uport is the upstream of dports to be enumerated
 *
 * Returns a positive number of dports enumerated or a negative error
 * code.
 */
int devm_cxl_port_enumerate_dports(struct cxl_port *port)
{
	struct pci_bus *bus = cxl_port_to_pci_bus(port);
	struct cxl_walk_context ctx;
	int type;

	if (!bus)
		return -ENXIO;

	if (pci_is_root_bus(bus))
		type = PCI_EXP_TYPE_ROOT_PORT;
	else
		type = PCI_EXP_TYPE_DOWNSTREAM;

	ctx = (struct cxl_walk_context) {
		.port = port,
		.bus = bus,
		.type = type,
	};
	pci_walk_bus(bus, match_add_dports, &ctx);

	if (ctx.count == 0)
		return -ENODEV;
	if (ctx.error)
		return ctx.error;
	return ctx.count;
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_port_enumerate_dports, CXL);

/*
 * Wait up to @media_ready_timeout for the device to report memory
 * active.
 */
int cxl_await_media_ready(struct cxl_dev_state *cxlds)
{
	struct pci_dev *pdev = to_pci_dev(cxlds->dev);
	int d = cxlds->cxl_dvsec;
	bool active = false;
	u64 md_status;
	int rc, i;

	for (i = media_ready_timeout; i; i--) {
		u32 temp;

		rc = pci_read_config_dword(
			pdev, d + CXL_DVSEC_RANGE_SIZE_LOW(0), &temp);
		if (rc)
			return rc;

		active = FIELD_GET(CXL_DVSEC_MEM_ACTIVE, temp);
		if (active)
			break;
		msleep(1000);
	}

	if (!active) {
		dev_err(&pdev->dev,
			"timeout awaiting memory active after %d seconds\n",
			media_ready_timeout);
		return -ETIMEDOUT;
	}

	md_status = readq(cxlds->regs.memdev + CXLMDEV_STATUS_OFFSET);
	if (!CXLMDEV_READY(md_status))
		return -EIO;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cxl_await_media_ready, CXL);
