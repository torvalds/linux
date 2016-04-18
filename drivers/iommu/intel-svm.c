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
#include <linux/dmar.h>
#include <linux/interrupt.h>

static irqreturn_t prq_event_thread(int irq, void *d);

struct pasid_entry {
	u64 val;
};

struct pasid_state_entry {
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
		goto err;
	}
	dmar_writeq(iommu->reg + DMAR_PQH_REG, 0ULL);
	dmar_writeq(iommu->reg + DMAR_PQT_REG, 0ULL);
	dmar_writeq(iommu->reg + DMAR_PQA_REG, virt_to_phys(iommu->prq) | PRQ_ORDER);

	return 0;
}

int intel_svm_finish_prq(struct intel_iommu *iommu)
{
	dmar_writeq(iommu->reg + DMAR_PQH_REG, 0ULL);
	dmar_writeq(iommu->reg + DMAR_PQT_REG, 0ULL);
	dmar_writeq(iommu->reg + DMAR_PQA_REG, 0ULL);

	free_irq(iommu->pr_irq, iommu);
	dmar_free_hwirq(iommu->pr_irq);
	iommu->pr_irq = 0;

	free_pages((unsigned long)iommu->prq, PRQ_ORDER);
	iommu->prq = NULL;

	return 0;
}

static void intel_flush_svm_range_dev (struct intel_svm *svm, struct intel_svm_dev *sdev,
				       unsigned long address, unsigned long pages, int ih, int gl)
{
	struct qi_desc desc;

	if (pages == -1) {
		/* For global kernel pages we have to flush them in *all* PASIDs
		 * because that's the only option the hardware gives us. Despite
		 * the fact that they are actually only accessible through one. */
		if (gl)
			desc.low = QI_EIOTLB_PASID(svm->pasid) | QI_EIOTLB_DID(sdev->did) |
				QI_EIOTLB_GRAN(QI_GRAN_ALL_ALL) | QI_EIOTLB_TYPE;
		else
			desc.low = QI_EIOTLB_PASID(svm->pasid) | QI_EIOTLB_DID(sdev->did) |
				QI_EIOTLB_GRAN(QI_GRAN_NONG_PASID) | QI_EIOTLB_TYPE;
		desc.high = 0;
	} else {
		int mask = ilog2(__roundup_pow_of_two(pages));

		desc.low = QI_EIOTLB_PASID(svm->pasid) | QI_EIOTLB_DID(sdev->did) |
			QI_EIOTLB_GRAN(QI_GRAN_PSI_PASID) | QI_EIOTLB_TYPE;
		desc.high = QI_EIOTLB_ADDR(address) | QI_EIOTLB_GL(gl) |
			QI_EIOTLB_IH(ih) | QI_EIOTLB_AM(mask);
	}
	qi_submit_sync(&desc, svm->iommu);

	if (sdev->dev_iotlb) {
		desc.low = QI_DEV_EIOTLB_PASID(svm->pasid) | QI_DEV_EIOTLB_SID(sdev->sid) |
			QI_DEV_EIOTLB_QDEP(sdev->qdep) | QI_DEIOTLB_TYPE;
		if (pages == -1) {
			desc.high = QI_DEV_EIOTLB_ADDR(-1ULL >> 1) | QI_DEV_EIOTLB_SIZE;
		} else if (pages > 1) {
			/* The least significant zero bit indicates the size. So,
			 * for example, an "address" value of 0x12345f000 will
			 * flush from 0x123440000 to 0x12347ffff (256KiB). */
			unsigned long last = address + ((unsigned long)(pages - 1) << VTD_PAGE_SHIFT);
			unsigned long mask = __rounddown_pow_of_two(address ^ last);;

			desc.high = QI_DEV_EIOTLB_ADDR((address & ~mask) | (mask - 1)) | QI_DEV_EIOTLB_SIZE;
		} else {
			desc.high = QI_DEV_EIOTLB_ADDR(address);
		}
		qi_submit_sync(&desc, svm->iommu);
	}
}

static void intel_flush_svm_range(struct intel_svm *svm, unsigned long address,
				  unsigned long pages, int ih, int gl)
{
	struct intel_svm_dev *sdev;

	/* Try deferred invalidate if available */
	if (svm->iommu->pasid_state_table &&
	    !cmpxchg64(&svm->iommu->pasid_state_table[svm->pasid].val, 0, 1ULL << 63))
		return;

	rcu_read_lock();
	list_for_each_entry_rcu(sdev, &svm->devs, list)
		intel_flush_svm_range_dev(svm, sdev, address, pages, ih, gl);
	rcu_read_unlock();
}

static void intel_change_pte(struct mmu_notifier *mn, struct mm_struct *mm,
			     unsigned long address, pte_t pte)
{
	struct intel_svm *svm = container_of(mn, struct intel_svm, notifier);

	intel_flush_svm_range(svm, address, 1, 1, 0);
}

static void intel_invalidate_page(struct mmu_notifier *mn, struct mm_struct *mm,
				  unsigned long address)
{
	struct intel_svm *svm = container_of(mn, struct intel_svm, notifier);

	intel_flush_svm_range(svm, address, 1, 1, 0);
}

/* Pages have been freed at this point */
static void intel_invalidate_range(struct mmu_notifier *mn,
				   struct mm_struct *mm,
				   unsigned long start, unsigned long end)
{
	struct intel_svm *svm = container_of(mn, struct intel_svm, notifier);

	intel_flush_svm_range(svm, start,
			      (end - start + PAGE_SIZE - 1) >> VTD_PAGE_SHIFT, 0, 0);
}


static void intel_flush_pasid_dev(struct intel_svm *svm, struct intel_svm_dev *sdev, int pasid)
{
	struct qi_desc desc;

	desc.high = 0;
	desc.low = QI_PC_TYPE | QI_PC_DID(sdev->did) | QI_PC_PASID_SEL | QI_PC_PASID(pasid);

	qi_submit_sync(&desc, svm->iommu);
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
	svm->iommu->pasid_table[svm->pasid].val = 0;
	wmb();

	rcu_read_lock();
	list_for_each_entry_rcu(sdev, &svm->devs, list) {
		intel_flush_pasid_dev(svm, sdev, svm->pasid);
		intel_flush_svm_range_dev(svm, sdev, 0, -1, 0, !svm->mm);
	}
	rcu_read_unlock();

}

static const struct mmu_notifier_ops intel_mmuops = {
	.release = intel_mm_release,
	.change_pte = intel_change_pte,
	.invalidate_page = intel_invalidate_page,
	.invalidate_range = intel_invalidate_range,
};

static DEFINE_MUTEX(pasid_mutex);

int intel_svm_bind_mm(struct device *dev, int *pasid, int flags, struct svm_dev_ops *ops)
{
	struct intel_iommu *iommu = intel_svm_device_to_iommu(dev);
	struct intel_svm_dev *sdev;
	struct intel_svm *svm = NULL;
	struct mm_struct *mm = NULL;
	int pasid_max;
	int ret;

	if (WARN_ON(!iommu))
		return -EINVAL;

	if (dev_is_pci(dev)) {
		pasid_max = pci_max_pasids(to_pci_dev(dev));
		if (pasid_max < 0)
			return -EINVAL;
	} else
		pasid_max = 1 << 20;

	if ((flags & SVM_FLAG_SUPERVISOR_MODE)) {
		if (!ecap_srs(iommu->ecap))
			return -EINVAL;
	} else if (pasid) {
		mm = get_task_mm(current);
		BUG_ON(!mm);
	}

	mutex_lock(&pasid_mutex);
	if (pasid && !(flags & SVM_FLAG_PRIVATE_PASID)) {
		int i;

		idr_for_each_entry(&iommu->pasid_idr, svm, i) {
			if (svm->mm != mm ||
			    (svm->flags & SVM_FLAG_PRIVATE_PASID))
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
					if (sdev->ops != ops) {
						ret = -EBUSY;
						goto out;
					}
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
	sdev->ops = ops;
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

		/* Do not use PASID 0 in caching mode (virtualised IOMMU) */
		ret = idr_alloc(&iommu->pasid_idr, svm,
				!!cap_caching_mode(iommu->cap),
				pasid_max - 1, GFP_KERNEL);
		if (ret < 0) {
			kfree(svm);
			goto out;
		}
		svm->pasid = ret;
		svm->notifier.ops = &intel_mmuops;
		svm->mm = mm;
		svm->flags = flags;
		INIT_LIST_HEAD_RCU(&svm->devs);
		ret = -ENOMEM;
		if (mm) {
			ret = mmu_notifier_register(&svm->notifier, mm);
			if (ret) {
				idr_remove(&svm->iommu->pasid_idr, svm->pasid);
				kfree(svm);
				kfree(sdev);
				goto out;
			}
			iommu->pasid_table[svm->pasid].val = (u64)__pa(mm->pgd) | 1;
		} else
			iommu->pasid_table[svm->pasid].val = (u64)__pa(init_mm.pgd) | 1 | (1ULL << 11);
		wmb();
		/* In caching mode, we still have to flush with PASID 0 when
		 * a PASID table entry becomes present. Not entirely clear
		 * *why* that would be the case — surely we could just issue
		 * a flush with the PASID value that we've changed? The PASID
		 * is the index into the table, after all. It's not like domain
		 * IDs in the case of the equivalent context-entry change in
		 * caching mode. And for that matter it's not entirely clear why
		 * a VMM would be in the business of caching the PASID table
		 * anyway. Surely that can be left entirely to the guest? */
		if (cap_caching_mode(iommu->cap))
			intel_flush_pasid_dev(svm, sdev, 0);
	}
	list_add_rcu(&sdev->list, &svm->devs);

 success:
	*pasid = svm->pasid;
	ret = 0;
 out:
	mutex_unlock(&pasid_mutex);
	if (mm)
		mmput(mm);
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
				intel_flush_pasid_dev(svm, sdev, svm->pasid);
				intel_flush_svm_range_dev(svm, sdev, 0, -1, 0, !svm->mm);
				kfree_rcu(sdev, rcu);

				if (list_empty(&svm->devs)) {

					idr_remove(&svm->iommu->pasid_idr, svm->pasid);
					if (svm->mm)
						mmu_notifier_unregister(&svm->notifier, svm->mm);

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

/* Page request queue descriptor */
struct page_req_dsc {
	u64 srr:1;
	u64 bof:1;
	u64 pasid_present:1;
	u64 lpig:1;
	u64 pasid:20;
	u64 bus:8;
	u64 private:23;
	u64 prg_index:9;
	u64 rd_req:1;
	u64 wr_req:1;
	u64 exe_req:1;
	u64 priv_req:1;
	u64 devfn:8;
	u64 addr:52;
};

#define PRQ_RING_MASK ((0x1000 << PRQ_ORDER) - 0x10)

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

static irqreturn_t prq_event_thread(int irq, void *d)
{
	struct intel_iommu *iommu = d;
	struct intel_svm *svm = NULL;
	int head, tail, handled = 0;

	/* Clear PPR bit before reading head/tail registers, to
	 * ensure that we get a new interrupt if needed. */
	writel(DMA_PRS_PPR, iommu->reg + DMAR_PRS_REG);

	tail = dmar_readq(iommu->reg + DMAR_PQT_REG) & PRQ_RING_MASK;
	head = dmar_readq(iommu->reg + DMAR_PQH_REG) & PRQ_RING_MASK;
	while (head != tail) {
		struct intel_svm_dev *sdev;
		struct vm_area_struct *vma;
		struct page_req_dsc *req;
		struct qi_desc resp;
		int ret, result;
		u64 address;

		handled = 1;

		req = &iommu->prq[head / sizeof(*req)];

		result = QI_RESP_FAILURE;
		address = (u64)req->addr << VTD_PAGE_SHIFT;
		if (!req->pasid_present) {
			pr_err("%s: Page request without PASID: %08llx %08llx\n",
			       iommu->name, ((unsigned long long *)req)[0],
			       ((unsigned long long *)req)[1]);
			goto bad_req;
		}

		if (!svm || svm->pasid != req->pasid) {
			rcu_read_lock();
			svm = idr_find(&iommu->pasid_idr, req->pasid);
			/* It *can't* go away, because the driver is not permitted
			 * to unbind the mm while any page faults are outstanding.
			 * So we only need RCU to protect the internal idr code. */
			rcu_read_unlock();

			if (!svm) {
				pr_err("%s: Page request for invalid PASID %d: %08llx %08llx\n",
				       iommu->name, req->pasid, ((unsigned long long *)req)[0],
				       ((unsigned long long *)req)[1]);
				goto no_pasid;
			}
		}

		result = QI_RESP_INVALID;
		/* Since we're using init_mm.pgd directly, we should never take
		 * any faults on kernel addresses. */
		if (!svm->mm)
			goto bad_req;
		/* If the mm is already defunct, don't handle faults. */
		if (!atomic_inc_not_zero(&svm->mm->mm_users))
			goto bad_req;
		down_read(&svm->mm->mmap_sem);
		vma = find_extend_vma(svm->mm, address);
		if (!vma || address < vma->vm_start)
			goto invalid;

		if (access_error(vma, req))
			goto invalid;

		ret = handle_mm_fault(svm->mm, vma, address,
				      req->wr_req ? FAULT_FLAG_WRITE : 0);
		if (ret & VM_FAULT_ERROR)
			goto invalid;

		result = QI_RESP_SUCCESS;
	invalid:
		up_read(&svm->mm->mmap_sem);
		mmput(svm->mm);
	bad_req:
		/* Accounting for major/minor faults? */
		rcu_read_lock();
		list_for_each_entry_rcu(sdev, &svm->devs, list) {
			if (sdev->sid == PCI_DEVID(req->bus, req->devfn))
				break;
		}
		/* Other devices can go away, but the drivers are not permitted
		 * to unbind while any page faults might be in flight. So it's
		 * OK to drop the 'lock' here now we have it. */
		rcu_read_unlock();

		if (WARN_ON(&sdev->list == &svm->devs))
			sdev = NULL;

		if (sdev && sdev->ops && sdev->ops->fault_cb) {
			int rwxp = (req->rd_req << 3) | (req->wr_req << 2) |
				(req->exe_req << 1) | (req->priv_req);
			sdev->ops->fault_cb(sdev->dev, req->pasid, req->addr, req->private, rwxp, result);
		}
		/* We get here in the error case where the PASID lookup failed,
		   and these can be NULL. Do not use them below this point! */
		sdev = NULL;
		svm = NULL;
	no_pasid:
		if (req->lpig) {
			/* Page Group Response */
			resp.low = QI_PGRP_PASID(req->pasid) |
				QI_PGRP_DID((req->bus << 8) | req->devfn) |
				QI_PGRP_PASID_P(req->pasid_present) |
				QI_PGRP_RESP_TYPE;
			resp.high = QI_PGRP_IDX(req->prg_index) |
				QI_PGRP_PRIV(req->private) | QI_PGRP_RESP_CODE(result);

			qi_submit_sync(&resp, iommu);
		} else if (req->srr) {
			/* Page Stream Response */
			resp.low = QI_PSTRM_IDX(req->prg_index) |
				QI_PSTRM_PRIV(req->private) | QI_PSTRM_BUS(req->bus) |
				QI_PSTRM_PASID(req->pasid) | QI_PSTRM_RESP_TYPE;
			resp.high = QI_PSTRM_ADDR(address) | QI_PSTRM_DEVFN(req->devfn) |
				QI_PSTRM_RESP_CODE(result);

			qi_submit_sync(&resp, iommu);
		}

		head = (head + sizeof(*req)) & PRQ_RING_MASK;
	}

	dmar_writeq(iommu->reg + DMAR_PQH_REG, tail);

	return IRQ_RETVAL(handled);
}
