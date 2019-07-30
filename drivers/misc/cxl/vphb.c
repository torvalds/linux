// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2014 IBM Corp.
 */

#include <linux/pci.h>
#include <misc/cxl.h>
#include "cxl.h"

static int cxl_pci_probe_mode(struct pci_bus *bus)
{
	return PCI_PROBE_NORMAL;
}

static int cxl_setup_msi_irqs(struct pci_dev *pdev, int nvec, int type)
{
	return -ENODEV;
}

static void cxl_teardown_msi_irqs(struct pci_dev *pdev)
{
	/*
	 * MSI should never be set but need still need to provide this call
	 * back.
	 */
}

static bool cxl_pci_enable_device_hook(struct pci_dev *dev)
{
	struct pci_controller *phb;
	struct cxl_afu *afu;
	struct cxl_context *ctx;

	phb = pci_bus_to_host(dev->bus);
	afu = (struct cxl_afu *)phb->private_data;

	if (!cxl_ops->link_ok(afu->adapter, afu)) {
		dev_warn(&dev->dev, "%s: Device link is down, refusing to enable AFU\n", __func__);
		return false;
	}

	dev->dev.archdata.dma_offset = PAGE_OFFSET;

	/*
	 * Allocate a context to do cxl things too.  If we eventually do real
	 * DMA ops, we'll need a default context to attach them to
	 */
	ctx = cxl_dev_context_init(dev);
	if (IS_ERR(ctx))
		return false;
	dev->dev.archdata.cxl_ctx = ctx;

	return (cxl_ops->afu_check_and_enable(afu) == 0);
}

static void cxl_pci_disable_device(struct pci_dev *dev)
{
	struct cxl_context *ctx = cxl_get_context(dev);

	if (ctx) {
		if (ctx->status == STARTED) {
			dev_err(&dev->dev, "Default context started\n");
			return;
		}
		dev->dev.archdata.cxl_ctx = NULL;
		cxl_release_context(ctx);
	}
}

static resource_size_t cxl_pci_window_alignment(struct pci_bus *bus,
						unsigned long type)
{
	return 1;
}

static void cxl_pci_reset_secondary_bus(struct pci_dev *dev)
{
	/* Should we do an AFU reset here ? */
}

static int cxl_pcie_cfg_record(u8 bus, u8 devfn)
{
	return (bus << 8) + devfn;
}

static inline struct cxl_afu *pci_bus_to_afu(struct pci_bus *bus)
{
	struct pci_controller *phb = bus ? pci_bus_to_host(bus) : NULL;

	return phb ? phb->private_data : NULL;
}

static void cxl_afu_configured_put(struct cxl_afu *afu)
{
	atomic_dec_if_positive(&afu->configured_state);
}

static bool cxl_afu_configured_get(struct cxl_afu *afu)
{
	return atomic_inc_unless_negative(&afu->configured_state);
}

static inline int cxl_pcie_config_info(struct pci_bus *bus, unsigned int devfn,
				       struct cxl_afu *afu, int *_record)
{
	int record;

	record = cxl_pcie_cfg_record(bus->number, devfn);
	if (record > afu->crs_num)
		return PCIBIOS_DEVICE_NOT_FOUND;

	*_record = record;
	return 0;
}

static int cxl_pcie_read_config(struct pci_bus *bus, unsigned int devfn,
				int offset, int len, u32 *val)
{
	int rc, record;
	struct cxl_afu *afu;
	u8 val8;
	u16 val16;
	u32 val32;

	afu = pci_bus_to_afu(bus);
	/* Grab a reader lock on afu. */
	if (afu == NULL || !cxl_afu_configured_get(afu))
		return PCIBIOS_DEVICE_NOT_FOUND;

	rc = cxl_pcie_config_info(bus, devfn, afu, &record);
	if (rc)
		goto out;

	switch (len) {
	case 1:
		rc = cxl_ops->afu_cr_read8(afu, record, offset,	&val8);
		*val = val8;
		break;
	case 2:
		rc = cxl_ops->afu_cr_read16(afu, record, offset, &val16);
		*val = val16;
		break;
	case 4:
		rc = cxl_ops->afu_cr_read32(afu, record, offset, &val32);
		*val = val32;
		break;
	default:
		WARN_ON(1);
	}

out:
	cxl_afu_configured_put(afu);
	return rc ? PCIBIOS_DEVICE_NOT_FOUND : PCIBIOS_SUCCESSFUL;
}

static int cxl_pcie_write_config(struct pci_bus *bus, unsigned int devfn,
				 int offset, int len, u32 val)
{
	int rc, record;
	struct cxl_afu *afu;

	afu = pci_bus_to_afu(bus);
	/* Grab a reader lock on afu. */
	if (afu == NULL || !cxl_afu_configured_get(afu))
		return PCIBIOS_DEVICE_NOT_FOUND;

	rc = cxl_pcie_config_info(bus, devfn, afu, &record);
	if (rc)
		goto out;

	switch (len) {
	case 1:
		rc = cxl_ops->afu_cr_write8(afu, record, offset, val & 0xff);
		break;
	case 2:
		rc = cxl_ops->afu_cr_write16(afu, record, offset, val & 0xffff);
		break;
	case 4:
		rc = cxl_ops->afu_cr_write32(afu, record, offset, val);
		break;
	default:
		WARN_ON(1);
	}

out:
	cxl_afu_configured_put(afu);
	return rc ? PCIBIOS_SET_FAILED : PCIBIOS_SUCCESSFUL;
}

static struct pci_ops cxl_pcie_pci_ops =
{
	.read = cxl_pcie_read_config,
	.write = cxl_pcie_write_config,
};


static struct pci_controller_ops cxl_pci_controller_ops =
{
	.probe_mode = cxl_pci_probe_mode,
	.enable_device_hook = cxl_pci_enable_device_hook,
	.disable_device = cxl_pci_disable_device,
	.release_device = cxl_pci_disable_device,
	.window_alignment = cxl_pci_window_alignment,
	.reset_secondary_bus = cxl_pci_reset_secondary_bus,
	.setup_msi_irqs = cxl_setup_msi_irqs,
	.teardown_msi_irqs = cxl_teardown_msi_irqs,
};

int cxl_pci_vphb_add(struct cxl_afu *afu)
{
	struct pci_controller *phb;
	struct device_node *vphb_dn;
	struct device *parent;

	/*
	 * If there are no AFU configuration records we won't have anything to
	 * expose under the vPHB, so skip creating one, returning success since
	 * this is still a valid case. This will also opt us out of EEH
	 * handling since we won't have anything special to do if there are no
	 * kernel drivers attached to the vPHB, and EEH handling is not yet
	 * supported in the peer model.
	 */
	if (!afu->crs_num)
		return 0;

	/* The parent device is the adapter. Reuse the device node of
	 * the adapter.
	 * We don't seem to care what device node is used for the vPHB,
	 * but tools such as lsvpd walk up the device parents looking
	 * for a valid location code, so we might as well show devices
	 * attached to the adapter as being located on that adapter.
	 */
	parent = afu->adapter->dev.parent;
	vphb_dn = parent->of_node;

	/* Alloc and setup PHB data structure */
	phb = pcibios_alloc_controller(vphb_dn);
	if (!phb)
		return -ENODEV;

	/* Setup parent in sysfs */
	phb->parent = parent;

	/* Setup the PHB using arch provided callback */
	phb->ops = &cxl_pcie_pci_ops;
	phb->cfg_addr = NULL;
	phb->cfg_data = NULL;
	phb->private_data = afu;
	phb->controller_ops = cxl_pci_controller_ops;

	/* Scan the bus */
	pcibios_scan_phb(phb);
	if (phb->bus == NULL)
		return -ENXIO;

	/* Set release hook on root bus */
	pci_set_host_bridge_release(to_pci_host_bridge(phb->bus->bridge),
				    pcibios_free_controller_deferred,
				    (void *) phb);

	/* Claim resources. This might need some rework as well depending
	 * whether we are doing probe-only or not, like assigning unassigned
	 * resources etc...
	 */
	pcibios_claim_one_bus(phb->bus);

	/* Add probed PCI devices to the device model */
	pci_bus_add_devices(phb->bus);

	afu->phb = phb;

	return 0;
}

void cxl_pci_vphb_remove(struct cxl_afu *afu)
{
	struct pci_controller *phb;

	/* If there is no configuration record we won't have one of these */
	if (!afu || !afu->phb)
		return;

	phb = afu->phb;
	afu->phb = NULL;

	pci_remove_root_bus(phb->bus);
	/*
	 * We don't free phb here - that's handled by
	 * pcibios_free_controller_deferred()
	 */
}

bool cxl_pci_is_vphb_device(struct pci_dev *dev)
{
	struct pci_controller *phb;

	phb = pci_bus_to_host(dev->bus);

	return (phb->ops == &cxl_pcie_pci_ops);
}

struct cxl_afu *cxl_pci_to_afu(struct pci_dev *dev)
{
	struct pci_controller *phb;

	phb = pci_bus_to_host(dev->bus);

	return (struct cxl_afu *)phb->private_data;
}
EXPORT_SYMBOL_GPL(cxl_pci_to_afu);

unsigned int cxl_pci_to_cfg_record(struct pci_dev *dev)
{
	return cxl_pcie_cfg_record(dev->bus->number, dev->devfn);
}
EXPORT_SYMBOL_GPL(cxl_pci_to_cfg_record);
