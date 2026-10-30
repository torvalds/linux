// SPDX-License-Identifier: GPL-2.0-only

#include <linux/acpi.h>
#include <linux/acpi_iovt.h>
#include <linux/pci.h>
#include "init.h"

struct iovt_device_entry {
	struct list_head list;
	int start_devid, end_devid;
};

struct iovt_fwnode {
	struct list_head list;
	struct fwnode_handle *fwnode;
	int flag;
	int nid, segment, devid;
	struct list_head ep_list;
};

static LIST_HEAD(iovt_fwnode_list);
static DEFINE_SPINLOCK(iovt_fwnode_lock);

#ifdef CONFIG_PCI
static void __init iovt_enable_acs(struct acpi_iovt_iommu *iommu)
{
	static bool acs_enabled __initdata;

	if (acs_enabled)
		return;

	/* IOMMU V1 only supports PCI device management */
	if ((iommu->header.type == ACPI_IOVT_IOMMU_V1) ||
	    (iommu->flags & (ACPI_IOVT_IOMMU_PCI_DEVICE | ACPI_IOVT_IOMMU_MAGAGE_BY_SEGMENT))) {
		pci_request_acs();
		acs_enabled = true;
	}
}
#else
static inline void iovt_enable_acs(struct acpi_iovt_iommu *iommu) { }
#endif

static int __init iovt_get_pci_iommu_fwnode(struct iovt_fwnode *np, u16 segment, u16 bdf)
{
	struct pci_dev *pdev;
	struct fwnode_handle *fwnode;

	pdev = pci_get_domain_bus_and_slot(segment, PCI_BUS_NUM(bdf), bdf & 0xff);
	if (!pdev) {
		pr_err("No PCI IOMMU found for segment 0x%x bdf 0x%x\n", segment, bdf);
		return -ENODEV;
	}

	fwnode = dev_fwnode(&pdev->dev);
	if (!fwnode) {
		/*
		 * PCI devices aren't necessarily described by ACPI. Create
		 * a fwnode so the IOMMU subsystem can identify this device.
		 */
		fwnode = acpi_alloc_fwnode_static();
		if (!fwnode) {
			pci_dev_put(pdev);
			return -ENOMEM;
		}
		set_primary_fwnode(&pdev->dev, fwnode);
	}

	np->fwnode = dev_fwnode(&pdev->dev);
	if (np->flag & ACPI_IOVT_IOMMU_PXM_VALID)
		set_dev_node(&pdev->dev, np->nid);
	pci_dev_put(pdev);

	return 0;
}

static int __init iovt_add_iommu(struct acpi_iovt_iommu *iommu)
{
	int i, ret;
	bool is_start = false;
	struct iovt_fwnode *np;
	struct iovt_device_entry *entry;
	struct acpi_iovt_device_entry *ep;

	np = kzalloc_obj(struct iovt_fwnode, GFP_ATOMIC);
	if (WARN_ON(!np))
		return -ENOMEM;

	INIT_LIST_HEAD(&np->list);
	np->flag = iommu->flags;
	np->segment = iommu->segment;
	if (np->flag & ACPI_IOVT_IOMMU_PXM_VALID)
		np->nid = pxm_to_node(iommu->proximity_domain);

	if (np->flag & ACPI_IOVT_IOMMU_PCI_DEVICE) {
		np->devid = iommu->device_id;
		ret = iovt_get_pci_iommu_fwnode(np, np->segment, np->devid);
		if (ret < 0) {
			kfree(np);
			return ret;
		}
	} else {
		np->fwnode = acpi_alloc_fwnode_static();
		if (!np->fwnode) {
			kfree(np);
			return -ENOMEM;
		}
	}

	/* All devices in the segment are managed by this IOMMU */
	if (np->flag & ACPI_IOVT_IOMMU_MAGAGE_BY_SEGMENT)
		goto skip;

	INIT_LIST_HEAD(&np->ep_list);
	ep = ACPI_ADD_PTR(struct acpi_iovt_device_entry, iommu, iommu->device_entry_offset);
	for (i = 0; i < iommu->device_entry_num; i++) {
		switch (ep->type) {
		case ACPI_IOVT_DEVICE_ENTRY_START:
			is_start = true;
			entry->start_devid = ep->device_id;
			break;
		case ACPI_IOVT_DEVICE_ENTRY_END:
			if (!is_start)
				break;

			entry = kzalloc_obj(struct iovt_device_entry, GFP_ATOMIC);
			if (!entry)
				return -ENOMEM;

			is_start = false;
			entry->end_devid = ep->device_id;
			list_add_tail(&entry->list, &np->ep_list);
			break;
		case ACPI_IOVT_DEVICE_ENTRY_SINGLE:
			entry = kzalloc_obj(struct iovt_device_entry, GFP_ATOMIC);
			if (!entry)
				return -ENOMEM;

			is_start = false;
			entry->start_devid = ep->device_id;
			entry->end_devid = ep->device_id;
			list_add_tail(&entry->list, &np->ep_list);
			break;
		default:
			break;
		}
		ep = ACPI_ADD_PTR(struct acpi_iovt_device_entry, ep, ep->length);
	}

skip:
	spin_lock(&iovt_fwnode_lock);
	list_add_tail(&np->list, &iovt_fwnode_list);
	spin_unlock(&iovt_fwnode_lock);
	return 0;
}

static void __init iovt_init_devices(struct acpi_table_header *header)
{
	int i;
	struct acpi_table_iovt *iovt;
	struct acpi_iovt_iommu *iommu;

	/* Get the first IOVT node */
	iovt = (struct acpi_table_iovt *)header;
	iommu = ACPI_ADD_PTR(struct acpi_iovt_iommu, iovt, iovt->iommu_offset);
	for (i = 0; i < iovt->iommu_count; i++) {
		iovt_add_iommu(iommu);
		iommu = ACPI_ADD_PTR(struct acpi_iovt_iommu, iommu, iommu->header.length);
	}
}

void __init acpi_iovt_init(void)
{
	int i;
	acpi_status status;
	struct acpi_table_header *hdr;
	struct acpi_table_iovt *iovt;
	struct acpi_iovt_iommu *iommu;

	status = acpi_get_table(ACPI_SIG_IOVT, 0, &hdr);
	if (ACPI_FAILURE(status)) {
		if (status != AE_NOT_FOUND)
			pr_err("Failed to get table, %s\n", acpi_format_exception(status));

		return;
	}

	iovt = (struct acpi_table_iovt *)&hdr;
	iommu = ACPI_ADD_PTR(struct acpi_iovt_iommu, iovt, iovt->iommu_offset);
	for (i = 0; i < iovt->iommu_count; i++) {
		iovt_enable_acs(iommu);
		iommu = ACPI_ADD_PTR(struct acpi_iovt_iommu, iommu, iommu->header.length);
	}

	acpi_put_table(hdr);
}

void __init acpi_iovt_late_init(void)
{
	acpi_status status;
	struct acpi_table_header *hdr;

	status = acpi_get_table(ACPI_SIG_IOVT, 0, &hdr);
	if (ACPI_FAILURE(status)) {
		if (status != AE_NOT_FOUND)
			pr_err("Failed to get table, %s\n", acpi_format_exception(status));

		return;
	}

	iovt_init_devices(hdr);
	acpi_put_table(hdr);
}

static int iovt_pci_iommu_init(struct pci_dev *pdev, u16 dev_id, void *data)
{
	struct iovt_fwnode *np;
	struct iovt_device_entry *entry;
	struct device *aliased_dev = data;
	u32 domain = pci_domain_nr(pdev->bus);

	list_for_each_entry(np, &iovt_fwnode_list, list) {
		if (domain != np->segment)
			continue;

		/* We're not translating ourself */
		if (dev_id == np->devid)
			return -EINVAL;

		if (np->flag & ACPI_IOVT_IOMMU_MAGAGE_BY_SEGMENT)
			goto found;

		list_for_each_entry(entry, &np->ep_list, list) {
			if (dev_id >= entry->start_devid && dev_id <= entry->end_devid)
				goto found;
		}
	}

	return -ENODEV;

found:
	return acpi_iommu_fwspec_init(aliased_dev, (domain << 16) + dev_id, np->fwnode);
}

/*
 * iovt_iommu_configure_id - Set-up IOMMU configuration for a device.
 *
 * @dev: device to configure
 * @id_in: optional input id const value pointer
 *
 * Returns: 0 on success, <0 on failure
 */
int iovt_iommu_configure_id(struct device *dev, const u32 *id_in)
{
	if (!dev_is_pci(dev))
		return -ENODEV;

	return pci_for_each_dma_alias(to_pci_dev(dev), iovt_pci_iommu_init, dev);
}
