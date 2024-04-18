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
	free_pages((unsigned long)iommu->ppr_log, get_order(PPR_LOG_SIZE));
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

		/* TODO: PPR Handler will be added when we add IOPF support */
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
