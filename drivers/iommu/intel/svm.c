// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2015 Intel Corporation.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 */

#include <linux/intel-iommu.h>
#include <linux/mmu_notifier.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/intel-svm.h>
#include <linux/rculist.h>
#include <linux/pci.h>
#include <linux/pci-ats.h>
#include <linux/dmar.h>
#include <linux/interrupt.h>
#include <linux/mm_types.h>
#include <linux/xarray.h>
#include <linux/ioasid.h>
#include <asm/page.h>
#include <asm/fpu/api.h>
#include <trace/events/intel_iommu.h>

#include "pasid.h"
#include "perf.h"
#include "../iommu-sva-lib.h"

static irqreturn_t prq_event_thread(int irq, void *d);
static void intel_svm_drain_prq(struct device *dev, u32 pasid);
#define to_intel_svm_dev(handle) container_of(handle, struct intel_svm_dev, sva)

#define PRQ_ORDER 0

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
svm_lookup_device_by_sid(struct intel_svm *svm, u16 sid)
{
	struct intel_svm_dev *sdev = NULL, *t;

	rcu_read_lock();
	list_for_each_entry_rcu(t, &svm->devs, list) {
		if (t->sid == sid) {
			sdev = t;
			break;
		}
	}
	rcu_read_unlock();

	return sdev;
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

	irq = dmar_alloc_hwirq(DMAR_UNITS_SUPPORTED + iommu->seq_id, iommu->node, iommu);
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

static inline bool intel_svm_capable(struct intel_iommu *iommu)
{
	return iommu->flags & VTD_FLAG_SVM_CAPABLE;
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
	    !cap_5lp_support(iommu->cap)) {
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
	struct device_domain_info *info = get_domain_info(sdev->dev);

	if (WARN_ON(!pages))
		return;

	qi_flush_piotlb(sdev->iommu, sdev->did, svm->pasid, address, pages, ih);
	if (info->ats_enabled)
		qi_flush_dev_iotlb_pasid(sdev->iommu, sdev->sid, info->pfsid,
					 svm->pasid, sdev->qdep, address,
					 order_base_2(pages));
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
static void intel_invalidate_range(struct mmu_notifier *mn,
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
	.invalidate_range = intel_invalidate_range,
};

static DEFINE_MUTEX(pasid_mutex);

static int pasid_to_svm_sdev(struct device *dev, unsigned int pasid,
			     struct intel_svm **rsvm,
			     struct intel_svm_dev **rsdev)
{
	struct intel_svm_dev *sdev = NULL;
	struct intel_svm *svm;

	/* The caller should hold the pasid_mutex lock */
	if (WARN_ON(!mutex_is_locked(&pasid_mutex)))
		return -EINVAL;

	if (pasid == INVALID_IOASID || pasid >= PASID_MAX)
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

int intel_svm_bind_gpasid(struct iommu_domain *domain, struct device *dev,
			  struct iommu_gpasid_bind_data *data)
{
	struct intel_iommu *iommu = device_to_iommu(dev, NULL, NULL);
	struct intel_svm_dev *sdev = NULL;
	struct dmar_domain *dmar_domain;
	struct device_domain_info *info;
	struct intel_svm *svm = NULL;
	unsigned long iflags;
	int ret = 0;

	if (WARN_ON(!iommu) || !data)
		return -EINVAL;

	if (data->format != IOMMU_PASID_FORMAT_INTEL_VTD)
		return -EINVAL;

	/* IOMMU core ensures argsz is more than the start of the union */
	if (data->argsz < offsetofend(struct iommu_gpasid_bind_data, vendor.vtd))
		return -EINVAL;

	/* Make sure no undefined flags are used in vendor data */
	if (data->vendor.vtd.flags & ~(IOMMU_SVA_VTD_GPASID_LAST - 1))
		return -EINVAL;

	if (!dev_is_pci(dev))
		return -ENOTSUPP;

	/* VT-d supports devices with full 20 bit PASIDs only */
	if (pci_max_pasids(to_pci_dev(dev)) != PASID_MAX)
		return -EINVAL;

	/*
	 * We only check host PASID range, we have no knowledge to check
	 * guest PASID range.
	 */
	if (data->hpasid <= 0 || data->hpasid >= PASID_MAX)
		return -EINVAL;

	info = get_domain_info(dev);
	if (!info)
		return -EINVAL;

	dmar_domain = to_dmar_domain(domain);

	mutex_lock(&pasid_mutex);
	ret = pasid_to_svm_sdev(dev, data->hpasid, &svm, &sdev);
	if (ret)
		goto out;

	if (sdev) {
		/*
		 * Do not allow multiple bindings of the same device-PASID since
		 * there is only one SL page tables per PASID. We may revisit
		 * once sharing PGD across domains are supported.
		 */
		dev_warn_ratelimited(dev, "Already bound with PASID %u\n",
				     svm->pasid);
		ret = -EBUSY;
		goto out;
	}

	if (!svm) {
		/* We come here when PASID has never been bond to a device. */
		svm = kzalloc(sizeof(*svm), GFP_KERNEL);
		if (!svm) {
			ret = -ENOMEM;
			goto out;
		}
		/* REVISIT: upper layer/VFIO can track host process that bind
		 * the PASID. ioasid_set = mm might be sufficient for vfio to
		 * check pasid VMM ownership. We can drop the following line
		 * once VFIO and IOASID set check is in place.
		 */
		svm->mm = get_task_mm(current);
		svm->pasid = data->hpasid;
		if (data->flags & IOMMU_SVA_GPASID_VAL) {
			svm->gpasid = data->gpasid;
			svm->flags |= SVM_FLAG_GUEST_PASID;
		}
		pasid_private_add(data->hpasid, svm);
		INIT_LIST_HEAD_RCU(&svm->devs);
		mmput(svm->mm);
	}
	sdev = kzalloc(sizeof(*sdev), GFP_KERNEL);
	if (!sdev) {
		ret = -ENOMEM;
		goto out;
	}
	sdev->dev = dev;
	sdev->sid = PCI_DEVID(info->bus, info->devfn);
	sdev->iommu = iommu;

	/* Only count users if device has aux domains */
	if (iommu_dev_feature_enabled(dev, IOMMU_DEV_FEAT_AUX))
		sdev->users = 1;

	/* Set up device context entry for PASID if not enabled already */
	ret = intel_iommu_enable_pasid(iommu, sdev->dev);
	if (ret) {
		dev_err_ratelimited(dev, "Failed to enable PASID capability\n");
		kfree(sdev);
		goto out;
	}

	/*
	 * PASID table is per device for better security. Therefore, for
	 * each bind of a new device even with an existing PASID, we need to
	 * call the nested mode setup function here.
	 */
	spin_lock_irqsave(&iommu->lock, iflags);
	ret = intel_pasid_setup_nested(iommu, dev,
				       (pgd_t *)(uintptr_t)data->gpgd,
				       data->hpasid, &data->vendor.vtd, dmar_domain,
				       data->addr_width);
	spin_unlock_irqrestore(&iommu->lock, iflags);
	if (ret) {
		dev_err_ratelimited(dev, "Failed to set up PASID %llu in nested mode, Err %d\n",
				    data->hpasid, ret);
		/*
		 * PASID entry should be in cleared state if nested mode
		 * set up failed. So we only need to clear IOASID tracking
		 * data such that free call will succeed.
		 */
		kfree(sdev);
		goto out;
	}

	svm->flags |= SVM_FLAG_GUEST_MODE;

	init_rcu_head(&sdev->rcu);
	list_add_rcu(&sdev->list, &svm->devs);
 out:
	if (!IS_ERR_OR_NULL(svm) && list_empty(&svm->devs)) {
		pasid_private_remove(data->hpasid);
		kfree(svm);
	}

	mutex_unlock(&pasid_mutex);
	return ret;
}

int intel_svm_unbind_gpasid(struct device *dev, u32 pasid)
{
	struct intel_iommu *iommu = device_to_iommu(dev, NULL, NULL);
	struct intel_svm_dev *sdev;
	struct intel_svm *svm;
	int ret;

	if (WARN_ON(!iommu))
		return -EINVAL;

	mutex_lock(&pasid_mutex);
	ret = pasid_to_svm_sdev(dev, pasid, &svm, &sdev);
	if (ret)
		goto out;

	if (sdev) {
		if (iommu_dev_feature_enabled(dev, IOMMU_DEV_FEAT_AUX))
			sdev->users--;
		if (!sdev->users) {
			list_del_rcu(&sdev->list);
			intel_pasid_tear_down_entry(iommu, dev,
						    svm->pasid, false);
			intel_svm_drain_prq(dev, svm->pasid);
			kfree_rcu(sdev, rcu);

			if (list_empty(&svm->devs)) {
				/*
				 * We do not free the IOASID here in that
				 * IOMMU driver did not allocate it.
				 * Unlike native SVM, IOASID for guest use was
				 * allocated prior to the bind call.
				 * In any case, if the free call comes before
				 * the unbind, IOMMU driver will get notified
				 * and perform cleanup.
				 */
				pasid_private_remove(pasid);
				kfree(svm);
			}
		}
	}
out:
	mutex_unlock(&pasid_mutex);
	return ret;
}

static void _load_pasid(void *unused)
{
	update_pasid();
}

static void load_pasid(struct mm_struct *mm, u32 pasid)
{
	mutex_lock(&mm->context.lock);

	/* Synchronize with READ_ONCE in update_pasid(). */
	smp_store_release(&mm->pasid, pasid);

	/* Update PASID MSR on all CPUs running the mm's tasks. */
	on_each_cpu_mask(mm_cpumask(mm), _load_pasid, NULL, true);

	mutex_unlock(&mm->context.lock);
}

static int intel_svm_alloc_pasid(struct device *dev, struct mm_struct *mm,
				 unsigned int flags)
{
	ioasid_t max_pasid = dev_is_pci(dev) ?
			pci_max_pasids(to_pci_dev(dev)) : intel_pasid_max_id;

	return iommu_sva_alloc_pasid(mm, PASID_MIN, max_pasid - 1);
}

static void intel_svm_free_pasid(struct mm_struct *mm)
{
	iommu_sva_free_pasid(mm);
}

static struct iommu_sva *intel_svm_bind_mm(struct intel_iommu *iommu,
					   struct device *dev,
					   struct mm_struct *mm,
					   unsigned int flags)
{
	struct device_domain_info *info = get_domain_info(dev);
	unsigned long iflags, sflags;
	struct intel_svm_dev *sdev;
	struct intel_svm *svm;
	int ret = 0;

	svm = pasid_private_find(mm->pasid);
	if (!svm) {
		svm = kzalloc(sizeof(*svm), GFP_KERNEL);
		if (!svm)
			return ERR_PTR(-ENOMEM);

		svm->pasid = mm->pasid;
		svm->mm = mm;
		svm->flags = flags;
		INIT_LIST_HEAD_RCU(&svm->devs);

		if (!(flags & SVM_FLAG_SUPERVISOR_MODE)) {
			svm->notifier.ops = &intel_mmuops;
			ret = mmu_notifier_register(&svm->notifier, mm);
			if (ret) {
				kfree(svm);
				return ERR_PTR(ret);
			}
		}

		ret = pasid_private_add(svm->pasid, svm);
		if (ret) {
			if (svm->notifier.ops)
				mmu_notifier_unregister(&svm->notifier, mm);
			kfree(svm);
			return ERR_PTR(ret);
		}
	}

	/* Find the matching device in svm list */
	sdev = svm_lookup_device_by_dev(svm, dev);
	if (sdev) {
		sdev->users++;
		goto success;
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
	sdev->users = 1;
	sdev->pasid = svm->pasid;
	sdev->sva.dev = dev;
	init_rcu_head(&sdev->rcu);
	if (info->ats_enabled) {
		sdev->dev_iotlb = 1;
		sdev->qdep = info->ats_qdep;
		if (sdev->qdep >= QI_DEV_EIOTLB_MAX_INVS)
			sdev->qdep = 0;
	}

	/* Setup the pasid table: */
	sflags = (flags & SVM_FLAG_SUPERVISOR_MODE) ?
			PASID_FLAG_SUPERVISOR_MODE : 0;
	sflags |= cpu_feature_enabled(X86_FEATURE_LA57) ? PASID_FLAG_FL5LP : 0;
	spin_lock_irqsave(&iommu->lock, iflags);
	ret = intel_pasid_setup_first_level(iommu, dev, mm->pgd, mm->pasid,
					    FLPT_DEFAULT_DID, sflags);
	spin_unlock_irqrestore(&iommu->lock, iflags);

	if (ret)
		goto free_sdev;

	/* The newly allocated pasid is loaded to the mm. */
	if (!(flags & SVM_FLAG_SUPERVISOR_MODE) && list_empty(&svm->devs))
		load_pasid(mm, svm->pasid);

	list_add_rcu(&sdev->list, &svm->devs);
success:
	return &sdev->sva;

free_sdev:
	kfree(sdev);
free_svm:
	if (list_empty(&svm->devs)) {
		if (svm->notifier.ops)
			mmu_notifier_unregister(&svm->notifier, mm);
		pasid_private_remove(mm->pasid);
		kfree(svm);
	}

	return ERR_PTR(ret);
}

/* Caller must hold pasid_mutex */
static int intel_svm_unbind_mm(struct device *dev, u32 pasid)
{
	struct intel_svm_dev *sdev;
	struct intel_iommu *iommu;
	struct intel_svm *svm;
	struct mm_struct *mm;
	int ret = -EINVAL;

	iommu = device_to_iommu(dev, NULL, NULL);
	if (!iommu)
		goto out;

	ret = pasid_to_svm_sdev(dev, pasid, &svm, &sdev);
	if (ret)
		goto out;
	mm = svm->mm;

	if (sdev) {
		sdev->users--;
		if (!sdev->users) {
			list_del_rcu(&sdev->list);
			/* Flush the PASID cache and IOTLB for this device.
			 * Note that we do depend on the hardware *not* using
			 * the PASID any more. Just as we depend on other
			 * devices never using PASIDs that they have no right
			 * to use. We have a *shared* PASID table, because it's
			 * large and has to be physically contiguous. So it's
			 * hard to be as defensive as we might like. */
			intel_pasid_tear_down_entry(iommu, dev,
						    svm->pasid, false);
			intel_svm_drain_prq(dev, svm->pasid);
			kfree_rcu(sdev, rcu);

			if (list_empty(&svm->devs)) {
				intel_svm_free_pasid(mm);
				if (svm->notifier.ops) {
					mmu_notifier_unregister(&svm->notifier, mm);
					/* Clear mm's pasid. */
					load_pasid(mm, PASID_DISABLED);
				}
				pasid_private_remove(svm->pasid);
				/* We mandate that no page faults may be outstanding
				 * for the PASID when intel_svm_unbind_mm() is called.
				 * If that is not obeyed, subtle errors will happen.
				 * Let's make them less subtle... */
				memset(svm, 0x6b, sizeof(*svm));
				kfree(svm);
			}
		}
	}
out:
	return ret;
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

#define PRQ_RING_MASK	((0x1000 << PRQ_ORDER) - 0x20)

static bool is_canonical_address(u64 addr)
{
	int shift = 64 - (__VIRTUAL_MASK_SHIFT + 1);
	long saddr = (long) addr;

	return (((saddr << shift) >> shift) == saddr);
}

/**
 * intel_svm_drain_prq - Drain page requests and responses for a pasid
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
static void intel_svm_drain_prq(struct device *dev, u32 pasid)
{
	struct device_domain_info *info;
	struct dmar_domain *domain;
	struct intel_iommu *iommu;
	struct qi_desc desc[3];
	struct pci_dev *pdev;
	int head, tail;
	u16 sid, did;
	int qdep;

	info = get_domain_info(dev);
	if (WARN_ON(!info || !dev_is_pci(dev)))
		return;

	if (!info->pri_enabled)
		return;

	iommu = info->iommu;
	domain = info->domain;
	pdev = to_pci_dev(dev);
	sid = PCI_DEVID(info->bus, info->devfn);
	did = domain->iommu_did[iommu->seq_id];
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
	struct intel_svm_dev *sdev = NULL;
	struct intel_iommu *iommu = d;
	struct intel_svm *svm = NULL;
	struct page_req_dsc *req;
	int head, tail, handled;
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
			svm = NULL;
			sdev = NULL;
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

		if (!svm || svm->pasid != req->pasid) {
			/*
			 * It can't go away, because the driver is not permitted
			 * to unbind the mm while any page faults are outstanding.
			 */
			svm = pasid_private_find(req->pasid);
			if (IS_ERR_OR_NULL(svm) || (svm->flags & SVM_FLAG_SUPERVISOR_MODE))
				goto bad_req;
		}

		if (!sdev || sdev->sid != req->rid) {
			sdev = svm_lookup_device_by_sid(svm, req->rid);
			if (!sdev)
				goto bad_req;
		}

		sdev->prq_seq_number++;

		/*
		 * If prq is to be handled outside iommu driver via receiver of
		 * the fault notifiers, we skip the page response here.
		 */
		if (intel_svm_prq_report(iommu, sdev->dev, req))
			handle_bad_prq_event(iommu, req, QI_RESP_INVALID);

		trace_prq_report(iommu, sdev->dev, req->qw_0, req->qw_1,
				 req->priv_data[0], req->priv_data[1],
				 sdev->prq_seq_number);
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

struct iommu_sva *intel_svm_bind(struct device *dev, struct mm_struct *mm, void *drvdata)
{
	struct intel_iommu *iommu = device_to_iommu(dev, NULL, NULL);
	unsigned int flags = 0;
	struct iommu_sva *sva;
	int ret;

	if (drvdata)
		flags = *(unsigned int *)drvdata;

	if (flags & SVM_FLAG_SUPERVISOR_MODE) {
		if (!ecap_srs(iommu->ecap)) {
			dev_err(dev, "%s: Supervisor PASID not supported\n",
				iommu->name);
			return ERR_PTR(-EOPNOTSUPP);
		}

		if (mm) {
			dev_err(dev, "%s: Supervisor PASID with user provided mm\n",
				iommu->name);
			return ERR_PTR(-EINVAL);
		}

		mm = &init_mm;
	}

	mutex_lock(&pasid_mutex);
	ret = intel_svm_alloc_pasid(dev, mm, flags);
	if (ret) {
		mutex_unlock(&pasid_mutex);
		return ERR_PTR(ret);
	}

	sva = intel_svm_bind_mm(iommu, dev, mm, flags);
	if (IS_ERR_OR_NULL(sva))
		intel_svm_free_pasid(mm);
	mutex_unlock(&pasid_mutex);

	return sva;
}

void intel_svm_unbind(struct iommu_sva *sva)
{
	struct intel_svm_dev *sdev = to_intel_svm_dev(sva);

	mutex_lock(&pasid_mutex);
	intel_svm_unbind_mm(sdev->dev, sdev->pasid);
	mutex_unlock(&pasid_mutex);
}

u32 intel_svm_get_pasid(struct iommu_sva *sva)
{
	struct intel_svm_dev *sdev;
	u32 pasid;

	mutex_lock(&pasid_mutex);
	sdev = to_intel_svm_dev(sva);
	pasid = sdev->pasid;
	mutex_unlock(&pasid_mutex);

	return pasid;
}

int intel_svm_page_response(struct device *dev,
			    struct iommu_fault_event *evt,
			    struct iommu_page_response *msg)
{
	struct iommu_fault_page_request *prm;
	struct intel_svm_dev *sdev = NULL;
	struct intel_svm *svm = NULL;
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

	mutex_lock(&pasid_mutex);

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

	ret = pasid_to_svm_sdev(dev, prm->pasid, &svm, &sdev);
	if (ret || !sdev) {
		ret = -ENODEV;
		goto out;
	}

	/*
	 * For responses from userspace, need to make sure that the
	 * pasid has been bound to its mm.
	 */
	if (svm->flags & SVM_FLAG_GUEST_MODE) {
		struct mm_struct *mm;

		mm = get_task_mm(current);
		if (!mm) {
			ret = -EINVAL;
			goto out;
		}

		if (mm != svm->mm) {
			ret = -ENODEV;
			mmput(mm);
			goto out;
		}

		mmput(mm);
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
	mutex_unlock(&pasid_mutex);
	return ret;
}
