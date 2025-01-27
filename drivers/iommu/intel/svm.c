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
#include "../iommu-pages.h"
#include "trace.h"

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

/* Pages have been freed at this point */
static void intel_arch_invalidate_secondary_tlbs(struct mmu_notifier *mn,
					struct mm_struct *mm,
					unsigned long start, unsigned long end)
{
	struct dmar_domain *domain = container_of(mn, struct dmar_domain, notifier);

	if (start == 0 && end == ULONG_MAX) {
		cache_tag_flush_all(domain);
		return;
	}

	/*
	 * The mm_types defines vm_end as the first byte after the end address,
	 * different from IOMMU subsystem using the last address of an address
	 * range.
	 */
	cache_tag_flush_range(domain, start, end - 1, 0);
}

static void intel_mm_release(struct mmu_notifier *mn, struct mm_struct *mm)
{
	struct dmar_domain *domain = container_of(mn, struct dmar_domain, notifier);
	struct dev_pasid_info *dev_pasid;
	struct device_domain_info *info;
	unsigned long flags;

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
	spin_lock_irqsave(&domain->lock, flags);
	list_for_each_entry(dev_pasid, &domain->dev_pasids, link_domain) {
		info = dev_iommu_priv_get(dev_pasid->dev);
		intel_pasid_tear_down_entry(info->iommu, dev_pasid->dev,
					    dev_pasid->pasid, true);
	}
	spin_unlock_irqrestore(&domain->lock, flags);

}

static void intel_mm_free_notifier(struct mmu_notifier *mn)
{
	struct dmar_domain *domain = container_of(mn, struct dmar_domain, notifier);

	kfree(domain->qi_batch);
	kfree(domain);
}

static const struct mmu_notifier_ops intel_mmuops = {
	.release = intel_mm_release,
	.arch_invalidate_secondary_tlbs = intel_arch_invalidate_secondary_tlbs,
	.free_notifier = intel_mm_free_notifier,
};

static int intel_svm_set_dev_pasid(struct iommu_domain *domain,
				   struct device *dev, ioasid_t pasid,
				   struct iommu_domain *old)
{
	struct device_domain_info *info = dev_iommu_priv_get(dev);
	struct intel_iommu *iommu = info->iommu;
	struct mm_struct *mm = domain->mm;
	struct dev_pasid_info *dev_pasid;
	unsigned long sflags;
	int ret = 0;

	dev_pasid = domain_add_dev_pasid(domain, dev, pasid);
	if (IS_ERR(dev_pasid))
		return PTR_ERR(dev_pasid);

	/* Setup the pasid table: */
	sflags = cpu_feature_enabled(X86_FEATURE_LA57) ? PASID_FLAG_FL5LP : 0;
	ret = __domain_setup_first_level(iommu, dev, pasid,
					 FLPT_DEFAULT_DID, mm->pgd,
					 sflags, old);
	if (ret)
		goto out_remove_dev_pasid;

	domain_remove_dev_pasid(old, dev, pasid);

	return 0;

out_remove_dev_pasid:
	domain_remove_dev_pasid(domain, dev, pasid);
	return ret;
}

static void intel_svm_domain_free(struct iommu_domain *domain)
{
	struct dmar_domain *dmar_domain = to_dmar_domain(domain);

	/* dmar_domain free is deferred to the mmu free_notifier callback. */
	mmu_notifier_put(&dmar_domain->notifier);
}

static const struct iommu_domain_ops intel_svm_domain_ops = {
	.set_dev_pasid		= intel_svm_set_dev_pasid,
	.free			= intel_svm_domain_free
};

struct iommu_domain *intel_svm_domain_alloc(struct device *dev,
					    struct mm_struct *mm)
{
	struct dmar_domain *domain;
	int ret;

	domain = kzalloc(sizeof(*domain), GFP_KERNEL);
	if (!domain)
		return ERR_PTR(-ENOMEM);

	domain->domain.ops = &intel_svm_domain_ops;
	domain->use_first_level = true;
	INIT_LIST_HEAD(&domain->dev_pasids);
	INIT_LIST_HEAD(&domain->cache_tags);
	spin_lock_init(&domain->cache_lock);
	spin_lock_init(&domain->lock);

	domain->notifier.ops = &intel_mmuops;
	ret = mmu_notifier_register(&domain->notifier, mm);
	if (ret) {
		kfree(domain);
		return ERR_PTR(ret);
	}

	return &domain->domain;
}
