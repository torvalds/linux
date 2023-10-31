// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2015 Intel Corporation.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 */

#include <linux/mmu_notifier.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/rculist.h>
#include <linux/pci.h>
#include <linux/pci-ats.h>
#include <linux/dmar.h>
#include <linux/interrupt.h>
#include <linux/mm_types.h>
#include <linux/xarray.h>
#include <asm/page.h>
#include <asm/fpu/api.h>

#include "iommu.h"
#include "pasid.h"
#include "perf.h"
#include "../iommu-sva.h"
#include "trace.h"

static irqreturn_t prq_event_thread(int irq, void *d);

static DEFINE_XARRAY_ALLOC(pasid_private_array);
static int pasid_private_add(ioasid_t pasid, void *priv)
{
	return xa_alloc(&pasid_private_array, &pasid, priv,
			XA_LIMIT(pasid, pasid), GFP_ATOMIC);
}

static void pasid_private_remove(ioasid_t pasid)
{
	xa_erase(&pasid_private_array, pasid);
}

static void *pasid_private_find(ioasid_t pasid)
{
	return xa_load(&pasid_private_array, pasid);
}

static struct intel_svm_dev *
svm_lookup_device_by_dev(struct intel_svm *svm, struct device *dev)
{
	struct intel_svm_dev *sdev = NULL, *t;

	rcu_read_lock();
	list_for_each_entry_rcu(t, &svm->devs, list) {
		if (t->dev == dev) {
			sdev = t;
			break;
		}
	}
	rcu_read_unlock();

	return sdev;
}

int intel_svm_enable_prq(struct intel_iommu *iommu)
{
	struct iopf_queue *iopfq;
	struct page *pages;
	int irq, ret;

	pages = alloc_pages(GFP_KERNEL | __GFP_ZERO, PRQ_ORDER);
	if (!pages) {
		pr_warn("IOMMU: %s: Failed to allocate page request queue\n",
			iommu->name);
		return -ENOMEM;
	}
	iommu->prq = page_address(pages);

	irq = dmar_alloc_hwirq(IOMMU_IRQ_ID_OFFSET_PRQ + iommu->seq_id, iommu->node, iommu);
	if (irq <= 0) {
		pr_err("IOMMU: %s: Failed to create IRQ vector for page request queue\n",
		       iommu->name);
		ret = -EINVAL;
		goto free_prq;
	}
	iommu->pr_irq = irq;

	snprintf(iommu->iopfq_name, sizeof(iommu->iopfq_name),
		 "dmar%d-iopfq", iommu->seq_id);
	iopfq = iopf_queue_alloc(iommu->iopfq_name);
	if (!iopfq) {
		pr_err("IOMMU: %s: Failed to allocate iopf queue\n", iommu->name);
		ret = -ENOMEM;
		goto free_hwirq;
	}
	iommu->iopf_queue = iopfq;

	snprintf(iommu->prq_name, sizeof(iommu->prq_name), "dmar%d-prq", iommu->seq_id);

	ret = request_threaded_irq(irq, NULL, prq_event_thread, IRQF_ONESHOT,
				   iommu->prq_name, iommu);
	if (ret) {
		pr_err("IOMMU: %s: Failed to request IRQ for page request queue\n",
		       iommu->name);
		goto free_iopfq;
	}
	dmar_writeq(iommu->reg + DMAR_PQH_REG, 0ULL);
	dmar_writeq(iommu->reg + DMAR_PQT_REG, 0ULL);
	dmar_writeq(iommu->reg + DMAR_PQA_REG, virt_to_phys(iommu->prq) | PRQ_ORDER);

	init_completion(&iommu->prq_complete);

	return 0;

free_iopfq:
	iopf_queue_free(iommu->iopf_queue);
	iommu->iopf_queue = NULL;
free_hwirq:
	dmar_free_hwirq(irq);
	iommu->pr_irq = 0;
free_prq:
	free_pages((unsigned long)iommu->prq, PRQ_ORDER);
	iommu->prq = NULL;

	return ret;
}

int intel_svm_finish_prq(struct intel_iommu *iommu)
{
	dmar_writeq(iommu->reg + DMAR_PQH_REG, 0ULL);
	dmar_writeq(iommu->reg + DMAR_PQT_REG, 0ULL);
	dmar_writeq(iommu->reg + DMAR_PQA_REG, 0ULL);

	if (iommu->pr_irq) {
		free_irq(iommu->pr_irq, iommu);
		dmar_free_hwirq(iommu->pr_irq);
		iommu->pr_irq = 0;
	}

	if (iommu->iopf_queue) {
		iopf_queue_free(iommu->iopf_queue);
		iommu->iopf_queue = NULL;
	}

	free_pages((unsigned long)iommu->prq, PRQ_ORDER);
	iommu->prq = NULL;

	return 0;
}

void intel_svm_check(struct intel_iommu *iommu)
{
	if (!pasid_supported(iommu))
		return;

	if (cpu_feature_enabled(X86_FEATURE_GBPAGES) &&
	    !cap_fl1gp_support(iommu->cap)) {
		pr_err("%s SVM disabled, incompatible 1GB page capability\n",
		       iommu->name);
		return;
	}

	if (cpu_feature_enabled(X86_FEATURE_LA57) &&
	    !cap_fl5lp_support(iommu->cap)) {
		pr_err("%s SVM disabled, incompatible paging mode\n",
		       iommu->name);
		return;
	}

	iommu->flags |= VTD_FLAG_SVM_CAPABLE;
}

static void __flush_svm_range_dev(struct intel_svm *svm,
				  struct intel_svm_dev *sdev,
				  unsigned long address,
				  unsigned long pages, int ih)
{
	struct device_domain_info *info = dev_iommu_priv_get(sdev->dev);

	if (WARN_ON(!pages))
		return;

	qi_flush_piotlb(sdev->iommu, sdev->did, svm->pasid, address, pages, ih);
	if (info->ats_enabled) {
		qi_flush_dev_iotlb_pasid(sdev->iommu, sdev->sid, info->pfsid,
					 svm->pasid, sdev->qdep, address,
					 order_base_2(pages));
		quirk_extra_dev_tlb_flush(info, address, order_base_2(pages),
					  svm->pasid, sdev->qdep);
	}
}

static void intel_flush_svm_range_dev(struct intel_svm *svm,
				      struct intel_svm_dev *sdev,
				      unsigned long address,
				      unsigned long pages, int ih)
{
	unsigned long shift = ilog2(__roundup_pow_of_two(pages));
	unsigned long align = (1ULL << (VTD_PAGE_SHIFT + shift));
	unsigned long start = ALIGN_DOWN(address, align);
	unsigned long end = ALIGN(address + (pages << VTD_PAGE_SHIFT), align);

	while (start < end) {
		__flush_svm_range_dev(svm, sdev, start, align >> VTD_PAGE_SHIFT, ih);
		start += align;
	}
}

static void intel_flush_svm_range(struct intel_svm *svm, unsigned long address,
				unsigned long pages, int ih)
{
	struct intel_svm_dev *sdev;

	rcu_read_lock();
	list_for_each_entry_rcu(sdev, &svm->devs, list)
		intel_flush_svm_range_dev(svm, sdev, address, pages, ih);
	rcu_read_unlock();
}

/* Pages have been freed at this point */
static void intel_arch_invalidate_secondary_tlbs(struct mmu_notifier *mn,
					struct mm_struct *mm,
					unsigned long start, unsigned long end)
{
	struct intel_svm *svm = container_of(mn, struct intel_svm, notifier);

	intel_flush_svm_range(svm, start,
			      (end - start + PAGE_SIZE - 1) >> VTD_PAGE_SHIFT, 0);
}

static void intel_mm_release(struct mmu_notifier *mn, struct mm_struct *mm)
{
	struct intel_svm *svm = container_of(mn, struct intel_svm, notifier);
	struct intel_svm_dev *sdev;

	/* This might end up being called from exit_mmap(), *before* the page
	 * tables are cleared. And __mmu_notifier_release() will delete us from
	 * the list of notifiers so that our invalidate_range() callback doesn't
	 * get called when the page tables are cleared. So we need to protect
	 * against hardware accessing those page tables.
	 *
	 * We do it by clearing the entry in the PASID table and then flushing
	 * the IOTLB and the PASID table caches. This might upset hardware;
	 * perhaps we'll want to point the PASID to a dummy PGD (like the zero
	 * page) so that we end up taking a fault that the hardware really
	 * *has* to handle gracefully without affecting other processes.
	 */
	rcu_read_lock();
	list_for_each_entry_rcu(sdev, &svm->devs, list)
		intel_pasid_tear_down_entry(sdev->iommu, sdev->dev,
					    svm->pasid, true);
	rcu_read_unlock();

}

static const struct mmu_notifier_ops intel_mmuops = {
	.release = intel_mm_release,
	.arch_invalidate_secondary_tlbs = intel_arch_invalidate_secondary_tlbs,
};

static int pasid_to_svm_sdev(struct device *dev, unsigned int pasid,
			     struct intel_svm **rsvm,
			     struct intel_svm_dev **rsdev)
{
	struct intel_svm_dev *sdev = NULL;
	struct intel_svm *svm;

	if (pasid == IOMMU_PASID_INVALID || pasid >= PASID_MAX)
		return -EINVAL;

	svm = pasid_private_find(pasid);
	if (IS_ERR(svm))
		return PTR_ERR(svm);

	if (!svm)
		goto out;

	/*
	 * If we found svm for the PASID, there must be at least one device
	 * bond.
	 */
	if (WARN_ON(list_empty(&svm->devs)))
		return -EINVAL;
	sdev = svm_lookup_device_by_dev(svm, dev);

out:
	*rsvm = svm;
	*rsdev = sdev;

	return 0;
}

static int intel_svm_bind_mm(struct intel_iommu *iommu, struct device *dev,
			     struct mm_struct *mm)
{
	struct device_domain_info *info = dev_iommu_priv_get(dev);
	struct intel_svm_dev *sdev;
	struct intel_svm *svm;
	unsigned long sflags;
	int ret = 0;

	svm = pasid_private_find(mm->pasid);
	if (!svm) {
		svm = kzalloc(sizeof(*svm), GFP_KERNEL);
		if (!svm)
			return -ENOMEM;

		svm->pasid = mm->pasid;
		svm->mm = mm;
		INIT_LIST_HEAD_RCU(&svm->devs);

		svm->notifier.ops = &intel_mmuops;
		ret = mmu_notifier_register(&svm->notifier, mm);
		if (ret) {
			kfree(svm);
			return ret;
		}

		ret = pasid_private_add(svm->pasid, svm);
		if (ret) {
			mmu_notifier_unregister(&svm->notifier, mm);
			kfree(svm);
			return ret;
		}
	}

	sdev = kzalloc(sizeof(*sdev), GFP_KERNEL);
	if (!sdev) {
		ret = -ENOMEM;
		goto free_svm;
	}

	sdev->dev = dev;
	sdev->iommu = iommu;
	sdev->did = FLPT_DEFAULT_DID;
	sdev->sid = PCI_DEVID(info->bus, info->devfn);
	init_rcu_head(&sdev->rcu);
	if (info->ats_enabled) {
		sdev->qdep = info->ats_qdep;
		if (sdev->qdep >= QI_DEV_EIOTLB_MAX_INVS)
			sdev->qdep = 0;
	}

	/* Setup the pasid table: */
	sflags = cpu_feature_enabled(X86_FEATURE_LA57) ? PASID_FLAG_FL5LP : 0;
	ret = intel_pasid_setup_first_level(iommu, dev, mm->pgd, mm->pasid,
					    FLPT_DEFAULT_DID, sflags);
	if (ret)
		goto free_sdev;

	list_add_rcu(&sdev->list, &svm->devs);

	return 0;

free_sdev:
	kfree(sdev);
free_svm:
	if (list_empty(&svm->devs)) {
		mmu_notifier_unregister(&svm->notifier, mm);
		pasid_private_remove(mm->pasid);
		kfree(svm);
	}

	return ret;
}

void intel_svm_remove_dev_pasid(struct device *dev, u32 pasid)
{
	struct intel_svm_dev *sdev;
	struct intel_iommu *iommu;
	struct intel_svm *svm;
	struct mm_struct *mm;

	iommu = device_to_iommu(dev, NULL, NULL);
	if (!iommu)
		return;

	if (pasid_to_svm_sdev(dev, pasid, &svm, &sdev))
		return;
	mm = svm->mm;

	if (sdev) {
		list_del_rcu(&sdev->list);
		kfree_rcu(sdev, rcu);

		if (list_empty(&svm->devs)) {
			if (svm->notifier.ops)
				mmu_notifier_unregister(&svm->notifier, mm);
			pasid_private_remove(svm->pasid);
			/*
			 * We mandate that no page faults may be outstanding
			 * for the PASID when intel_svm_unbind_mm() is called.
			 * If that is not obeyed, subtle errors will happen.
			 * Let's make them less subtle...
			 */
			memset(svm, 0x6b, sizeof(*svm));
			kfree(svm);
		}
	}
}

/* Page request queue descriptor */
struct page_req_dsc {
	union {
		struct {
			u64 type:8;
			u64 pasid_present:1;
			u64 priv_data_present:1;
			u64 rsvd:6;
			u64 rid:16;
			u64 pasid:20;
			u64 exe_req:1;
			u64 pm_req:1;
			u64 rsvd2:10;
		};
		u64 qw_0;
	};
	union {
		struct {
			u64 rd_req:1;
			u64 wr_req:1;
			u64 lpig:1;
			u64 prg_index:9;
			u64 addr:52;
		};
		u64 qw_1;
	};
	u64 priv_data[2];
};

static bool is_canonical_address(u64 addr)
{
	int shift = 64 - (__VIRTUAL_MASK_SHIFT + 1);
	long saddr = (long) addr;

	return (((saddr << shift) >> shift) == saddr);
}

/**
 * intel_drain_pasid_prq - Drain page requests and responses for a pasid
 * @dev: target device
 * @pasid: pasid for draining
 *
 * Drain all pending page requests and responses related to @pasid in both
 * software and hardware. This is supposed to be called after the device
 * driver has stopped DMA, the pasid entry has been cleared, and both IOTLB
 * and DevTLB have been invalidated.
 *
 * It waits until all pending page requests for @pasid in the page fault
 * queue are completed by the prq handling thread. Then follow the steps
 * described in VT-d spec CH7.10 to drain all page requests and page
 * responses pending in the hardware.
 */
void intel_drain_pasid_prq(struct device *dev, u32 pasid)
{
	struct device_domain_info *info;
	struct dmar_domain *domain;
	struct intel_iommu *iommu;
	struct qi_desc desc[3];
	struct pci_dev *pdev;
	int head, tail;
	u16 sid, did;
	int qdep;

	info = dev_iommu_priv_get(dev);
	if (WARN_ON(!info || !dev_is_pci(dev)))
		return;

	if (!info->pri_enabled)
		return;

	iommu = info->iommu;
	domain = info->domain;
	pdev = to_pci_dev(dev);
	sid = PCI_DEVID(info->bus, info->devfn);
	did = domain_id_iommu(domain, iommu);
	qdep = pci_ats_queue_depth(pdev);

	/*
	 * Check and wait until all pending page requests in the queue are
	 * handled by the prq handling thread.
	 */
prq_retry:
	reinit_completion(&iommu->prq_complete);
	tail = dmar_readq(iommu->reg + DMAR_PQT_REG) & PRQ_RING_MASK;
	head = dmar_readq(iommu->reg + DMAR_PQH_REG) & PRQ_RING_MASK;
	while (head != tail) {
		struct page_req_dsc *req;

		req = &iommu->prq[head / sizeof(*req)];
		if (!req->pasid_present || req->pasid != pasid) {
			head = (head + sizeof(*req)) & PRQ_RING_MASK;
			continue;
		}

		wait_for_completion(&iommu->prq_complete);
		goto prq_retry;
	}

	iopf_queue_flush_dev(dev);

	/*
	 * Perform steps described in VT-d spec CH7.10 to drain page
	 * requests and responses in hardware.
	 */
	memset(desc, 0, sizeof(desc));
	desc[0].qw0 = QI_IWD_STATUS_DATA(QI_DONE) |
			QI_IWD_FENCE |
			QI_IWD_TYPE;
	desc[1].qw0 = QI_EIOTLB_PASID(pasid) |
			QI_EIOTLB_DID(did) |
			QI_EIOTLB_GRAN(QI_GRAN_NONG_PASID) |
			QI_EIOTLB_TYPE;
	desc[2].qw0 = QI_DEV_EIOTLB_PASID(pasid) |
			QI_DEV_EIOTLB_SID(sid) |
			QI_DEV_EIOTLB_QDEP(qdep) |
			QI_DEIOTLB_TYPE |
			QI_DEV_IOTLB_PFSID(info->pfsid);
qi_retry:
	reinit_completion(&iommu->prq_complete);
	qi_submit_sync(iommu, desc, 3, QI_OPT_WAIT_DRAIN);
	if (readl(iommu->reg + DMAR_PRS_REG) & DMA_PRS_PRO) {
		wait_for_completion(&iommu->prq_complete);
		goto qi_retry;
	}
}

static int prq_to_iommu_prot(struct page_req_dsc *req)
{
	int prot = 0;

	if (req->rd_req)
		prot |= IOMMU_FAULT_PERM_READ;
	if (req->wr_req)
		prot |= IOMMU_FAULT_PERM_WRITE;
	if (req->exe_req)
		prot |= IOMMU_FAULT_PERM_EXEC;
	if (req->pm_req)
		prot |= IOMMU_FAULT_PERM_PRIV;

	return prot;
}

static int intel_svm_prq_report(struct intel_iommu *iommu, struct device *dev,
				struct page_req_dsc *desc)
{
	struct iommu_fault_event event;

	if (!dev || !dev_is_pci(dev))
		return -ENODEV;

	/* Fill in event data for device specific processing */
	memset(&event, 0, sizeof(struct iommu_fault_event));
	event.fault.type = IOMMU_FAULT_PAGE_REQ;
	event.fault.prm.addr = (u64)desc->addr << VTD_PAGE_SHIFT;
	event.fault.prm.pasid = desc->pasid;
	event.fault.prm.grpid = desc->prg_index;
	event.fault.prm.perm = prq_to_iommu_prot(desc);

	if (desc->lpig)
		event.fault.prm.flags |= IOMMU_FAULT_PAGE_REQUEST_LAST_PAGE;
	if (desc->pasid_present) {
		event.fault.prm.flags |= IOMMU_FAULT_PAGE_REQUEST_PASID_VALID;
		event.fault.prm.flags |= IOMMU_FAULT_PAGE_RESPONSE_NEEDS_PASID;
	}
	if (desc->priv_data_present) {
		/*
		 * Set last page in group bit if private data is present,
		 * page response is required as it does for LPIG.
		 * iommu_report_device_fault() doesn't understand this vendor
		 * specific requirement thus we set last_page as a workaround.
		 */
		event.fault.prm.flags |= IOMMU_FAULT_PAGE_REQUEST_LAST_PAGE;
		event.fault.prm.flags |= IOMMU_FAULT_PAGE_REQUEST_PRIV_DATA;
		event.fault.prm.private_data[0] = desc->priv_data[0];
		event.fault.prm.private_data[1] = desc->priv_data[1];
	} else if (dmar_latency_enabled(iommu, DMAR_LATENCY_PRQ)) {
		/*
		 * If the private data fields are not used by hardware, use it
		 * to monitor the prq handle latency.
		 */
		event.fault.prm.private_data[0] = ktime_to_ns(ktime_get());
	}

	return iommu_report_device_fault(dev, &event);
}

static void handle_bad_prq_event(struct intel_iommu *iommu,
				 struct page_req_dsc *req, int result)
{
	struct qi_desc desc;

	pr_err("%s: Invalid page request: %08llx %08llx\n",
	       iommu->name, ((unsigned long long *)req)[0],
	       ((unsigned long long *)req)[1]);

	/*
	 * Per VT-d spec. v3.0 ch7.7, system software must
	 * respond with page group response if private data
	 * is present (PDP) or last page in group (LPIG) bit
	 * is set. This is an additional VT-d feature beyond
	 * PCI ATS spec.
	 */
	if (!req->lpig && !req->priv_data_present)
		return;

	desc.qw0 = QI_PGRP_PASID(req->pasid) |
			QI_PGRP_DID(req->rid) |
			QI_PGRP_PASID_P(req->pasid_present) |
			QI_PGRP_PDP(req->priv_data_present) |
			QI_PGRP_RESP_CODE(result) |
			QI_PGRP_RESP_TYPE;
	desc.qw1 = QI_PGRP_IDX(req->prg_index) |
			QI_PGRP_LPIG(req->lpig);

	if (req->priv_data_present) {
		desc.qw2 = req->priv_data[0];
		desc.qw3 = req->priv_data[1];
	} else {
		desc.qw2 = 0;
		desc.qw3 = 0;
	}

	qi_submit_sync(iommu, &desc, 1, 0);
}

static irqreturn_t prq_event_thread(int irq, void *d)
{
	struct intel_iommu *iommu = d;
	struct page_req_dsc *req;
	int head, tail, handled;
	struct pci_dev *pdev;
	u64 address;

	/*
	 * Clear PPR bit before reading head/tail registers, to ensure that
	 * we get a new interrupt if needed.
	 */
	writel(DMA_PRS_PPR, iommu->reg + DMAR_PRS_REG);

	tail = dmar_readq(iommu->reg + DMAR_PQT_REG) & PRQ_RING_MASK;
	head = dmar_readq(iommu->reg + DMAR_PQH_REG) & PRQ_RING_MASK;
	handled = (head != tail);
	while (head != tail) {
		req = &iommu->prq[head / sizeof(*req)];
		address = (u64)req->addr << VTD_PAGE_SHIFT;

		if (unlikely(!req->pasid_present)) {
			pr_err("IOMMU: %s: Page request without PASID\n",
			       iommu->name);
bad_req:
			handle_bad_prq_event(iommu, req, QI_RESP_INVALID);
			goto prq_advance;
		}

		if (unlikely(!is_canonical_address(address))) {
			pr_err("IOMMU: %s: Address is not canonical\n",
			       iommu->name);
			goto bad_req;
		}

		if (unlikely(req->pm_req && (req->rd_req | req->wr_req))) {
			pr_err("IOMMU: %s: Page request in Privilege Mode\n",
			       iommu->name);
			goto bad_req;
		}

		if (unlikely(req->exe_req && req->rd_req)) {
			pr_err("IOMMU: %s: Execution request not supported\n",
			       iommu->name);
			goto bad_req;
		}

		/* Drop Stop Marker message. No need for a response. */
		if (unlikely(req->lpig && !req->rd_req && !req->wr_req))
			goto prq_advance;

		pdev = pci_get_domain_bus_and_slot(iommu->segment,
						   PCI_BUS_NUM(req->rid),
						   req->rid & 0xff);
		/*
		 * If prq is to be handled outside iommu driver via receiver of
		 * the fault notifiers, we skip the page response here.
		 */
		if (!pdev)
			goto bad_req;

		if (intel_svm_prq_report(iommu, &pdev->dev, req))
			handle_bad_prq_event(iommu, req, QI_RESP_INVALID);
		else
			trace_prq_report(iommu, &pdev->dev, req->qw_0, req->qw_1,
					 req->priv_data[0], req->priv_data[1],
					 iommu->prq_seq_number++);
		pci_dev_put(pdev);
prq_advance:
		head = (head + sizeof(*req)) & PRQ_RING_MASK;
	}

	dmar_writeq(iommu->reg + DMAR_PQH_REG, tail);

	/*
	 * Clear the page request overflow bit and wake up all threads that
	 * are waiting for the completion of this handling.
	 */
	if (readl(iommu->reg + DMAR_PRS_REG) & DMA_PRS_PRO) {
		pr_info_ratelimited("IOMMU: %s: PRQ overflow detected\n",
				    iommu->name);
		head = dmar_readq(iommu->reg + DMAR_PQH_REG) & PRQ_RING_MASK;
		tail = dmar_readq(iommu->reg + DMAR_PQT_REG) & PRQ_RING_MASK;
		if (head == tail) {
			iopf_queue_discard_partial(iommu->iopf_queue);
			writel(DMA_PRS_PRO, iommu->reg + DMAR_PRS_REG);
			pr_info_ratelimited("IOMMU: %s: PRQ overflow cleared",
					    iommu->name);
		}
	}

	if (!completion_done(&iommu->prq_complete))
		complete(&iommu->prq_complete);

	return IRQ_RETVAL(handled);
}

int intel_svm_page_response(struct device *dev,
			    struct iommu_fault_event *evt,
			    struct iommu_page_response *msg)
{
	struct iommu_fault_page_request *prm;
	struct intel_iommu *iommu;
	bool private_present;
	bool pasid_present;
	bool last_page;
	u8 bus, devfn;
	int ret = 0;
	u16 sid;

	if (!dev || !dev_is_pci(dev))
		return -ENODEV;

	iommu = device_to_iommu(dev, &bus, &devfn);
	if (!iommu)
		return -ENODEV;

	if (!msg || !evt)
		return -EINVAL;

	prm = &evt->fault.prm;
	sid = PCI_DEVID(bus, devfn);
	pasid_present = prm->flags & IOMMU_FAULT_PAGE_REQUEST_PASID_VALID;
	private_present = prm->flags & IOMMU_FAULT_PAGE_REQUEST_PRIV_DATA;
	last_page = prm->flags & IOMMU_FAULT_PAGE_REQUEST_LAST_PAGE;

	if (!pasid_present) {
		ret = -EINVAL;
		goto out;
	}

	if (prm->pasid == 0 || prm->pasid >= PASID_MAX) {
		ret = -EINVAL;
		goto out;
	}

	/*
	 * Per VT-d spec. v3.0 ch7.7, system software must respond
	 * with page group response if private data is present (PDP)
	 * or last page in group (LPIG) bit is set. This is an
	 * additional VT-d requirement beyond PCI ATS spec.
	 */
	if (last_page || private_present) {
		struct qi_desc desc;

		desc.qw0 = QI_PGRP_PASID(prm->pasid) | QI_PGRP_DID(sid) |
				QI_PGRP_PASID_P(pasid_present) |
				QI_PGRP_PDP(private_present) |
				QI_PGRP_RESP_CODE(msg->code) |
				QI_PGRP_RESP_TYPE;
		desc.qw1 = QI_PGRP_IDX(prm->grpid) | QI_PGRP_LPIG(last_page);
		desc.qw2 = 0;
		desc.qw3 = 0;

		if (private_present) {
			desc.qw2 = prm->private_data[0];
			desc.qw3 = prm->private_data[1];
		} else if (prm->private_data[0]) {
			dmar_latency_update(iommu, DMAR_LATENCY_PRQ,
				ktime_to_ns(ktime_get()) - prm->private_data[0]);
		}

		qi_submit_sync(iommu, &desc, 1, 0);
	}
out:
	return ret;
}

static int intel_svm_set_dev_pasid(struct iommu_domain *domain,
				   struct device *dev, ioasid_t pasid)
{
	struct device_domain_info *info = dev_iommu_priv_get(dev);
	struct intel_iommu *iommu = info->iommu;
	struct mm_struct *mm = domain->mm;

	return intel_svm_bind_mm(iommu, dev, mm);
}

static void intel_svm_domain_free(struct iommu_domain *domain)
{
	kfree(to_dmar_domain(domain));
}

static const struct iommu_domain_ops intel_svm_domain_ops = {
	.set_dev_pasid		= intel_svm_set_dev_pasid,
	.free			= intel_svm_domain_free
};

struct iommu_domain *intel_svm_domain_alloc(void)
{
	struct dmar_domain *domain;

	domain = kzalloc(sizeof(*domain), GFP_KERNEL);
	if (!domain)
		return NULL;
	domain->domain.ops = &intel_svm_domain_ops;

	return &domain->domain;
}
