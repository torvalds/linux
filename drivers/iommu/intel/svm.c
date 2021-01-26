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
#include <linux/ioasid.h>
#include <asm/page.h>
#include <asm/fpu/api.h>

#include "pasid.h"

static irqreturn_t prq_event_thread(int irq, void *d);
static void intel_svm_drain_prq(struct device *dev, u32 pasid);

#define PRQ_ORDER 0

int intel_svm_enable_prq(struct intel_iommu *iommu)
{
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
	err:
		free_pages((unsigned long)iommu->prq, PRQ_ORDER);
		iommu->prq = NULL;
		return ret;
	}
	iommu->pr_irq = irq;

	snprintf(iommu->prq_name, sizeof(iommu->prq_name), "dmar%d-prq", iommu->seq_id);

	ret = request_threaded_irq(irq, NULL, prq_event_thread, IRQF_ONESHOT,
				   iommu->prq_name, iommu);
	if (ret) {
		pr_err("IOMMU: %s: Failed to request IRQ for page request queue\n",
		       iommu->name);
		dmar_free_hwirq(irq);
		iommu->pr_irq = 0;
		goto err;
	}
	dmar_writeq(iommu->reg + DMAR_PQH_REG, 0ULL);
	dmar_writeq(iommu->reg + DMAR_PQT_REG, 0ULL);
	dmar_writeq(iommu->reg + DMAR_PQA_REG, virt_to_phys(iommu->prq) | PRQ_ORDER);

	init_completion(&iommu->prq_complete);

	return 0;
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
static LIST_HEAD(global_svm_list);

#define for_each_svm_dev(sdev, svm, d)			\
	list_for_each_entry((sdev), &(svm)->devs, list)	\
		if ((d) != (sdev)->dev) {} else

static int pasid_to_svm_sdev(struct device *dev, unsigned int pasid,
			     struct intel_svm **rsvm,
			     struct intel_svm_dev **rsdev)
{
	struct intel_svm_dev *d, *sdev = NULL;
	struct intel_svm *svm;

	/* The caller should hold the pasid_mutex lock */
	if (WARN_ON(!mutex_is_locked(&pasid_mutex)))
		return -EINVAL;

	if (pasid == INVALID_IOASID || pasid >= PASID_MAX)
		return -EINVAL;

	svm = ioasid_find(NULL, pasid, NULL);
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

	rcu_read_lock();
	list_for_each_entry_rcu(d, &svm->devs, list) {
		if (d->dev == dev) {
			sdev = d;
			break;
		}
	}
	rcu_read_unlock();

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
		ioasid_set_data(data->hpasid, svm);
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
		ioasid_set_data(data->hpasid, NULL);
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
				ioasid_set_data(pasid, NULL);
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

/* Caller must hold pasid_mutex, mm reference */
static int
intel_svm_bind_mm(struct device *dev, unsigned int flags,
		  struct svm_dev_ops *ops,
		  struct mm_struct *mm, struct intel_svm_dev **sd)
{
	struct intel_iommu *iommu = device_to_iommu(dev, NULL, NULL);
	struct device_domain_info *info;
	struct intel_svm_dev *sdev;
	struct intel_svm *svm = NULL;
	unsigned long iflags;
	int pasid_max;
	int ret;

	if (!iommu || dmar_disabled)
		return -EINVAL;

	if (!intel_svm_capable(iommu))
		return -ENOTSUPP;

	if (dev_is_pci(dev)) {
		pasid_max = pci_max_pasids(to_pci_dev(dev));
		if (pasid_max < 0)
			return -EINVAL;
	} else
		pasid_max = 1 << 20;

	/* Bind supervisor PASID shuld have mm = NULL */
	if (flags & SVM_FLAG_SUPERVISOR_MODE) {
		if (!ecap_srs(iommu->ecap) || mm) {
			pr_err("Supervisor PASID with user provided mm.\n");
			return -EINVAL;
		}
	}

	if (!(flags & SVM_FLAG_PRIVATE_PASID)) {
		struct intel_svm *t;

		list_for_each_entry(t, &global_svm_list, list) {
			if (t->mm != mm || (t->flags & SVM_FLAG_PRIVATE_PASID))
				continue;

			svm = t;
			if (svm->pasid >= pasid_max) {
				dev_warn(dev,
					 "Limited PASID width. Cannot use existing PASID %d\n",
					 svm->pasid);
				ret = -ENOSPC;
				goto out;
			}

			/* Find the matching device in svm list */
			for_each_svm_dev(sdev, svm, dev) {
				if (sdev->ops != ops) {
					ret = -EBUSY;
					goto out;
				}
				sdev->users++;
				goto success;
			}

			break;
		}
	}

	sdev = kzalloc(sizeof(*sdev), GFP_KERNEL);
	if (!sdev) {
		ret = -ENOMEM;
		goto out;
	}
	sdev->dev = dev;
	sdev->iommu = iommu;

	ret = intel_iommu_enable_pasid(iommu, dev);
	if (ret) {
		kfree(sdev);
		goto out;
	}

	info = get_domain_info(dev);
	sdev->did = FLPT_DEFAULT_DID;
	sdev->sid = PCI_DEVID(info->bus, info->devfn);
	if (info->ats_enabled) {
		sdev->dev_iotlb = 1;
		sdev->qdep = info->ats_qdep;
		if (sdev->qdep >= QI_DEV_EIOTLB_MAX_INVS)
			sdev->qdep = 0;
	}

	/* Finish the setup now we know we're keeping it */
	sdev->users = 1;
	sdev->ops = ops;
	init_rcu_head(&sdev->rcu);

	if (!svm) {
		svm = kzalloc(sizeof(*svm), GFP_KERNEL);
		if (!svm) {
			ret = -ENOMEM;
			kfree(sdev);
			goto out;
		}

		if (pasid_max > intel_pasid_max_id)
			pasid_max = intel_pasid_max_id;

		/* Do not use PASID 0, reserved for RID to PASID */
		svm->pasid = ioasid_alloc(NULL, PASID_MIN,
					  pasid_max - 1, svm);
		if (svm->pasid == INVALID_IOASID) {
			kfree(svm);
			kfree(sdev);
			ret = -ENOSPC;
			goto out;
		}
		svm->notifier.ops = &intel_mmuops;
		svm->mm = mm;
		svm->flags = flags;
		INIT_LIST_HEAD_RCU(&svm->devs);
		INIT_LIST_HEAD(&svm->list);
		ret = -ENOMEM;
		if (mm) {
			ret = mmu_notifier_register(&svm->notifier, mm);
			if (ret) {
				ioasid_put(svm->pasid);
				kfree(svm);
				kfree(sdev);
				goto out;
			}
		}

		spin_lock_irqsave(&iommu->lock, iflags);
		ret = intel_pasid_setup_first_level(iommu, dev,
				mm ? mm->pgd : init_mm.pgd,
				svm->pasid, FLPT_DEFAULT_DID,
				(mm ? 0 : PASID_FLAG_SUPERVISOR_MODE) |
				(cpu_feature_enabled(X86_FEATURE_LA57) ?
				 PASID_FLAG_FL5LP : 0));
		spin_unlock_irqrestore(&iommu->lock, iflags);
		if (ret) {
			if (mm)
				mmu_notifier_unregister(&svm->notifier, mm);
			ioasid_put(svm->pasid);
			kfree(svm);
			kfree(sdev);
			goto out;
		}

		list_add_tail(&svm->list, &global_svm_list);
		if (mm) {
			/* The newly allocated pasid is loaded to the mm. */
			load_pasid(mm, svm->pasid);
		}
	} else {
		/*
		 * Binding a new device with existing PASID, need to setup
		 * the PASID entry.
		 */
		spin_lock_irqsave(&iommu->lock, iflags);
		ret = intel_pasid_setup_first_level(iommu, dev,
						mm ? mm->pgd : init_mm.pgd,
						svm->pasid, FLPT_DEFAULT_DID,
						(mm ? 0 : PASID_FLAG_SUPERVISOR_MODE) |
						(cpu_feature_enabled(X86_FEATURE_LA57) ?
						PASID_FLAG_FL5LP : 0));
		spin_unlock_irqrestore(&iommu->lock, iflags);
		if (ret) {
			kfree(sdev);
			goto out;
		}
	}
	list_add_rcu(&sdev->list, &svm->devs);
success:
	sdev->pasid = svm->pasid;
	sdev->sva.dev = dev;
	if (sd)
		*sd = sdev;
	ret = 0;
out:
	return ret;
}

/* Caller must hold pasid_mutex */
static int intel_svm_unbind_mm(struct device *dev, u32 pasid)
{
	struct intel_svm_dev *sdev;
	struct intel_iommu *iommu;
	struct intel_svm *svm;
	int ret = -EINVAL;

	iommu = device_to_iommu(dev, NULL, NULL);
	if (!iommu)
		goto out;

	ret = pasid_to_svm_sdev(dev, pasid, &svm, &sdev);
	if (ret)
		goto out;

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
				ioasid_put(svm->pasid);
				if (svm->mm) {
					mmu_notifier_unregister(&svm->notifier, svm->mm);
					/* Clear mm's pasid. */
					load_pasid(svm->mm, PASID_DISABLED);
				}
				list_del(&svm->list);
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

static bool access_error(struct vm_area_struct *vma, struct page_req_dsc *req)
{
	unsigned long requested = 0;

	if (req->exe_req)
		requested |= VM_EXEC;

	if (req->rd_req)
		requested |= VM_READ;

	if (req->wr_req)
		requested |= VM_WRITE;

	return (requested & ~vma->vm_flags) != 0;
}

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

static int
intel_svm_prq_report(struct device *dev, struct page_req_dsc *desc)
{
	struct iommu_fault_event event;

	if (!dev || !dev_is_pci(dev))
		return -ENODEV;

	/* Fill in event data for device specific processing */
	memset(&event, 0, sizeof(struct iommu_fault_event));
	event.fault.type = IOMMU_FAULT_PAGE_REQ;
	event.fault.prm.addr = desc->addr;
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
		memcpy(event.fault.prm.private_data, desc->priv_data,
		       sizeof(desc->priv_data));
	}

	return iommu_report_device_fault(dev, &event);
}

static irqreturn_t prq_event_thread(int irq, void *d)
{
	struct intel_svm_dev *sdev = NULL;
	struct intel_iommu *iommu = d;
	struct intel_svm *svm = NULL;
	int head, tail, handled = 0;

	/* Clear PPR bit before reading head/tail registers, to
	 * ensure that we get a new interrupt if needed. */
	writel(DMA_PRS_PPR, iommu->reg + DMAR_PRS_REG);

	tail = dmar_readq(iommu->reg + DMAR_PQT_REG) & PRQ_RING_MASK;
	head = dmar_readq(iommu->reg + DMAR_PQH_REG) & PRQ_RING_MASK;
	while (head != tail) {
		struct vm_area_struct *vma;
		struct page_req_dsc *req;
		struct qi_desc resp;
		int result;
		vm_fault_t ret;
		u64 address;

		handled = 1;
		req = &iommu->prq[head / sizeof(*req)];
		result = QI_RESP_INVALID;
		address = (u64)req->addr << VTD_PAGE_SHIFT;
		if (!req->pasid_present) {
			pr_err("%s: Page request without PASID: %08llx %08llx\n",
			       iommu->name, ((unsigned long long *)req)[0],
			       ((unsigned long long *)req)[1]);
			goto no_pasid;
		}

		if (!svm || svm->pasid != req->pasid) {
			rcu_read_lock();
			svm = ioasid_find(NULL, req->pasid, NULL);
			/* It *can't* go away, because the driver is not permitted
			 * to unbind the mm while any page faults are outstanding.
			 * So we only need RCU to protect the internal idr code. */
			rcu_read_unlock();
			if (IS_ERR_OR_NULL(svm)) {
				pr_err("%s: Page request for invalid PASID %d: %08llx %08llx\n",
				       iommu->name, req->pasid, ((unsigned long long *)req)[0],
				       ((unsigned long long *)req)[1]);
				goto no_pasid;
			}
		}

		if (!sdev || sdev->sid != req->rid) {
			struct intel_svm_dev *t;

			sdev = NULL;
			rcu_read_lock();
			list_for_each_entry_rcu(t, &svm->devs, list) {
				if (t->sid == req->rid) {
					sdev = t;
					break;
				}
			}
			rcu_read_unlock();
		}

		/* Since we're using init_mm.pgd directly, we should never take
		 * any faults on kernel addresses. */
		if (!svm->mm)
			goto bad_req;

		/* If address is not canonical, return invalid response */
		if (!is_canonical_address(address))
			goto bad_req;

		/*
		 * If prq is to be handled outside iommu driver via receiver of
		 * the fault notifiers, we skip the page response here.
		 */
		if (svm->flags & SVM_FLAG_GUEST_MODE) {
			if (sdev && !intel_svm_prq_report(sdev->dev, req))
				goto prq_advance;
			else
				goto bad_req;
		}

		/* If the mm is already defunct, don't handle faults. */
		if (!mmget_not_zero(svm->mm))
			goto bad_req;

		mmap_read_lock(svm->mm);
		vma = find_extend_vma(svm->mm, address);
		if (!vma || address < vma->vm_start)
			goto invalid;

		if (access_error(vma, req))
			goto invalid;

		ret = handle_mm_fault(vma, address,
				      req->wr_req ? FAULT_FLAG_WRITE : 0,
				      NULL);
		if (ret & VM_FAULT_ERROR)
			goto invalid;

		result = QI_RESP_SUCCESS;
invalid:
		mmap_read_unlock(svm->mm);
		mmput(svm->mm);
bad_req:
		WARN_ON(!sdev);
		if (sdev && sdev->ops && sdev->ops->fault_cb) {
			int rwxp = (req->rd_req << 3) | (req->wr_req << 2) |
				(req->exe_req << 1) | (req->pm_req);
			sdev->ops->fault_cb(sdev->dev, req->pasid, req->addr,
					    req->priv_data, rwxp, result);
		}
		/* We get here in the error case where the PASID lookup failed,
		   and these can be NULL. Do not use them below this point! */
		sdev = NULL;
		svm = NULL;
no_pasid:
		if (req->lpig || req->priv_data_present) {
			/*
			 * Per VT-d spec. v3.0 ch7.7, system software must
			 * respond with page group response if private data
			 * is present (PDP) or last page in group (LPIG) bit
			 * is set. This is an additional VT-d feature beyond
			 * PCI ATS spec.
			 */
			resp.qw0 = QI_PGRP_PASID(req->pasid) |
				QI_PGRP_DID(req->rid) |
				QI_PGRP_PASID_P(req->pasid_present) |
				QI_PGRP_PDP(req->priv_data_present) |
				QI_PGRP_RESP_CODE(result) |
				QI_PGRP_RESP_TYPE;
			resp.qw1 = QI_PGRP_IDX(req->prg_index) |
				QI_PGRP_LPIG(req->lpig);

			if (req->priv_data_present)
				memcpy(&resp.qw2, req->priv_data,
				       sizeof(req->priv_data));
			resp.qw2 = 0;
			resp.qw3 = 0;
			qi_submit_sync(iommu, &resp, 1, 0);
		}
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
			writel(DMA_PRS_PRO, iommu->reg + DMAR_PRS_REG);
			pr_info_ratelimited("IOMMU: %s: PRQ overflow cleared",
					    iommu->name);
		}
	}

	if (!completion_done(&iommu->prq_complete))
		complete(&iommu->prq_complete);

	return IRQ_RETVAL(handled);
}

#define to_intel_svm_dev(handle) container_of(handle, struct intel_svm_dev, sva)
struct iommu_sva *
intel_svm_bind(struct device *dev, struct mm_struct *mm, void *drvdata)
{
	struct iommu_sva *sva = ERR_PTR(-EINVAL);
	struct intel_svm_dev *sdev = NULL;
	unsigned int flags = 0;
	int ret;

	/*
	 * TODO: Consolidate with generic iommu-sva bind after it is merged.
	 * It will require shared SVM data structures, i.e. combine io_mm
	 * and intel_svm etc.
	 */
	if (drvdata)
		flags = *(unsigned int *)drvdata;
	mutex_lock(&pasid_mutex);
	ret = intel_svm_bind_mm(dev, flags, NULL, mm, &sdev);
	if (ret)
		sva = ERR_PTR(ret);
	else if (sdev)
		sva = &sdev->sva;
	else
		WARN(!sdev, "SVM bind succeeded with no sdev!\n");

	mutex_unlock(&pasid_mutex);

	return sva;
}

void intel_svm_unbind(struct iommu_sva *sva)
{
	struct intel_svm_dev *sdev;

	mutex_lock(&pasid_mutex);
	sdev = to_intel_svm_dev(sva);
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
		if (private_present)
			memcpy(&desc.qw2, prm->private_data,
			       sizeof(prm->private_data));

		qi_submit_sync(iommu, &desc, 1, 0);
	}
out:
	mutex_unlock(&pasid_mutex);
	return ret;
}
