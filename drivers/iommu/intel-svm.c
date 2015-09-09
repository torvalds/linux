/*
 * Copyright © 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 */

#include <linux/intel-iommu.h>
#include <linux/mmu_notifier.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/intel-svm.h>
#include <linux/rculist.h>
#include <linux/pci.h>
#include <linux/pci-ats.h>

struct pasid_entry {
	u64 val;
};

int intel_svm_alloc_pasid_tables(struct intel_iommu *iommu)
{
	struct page *pages;
	int order;

	order = ecap_pss(iommu->ecap) + 7 - PAGE_SHIFT;
	if (order < 0)
		order = 0;

	pages = alloc_pages(GFP_KERNEL | __GFP_ZERO, order);
	if (!pages) {
		pr_warn("IOMMU: %s: Failed to allocate PASID table\n",
			iommu->name);
		return -ENOMEM;
	}
	iommu->pasid_table = page_address(pages);
	pr_info("%s: Allocated order %d PASID table.\n", iommu->name, order);

	if (ecap_dis(iommu->ecap)) {
		pages = alloc_pages(GFP_KERNEL | __GFP_ZERO, order);
		if (pages)
			iommu->pasid_state_table = page_address(pages);
		else
			pr_warn("IOMMU: %s: Failed to allocate PASID state table\n",
				iommu->name);
	}

	idr_init(&iommu->pasid_idr);

	return 0;
}

int intel_svm_free_pasid_tables(struct intel_iommu *iommu)
{
	int order;

	order = ecap_pss(iommu->ecap) + 7 - PAGE_SHIFT;
	if (order < 0)
		order = 0;

	if (iommu->pasid_table) {
		free_pages((unsigned long)iommu->pasid_table, order);
		iommu->pasid_table = NULL;
	}
	if (iommu->pasid_state_table) {
		free_pages((unsigned long)iommu->pasid_state_table, order);
		iommu->pasid_state_table = NULL;
	}
	idr_destroy(&iommu->pasid_idr);
	return 0;
}

static void intel_flush_svm_range_dev (struct intel_svm *svm, struct intel_svm_dev *sdev,
				       unsigned long address, int pages, int ih)
{
	struct qi_desc desc;
	int mask = ilog2(__roundup_pow_of_two(pages));

	if (pages == -1 || !cap_pgsel_inv(svm->iommu->cap) ||
	    mask > cap_max_amask_val(svm->iommu->cap)) {
		desc.low = QI_EIOTLB_PASID(svm->pasid) | QI_EIOTLB_DID(sdev->did) |
			QI_EIOTLB_GRAN(QI_GRAN_NONG_PASID) | QI_EIOTLB_TYPE;
		desc.high = 0;
	} else {
		desc.low = QI_EIOTLB_PASID(svm->pasid) | QI_EIOTLB_DID(sdev->did) |
			QI_EIOTLB_GRAN(QI_GRAN_PSI_PASID) | QI_EIOTLB_TYPE;
		desc.high = QI_EIOTLB_ADDR(address) | QI_EIOTLB_GL(1) |
			QI_EIOTLB_IH(ih) | QI_EIOTLB_AM(mask);
	}

	qi_submit_sync(&desc, svm->iommu);

	if (sdev->dev_iotlb) {
		desc.low = QI_DEV_EIOTLB_PASID(svm->pasid) | QI_DEV_EIOTLB_SID(sdev->sid) |
			QI_DEV_EIOTLB_QDEP(sdev->qdep) | QI_DEIOTLB_TYPE;
		if (mask) {
			unsigned long adr, delta;

			/* Least significant zero bits in the address indicate the
			 * range of the request. So mask them out according to the
			 * size. */
			adr = address & ((1<<(VTD_PAGE_SHIFT + mask)) - 1);

			/* Now ensure that we round down further if the original
			 * request was not aligned w.r.t. its size */
			delta = address - adr;
			if (delta + (pages << VTD_PAGE_SHIFT) >= (1 << (VTD_PAGE_SHIFT + mask)))
				adr &= ~(1 << (VTD_PAGE_SHIFT + mask));
			desc.high = QI_DEV_EIOTLB_ADDR(adr) | QI_DEV_EIOTLB_SIZE;
		} else {
			desc.high = QI_DEV_EIOTLB_ADDR(address);
		}
		qi_submit_sync(&desc, svm->iommu);
	}
}

static void intel_flush_svm_range(struct intel_svm *svm, unsigned long address,
				  int pages, int ih)
{
	struct intel_svm_dev *sdev;

	rcu_read_lock();
	list_for_each_entry_rcu(sdev, &svm->devs, list)
		intel_flush_svm_range_dev(svm, sdev, address, pages, ih);
	rcu_read_unlock();
}

static void intel_change_pte(struct mmu_notifier *mn, struct mm_struct *mm,
			     unsigned long address, pte_t pte)
{
	struct intel_svm *svm = container_of(mn, struct intel_svm, notifier);

	intel_flush_svm_range(svm, address, 1, 1);
}

static void intel_invalidate_page(struct mmu_notifier *mn, struct mm_struct *mm,
				  unsigned long address)
{
	struct intel_svm *svm = container_of(mn, struct intel_svm, notifier);

	intel_flush_svm_range(svm, address, 1, 1);
}

/* Pages have been freed at this point */
static void intel_invalidate_range(struct mmu_notifier *mn,
				   struct mm_struct *mm,
				   unsigned long start, unsigned long end)
{
	struct intel_svm *svm = container_of(mn, struct intel_svm, notifier);

	intel_flush_svm_range(svm, start,
			      (end - start + PAGE_SIZE - 1) >> VTD_PAGE_SHIFT , 0);
}


static void intel_flush_pasid_dev(struct intel_svm *svm, struct intel_svm_dev *sdev)
{
	struct qi_desc desc;

	desc.high = 0;
	desc.low = QI_PC_TYPE | QI_PC_DID(sdev->did) | QI_PC_PASID_SEL | QI_PC_PASID(svm->pasid);

	qi_submit_sync(&desc, svm->iommu);
}

static void intel_mm_release(struct mmu_notifier *mn, struct mm_struct *mm)
{
	struct intel_svm *svm = container_of(mn, struct intel_svm, notifier);

	svm->iommu->pasid_table[svm->pasid].val = 0;

	/* There's no need to do any flush because we can't get here if there
	 * are any devices left anyway. */
	WARN_ON(!list_empty(&svm->devs));
}

static const struct mmu_notifier_ops intel_mmuops = {
	.release = intel_mm_release,
	.change_pte = intel_change_pte,
	.invalidate_page = intel_invalidate_page,
	.invalidate_range = intel_invalidate_range,
};

static DEFINE_MUTEX(pasid_mutex);

int intel_svm_bind_mm(struct device *dev, int *pasid)
{
	struct intel_iommu *iommu = intel_svm_device_to_iommu(dev);
	struct intel_svm_dev *sdev;
	struct intel_svm *svm = NULL;
	int pasid_max;
	int ret;

	BUG_ON(pasid && !current->mm);

	if (WARN_ON(!iommu))
		return -EINVAL;

	if (dev_is_pci(dev)) {
		pasid_max = pci_max_pasids(to_pci_dev(dev));
		if (pasid_max < 0)
			return -EINVAL;
	} else
		pasid_max = 1 << 20;

	mutex_lock(&pasid_mutex);
	if (pasid) {
		int i;

		idr_for_each_entry(&iommu->pasid_idr, svm, i) {
			if (svm->mm != current->mm)
				continue;

			if (svm->pasid >= pasid_max) {
				dev_warn(dev,
					 "Limited PASID width. Cannot use existing PASID %d\n",
					 svm->pasid);
				ret = -ENOSPC;
				goto out;
			}

			list_for_each_entry(sdev, &svm->devs, list) {
				if (dev == sdev->dev) {
					sdev->users++;
					goto success;
				}
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

	ret = intel_iommu_enable_pasid(iommu, sdev);
	if (ret || !pasid) {
		/* If they don't actually want to assign a PASID, this is
		 * just an enabling check/preparation. */
		kfree(sdev);
		goto out;
	}
	/* Finish the setup now we know we're keeping it */
	sdev->users = 1;
	init_rcu_head(&sdev->rcu);

	if (!svm) {
		svm = kzalloc(sizeof(*svm), GFP_KERNEL);
		if (!svm) {
			ret = -ENOMEM;
			kfree(sdev);
			goto out;
		}
		svm->iommu = iommu;

		if (pasid_max > 2 << ecap_pss(iommu->ecap))
			pasid_max = 2 << ecap_pss(iommu->ecap);

		ret = idr_alloc(&iommu->pasid_idr, svm, 0, pasid_max - 1,
				GFP_KERNEL);
		if (ret < 0) {
			kfree(svm);
			goto out;
		}
		svm->pasid = ret;
		svm->notifier.ops = &intel_mmuops;
		svm->mm = get_task_mm(current);
		INIT_LIST_HEAD_RCU(&svm->devs);
		ret = -ENOMEM;
		if (!svm->mm || (ret = mmu_notifier_register(&svm->notifier, svm->mm))) {
			idr_remove(&svm->iommu->pasid_idr, svm->pasid);
			kfree(svm);
			kfree(sdev);
			goto out;
		}
		iommu->pasid_table[svm->pasid].val = (u64)__pa(svm->mm->pgd) | 1;
		wmb();
	}
	list_add_rcu(&sdev->list, &svm->devs);

 success:
	*pasid = svm->pasid;
	ret = 0;
 out:
	mutex_unlock(&pasid_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(intel_svm_bind_mm);

int intel_svm_unbind_mm(struct device *dev, int pasid)
{
	struct intel_svm_dev *sdev;
	struct intel_iommu *iommu;
	struct intel_svm *svm;
	int ret = -EINVAL;

	mutex_lock(&pasid_mutex);
	iommu = intel_svm_device_to_iommu(dev);
	if (!iommu || !iommu->pasid_table)
		goto out;

	svm = idr_find(&iommu->pasid_idr, pasid);
	if (!svm)
		goto out;

	list_for_each_entry(sdev, &svm->devs, list) {
		if (dev == sdev->dev) {
			ret = 0;
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
				intel_flush_pasid_dev(svm, sdev);
				intel_flush_svm_range_dev(svm, sdev, 0, -1, 0);
				kfree_rcu(sdev, rcu);

				if (list_empty(&svm->devs)) {
					mmu_notifier_unregister(&svm->notifier, svm->mm);

					idr_remove(&svm->iommu->pasid_idr, svm->pasid);
					mmput(svm->mm);
					/* We mandate that no page faults may be outstanding
					 * for the PASID when intel_svm_unbind_mm() is called.
					 * If that is not obeyed, subtle errors will happen.
					 * Let's make them less subtle... */
					memset(svm, 0x6b, sizeof(*svm));
					kfree(svm);
				}
			}
			break;
		}
	}
 out:
	mutex_unlock(&pasid_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(intel_svm_unbind_mm);
