// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Advanced Micro Devices, Inc.
 */

#define pr_fmt(fmt)     "AMD-Vi: " fmt
#define dev_fmt(fmt)    pr_fmt(fmt)

#include <linux/amd-iommu.h>
#include <linux/delay.h>
#include <linux/mmu_notifier.h>

#include <asm/iommu.h>

#include "amd_iommu.h"
#include "amd_iommu_types.h"

#include "../iommu-pages.h"

int __init amd_iommu_alloc_ppr_log(struct amd_iommu *iommu)
{
	iommu->ppr_log = iommu_alloc_4k_pages(iommu, GFP_KERNEL | __GFP_ZERO,
					      PPR_LOG_SIZE);
	return iommu->ppr_log ? 0 : -ENOMEM;
}

void amd_iommu_enable_ppr_log(struct amd_iommu *iommu)
{
	u64 entry;

	if (iommu->ppr_log == NULL)
		return;

	iommu_feature_enable(iommu, CONTROL_PPR_EN);

	entry = iommu_virt_to_phys(iommu->ppr_log) | PPR_LOG_SIZE_512;

	memcpy_toio(iommu->mmio_base + MMIO_PPR_LOG_OFFSET,
		    &entry, sizeof(entry));

	/* set head and tail to zero manually */
	writel(0x00, iommu->mmio_base + MMIO_PPR_HEAD_OFFSET);
	writel(0x00, iommu->mmio_base + MMIO_PPR_TAIL_OFFSET);

	iommu_feature_enable(iommu, CONTROL_PPRINT_EN);
	iommu_feature_enable(iommu, CONTROL_PPRLOG_EN);
}

void __init amd_iommu_free_ppr_log(struct amd_iommu *iommu)
{
	iommu_free_pages(iommu->ppr_log, get_order(PPR_LOG_SIZE));
}

/*
 * This function restarts ppr logging in case the IOMMU experienced
 * PPR log overflow.
 */
void amd_iommu_restart_ppr_log(struct amd_iommu *iommu)
{
	amd_iommu_restart_log(iommu, "PPR", CONTROL_PPRINT_EN,
			      CONTROL_PPRLOG_EN, MMIO_STATUS_PPR_RUN_MASK,
			      MMIO_STATUS_PPR_OVERFLOW_MASK);
}

static inline u32 ppr_flag_to_fault_perm(u16 flag)
{
	int perm = 0;

	if (flag & PPR_FLAG_READ)
		perm |= IOMMU_FAULT_PERM_READ;
	if (flag & PPR_FLAG_WRITE)
		perm |= IOMMU_FAULT_PERM_WRITE;
	if (flag & PPR_FLAG_EXEC)
		perm |= IOMMU_FAULT_PERM_EXEC;
	if (!(flag & PPR_FLAG_US))
		perm |= IOMMU_FAULT_PERM_PRIV;

	return perm;
}

static bool ppr_is_valid(struct amd_iommu *iommu, u64 *raw)
{
	struct device *dev = iommu->iommu.dev;
	u16 devid = PPR_DEVID(raw[0]);

	if (!(PPR_FLAGS(raw[0]) & PPR_FLAG_GN)) {
		dev_dbg(dev, "PPR logged [Request ignored due to GN=0 (device=%04x:%02x:%02x.%x "
			"pasid=0x%05llx address=0x%llx flags=0x%04llx tag=0x%03llx]\n",
			iommu->pci_seg->id, PCI_BUS_NUM(devid), PCI_SLOT(devid), PCI_FUNC(devid),
			PPR_PASID(raw[0]), raw[1], PPR_FLAGS(raw[0]), PPR_TAG(raw[0]));
		return false;
	}

	if (PPR_FLAGS(raw[0]) & PPR_FLAG_RVSD) {
		dev_dbg(dev, "PPR logged [Invalid request format (device=%04x:%02x:%02x.%x "
			"pasid=0x%05llx address=0x%llx flags=0x%04llx tag=0x%03llx]\n",
			iommu->pci_seg->id, PCI_BUS_NUM(devid), PCI_SLOT(devid), PCI_FUNC(devid),
			PPR_PASID(raw[0]), raw[1], PPR_FLAGS(raw[0]), PPR_TAG(raw[0]));
		return false;
	}

	return true;
}

static void iommu_call_iopf_notifier(struct amd_iommu *iommu, u64 *raw)
{
	struct iommu_dev_data *dev_data;
	struct iopf_fault event;
	struct pci_dev *pdev;
	u16 devid = PPR_DEVID(raw[0]);

	if (PPR_REQ_TYPE(raw[0]) != PPR_REQ_FAULT) {
		pr_info_ratelimited("Unknown PPR request received\n");
		return;
	}

	pdev = pci_get_domain_bus_and_slot(iommu->pci_seg->id,
					   PCI_BUS_NUM(devid), devid & 0xff);
	if (!pdev)
		return;

	if (!ppr_is_valid(iommu, raw))
		goto out;

	memset(&event, 0, sizeof(struct iopf_fault));

	event.fault.type = IOMMU_FAULT_PAGE_REQ;
	event.fault.prm.perm = ppr_flag_to_fault_perm(PPR_FLAGS(raw[0]));
	event.fault.prm.addr = (u64)(raw[1] & PAGE_MASK);
	event.fault.prm.pasid = PPR_PASID(raw[0]);
	event.fault.prm.grpid = PPR_TAG(raw[0]) & 0x1FF;

	/*
	 * PASID zero is used for requests from the I/O device without
	 * a PASID
	 */
	dev_data = dev_iommu_priv_get(&pdev->dev);
	if (event.fault.prm.pasid == 0 ||
	    event.fault.prm.pasid >= dev_data->max_pasids) {
		pr_info_ratelimited("Invalid PASID : 0x%x, device : 0x%x\n",
				    event.fault.prm.pasid, pdev->dev.id);
		goto out;
	}

	event.fault.prm.flags |= IOMMU_FAULT_PAGE_RESPONSE_NEEDS_PASID;
	event.fault.prm.flags |= IOMMU_FAULT_PAGE_REQUEST_PASID_VALID;
	if (PPR_TAG(raw[0]) & 0x200)
		event.fault.prm.flags |= IOMMU_FAULT_PAGE_REQUEST_LAST_PAGE;

	/* Submit event */
	iommu_report_device_fault(&pdev->dev, &event);

	return;

out:
	/* Nobody cared, abort */
	amd_iommu_complete_ppr(&pdev->dev, PPR_PASID(raw[0]),
			       IOMMU_PAGE_RESP_FAILURE,
			       PPR_TAG(raw[0]) & 0x1FF);
}

void amd_iommu_poll_ppr_log(struct amd_iommu *iommu)
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
		 * To detect the hardware errata 733 we need to clear the
		 * entry back to zero. This issue does not exist on SNP
		 * enabled system. Also this buffer is not writeable on
		 * SNP enabled system.
		 */
		if (!amd_iommu_snp_en)
			raw[0] = raw[1] = 0UL;

		/* Update head pointer of hardware ring-buffer */
		head = (head + PPR_ENTRY_SIZE) % PPR_LOG_SIZE;
		writel(head, iommu->mmio_base + MMIO_PPR_HEAD_OFFSET);

		/* Handle PPR entry */
		iommu_call_iopf_notifier(iommu, entry);
	}
}

/**************************************************************
 *
 * IOPF handling stuff
 */

/* Setup per-IOMMU IOPF queue if not exist. */
int amd_iommu_iopf_init(struct amd_iommu *iommu)
{
	int ret = 0;

	if (iommu->iopf_queue)
		return ret;

	snprintf(iommu->iopfq_name, sizeof(iommu->iopfq_name),
		 "amdiommu-%#x-iopfq",
		 PCI_SEG_DEVID_TO_SBDF(iommu->pci_seg->id, iommu->devid));

	iommu->iopf_queue = iopf_queue_alloc(iommu->iopfq_name);
	if (!iommu->iopf_queue)
		ret = -ENOMEM;

	return ret;
}

/* Destroy per-IOMMU IOPF queue if no longer needed. */
void amd_iommu_iopf_uninit(struct amd_iommu *iommu)
{
	iopf_queue_free(iommu->iopf_queue);
	iommu->iopf_queue = NULL;
}

void amd_iommu_page_response(struct device *dev, struct iopf_fault *evt,
			     struct iommu_page_response *resp)
{
	amd_iommu_complete_ppr(dev, resp->pasid, resp->code, resp->grpid);
}

int amd_iommu_iopf_add_device(struct amd_iommu *iommu,
			      struct iommu_dev_data *dev_data)
{
	unsigned long flags;
	int ret = 0;

	if (!dev_data->pri_enabled)
		return ret;

	raw_spin_lock_irqsave(&iommu->lock, flags);

	if (!iommu->iopf_queue) {
		ret = -EINVAL;
		goto out_unlock;
	}

	ret = iopf_queue_add_device(iommu->iopf_queue, dev_data->dev);
	if (ret)
		goto out_unlock;

	dev_data->ppr = true;

out_unlock:
	raw_spin_unlock_irqrestore(&iommu->lock, flags);
	return ret;
}

/* Its assumed that caller has verified that device was added to iopf queue */
void amd_iommu_iopf_remove_device(struct amd_iommu *iommu,
				  struct iommu_dev_data *dev_data)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&iommu->lock, flags);

	iopf_queue_remove_device(iommu->iopf_queue, dev_data->dev);
	dev_data->ppr = false;

	raw_spin_unlock_irqrestore(&iommu->lock, flags);
}
