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

		/* Cache the data early to ensure is_visible() works */
		read_cdat_data(port);

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

static ssize_t CDAT_read(struct file *filp, struct kobject *kobj,
			 struct bin_attribute *bin_attr, char *buf,
			 loff_t offset, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct cxl_port *port = to_cxl_port(dev);

	if (!port->cdat_available)
		return -ENXIO;

	if (!port->cdat.table)
		return 0;

	return memory_read_from_buffer(buf, count, &offset,
				       port->cdat.table,
				       port->cdat.length);
}

static BIN_ATTR_ADMIN_RO(CDAT, 0);

static umode_t cxl_port_bin_attr_is_visible(struct kobject *kobj,
					    struct bin_attribute *attr, int i)
{
	struct device *dev = kobj_to_dev(kobj);
	struct cxl_port *port = to_cxl_port(dev);

	if ((attr == &bin_attr_CDAT) && port->cdat_available)
		return attr->attr.mode;

	return 0;
}

static struct bin_attribute *cxl_cdat_bin_attributes[] = {
	&bin_attr_CDAT,
	NULL,
};

static struct attribute_group cxl_cdat_attribute_group = {
	.bin_attrs = cxl_cdat_bin_attributes,
	.is_bin_visible = cxl_port_bin_attr_is_visible,
};

static const struct attribute_group *cxl_port_attribute_groups[] = {
	&cxl_cdat_attribute_group,
	NULL,
};

static struct cxl_driver cxl_port_driver = {
	.name = "cxl_port",
	.probe = cxl_port_probe,
	.id = CXL_DEVICE_PORT,
	.drv = {
		.dev_groups = cxl_port_attribute_groups,
	},
};

module_cxl_driver(cxl_port_driver);
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(CXL);
MODULE_ALIAS_CXL(CXL_DEVICE_PORT);
