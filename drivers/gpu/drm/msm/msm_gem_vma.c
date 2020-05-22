// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include "msm_drv.h"
#include "msm_gem.h"
#include "msm_mmu.h"

static void
msm_gem_address_space_destroy(struct kref *kref)
{
	struct msm_gem_address_space *aspace = container_of(kref,
			struct msm_gem_address_space, kref);

	drm_mm_takedown(&aspace->mm);
	if (aspace->mmu)
		aspace->mmu->funcs->destroy(aspace->mmu);
	kfree(aspace);
}


void msm_gem_address_space_put(struct msm_gem_address_space *aspace)
{
	if (aspace)
		kref_put(&aspace->kref, msm_gem_address_space_destroy);
}

/* Actually unmap memory for the vma */
void msm_gem_purge_vma(struct msm_gem_address_space *aspace,
		struct msm_gem_vma *vma)
{
	unsigned size = vma->node.size << PAGE_SHIFT;

	/* Print a message if we try to purge a vma in use */
	if (WARN_ON(vma->inuse > 0))
		return;

	/* Don't do anything if the memory isn't mapped */
	if (!vma->mapped)
		return;

	if (aspace->mmu)
		aspace->mmu->funcs->unmap(aspace->mmu, vma->iova, size);

	vma->mapped = false;
}

/* Remove reference counts for the mapping */
void msm_gem_unmap_vma(struct msm_gem_address_space *aspace,
		struct msm_gem_vma *vma)
{
	if (!WARN_ON(!vma->iova))
		vma->inuse--;
}

int
msm_gem_map_vma(struct msm_gem_address_space *aspace,
		struct msm_gem_vma *vma, int prot,
		struct sg_table *sgt, int npages)
{
	unsigned size = npages << PAGE_SHIFT;
	int ret = 0;

	if (WARN_ON(!vma->iova))
		return -EINVAL;

	/* Increase the usage counter */
	vma->inuse++;

	if (vma->mapped)
		return 0;

	vma->mapped = true;

	if (aspace && aspace->mmu)
		ret = aspace->mmu->funcs->map(aspace->mmu, vma->iova, sgt,
				size, prot);

	if (ret)
		vma->mapped = false;

	return ret;
}

/* Close an iova.  Warn if it is still in use */
void msm_gem_close_vma(struct msm_gem_address_space *aspace,
		struct msm_gem_vma *vma)
{
	if (WARN_ON(vma->inuse > 0 || vma->mapped))
		return;

	spin_lock(&aspace->lock);
	if (vma->iova)
		drm_mm_remove_node(&vma->node);
	spin_unlock(&aspace->lock);

	vma->iova = 0;

	msm_gem_address_space_put(aspace);
}

/* Initialize a new vma and allocate an iova for it */
int msm_gem_init_vma(struct msm_gem_address_space *aspace,
		struct msm_gem_vma *vma, int npages,
		u64 range_start, u64 range_end)
{
	int ret;

	if (WARN_ON(vma->iova))
		return -EBUSY;

	spin_lock(&aspace->lock);
	ret = drm_mm_insert_node_in_range(&aspace->mm, &vma->node, npages, 0,
		0, range_start, range_end, 0);
	spin_unlock(&aspace->lock);

	if (ret)
		return ret;

	vma->iova = vma->node.start << PAGE_SHIFT;
	vma->mapped = false;

	kref_get(&aspace->kref);

	return 0;
}

struct msm_gem_address_space *
msm_gem_address_space_create(struct msm_mmu *mmu, const char *name,
		u64 va_start, u64 size)
{
	struct msm_gem_address_space *aspace;

	if (IS_ERR(mmu))
		return ERR_CAST(mmu);

	aspace = kzalloc(sizeof(*aspace), GFP_KERNEL);
	if (!aspace)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&aspace->lock);
	aspace->name = name;
	aspace->mmu = mmu;

	drm_mm_init(&aspace->mm, va_start >> PAGE_SHIFT, size >> PAGE_SHIFT);

	kref_init(&aspace->kref);

	return aspace;
}
