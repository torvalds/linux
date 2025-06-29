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

/* Actually unmap memory for the vma */
void msm_gem_vma_unmap(struct drm_gpuva *vma)
{
	struct msm_gem_vma *msm_vma = to_msm_vma(vma);
	struct msm_gem_vm *vm = to_msm_vm(vma->vm);
	unsigned size = vma->va.range;

	/* Don't do anything if the memory isn't mapped */
	if (!msm_vma->mapped)
		return;

	vm->mmu->funcs->unmap(vm->mmu, vma->va.addr, size);

	msm_vma->mapped = false;
}

/* Map and pin vma: */
int
msm_gem_vma_map(struct drm_gpuva *vma, int prot, struct sg_table *sgt)
{
	struct msm_gem_vma *msm_vma = to_msm_vma(vma);
	struct msm_gem_vm *vm = to_msm_vm(vma->vm);
	int ret;

	if (GEM_WARN_ON(!vma->va.addr))
		return -EINVAL;

	if (msm_vma->mapped)
		return 0;

	msm_vma->mapped = true;

	/*
	 * NOTE: iommu/io-pgtable can allocate pages, so we cannot hold
	 * a lock across map/unmap which is also used in the job_run()
	 * path, as this can cause deadlock in job_run() vs shrinker/
	 * reclaim.
	 *
	 * Revisit this if we can come up with a scheme to pre-alloc pages
	 * for the pgtable in map/unmap ops.
	 */
	ret = vm->mmu->funcs->map(vm->mmu, vma->va.addr, sgt,
				  vma->gem.offset, vma->va.range,
				  prot);
	if (ret) {
		msm_vma->mapped = false;
	}

	return ret;
}

/* Close an iova.  Warn if it is still in use */
void msm_gem_vma_close(struct drm_gpuva *vma)
{
	struct msm_gem_vm *vm = to_msm_vm(vma->vm);
	struct msm_gem_vma *msm_vma = to_msm_vma(vma);

	GEM_WARN_ON(msm_vma->mapped);

	drm_gpuvm_resv_assert_held(&vm->base);

	if (vma->va.addr && vm->managed)
		drm_mm_remove_node(&msm_vma->node);

	drm_gpuva_remove(vma);
	drm_gpuva_unlink(vma);

	kfree(vma);
}

/* Create a new vma and allocate an iova for it */
struct drm_gpuva *
msm_gem_vma_new(struct drm_gpuvm *gpuvm, struct drm_gem_object *obj,
		u64 offset, u64 range_start, u64 range_end)
{
	struct msm_gem_vm *vm = to_msm_vm(gpuvm);
	struct drm_gpuvm_bo *vm_bo;
	struct msm_gem_vma *vma;
	int ret;

	drm_gpuvm_resv_assert_held(&vm->base);

	vma = kzalloc(sizeof(*vma), GFP_KERNEL);
	if (!vma)
		return ERR_PTR(-ENOMEM);

	if (vm->managed) {
		BUG_ON(offset != 0);
		ret = drm_mm_insert_node_in_range(&vm->mm, &vma->node,
						obj->size, PAGE_SIZE, 0,
						range_start, range_end, 0);

		if (ret)
			goto err_free_vma;

		range_start = vma->node.start;
		range_end   = range_start + obj->size;
	}

	GEM_WARN_ON((range_end - range_start) > obj->size);

	drm_gpuva_init(&vma->base, range_start, range_end - range_start, obj, offset);
	vma->mapped = false;

	ret = drm_gpuva_insert(&vm->base, &vma->base);
	if (ret)
		goto err_free_range;

	vm_bo = drm_gpuvm_bo_obtain(&vm->base, obj);
	if (IS_ERR(vm_bo)) {
		ret = PTR_ERR(vm_bo);
		goto err_va_remove;
	}

	drm_gpuvm_bo_extobj_add(vm_bo);
	drm_gpuva_link(&vma->base, vm_bo);
	GEM_WARN_ON(drm_gpuvm_bo_put(vm_bo));

	return &vma->base;

err_va_remove:
	drm_gpuva_remove(&vma->base);
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
struct drm_gpuvm *
msm_gem_vm_create(struct drm_device *drm, struct msm_mmu *mmu, const char *name,
		  u64 va_start, u64 va_size, bool managed)
{
	/*
	 * We mostly want to use DRM_GPUVM_RESV_PROTECTED, except that
	 * makes drm_gpuvm_bo_evict() a no-op for extobjs (ie. we loose
	 * tracking that an extobj is evicted) :facepalm:
	 */
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

	vm->mmu = mmu;
	vm->managed = managed;

	drm_mm_init(&vm->mm, va_start, va_size);

	return &vm->base;

err_free_vm:
	kfree(vm);
	return ERR_PTR(ret);

}
