// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include "msm_drv.h"
#include "msm_fence.h"
#include "msm_gem.h"
#include "msm_mmu.h"

static void
msm_gem_vm_destroy(struct kref *kref)
{
	struct msm_gem_vm *vm = container_of(kref, struct msm_gem_vm, kref);

	drm_mm_takedown(&vm->mm);
	if (vm->mmu)
		vm->mmu->funcs->destroy(vm->mmu);
	put_pid(vm->pid);
	kfree(vm);
}


void msm_gem_vm_put(struct msm_gem_vm *vm)
{
	if (vm)
		kref_put(&vm->kref, msm_gem_vm_destroy);
}

struct msm_gem_vm *
msm_gem_vm_get(struct msm_gem_vm *vm)
{
	if (!IS_ERR_OR_NULL(vm))
		kref_get(&vm->kref);

	return vm;
}

/* Actually unmap memory for the vma */
void msm_gem_vma_purge(struct msm_gem_vma *vma)
{
	struct msm_gem_vm *vm = vma->vm;
	unsigned size = vma->node.size;

	/* Don't do anything if the memory isn't mapped */
	if (!vma->mapped)
		return;

	vm->mmu->funcs->unmap(vm->mmu, vma->iova, size);

	vma->mapped = false;
}

/* Map and pin vma: */
int
msm_gem_vma_map(struct msm_gem_vma *vma, int prot,
		struct sg_table *sgt, int size)
{
	struct msm_gem_vm *vm = vma->vm;
	int ret;

	if (GEM_WARN_ON(!vma->iova))
		return -EINVAL;

	if (vma->mapped)
		return 0;

	vma->mapped = true;

	if (!vm)
		return 0;

	/*
	 * NOTE: iommu/io-pgtable can allocate pages, so we cannot hold
	 * a lock across map/unmap which is also used in the job_run()
	 * path, as this can cause deadlock in job_run() vs shrinker/
	 * reclaim.
	 *
	 * Revisit this if we can come up with a scheme to pre-alloc pages
	 * for the pgtable in map/unmap ops.
	 */
	ret = vm->mmu->funcs->map(vm->mmu, vma->iova, sgt, size, prot);

	if (ret) {
		vma->mapped = false;
	}

	return ret;
}

/* Close an iova.  Warn if it is still in use */
void msm_gem_vma_close(struct msm_gem_vma *vma)
{
	struct msm_gem_vm *vm = vma->vm;

	GEM_WARN_ON(vma->mapped);

	spin_lock(&vm->lock);
	if (vma->iova)
		drm_mm_remove_node(&vma->node);
	spin_unlock(&vm->lock);

	vma->iova = 0;

	msm_gem_vm_put(vm);
}

/* Create a new vma and allocate an iova for it */
struct msm_gem_vma *
msm_gem_vma_new(struct msm_gem_vm *vm, struct drm_gem_object *obj,
		u64 range_start, u64 range_end)
{
	struct msm_gem_vma *vma;
	int ret;

	vma = kzalloc(sizeof(*vma), GFP_KERNEL);
	if (!vma)
		return ERR_PTR(-ENOMEM);

	vma->vm = vm;

	spin_lock(&vm->lock);
	ret = drm_mm_insert_node_in_range(&vm->mm, &vma->node,
					  obj->size, PAGE_SIZE, 0,
					  range_start, range_end, 0);
	spin_unlock(&vm->lock);

	if (ret)
		goto err_free_vma;

	vma->iova = vma->node.start;
	vma->mapped = false;

	INIT_LIST_HEAD(&vma->list);

	kref_get(&vm->kref);

	return vma;

err_free_vma:
	kfree(vma);
	return ERR_PTR(ret);
}

struct msm_gem_vm *
msm_gem_vm_create(struct msm_mmu *mmu, const char *name,
		u64 va_start, u64 size)
{
	struct msm_gem_vm *vm;

	if (IS_ERR(mmu))
		return ERR_CAST(mmu);

	vm = kzalloc(sizeof(*vm), GFP_KERNEL);
	if (!vm)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&vm->lock);
	vm->name = name;
	vm->mmu = mmu;
	vm->va_start = va_start;
	vm->va_size  = size;

	drm_mm_init(&vm->mm, va_start, size);

	kref_init(&vm->kref);

	return vm;
}
