/*
 * Copyright (C) 2016 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
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

void
msm_gem_unmap_vma(struct msm_gem_address_space *aspace,
		struct msm_gem_vma *vma, struct sg_table *sgt)
{
	if (!aspace || !vma->iova)
		return;

	if (aspace->mmu) {
		unsigned size = vma->node.size << PAGE_SHIFT;
		aspace->mmu->funcs->unmap(aspace->mmu, vma->iova, sgt, size);
	}

	spin_lock(&aspace->lock);
	drm_mm_remove_node(&vma->node);
	spin_unlock(&aspace->lock);

	vma->iova = 0;

	msm_gem_address_space_put(aspace);
}

int
msm_gem_map_vma(struct msm_gem_address_space *aspace,
		struct msm_gem_vma *vma, struct sg_table *sgt, int npages)
{
	int ret;

	spin_lock(&aspace->lock);
	if (WARN_ON(drm_mm_node_allocated(&vma->node))) {
		spin_unlock(&aspace->lock);
		return 0;
	}

	ret = drm_mm_insert_node(&aspace->mm, &vma->node, npages);
	spin_unlock(&aspace->lock);

	if (ret)
		return ret;

	vma->iova = vma->node.start << PAGE_SHIFT;

	if (aspace->mmu) {
		unsigned size = npages << PAGE_SHIFT;
		ret = aspace->mmu->funcs->map(aspace->mmu, vma->iova, sgt,
				size, IOMMU_READ | IOMMU_WRITE);
	}

	/* Get a reference to the aspace to keep it around */
	kref_get(&aspace->kref);

	return ret;
}

struct msm_gem_address_space *
msm_gem_address_space_create(struct device *dev, struct iommu_domain *domain,
		const char *name)
{
	struct msm_gem_address_space *aspace;

	aspace = kzalloc(sizeof(*aspace), GFP_KERNEL);
	if (!aspace)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&aspace->lock);
	aspace->name = name;
	aspace->mmu = msm_iommu_new(dev, domain);

	drm_mm_init(&aspace->mm, (domain->geometry.aperture_start >> PAGE_SHIFT),
			(domain->geometry.aperture_end >> PAGE_SHIFT) - 1);

	kref_init(&aspace->kref);

	return aspace;
}
