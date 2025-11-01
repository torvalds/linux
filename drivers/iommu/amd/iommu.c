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
#include <linux/string_choices.h>
#include <linux/debugfs.h>
#include <linux/scatterlist.h>
#include <linux/dma-map-ops.h>
#include <linux/dma-direct.h>
#include <linux/idr.h>
#include <linux/iommu-helper.h>
#include <linux/delay.h>
#include <linux/amd-iommu.h>
#include <linux/notifier.h>
#include <linux/export.h>
#include <linux/irq.h>
#include <linux/irqchip/irq-msi-lib.h>
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
#include <uapi/linux/iommufd.h>

#include "amd_iommu.h"
#include "../dma-iommu.h"
#include "../irq_remapping.h"
#include "../iommu-pages.h"

#define CMD_SET_TYPE(cmd, t) ((cmd)->data[1] |= ((t) << 28))

/* Reserved IOVA ranges */
#define MSI_RANGE_START		(0xfee00000)
#define MSI_RANGE_END		(0xfeefffff)
#define HT_RANGE_START		(0xfd00000000ULL)
#define HT_RANGE_END		(0xffffffffffULL)

LIST_HEAD(ioapic_map);
LIST_HEAD(hpet_map);
LIST_HEAD(acpihid_map);

const struct iommu_ops amd_iommu_ops;
static const struct iommu_dirty_ops amd_dirty_ops;

int amd_iommu_max_glx_val = -1;

/*
 * AMD IOMMU allows up to 2^16 different protection domains. This is a bitmap
 * to know which ones are already in use.
 */
DEFINE_IDA(pdom_ids);

static int amd_iommu_attach_device(struct iommu_domain *dom,
				   struct device *dev);

static void set_dte_entry(struct amd_iommu *iommu,
			  struct iommu_dev_data *dev_data);

static void iommu_flush_dte_sync(struct amd_iommu *iommu, u16 devid);

static struct iommu_dev_data *find_dev_data(struct amd_iommu *iommu, u16 devid);

/****************************************************************************
 *
 * Helper functions
 *
 ****************************************************************************/

static __always_inline void amd_iommu_atomic128_set(__int128 *ptr, __int128 val)
{
	/*
	 * Note:
	 * We use arch_cmpxchg128_local() because:
	 * - Need cmpxchg16b instruction mainly for 128-bit store to DTE
	 *   (not necessary for cmpxchg since this function is already
	 *   protected by a spin_lock for this DTE).
	 * - Neither need LOCK_PREFIX nor try loop because of the spin_lock.
	 */
	arch_cmpxchg128_local(ptr, *ptr, val);
}

static void write_dte_upper128(struct dev_table_entry *ptr, struct dev_table_entry *new)
{
	struct dev_table_entry old;

	old.data128[1] = ptr->data128[1];
	/*
	 * Preserve DTE_DATA2_INTR_MASK. This needs to be
	 * done here since it requires to be inside
	 * spin_lock(&dev_data->dte_lock) context.
	 */
	new->data[2] &= ~DTE_DATA2_INTR_MASK;
	new->data[2] |= old.data[2] & DTE_DATA2_INTR_MASK;

	amd_iommu_atomic128_set(&ptr->data128[1], new->data128[1]);
}

static void write_dte_lower128(struct dev_table_entry *ptr, struct dev_table_entry *new)
{
	amd_iommu_atomic128_set(&ptr->data128[0], new->data128[0]);
}

/*
 * Note:
 * IOMMU reads the entire Device Table entry in a single 256-bit transaction
 * but the driver is programming DTE using 2 128-bit cmpxchg. So, the driver
 * need to ensure the following:
 *   - DTE[V|GV] bit is being written last when setting.
 *   - DTE[V|GV] bit is being written first when clearing.
 *
 * This function is used only by code, which updates DMA translation part of the DTE.
 * So, only consider control bits related to DMA when updating the entry.
 */
static void update_dte256(struct amd_iommu *iommu, struct iommu_dev_data *dev_data,
			  struct dev_table_entry *new)
{
	unsigned long flags;
	struct dev_table_entry *dev_table = get_dev_table(iommu);
	struct dev_table_entry *ptr = &dev_table[dev_data->devid];

	spin_lock_irqsave(&dev_data->dte_lock, flags);

	if (!(ptr->data[0] & DTE_FLAG_V)) {
		/* Existing DTE is not valid. */
		write_dte_upper128(ptr, new);
		write_dte_lower128(ptr, new);
		iommu_flush_dte_sync(iommu, dev_data->devid);
	} else if (!(new->data[0] & DTE_FLAG_V)) {
		/* Existing DTE is valid. New DTE is not valid.  */
		write_dte_lower128(ptr, new);
		write_dte_upper128(ptr, new);
		iommu_flush_dte_sync(iommu, dev_data->devid);
	} else if (!FIELD_GET(DTE_FLAG_GV, ptr->data[0])) {
		/*
		 * Both DTEs are valid.
		 * Existing DTE has no guest page table.
		 */
		write_dte_upper128(ptr, new);
		write_dte_lower128(ptr, new);
		iommu_flush_dte_sync(iommu, dev_data->devid);
	} else if (!FIELD_GET(DTE_FLAG_GV, new->data[0])) {
		/*
		 * Both DTEs are valid.
		 * Existing DTE has guest page table,
		 * new DTE has no guest page table,
		 */
		write_dte_lower128(ptr, new);
		write_dte_upper128(ptr, new);
		iommu_flush_dte_sync(iommu, dev_data->devid);
	} else if (FIELD_GET(DTE_GPT_LEVEL_MASK, ptr->data[2]) !=
		   FIELD_GET(DTE_GPT_LEVEL_MASK, new->data[2])) {
		/*
		 * Both DTEs are valid and have guest page table,
		 * but have different number of levels. So, we need
		 * to upadte both upper and lower 128-bit value, which
		 * require disabling and flushing.
		 */
		struct dev_table_entry clear = {};

		/* First disable DTE */
		write_dte_lower128(ptr, &clear);
		iommu_flush_dte_sync(iommu, dev_data->devid);

		/* Then update DTE */
		write_dte_upper128(ptr, new);
		write_dte_lower128(ptr, new);
		iommu_flush_dte_sync(iommu, dev_data->devid);
	} else {
		/*
		 * Both DTEs are valid and have guest page table,
		 * and same number of levels. We just need to only
		 * update the lower 128-bit. So no need to disable DTE.
		 */
		write_dte_lower128(ptr, new);
	}

	spin_unlock_irqrestore(&dev_data->dte_lock, flags);
}

static void get_dte256(struct amd_iommu *iommu, struct iommu_dev_data *dev_data,
		      struct dev_table_entry *dte)
{
	unsigned long flags;
	struct dev_table_entry *ptr;
	struct dev_table_entry *dev_table = get_dev_table(iommu);

	ptr = &dev_table[dev_data->devid];

	spin_lock_irqsave(&dev_data->dte_lock, flags);
	dte->data128[0] = ptr->data128[0];
	dte->data128[1] = ptr->data128[1];
	spin_unlock_irqrestore(&dev_data->dte_lock, flags);
}

static inline bool pdom_is_v2_pgtbl_mode(struct protection_domain *pdom)
{
	return (pdom && (pdom->pd_mode == PD_MODE_V2));
}

static inline bool pdom_is_in_pt_mode(struct protection_domain *pdom)
{
	return (pdom->domain.type == IOMMU_DOMAIN_IDENTITY);
}

/*
 * We cannot support PASID w/ existing v1 page table in the same domain
 * since it will be nested. However, existing domain w/ v2 page table
 * or passthrough mode can be used for PASID.
 */
static inline bool pdom_is_sva_capable(struct protection_domain *pdom)
{
	return pdom_is_v2_pgtbl_mode(pdom) || pdom_is_in_pt_mode(pdom);
}

static inline int get_acpihid_device_id(struct device *dev,
					struct acpihid_map_entry **entry)
{
	struct acpi_device *adev = ACPI_COMPANION(dev);
	struct acpihid_map_entry *p, *p1 = NULL;
	int hid_count = 0;
	bool fw_bug;

	if (!adev)
		return -ENODEV;

	list_for_each_entry(p, &acpihid_map, list) {
		if (acpi_dev_hid_uid_match(adev, p->hid,
					   p->uid[0] ? p->uid : NULL)) {
			p1 = p;
			fw_bug = false;
			hid_count = 1;
			break;
		}

		/*
		 * Count HID matches w/o UID, raise FW_BUG but allow exactly one match
		 */
		if (acpi_dev_hid_match(adev, p->hid)) {
			p1 = p;
			hid_count++;
			fw_bug = true;
		}
	}

	if (!p1)
		return -EINVAL;
	if (fw_bug)
		dev_err_once(dev, FW_BUG "No ACPI device matched UID, but %d device%s matched HID.\n",
			     hid_count, str_plural(hid_count));
	if (hid_count > 1)
		return -EINVAL;
	if (entry)
		*entry = p1;

	return p1->devid;
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

static struct iommu_dev_data *alloc_dev_data(struct amd_iommu *iommu, u16 devid)
{
	struct iommu_dev_data *dev_data;
	struct amd_iommu_pci_seg *pci_seg = iommu->pci_seg;

	dev_data = kzalloc(sizeof(*dev_data), GFP_KERNEL);
	if (!dev_data)
		return NULL;

	mutex_init(&dev_data->mutex);
	spin_lock_init(&dev_data->dte_lock);
	dev_data->devid = devid;
	ratelimit_default_init(&dev_data->rs);

	llist_add(&dev_data->dev_data_list, &pci_seg->dev_data_list);
	return dev_data;
}

struct iommu_dev_data *search_dev_data(struct amd_iommu *iommu, u16 devid)
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
	struct dev_table_entry new;
	struct amd_iommu *iommu;
	struct iommu_dev_data *dev_data, *alias_data;
	u16 devid = pci_dev_id(pdev);
	int ret = 0;

	if (devid == alias)
		return 0;

	iommu = rlookup_amd_iommu(&pdev->dev);
	if (!iommu)
		return 0;

	/* Copy the data from pdev */
	dev_data = dev_iommu_priv_get(&pdev->dev);
	if (!dev_data) {
		pr_err("%s : Failed to get dev_data for 0x%x\n", __func__, devid);
		ret = -EINVAL;
		goto out;
	}
	get_dte256(iommu, dev_data, &new);

	/* Setup alias */
	alias_data = find_dev_data(iommu, alias);
	if (!alias_data) {
		pr_err("%s : Failed to get alias dev_data for 0x%x\n", __func__, alias);
		ret = -EINVAL;
		goto out;
	}
	update_dte256(iommu, alias_data, &new);

	amd_iommu_set_rlookup_table(iommu, alias);
out:
	return ret;
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

static inline bool pdev_pasid_supported(struct iommu_dev_data *dev_data)
{
	return (dev_data->flags & AMD_IOMMU_DEVICE_FLAG_PASID_SUP);
}

static u32 pdev_get_caps(struct pci_dev *pdev)
{
	int features;
	u32 flags = 0;

	if (pci_ats_supported(pdev))
		flags |= AMD_IOMMU_DEVICE_FLAG_ATS_SUP;

	if (pci_pri_supported(pdev))
		flags |= AMD_IOMMU_DEVICE_FLAG_PRI_SUP;

	features = pci_pasid_features(pdev);
	if (features >= 0) {
		flags |= AMD_IOMMU_DEVICE_FLAG_PASID_SUP;

		if (features & PCI_PASID_CAP_EXEC)
			flags |= AMD_IOMMU_DEVICE_FLAG_EXEC_SUP;

		if (features & PCI_PASID_CAP_PRIV)
			flags |= AMD_IOMMU_DEVICE_FLAG_PRIV_SUP;
	}

	return flags;
}

static inline int pdev_enable_cap_ats(struct pci_dev *pdev)
{
	struct iommu_dev_data *dev_data = dev_iommu_priv_get(&pdev->dev);
	int ret = -EINVAL;

	if (dev_data->ats_enabled)
		return 0;

	if (amd_iommu_iotlb_sup &&
	    (dev_data->flags & AMD_IOMMU_DEVICE_FLAG_ATS_SUP)) {
		ret = pci_enable_ats(pdev, PAGE_SHIFT);
		if (!ret) {
			dev_data->ats_enabled = 1;
			dev_data->ats_qdep    = pci_ats_queue_depth(pdev);
		}
	}

	return ret;
}

static inline void pdev_disable_cap_ats(struct pci_dev *pdev)
{
	struct iommu_dev_data *dev_data = dev_iommu_priv_get(&pdev->dev);

	if (dev_data->ats_enabled) {
		pci_disable_ats(pdev);
		dev_data->ats_enabled = 0;
	}
}

static inline int pdev_enable_cap_pri(struct pci_dev *pdev)
{
	struct iommu_dev_data *dev_data = dev_iommu_priv_get(&pdev->dev);
	int ret = -EINVAL;

	if (dev_data->pri_enabled)
		return 0;

	if (!dev_data->ats_enabled)
		return 0;

	if (dev_data->flags & AMD_IOMMU_DEVICE_FLAG_PRI_SUP) {
		/*
		 * First reset the PRI state of the device.
		 * FIXME: Hardcode number of outstanding requests for now
		 */
		if (!pci_reset_pri(pdev) && !pci_enable_pri(pdev, 32)) {
			dev_data->pri_enabled = 1;
			dev_data->pri_tlp     = pci_prg_resp_pasid_required(pdev);

			ret = 0;
		}
	}

	return ret;
}

static inline void pdev_disable_cap_pri(struct pci_dev *pdev)
{
	struct iommu_dev_data *dev_data = dev_iommu_priv_get(&pdev->dev);

	if (dev_data->pri_enabled) {
		pci_disable_pri(pdev);
		dev_data->pri_enabled = 0;
	}
}

static inline int pdev_enable_cap_pasid(struct pci_dev *pdev)
{
	struct iommu_dev_data *dev_data = dev_iommu_priv_get(&pdev->dev);
	int ret = -EINVAL;

	if (dev_data->pasid_enabled)
		return 0;

	if (dev_data->flags & AMD_IOMMU_DEVICE_FLAG_PASID_SUP) {
		/* Only allow access to user-accessible pages */
		ret = pci_enable_pasid(pdev, 0);
		if (!ret)
			dev_data->pasid_enabled = 1;
	}

	return ret;
}

static inline void pdev_disable_cap_pasid(struct pci_dev *pdev)
{
	struct iommu_dev_data *dev_data = dev_iommu_priv_get(&pdev->dev);

	if (dev_data->pasid_enabled) {
		pci_disable_pasid(pdev);
		dev_data->pasid_enabled = 0;
	}
}

static void pdev_enable_caps(struct pci_dev *pdev)
{
	pdev_enable_cap_pasid(pdev);
	pdev_enable_cap_ats(pdev);
	pdev_enable_cap_pri(pdev);
}

static void pdev_disable_caps(struct pci_dev *pdev)
{
	pdev_disable_cap_ats(pdev);
	pdev_disable_cap_pasid(pdev);
	pdev_disable_cap_pri(pdev);
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

	/*
	 * The dev_iommu_priv_set() needes to be called before setup_aliases.
	 * Otherwise, subsequent call to dev_iommu_priv_get() will fail.
	 */
	dev_iommu_priv_set(dev, dev_data);
	setup_aliases(iommu, dev);

	/*
	 * By default we use passthrough mode for IOMMUv2 capable device.
	 * But if amd_iommu=force_isolation is set (e.g. to debug DMA to
	 * invalid address), we ignore the capability for the device so
	 * it'll be forced to go into translation mode.
	 */
	if ((iommu_default_passthrough() || !amd_iommu_force_isolation) &&
	    dev_is_pci(dev) && amd_iommu_gt_ppr_supported()) {
		dev_data->flags = pdev_get_caps(to_pci_dev(dev));
	}

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


/****************************************************************************
 *
 * Interrupt handling functions
 *
 ****************************************************************************/

static void dump_dte_entry(struct amd_iommu *iommu, u16 devid)
{
	int i;
	struct dev_table_entry dte;
	struct iommu_dev_data *dev_data = find_dev_data(iommu, devid);

	get_dte256(iommu, dev_data, &dte);

	for (i = 0; i < 4; ++i)
		pr_err("DTE[%d]: %016llx\n", i, dte.data[i]);
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
	u64 address, ctrl;
	u32 pasid;

retry:
	type    = (event[1] >> EVENT_TYPE_SHIFT)  & EVENT_TYPE_MASK;
	devid   = (event[0] >> EVENT_DEVID_SHIFT) & EVENT_DEVID_MASK;
	pasid   = (event[0] & EVENT_DOMID_MASK_HI) |
		  (event[1] & EVENT_DOMID_MASK_LO);
	flags   = (event[1] >> EVENT_FLAGS_SHIFT) & EVENT_FLAGS_MASK;
	address = (u64)(((u64)event[3]) << 32) | event[2];
	ctrl    = readq(iommu->mmio_base + MMIO_CONTROL_OFFSET);

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
		dev_err(dev, "Control Reg : 0x%llx\n", ctrl);
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

	/*
	 * To detect the hardware errata 732 we need to clear the
	 * entry back to zero. This issue does not exist on SNP
	 * enabled system. Also this buffer is not writeable on
	 * SNP enabled system.
	 */
	if (!amd_iommu_snp_en)
		memset(__evt, 0, 4 * sizeof(u32));
}

static void iommu_poll_events(struct amd_iommu *iommu)
{
	u32 head, tail;

	head = readl(iommu->mmio_base + MMIO_EVT_HEAD_OFFSET);
	tail = readl(iommu->mmio_base + MMIO_EVT_TAIL_OFFSET);

	while (head != tail) {
		iommu_print_event(iommu, iommu->evt_buf + head);

		/* Update head pointer of hardware ring-buffer */
		head = (head + EVENT_ENTRY_SIZE) % EVT_BUFFER_SIZE;
		writel(head, iommu->mmio_base + MMIO_EVT_HEAD_OFFSET);
	}

}

#ifdef CONFIG_IRQ_REMAP
static int (*iommu_ga_log_notifier)(u32);

int amd_iommu_register_ga_log_notifier(int (*notifier)(u32))
{
	iommu_ga_log_notifier = notifier;

	/*
	 * Ensure all in-flight IRQ handlers run to completion before returning
	 * to the caller, e.g. to ensure module code isn't unloaded while it's
	 * being executed in the IRQ handler.
	 */
	if (!notifier)
		synchronize_rcu();

	return 0;
}
EXPORT_SYMBOL(amd_iommu_register_ga_log_notifier);

static void iommu_poll_ga_log(struct amd_iommu *iommu)
{
	u32 head, tail;

	if (iommu->ga_log == NULL)
		return;

	head = readl(iommu->mmio_base + MMIO_GA_HEAD_OFFSET);
	tail = readl(iommu->mmio_base + MMIO_GA_TAIL_OFFSET);

	while (head != tail) {
		volatile u64 *raw;
		u64 log_entry;

		raw = (u64 *)(iommu->ga_log + head);

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
	    !pci_dev_has_default_msi_parent_domain(to_pci_dev(dev)))
		return;

	dev_set_msi_domain(dev, iommu->ir_domain);
}

#else /* CONFIG_IRQ_REMAP */
static inline void
amd_iommu_set_pci_msi_domain(struct device *dev, struct amd_iommu *iommu) { }
#endif /* !CONFIG_IRQ_REMAP */

static void amd_iommu_handle_irq(void *data, const char *evt_type,
				 u32 int_mask, u32 overflow_mask,
				 void (*int_handler)(struct amd_iommu *),
				 void (*overflow_handler)(struct amd_iommu *))
{
	struct amd_iommu *iommu = (struct amd_iommu *) data;
	u32 status = readl(iommu->mmio_base + MMIO_STATUS_OFFSET);
	u32 mask = int_mask | overflow_mask;

	while (status & mask) {
		/* Enable interrupt sources again */
		writel(mask, iommu->mmio_base + MMIO_STATUS_OFFSET);

		if (int_handler) {
			pr_devel("Processing IOMMU (ivhd%d) %s Log\n",
				 iommu->index, evt_type);
			int_handler(iommu);
		}

		if ((status & overflow_mask) && overflow_handler)
			overflow_handler(iommu);

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
}

irqreturn_t amd_iommu_int_thread_evtlog(int irq, void *data)
{
	amd_iommu_handle_irq(data, "Evt", MMIO_STATUS_EVT_INT_MASK,
			     MMIO_STATUS_EVT_OVERFLOW_MASK,
			     iommu_poll_events, amd_iommu_restart_event_logging);

	return IRQ_HANDLED;
}

irqreturn_t amd_iommu_int_thread_pprlog(int irq, void *data)
{
	amd_iommu_handle_irq(data, "PPR", MMIO_STATUS_PPR_INT_MASK,
			     MMIO_STATUS_PPR_OVERFLOW_MASK,
			     amd_iommu_poll_ppr_log, amd_iommu_restart_ppr_log);

	return IRQ_HANDLED;
}

irqreturn_t amd_iommu_int_thread_galog(int irq, void *data)
{
#ifdef CONFIG_IRQ_REMAP
	amd_iommu_handle_irq(data, "GA", MMIO_STATUS_GALOG_INT_MASK,
			     MMIO_STATUS_GALOG_OVERFLOW_MASK,
			     iommu_poll_ga_log, amd_iommu_restart_ga_log);
#endif

	return IRQ_HANDLED;
}

irqreturn_t amd_iommu_int_thread(int irq, void *data)
{
	amd_iommu_int_thread_evtlog(irq, data);
	amd_iommu_int_thread_pprlog(irq, data);
	amd_iommu_int_thread_galog(irq, data);

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
	u64 paddr = iommu->cmd_sem_paddr;

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
				  size_t size, u16 domid,
				  ioasid_t pasid, bool gn)
{
	u64 inv_address = build_inv_address(address, size);

	memset(cmd, 0, sizeof(*cmd));

	cmd->data[1] |= domid;
	cmd->data[2]  = lower_32_bits(inv_address);
	cmd->data[3]  = upper_32_bits(inv_address);
	/* PDE bit - we want to flush everything, not only the PTEs */
	cmd->data[2] |= CMD_INV_IOMMU_PAGES_PDE_MASK;
	if (gn) {
		cmd->data[0] |= pasid;
		cmd->data[2] |= CMD_INV_IOMMU_PAGES_GN_MASK;
	}
	CMD_SET_TYPE(cmd, CMD_INV_IOMMU_PAGES);
}

static void build_inv_iotlb_pages(struct iommu_cmd *cmd, u16 devid, int qdep,
				  u64 address, size_t size,
				  ioasid_t pasid, bool gn)
{
	u64 inv_address = build_inv_address(address, size);

	memset(cmd, 0, sizeof(*cmd));

	cmd->data[0]  = devid;
	cmd->data[0] |= (qdep & 0xff) << 24;
	cmd->data[1]  = devid;
	cmd->data[2]  = lower_32_bits(inv_address);
	cmd->data[3]  = upper_32_bits(inv_address);
	if (gn) {
		cmd->data[0] |= ((pasid >> 8) & 0xff) << 16;
		cmd->data[1] |= (pasid & 0xff) << 16;
		cmd->data[2] |= CMD_INV_IOMMU_PAGES_GN_MASK;
	}

	CMD_SET_TYPE(cmd, CMD_INV_IOTLB_PAGES);
}

static void build_complete_ppr(struct iommu_cmd *cmd, u16 devid, u32 pasid,
			       int status, int tag, u8 gn)
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

	data = atomic64_inc_return(&iommu->cmd_sem_val);
	build_completion_wait(&cmd, iommu, data);

	raw_spin_lock_irqsave(&iommu->lock, flags);

	ret = __iommu_queue_command_sync(iommu, &cmd, false);
	if (ret)
		goto out_unlock;

	ret = wait_on_sem(iommu, data);

out_unlock:
	raw_spin_unlock_irqrestore(&iommu->lock, flags);

	return ret;
}

static void domain_flush_complete(struct protection_domain *domain)
{
	struct pdom_iommu_info *pdom_iommu_info;
	unsigned long i;

	lockdep_assert_held(&domain->lock);

	/*
	 * Devices of this domain are behind this IOMMU
	 * We need to wait for completion of all commands.
	 */
	 xa_for_each(&domain->iommu_array, i, pdom_iommu_info)
		iommu_completion_wait(pdom_iommu_info->iommu);
}

static int iommu_flush_dte(struct amd_iommu *iommu, u16 devid)
{
	struct iommu_cmd cmd;

	build_inv_dte(&cmd, devid);

	return iommu_queue_command(iommu, &cmd);
}

static void iommu_flush_dte_sync(struct amd_iommu *iommu, u16 devid)
{
	int ret;

	ret = iommu_flush_dte(iommu, devid);
	if (!ret)
		iommu_completion_wait(iommu);
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
				      dom_id, IOMMU_NO_PASID, false);
		iommu_queue_command(iommu, &cmd);
	}

	iommu_completion_wait(iommu);
}

static void amd_iommu_flush_tlb_domid(struct amd_iommu *iommu, u32 dom_id)
{
	struct iommu_cmd cmd;

	build_inv_iommu_pages(&cmd, 0, CMD_INV_IOMMU_ALL_PAGES_ADDRESS,
			      dom_id, IOMMU_NO_PASID, false);
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

	if (iommu->irtcachedis_enabled)
		return;

	for (devid = 0; devid <= last_bdf; devid++)
		iommu_flush_irt(iommu, devid);

	iommu_completion_wait(iommu);
}

void amd_iommu_flush_all_caches(struct amd_iommu *iommu)
{
	if (check_feature(FEATURE_IA)) {
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
static int device_flush_iotlb(struct iommu_dev_data *dev_data, u64 address,
			      size_t size, ioasid_t pasid, bool gn)
{
	struct amd_iommu *iommu = get_amd_iommu_from_dev_data(dev_data);
	struct iommu_cmd cmd;
	int qdep = dev_data->ats_qdep;

	build_inv_iotlb_pages(&cmd, dev_data->devid, qdep, address,
			      size, pasid, gn);

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
	struct amd_iommu *iommu = get_amd_iommu_from_dev_data(dev_data);
	struct pci_dev *pdev = NULL;
	struct amd_iommu_pci_seg *pci_seg;
	u16 alias;
	int ret;

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

	if (dev_data->ats_enabled) {
		/* Invalidate the entire contents of an IOTLB */
		ret = device_flush_iotlb(dev_data, 0, ~0UL,
					 IOMMU_NO_PASID, false);
	}

	return ret;
}

static int domain_flush_pages_v2(struct protection_domain *pdom,
				 u64 address, size_t size)
{
	struct iommu_dev_data *dev_data;
	struct iommu_cmd cmd;
	int ret = 0;

	lockdep_assert_held(&pdom->lock);
	list_for_each_entry(dev_data, &pdom->dev_list, list) {
		struct amd_iommu *iommu = get_amd_iommu_from_dev(dev_data->dev);
		u16 domid = dev_data->gcr3_info.domid;

		build_inv_iommu_pages(&cmd, address, size,
				      domid, IOMMU_NO_PASID, true);

		ret |= iommu_queue_command(iommu, &cmd);
	}

	return ret;
}

static int domain_flush_pages_v1(struct protection_domain *pdom,
				 u64 address, size_t size)
{
	struct pdom_iommu_info *pdom_iommu_info;
	struct iommu_cmd cmd;
	int ret = 0;
	unsigned long i;

	lockdep_assert_held(&pdom->lock);

	build_inv_iommu_pages(&cmd, address, size,
			      pdom->id, IOMMU_NO_PASID, false);

	xa_for_each(&pdom->iommu_array, i, pdom_iommu_info) {
		/*
		 * Devices of this domain are behind this IOMMU
		 * We need a TLB flush
		 */
		ret |= iommu_queue_command(pdom_iommu_info->iommu, &cmd);
	}

	return ret;
}

/*
 * TLB invalidation function which is called from the mapping functions.
 * It flushes range of PTEs of the domain.
 */
static void __domain_flush_pages(struct protection_domain *domain,
				 u64 address, size_t size)
{
	struct iommu_dev_data *dev_data;
	int ret = 0;
	ioasid_t pasid = IOMMU_NO_PASID;
	bool gn = false;

	lockdep_assert_held(&domain->lock);

	if (pdom_is_v2_pgtbl_mode(domain)) {
		gn = true;
		ret = domain_flush_pages_v2(domain, address, size);
	} else {
		ret = domain_flush_pages_v1(domain, address, size);
	}

	list_for_each_entry(dev_data, &domain->dev_list, list) {

		if (!dev_data->ats_enabled)
			continue;

		ret |= device_flush_iotlb(dev_data, address, size, pasid, gn);
	}

	WARN_ON(ret);
}

void amd_iommu_domain_flush_pages(struct protection_domain *domain,
				  u64 address, size_t size)
{
	lockdep_assert_held(&domain->lock);

	if (likely(!amd_iommu_np_cache)) {
		__domain_flush_pages(domain, address, size);

		/* Wait until IOMMU TLB and all device IOTLB flushes are complete */
		domain_flush_complete(domain);

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

		__domain_flush_pages(domain, address, flush_size);
		address += flush_size;
		size -= flush_size;
	}

	/* Wait until IOMMU TLB and all device IOTLB flushes are complete */
	domain_flush_complete(domain);
}

/* Flush the whole IO/TLB for a given protection domain - including PDE */
static void amd_iommu_domain_flush_all(struct protection_domain *domain)
{
	amd_iommu_domain_flush_pages(domain, 0,
				     CMD_INV_IOMMU_ALL_PAGES_ADDRESS);
}

void amd_iommu_dev_flush_pasid_pages(struct iommu_dev_data *dev_data,
				     ioasid_t pasid, u64 address, size_t size)
{
	struct iommu_cmd cmd;
	struct amd_iommu *iommu = get_amd_iommu_from_dev(dev_data->dev);

	build_inv_iommu_pages(&cmd, address, size,
			      dev_data->gcr3_info.domid, pasid, true);
	iommu_queue_command(iommu, &cmd);

	if (dev_data->ats_enabled)
		device_flush_iotlb(dev_data, address, size, pasid, true);

	iommu_completion_wait(iommu);
}

static void dev_flush_pasid_all(struct iommu_dev_data *dev_data,
				ioasid_t pasid)
{
	amd_iommu_dev_flush_pasid_pages(dev_data, pasid, 0,
					CMD_INV_IOMMU_ALL_PAGES_ADDRESS);
}

/* Flush the not present cache if it exists */
static void domain_flush_np_cache(struct protection_domain *domain,
		dma_addr_t iova, size_t size)
{
	if (unlikely(amd_iommu_np_cache)) {
		unsigned long flags;

		spin_lock_irqsave(&domain->lock, flags);
		amd_iommu_domain_flush_pages(domain, iova, size);
		spin_unlock_irqrestore(&domain->lock, flags);
	}
}


/*
 * This function flushes the DTEs for all devices in domain
 */
void amd_iommu_update_and_flush_device_table(struct protection_domain *domain)
{
	struct iommu_dev_data *dev_data;

	lockdep_assert_held(&domain->lock);

	list_for_each_entry(dev_data, &domain->dev_list, list) {
		struct amd_iommu *iommu = rlookup_amd_iommu(dev_data->dev);

		set_dte_entry(iommu, dev_data);
		clone_aliases(iommu, dev_data->dev);
	}

	list_for_each_entry(dev_data, &domain->dev_list, list)
		device_flush_dte(dev_data);

	domain_flush_complete(domain);
}

int amd_iommu_complete_ppr(struct device *dev, u32 pasid, int status, int tag)
{
	struct iommu_dev_data *dev_data;
	struct amd_iommu *iommu;
	struct iommu_cmd cmd;

	dev_data = dev_iommu_priv_get(dev);
	iommu    = get_amd_iommu_from_dev(dev);

	build_complete_ppr(&cmd, dev_data->devid, pasid, status,
			   tag, dev_data->pri_tlp);

	return iommu_queue_command(iommu, &cmd);
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

static int pdom_id_alloc(void)
{
	return ida_alloc_range(&pdom_ids, 1, MAX_DOMAIN_ID - 1, GFP_ATOMIC);
}

static void pdom_id_free(int id)
{
	ida_free(&pdom_ids, id);
}

static void free_gcr3_tbl_level1(u64 *tbl)
{
	u64 *ptr;
	int i;

	for (i = 0; i < 512; ++i) {
		if (!(tbl[i] & GCR3_VALID))
			continue;

		ptr = iommu_phys_to_virt(tbl[i] & PAGE_MASK);

		iommu_free_pages(ptr);
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

static void free_gcr3_table(struct gcr3_tbl_info *gcr3_info)
{
	if (gcr3_info->glx == 2)
		free_gcr3_tbl_level2(gcr3_info->gcr3_tbl);
	else if (gcr3_info->glx == 1)
		free_gcr3_tbl_level1(gcr3_info->gcr3_tbl);
	else
		WARN_ON_ONCE(gcr3_info->glx != 0);

	gcr3_info->glx = 0;

	/* Free per device domain ID */
	pdom_id_free(gcr3_info->domid);

	iommu_free_pages(gcr3_info->gcr3_tbl);
	gcr3_info->gcr3_tbl = NULL;
}

/*
 * Number of GCR3 table levels required. Level must be 4-Kbyte
 * page and can contain up to 512 entries.
 */
static int get_gcr3_levels(int pasids)
{
	int levels;

	if (pasids == -1)
		return amd_iommu_max_glx_val;

	levels = get_count_order(pasids);

	return levels ? (DIV_ROUND_UP(levels, 9) - 1) : levels;
}

static int setup_gcr3_table(struct gcr3_tbl_info *gcr3_info,
			    struct amd_iommu *iommu, int pasids)
{
	int levels = get_gcr3_levels(pasids);
	int nid = iommu ? dev_to_node(&iommu->dev->dev) : NUMA_NO_NODE;
	int domid;

	if (levels > amd_iommu_max_glx_val)
		return -EINVAL;

	if (gcr3_info->gcr3_tbl)
		return -EBUSY;

	/* Allocate per device domain ID */
	domid = pdom_id_alloc();
	if (domid <= 0)
		return -ENOSPC;
	gcr3_info->domid = domid;

	gcr3_info->gcr3_tbl = iommu_alloc_pages_node_sz(nid, GFP_ATOMIC, SZ_4K);
	if (gcr3_info->gcr3_tbl == NULL) {
		pdom_id_free(domid);
		return -ENOMEM;
	}

	gcr3_info->glx = levels;

	return 0;
}

static u64 *__get_gcr3_pte(struct gcr3_tbl_info *gcr3_info,
			   ioasid_t pasid, bool alloc)
{
	int index;
	u64 *pte;
	u64 *root = gcr3_info->gcr3_tbl;
	int level = gcr3_info->glx;

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

static int update_gcr3(struct iommu_dev_data *dev_data,
		       ioasid_t pasid, unsigned long gcr3, bool set)
{
	struct gcr3_tbl_info *gcr3_info = &dev_data->gcr3_info;
	u64 *pte;

	pte = __get_gcr3_pte(gcr3_info, pasid, true);
	if (pte == NULL)
		return -ENOMEM;

	if (set)
		*pte = (gcr3 & PAGE_MASK) | GCR3_VALID;
	else
		*pte = 0;

	dev_flush_pasid_all(dev_data, pasid);
	return 0;
}

int amd_iommu_set_gcr3(struct iommu_dev_data *dev_data, ioasid_t pasid,
		       unsigned long gcr3)
{
	struct gcr3_tbl_info *gcr3_info = &dev_data->gcr3_info;
	int ret;

	iommu_group_mutex_assert(dev_data->dev);

	ret = update_gcr3(dev_data, pasid, gcr3, true);
	if (ret)
		return ret;

	gcr3_info->pasid_cnt++;
	return ret;
}

int amd_iommu_clear_gcr3(struct iommu_dev_data *dev_data, ioasid_t pasid)
{
	struct gcr3_tbl_info *gcr3_info = &dev_data->gcr3_info;
	int ret;

	iommu_group_mutex_assert(dev_data->dev);

	ret = update_gcr3(dev_data, pasid, 0, false);
	if (ret)
		return ret;

	gcr3_info->pasid_cnt--;
	return ret;
}

static void make_clear_dte(struct iommu_dev_data *dev_data, struct dev_table_entry *ptr,
			   struct dev_table_entry *new)
{
	/* All existing DTE must have V bit set */
	new->data128[0] = DTE_FLAG_V;
	new->data128[1] = 0;
}

/*
 * Note:
 * The old value for GCR3 table and GPT have been cleared from caller.
 */
static void set_dte_gcr3_table(struct amd_iommu *iommu,
			       struct iommu_dev_data *dev_data,
			       struct dev_table_entry *target)
{
	struct gcr3_tbl_info *gcr3_info = &dev_data->gcr3_info;
	u64 gcr3;

	if (!gcr3_info->gcr3_tbl)
		return;

	pr_debug("%s: devid=%#x, glx=%#x, gcr3_tbl=%#llx\n",
		 __func__, dev_data->devid, gcr3_info->glx,
		 (unsigned long long)gcr3_info->gcr3_tbl);

	gcr3 = iommu_virt_to_phys(gcr3_info->gcr3_tbl);

	target->data[0] |= DTE_FLAG_GV |
			   FIELD_PREP(DTE_GLX, gcr3_info->glx) |
			   FIELD_PREP(DTE_GCR3_14_12, gcr3 >> 12);
	if (pdom_is_v2_pgtbl_mode(dev_data->domain))
		target->data[0] |= DTE_FLAG_GIOV;

	target->data[1] |= FIELD_PREP(DTE_GCR3_30_15, gcr3 >> 15) |
			   FIELD_PREP(DTE_GCR3_51_31, gcr3 >> 31);

	/* Guest page table can only support 4 and 5 levels  */
	if (amd_iommu_gpt_level == PAGE_MODE_5_LEVEL)
		target->data[2] |= FIELD_PREP(DTE_GPT_LEVEL_MASK, GUEST_PGTABLE_5_LEVEL);
	else
		target->data[2] |= FIELD_PREP(DTE_GPT_LEVEL_MASK, GUEST_PGTABLE_4_LEVEL);
}

static void set_dte_entry(struct amd_iommu *iommu,
			  struct iommu_dev_data *dev_data)
{
	u16 domid;
	u32 old_domid;
	struct dev_table_entry *initial_dte;
	struct dev_table_entry new = {};
	struct protection_domain *domain = dev_data->domain;
	struct gcr3_tbl_info *gcr3_info = &dev_data->gcr3_info;
	struct dev_table_entry *dte = &get_dev_table(iommu)[dev_data->devid];

	if (gcr3_info && gcr3_info->gcr3_tbl)
		domid = dev_data->gcr3_info.domid;
	else
		domid = domain->id;

	make_clear_dte(dev_data, dte, &new);

	if (domain->iop.mode != PAGE_MODE_NONE)
		new.data[0] |= iommu_virt_to_phys(domain->iop.root);

	new.data[0] |= (domain->iop.mode & DEV_ENTRY_MODE_MASK)
		    << DEV_ENTRY_MODE_SHIFT;

	new.data[0] |= DTE_FLAG_IR | DTE_FLAG_IW;

	/*
	 * When SNP is enabled, we can only support TV=1 with non-zero domain ID.
	 * This is prevented by the SNP-enable and IOMMU_DOMAIN_IDENTITY check in
	 * do_iommu_domain_alloc().
	 */
	WARN_ON(amd_iommu_snp_en && (domid == 0));
	new.data[0] |= DTE_FLAG_TV;

	if (dev_data->ppr)
		new.data[0] |= 1ULL << DEV_ENTRY_PPR;

	if (domain->dirty_tracking)
		new.data[0] |= DTE_FLAG_HAD;

	if (dev_data->ats_enabled)
		new.data[1] |= DTE_FLAG_IOTLB;

	old_domid = READ_ONCE(dte->data[1]) & DEV_DOMID_MASK;
	new.data[1] |= domid;

	/*
	 * Restore cached persistent DTE bits, which can be set by information
	 * in IVRS table. See set_dev_entry_from_acpi().
	 */
	initial_dte = amd_iommu_get_ivhd_dte_flags(iommu->pci_seg->id, dev_data->devid);
	if (initial_dte) {
		new.data128[0] |= initial_dte->data128[0];
		new.data128[1] |= initial_dte->data128[1];
	}

	set_dte_gcr3_table(iommu, dev_data, &new);

	update_dte256(iommu, dev_data, &new);

	/*
	 * A kdump kernel might be replacing a domain ID that was copied from
	 * the previous kernel--if so, it needs to flush the translation cache
	 * entries for the old domain ID that is being overwritten
	 */
	if (old_domid) {
		amd_iommu_flush_tlb_domid(iommu, old_domid);
	}
}

/*
 * Clear DMA-remap related flags to block all DMA (blockeded domain)
 */
static void clear_dte_entry(struct amd_iommu *iommu, struct iommu_dev_data *dev_data)
{
	struct dev_table_entry new = {};
	struct dev_table_entry *dte = &get_dev_table(iommu)[dev_data->devid];

	make_clear_dte(dev_data, dte, &new);
	update_dte256(iommu, dev_data, &new);
}

/* Update and flush DTE for the given device */
static void dev_update_dte(struct iommu_dev_data *dev_data, bool set)
{
	struct amd_iommu *iommu = get_amd_iommu_from_dev(dev_data->dev);

	if (set)
		set_dte_entry(iommu, dev_data);
	else
		clear_dte_entry(iommu, dev_data);

	clone_aliases(iommu, dev_data->dev);
	device_flush_dte(dev_data);
	iommu_completion_wait(iommu);
}

/*
 * If domain is SVA capable then initialize GCR3 table. Also if domain is
 * in v2 page table mode then update GCR3[0].
 */
static int init_gcr3_table(struct iommu_dev_data *dev_data,
			   struct protection_domain *pdom)
{
	struct amd_iommu *iommu = get_amd_iommu_from_dev_data(dev_data);
	int max_pasids = dev_data->max_pasids;
	int ret = 0;

	 /*
	  * If domain is in pt mode then setup GCR3 table only if device
	  * is PASID capable
	  */
	if (pdom_is_in_pt_mode(pdom) && !pdev_pasid_supported(dev_data))
		return ret;

	/*
	 * By default, setup GCR3 table to support MAX PASIDs
	 * supported by the device/IOMMU.
	 */
	ret = setup_gcr3_table(&dev_data->gcr3_info, iommu,
			       max_pasids > 0 ?  max_pasids : 1);
	if (ret)
		return ret;

	/* Setup GCR3[0] only if domain is setup with v2 page table mode */
	if (!pdom_is_v2_pgtbl_mode(pdom))
		return ret;

	ret = update_gcr3(dev_data, 0, iommu_virt_to_phys(pdom->iop.pgd), true);
	if (ret)
		free_gcr3_table(&dev_data->gcr3_info);

	return ret;
}

static void destroy_gcr3_table(struct iommu_dev_data *dev_data,
			       struct protection_domain *pdom)
{
	struct gcr3_tbl_info *gcr3_info = &dev_data->gcr3_info;

	if (pdom_is_v2_pgtbl_mode(pdom))
		update_gcr3(dev_data, 0, 0, false);

	if (gcr3_info->gcr3_tbl == NULL)
		return;

	free_gcr3_table(gcr3_info);
}

static int pdom_attach_iommu(struct amd_iommu *iommu,
			     struct protection_domain *pdom)
{
	struct pdom_iommu_info *pdom_iommu_info, *curr;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&pdom->lock, flags);

	pdom_iommu_info = xa_load(&pdom->iommu_array, iommu->index);
	if (pdom_iommu_info) {
		pdom_iommu_info->refcnt++;
		goto out_unlock;
	}

	pdom_iommu_info = kzalloc(sizeof(*pdom_iommu_info), GFP_ATOMIC);
	if (!pdom_iommu_info) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	pdom_iommu_info->iommu = iommu;
	pdom_iommu_info->refcnt = 1;

	curr = xa_cmpxchg(&pdom->iommu_array, iommu->index,
			  NULL, pdom_iommu_info, GFP_ATOMIC);
	if (curr) {
		kfree(pdom_iommu_info);
		ret = -ENOSPC;
		goto out_unlock;
	}

out_unlock:
	spin_unlock_irqrestore(&pdom->lock, flags);
	return ret;
}

static void pdom_detach_iommu(struct amd_iommu *iommu,
			      struct protection_domain *pdom)
{
	struct pdom_iommu_info *pdom_iommu_info;
	unsigned long flags;

	spin_lock_irqsave(&pdom->lock, flags);

	pdom_iommu_info = xa_load(&pdom->iommu_array, iommu->index);
	if (!pdom_iommu_info) {
		spin_unlock_irqrestore(&pdom->lock, flags);
		return;
	}

	pdom_iommu_info->refcnt--;
	if (pdom_iommu_info->refcnt == 0) {
		xa_erase(&pdom->iommu_array, iommu->index);
		kfree(pdom_iommu_info);
	}

	spin_unlock_irqrestore(&pdom->lock, flags);
}

/*
 * If a device is not yet associated with a domain, this function makes the
 * device visible in the domain
 */
static int attach_device(struct device *dev,
			 struct protection_domain *domain)
{
	struct iommu_dev_data *dev_data = dev_iommu_priv_get(dev);
	struct amd_iommu *iommu = get_amd_iommu_from_dev_data(dev_data);
	struct pci_dev *pdev;
	unsigned long flags;
	int ret = 0;

	mutex_lock(&dev_data->mutex);

	if (dev_data->domain != NULL) {
		ret = -EBUSY;
		goto out;
	}

	/* Do reference counting */
	ret = pdom_attach_iommu(iommu, domain);
	if (ret)
		goto out;

	/* Setup GCR3 table */
	if (pdom_is_sva_capable(domain)) {
		ret = init_gcr3_table(dev_data, domain);
		if (ret) {
			pdom_detach_iommu(iommu, domain);
			goto out;
		}
	}

	pdev = dev_is_pci(dev_data->dev) ? to_pci_dev(dev_data->dev) : NULL;
	if (pdev && pdom_is_sva_capable(domain)) {
		pdev_enable_caps(pdev);

		/*
		 * Device can continue to function even if IOPF
		 * enablement failed. Hence in error path just
		 * disable device PRI support.
		 */
		if (amd_iommu_iopf_add_device(iommu, dev_data))
			pdev_disable_cap_pri(pdev);
	} else if (pdev) {
		pdev_enable_cap_ats(pdev);
	}

	/* Update data structures */
	dev_data->domain = domain;
	spin_lock_irqsave(&domain->lock, flags);
	list_add(&dev_data->list, &domain->dev_list);
	spin_unlock_irqrestore(&domain->lock, flags);

	/* Update device table */
	dev_update_dte(dev_data, true);

out:
	mutex_unlock(&dev_data->mutex);

	return ret;
}

/*
 * Removes a device from a protection domain (with devtable_lock held)
 */
static void detach_device(struct device *dev)
{
	struct iommu_dev_data *dev_data = dev_iommu_priv_get(dev);
	struct amd_iommu *iommu = get_amd_iommu_from_dev_data(dev_data);
	struct protection_domain *domain = dev_data->domain;
	unsigned long flags;

	mutex_lock(&dev_data->mutex);

	/*
	 * First check if the device is still attached. It might already
	 * be detached from its domain because the generic
	 * iommu_detach_group code detached it and we try again here in
	 * our alias handling.
	 */
	if (WARN_ON(!dev_data->domain))
		goto out;

	/* Remove IOPF handler */
	if (dev_data->ppr) {
		iopf_queue_flush_dev(dev);
		amd_iommu_iopf_remove_device(iommu, dev_data);
	}

	if (dev_is_pci(dev))
		pdev_disable_caps(to_pci_dev(dev));

	/* Clear DTE and flush the entry */
	dev_update_dte(dev_data, false);

	/* Flush IOTLB and wait for the flushes to finish */
	spin_lock_irqsave(&domain->lock, flags);
	amd_iommu_domain_flush_all(domain);
	list_del(&dev_data->list);
	spin_unlock_irqrestore(&domain->lock, flags);

	/* Clear GCR3 table */
	if (pdom_is_sva_capable(domain))
		destroy_gcr3_table(dev_data, domain);

	/* Update data structures */
	dev_data->domain = NULL;

	/* decrease reference counters - needs to happen after the flushes */
	pdom_detach_iommu(iommu, domain);

out:
	mutex_unlock(&dev_data->mutex);
}

static struct iommu_device *amd_iommu_probe_device(struct device *dev)
{
	struct iommu_device *iommu_dev;
	struct amd_iommu *iommu;
	struct iommu_dev_data *dev_data;
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
		dev_err(dev, "Failed to initialize - trying to proceed anyway\n");
		iommu_dev = ERR_PTR(ret);
		iommu_ignore_device(iommu, dev);
		goto out_err;
	}

	amd_iommu_set_pci_msi_domain(dev, iommu);
	iommu_dev = &iommu->iommu;

	/*
	 * If IOMMU and device supports PASID then it will contain max
	 * supported PASIDs, else it will be zero.
	 */
	dev_data = dev_iommu_priv_get(dev);
	if (amd_iommu_pasid_supported() && dev_is_pci(dev) &&
	    pdev_pasid_supported(dev_data)) {
		dev_data->max_pasids = min_t(u32, iommu->iommu.max_pasids,
					     pci_max_pasids(to_pci_dev(dev)));
	}

	if (amd_iommu_pgtable == PD_MODE_NONE) {
		pr_warn_once("%s: DMA translation not supported by iommu.\n",
			     __func__);
		iommu_dev = ERR_PTR(-ENODEV);
		goto out_err;
	}

out_err:

	iommu_completion_wait(iommu);

	if (FEATURE_NUM_INT_REMAP_SUP_2K(amd_iommu_efr2))
		dev_data->max_irqs = MAX_IRQS_PER_TABLE_2K;
	else
		dev_data->max_irqs = MAX_IRQS_PER_TABLE_512;

	if (dev_is_pci(dev))
		pci_prepare_ats(to_pci_dev(dev), PAGE_SHIFT);

	return iommu_dev;
}

static void amd_iommu_release_device(struct device *dev)
{
	struct iommu_dev_data *dev_data = dev_iommu_priv_get(dev);

	WARN_ON(dev_data->domain);

	/*
	 * We keep dev_data around for unplugged devices and reuse it when the
	 * device is re-plugged - not doing so would introduce a ton of races.
	 */
}

static struct iommu_group *amd_iommu_device_group(struct device *dev)
{
	if (dev_is_pci(dev))
		return pci_device_group(dev);

	return acpihid_device_group(dev);
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

static void protection_domain_init(struct protection_domain *domain)
{
	spin_lock_init(&domain->lock);
	INIT_LIST_HEAD(&domain->dev_list);
	INIT_LIST_HEAD(&domain->dev_data_list);
	xa_init(&domain->iommu_array);
}

struct protection_domain *protection_domain_alloc(void)
{
	struct protection_domain *domain;
	int domid;

	domain = kzalloc(sizeof(*domain), GFP_KERNEL);
	if (!domain)
		return NULL;

	domid = pdom_id_alloc();
	if (domid <= 0) {
		kfree(domain);
		return NULL;
	}
	domain->id = domid;

	protection_domain_init(domain);

	return domain;
}

static int pdom_setup_pgtable(struct protection_domain *domain,
			      struct device *dev)
{
	struct io_pgtable_ops *pgtbl_ops;
	enum io_pgtable_fmt fmt;

	switch (domain->pd_mode) {
	case PD_MODE_V1:
		fmt = AMD_IOMMU_V1;
		break;
	case PD_MODE_V2:
		fmt = AMD_IOMMU_V2;
		break;
	case PD_MODE_NONE:
		WARN_ON_ONCE(1);
		return -EPERM;
	}

	domain->iop.pgtbl.cfg.amd.nid = dev_to_node(dev);
	pgtbl_ops = alloc_io_pgtable_ops(fmt, &domain->iop.pgtbl.cfg, domain);
	if (!pgtbl_ops)
		return -ENOMEM;

	return 0;
}

static inline u64 dma_max_address(enum protection_domain_mode pgtable)
{
	if (pgtable == PD_MODE_V1)
		return PM_LEVEL_SIZE(amd_iommu_hpt_level);

	/*
	 * V2 with 4/5 level page table. Note that "2.2.6.5 AMD64 4-Kbyte Page
	 * Translation" shows that the V2 table sign extends the top of the
	 * address space creating a reserved region in the middle of the
	 * translation, just like the CPU does. Further Vasant says the docs are
	 * incomplete and this only applies to non-zero PASIDs. If the AMDv2
	 * page table is assigned to the 0 PASID then there is no sign extension
	 * check.
	 *
	 * Since the IOMMU must have a fixed geometry, and the core code does
	 * not understand sign extended addressing, we have to chop off the high
	 * bit to get consistent behavior with attachments of the domain to any
	 * PASID.
	 */
	return ((1ULL << (PM_LEVEL_SHIFT(amd_iommu_gpt_level) - 1)) - 1);
}

static bool amd_iommu_hd_support(struct amd_iommu *iommu)
{
	if (amd_iommu_hatdis)
		return false;

	return iommu && (iommu->features & FEATURE_HDSUP);
}

static struct iommu_domain *
do_iommu_domain_alloc(struct device *dev, u32 flags,
		      enum protection_domain_mode pgtable)
{
	bool dirty_tracking = flags & IOMMU_HWPT_ALLOC_DIRTY_TRACKING;
	struct amd_iommu *iommu = get_amd_iommu_from_dev(dev);
	struct protection_domain *domain;
	int ret;

	domain = protection_domain_alloc();
	if (!domain)
		return ERR_PTR(-ENOMEM);

	domain->pd_mode = pgtable;
	ret = pdom_setup_pgtable(domain, dev);
	if (ret) {
		pdom_id_free(domain->id);
		kfree(domain);
		return ERR_PTR(ret);
	}

	domain->domain.geometry.aperture_start = 0;
	domain->domain.geometry.aperture_end   = dma_max_address(pgtable);
	domain->domain.geometry.force_aperture = true;
	domain->domain.pgsize_bitmap = domain->iop.pgtbl.cfg.pgsize_bitmap;

	domain->domain.type = IOMMU_DOMAIN_UNMANAGED;
	domain->domain.ops = iommu->iommu.ops->default_domain_ops;

	if (dirty_tracking)
		domain->domain.dirty_ops = &amd_dirty_ops;

	return &domain->domain;
}

static struct iommu_domain *
amd_iommu_domain_alloc_paging_flags(struct device *dev, u32 flags,
				    const struct iommu_user_data *user_data)

{
	struct amd_iommu *iommu = get_amd_iommu_from_dev(dev);
	const u32 supported_flags = IOMMU_HWPT_ALLOC_DIRTY_TRACKING |
						IOMMU_HWPT_ALLOC_PASID;

	if ((flags & ~supported_flags) || user_data)
		return ERR_PTR(-EOPNOTSUPP);

	switch (flags & supported_flags) {
	case IOMMU_HWPT_ALLOC_DIRTY_TRACKING:
		/* Allocate domain with v1 page table for dirty tracking */
		if (!amd_iommu_hd_support(iommu))
			break;
		return do_iommu_domain_alloc(dev, flags, PD_MODE_V1);
	case IOMMU_HWPT_ALLOC_PASID:
		/* Allocate domain with v2 page table if IOMMU supports PASID. */
		if (!amd_iommu_pasid_supported())
			break;
		return do_iommu_domain_alloc(dev, flags, PD_MODE_V2);
	case 0:
		/* If nothing specific is required use the kernel commandline default */
		return do_iommu_domain_alloc(dev, 0, amd_iommu_pgtable);
	default:
		break;
	}
	return ERR_PTR(-EOPNOTSUPP);
}

void amd_iommu_domain_free(struct iommu_domain *dom)
{
	struct protection_domain *domain = to_pdomain(dom);

	WARN_ON(!list_empty(&domain->dev_list));
	if (domain->domain.type & __IOMMU_DOMAIN_PAGING)
		free_io_pgtable_ops(&domain->iop.pgtbl.ops);
	pdom_id_free(domain->id);
	kfree(domain);
}

static int blocked_domain_attach_device(struct iommu_domain *domain,
					struct device *dev)
{
	struct iommu_dev_data *dev_data = dev_iommu_priv_get(dev);

	if (dev_data->domain)
		detach_device(dev);

	/* Clear DTE and flush the entry */
	mutex_lock(&dev_data->mutex);
	dev_update_dte(dev_data, false);
	mutex_unlock(&dev_data->mutex);

	return 0;
}

static int blocked_domain_set_dev_pasid(struct iommu_domain *domain,
					struct device *dev, ioasid_t pasid,
					struct iommu_domain *old)
{
	amd_iommu_remove_dev_pasid(dev, pasid, old);
	return 0;
}

static struct iommu_domain blocked_domain = {
	.type = IOMMU_DOMAIN_BLOCKED,
	.ops = &(const struct iommu_domain_ops) {
		.attach_dev     = blocked_domain_attach_device,
		.set_dev_pasid  = blocked_domain_set_dev_pasid,
	}
};

static struct protection_domain identity_domain;

static const struct iommu_domain_ops identity_domain_ops = {
	.attach_dev = amd_iommu_attach_device,
};

void amd_iommu_init_identity_domain(void)
{
	struct iommu_domain *domain = &identity_domain.domain;

	domain->type = IOMMU_DOMAIN_IDENTITY;
	domain->ops = &identity_domain_ops;
	domain->owner = &amd_iommu_ops;

	identity_domain.id = pdom_id_alloc();

	protection_domain_init(&identity_domain);
}

/* Same as blocked domain except it supports only ops->attach_dev() */
static struct iommu_domain release_domain = {
	.type = IOMMU_DOMAIN_BLOCKED,
	.ops = &(const struct iommu_domain_ops) {
		.attach_dev     = blocked_domain_attach_device,
	}
};

static int amd_iommu_attach_device(struct iommu_domain *dom,
				   struct device *dev)
{
	struct iommu_dev_data *dev_data = dev_iommu_priv_get(dev);
	struct protection_domain *domain = to_pdomain(dom);
	struct amd_iommu *iommu = get_amd_iommu_from_dev(dev);
	int ret;

	/*
	 * Skip attach device to domain if new domain is same as
	 * devices current domain
	 */
	if (dev_data->domain == domain)
		return 0;

	dev_data->defer_attach = false;

	/*
	 * Restrict to devices with compatible IOMMU hardware support
	 * when enforcement of dirty tracking is enabled.
	 */
	if (dom->dirty_ops && !amd_iommu_hd_support(iommu))
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

	return ret;
}

static int amd_iommu_iotlb_sync_map(struct iommu_domain *dom,
				    unsigned long iova, size_t size)
{
	struct protection_domain *domain = to_pdomain(dom);
	struct io_pgtable_ops *ops = &domain->iop.pgtbl.ops;

	if (ops->map_pages)
		domain_flush_np_cache(domain, iova, size);
	return 0;
}

static int amd_iommu_map_pages(struct iommu_domain *dom, unsigned long iova,
			       phys_addr_t paddr, size_t pgsize, size_t pgcount,
			       int iommu_prot, gfp_t gfp, size_t *mapped)
{
	struct protection_domain *domain = to_pdomain(dom);
	struct io_pgtable_ops *ops = &domain->iop.pgtbl.ops;
	int prot = 0;
	int ret = -EINVAL;

	if ((domain->pd_mode == PD_MODE_V1) &&
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
	struct io_pgtable_ops *ops = &domain->iop.pgtbl.ops;
	size_t r;

	if ((domain->pd_mode == PD_MODE_V1) &&
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
	struct io_pgtable_ops *ops = &domain->iop.pgtbl.ops;

	return ops->iova_to_phys(ops, iova);
}

static bool amd_iommu_capable(struct device *dev, enum iommu_cap cap)
{
	switch (cap) {
	case IOMMU_CAP_CACHE_COHERENCY:
		return true;
	case IOMMU_CAP_NOEXEC:
		return false;
	case IOMMU_CAP_PRE_BOOT_PROTECTION:
		return amdr_ivrs_remap_support;
	case IOMMU_CAP_ENFORCE_CACHE_COHERENCY:
		return true;
	case IOMMU_CAP_DEFERRED_FLUSH:
		return true;
	case IOMMU_CAP_DIRTY_TRACKING: {
		struct amd_iommu *iommu = get_amd_iommu_from_dev(dev);

		return amd_iommu_hd_support(iommu);
	}
	default:
		break;
	}

	return false;
}

static int amd_iommu_set_dirty_tracking(struct iommu_domain *domain,
					bool enable)
{
	struct protection_domain *pdomain = to_pdomain(domain);
	struct dev_table_entry *dte;
	struct iommu_dev_data *dev_data;
	bool domain_flush = false;
	struct amd_iommu *iommu;
	unsigned long flags;
	u64 new;

	spin_lock_irqsave(&pdomain->lock, flags);
	if (!(pdomain->dirty_tracking ^ enable)) {
		spin_unlock_irqrestore(&pdomain->lock, flags);
		return 0;
	}

	list_for_each_entry(dev_data, &pdomain->dev_list, list) {
		spin_lock(&dev_data->dte_lock);
		iommu = get_amd_iommu_from_dev_data(dev_data);
		dte = &get_dev_table(iommu)[dev_data->devid];
		new = dte->data[0];
		new = (enable ? new | DTE_FLAG_HAD : new & ~DTE_FLAG_HAD);
		dte->data[0] = new;
		spin_unlock(&dev_data->dte_lock);

		/* Flush device DTE */
		device_flush_dte(dev_data);
		domain_flush = true;
	}

	/* Flush IOTLB to mark IOPTE dirty on the next translation(s) */
	if (domain_flush)
		amd_iommu_domain_flush_all(pdomain);

	pdomain->dirty_tracking = enable;
	spin_unlock_irqrestore(&pdomain->lock, flags);

	return 0;
}

static int amd_iommu_read_and_clear_dirty(struct iommu_domain *domain,
					  unsigned long iova, size_t size,
					  unsigned long flags,
					  struct iommu_dirty_bitmap *dirty)
{
	struct protection_domain *pdomain = to_pdomain(domain);
	struct io_pgtable_ops *ops = &pdomain->iop.pgtbl.ops;
	unsigned long lflags;

	if (!ops || !ops->read_and_clear_dirty)
		return -EOPNOTSUPP;

	spin_lock_irqsave(&pdomain->lock, lflags);
	if (!pdomain->dirty_tracking && dirty->bitmap) {
		spin_unlock_irqrestore(&pdomain->lock, lflags);
		return -EINVAL;
	}
	spin_unlock_irqrestore(&pdomain->lock, lflags);

	return ops->read_and_clear_dirty(ops, iova, size, flags, dirty);
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
	iommu = get_amd_iommu_from_dev(dev);
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

	if (amd_iommu_ht_range_ignore())
		return;

	region = iommu_alloc_resv_region(HT_RANGE_START,
					 HT_RANGE_END - HT_RANGE_START + 1,
					 0, IOMMU_RESV_RESERVED, GFP_KERNEL);
	if (!region)
		return;
	list_add_tail(&region->list, head);
}

static bool amd_iommu_is_attach_deferred(struct device *dev)
{
	struct iommu_dev_data *dev_data = dev_iommu_priv_get(dev);

	return dev_data->defer_attach;
}

static void amd_iommu_flush_iotlb_all(struct iommu_domain *domain)
{
	struct protection_domain *dom = to_pdomain(domain);
	unsigned long flags;

	spin_lock_irqsave(&dom->lock, flags);
	amd_iommu_domain_flush_all(dom);
	spin_unlock_irqrestore(&dom->lock, flags);
}

static void amd_iommu_iotlb_sync(struct iommu_domain *domain,
				 struct iommu_iotlb_gather *gather)
{
	struct protection_domain *dom = to_pdomain(domain);
	unsigned long flags;

	spin_lock_irqsave(&dom->lock, flags);
	amd_iommu_domain_flush_pages(dom, gather->start,
				     gather->end - gather->start + 1);
	spin_unlock_irqrestore(&dom->lock, flags);
}

static int amd_iommu_def_domain_type(struct device *dev)
{
	struct iommu_dev_data *dev_data;

	dev_data = dev_iommu_priv_get(dev);
	if (!dev_data)
		return 0;

	/* Always use DMA domain for untrusted device */
	if (dev_is_pci(dev) && to_pci_dev(dev)->untrusted)
		return IOMMU_DOMAIN_DMA;

	/*
	 * Do not identity map IOMMUv2 capable devices when:
	 *  - memory encryption is active, because some of those devices
	 *    (AMD GPUs) don't have the encryption bit in their DMA-mask
	 *    and require remapping.
	 *  - SNP is enabled, because it prohibits DTE[Mode]=0.
	 */
	if (pdev_pasid_supported(dev_data) &&
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

static const struct iommu_dirty_ops amd_dirty_ops = {
	.set_dirty_tracking = amd_iommu_set_dirty_tracking,
	.read_and_clear_dirty = amd_iommu_read_and_clear_dirty,
};

const struct iommu_ops amd_iommu_ops = {
	.capable = amd_iommu_capable,
	.blocked_domain = &blocked_domain,
	.release_domain = &release_domain,
	.identity_domain = &identity_domain.domain,
	.domain_alloc_paging_flags = amd_iommu_domain_alloc_paging_flags,
	.domain_alloc_sva = amd_iommu_domain_alloc_sva,
	.probe_device = amd_iommu_probe_device,
	.release_device = amd_iommu_release_device,
	.device_group = amd_iommu_device_group,
	.get_resv_regions = amd_iommu_get_resv_regions,
	.is_attach_deferred = amd_iommu_is_attach_deferred,
	.def_domain_type = amd_iommu_def_domain_type,
	.page_response = amd_iommu_page_response,
	.default_domain_ops = &(const struct iommu_domain_ops) {
		.attach_dev	= amd_iommu_attach_device,
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

#ifdef CONFIG_IRQ_REMAP

/*****************************************************************************
 *
 * Interrupt Remapping Implementation
 *
 *****************************************************************************/

static struct irq_chip amd_ir_chip;
static DEFINE_SPINLOCK(iommu_table_lock);

static void iommu_flush_irt_and_complete(struct amd_iommu *iommu, u16 devid)
{
	int ret;
	u64 data;
	unsigned long flags;
	struct iommu_cmd cmd, cmd2;

	if (iommu->irtcachedis_enabled)
		return;

	build_inv_irt(&cmd, devid);
	data = atomic64_inc_return(&iommu->cmd_sem_val);
	build_completion_wait(&cmd2, iommu, data);

	raw_spin_lock_irqsave(&iommu->lock, flags);
	ret = __iommu_queue_command_sync(iommu, &cmd, true);
	if (ret)
		goto out;
	ret = __iommu_queue_command_sync(iommu, &cmd2, false);
	if (ret)
		goto out;
	wait_on_sem(iommu, data);
out:
	raw_spin_unlock_irqrestore(&iommu->lock, flags);
}

static inline u8 iommu_get_int_tablen(struct iommu_dev_data *dev_data)
{
	if (dev_data && dev_data->max_irqs == MAX_IRQS_PER_TABLE_2K)
		return DTE_INTTABLEN_2K;
	return DTE_INTTABLEN_512;
}

static void set_dte_irq_entry(struct amd_iommu *iommu, u16 devid,
			      struct irq_remap_table *table)
{
	u64 new;
	struct dev_table_entry *dte = &get_dev_table(iommu)[devid];
	struct iommu_dev_data *dev_data = search_dev_data(iommu, devid);

	if (dev_data)
		spin_lock(&dev_data->dte_lock);

	new = READ_ONCE(dte->data[2]);
	new &= ~DTE_IRQ_PHYS_ADDR_MASK;
	new |= iommu_virt_to_phys(table->table);
	new |= DTE_IRQ_REMAP_INTCTL;
	new |= iommu_get_int_tablen(dev_data);
	new |= DTE_IRQ_REMAP_ENABLE;
	WRITE_ONCE(dte->data[2], new);

	if (dev_data)
		spin_unlock(&dev_data->dte_lock);
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

static struct irq_remap_table *__alloc_irq_table(int nid, size_t size)
{
	struct irq_remap_table *table;

	table = kzalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		return NULL;

	table->table = iommu_alloc_pages_node_sz(
		nid, GFP_KERNEL, max(DTE_INTTAB_ALIGNMENT, size));
	if (!table->table) {
		kfree(table);
		return NULL;
	}
	raw_spin_lock_init(&table->lock);

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

static inline size_t get_irq_table_size(unsigned int max_irqs)
{
	if (!AMD_IOMMU_GUEST_IR_GA(amd_iommu_guest_ir))
		return max_irqs * sizeof(u32);

	return max_irqs * (sizeof(u64) * 2);
}

static struct irq_remap_table *alloc_irq_table(struct amd_iommu *iommu,
					       u16 devid, struct pci_dev *pdev,
					       unsigned int max_irqs)
{
	struct irq_remap_table *table = NULL;
	struct irq_remap_table *new_table = NULL;
	struct amd_iommu_pci_seg *pci_seg;
	unsigned long flags;
	int nid = iommu && iommu->dev ? dev_to_node(&iommu->dev->dev) : NUMA_NO_NODE;
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
	new_table = __alloc_irq_table(nid, get_irq_table_size(max_irqs));
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
		iommu_free_pages(new_table->table);
		kfree(new_table);
	}
	return table;
}

static int alloc_irq_index(struct amd_iommu *iommu, u16 devid, int count,
			   bool align, struct pci_dev *pdev,
			   unsigned long max_irqs)
{
	struct irq_remap_table *table;
	int index, c, alignment = 1;
	unsigned long flags;

	table = alloc_irq_table(iommu, devid, pdev, max_irqs);
	if (!table)
		return -ENODEV;

	if (align)
		alignment = roundup_pow_of_two(count);

	raw_spin_lock_irqsave(&table->lock, flags);

	/* Scan table for free entries */
	for (index = ALIGN(table->min_index, alignment), c = 0;
	     index < max_irqs;) {
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

static int __modify_irte_ga(struct amd_iommu *iommu, u16 devid, int index,
			    struct irte_ga *irte)
{
	struct irq_remap_table *table;
	struct irte_ga *entry;
	unsigned long flags;
	u128 old;

	table = get_irq_table(iommu, devid);
	if (!table)
		return -ENOMEM;

	raw_spin_lock_irqsave(&table->lock, flags);

	entry = (struct irte_ga *)table->table;
	entry = &entry[index];

	/*
	 * We use cmpxchg16 to atomically update the 128-bit IRTE,
	 * and it cannot be updated by the hardware or other processors
	 * behind us, so the return value of cmpxchg16 should be the
	 * same as the old value.
	 */
	old = entry->irte;
	WARN_ON(!try_cmpxchg128(&entry->irte, &old, irte->irte));

	raw_spin_unlock_irqrestore(&table->lock, flags);

	return 0;
}

static int modify_irte_ga(struct amd_iommu *iommu, u16 devid, int index,
			  struct irte_ga *irte)
{
	bool ret;

	ret = __modify_irte_ga(iommu, devid, index, irte);
	if (ret)
		return ret;

	iommu_flush_irt_and_complete(iommu, devid);

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

	iommu_flush_irt_and_complete(iommu, devid);

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

	iommu_flush_irt_and_complete(iommu, devid);
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
	modify_irte_ga(iommu, devid, index, irte);
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
	modify_irte_ga(iommu, devid, index, irte);
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
		modify_irte_ga(iommu, devid, index, irte);
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
	/*
	 * The struct msi_msg.dest_mode_logical is used to set the DM bit
	 * in MSI Message Address Register. For device w/ 2K int-remap support,
	 * this is bit must be set to 1 regardless of the actual destination
	 * mode, which is signified by the IRTE[DM].
	 */
	if (FEATURE_NUM_INT_REMAP_SUP_2K(amd_iommu_efr2))
		msg->arch_addr_lo.dest_mode_logical = true;
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
	iommu->irte_ops->prepare(data->entry, APIC_DELIVERY_MODE_FIXED,
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
	struct iommu_dev_data *dev_data;
	unsigned long max_irqs;
	int i, ret, devid, seg, sbdf;
	int index;

	if (!info)
		return -EINVAL;
	if (nr_irqs > 1 && info->type != X86_IRQ_ALLOC_TYPE_PCI_MSI)
		return -EINVAL;

	sbdf = get_devid(info);
	if (sbdf < 0)
		return -EINVAL;

	seg = PCI_SBDF_TO_SEGID(sbdf);
	devid = PCI_SBDF_TO_DEVID(sbdf);
	iommu = __rlookup_amd_iommu(seg, devid);
	if (!iommu)
		return -EINVAL;

	dev_data = search_dev_data(iommu, devid);
	max_irqs = dev_data ? dev_data->max_irqs : MAX_IRQS_PER_TABLE_512;

	ret = irq_domain_alloc_irqs_parent(domain, virq, nr_irqs, arg);
	if (ret < 0)
		return ret;

	if (info->type == X86_IRQ_ALLOC_TYPE_IOAPIC) {
		struct irq_remap_table *table;

		table = alloc_irq_table(iommu, devid, NULL, max_irqs);
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
					msi_desc_to_pci_dev(info->desc),
					max_irqs);
	} else {
		index = alloc_irq_index(iommu, devid, nr_irqs, false, NULL,
					max_irqs);
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

static void __amd_iommu_update_ga(struct irte_ga *entry, int cpu,
				  bool ga_log_intr)
{
	if (cpu >= 0) {
		entry->lo.fields_vapic.destination =
					APICID_TO_IRTE_DEST_LO(cpu);
		entry->hi.fields.destination =
					APICID_TO_IRTE_DEST_HI(cpu);
		entry->lo.fields_vapic.is_run = true;
		entry->lo.fields_vapic.ga_log_intr = false;
	} else {
		entry->lo.fields_vapic.is_run = false;
		entry->lo.fields_vapic.ga_log_intr = ga_log_intr;
	}
}

/*
 * Update the pCPU information for an IRTE that is configured to post IRQs to
 * a vCPU, without issuing an IOMMU invalidation for the IRTE.
 *
 * If the vCPU is associated with a pCPU (@cpu >= 0), configure the Destination
 * with the pCPU's APIC ID, set IsRun, and clear GALogIntr.  If the vCPU isn't
 * associated with a pCPU (@cpu < 0), clear IsRun and set/clear GALogIntr based
 * on input from the caller (e.g. KVM only requests GALogIntr when the vCPU is
 * blocking and requires a notification wake event).  I.e. treat vCPUs that are
 * associated with a pCPU as running.  This API is intended to be used when a
 * vCPU is scheduled in/out (or stops running for any reason), to do a fast
 * update of IsRun, GALogIntr, and (conditionally) Destination.
 *
 * Per the IOMMU spec, the Destination, IsRun, and GATag fields are not cached
 * and thus don't require an invalidation to ensure the IOMMU consumes fresh
 * information.
 */
int amd_iommu_update_ga(void *data, int cpu, bool ga_log_intr)
{
	struct amd_ir_data *ir_data = (struct amd_ir_data *)data;
	struct irte_ga *entry = (struct irte_ga *) ir_data->entry;

	if (WARN_ON_ONCE(!AMD_IOMMU_GUEST_IR_VAPIC(amd_iommu_guest_ir)))
		return -EINVAL;

	if (!entry || !entry->lo.fields_vapic.guest_mode)
		return 0;

	if (!ir_data->iommu)
		return -ENODEV;

	__amd_iommu_update_ga(entry, cpu, ga_log_intr);

	return __modify_irte_ga(ir_data->iommu, ir_data->irq_2_irte.devid,
				ir_data->irq_2_irte.index, entry);
}
EXPORT_SYMBOL(amd_iommu_update_ga);

int amd_iommu_activate_guest_mode(void *data, int cpu, bool ga_log_intr)
{
	struct amd_ir_data *ir_data = (struct amd_ir_data *)data;
	struct irte_ga *entry = (struct irte_ga *) ir_data->entry;
	u64 valid;

	if (WARN_ON_ONCE(!AMD_IOMMU_GUEST_IR_VAPIC(amd_iommu_guest_ir)))
		return -EINVAL;

	if (!entry)
		return 0;

	valid = entry->lo.fields_vapic.valid;

	entry->lo.val = 0;
	entry->hi.val = 0;

	entry->lo.fields_vapic.valid       = valid;
	entry->lo.fields_vapic.guest_mode  = 1;
	entry->hi.fields.ga_root_ptr       = ir_data->ga_root_ptr;
	entry->hi.fields.vector            = ir_data->ga_vector;
	entry->lo.fields_vapic.ga_tag      = ir_data->ga_tag;

	__amd_iommu_update_ga(entry, cpu, ga_log_intr);

	return modify_irte_ga(ir_data->iommu, ir_data->irq_2_irte.devid,
			      ir_data->irq_2_irte.index, entry);
}
EXPORT_SYMBOL(amd_iommu_activate_guest_mode);

int amd_iommu_deactivate_guest_mode(void *data)
{
	struct amd_ir_data *ir_data = (struct amd_ir_data *)data;
	struct irte_ga *entry = (struct irte_ga *) ir_data->entry;
	struct irq_cfg *cfg = ir_data->cfg;
	u64 valid;

	if (WARN_ON_ONCE(!AMD_IOMMU_GUEST_IR_VAPIC(amd_iommu_guest_ir)))
		return -EINVAL;

	if (!entry || !entry->lo.fields_vapic.guest_mode)
		return 0;

	valid = entry->lo.fields_remap.valid;

	entry->lo.val = 0;
	entry->hi.val = 0;

	entry->lo.fields_remap.valid       = valid;
	entry->lo.fields_remap.dm          = apic->dest_mode_logical;
	entry->lo.fields_remap.int_type    = APIC_DELIVERY_MODE_FIXED;
	entry->hi.fields.vector            = cfg->vector;
	entry->lo.fields_remap.destination =
				APICID_TO_IRTE_DEST_LO(cfg->dest_apicid);
	entry->hi.fields.destination =
				APICID_TO_IRTE_DEST_HI(cfg->dest_apicid);

	return modify_irte_ga(ir_data->iommu, ir_data->irq_2_irte.devid,
			      ir_data->irq_2_irte.index, entry);
}
EXPORT_SYMBOL(amd_iommu_deactivate_guest_mode);

static int amd_ir_set_vcpu_affinity(struct irq_data *data, void *info)
{
	int ret;
	struct amd_iommu_pi_data *pi_data = info;
	struct amd_ir_data *ir_data = data->chip_data;
	struct irq_2_irte *irte_info = &ir_data->irq_2_irte;
	struct iommu_dev_data *dev_data;

	if (WARN_ON_ONCE(!AMD_IOMMU_GUEST_IR_VAPIC(amd_iommu_guest_ir)))
		return -EINVAL;

	if (ir_data->iommu == NULL)
		return -EINVAL;

	dev_data = search_dev_data(ir_data->iommu, irte_info->devid);

	/* Note:
	 * This device has never been set up for guest mode.
	 * we should not modify the IRTE
	 */
	if (!dev_data || !dev_data->use_vapic)
		return -EINVAL;

	ir_data->cfg = irqd_cfg(data);

	if (pi_data) {
		pi_data->ir_data = ir_data;

		ir_data->ga_root_ptr = (pi_data->vapic_addr >> 12);
		ir_data->ga_vector = pi_data->vector;
		ir_data->ga_tag = pi_data->ga_tag;
		if (pi_data->is_guest_mode)
			ret = amd_iommu_activate_guest_mode(ir_data, pi_data->cpu,
							    pi_data->ga_log_intr);
		else
			ret = amd_iommu_deactivate_guest_mode(ir_data);
	} else {
		ret = amd_iommu_deactivate_guest_mode(ir_data);
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
	vector_schedule_cleanup(cfg);

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

static const struct msi_parent_ops amdvi_msi_parent_ops = {
	.supported_flags	= X86_VECTOR_MSI_FLAGS_SUPPORTED | MSI_FLAG_MULTI_PCI_MSI,
	.bus_select_token	= DOMAIN_BUS_AMDVI,
	.bus_select_mask	= MATCH_PCI_MSI,
	.prefix			= "IR-",
	.init_dev_msi_info	= msi_parent_init_dev_msi_info,
};

int amd_iommu_create_irq_domain(struct amd_iommu *iommu)
{
	struct irq_domain_info info = {
		.fwnode		= irq_domain_alloc_named_id_fwnode("AMD-IR", iommu->index),
		.ops		= &amd_ir_domain_ops,
		.domain_flags	= IRQ_DOMAIN_FLAG_ISOLATED_MSI,
		.host_data	= iommu,
		.parent		= arch_get_ir_parent_domain(),
	};

	if (!info.fwnode)
		return -ENOMEM;

	iommu->ir_domain = msi_create_parent_irq_domain(&info, &amdvi_msi_parent_ops);
	if (!iommu->ir_domain) {
		irq_domain_free_fwnode(info.fwnode);
		return -ENOMEM;
	}
	return 0;
}
#endif
