/* SPDX-License-Identifier: GPL-2.0 or MIT */
/* Copyright 2019 Linaro, Ltd, Rob Herring <robh@kernel.org> */
/* Copyright 2023 Collabora ltd. */

#ifndef __PANTHOR_GEM_H__
#define __PANTHOR_GEM_H__

#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_mm.h>

#include <linux/iosys-map.h>
#include <linux/rwsem.h>

struct panthor_vm;

/**
 * struct panthor_gem_object - Driver specific GEM object.
 */
struct panthor_gem_object {
	/** @base: Inherit from drm_gem_shmem_object. */
	struct drm_gem_shmem_object base;

	/**
	 * @exclusive_vm_root_gem: Root GEM of the exclusive VM this GEM object
	 * is attached to.
	 *
	 * If @exclusive_vm_root_gem != NULL, any attempt to bind the GEM to a
	 * different VM will fail.
	 *
	 * All FW memory objects have this field set to the root GEM of the MCU
	 * VM.
	 */
	struct drm_gem_object *exclusive_vm_root_gem;

	/**
	 * @gpuva_list_lock: Custom GPUVA lock.
	 *
	 * Used to protect insertion of drm_gpuva elements to the
	 * drm_gem_object.gpuva.list list.
	 *
	 * We can't use the GEM resv for that, because drm_gpuva_link() is
	 * called in a dma-signaling path, where we're not allowed to take
	 * resv locks.
	 */
	struct mutex gpuva_list_lock;

	/** @flags: Combination of drm_panthor_bo_flags flags. */
	u32 flags;
};

/**
 * struct panthor_kernel_bo - Kernel buffer object.
 *
 * These objects are only manipulated by the kernel driver and not
 * directly exposed to the userspace. The GPU address of a kernel
 * BO might be passed to userspace though.
 */
struct panthor_kernel_bo {
	/**
	 * @obj: The GEM object backing this kernel buffer object.
	 */
	struct drm_gem_object *obj;

	/**
	 * @vm: VM this private buffer is attached to.
	 */
	struct panthor_vm *vm;

	/**
	 * @va_node: VA space allocated to this GEM.
	 */
	struct drm_mm_node va_node;

	/**
	 * @kmap: Kernel CPU mapping of @gem.
	 */
	void *kmap;
};

static inline
struct panthor_gem_object *to_panthor_bo(struct drm_gem_object *obj)
{
	return container_of(to_drm_gem_shmem_obj(obj), struct panthor_gem_object, base);
}

struct drm_gem_object *panthor_gem_create_object(struct drm_device *ddev, size_t size);

int
panthor_gem_create_with_handle(struct drm_file *file,
			       struct drm_device *ddev,
			       struct panthor_vm *exclusive_vm,
			       u64 *size, u32 flags, uint32_t *handle);

static inline u64
panthor_kernel_bo_gpuva(struct panthor_kernel_bo *bo)
{
	return bo->va_node.start;
}

static inline size_t
panthor_kernel_bo_size(struct panthor_kernel_bo *bo)
{
	return bo->obj->size;
}

static inline int
panthor_kernel_bo_vmap(struct panthor_kernel_bo *bo)
{
	struct iosys_map map;
	int ret;

	if (bo->kmap)
		return 0;

	ret = drm_gem_vmap_unlocked(bo->obj, &map);
	if (ret)
		return ret;

	bo->kmap = map.vaddr;
	return 0;
}

static inline void
panthor_kernel_bo_vunmap(struct panthor_kernel_bo *bo)
{
	if (bo->kmap) {
		struct iosys_map map = IOSYS_MAP_INIT_VADDR(bo->kmap);

		drm_gem_vunmap_unlocked(bo->obj, &map);
		bo->kmap = NULL;
	}
}

struct panthor_kernel_bo *
panthor_kernel_bo_create(struct panthor_device *ptdev, struct panthor_vm *vm,
			 size_t size, u32 bo_flags, u32 vm_map_flags,
			 u64 gpu_va);

void panthor_kernel_bo_destroy(struct panthor_kernel_bo *bo);

#endif /* __PANTHOR_GEM_H__ */
