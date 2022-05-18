// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation. All rights reserved. */
#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "cxlmem.h"
#include "cxlpci.h"

/**
 * DOC: cxl port
 *
 * The port driver enumerates dport via PCI and scans for HDM
 * (Host-managed-Device-Memory) decoder resources via the
 * @component_reg_phys value passed in by the agent that registered the
 * port. All descendant ports of a CXL root port (described by platform
 * firmware) are managed in this drivers context. Each driver instance
 * is responsible for tearing down the driver context of immediate
 * descendant ports. The locking for this is validated by
 * CONFIG_PROVE_CXL_LOCKING.
 *
 * The primary service this driver provides is presenting APIs to other
 * drivers to utilize the decoders, and indicating to userspace (via bind
 * status) the connectivity of the CXL.mem protocol throughout the
 * PCIe topology.
 */

static void schedule_detach(void *cxlmd)
{
	schedule_cxl_memdev_detach(cxlmd);
}

static int cxl_port_probe(struct device *dev)
{
	struct cxl_port *port = to_cxl_port(dev);
	struct cxl_hdm *cxlhdm;
	int rc;


	if (!is_cxl_endpoint(port)) {
		rc = devm_cxl_port_enumerate_dports(port);
		if (rc < 0)
			return rc;
		if (rc == 1)
			return devm_cxl_add_passthrough_decoder(port);
	}

	cxlhdm = devm_cxl_setup_hdm(port);
	if (IS_ERR(cxlhdm))
		return PTR_ERR(cxlhdm);

	if (is_cxl_endpoint(port)) {
		struct cxl_memdev *cxlmd = to_cxl_memdev(port->uport);
		struct cxl_dev_state *cxlds = cxlmd->cxlds;

		get_device(&cxlmd->dev);
		rc = devm_add_action_or_reset(dev, schedule_detach, cxlmd);
		if (rc)
			return rc;

		rc = cxl_hdm_decode_init(cxlds, cxlhdm);
		if (rc)
			return rc;

		rc = cxl_await_media_ready(cxlds);
		if (rc) {
			dev_err(dev, "Media not active (%d)\n", rc);
			return rc;
		}
	}

	rc = devm_cxl_enumerate_decoders(cxlhdm);
	if (rc) {
		dev_err(dev, "Couldn't enumerate decoders (%d)\n", rc);
		return rc;
	}

	return 0;
}

static struct cxl_driver cxl_port_driver = {
	.name = "cxl_port",
	.probe = cxl_port_probe,
	.id = CXL_DEVICE_PORT,
};

module_cxl_driver(cxl_port_driver);
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(CXL);
MODULE_ALIAS_CXL(CXL_DEVICE_PORT);
