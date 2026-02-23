// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation. All rights reserved. */
#include <linux/aer.h>
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

static int discover_region(struct device *dev, void *unused)
{
	struct cxl_endpoint_decoder *cxled;
	int rc;

	if (!is_endpoint_decoder(dev))
		return 0;

	cxled = to_cxl_endpoint_decoder(dev);
	if ((cxled->cxld.flags & CXL_DECODER_F_ENABLE) == 0)
		return 0;

	if (cxled->state != CXL_DECODER_STATE_AUTO)
		return 0;

	/*
	 * Region enumeration is opportunistic, if this add-event fails,
	 * continue to the next endpoint decoder.
	 */
	rc = cxl_add_to_region(cxled);
	if (rc)
		dev_dbg(dev, "failed to add to region: %#llx-%#llx\n",
			cxled->cxld.hpa_range.start, cxled->cxld.hpa_range.end);

	return 0;
}

static int cxl_switch_port_probe(struct cxl_port *port)
{
	/* Reset nr_dports for rebind of driver */
	port->nr_dports = 0;

	/* Cache the data early to ensure is_visible() works */
	read_cdat_data(port);

	return 0;
}

static int cxl_ras_unmask(struct cxl_port *port)
{
	struct pci_dev *pdev;
	void __iomem *addr;
	u32 orig_val, val, mask;
	u16 cap;
	int rc;

	if (!dev_is_pci(port->uport_dev))
		return 0;
	pdev = to_pci_dev(port->uport_dev);

	if (!port->regs.ras) {
		pci_dbg(pdev, "No RAS registers.\n");
		return 0;
	}

	/* BIOS has PCIe AER error control */
	if (!pcie_aer_is_native(pdev))
		return 0;

	rc = pcie_capability_read_word(pdev, PCI_EXP_DEVCTL, &cap);
	if (rc)
		return rc;

	if (cap & PCI_EXP_DEVCTL_URRE) {
		addr = port->regs.ras + CXL_RAS_UNCORRECTABLE_MASK_OFFSET;
		orig_val = readl(addr);

		mask = CXL_RAS_UNCORRECTABLE_MASK_MASK |
		       CXL_RAS_UNCORRECTABLE_MASK_F256B_MASK;
		val = orig_val & ~mask;
		writel(val, addr);
		pci_dbg(pdev, "Uncorrectable RAS Errors Mask: %#x -> %#x\n",
			orig_val, val);
	}

	if (cap & PCI_EXP_DEVCTL_CERE) {
		addr = port->regs.ras + CXL_RAS_CORRECTABLE_MASK_OFFSET;
		orig_val = readl(addr);
		val = orig_val & ~CXL_RAS_CORRECTABLE_MASK_MASK;
		writel(val, addr);
		pci_dbg(pdev, "Correctable RAS Errors Mask: %#x -> %#x\n",
			orig_val, val);
	}

	return 0;
}

static int cxl_endpoint_port_probe(struct cxl_port *port)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(port->uport_dev);
	struct cxl_dport *dport = port->parent_dport;
	int rc;

	/* Cache the data early to ensure is_visible() works */
	read_cdat_data(port);
	cxl_endpoint_parse_cdat(port);

	get_device(&cxlmd->dev);
	rc = devm_add_action_or_reset(&port->dev, schedule_detach, cxlmd);
	if (rc)
		return rc;

	rc = devm_cxl_endpoint_decoders_setup(port);
	if (rc)
		return rc;

	/*
	 * With VH (CXL Virtual Host) topology the cxl_port::add_dport() method
	 * handles RAS setup for downstream ports. With RCH (CXL Restricted CXL
	 * Host) topologies the downstream port is enumerated early by platform
	 * firmware, but the RCRB (root complex register block) is not mapped
	 * until after the cxl_pci driver attaches to the RCIeP (root complex
	 * integrated endpoint).
	 */
	if (dport->rch)
		devm_cxl_dport_rch_ras_setup(dport);

	devm_cxl_port_ras_setup(port);
	if (cxl_ras_unmask(port))
		dev_dbg(&port->dev, "failed to unmask RAS interrupts\n");

	/*
	 * Now that all endpoint decoders are successfully enumerated, try to
	 * assemble regions from committed decoders
	 */
	device_for_each_child(&port->dev, NULL, discover_region);

	return 0;
}

static int cxl_port_probe(struct device *dev)
{
	struct cxl_port *port = to_cxl_port(dev);

	if (is_cxl_endpoint(port))
		return cxl_endpoint_port_probe(port);
	return cxl_switch_port_probe(port);
}

static ssize_t CDAT_read(struct file *filp, struct kobject *kobj,
			 const struct bin_attribute *bin_attr, char *buf,
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

static const BIN_ATTR_ADMIN_RO(CDAT, 0);

static umode_t cxl_port_bin_attr_is_visible(struct kobject *kobj,
					    const struct bin_attribute *attr, int i)
{
	struct device *dev = kobj_to_dev(kobj);
	struct cxl_port *port = to_cxl_port(dev);

	if ((attr == &bin_attr_CDAT) && port->cdat_available)
		return attr->attr.mode;

	return 0;
}

static const struct bin_attribute *const cxl_cdat_bin_attributes[] = {
	&bin_attr_CDAT,
	NULL,
};

static const struct attribute_group cxl_cdat_attribute_group = {
	.bin_attrs = cxl_cdat_bin_attributes,
	.is_bin_visible = cxl_port_bin_attr_is_visible,
};

static const struct attribute_group *cxl_port_attribute_groups[] = {
	&cxl_cdat_attribute_group,
	NULL,
};

/* note this implicitly casts the group back to its @port */
DEFINE_FREE(cxl_port_release_dr_group, struct cxl_port *,
	    if (_T) devres_release_group(&_T->dev, _T))

static struct cxl_dport *cxl_port_add_dport(struct cxl_port *port,
					    struct device *dport_dev)
{
	struct cxl_dport *dport;
	int rc;

	/* Temp group for all "first dport" and "per dport" setup actions */
	void *port_dr_group __free(cxl_port_release_dr_group) =
		devres_open_group(&port->dev, port, GFP_KERNEL);
	if (!port_dr_group)
		return ERR_PTR(-ENOMEM);

	if (port->nr_dports == 0) {
		/*
		 * Some host bridges are known to not have component regsisters
		 * available until a root port has trained CXL. Perform that
		 * setup now.
		 */
		rc = cxl_port_setup_regs(port, port->component_reg_phys);
		if (rc)
			return ERR_PTR(rc);

		rc = devm_cxl_switch_port_decoders_setup(port);
		if (rc)
			return ERR_PTR(rc);

		/*
		 * RAS setup is optional, either driver operation can continue
		 * on failure, or the device does not implement RAS registers.
		 */
		devm_cxl_port_ras_setup(port);
	}

	dport = devm_cxl_add_dport_by_dev(port, dport_dev);
	if (IS_ERR(dport))
		return dport;

	/* This group was only needed for early exit above */
	devres_remove_group(&port->dev, no_free_ptr(port_dr_group));

	cxl_switch_parse_cdat(dport);

	/* New dport added, update the decoder targets */
	cxl_port_update_decoder_targets(port, dport);

	dev_dbg(&port->dev, "dport%d:%s added\n", dport->port_id,
		dev_name(dport_dev));

	return dport;
}

static struct cxl_driver cxl_port_driver = {
	.name = "cxl_port",
	.probe = cxl_port_probe,
	.add_dport = cxl_port_add_dport,
	.id = CXL_DEVICE_PORT,
	.drv = {
		.probe_type = PROBE_FORCE_SYNCHRONOUS,
		.dev_groups = cxl_port_attribute_groups,
	},
};

int devm_cxl_add_endpoint(struct device *host, struct cxl_memdev *cxlmd,
			  struct cxl_dport *parent_dport)
{
	struct cxl_port *parent_port = parent_dport->port;
	struct cxl_port *endpoint, *iter, *down;
	int rc;

	/*
	 * Now that the path to the root is established record all the
	 * intervening ports in the chain.
	 */
	for (iter = parent_port, down = NULL; !is_cxl_root(iter);
	     down = iter, iter = to_cxl_port(iter->dev.parent)) {
		struct cxl_ep *ep;

		ep = cxl_ep_load(iter, cxlmd);
		ep->next = down;
	}

	/* Note: endpoint port component registers are derived from @cxlds */
	endpoint = devm_cxl_add_port(host, &cxlmd->dev, CXL_RESOURCE_NONE,
				     parent_dport);
	if (IS_ERR(endpoint))
		return PTR_ERR(endpoint);

	rc = cxl_endpoint_autoremove(cxlmd, endpoint);
	if (rc)
		return rc;

	if (!endpoint->dev.driver) {
		dev_err(&cxlmd->dev, "%s failed probe\n",
			dev_name(&endpoint->dev));
		return -ENXIO;
	}

	return 0;
}
EXPORT_SYMBOL_FOR_MODULES(devm_cxl_add_endpoint, "cxl_mem");

static int __init cxl_port_init(void)
{
	return cxl_driver_register(&cxl_port_driver);
}
/*
 * Be ready to immediately enable ports emitted by the platform CXL root
 * (e.g. cxl_acpi) when CONFIG_CXL_PORT=y.
 */
subsys_initcall(cxl_port_init);

static void __exit cxl_port_exit(void)
{
	cxl_driver_unregister(&cxl_port_driver);
}
module_exit(cxl_port_exit);

MODULE_DESCRIPTION("CXL: Port enumeration and services");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("CXL");
MODULE_ALIAS_CXL(CXL_DEVICE_PORT);
