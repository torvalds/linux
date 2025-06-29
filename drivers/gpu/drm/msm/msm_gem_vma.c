// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include "msm_drv.h"
#include "msm_gem.h"
#include "msm_mmu.h"

static void
msm_gem_vm_free(struct drm_gpuvm *gpuvm)
{
	struct msm_gem_vm *vm = container_of(gpuvm, struct msm_gem_vm, base);

	drm_mm_takedown(&vm->mm);
	if (vm->mmu)
		vm->mmu->funcs->destroy(vm->mmu);
	put_pid(vm->pid);
	kfree(vm);
}


void msm_gem_vm_put(struct msm_gem_vm *vm)
{
	if (vm)
		drm_gpuvm_put(&vm->base);
}

struct msm_gem_vm *
msm_gem_vm_get(struct msm_gem_vm *vm)
{
	if (!IS_ERR_OR_NULL(vm))
		drm_gpuvm_get(&vm->base);

	return vm;
}

/* Actually unmap memory for the vma */
void msm_gem_vma_purge(struct msm_gem_vma *vma)
{
	struct msm_gem_vm *vm = to_msm_vm(vma->base.vm);
	unsigned size = vma->base.va.range;

	/* Don't do anything if the memory isn't mapped */
	if (!vma->mapped)
		return;

	vm->mmu->funcs->unmap(vm->mmu, vma->base.va.addr, size);

	vma->mapped = false;
}

/* Map and pin vma: */
int
msm_gem_vma_map(struct msm_gem_vma *vma, int prot,
		struct sg_table *sgt, int size)
{
	struct msm_gem_vm *vm = to_msm_vm(vma->base.vm);
	int ret;

	if (GEM_WARN_ON(!vma->base.va.addr))
		return -EINVAL;

	if (vma->mapped)
		return 0;

	vma->mapped = true;

	/*
	 * NOTE: iommu/io-pgtable can allocate pages, so we cannot hold
	 * a lock across map/unmap which is also used in the job_run()
	 * path, as this can cause deadlock in job_run() vs shrinker/
	 * reclaim.
	 *
	 * Revisit this if we can come up with a scheme to pre-alloc pages
	 * for the pgtable in map/unmap ops.
	 */
	ret = vm->mmu->funcs->map(vm->mmu, vma->base.va.addr, sgt, size, prot);

	if (ret) {
		vma->mapped = false;
	}

	return ret;
}

/* Close an iova.  Warn if it is still in use */
void msm_gem_vma_close(struct msm_gem_vma *vma)
{
	struct msm_gem_vm *vm = to_msm_vm(vma->base.vm);

	GEM_WARN_ON(vma->mapped);

	spin_lock(&vm->mm_lock);
	if (vma->base.va.addr)
		drm_mm_remove_node(&vma->node);
	spin_unlock(&vm->mm_lock);

	mutex_lock(&vm->vm_lock);
	drm_gpuva_remove(&vma->base);
	drm_gpuva_unlink(&vma->base);
	mutex_unlock(&vm->vm_lock);

	kfree(vma);
}

/* Create a new vma and allocate an iova for it */
struct msm_gem_vma *
msm_gem_vma_new(struct msm_gem_vm *vm, struct drm_gem_object *obj,
		u64 range_start, u64 range_end)
{
	struct drm_gpuvm_bo *vm_bo;
	struct msm_gem_vma *vma;
	int ret;

	vma = kzalloc(sizeof(*vma), GFP_KERNEL);
	if (!vma)
		return ERR_PTR(-ENOMEM);

	if (vm->managed) {
		spin_lock(&vm->mm_lock);
		ret = drm_mm_insert_node_in_range(&vm->mm, &vma->node,
						obj->size, PAGE_SIZE, 0,
						range_start, range_end, 0);
		spin_unlock(&vm->mm_lock);

		if (ret)
			goto err_free_vma;

		range_start = vma->node.start;
		range_end   = range_start + obj->size;
	}

	GEM_WARN_ON((range_end - range_start) > obj->size);

	drm_gpuva_init(&vma->base, range_start, range_end - range_start, obj, 0);
	vma->mapped = false;

	mutex_lock(&vm->vm_lock);
	ret = drm_gpuva_insert(&vm->base, &vma->base);
	mutex_unlock(&vm->vm_lock);
	if (ret)
		goto err_free_range;

	vm_bo = drm_gpuvm_bo_obtain(&vm->base, obj);
	if (IS_ERR(vm_bo)) {
		ret = PTR_ERR(vm_bo);
		goto err_va_remove;
	}

	mutex_lock(&vm->vm_lock);
	drm_gpuvm_bo_extobj_add(vm_bo);
	drm_gpuva_link(&vma->base, vm_bo);
	mutex_unlock(&vm->vm_lock);
	GEM_WARN_ON(drm_gpuvm_bo_put(vm_bo));

	return vma;

err_va_remove:
	mutex_lock(&vm->vm_lock);
	drm_gpuva_remove(&vma->base);
	mutex_unlock(&vm->vm_lock);
err_free_range:
	if (vm->managed)
		drm_mm_remove_node(&vma->node);
err_free_vma:
	kfree(vma);
	return ERR_PTR(ret);
}

static const struct drm_gpuvm_ops msm_gpuvm_ops = {
	.vm_free = msm_gem_vm_free,
};

/**
 * msm_gem_vm_create() - Create and initialize a &msm_gem_vm
 * @drm: the drm device
 * @mmu: the backing MMU objects handling mapping/unmapping
 * @name: the name of the VM
 * @va_start: the start offset of the VA space
 * @va_size: the size of the VA space
 * @managed: is it a kernel managed VM?
 *
 * In a kernel managed VM, the kernel handles address allocation, and only
 * synchronous operations are supported.  In a user managed VM, userspace
 * handles virtual address allocation, and both async and sync operations
 * are supported.
 */
struct msm_gem_vm *
msm_gem_vm_create(struct drm_device *drm, struct msm_mmu *mmu, const char *name,
		  u64 va_start, u64 va_size, bool managed)
{
	enum drm_gpuvm_flags flags = 0;
	struct msm_gem_vm *vm;
	struct drm_gem_object *dummy_gem;
	int ret = 0;

	if (IS_ERR(mmu))
		return ERR_CAST(mmu);

	vm = kzalloc(sizeof(*vm), GFP_KERNEL);
	if (!vm)
		return ERR_PTR(-ENOMEM);

	dummy_gem = drm_gpuvm_resv_object_alloc(drm);
	if (!dummy_gem) {
		ret = -ENOMEM;
		goto err_free_vm;
	}

	drm_gpuvm_init(&vm->base, name, flags, drm, dummy_gem,
		       va_start, va_size, 0, 0, &msm_gpuvm_ops);
	drm_gem_object_put(dummy_gem);

	spin_lock_init(&vm->mm_lock);
	mutex_init(&vm->vm_lock);

	vm->mmu = mmu;
	vm->managed = managed;

	drm_mm_init(&vm->mm, va_start, va_size);

	return vm;

err_free_vm:
	kfree(vm);
	return ERR_PTR(ret);

}
