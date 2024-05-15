// SPDX-License-Identifier: GPL-2.0 or MIT
/* Copyright 2019 Linaro, Ltd, Rob Herring <robh@kernel.org> */
/* Copyright 2023 Collabora ltd. */

#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/slab.h>

#include <drm/panthor_drm.h>

#include "panthor_device.h"
#include "panthor_gem.h"
#include "panthor_mmu.h"

static void panthor_gem_free_object(struct drm_gem_object *obj)
{
	struct panthor_gem_object *bo = to_panthor_bo(obj);
	struct drm_gem_object *vm_root_gem = bo->exclusive_vm_root_gem;

	drm_gem_free_mmap_offset(&bo->base.base);
	mutex_destroy(&bo->gpuva_list_lock);
	drm_gem_shmem_free(&bo->base);
	drm_gem_object_put(vm_root_gem);
}

/**
 * panthor_kernel_bo_destroy() - Destroy a kernel buffer object
 * @vm: The VM this BO was mapped to.
 * @bo: Kernel buffer object to destroy. If NULL or an ERR_PTR(), the destruction
 * is skipped.
 */
void panthor_kernel_bo_destroy(struct panthor_vm *vm,
			       struct panthor_kernel_bo *bo)
{
	int ret;

	if (IS_ERR_OR_NULL(bo))
		return;

	panthor_kernel_bo_vunmap(bo);

	if (drm_WARN_ON(bo->obj->dev,
			to_panthor_bo(bo->obj)->exclusive_vm_root_gem != panthor_vm_root_gem(vm)))
		goto out_free_bo;

	ret = panthor_vm_unmap_range(vm, bo->va_node.start,
				     panthor_kernel_bo_size(bo));
	if (ret)
		goto out_free_bo;

	panthor_vm_free_va(vm, &bo->va_node);
	drm_gem_object_put(bo->obj);

out_free_bo:
	kfree(bo);
}

/**
 * panthor_kernel_bo_create() - Create and map a GEM object to a VM
 * @ptdev: Device.
 * @vm: VM to map the GEM to. If NULL, the kernel object is not GPU mapped.
 * @size: Size of the buffer object.
 * @bo_flags: Combination of drm_panthor_bo_flags flags.
 * @vm_map_flags: Combination of drm_panthor_vm_bind_op_flags (only those
 * that are related to map operations).
 * @gpu_va: GPU address assigned when mapping to the VM.
 * If gpu_va == PANTHOR_VM_KERNEL_AUTO_VA, the virtual address will be
 * automatically allocated.
 *
 * Return: A valid pointer in case of success, an ERR_PTR() otherwise.
 */
struct panthor_kernel_bo *
panthor_kernel_bo_create(struct panthor_device *ptdev, struct panthor_vm *vm,
			 size_t size, u32 bo_flags, u32 vm_map_flags,
			 u64 gpu_va)
{
	struct drm_gem_shmem_object *obj;
	struct panthor_kernel_bo *kbo;
	struct panthor_gem_object *bo;
	int ret;

	if (drm_WARN_ON(&ptdev->base, !vm))
		return ERR_PTR(-EINVAL);

	kbo = kzalloc(sizeof(*kbo), GFP_KERNEL);
	if (!kbo)
		return ERR_PTR(-ENOMEM);

	obj = drm_gem_shmem_create(&ptdev->base, size);
	if (IS_ERR(obj)) {
		ret = PTR_ERR(obj);
		goto err_free_bo;
	}

	bo = to_panthor_bo(&obj->base);
	size = obj->base.size;
	kbo->obj = &obj->base;
	bo->flags = bo_flags;

	ret = panthor_vm_alloc_va(vm, gpu_va, size, &kbo->va_node);
	if (ret)
		goto err_put_obj;

	ret = panthor_vm_map_bo_range(vm, bo, 0, size, kbo->va_node.start, vm_map_flags);
	if (ret)
		goto err_free_va;

	bo->exclusive_vm_root_gem = panthor_vm_root_gem(vm);
	drm_gem_object_get(bo->exclusive_vm_root_gem);
	bo->base.base.resv = bo->exclusive_vm_root_gem->resv;
	return kbo;

err_free_va:
	panthor_vm_free_va(vm, &kbo->va_node);

err_put_obj:
	drm_gem_object_put(&obj->base);

err_free_bo:
	kfree(kbo);
	return ERR_PTR(ret);
}

static int panthor_gem_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	struct panthor_gem_object *bo = to_panthor_bo(obj);

	/* Don't allow mmap on objects that have the NO_MMAP flag set. */
	if (bo->flags & DRM_PANTHOR_BO_NO_MMAP)
		return -EINVAL;

	return drm_gem_shmem_object_mmap(obj, vma);
}

static struct dma_buf *
panthor_gem_prime_export(struct drm_gem_object *obj, int flags)
{
	/* We can't export GEMs that have an exclusive VM. */
	if (to_panthor_bo(obj)->exclusive_vm_root_gem)
		return ERR_PTR(-EINVAL);

	return drm_gem_prime_export(obj, flags);
}

static const struct drm_gem_object_funcs panthor_gem_funcs = {
	.free = panthor_gem_free_object,
	.print_info = drm_gem_shmem_object_print_info,
	.pin = drm_gem_shmem_object_pin,
	.unpin = drm_gem_shmem_object_unpin,
	.get_sg_table = drm_gem_shmem_object_get_sg_table,
	.vmap = drm_gem_shmem_object_vmap,
	.vunmap = drm_gem_shmem_object_vunmap,
	.mmap = panthor_gem_mmap,
	.export = panthor_gem_prime_export,
	.vm_ops = &drm_gem_shmem_vm_ops,
};

/**
 * panthor_gem_create_object - Implementation of driver->gem_create_object.
 * @ddev: DRM device
 * @size: Size in bytes of the memory the object will reference
 *
 * This lets the GEM helpers allocate object structs for us, and keep
 * our BO stats correct.
 */
struct drm_gem_object *panthor_gem_create_object(struct drm_device *ddev, size_t size)
{
	struct panthor_device *ptdev = container_of(ddev, struct panthor_device, base);
	struct panthor_gem_object *obj;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	obj->base.base.funcs = &panthor_gem_funcs;
	obj->base.map_wc = !ptdev->coherent;
	mutex_init(&obj->gpuva_list_lock);
	drm_gem_gpuva_set_lock(&obj->base.base, &obj->gpuva_list_lock);

	return &obj->base.base;
}

/**
 * panthor_gem_create_with_handle() - Create a GEM object and attach it to a handle.
 * @file: DRM file.
 * @ddev: DRM device.
 * @exclusive_vm: Exclusive VM. Not NULL if the GEM object can't be shared.
 * @size: Size of the GEM object to allocate.
 * @flags: Combination of drm_panthor_bo_flags flags.
 * @handle: Pointer holding the handle pointing to the new GEM object.
 *
 * Return: Zero on success
 */
int
panthor_gem_create_with_handle(struct drm_file *file,
			       struct drm_device *ddev,
			       struct panthor_vm *exclusive_vm,
			       u64 *size, u32 flags, u32 *handle)
{
	int ret;
	struct drm_gem_shmem_object *shmem;
	struct panthor_gem_object *bo;

	shmem = drm_gem_shmem_create(ddev, *size);
	if (IS_ERR(shmem))
		return PTR_ERR(shmem);

	bo = to_panthor_bo(&shmem->base);
	bo->flags = flags;

	if (exclusive_vm) {
		bo->exclusive_vm_root_gem = panthor_vm_root_gem(exclusive_vm);
		drm_gem_object_get(bo->exclusive_vm_root_gem);
		bo->base.base.resv = bo->exclusive_vm_root_gem->resv;
	}

	/*
	 * Allocate an id of idr table where the obj is registered
	 * and handle has the id what user can see.
	 */
	ret = drm_gem_handle_create(file, &shmem->base, handle);
	if (!ret)
		*size = bo->base.base.size;

	/* drop reference from allocate - handle holds it now. */
	drm_gem_object_put(&shmem->base);

	return ret;
}
