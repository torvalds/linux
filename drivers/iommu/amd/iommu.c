// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2007-2010 Advanced Micro Devices, Inc.
 * Author: Joerg Roedel <jroedel@suse.de>
 *         Leo Duran <leo.duran@amd.com>
 */

#define pr_fmt(fmt)     "AMD-Vi: " fmt
#define dev_fmt(fmt)    pr_fmt(fmt)

#include <linux/ratelimit.h>
#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/pci-ats.h>
#include <linux/bitmap.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/scatterlist.h>
#include <linux/dma-map-ops.h>
#include <linux/dma-direct.h>
#include <linux/iommu-helper.h>
#include <linux/delay.h>
#include <linux/amd-iommu.h>
#include <linux/notifier.h>
#include <linux/export.h>
#include <linux/irq.h>
#include <linux/msi.h>
#include <linux/irqdomain.h>
#include <linux/percpu.h>
#include <linux/io-pgtable.h>
#include <linux/cc_platform.h>
#include <asm/irq_remapping.h>
#include <asm/io_apic.h>
#include <asm/apic.h>
#include <asm/hw_irq.h>
#include <asm/proto.h>
#include <asm/iommu.h>
#include <asm/gart.h>
#include <asm/dma.h>

#include "amd_iommu.h"
#include "../dma-iommu.h"
#include "../irq_remapping.h"

#define CMD_SET_TYPE(cmd, t) ((cmd)->data[1] |= ((t) << 28))

#define LOOP_TIMEOUT	100000

/* IO virtual address start page frame number */
#define IOVA_START_PFN		(1)
#define IOVA_PFN(addr)		((addr) >> PAGE_SHIFT)

/* Reserved IOVA ranges */
#define MSI_RANGE_START		(0xfee00000)
#define MSI_RANGE_END		(0xfeefffff)
#define HT_RANGE_START		(0xfd00000000ULL)
#define HT_RANGE_END		(0xffffffffffULL)

#define DEFAULT_PGTABLE_LEVEL	PAGE_MODE_3_LEVEL

static DEFINE_SPINLOCK(pd_bitmap_lock);

LIST_HEAD(ioapic_map);
LIST_HEAD(hpet_map);
LIST_HEAD(acpihid_map);

const struct iommu_ops amd_iommu_ops;

static ATOMIC_NOTIFIER_HEAD(ppr_notifier);
int amd_iommu_max_glx_val = -1;

/*
 * general struct to manage commands send to an IOMMU
 */
struct iommu_cmd {
	u32 data[4];
};

struct kmem_cache *amd_iommu_irq_cache;

static void detach_device(struct device *dev);
static int domain_enable_v2(struct protection_domain *domain, int pasids);

/****************************************************************************
 *
 * Helper functions
 *
 ****************************************************************************/

static inline int get_acpihid_device_id(struct device *dev,
					struct acpihid_map_entry **entry)
{
	struct acpi_device *adev = ACPI_COMPANION(dev);
	struct acpihid_map_entry *p;

	if (!adev)
		return -ENODEV;

	list_for_each_entry(p, &acpihid_map, list) {
		if (acpi_dev_hid_uid_match(adev, p->hid,
					   p->uid[0] ? p->uid : NULL)) {
			if (entry)
				*entry = p;
			return p->devid;
		}
	}
	return -EINVAL;
}

static inline int get_device_sbdf_id(struct device *dev)
{
	int sbdf;

	if (dev_is_pci(dev))
		sbdf = get_pci_sbdf_id(to_pci_dev(dev));
	else
		sbdf = get_acpihid_device_id(dev, NULL);

	return sbdf;
}

struct dev_table_entry *get_dev_table(struct amd_iommu *iommu)
{
	struct dev_table_entry *dev_table;
	struct amd_iommu_pci_seg *pci_seg = iommu->pci_seg;

	BUG_ON(pci_seg == NULL);
	dev_table = pci_seg->dev_table;
	BUG_ON(dev_table == NULL);

	return dev_table;
}

static inline u16 get_device_segment(struct device *dev)
{
	u16 seg;

	if (dev_is_pci(dev)) {
		struct pci_dev *pdev = to_pci_dev(dev);

		seg = pci_domain_nr(pdev->bus);
	} else {
		u32 devid = get_acpihid_device_id(dev, NULL);

		seg = PCI_SBDF_TO_SEGID(devid);
	}

	return seg;
}

/* Writes the specific IOMMU for a device into the PCI segment rlookup table */
void amd_iommu_set_rlookup_table(struct amd_iommu *iommu, u16 devid)
{
	struct amd_iommu_pci_seg *pci_seg = iommu->pci_seg;

	pci_seg->rlookup_table[devid] = iommu;
}

static struct amd_iommu *__rlookup_amd_iommu(u16 seg, u16 devid)
{
	struct amd_iommu_pci_seg *pci_seg;

	for_each_pci_segment(pci_seg) {
		if (pci_seg->id == seg)
			return pci_seg->rlookup_table[devid];
	}
	return NULL;
}

static struct amd_iommu *rlookup_amd_iommu(struct device *dev)
{
	u16 seg = get_device_segment(dev);
	int devid = get_device_sbdf_id(dev);

	if (devid < 0)
		return NULL;
	return __rlookup_amd_iommu(seg, PCI_SBDF_TO_DEVID(devid));
}

static struct protection_domain *to_pdomain(struct iommu_domain *dom)
{
	return container_of(dom, struct protection_domain, domain);
}

static struct iommu_dev_data *alloc_dev_data(struct amd_iommu *iommu, u16 devid)
{
	struct iommu_dev_data *dev_data;
	struct amd_iommu_pci_seg *pci_seg = iommu->pci_seg;

	dev_data = kzalloc(sizeof(*dev_data), GFP_KERNEL);
	if (!dev_data)
		return NULL;

	spin_lock_init(&dev_data->lock);
	dev_data->devid = devid;
	ratelimit_default_init(&dev_data->rs);

	llist_add(&dev_data->dev_data_list, &pci_seg->dev_data_list);
	return dev_data;
}

static struct iommu_dev_data *search_dev_data(struct amd_iommu *iommu, u16 devid)
{
	struct iommu_dev_data *dev_data;
	struct llist_node *node;
	struct amd_iommu_pci_seg *pci_seg = iommu->pci_seg;

	if (llist_empty(&pci_seg->dev_data_list))
		return NULL;

	node = pci_seg->dev_data_list.first;
	llist_for_each_entry(dev_data, node, dev_data_list) {
		if (dev_data->devid == devid)
			return dev_data;
	}

	return NULL;
}

static int clone_alias(struct pci_dev *pdev, u16 alias, void *data)
{
	struct amd_iommu *iommu;
	struct dev_table_entry *dev_table;
	u16 devid = pci_dev_id(pdev);

	if (devid == alias)
		return 0;

	iommu = rlookup_amd_iommu(&pdev->dev);
	if (!iommu)
		return 0;

	amd_iommu_set_rlookup_table(iommu, alias);
	dev_table = get_dev_table(iommu);
	memcpy(dev_table[alias].data,
	       dev_table[devid].data,
	       sizeof(dev_table[alias].data));

	return 0;
}

static void clone_aliases(struct amd_iommu *iommu, struct device *dev)
{
	struct pci_dev *pdev;

	if (!dev_is_pci(dev))
		return;
	pdev = to_pci_dev(dev);

	/*
	 * The IVRS alias stored in the alias table may not be
	 * part of the PCI DMA aliases if it's bus differs
	 * from the original device.
	 */
	clone_alias(pdev, iommu->pci_seg->alias_table[pci_dev_id(pdev)], NULL);

	pci_for_each_dma_alias(pdev, clone_alias, NULL);
}

static void setup_aliases(struct amd_iommu *iommu, struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct amd_iommu_pci_seg *pci_seg = iommu->pci_seg;
	u16 ivrs_alias;

	/* For ACPI HID devices, there are no aliases */
	if (!dev_is_pci(dev))
		return;

	/*
	 * Add the IVRS alias to the pci aliases if it is on the same
	 * bus. The IVRS table may know about a quirk that we don't.
	 */
	ivrs_alias = pci_seg->alias_table[pci_dev_id(pdev)];
	if (ivrs_alias != pci_dev_id(pdev) &&
	    PCI_BUS_NUM(ivrs_alias) == pdev->bus->number)
		pci_add_dma_alias(pdev, ivrs_alias & 0xff, 1);

	clone_aliases(iommu, dev);
}

static struct iommu_dev_data *find_dev_data(struct amd_iommu *iommu, u16 devid)
{
	struct iommu_dev_data *dev_data;

	dev_data = search_dev_data(iommu, devid);

	if (dev_data == NULL) {
		dev_data = alloc_dev_data(iommu, devid);
		if (!dev_data)
			return NULL;

		if (translation_pre_enabled(iommu))
			dev_data->defer_attach = true;
	}

	return dev_data;
}

/*
* Find or create an IOMMU group for a acpihid device.
*/
static struct iommu_group *acpihid_device_group(struct device *dev)
{
	struct acpihid_map_entry *p, *entry = NULL;
	int devid;

	devid = get_acpihid_device_id(dev, &entry);
	if (devid < 0)
		return ERR_PTR(devid);

	list_for_each_entry(p, &acpihid_map, list) {
		if ((devid == p->devid) && p->group)
			entry->group = p->group;
	}

	if (!entry->group)
		entry->group = generic_device_group(dev);
	else
		iommu_group_ref_get(entry->group);

	return entry->group;
}

static bool pci_iommuv2_capable(struct pci_dev *pdev)
{
	static const int caps[] = {
		PCI_EXT_CAP_ID_PRI,
		PCI_EXT_CAP_ID_PASID,
	};
	int i, pos;

	if (!pci_ats_supported(pdev))
		return false;

	for (i = 0; i < 2; ++i) {
		pos = pci_find_ext_capability(pdev, caps[i]);
		if (pos == 0)
			return false;
	}

	return true;
}

/*
 * This function checks if the driver got a valid device from the caller to
 * avoid dereferencing invalid pointers.
 */
static bool check_device(struct device *dev)
{
	struct amd_iommu_pci_seg *pci_seg;
	struct amd_iommu *iommu;
	int devid, sbdf;

	if (!dev)
		return false;

	sbdf = get_device_sbdf_id(dev);
	if (sbdf < 0)
		return false;
	devid = PCI_SBDF_TO_DEVID(sbdf);

	iommu = rlookup_amd_iommu(dev);
	if (!iommu)
		return false;

	/* Out of our scope? */
	pci_seg = iommu->pci_seg;
	if (devid > pci_seg->last_bdf)
		return false;

	return true;
}

static int iommu_init_device(struct amd_iommu *iommu, struct device *dev)
{
	struct iommu_dev_data *dev_data;
	int devid, sbdf;

	if (dev_iommu_priv_get(dev))
		return 0;

	sbdf = get_device_sbdf_id(dev);
	if (sbdf < 0)
		return sbdf;

	devid = PCI_SBDF_TO_DEVID(sbdf);
	dev_data = find_dev_data(iommu, devid);
	if (!dev_data)
		return -ENOMEM;

	dev_data->dev = dev;
	setup_aliases(iommu, dev);

	/*
	 * By default we use passthrough mode for IOMMUv2 capable device.
	 * But if amd_iommu=force_isolation is set (e.g. to debug DMA to
	 * invalid address), we ignore the capability for the device so
	 * it'll be forced to go into translation mode.
	 */
	if ((iommu_default_passthrough() || !amd_iommu_force_isolation) &&
	    dev_is_pci(dev) && pci_iommuv2_capable(to_pci_dev(dev))) {
		dev_data->iommu_v2 = iommu->is_iommu_v2;
	}

	dev_iommu_priv_set(dev, dev_data);

	return 0;
}

static void iommu_ignore_device(struct amd_iommu *iommu, struct device *dev)
{
	struct amd_iommu_pci_seg *pci_seg = iommu->pci_seg;
	struct dev_table_entry *dev_table = get_dev_table(iommu);
	int devid, sbdf;

	sbdf = get_device_sbdf_id(dev);
	if (sbdf < 0)
		return;

	devid = PCI_SBDF_TO_DEVID(sbdf);
	pci_seg->rlookup_table[devid] = NULL;
	memset(&dev_table[devid], 0, sizeof(struct dev_table_entry));

	setup_aliases(iommu, dev);
}

static void amd_iommu_uninit_device(struct device *dev)
{
	struct iommu_dev_data *dev_data;

	dev_data = dev_iommu_priv_get(dev);
	if (!dev_data)
		return;

	if (dev_data->domain)
		detach_device(dev);

	dev_iommu_priv_set(dev, NULL);

	/*
	 * We keep dev_data around for unplugged devices and reuse it when the
	 * device is re-plugged - not doing so would introduce a ton of races.
	 */
}

/****************************************************************************
 *
 * Interrupt handling functions
 *
 ****************************************************************************/

static void dump_dte_entry(struct amd_iommu *iommu, u16 devid)
{
	int i;
	struct dev_table_entry *dev_table = get_dev_table(iommu);

	for (i = 0; i < 4; ++i)
		pr_err("DTE[%d]: %016llx\n", i, dev_table[devid].data[i]);
}

static void dump_command(unsigned long phys_addr)
{
	struct iommu_cmd *cmd = iommu_phys_to_virt(phys_addr);
	int i;

	for (i = 0; i < 4; ++i)
		pr_err("CMD[%d]: %08x\n", i, cmd->data[i]);
}

static void amd_iommu_report_rmp_hw_error(struct amd_iommu *iommu, volatile u32 *event)
{
	struct iommu_dev_data *dev_data = NULL;
	int devid, vmg_tag, flags;
	struct pci_dev *pdev;
	u64 spa;

	devid   = (event[0] >> EVENT_DEVID_SHIFT) & EVENT_DEVID_MASK;
	vmg_tag = (event[1]) & 0xFFFF;
	flags   = (event[1] >> EVENT_FLAGS_SHIFT) & EVENT_FLAGS_MASK;
	spa     = ((u64)event[3] << 32) | (event[2] & 0xFFFFFFF8);

	pdev = pci_get_domain_bus_and_slot(iommu->pci_seg->id, PCI_BUS_NUM(devid),
					   devid & 0xff);
	if (pdev)
		dev_data = dev_iommu_priv_get(&pdev->dev);

	if (dev_data) {
		if (__ratelimit(&dev_data->rs)) {
			pci_err(pdev, "Event logged [RMP_HW_ERROR vmg_tag=0x%04x, spa=0x%llx, flags=0x%04x]\n",
				vmg_tag, spa, flags);
		}
	} else {
		pr_err_ratelimited("Event logged [RMP_HW_ERROR device=%04x:%02x:%02x.%x, vmg_tag=0x%04x, spa=0x%llx, flags=0x%04x]\n",
			iommu->pci_seg->id, PCI_BUS_NUM(devid), PCI_SLOT(devid), PCI_FUNC(devid),
			vmg_tag, spa, flags);
	}

	if (pdev)
		pci_dev_put(pdev);
}

static void amd_iommu_report_rmp_fault(struct amd_iommu *iommu, volatile u32 *event)
{
	struct iommu_dev_data *dev_data = NULL;
	int devid, flags_rmp, vmg_tag, flags;
	struct pci_dev *pdev;
	u64 gpa;

	devid     = (event[0] >> EVENT_DEVID_SHIFT) & EVENT_DEVID_MASK;
	flags_rmp = (event[0] >> EVENT_FLAGS_SHIFT) & 0xFF;
	vmg_tag   = (event[1]) & 0xFFFF;
	flags     = (event[1] >> EVENT_FLAGS_SHIFT) & EVENT_FLAGS_MASK;
	gpa       = ((u64)event[3] << 32) | event[2];

	pdev = pci_get_domain_bus_and_slot(iommu->pci_seg->id, PCI_BUS_NUM(devid),
					   devid & 0xff);
	if (pdev)
		dev_data = dev_iommu_priv_get(&pdev->dev);

	if (dev_data) {
		if (__ratelimit(&dev_data->rs)) {
			pci_err(pdev, "Event logged [RMP_PAGE_FAULT vmg_tag=0x%04x, gpa=0x%llx, flags_rmp=0x%04x, flags=0x%04x]\n",
				vmg_tag, gpa, flags_rmp, flags);
		}
	} else {
		pr_err_ratelimited("Event logged [RMP_PAGE_FAULT device=%04x:%02x:%02x.%x, vmg_tag=0x%04x, gpa=0x%llx, flags_rmp=0x%04x, flags=0x%04x]\n",
			iommu->pci_seg->id, PCI_BUS_NUM(devid), PCI_SLOT(devid), PCI_FUNC(devid),
			vmg_tag, gpa, flags_rmp, flags);
	}

	if (pdev)
		pci_dev_put(pdev);
}

#define IS_IOMMU_MEM_TRANSACTION(flags)		\
	(((flags) & EVENT_FLAG_I) == 0)

#define IS_WRITE_REQUEST(flags)			\
	((flags) & EVENT_FLAG_RW)

static void amd_iommu_report_page_fault(struct amd_iommu *iommu,
					u16 devid, u16 domain_id,
					u64 address, int flags)
{
	struct iommu_dev_data *dev_data = NULL;
	struct pci_dev *pdev;

	pdev = pci_get_domain_bus_and_slot(iommu->pci_seg->id, PCI_BUS_NUM(devid),
					   devid & 0xff);
	if (pdev)
		dev_data = dev_iommu_priv_get(&pdev->dev);

	if (dev_data) {
		/*
		 * If this is a DMA fault (for which the I(nterrupt)
		 * bit will be unset), allow report_iommu_fault() to
		 * prevent logging it.
		 */
		if (IS_IOMMU_MEM_TRANSACTION(flags)) {
			/* Device not attached to domain properly */
			if (dev_data->domain == NULL) {
				pr_err_ratelimited("Event logged [Device not attached to domain properly]\n");
				pr_err_ratelimited("  device=%04x:%02x:%02x.%x domain=0x%04x\n",
						   iommu->pci_seg->id, PCI_BUS_NUM(devid), PCI_SLOT(devid),
						   PCI_FUNC(devid), domain_id);
				goto out;
			}

			if (!report_iommu_fault(&dev_data->domain->domain,
						&pdev->dev, address,
						IS_WRITE_REQUEST(flags) ?
							IOMMU_FAULT_WRITE :
							IOMMU_FAULT_READ))
				goto out;
		}

		if (__ratelimit(&dev_data->rs)) {
			pci_err(pdev, "Event logged [IO_PAGE_FAULT domain=0x%04x address=0x%llx flags=0x%04x]\n",
				domain_id, address, flags);
		}
	} else {
		pr_err_ratelimited("Event logged [IO_PAGE_FAULT device=%04x:%02x:%02x.%x domain=0x%04x address=0x%llx flags=0x%04x]\n",
			iommu->pci_seg->id, PCI_BUS_NUM(devid), PCI_SLOT(devid), PCI_FUNC(devid),
			domain_id, address, flags);
	}

out:
	if (pdev)
		pci_dev_put(pdev);
}

static void iommu_print_event(struct amd_iommu *iommu, void *__evt)
{
	struct device *dev = iommu->iommu.dev;
	int type, devid, flags, tag;
	volatile u32 *event = __evt;
	int count = 0;
	u64 address;
	u32 pasid;

retry:
	type    = (event[1] >> EVENT_TYPE_SHIFT)  & EVENT_TYPE_MASK;
	devid   = (event[0] >> EVENT_DEVID_SHIFT) & EVENT_DEVID_MASK;
	pasid   = (event[0] & EVENT_DOMID_MASK_HI) |
		  (event[1] & EVENT_DOMID_MASK_LO);
	flags   = (event[1] >> EVENT_FLAGS_SHIFT) & EVENT_FLAGS_MASK;
	address = (u64)(((u64)event[3]) << 32) | event[2];

	if (type == 0) {
		/* Did we hit the erratum? */
		if (++count == LOOP_TIMEOUT) {
			pr_err("No event written to event log\n");
			return;
		}
		udelay(1);
		goto retry;
	}

	if (type == EVENT_TYPE_IO_FAULT) {
		amd_iommu_report_page_fault(iommu, devid, pasid, address, flags);
		return;
	}

	switch (type) {
	case EVENT_TYPE_ILL_DEV:
		dev_err(dev, "Event logged [ILLEGAL_DEV_TABLE_ENTRY device=%04x:%02x:%02x.%x pasid=0x%05x address=0x%llx flags=0x%04x]\n",
			iommu->pci_seg->id, PCI_BUS_NUM(devid), PCI_SLOT(devid), PCI_FUNC(devid),
			pasid, address, flags);
		dump_dte_entry(iommu, devid);
		break;
	case EVENT_TYPE_DEV_TAB_ERR:
		dev_err(dev, "Event logged [DEV_TAB_HARDWARE_ERROR device=%04x:%02x:%02x.%x "
			"address=0x%llx flags=0x%04x]\n",
			iommu->pci_seg->id, PCI_BUS_NUM(devid), PCI_SLOT(devid), PCI_FUNC(devid),
			address, flags);
		break;
	case EVENT_TYPE_PAGE_TAB_ERR:
		dev_err(dev, "Event logged [PAGE_TAB_HARDWARE_ERROR device=%04x:%02x:%02x.%x pasid=0x%04x address=0x%llx flags=0x%04x]\n",
			iommu->pci_seg->id, PCI_BUS_NUM(devid), PCI_SLOT(devid), PCI_FUNC(devid),
			pasid, address, flags);
		break;
	case EVENT_TYPE_ILL_CMD:
		dev_err(dev, "Event logged [ILLEGAL_COMMAND_ERROR address=0x%llx]\n", address);
		dump_command(address);
		break;
	case EVENT_TYPE_CMD_HARD_ERR:
		dev_err(dev, "Event logged [COMMAND_HARDWARE_ERROR address=0x%llx flags=0x%04x]\n",
			address, flags);
		break;
	case EVENT_TYPE_IOTLB_INV_TO:
		dev_err(dev, "Event logged [IOTLB_INV_TIMEOUT device=%04x:%02x:%02x.%x address=0x%llx]\n",
			iommu->pci_seg->id, PCI_BUS_NUM(devid), PCI_SLOT(devid), PCI_FUNC(devid),
			address);
		break;
	case EVENT_TYPE_INV_DEV_REQ:
		dev_err(dev, "Event logged [INVALID_DEVICE_REQUEST device=%04x:%02x:%02x.%x pasid=0x%05x address=0x%llx flags=0x%04x]\n",
			iommu->pci_seg->id, PCI_BUS_NUM(devid), PCI_SLOT(devid), PCI_FUNC(devid),
			pasid, address, flags);
		break;
	case EVENT_TYPE_RMP_FAULT:
		amd_iommu_report_rmp_fault(iommu, event);
		break;
	case EVENT_TYPE_RMP_HW_ERR:
		amd_iommu_report_rmp_hw_error(iommu, event);
		break;
	case EVENT_TYPE_INV_PPR_REQ:
		pasid = PPR_PASID(*((u64 *)__evt));
		tag = event[1] & 0x03FF;
		dev_err(dev, "Event logged [INVALID_PPR_REQUEST device=%04x:%02x:%02x.%x pasid=0x%05x address=0x%llx flags=0x%04x tag=0x%03x]\n",
			iommu->pci_seg->id, PCI_BUS_NUM(devid), PCI_SLOT(devid), PCI_FUNC(devid),
			pasid, address, flags, tag);
		break;
	default:
		dev_err(dev, "Event logged [UNKNOWN event[0]=0x%08x event[1]=0x%08x event[2]=0x%08x event[3]=0x%08x\n",
			event[0], event[1], event[2], event[3]);
	}

	memset(__evt, 0, 4 * sizeof(u32));
}

static void iommu_poll_events(struct amd_iommu *iommu)
{
	u32 head, tail;

	head = readl(iommu->mmio_base + MMIO_EVT_HEAD_OFFSET);
	tail = readl(iommu->mmio_base + MMIO_EVT_TAIL_OFFSET);

	while (head != tail) {
		iommu_print_event(iommu, iommu->evt_buf + head);
		head = (head + EVENT_ENTRY_SIZE) % EVT_BUFFER_SIZE;
	}

	writel(head, iommu->mmio_base + MMIO_EVT_HEAD_OFFSET);
}

static void iommu_handle_ppr_entry(struct amd_iommu *iommu, u64 *raw)
{
	struct amd_iommu_fault fault;

	if (PPR_REQ_TYPE(raw[0]) != PPR_REQ_FAULT) {
		pr_err_ratelimited("Unknown PPR request received\n");
		return;
	}

	fault.address   = raw[1];
	fault.pasid     = PPR_PASID(raw[0]);
	fault.sbdf      = PCI_SEG_DEVID_TO_SBDF(iommu->pci_seg->id, PPR_DEVID(raw[0]));
	fault.tag       = PPR_TAG(raw[0]);
	fault.flags     = PPR_FLAGS(raw[0]);

	atomic_notifier_call_chain(&ppr_notifier, 0, &fault);
}

static void iommu_poll_ppr_log(struct amd_iommu *iommu)
{
	u32 head, tail;

	if (iommu->ppr_log == NULL)
		return;

	head = readl(iommu->mmio_base + MMIO_PPR_HEAD_OFFSET);
	tail = readl(iommu->mmio_base + MMIO_PPR_TAIL_OFFSET);

	while (head != tail) {
		volatile u64 *raw;
		u64 entry[2];
		int i;

		raw = (u64 *)(iommu->ppr_log + head);

		/*
		 * Hardware bug: Interrupt may arrive before the entry is
		 * written to memory. If this happens we need to wait for the
		 * entry to arrive.
		 */
		for (i = 0; i < LOOP_TIMEOUT; ++i) {
			if (PPR_REQ_TYPE(raw[0]) != 0)
				break;
			udelay(1);
		}

		/* Avoid memcpy function-call overhead */
		entry[0] = raw[0];
		entry[1] = raw[1];

		/*
		 * To detect the hardware bug we need to clear the entry
		 * back to zero.
		 */
		raw[0] = raw[1] = 0UL;

		/* Update head pointer of hardware ring-buffer */
		head = (head + PPR_ENTRY_SIZE) % PPR_LOG_SIZE;
		writel(head, iommu->mmio_base + MMIO_PPR_HEAD_OFFSET);

		/* Handle PPR entry */
		iommu_handle_ppr_entry(iommu, entry);

		/* Refresh ring-buffer information */
		head = readl(iommu->mmio_base + MMIO_PPR_HEAD_OFFSET);
		tail = readl(iommu->mmio_base + MMIO_PPR_TAIL_OFFSET);
	}
}

#ifdef CONFIG_IRQ_REMAP
static int (*iommu_ga_log_notifier)(u32);

int amd_iommu_register_ga_log_notifier(int (*notifier)(u32))
{
	iommu_ga_log_notifier = notifier;

	return 0;
}
EXPORT_SYMBOL(amd_iommu_register_ga_log_notifier);

static void iommu_poll_ga_log(struct amd_iommu *iommu)
{
	u32 head, tail, cnt = 0;

	if (iommu->ga_log == NULL)
		return;

	head = readl(iommu->mmio_base + MMIO_GA_HEAD_OFFSET);
	tail = readl(iommu->mmio_base + MMIO_GA_TAIL_OFFSET);

	while (head != tail) {
		volatile u64 *raw;
		u64 log_entry;

		raw = (u64 *)(iommu->ga_log + head);
		cnt++;

		/* Avoid memcpy function-call overhead */
		log_entry = *raw;

		/* Update head pointer of hardware ring-buffer */
		head = (head + GA_ENTRY_SIZE) % GA_LOG_SIZE;
		writel(head, iommu->mmio_base + MMIO_GA_HEAD_OFFSET);

		/* Handle GA entry */
		switch (GA_REQ_TYPE(log_entry)) {
		case GA_GUEST_NR:
			if (!iommu_ga_log_notifier)
				break;

			pr_debug("%s: devid=%#x, ga_tag=%#x\n",
				 __func__, GA_DEVID(log_entry),
				 GA_TAG(log_entry));

			if (iommu_ga_log_notifier(GA_TAG(log_entry)) != 0)
				pr_err("GA log notifier failed.\n");
			break;
		default:
			break;
		}
	}
}

static void
amd_iommu_set_pci_msi_domain(struct device *dev, struct amd_iommu *iommu)
{
	if (!irq_remapping_enabled || !dev_is_pci(dev) ||
	    pci_dev_has_special_msi_domain(to_pci_dev(dev)))
		return;

	dev_set_msi_domain(dev, iommu->msi_domain);
}

#else /* CONFIG_IRQ_REMAP */
static inline void
amd_iommu_set_pci_msi_domain(struct device *dev, struct amd_iommu *iommu) { }
#endif /* !CONFIG_IRQ_REMAP */

#define AMD_IOMMU_INT_MASK	\
	(MMIO_STATUS_EVT_OVERFLOW_INT_MASK | \
	 MMIO_STATUS_EVT_INT_MASK | \
	 MMIO_STATUS_PPR_INT_MASK | \
	 MMIO_STATUS_GALOG_INT_MASK)

irqreturn_t amd_iommu_int_thread(int irq, void *data)
{
	struct amd_iommu *iommu = (struct amd_iommu *) data;
	u32 status = readl(iommu->mmio_base + MMIO_STATUS_OFFSET);

	while (status & AMD_IOMMU_INT_MASK) {
		/* Enable interrupt sources again */
		writel(AMD_IOMMU_INT_MASK,
			iommu->mmio_base + MMIO_STATUS_OFFSET);

		if (status & MMIO_STATUS_EVT_INT_MASK) {
			pr_devel("Processing IOMMU Event Log\n");
			iommu_poll_events(iommu);
		}

		if (status & MMIO_STATUS_PPR_INT_MASK) {
			pr_devel("Processing IOMMU PPR Log\n");
			iommu_poll_ppr_log(iommu);
		}

#ifdef CONFIG_IRQ_REMAP
		if (status & MMIO_STATUS_GALOG_INT_MASK) {
			pr_devel("Processing IOMMU GA Log\n");
			iommu_poll_ga_log(iommu);
		}
#endif

		if (status & MMIO_STATUS_EVT_OVERFLOW_INT_MASK) {
			pr_info_ratelimited("IOMMU event log overflow\n");
			amd_iommu_restart_event_logging(iommu);
		}

		/*
		 * Hardware bug: ERBT1312
		 * When re-enabling interrupt (by writing 1
		 * to clear the bit), the hardware might also try to set
		 * the interrupt bit in the event status register.
		 * In this scenario, the bit will be set, and disable
		 * subsequent interrupts.
		 *
		 * Workaround: The IOMMU driver should read back the
		 * status register and check if the interrupt bits are cleared.
		 * If not, driver will need to go through the interrupt handler
		 * again and re-clear the bits
		 */
		status = readl(iommu->mmio_base + MMIO_STATUS_OFFSET);
	}
	return IRQ_HANDLED;
}

irqreturn_t amd_iommu_int_handler(int irq, void *data)
{
	return IRQ_WAKE_THREAD;
}

/****************************************************************************
 *
 * IOMMU command queuing functions
 *
 ****************************************************************************/

static int wait_on_sem(struct amd_iommu *iommu, u64 data)
{
	int i = 0;

	while (*iommu->cmd_sem != data && i < LOOP_TIMEOUT) {
		udelay(1);
		i += 1;
	}

	if (i == LOOP_TIMEOUT) {
		pr_alert("Completion-Wait loop timed out\n");
		return -EIO;
	}

	return 0;
}

static void copy_cmd_to_buffer(struct amd_iommu *iommu,
			       struct iommu_cmd *cmd)
{
	u8 *target;
	u32 tail;

	/* Copy command to buffer */
	tail = iommu->cmd_buf_tail;
	target = iommu->cmd_buf + tail;
	memcpy(target, cmd, sizeof(*cmd));

	tail = (tail + sizeof(*cmd)) % CMD_BUFFER_SIZE;
	iommu->cmd_buf_tail = tail;

	/* Tell the IOMMU about it */
	writel(tail, iommu->mmio_base + MMIO_CMD_TAIL_OFFSET);
}

static void build_completion_wait(struct iommu_cmd *cmd,
				  struct amd_iommu *iommu,
				  u64 data)
{
	u64 paddr = iommu_virt_to_phys((void *)iommu->cmd_sem);

	memset(cmd, 0, sizeof(*cmd));
	cmd->data[0] = lower_32_bits(paddr) | CMD_COMPL_WAIT_STORE_MASK;
	cmd->data[1] = upper_32_bits(paddr);
	cmd->data[2] = lower_32_bits(data);
	cmd->data[3] = upper_32_bits(data);
	CMD_SET_TYPE(cmd, CMD_COMPL_WAIT);
}

static void build_inv_dte(struct iommu_cmd *cmd, u16 devid)
{
	memset(cmd, 0, sizeof(*cmd));
	cmd->data[0] = devid;
	CMD_SET_TYPE(cmd, CMD_INV_DEV_ENTRY);
}

/*
 * Builds an invalidation address which is suitable for one page or multiple
 * pages. Sets the size bit (S) as needed is more than one page is flushed.
 */
static inline u64 build_inv_address(u64 address, size_t size)
{
	u64 pages, end, msb_diff;

	pages = iommu_num_pages(address, size, PAGE_SIZE);

	if (pages == 1)
		return address & PAGE_MASK;

	end = address + size - 1;

	/*
	 * msb_diff would hold the index of the most significant bit that
	 * flipped between the start and end.
	 */
	msb_diff = fls64(end ^ address) - 1;

	/*
	 * Bits 63:52 are sign extended. If for some reason bit 51 is different
	 * between the start and the end, invalidate everything.
	 */
	if (unlikely(msb_diff > 51)) {
		address = CMD_INV_IOMMU_ALL_PAGES_ADDRESS;
	} else {
		/*
		 * The msb-bit must be clear on the address. Just set all the
		 * lower bits.
		 */
		address |= (1ull << msb_diff) - 1;
	}

	/* Clear bits 11:0 */
	address &= PAGE_MASK;

	/* Set the size bit - we flush more than one 4kb page */
	return address | CMD_INV_IOMMU_PAGES_SIZE_MASK;
}

static void build_inv_iommu_pages(struct iommu_cmd *cmd, u64 address,
				  size_t size, u16 domid, int pde)
{
	u64 inv_address = build_inv_address(address, size);

	memset(cmd, 0, sizeof(*cmd));
	cmd->data[1] |= domid;
	cmd->data[2]  = lower_32_bits(inv_address);
	cmd->data[3]  = upper_32_bits(inv_address);
	CMD_SET_TYPE(cmd, CMD_INV_IOMMU_PAGES);
	if (pde) /* PDE bit - we want to flush everything, not only the PTEs */
		cmd->data[2] |= CMD_INV_IOMMU_PAGES_PDE_MASK;
}

static void build_inv_iotlb_pages(struct iommu_cmd *cmd, u16 devid, int qdep,
				  u64 address, size_t size)
{
	u64 inv_address = build_inv_address(address, size);

	memset(cmd, 0, sizeof(*cmd));
	cmd->data[0]  = devid;
	cmd->data[0] |= (qdep & 0xff) << 24;
	cmd->data[1]  = devid;
	cmd->data[2]  = lower_32_bits(inv_address);
	cmd->data[3]  = upper_32_bits(inv_address);
	CMD_SET_TYPE(cmd, CMD_INV_IOTLB_PAGES);
}

static void build_inv_iommu_pasid(struct iommu_cmd *cmd, u16 domid, u32 pasid,
				  u64 address, bool size)
{
	memset(cmd, 0, sizeof(*cmd));

	address &= ~(0xfffULL);

	cmd->data[0]  = pasid;
	cmd->data[1]  = domid;
	cmd->data[2]  = lower_32_bits(address);
	cmd->data[3]  = upper_32_bits(address);
	cmd->data[2] |= CMD_INV_IOMMU_PAGES_PDE_MASK;
	cmd->data[2] |= CMD_INV_IOMMU_PAGES_GN_MASK;
	if (size)
		cmd->data[2] |= CMD_INV_IOMMU_PAGES_SIZE_MASK;
	CMD_SET_TYPE(cmd, CMD_INV_IOMMU_PAGES);
}

static void build_inv_iotlb_pasid(struct iommu_cmd *cmd, u16 devid, u32 pasid,
				  int qdep, u64 address, bool size)
{
	memset(cmd, 0, sizeof(*cmd));

	address &= ~(0xfffULL);

	cmd->data[0]  = devid;
	cmd->data[0] |= ((pasid >> 8) & 0xff) << 16;
	cmd->data[0] |= (qdep  & 0xff) << 24;
	cmd->data[1]  = devid;
	cmd->data[1] |= (pasid & 0xff) << 16;
	cmd->data[2]  = lower_32_bits(address);
	cmd->data[2] |= CMD_INV_IOMMU_PAGES_GN_MASK;
	cmd->data[3]  = upper_32_bits(address);
	if (size)
		cmd->data[2] |= CMD_INV_IOMMU_PAGES_SIZE_MASK;
	CMD_SET_TYPE(cmd, CMD_INV_IOTLB_PAGES);
}

static void build_complete_ppr(struct iommu_cmd *cmd, u16 devid, u32 pasid,
			       int status, int tag, bool gn)
{
	memset(cmd, 0, sizeof(*cmd));

	cmd->data[0]  = devid;
	if (gn) {
		cmd->data[1]  = pasid;
		cmd->data[2]  = CMD_INV_IOMMU_PAGES_GN_MASK;
	}
	cmd->data[3]  = tag & 0x1ff;
	cmd->data[3] |= (status & PPR_STATUS_MASK) << PPR_STATUS_SHIFT;

	CMD_SET_TYPE(cmd, CMD_COMPLETE_PPR);
}

static void build_inv_all(struct iommu_cmd *cmd)
{
	memset(cmd, 0, sizeof(*cmd));
	CMD_SET_TYPE(cmd, CMD_INV_ALL);
}

static void build_inv_irt(struct iommu_cmd *cmd, u16 devid)
{
	memset(cmd, 0, sizeof(*cmd));
	cmd->data[0] = devid;
	CMD_SET_TYPE(cmd, CMD_INV_IRT);
}

/*
 * Writes the command to the IOMMUs command buffer and informs the
 * hardware about the new command.
 */
static int __iommu_queue_command_sync(struct amd_iommu *iommu,
				      struct iommu_cmd *cmd,
				      bool sync)
{
	unsigned int count = 0;
	u32 left, next_tail;

	next_tail = (iommu->cmd_buf_tail + sizeof(*cmd)) % CMD_BUFFER_SIZE;
again:
	left      = (iommu->cmd_buf_head - next_tail) % CMD_BUFFER_SIZE;

	if (left <= 0x20) {
		/* Skip udelay() the first time around */
		if (count++) {
			if (count == LOOP_TIMEOUT) {
				pr_err("Command buffer timeout\n");
				return -EIO;
			}

			udelay(1);
		}

		/* Update head and recheck remaining space */
		iommu->cmd_buf_head = readl(iommu->mmio_base +
					    MMIO_CMD_HEAD_OFFSET);

		goto again;
	}

	copy_cmd_to_buffer(iommu, cmd);

	/* Do we need to make sure all commands are processed? */
	iommu->need_sync = sync;

	return 0;
}

static int iommu_queue_command_sync(struct amd_iommu *iommu,
				    struct iommu_cmd *cmd,
				    bool sync)
{
	unsigned long flags;
	int ret;

	raw_spin_lock_irqsave(&iommu->lock, flags);
	ret = __iommu_queue_command_sync(iommu, cmd, sync);
	raw_spin_unlock_irqrestore(&iommu->lock, flags);

	return ret;
}

static int iommu_queue_command(struct amd_iommu *iommu, struct iommu_cmd *cmd)
{
	return iommu_queue_command_sync(iommu, cmd, true);
}

/*
 * This function queues a completion wait command into the command
 * buffer of an IOMMU
 */
static int iommu_completion_wait(struct amd_iommu *iommu)
{
	struct iommu_cmd cmd;
	unsigned long flags;
	int ret;
	u64 data;

	if (!iommu->need_sync)
		return 0;

	raw_spin_lock_irqsave(&iommu->lock, flags);

	data = ++iommu->cmd_sem_val;
	build_completion_wait(&cmd, iommu, data);

	ret = __iommu_queue_command_sync(iommu, &cmd, false);
	if (ret)
		goto out_unlock;

	ret = wait_on_sem(iommu, data);

out_unlock:
	raw_spin_unlock_irqrestore(&iommu->lock, flags);

	return ret;
}

static int iommu_flush_dte(struct amd_iommu *iommu, u16 devid)
{
	struct iommu_cmd cmd;

	build_inv_dte(&cmd, devid);

	return iommu_queue_command(iommu, &cmd);
}

static void amd_iommu_flush_dte_all(struct amd_iommu *iommu)
{
	u32 devid;
	u16 last_bdf = iommu->pci_seg->last_bdf;

	for (devid = 0; devid <= last_bdf; ++devid)
		iommu_flush_dte(iommu, devid);

	iommu_completion_wait(iommu);
}

/*
 * This function uses heavy locking and may disable irqs for some time. But
 * this is no issue because it is only called during resume.
 */
static void amd_iommu_flush_tlb_all(struct amd_iommu *iommu)
{
	u32 dom_id;
	u16 last_bdf = iommu->pci_seg->last_bdf;

	for (dom_id = 0; dom_id <= last_bdf; ++dom_id) {
		struct iommu_cmd cmd;
		build_inv_iommu_pages(&cmd, 0, CMD_INV_IOMMU_ALL_PAGES_ADDRESS,
				      dom_id, 1);
		iommu_queue_command(iommu, &cmd);
	}

	iommu_completion_wait(iommu);
}

static void amd_iommu_flush_tlb_domid(struct amd_iommu *iommu, u32 dom_id)
{
	struct iommu_cmd cmd;

	build_inv_iommu_pages(&cmd, 0, CMD_INV_IOMMU_ALL_PAGES_ADDRESS,
			      dom_id, 1);
	iommu_queue_command(iommu, &cmd);

	iommu_completion_wait(iommu);
}

static void amd_iommu_flush_all(struct amd_iommu *iommu)
{
	struct iommu_cmd cmd;

	build_inv_all(&cmd);

	iommu_queue_command(iommu, &cmd);
	iommu_completion_wait(iommu);
}

static void iommu_flush_irt(struct amd_iommu *iommu, u16 devid)
{
	struct iommu_cmd cmd;

	build_inv_irt(&cmd, devid);

	iommu_queue_command(iommu, &cmd);
}

static void amd_iommu_flush_irt_all(struct amd_iommu *iommu)
{
	u32 devid;
	u16 last_bdf = iommu->pci_seg->last_bdf;

	for (devid = 0; devid <= last_bdf; devid++)
		iommu_flush_irt(iommu, devid);

	iommu_completion_wait(iommu);
}

void iommu_flush_all_caches(struct amd_iommu *iommu)
{
	if (iommu_feature(iommu, FEATURE_IA)) {
		amd_iommu_flush_all(iommu);
	} else {
		amd_iommu_flush_dte_all(iommu);
		amd_iommu_flush_irt_all(iommu);
		amd_iommu_flush_tlb_all(iommu);
	}
}

/*
 * Command send function for flushing on-device TLB
 */
static int device_flush_iotlb(struct iommu_dev_data *dev_data,
			      u64 address, size_t size)
{
	struct amd_iommu *iommu;
	struct iommu_cmd cmd;
	int qdep;

	qdep     = dev_data->ats.qdep;
	iommu    = rlookup_amd_iommu(dev_data->dev);
	if (!iommu)
		return -EINVAL;

	build_inv_iotlb_pages(&cmd, dev_data->devid, qdep, address, size);

	return iommu_queue_command(iommu, &cmd);
}

static int device_flush_dte_alias(struct pci_dev *pdev, u16 alias, void *data)
{
	struct amd_iommu *iommu = data;

	return iommu_flush_dte(iommu, alias);
}

/*
 * Command send function for invalidating a device table entry
 */
static int device_flush_dte(struct iommu_dev_data *dev_data)
{
	struct amd_iommu *iommu;
	struct pci_dev *pdev = NULL;
	struct amd_iommu_pci_seg *pci_seg;
	u16 alias;
	int ret;

	iommu = rlookup_amd_iommu(dev_data->dev);
	if (!iommu)
		return -EINVAL;

	if (dev_is_pci(dev_data->dev))
		pdev = to_pci_dev(dev_data->dev);

	if (pdev)
		ret = pci_for_each_dma_alias(pdev,
					     device_flush_dte_alias, iommu);
	else
		ret = iommu_flush_dte(iommu, dev_data->devid);
	if (ret)
		return ret;

	pci_seg = iommu->pci_seg;
	alias = pci_seg->alias_table[dev_data->devid];
	if (alias != dev_data->devid) {
		ret = iommu_flush_dte(iommu, alias);
		if (ret)
			return ret;
	}

	if (dev_data->ats.enabled)
		ret = device_flush_iotlb(dev_data, 0, ~0UL);

	return ret;
}

/*
 * TLB invalidation function which is called from the mapping functions.
 * It invalidates a single PTE if the range to flush is within a single
 * page. Otherwise it flushes the whole TLB of the IOMMU.
 */
static void __domain_flush_pages(struct protection_domain *domain,
				 u64 address, size_t size, int pde)
{
	struct iommu_dev_data *dev_data;
	struct iommu_cmd cmd;
	int ret = 0, i;

	build_inv_iommu_pages(&cmd, address, size, domain->id, pde);

	for (i = 0; i < amd_iommu_get_num_iommus(); ++i) {
		if (!domain->dev_iommu[i])
			continue;

		/*
		 * Devices of this domain are behind this IOMMU
		 * We need a TLB flush
		 */
		ret |= iommu_queue_command(amd_iommus[i], &cmd);
	}

	list_for_each_entry(dev_data, &domain->dev_list, list) {

		if (!dev_data->ats.enabled)
			continue;

		ret |= device_flush_iotlb(dev_data, address, size);
	}

	WARN_ON(ret);
}

static void domain_flush_pages(struct protection_domain *domain,
			       u64 address, size_t size, int pde)
{
	if (likely(!amd_iommu_np_cache)) {
		__domain_flush_pages(domain, address, size, pde);
		return;
	}

	/*
	 * When NpCache is on, we infer that we run in a VM and use a vIOMMU.
	 * In such setups it is best to avoid flushes of ranges which are not
	 * naturally aligned, since it would lead to flushes of unmodified
	 * PTEs. Such flushes would require the hypervisor to do more work than
	 * necessary. Therefore, perform repeated flushes of aligned ranges
	 * until you cover the range. Each iteration flushes the smaller
	 * between the natural alignment of the address that we flush and the
	 * greatest naturally aligned region that fits in the range.
	 */
	while (size != 0) {
		int addr_alignment = __ffs(address);
		int size_alignment = __fls(size);
		int min_alignment;
		size_t flush_size;

		/*
		 * size is always non-zero, but address might be zero, causing
		 * addr_alignment to be negative. As the casting of the
		 * argument in __ffs(address) to long might trim the high bits
		 * of the address on x86-32, cast to long when doing the check.
		 */
		if (likely((unsigned long)address != 0))
			min_alignment = min(addr_alignment, size_alignment);
		else
			min_alignment = size_alignment;

		flush_size = 1ul << min_alignment;

		__domain_flush_pages(domain, address, flush_size, pde);
		address += flush_size;
		size -= flush_size;
	}
}

/* Flush the whole IO/TLB for a given protection domain - including PDE */
void amd_iommu_domain_flush_tlb_pde(struct protection_domain *domain)
{
	domain_flush_pages(domain, 0, CMD_INV_IOMMU_ALL_PAGES_ADDRESS, 1);
}

void amd_iommu_domain_flush_complete(struct protection_domain *domain)
{
	int i;

	for (i = 0; i < amd_iommu_get_num_iommus(); ++i) {
		if (domain && !domain->dev_iommu[i])
			continue;

		/*
		 * Devices of this domain are behind this IOMMU
		 * We need to wait for completion of all commands.
		 */
		iommu_completion_wait(amd_iommus[i]);
	}
}

/* Flush the not present cache if it exists */
static void domain_flush_np_cache(struct protection_domain *domain,
		dma_addr_t iova, size_t size)
{
	if (unlikely(amd_iommu_np_cache)) {
		unsigned long flags;

		spin_lock_irqsave(&domain->lock, flags);
		domain_flush_pages(domain, iova, size, 1);
		amd_iommu_domain_flush_complete(domain);
		spin_unlock_irqrestore(&domain->lock, flags);
	}
}


/*
 * This function flushes the DTEs for all devices in domain
 */
static void domain_flush_devices(struct protection_domain *domain)
{
	struct iommu_dev_data *dev_data;

	list_for_each_entry(dev_data, &domain->dev_list, list)
		device_flush_dte(dev_data);
}

/****************************************************************************
 *
 * The next functions belong to the domain allocation. A domain is
 * allocated for every IOMMU as the default domain. If device isolation
 * is enabled, every device get its own domain. The most important thing
 * about domains is the page table mapping the DMA address space they
 * contain.
 *
 ****************************************************************************/

static u16 domain_id_alloc(void)
{
	int id;

	spin_lock(&pd_bitmap_lock);
	id = find_first_zero_bit(amd_iommu_pd_alloc_bitmap, MAX_DOMAIN_ID);
	BUG_ON(id == 0);
	if (id > 0 && id < MAX_DOMAIN_ID)
		__set_bit(id, amd_iommu_pd_alloc_bitmap);
	else
		id = 0;
	spin_unlock(&pd_bitmap_lock);

	return id;
}

static void domain_id_free(int id)
{
	spin_lock(&pd_bitmap_lock);
	if (id > 0 && id < MAX_DOMAIN_ID)
		__clear_bit(id, amd_iommu_pd_alloc_bitmap);
	spin_unlock(&pd_bitmap_lock);
}

static void free_gcr3_tbl_level1(u64 *tbl)
{
	u64 *ptr;
	int i;

	for (i = 0; i < 512; ++i) {
		if (!(tbl[i] & GCR3_VALID))
			continue;

		ptr = iommu_phys_to_virt(tbl[i] & PAGE_MASK);

		free_page((unsigned long)ptr);
	}
}

static void free_gcr3_tbl_level2(u64 *tbl)
{
	u64 *ptr;
	int i;

	for (i = 0; i < 512; ++i) {
		if (!(tbl[i] & GCR3_VALID))
			continue;

		ptr = iommu_phys_to_virt(tbl[i] & PAGE_MASK);

		free_gcr3_tbl_level1(ptr);
	}
}

static void free_gcr3_table(struct protection_domain *domain)
{
	if (domain->glx == 2)
		free_gcr3_tbl_level2(domain->gcr3_tbl);
	else if (domain->glx == 1)
		free_gcr3_tbl_level1(domain->gcr3_tbl);
	else
		BUG_ON(domain->glx != 0);

	free_page((unsigned long)domain->gcr3_tbl);
}

static void set_dte_entry(struct amd_iommu *iommu, u16 devid,
			  struct protection_domain *domain, bool ats, bool ppr)
{
	u64 pte_root = 0;
	u64 flags = 0;
	u32 old_domid;
	struct dev_table_entry *dev_table = get_dev_table(iommu);

	if (domain->iop.mode != PAGE_MODE_NONE)
		pte_root = iommu_virt_to_phys(domain->iop.root);

	pte_root |= (domain->iop.mode & DEV_ENTRY_MODE_MASK)
		    << DEV_ENTRY_MODE_SHIFT;

	pte_root |= DTE_FLAG_IR | DTE_FLAG_IW | DTE_FLAG_V;

	/*
	 * When SNP is enabled, Only set TV bit when IOMMU
	 * page translation is in use.
	 */
	if (!amd_iommu_snp_en || (domain->id != 0))
		pte_root |= DTE_FLAG_TV;

	flags = dev_table[devid].data[1];

	if (ats)
		flags |= DTE_FLAG_IOTLB;

	if (ppr) {
		if (iommu_feature(iommu, FEATURE_EPHSUP))
			pte_root |= 1ULL << DEV_ENTRY_PPR;
	}

	if (domain->flags & PD_IOMMUV2_MASK) {
		u64 gcr3 = iommu_virt_to_phys(domain->gcr3_tbl);
		u64 glx  = domain->glx;
		u64 tmp;

		pte_root |= DTE_FLAG_GV;
		pte_root |= (glx & DTE_GLX_MASK) << DTE_GLX_SHIFT;

		/* First mask out possible old values for GCR3 table */
		tmp = DTE_GCR3_VAL_B(~0ULL) << DTE_GCR3_SHIFT_B;
		flags    &= ~tmp;

		tmp = DTE_GCR3_VAL_C(~0ULL) << DTE_GCR3_SHIFT_C;
		flags    &= ~tmp;

		/* Encode GCR3 table into DTE */
		tmp = DTE_GCR3_VAL_A(gcr3) << DTE_GCR3_SHIFT_A;
		pte_root |= tmp;

		tmp = DTE_GCR3_VAL_B(gcr3) << DTE_GCR3_SHIFT_B;
		flags    |= tmp;

		tmp = DTE_GCR3_VAL_C(gcr3) << DTE_GCR3_SHIFT_C;
		flags    |= tmp;

		if (domain->flags & PD_GIOV_MASK)
			pte_root |= DTE_FLAG_GIOV;
	}

	flags &= ~DEV_DOMID_MASK;
	flags |= domain->id;

	old_domid = dev_table[devid].data[1] & DEV_DOMID_MASK;
	dev_table[devid].data[1]  = flags;
	dev_table[devid].data[0]  = pte_root;

	/*
	 * A kdump kernel might be replacing a domain ID that was copied from
	 * the previous kernel--if so, it needs to flush the translation cache
	 * entries for the old domain ID that is being overwritten
	 */
	if (old_domid) {
		amd_iommu_flush_tlb_domid(iommu, old_domid);
	}
}

static void clear_dte_entry(struct amd_iommu *iommu, u16 devid)
{
	struct dev_table_entry *dev_table = get_dev_table(iommu);

	/* remove entry from the device table seen by the hardware */
	dev_table[devid].data[0]  = DTE_FLAG_V;

	if (!amd_iommu_snp_en)
		dev_table[devid].data[0] |= DTE_FLAG_TV;

	dev_table[devid].data[1] &= DTE_FLAG_MASK;

	amd_iommu_apply_erratum_63(iommu, devid);
}

static void do_attach(struct iommu_dev_data *dev_data,
		      struct protection_domain *domain)
{
	struct amd_iommu *iommu;
	bool ats;

	iommu = rlookup_amd_iommu(dev_data->dev);
	if (!iommu)
		return;
	ats   = dev_data->ats.enabled;

	/* Update data structures */
	dev_data->domain = domain;
	list_add(&dev_data->list, &domain->dev_list);

	/* Do reference counting */
	domain->dev_iommu[iommu->index] += 1;
	domain->dev_cnt                 += 1;

	/* Update device table */
	set_dte_entry(iommu, dev_data->devid, domain,
		      ats, dev_data->iommu_v2);
	clone_aliases(iommu, dev_data->dev);

	device_flush_dte(dev_data);
}

static void do_detach(struct iommu_dev_data *dev_data)
{
	struct protection_domain *domain = dev_data->domain;
	struct amd_iommu *iommu;

	iommu = rlookup_amd_iommu(dev_data->dev);
	if (!iommu)
		return;

	/* Update data structures */
	dev_data->domain = NULL;
	list_del(&dev_data->list);
	clear_dte_entry(iommu, dev_data->devid);
	clone_aliases(iommu, dev_data->dev);

	/* Flush the DTE entry */
	device_flush_dte(dev_data);

	/* Flush IOTLB */
	amd_iommu_domain_flush_tlb_pde(domain);

	/* Wait for the flushes to finish */
	amd_iommu_domain_flush_complete(domain);

	/* decrease reference counters - needs to happen after the flushes */
	domain->dev_iommu[iommu->index] -= 1;
	domain->dev_cnt                 -= 1;
}

static void pdev_iommuv2_disable(struct pci_dev *pdev)
{
	pci_disable_ats(pdev);
	pci_disable_pri(pdev);
	pci_disable_pasid(pdev);
}

static int pdev_pri_ats_enable(struct pci_dev *pdev)
{
	int ret;

	/* Only allow access to user-accessible pages */
	ret = pci_enable_pasid(pdev, 0);
	if (ret)
		return ret;

	/* First reset the PRI state of the device */
	ret = pci_reset_pri(pdev);
	if (ret)
		goto out_err_pasid;

	/* Enable PRI */
	/* FIXME: Hardcode number of outstanding requests for now */
	ret = pci_enable_pri(pdev, 32);
	if (ret)
		goto out_err_pasid;

	ret = pci_enable_ats(pdev, PAGE_SHIFT);
	if (ret)
		goto out_err_pri;

	return 0;

out_err_pri:
	pci_disable_pri(pdev);

out_err_pasid:
	pci_disable_pasid(pdev);

	return ret;
}

/*
 * If a device is not yet associated with a domain, this function makes the
 * device visible in the domain
 */
static int attach_device(struct device *dev,
			 struct protection_domain *domain)
{
	struct iommu_dev_data *dev_data;
	struct pci_dev *pdev;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&domain->lock, flags);

	dev_data = dev_iommu_priv_get(dev);

	spin_lock(&dev_data->lock);

	ret = -EBUSY;
	if (dev_data->domain != NULL)
		goto out;

	if (!dev_is_pci(dev))
		goto skip_ats_check;

	pdev = to_pci_dev(dev);
	if (domain->flags & PD_IOMMUV2_MASK) {
		struct iommu_domain *def_domain = iommu_get_dma_domain(dev);

		ret = -EINVAL;

		/*
		 * In case of using AMD_IOMMU_V1 page table mode and the device
		 * is enabling for PPR/ATS support (using v2 table),
		 * we need to make sure that the domain type is identity map.
		 */
		if ((amd_iommu_pgtable == AMD_IOMMU_V1) &&
		    def_domain->type != IOMMU_DOMAIN_IDENTITY) {
			goto out;
		}

		if (dev_data->iommu_v2) {
			if (pdev_pri_ats_enable(pdev) != 0)
				goto out;

			dev_data->ats.enabled = true;
			dev_data->ats.qdep    = pci_ats_queue_depth(pdev);
			dev_data->pri_tlp     = pci_prg_resp_pasid_required(pdev);
		}
	} else if (amd_iommu_iotlb_sup &&
		   pci_enable_ats(pdev, PAGE_SHIFT) == 0) {
		dev_data->ats.enabled = true;
		dev_data->ats.qdep    = pci_ats_queue_depth(pdev);
	}

skip_ats_check:
	ret = 0;

	do_attach(dev_data, domain);

	/*
	 * We might boot into a crash-kernel here. The crashed kernel
	 * left the caches in the IOMMU dirty. So we have to flush
	 * here to evict all dirty stuff.
	 */
	amd_iommu_domain_flush_tlb_pde(domain);

	amd_iommu_domain_flush_complete(domain);

out:
	spin_unlock(&dev_data->lock);

	spin_unlock_irqrestore(&domain->lock, flags);

	return ret;
}

/*
 * Removes a device from a protection domain (with devtable_lock held)
 */
static void detach_device(struct device *dev)
{
	struct protection_domain *domain;
	struct iommu_dev_data *dev_data;
	unsigned long flags;

	dev_data = dev_iommu_priv_get(dev);
	domain   = dev_data->domain;

	spin_lock_irqsave(&domain->lock, flags);

	spin_lock(&dev_data->lock);

	/*
	 * First check if the device is still attached. It might already
	 * be detached from its domain because the generic
	 * iommu_detach_group code detached it and we try again here in
	 * our alias handling.
	 */
	if (WARN_ON(!dev_data->domain))
		goto out;

	do_detach(dev_data);

	if (!dev_is_pci(dev))
		goto out;

	if (domain->flags & PD_IOMMUV2_MASK && dev_data->iommu_v2)
		pdev_iommuv2_disable(to_pci_dev(dev));
	else if (dev_data->ats.enabled)
		pci_disable_ats(to_pci_dev(dev));

	dev_data->ats.enabled = false;

out:
	spin_unlock(&dev_data->lock);

	spin_unlock_irqrestore(&domain->lock, flags);
}

static struct iommu_device *amd_iommu_probe_device(struct device *dev)
{
	struct iommu_device *iommu_dev;
	struct amd_iommu *iommu;
	int ret;

	if (!check_device(dev))
		return ERR_PTR(-ENODEV);

	iommu = rlookup_amd_iommu(dev);
	if (!iommu)
		return ERR_PTR(-ENODEV);

	/* Not registered yet? */
	if (!iommu->iommu.ops)
		return ERR_PTR(-ENODEV);

	if (dev_iommu_priv_get(dev))
		return &iommu->iommu;

	ret = iommu_init_device(iommu, dev);
	if (ret) {
		if (ret != -ENOTSUPP)
			dev_err(dev, "Failed to initialize - trying to proceed anyway\n");
		iommu_dev = ERR_PTR(ret);
		iommu_ignore_device(iommu, dev);
	} else {
		amd_iommu_set_pci_msi_domain(dev, iommu);
		iommu_dev = &iommu->iommu;
	}

	iommu_completion_wait(iommu);

	return iommu_dev;
}

static void amd_iommu_probe_finalize(struct device *dev)
{
	/* Domains are initialized for this device - have a look what we ended up with */
	set_dma_ops(dev, NULL);
	iommu_setup_dma_ops(dev, 0, U64_MAX);
}

static void amd_iommu_release_device(struct device *dev)
{
	struct amd_iommu *iommu;

	if (!check_device(dev))
		return;

	iommu = rlookup_amd_iommu(dev);
	if (!iommu)
		return;

	amd_iommu_uninit_device(dev);
	iommu_completion_wait(iommu);
}

static struct iommu_group *amd_iommu_device_group(struct device *dev)
{
	if (dev_is_pci(dev))
		return pci_device_group(dev);

	return acpihid_device_group(dev);
}

/*****************************************************************************
 *
 * The next functions belong to the dma_ops mapping/unmapping code.
 *
 *****************************************************************************/

static void update_device_table(struct protection_domain *domain)
{
	struct iommu_dev_data *dev_data;

	list_for_each_entry(dev_data, &domain->dev_list, list) {
		struct amd_iommu *iommu = rlookup_amd_iommu(dev_data->dev);

		if (!iommu)
			continue;
		set_dte_entry(iommu, dev_data->devid, domain,
			      dev_data->ats.enabled, dev_data->iommu_v2);
		clone_aliases(iommu, dev_data->dev);
	}
}

void amd_iommu_update_and_flush_device_table(struct protection_domain *domain)
{
	update_device_table(domain);
	domain_flush_devices(domain);
}

void amd_iommu_domain_update(struct protection_domain *domain)
{
	/* Update device table */
	amd_iommu_update_and_flush_device_table(domain);

	/* Flush domain TLB(s) and wait for completion */
	amd_iommu_domain_flush_tlb_pde(domain);
	amd_iommu_domain_flush_complete(domain);
}

/*****************************************************************************
 *
 * The following functions belong to the exported interface of AMD IOMMU
 *
 * This interface allows access to lower level functions of the IOMMU
 * like protection domain handling and assignement of devices to domains
 * which is not possible with the dma_ops interface.
 *
 *****************************************************************************/

static void cleanup_domain(struct protection_domain *domain)
{
	struct iommu_dev_data *entry;
	unsigned long flags;

	spin_lock_irqsave(&domain->lock, flags);

	while (!list_empty(&domain->dev_list)) {
		entry = list_first_entry(&domain->dev_list,
					 struct iommu_dev_data, list);
		BUG_ON(!entry->domain);
		do_detach(entry);
	}

	spin_unlock_irqrestore(&domain->lock, flags);
}

static void protection_domain_free(struct protection_domain *domain)
{
	if (!domain)
		return;

	if (domain->iop.pgtbl_cfg.tlb)
		free_io_pgtable_ops(&domain->iop.iop.ops);

	if (domain->id)
		domain_id_free(domain->id);

	kfree(domain);
}

static int protection_domain_init_v1(struct protection_domain *domain, int mode)
{
	u64 *pt_root = NULL;

	BUG_ON(mode < PAGE_MODE_NONE || mode > PAGE_MODE_6_LEVEL);

	spin_lock_init(&domain->lock);
	domain->id = domain_id_alloc();
	if (!domain->id)
		return -ENOMEM;
	INIT_LIST_HEAD(&domain->dev_list);

	if (mode != PAGE_MODE_NONE) {
		pt_root = (void *)get_zeroed_page(GFP_KERNEL);
		if (!pt_root) {
			domain_id_free(domain->id);
			return -ENOMEM;
		}
	}

	amd_iommu_domain_set_pgtable(domain, pt_root, mode);

	return 0;
}

static int protection_domain_init_v2(struct protection_domain *domain)
{
	spin_lock_init(&domain->lock);
	domain->id = domain_id_alloc();
	if (!domain->id)
		return -ENOMEM;
	INIT_LIST_HEAD(&domain->dev_list);

	domain->flags |= PD_GIOV_MASK;

	domain->domain.pgsize_bitmap = AMD_IOMMU_PGSIZES_V2;

	if (domain_enable_v2(domain, 1)) {
		domain_id_free(domain->id);
		return -ENOMEM;
	}

	return 0;
}

static struct protection_domain *protection_domain_alloc(unsigned int type)
{
	struct io_pgtable_ops *pgtbl_ops;
	struct protection_domain *domain;
	int pgtable = amd_iommu_pgtable;
	int mode = DEFAULT_PGTABLE_LEVEL;
	int ret;

	domain = kzalloc(sizeof(*domain), GFP_KERNEL);
	if (!domain)
		return NULL;

	/*
	 * Force IOMMU v1 page table when iommu=pt and
	 * when allocating domain for pass-through devices.
	 */
	if (type == IOMMU_DOMAIN_IDENTITY) {
		pgtable = AMD_IOMMU_V1;
		mode = PAGE_MODE_NONE;
	} else if (type == IOMMU_DOMAIN_UNMANAGED) {
		pgtable = AMD_IOMMU_V1;
	}

	switch (pgtable) {
	case AMD_IOMMU_V1:
		ret = protection_domain_init_v1(domain, mode);
		break;
	case AMD_IOMMU_V2:
		ret = protection_domain_init_v2(domain);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret)
		goto out_err;

	pgtbl_ops = alloc_io_pgtable_ops(pgtable, &domain->iop.pgtbl_cfg, domain);
	if (!pgtbl_ops) {
		domain_id_free(domain->id);
		goto out_err;
	}

	return domain;
out_err:
	kfree(domain);
	return NULL;
}

static struct iommu_domain *amd_iommu_domain_alloc(unsigned type)
{
	struct protection_domain *domain;

	/*
	 * Since DTE[Mode]=0 is prohibited on SNP-enabled system,
	 * default to use IOMMU_DOMAIN_DMA[_FQ].
	 */
	if (amd_iommu_snp_en && (type == IOMMU_DOMAIN_IDENTITY))
		return NULL;

	domain = protection_domain_alloc(type);
	if (!domain)
		return NULL;

	domain->domain.geometry.aperture_start = 0;
	domain->domain.geometry.aperture_end   = ~0ULL;
	domain->domain.geometry.force_aperture = true;

	return &domain->domain;
}

static void amd_iommu_domain_free(struct iommu_domain *dom)
{
	struct protection_domain *domain;

	domain = to_pdomain(dom);

	if (domain->dev_cnt > 0)
		cleanup_domain(domain);

	BUG_ON(domain->dev_cnt != 0);

	if (!dom)
		return;

	if (domain->flags & PD_IOMMUV2_MASK)
		free_gcr3_table(domain);

	protection_domain_free(domain);
}

static void amd_iommu_detach_device(struct iommu_domain *dom,
				    struct device *dev)
{
	struct iommu_dev_data *dev_data = dev_iommu_priv_get(dev);
	struct amd_iommu *iommu;

	if (!check_device(dev))
		return;

	if (dev_data->domain != NULL)
		detach_device(dev);

	iommu = rlookup_amd_iommu(dev);
	if (!iommu)
		return;

#ifdef CONFIG_IRQ_REMAP
	if (AMD_IOMMU_GUEST_IR_VAPIC(amd_iommu_guest_ir) &&
	    (dom->type == IOMMU_DOMAIN_UNMANAGED))
		dev_data->use_vapic = 0;
#endif

	iommu_completion_wait(iommu);
}

static int amd_iommu_attach_device(struct iommu_domain *dom,
				   struct device *dev)
{
	struct protection_domain *domain = to_pdomain(dom);
	struct iommu_dev_data *dev_data;
	struct amd_iommu *iommu;
	int ret;

	if (!check_device(dev))
		return -EINVAL;

	dev_data = dev_iommu_priv_get(dev);
	dev_data->defer_attach = false;

	iommu = rlookup_amd_iommu(dev);
	if (!iommu)
		return -EINVAL;

	if (dev_data->domain)
		detach_device(dev);

	ret = attach_device(dev, domain);

#ifdef CONFIG_IRQ_REMAP
	if (AMD_IOMMU_GUEST_IR_VAPIC(amd_iommu_guest_ir)) {
		if (dom->type == IOMMU_DOMAIN_UNMANAGED)
			dev_data->use_vapic = 1;
		else
			dev_data->use_vapic = 0;
	}
#endif

	iommu_completion_wait(iommu);

	return ret;
}

static void amd_iommu_iotlb_sync_map(struct iommu_domain *dom,
				     unsigned long iova, size_t size)
{
	struct protection_domain *domain = to_pdomain(dom);
	struct io_pgtable_ops *ops = &domain->iop.iop.ops;

	if (ops->map_pages)
		domain_flush_np_cache(domain, iova, size);
}

static int amd_iommu_map_pages(struct iommu_domain *dom, unsigned long iova,
			       phys_addr_t paddr, size_t pgsize, size_t pgcount,
			       int iommu_prot, gfp_t gfp, size_t *mapped)
{
	struct protection_domain *domain = to_pdomain(dom);
	struct io_pgtable_ops *ops = &domain->iop.iop.ops;
	int prot = 0;
	int ret = -EINVAL;

	if ((amd_iommu_pgtable == AMD_IOMMU_V1) &&
	    (domain->iop.mode == PAGE_MODE_NONE))
		return -EINVAL;

	if (iommu_prot & IOMMU_READ)
		prot |= IOMMU_PROT_IR;
	if (iommu_prot & IOMMU_WRITE)
		prot |= IOMMU_PROT_IW;

	if (ops->map_pages) {
		ret = ops->map_pages(ops, iova, paddr, pgsize,
				     pgcount, prot, gfp, mapped);
	}

	return ret;
}

static void amd_iommu_iotlb_gather_add_page(struct iommu_domain *domain,
					    struct iommu_iotlb_gather *gather,
					    unsigned long iova, size_t size)
{
	/*
	 * AMD's IOMMU can flush as many pages as necessary in a single flush.
	 * Unless we run in a virtual machine, which can be inferred according
	 * to whether "non-present cache" is on, it is probably best to prefer
	 * (potentially) too extensive TLB flushing (i.e., more misses) over
	 * mutliple TLB flushes (i.e., more flushes). For virtual machines the
	 * hypervisor needs to synchronize the host IOMMU PTEs with those of
	 * the guest, and the trade-off is different: unnecessary TLB flushes
	 * should be avoided.
	 */
	if (amd_iommu_np_cache &&
	    iommu_iotlb_gather_is_disjoint(gather, iova, size))
		iommu_iotlb_sync(domain, gather);

	iommu_iotlb_gather_add_range(gather, iova, size);
}

static size_t amd_iommu_unmap_pages(struct iommu_domain *dom, unsigned long iova,
				    size_t pgsize, size_t pgcount,
				    struct iommu_iotlb_gather *gather)
{
	struct protection_domain *domain = to_pdomain(dom);
	struct io_pgtable_ops *ops = &domain->iop.iop.ops;
	size_t r;

	if ((amd_iommu_pgtable == AMD_IOMMU_V1) &&
	    (domain->iop.mode == PAGE_MODE_NONE))
		return 0;

	r = (ops->unmap_pages) ? ops->unmap_pages(ops, iova, pgsize, pgcount, NULL) : 0;

	if (r)
		amd_iommu_iotlb_gather_add_page(dom, gather, iova, r);

	return r;
}

static phys_addr_t amd_iommu_iova_to_phys(struct iommu_domain *dom,
					  dma_addr_t iova)
{
	struct protection_domain *domain = to_pdomain(dom);
	struct io_pgtable_ops *ops = &domain->iop.iop.ops;

	return ops->iova_to_phys(ops, iova);
}

static bool amd_iommu_capable(struct device *dev, enum iommu_cap cap)
{
	switch (cap) {
	case IOMMU_CAP_CACHE_COHERENCY:
		return true;
	case IOMMU_CAP_INTR_REMAP:
		return (irq_remapping_enabled == 1);
	case IOMMU_CAP_NOEXEC:
		return false;
	case IOMMU_CAP_PRE_BOOT_PROTECTION:
		return amdr_ivrs_remap_support;
	default:
		break;
	}

	return false;
}

static void amd_iommu_get_resv_regions(struct device *dev,
				       struct list_head *head)
{
	struct iommu_resv_region *region;
	struct unity_map_entry *entry;
	struct amd_iommu *iommu;
	struct amd_iommu_pci_seg *pci_seg;
	int devid, sbdf;

	sbdf = get_device_sbdf_id(dev);
	if (sbdf < 0)
		return;

	devid = PCI_SBDF_TO_DEVID(sbdf);
	iommu = rlookup_amd_iommu(dev);
	if (!iommu)
		return;
	pci_seg = iommu->pci_seg;

	list_for_each_entry(entry, &pci_seg->unity_map, list) {
		int type, prot = 0;
		size_t length;

		if (devid < entry->devid_start || devid > entry->devid_end)
			continue;

		type   = IOMMU_RESV_DIRECT;
		length = entry->address_end - entry->address_start;
		if (entry->prot & IOMMU_PROT_IR)
			prot |= IOMMU_READ;
		if (entry->prot & IOMMU_PROT_IW)
			prot |= IOMMU_WRITE;
		if (entry->prot & IOMMU_UNITY_MAP_FLAG_EXCL_RANGE)
			/* Exclusion range */
			type = IOMMU_RESV_RESERVED;

		region = iommu_alloc_resv_region(entry->address_start,
						 length, prot, type,
						 GFP_KERNEL);
		if (!region) {
			dev_err(dev, "Out of memory allocating dm-regions\n");
			return;
		}
		list_add_tail(&region->list, head);
	}

	region = iommu_alloc_resv_region(MSI_RANGE_START,
					 MSI_RANGE_END - MSI_RANGE_START + 1,
					 0, IOMMU_RESV_MSI, GFP_KERNEL);
	if (!region)
		return;
	list_add_tail(&region->list, head);

	region = iommu_alloc_resv_region(HT_RANGE_START,
					 HT_RANGE_END - HT_RANGE_START + 1,
					 0, IOMMU_RESV_RESERVED, GFP_KERNEL);
	if (!region)
		return;
	list_add_tail(&region->list, head);
}

bool amd_iommu_is_attach_deferred(struct device *dev)
{
	struct iommu_dev_data *dev_data = dev_iommu_priv_get(dev);

	return dev_data->defer_attach;
}
EXPORT_SYMBOL_GPL(amd_iommu_is_attach_deferred);

static void amd_iommu_flush_iotlb_all(struct iommu_domain *domain)
{
	struct protection_domain *dom = to_pdomain(domain);
	unsigned long flags;

	spin_lock_irqsave(&dom->lock, flags);
	amd_iommu_domain_flush_tlb_pde(dom);
	amd_iommu_domain_flush_complete(dom);
	spin_unlock_irqrestore(&dom->lock, flags);
}

static void amd_iommu_iotlb_sync(struct iommu_domain *domain,
				 struct iommu_iotlb_gather *gather)
{
	struct protection_domain *dom = to_pdomain(domain);
	unsigned long flags;

	spin_lock_irqsave(&dom->lock, flags);
	domain_flush_pages(dom, gather->start, gather->end - gather->start, 1);
	amd_iommu_domain_flush_complete(dom);
	spin_unlock_irqrestore(&dom->lock, flags);
}

static int amd_iommu_def_domain_type(struct device *dev)
{
	struct iommu_dev_data *dev_data;

	dev_data = dev_iommu_priv_get(dev);
	if (!dev_data)
		return 0;

	/*
	 * Do not identity map IOMMUv2 capable devices when:
	 *  - memory encryption is active, because some of those devices
	 *    (AMD GPUs) don't have the encryption bit in their DMA-mask
	 *    and require remapping.
	 *  - SNP is enabled, because it prohibits DTE[Mode]=0.
	 */
	if (dev_data->iommu_v2 &&
	    !cc_platform_has(CC_ATTR_MEM_ENCRYPT) &&
	    !amd_iommu_snp_en) {
		return IOMMU_DOMAIN_IDENTITY;
	}

	return 0;
}

static bool amd_iommu_enforce_cache_coherency(struct iommu_domain *domain)
{
	/* IOMMU_PTE_FC is always set */
	return true;
}

const struct iommu_ops amd_iommu_ops = {
	.capable = amd_iommu_capable,
	.domain_alloc = amd_iommu_domain_alloc,
	.probe_device = amd_iommu_probe_device,
	.release_device = amd_iommu_release_device,
	.probe_finalize = amd_iommu_probe_finalize,
	.device_group = amd_iommu_device_group,
	.get_resv_regions = amd_iommu_get_resv_regions,
	.is_attach_deferred = amd_iommu_is_attach_deferred,
	.pgsize_bitmap	= AMD_IOMMU_PGSIZES,
	.def_domain_type = amd_iommu_def_domain_type,
	.default_domain_ops = &(const struct iommu_domain_ops) {
		.attach_dev	= amd_iommu_attach_device,
		.detach_dev	= amd_iommu_detach_device,
		.map_pages	= amd_iommu_map_pages,
		.unmap_pages	= amd_iommu_unmap_pages,
		.iotlb_sync_map	= amd_iommu_iotlb_sync_map,
		.iova_to_phys	= amd_iommu_iova_to_phys,
		.flush_iotlb_all = amd_iommu_flush_iotlb_all,
		.iotlb_sync	= amd_iommu_iotlb_sync,
		.free		= amd_iommu_domain_free,
		.enforce_cache_coherency = amd_iommu_enforce_cache_coherency,
	}
};

/*****************************************************************************
 *
 * The next functions do a basic initialization of IOMMU for pass through
 * mode
 *
 * In passthrough mode the IOMMU is initialized and enabled but not used for
 * DMA-API translation.
 *
 *****************************************************************************/

/* IOMMUv2 specific functions */
int amd_iommu_register_ppr_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&ppr_notifier, nb);
}
EXPORT_SYMBOL(amd_iommu_register_ppr_notifier);

int amd_iommu_unregister_ppr_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&ppr_notifier, nb);
}
EXPORT_SYMBOL(amd_iommu_unregister_ppr_notifier);

void amd_iommu_domain_direct_map(struct iommu_domain *dom)
{
	struct protection_domain *domain = to_pdomain(dom);
	unsigned long flags;

	spin_lock_irqsave(&domain->lock, flags);

	if (domain->iop.pgtbl_cfg.tlb)
		free_io_pgtable_ops(&domain->iop.iop.ops);

	spin_unlock_irqrestore(&domain->lock, flags);
}
EXPORT_SYMBOL(amd_iommu_domain_direct_map);

/* Note: This function expects iommu_domain->lock to be held prior calling the function. */
static int domain_enable_v2(struct protection_domain *domain, int pasids)
{
	int levels;

	/* Number of GCR3 table levels required */
	for (levels = 0; (pasids - 1) & ~0x1ff; pasids >>= 9)
		levels += 1;

	if (levels > amd_iommu_max_glx_val)
		return -EINVAL;

	domain->gcr3_tbl = (void *)get_zeroed_page(GFP_ATOMIC);
	if (domain->gcr3_tbl == NULL)
		return -ENOMEM;

	domain->glx      = levels;
	domain->flags   |= PD_IOMMUV2_MASK;

	amd_iommu_domain_update(domain);

	return 0;
}

int amd_iommu_domain_enable_v2(struct iommu_domain *dom, int pasids)
{
	struct protection_domain *pdom = to_pdomain(dom);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&pdom->lock, flags);

	/*
	 * Save us all sanity checks whether devices already in the
	 * domain support IOMMUv2. Just force that the domain has no
	 * devices attached when it is switched into IOMMUv2 mode.
	 */
	ret = -EBUSY;
	if (pdom->dev_cnt > 0 || pdom->flags & PD_IOMMUV2_MASK)
		goto out;

	if (!pdom->gcr3_tbl)
		ret = domain_enable_v2(pdom, pasids);

out:
	spin_unlock_irqrestore(&pdom->lock, flags);
	return ret;
}
EXPORT_SYMBOL(amd_iommu_domain_enable_v2);

static int __flush_pasid(struct protection_domain *domain, u32 pasid,
			 u64 address, bool size)
{
	struct iommu_dev_data *dev_data;
	struct iommu_cmd cmd;
	int i, ret;

	if (!(domain->flags & PD_IOMMUV2_MASK))
		return -EINVAL;

	build_inv_iommu_pasid(&cmd, domain->id, pasid, address, size);

	/*
	 * IOMMU TLB needs to be flushed before Device TLB to
	 * prevent device TLB refill from IOMMU TLB
	 */
	for (i = 0; i < amd_iommu_get_num_iommus(); ++i) {
		if (domain->dev_iommu[i] == 0)
			continue;

		ret = iommu_queue_command(amd_iommus[i], &cmd);
		if (ret != 0)
			goto out;
	}

	/* Wait until IOMMU TLB flushes are complete */
	amd_iommu_domain_flush_complete(domain);

	/* Now flush device TLBs */
	list_for_each_entry(dev_data, &domain->dev_list, list) {
		struct amd_iommu *iommu;
		int qdep;

		/*
		   There might be non-IOMMUv2 capable devices in an IOMMUv2
		 * domain.
		 */
		if (!dev_data->ats.enabled)
			continue;

		qdep  = dev_data->ats.qdep;
		iommu = rlookup_amd_iommu(dev_data->dev);
		if (!iommu)
			continue;
		build_inv_iotlb_pasid(&cmd, dev_data->devid, pasid,
				      qdep, address, size);

		ret = iommu_queue_command(iommu, &cmd);
		if (ret != 0)
			goto out;
	}

	/* Wait until all device TLBs are flushed */
	amd_iommu_domain_flush_complete(domain);

	ret = 0;

out:

	return ret;
}

static int __amd_iommu_flush_page(struct protection_domain *domain, u32 pasid,
				  u64 address)
{
	return __flush_pasid(domain, pasid, address, false);
}

int amd_iommu_flush_page(struct iommu_domain *dom, u32 pasid,
			 u64 address)
{
	struct protection_domain *domain = to_pdomain(dom);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&domain->lock, flags);
	ret = __amd_iommu_flush_page(domain, pasid, address);
	spin_unlock_irqrestore(&domain->lock, flags);

	return ret;
}
EXPORT_SYMBOL(amd_iommu_flush_page);

static int __amd_iommu_flush_tlb(struct protection_domain *domain, u32 pasid)
{
	return __flush_pasid(domain, pasid, CMD_INV_IOMMU_ALL_PAGES_ADDRESS,
			     true);
}

int amd_iommu_flush_tlb(struct iommu_domain *dom, u32 pasid)
{
	struct protection_domain *domain = to_pdomain(dom);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&domain->lock, flags);
	ret = __amd_iommu_flush_tlb(domain, pasid);
	spin_unlock_irqrestore(&domain->lock, flags);

	return ret;
}
EXPORT_SYMBOL(amd_iommu_flush_tlb);

static u64 *__get_gcr3_pte(u64 *root, int level, u32 pasid, bool alloc)
{
	int index;
	u64 *pte;

	while (true) {

		index = (pasid >> (9 * level)) & 0x1ff;
		pte   = &root[index];

		if (level == 0)
			break;

		if (!(*pte & GCR3_VALID)) {
			if (!alloc)
				return NULL;

			root = (void *)get_zeroed_page(GFP_ATOMIC);
			if (root == NULL)
				return NULL;

			*pte = iommu_virt_to_phys(root) | GCR3_VALID;
		}

		root = iommu_phys_to_virt(*pte & PAGE_MASK);

		level -= 1;
	}

	return pte;
}

static int __set_gcr3(struct protection_domain *domain, u32 pasid,
		      unsigned long cr3)
{
	u64 *pte;

	if (domain->iop.mode != PAGE_MODE_NONE)
		return -EINVAL;

	pte = __get_gcr3_pte(domain->gcr3_tbl, domain->glx, pasid, true);
	if (pte == NULL)
		return -ENOMEM;

	*pte = (cr3 & PAGE_MASK) | GCR3_VALID;

	return __amd_iommu_flush_tlb(domain, pasid);
}

static int __clear_gcr3(struct protection_domain *domain, u32 pasid)
{
	u64 *pte;

	if (domain->iop.mode != PAGE_MODE_NONE)
		return -EINVAL;

	pte = __get_gcr3_pte(domain->gcr3_tbl, domain->glx, pasid, false);
	if (pte == NULL)
		return 0;

	*pte = 0;

	return __amd_iommu_flush_tlb(domain, pasid);
}

int amd_iommu_domain_set_gcr3(struct iommu_domain *dom, u32 pasid,
			      unsigned long cr3)
{
	struct protection_domain *domain = to_pdomain(dom);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&domain->lock, flags);
	ret = __set_gcr3(domain, pasid, cr3);
	spin_unlock_irqrestore(&domain->lock, flags);

	return ret;
}
EXPORT_SYMBOL(amd_iommu_domain_set_gcr3);

int amd_iommu_domain_clear_gcr3(struct iommu_domain *dom, u32 pasid)
{
	struct protection_domain *domain = to_pdomain(dom);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&domain->lock, flags);
	ret = __clear_gcr3(domain, pasid);
	spin_unlock_irqrestore(&domain->lock, flags);

	return ret;
}
EXPORT_SYMBOL(amd_iommu_domain_clear_gcr3);

int amd_iommu_complete_ppr(struct pci_dev *pdev, u32 pasid,
			   int status, int tag)
{
	struct iommu_dev_data *dev_data;
	struct amd_iommu *iommu;
	struct iommu_cmd cmd;

	dev_data = dev_iommu_priv_get(&pdev->dev);
	iommu    = rlookup_amd_iommu(&pdev->dev);
	if (!iommu)
		return -ENODEV;

	build_complete_ppr(&cmd, dev_data->devid, pasid, status,
			   tag, dev_data->pri_tlp);

	return iommu_queue_command(iommu, &cmd);
}
EXPORT_SYMBOL(amd_iommu_complete_ppr);

int amd_iommu_device_info(struct pci_dev *pdev,
                          struct amd_iommu_device_info *info)
{
	int max_pasids;
	int pos;

	if (pdev == NULL || info == NULL)
		return -EINVAL;

	if (!amd_iommu_v2_supported())
		return -EINVAL;

	memset(info, 0, sizeof(*info));

	if (pci_ats_supported(pdev))
		info->flags |= AMD_IOMMU_DEVICE_FLAG_ATS_SUP;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_PRI);
	if (pos)
		info->flags |= AMD_IOMMU_DEVICE_FLAG_PRI_SUP;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_PASID);
	if (pos) {
		int features;

		max_pasids = 1 << (9 * (amd_iommu_max_glx_val + 1));
		max_pasids = min(max_pasids, (1 << 20));

		info->flags |= AMD_IOMMU_DEVICE_FLAG_PASID_SUP;
		info->max_pasids = min(pci_max_pasids(pdev), max_pasids);

		features = pci_pasid_features(pdev);
		if (features & PCI_PASID_CAP_EXEC)
			info->flags |= AMD_IOMMU_DEVICE_FLAG_EXEC_SUP;
		if (features & PCI_PASID_CAP_PRIV)
			info->flags |= AMD_IOMMU_DEVICE_FLAG_PRIV_SUP;
	}

	return 0;
}
EXPORT_SYMBOL(amd_iommu_device_info);

#ifdef CONFIG_IRQ_REMAP

/*****************************************************************************
 *
 * Interrupt Remapping Implementation
 *
 *****************************************************************************/

static struct irq_chip amd_ir_chip;
static DEFINE_SPINLOCK(iommu_table_lock);

static void set_dte_irq_entry(struct amd_iommu *iommu, u16 devid,
			      struct irq_remap_table *table)
{
	u64 dte;
	struct dev_table_entry *dev_table = get_dev_table(iommu);

	dte	= dev_table[devid].data[2];
	dte	&= ~DTE_IRQ_PHYS_ADDR_MASK;
	dte	|= iommu_virt_to_phys(table->table);
	dte	|= DTE_IRQ_REMAP_INTCTL;
	dte	|= DTE_INTTABLEN;
	dte	|= DTE_IRQ_REMAP_ENABLE;

	dev_table[devid].data[2] = dte;
}

static struct irq_remap_table *get_irq_table(struct amd_iommu *iommu, u16 devid)
{
	struct irq_remap_table *table;
	struct amd_iommu_pci_seg *pci_seg = iommu->pci_seg;

	if (WARN_ONCE(!pci_seg->rlookup_table[devid],
		      "%s: no iommu for devid %x:%x\n",
		      __func__, pci_seg->id, devid))
		return NULL;

	table = pci_seg->irq_lookup_table[devid];
	if (WARN_ONCE(!table, "%s: no table for devid %x:%x\n",
		      __func__, pci_seg->id, devid))
		return NULL;

	return table;
}

static struct irq_remap_table *__alloc_irq_table(void)
{
	struct irq_remap_table *table;

	table = kzalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		return NULL;

	table->table = kmem_cache_alloc(amd_iommu_irq_cache, GFP_KERNEL);
	if (!table->table) {
		kfree(table);
		return NULL;
	}
	raw_spin_lock_init(&table->lock);

	if (!AMD_IOMMU_GUEST_IR_GA(amd_iommu_guest_ir))
		memset(table->table, 0,
		       MAX_IRQS_PER_TABLE * sizeof(u32));
	else
		memset(table->table, 0,
		       (MAX_IRQS_PER_TABLE * (sizeof(u64) * 2)));
	return table;
}

static void set_remap_table_entry(struct amd_iommu *iommu, u16 devid,
				  struct irq_remap_table *table)
{
	struct amd_iommu_pci_seg *pci_seg = iommu->pci_seg;

	pci_seg->irq_lookup_table[devid] = table;
	set_dte_irq_entry(iommu, devid, table);
	iommu_flush_dte(iommu, devid);
}

static int set_remap_table_entry_alias(struct pci_dev *pdev, u16 alias,
				       void *data)
{
	struct irq_remap_table *table = data;
	struct amd_iommu_pci_seg *pci_seg;
	struct amd_iommu *iommu = rlookup_amd_iommu(&pdev->dev);

	if (!iommu)
		return -EINVAL;

	pci_seg = iommu->pci_seg;
	pci_seg->irq_lookup_table[alias] = table;
	set_dte_irq_entry(iommu, alias, table);
	iommu_flush_dte(pci_seg->rlookup_table[alias], alias);

	return 0;
}

static struct irq_remap_table *alloc_irq_table(struct amd_iommu *iommu,
					       u16 devid, struct pci_dev *pdev)
{
	struct irq_remap_table *table = NULL;
	struct irq_remap_table *new_table = NULL;
	struct amd_iommu_pci_seg *pci_seg;
	unsigned long flags;
	u16 alias;

	spin_lock_irqsave(&iommu_table_lock, flags);

	pci_seg = iommu->pci_seg;
	table = pci_seg->irq_lookup_table[devid];
	if (table)
		goto out_unlock;

	alias = pci_seg->alias_table[devid];
	table = pci_seg->irq_lookup_table[alias];
	if (table) {
		set_remap_table_entry(iommu, devid, table);
		goto out_wait;
	}
	spin_unlock_irqrestore(&iommu_table_lock, flags);

	/* Nothing there yet, allocate new irq remapping table */
	new_table = __alloc_irq_table();
	if (!new_table)
		return NULL;

	spin_lock_irqsave(&iommu_table_lock, flags);

	table = pci_seg->irq_lookup_table[devid];
	if (table)
		goto out_unlock;

	table = pci_seg->irq_lookup_table[alias];
	if (table) {
		set_remap_table_entry(iommu, devid, table);
		goto out_wait;
	}

	table = new_table;
	new_table = NULL;

	if (pdev)
		pci_for_each_dma_alias(pdev, set_remap_table_entry_alias,
				       table);
	else
		set_remap_table_entry(iommu, devid, table);

	if (devid != alias)
		set_remap_table_entry(iommu, alias, table);

out_wait:
	iommu_completion_wait(iommu);

out_unlock:
	spin_unlock_irqrestore(&iommu_table_lock, flags);

	if (new_table) {
		kmem_cache_free(amd_iommu_irq_cache, new_table->table);
		kfree(new_table);
	}
	return table;
}

static int alloc_irq_index(struct amd_iommu *iommu, u16 devid, int count,
			   bool align, struct pci_dev *pdev)
{
	struct irq_remap_table *table;
	int index, c, alignment = 1;
	unsigned long flags;

	table = alloc_irq_table(iommu, devid, pdev);
	if (!table)
		return -ENODEV;

	if (align)
		alignment = roundup_pow_of_two(count);

	raw_spin_lock_irqsave(&table->lock, flags);

	/* Scan table for free entries */
	for (index = ALIGN(table->min_index, alignment), c = 0;
	     index < MAX_IRQS_PER_TABLE;) {
		if (!iommu->irte_ops->is_allocated(table, index)) {
			c += 1;
		} else {
			c     = 0;
			index = ALIGN(index + 1, alignment);
			continue;
		}

		if (c == count)	{
			for (; c != 0; --c)
				iommu->irte_ops->set_allocated(table, index - c + 1);

			index -= count - 1;
			goto out;
		}

		index++;
	}

	index = -ENOSPC;

out:
	raw_spin_unlock_irqrestore(&table->lock, flags);

	return index;
}

static int modify_irte_ga(struct amd_iommu *iommu, u16 devid, int index,
			  struct irte_ga *irte, struct amd_ir_data *data)
{
	bool ret;
	struct irq_remap_table *table;
	unsigned long flags;
	struct irte_ga *entry;

	table = get_irq_table(iommu, devid);
	if (!table)
		return -ENOMEM;

	raw_spin_lock_irqsave(&table->lock, flags);

	entry = (struct irte_ga *)table->table;
	entry = &entry[index];

	ret = cmpxchg_double(&entry->lo.val, &entry->hi.val,
			     entry->lo.val, entry->hi.val,
			     irte->lo.val, irte->hi.val);
	/*
	 * We use cmpxchg16 to atomically update the 128-bit IRTE,
	 * and it cannot be updated by the hardware or other processors
	 * behind us, so the return value of cmpxchg16 should be the
	 * same as the old value.
	 */
	WARN_ON(!ret);

	if (data)
		data->ref = entry;

	raw_spin_unlock_irqrestore(&table->lock, flags);

	iommu_flush_irt(iommu, devid);
	iommu_completion_wait(iommu);

	return 0;
}

static int modify_irte(struct amd_iommu *iommu,
		       u16 devid, int index, union irte *irte)
{
	struct irq_remap_table *table;
	unsigned long flags;

	table = get_irq_table(iommu, devid);
	if (!table)
		return -ENOMEM;

	raw_spin_lock_irqsave(&table->lock, flags);
	table->table[index] = irte->val;
	raw_spin_unlock_irqrestore(&table->lock, flags);

	iommu_flush_irt(iommu, devid);
	iommu_completion_wait(iommu);

	return 0;
}

static void free_irte(struct amd_iommu *iommu, u16 devid, int index)
{
	struct irq_remap_table *table;
	unsigned long flags;

	table = get_irq_table(iommu, devid);
	if (!table)
		return;

	raw_spin_lock_irqsave(&table->lock, flags);
	iommu->irte_ops->clear_allocated(table, index);
	raw_spin_unlock_irqrestore(&table->lock, flags);

	iommu_flush_irt(iommu, devid);
	iommu_completion_wait(iommu);
}

static void irte_prepare(void *entry,
			 u32 delivery_mode, bool dest_mode,
			 u8 vector, u32 dest_apicid, int devid)
{
	union irte *irte = (union irte *) entry;

	irte->val                = 0;
	irte->fields.vector      = vector;
	irte->fields.int_type    = delivery_mode;
	irte->fields.destination = dest_apicid;
	irte->fields.dm          = dest_mode;
	irte->fields.valid       = 1;
}

static void irte_ga_prepare(void *entry,
			    u32 delivery_mode, bool dest_mode,
			    u8 vector, u32 dest_apicid, int devid)
{
	struct irte_ga *irte = (struct irte_ga *) entry;

	irte->lo.val                      = 0;
	irte->hi.val                      = 0;
	irte->lo.fields_remap.int_type    = delivery_mode;
	irte->lo.fields_remap.dm          = dest_mode;
	irte->hi.fields.vector            = vector;
	irte->lo.fields_remap.destination = APICID_TO_IRTE_DEST_LO(dest_apicid);
	irte->hi.fields.destination       = APICID_TO_IRTE_DEST_HI(dest_apicid);
	irte->lo.fields_remap.valid       = 1;
}

static void irte_activate(struct amd_iommu *iommu, void *entry, u16 devid, u16 index)
{
	union irte *irte = (union irte *) entry;

	irte->fields.valid = 1;
	modify_irte(iommu, devid, index, irte);
}

static void irte_ga_activate(struct amd_iommu *iommu, void *entry, u16 devid, u16 index)
{
	struct irte_ga *irte = (struct irte_ga *) entry;

	irte->lo.fields_remap.valid = 1;
	modify_irte_ga(iommu, devid, index, irte, NULL);
}

static void irte_deactivate(struct amd_iommu *iommu, void *entry, u16 devid, u16 index)
{
	union irte *irte = (union irte *) entry;

	irte->fields.valid = 0;
	modify_irte(iommu, devid, index, irte);
}

static void irte_ga_deactivate(struct amd_iommu *iommu, void *entry, u16 devid, u16 index)
{
	struct irte_ga *irte = (struct irte_ga *) entry;

	irte->lo.fields_remap.valid = 0;
	modify_irte_ga(iommu, devid, index, irte, NULL);
}

static void irte_set_affinity(struct amd_iommu *iommu, void *entry, u16 devid, u16 index,
			      u8 vector, u32 dest_apicid)
{
	union irte *irte = (union irte *) entry;

	irte->fields.vector = vector;
	irte->fields.destination = dest_apicid;
	modify_irte(iommu, devid, index, irte);
}

static void irte_ga_set_affinity(struct amd_iommu *iommu, void *entry, u16 devid, u16 index,
				 u8 vector, u32 dest_apicid)
{
	struct irte_ga *irte = (struct irte_ga *) entry;

	if (!irte->lo.fields_remap.guest_mode) {
		irte->hi.fields.vector = vector;
		irte->lo.fields_remap.destination =
					APICID_TO_IRTE_DEST_LO(dest_apicid);
		irte->hi.fields.destination =
					APICID_TO_IRTE_DEST_HI(dest_apicid);
		modify_irte_ga(iommu, devid, index, irte, NULL);
	}
}

#define IRTE_ALLOCATED (~1U)
static void irte_set_allocated(struct irq_remap_table *table, int index)
{
	table->table[index] = IRTE_ALLOCATED;
}

static void irte_ga_set_allocated(struct irq_remap_table *table, int index)
{
	struct irte_ga *ptr = (struct irte_ga *)table->table;
	struct irte_ga *irte = &ptr[index];

	memset(&irte->lo.val, 0, sizeof(u64));
	memset(&irte->hi.val, 0, sizeof(u64));
	irte->hi.fields.vector = 0xff;
}

static bool irte_is_allocated(struct irq_remap_table *table, int index)
{
	union irte *ptr = (union irte *)table->table;
	union irte *irte = &ptr[index];

	return irte->val != 0;
}

static bool irte_ga_is_allocated(struct irq_remap_table *table, int index)
{
	struct irte_ga *ptr = (struct irte_ga *)table->table;
	struct irte_ga *irte = &ptr[index];

	return irte->hi.fields.vector != 0;
}

static void irte_clear_allocated(struct irq_remap_table *table, int index)
{
	table->table[index] = 0;
}

static void irte_ga_clear_allocated(struct irq_remap_table *table, int index)
{
	struct irte_ga *ptr = (struct irte_ga *)table->table;
	struct irte_ga *irte = &ptr[index];

	memset(&irte->lo.val, 0, sizeof(u64));
	memset(&irte->hi.val, 0, sizeof(u64));
}

static int get_devid(struct irq_alloc_info *info)
{
	switch (info->type) {
	case X86_IRQ_ALLOC_TYPE_IOAPIC:
		return get_ioapic_devid(info->devid);
	case X86_IRQ_ALLOC_TYPE_HPET:
		return get_hpet_devid(info->devid);
	case X86_IRQ_ALLOC_TYPE_PCI_MSI:
	case X86_IRQ_ALLOC_TYPE_PCI_MSIX:
		return get_device_sbdf_id(msi_desc_to_dev(info->desc));
	default:
		WARN_ON_ONCE(1);
		return -1;
	}
}

struct irq_remap_ops amd_iommu_irq_ops = {
	.prepare		= amd_iommu_prepare,
	.enable			= amd_iommu_enable,
	.disable		= amd_iommu_disable,
	.reenable		= amd_iommu_reenable,
	.enable_faulting	= amd_iommu_enable_faulting,
};

static void fill_msi_msg(struct msi_msg *msg, u32 index)
{
	msg->data = index;
	msg->address_lo = 0;
	msg->arch_addr_lo.base_address = X86_MSI_BASE_ADDRESS_LOW;
	msg->address_hi = X86_MSI_BASE_ADDRESS_HIGH;
}

static void irq_remapping_prepare_irte(struct amd_ir_data *data,
				       struct irq_cfg *irq_cfg,
				       struct irq_alloc_info *info,
				       int devid, int index, int sub_handle)
{
	struct irq_2_irte *irte_info = &data->irq_2_irte;
	struct amd_iommu *iommu = data->iommu;

	if (!iommu)
		return;

	data->irq_2_irte.devid = devid;
	data->irq_2_irte.index = index + sub_handle;
	iommu->irte_ops->prepare(data->entry, apic->delivery_mode,
				 apic->dest_mode_logical, irq_cfg->vector,
				 irq_cfg->dest_apicid, devid);

	switch (info->type) {
	case X86_IRQ_ALLOC_TYPE_IOAPIC:
	case X86_IRQ_ALLOC_TYPE_HPET:
	case X86_IRQ_ALLOC_TYPE_PCI_MSI:
	case X86_IRQ_ALLOC_TYPE_PCI_MSIX:
		fill_msi_msg(&data->msi_entry, irte_info->index);
		break;

	default:
		BUG_ON(1);
		break;
	}
}

struct amd_irte_ops irte_32_ops = {
	.prepare = irte_prepare,
	.activate = irte_activate,
	.deactivate = irte_deactivate,
	.set_affinity = irte_set_affinity,
	.set_allocated = irte_set_allocated,
	.is_allocated = irte_is_allocated,
	.clear_allocated = irte_clear_allocated,
};

struct amd_irte_ops irte_128_ops = {
	.prepare = irte_ga_prepare,
	.activate = irte_ga_activate,
	.deactivate = irte_ga_deactivate,
	.set_affinity = irte_ga_set_affinity,
	.set_allocated = irte_ga_set_allocated,
	.is_allocated = irte_ga_is_allocated,
	.clear_allocated = irte_ga_clear_allocated,
};

static int irq_remapping_alloc(struct irq_domain *domain, unsigned int virq,
			       unsigned int nr_irqs, void *arg)
{
	struct irq_alloc_info *info = arg;
	struct irq_data *irq_data;
	struct amd_ir_data *data = NULL;
	struct amd_iommu *iommu;
	struct irq_cfg *cfg;
	int i, ret, devid, seg, sbdf;
	int index;

	if (!info)
		return -EINVAL;
	if (nr_irqs > 1 && info->type != X86_IRQ_ALLOC_TYPE_PCI_MSI &&
	    info->type != X86_IRQ_ALLOC_TYPE_PCI_MSIX)
		return -EINVAL;

	/*
	 * With IRQ remapping enabled, don't need contiguous CPU vectors
	 * to support multiple MSI interrupts.
	 */
	if (info->type == X86_IRQ_ALLOC_TYPE_PCI_MSI)
		info->flags &= ~X86_IRQ_ALLOC_CONTIGUOUS_VECTORS;

	sbdf = get_devid(info);
	if (sbdf < 0)
		return -EINVAL;

	seg = PCI_SBDF_TO_SEGID(sbdf);
	devid = PCI_SBDF_TO_DEVID(sbdf);
	iommu = __rlookup_amd_iommu(seg, devid);
	if (!iommu)
		return -EINVAL;

	ret = irq_domain_alloc_irqs_parent(domain, virq, nr_irqs, arg);
	if (ret < 0)
		return ret;

	if (info->type == X86_IRQ_ALLOC_TYPE_IOAPIC) {
		struct irq_remap_table *table;

		table = alloc_irq_table(iommu, devid, NULL);
		if (table) {
			if (!table->min_index) {
				/*
				 * Keep the first 32 indexes free for IOAPIC
				 * interrupts.
				 */
				table->min_index = 32;
				for (i = 0; i < 32; ++i)
					iommu->irte_ops->set_allocated(table, i);
			}
			WARN_ON(table->min_index != 32);
			index = info->ioapic.pin;
		} else {
			index = -ENOMEM;
		}
	} else if (info->type == X86_IRQ_ALLOC_TYPE_PCI_MSI ||
		   info->type == X86_IRQ_ALLOC_TYPE_PCI_MSIX) {
		bool align = (info->type == X86_IRQ_ALLOC_TYPE_PCI_MSI);

		index = alloc_irq_index(iommu, devid, nr_irqs, align,
					msi_desc_to_pci_dev(info->desc));
	} else {
		index = alloc_irq_index(iommu, devid, nr_irqs, false, NULL);
	}

	if (index < 0) {
		pr_warn("Failed to allocate IRTE\n");
		ret = index;
		goto out_free_parent;
	}

	for (i = 0; i < nr_irqs; i++) {
		irq_data = irq_domain_get_irq_data(domain, virq + i);
		cfg = irq_data ? irqd_cfg(irq_data) : NULL;
		if (!cfg) {
			ret = -EINVAL;
			goto out_free_data;
		}

		ret = -ENOMEM;
		data = kzalloc(sizeof(*data), GFP_KERNEL);
		if (!data)
			goto out_free_data;

		if (!AMD_IOMMU_GUEST_IR_GA(amd_iommu_guest_ir))
			data->entry = kzalloc(sizeof(union irte), GFP_KERNEL);
		else
			data->entry = kzalloc(sizeof(struct irte_ga),
						     GFP_KERNEL);
		if (!data->entry) {
			kfree(data);
			goto out_free_data;
		}

		data->iommu = iommu;
		irq_data->hwirq = (devid << 16) + i;
		irq_data->chip_data = data;
		irq_data->chip = &amd_ir_chip;
		irq_remapping_prepare_irte(data, cfg, info, devid, index, i);
		irq_set_status_flags(virq + i, IRQ_MOVE_PCNTXT);
	}

	return 0;

out_free_data:
	for (i--; i >= 0; i--) {
		irq_data = irq_domain_get_irq_data(domain, virq + i);
		if (irq_data)
			kfree(irq_data->chip_data);
	}
	for (i = 0; i < nr_irqs; i++)
		free_irte(iommu, devid, index + i);
out_free_parent:
	irq_domain_free_irqs_common(domain, virq, nr_irqs);
	return ret;
}

static void irq_remapping_free(struct irq_domain *domain, unsigned int virq,
			       unsigned int nr_irqs)
{
	struct irq_2_irte *irte_info;
	struct irq_data *irq_data;
	struct amd_ir_data *data;
	int i;

	for (i = 0; i < nr_irqs; i++) {
		irq_data = irq_domain_get_irq_data(domain, virq  + i);
		if (irq_data && irq_data->chip_data) {
			data = irq_data->chip_data;
			irte_info = &data->irq_2_irte;
			free_irte(data->iommu, irte_info->devid, irte_info->index);
			kfree(data->entry);
			kfree(data);
		}
	}
	irq_domain_free_irqs_common(domain, virq, nr_irqs);
}

static void amd_ir_update_irte(struct irq_data *irqd, struct amd_iommu *iommu,
			       struct amd_ir_data *ir_data,
			       struct irq_2_irte *irte_info,
			       struct irq_cfg *cfg);

static int irq_remapping_activate(struct irq_domain *domain,
				  struct irq_data *irq_data, bool reserve)
{
	struct amd_ir_data *data = irq_data->chip_data;
	struct irq_2_irte *irte_info = &data->irq_2_irte;
	struct amd_iommu *iommu = data->iommu;
	struct irq_cfg *cfg = irqd_cfg(irq_data);

	if (!iommu)
		return 0;

	iommu->irte_ops->activate(iommu, data->entry, irte_info->devid,
				  irte_info->index);
	amd_ir_update_irte(irq_data, iommu, data, irte_info, cfg);
	return 0;
}

static void irq_remapping_deactivate(struct irq_domain *domain,
				     struct irq_data *irq_data)
{
	struct amd_ir_data *data = irq_data->chip_data;
	struct irq_2_irte *irte_info = &data->irq_2_irte;
	struct amd_iommu *iommu = data->iommu;

	if (iommu)
		iommu->irte_ops->deactivate(iommu, data->entry, irte_info->devid,
					    irte_info->index);
}

static int irq_remapping_select(struct irq_domain *d, struct irq_fwspec *fwspec,
				enum irq_domain_bus_token bus_token)
{
	struct amd_iommu *iommu;
	int devid = -1;

	if (!amd_iommu_irq_remap)
		return 0;

	if (x86_fwspec_is_ioapic(fwspec))
		devid = get_ioapic_devid(fwspec->param[0]);
	else if (x86_fwspec_is_hpet(fwspec))
		devid = get_hpet_devid(fwspec->param[0]);

	if (devid < 0)
		return 0;
	iommu = __rlookup_amd_iommu((devid >> 16), (devid & 0xffff));

	return iommu && iommu->ir_domain == d;
}

static const struct irq_domain_ops amd_ir_domain_ops = {
	.select = irq_remapping_select,
	.alloc = irq_remapping_alloc,
	.free = irq_remapping_free,
	.activate = irq_remapping_activate,
	.deactivate = irq_remapping_deactivate,
};

int amd_iommu_activate_guest_mode(void *data)
{
	struct amd_ir_data *ir_data = (struct amd_ir_data *)data;
	struct irte_ga *entry = (struct irte_ga *) ir_data->entry;
	u64 valid;

	if (!AMD_IOMMU_GUEST_IR_VAPIC(amd_iommu_guest_ir) ||
	    !entry || entry->lo.fields_vapic.guest_mode)
		return 0;

	valid = entry->lo.fields_vapic.valid;

	entry->lo.val = 0;
	entry->hi.val = 0;

	entry->lo.fields_vapic.valid       = valid;
	entry->lo.fields_vapic.guest_mode  = 1;
	entry->lo.fields_vapic.ga_log_intr = 1;
	entry->hi.fields.ga_root_ptr       = ir_data->ga_root_ptr;
	entry->hi.fields.vector            = ir_data->ga_vector;
	entry->lo.fields_vapic.ga_tag      = ir_data->ga_tag;

	return modify_irte_ga(ir_data->iommu, ir_data->irq_2_irte.devid,
			      ir_data->irq_2_irte.index, entry, ir_data);
}
EXPORT_SYMBOL(amd_iommu_activate_guest_mode);

int amd_iommu_deactivate_guest_mode(void *data)
{
	struct amd_ir_data *ir_data = (struct amd_ir_data *)data;
	struct irte_ga *entry = (struct irte_ga *) ir_data->entry;
	struct irq_cfg *cfg = ir_data->cfg;
	u64 valid;

	if (!AMD_IOMMU_GUEST_IR_VAPIC(amd_iommu_guest_ir) ||
	    !entry || !entry->lo.fields_vapic.guest_mode)
		return 0;

	valid = entry->lo.fields_remap.valid;

	entry->lo.val = 0;
	entry->hi.val = 0;

	entry->lo.fields_remap.valid       = valid;
	entry->lo.fields_remap.dm          = apic->dest_mode_logical;
	entry->lo.fields_remap.int_type    = apic->delivery_mode;
	entry->hi.fields.vector            = cfg->vector;
	entry->lo.fields_remap.destination =
				APICID_TO_IRTE_DEST_LO(cfg->dest_apicid);
	entry->hi.fields.destination =
				APICID_TO_IRTE_DEST_HI(cfg->dest_apicid);

	return modify_irte_ga(ir_data->iommu, ir_data->irq_2_irte.devid,
			      ir_data->irq_2_irte.index, entry, ir_data);
}
EXPORT_SYMBOL(amd_iommu_deactivate_guest_mode);

static int amd_ir_set_vcpu_affinity(struct irq_data *data, void *vcpu_info)
{
	int ret;
	struct amd_iommu_pi_data *pi_data = vcpu_info;
	struct vcpu_data *vcpu_pi_info = pi_data->vcpu_data;
	struct amd_ir_data *ir_data = data->chip_data;
	struct irq_2_irte *irte_info = &ir_data->irq_2_irte;
	struct iommu_dev_data *dev_data;

	if (ir_data->iommu == NULL)
		return -EINVAL;

	dev_data = search_dev_data(ir_data->iommu, irte_info->devid);

	/* Note:
	 * This device has never been set up for guest mode.
	 * we should not modify the IRTE
	 */
	if (!dev_data || !dev_data->use_vapic)
		return 0;

	ir_data->cfg = irqd_cfg(data);
	pi_data->ir_data = ir_data;

	/* Note:
	 * SVM tries to set up for VAPIC mode, but we are in
	 * legacy mode. So, we force legacy mode instead.
	 */
	if (!AMD_IOMMU_GUEST_IR_VAPIC(amd_iommu_guest_ir)) {
		pr_debug("%s: Fall back to using intr legacy remap\n",
			 __func__);
		pi_data->is_guest_mode = false;
	}

	pi_data->prev_ga_tag = ir_data->cached_ga_tag;
	if (pi_data->is_guest_mode) {
		ir_data->ga_root_ptr = (pi_data->base >> 12);
		ir_data->ga_vector = vcpu_pi_info->vector;
		ir_data->ga_tag = pi_data->ga_tag;
		ret = amd_iommu_activate_guest_mode(ir_data);
		if (!ret)
			ir_data->cached_ga_tag = pi_data->ga_tag;
	} else {
		ret = amd_iommu_deactivate_guest_mode(ir_data);

		/*
		 * This communicates the ga_tag back to the caller
		 * so that it can do all the necessary clean up.
		 */
		if (!ret)
			ir_data->cached_ga_tag = 0;
	}

	return ret;
}


static void amd_ir_update_irte(struct irq_data *irqd, struct amd_iommu *iommu,
			       struct amd_ir_data *ir_data,
			       struct irq_2_irte *irte_info,
			       struct irq_cfg *cfg)
{

	/*
	 * Atomically updates the IRTE with the new destination, vector
	 * and flushes the interrupt entry cache.
	 */
	iommu->irte_ops->set_affinity(iommu, ir_data->entry, irte_info->devid,
				      irte_info->index, cfg->vector,
				      cfg->dest_apicid);
}

static int amd_ir_set_affinity(struct irq_data *data,
			       const struct cpumask *mask, bool force)
{
	struct amd_ir_data *ir_data = data->chip_data;
	struct irq_2_irte *irte_info = &ir_data->irq_2_irte;
	struct irq_cfg *cfg = irqd_cfg(data);
	struct irq_data *parent = data->parent_data;
	struct amd_iommu *iommu = ir_data->iommu;
	int ret;

	if (!iommu)
		return -ENODEV;

	ret = parent->chip->irq_set_affinity(parent, mask, force);
	if (ret < 0 || ret == IRQ_SET_MASK_OK_DONE)
		return ret;

	amd_ir_update_irte(data, iommu, ir_data, irte_info, cfg);
	/*
	 * After this point, all the interrupts will start arriving
	 * at the new destination. So, time to cleanup the previous
	 * vector allocation.
	 */
	send_cleanup_vector(cfg);

	return IRQ_SET_MASK_OK_DONE;
}

static void ir_compose_msi_msg(struct irq_data *irq_data, struct msi_msg *msg)
{
	struct amd_ir_data *ir_data = irq_data->chip_data;

	*msg = ir_data->msi_entry;
}

static struct irq_chip amd_ir_chip = {
	.name			= "AMD-IR",
	.irq_ack		= apic_ack_irq,
	.irq_set_affinity	= amd_ir_set_affinity,
	.irq_set_vcpu_affinity	= amd_ir_set_vcpu_affinity,
	.irq_compose_msi_msg	= ir_compose_msi_msg,
};

int amd_iommu_create_irq_domain(struct amd_iommu *iommu)
{
	struct fwnode_handle *fn;

	fn = irq_domain_alloc_named_id_fwnode("AMD-IR", iommu->index);
	if (!fn)
		return -ENOMEM;
	iommu->ir_domain = irq_domain_create_tree(fn, &amd_ir_domain_ops, iommu);
	if (!iommu->ir_domain) {
		irq_domain_free_fwnode(fn);
		return -ENOMEM;
	}

	iommu->ir_domain->parent = arch_get_ir_parent_domain();
	iommu->msi_domain = arch_create_remap_msi_irq_domain(iommu->ir_domain,
							     "AMD-IR-MSI",
							     iommu->index);
	return 0;
}

int amd_iommu_update_ga(int cpu, bool is_run, void *data)
{
	unsigned long flags;
	struct amd_iommu *iommu;
	struct irq_remap_table *table;
	struct amd_ir_data *ir_data = (struct amd_ir_data *)data;
	int devid = ir_data->irq_2_irte.devid;
	struct irte_ga *entry = (struct irte_ga *) ir_data->entry;
	struct irte_ga *ref = (struct irte_ga *) ir_data->ref;

	if (!AMD_IOMMU_GUEST_IR_VAPIC(amd_iommu_guest_ir) ||
	    !ref || !entry || !entry->lo.fields_vapic.guest_mode)
		return 0;

	iommu = ir_data->iommu;
	if (!iommu)
		return -ENODEV;

	table = get_irq_table(iommu, devid);
	if (!table)
		return -ENODEV;

	raw_spin_lock_irqsave(&table->lock, flags);

	if (ref->lo.fields_vapic.guest_mode) {
		if (cpu >= 0) {
			ref->lo.fields_vapic.destination =
						APICID_TO_IRTE_DEST_LO(cpu);
			ref->hi.fields.destination =
						APICID_TO_IRTE_DEST_HI(cpu);
		}
		ref->lo.fields_vapic.is_run = is_run;
		barrier();
	}

	raw_spin_unlock_irqrestore(&table->lock, flags);

	iommu_flush_irt(iommu, devid);
	iommu_completion_wait(iommu);
	return 0;
}
EXPORT_SYMBOL(amd_iommu_update_ga);
#endif
