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

#define PANTHOR_BO_LABEL_MAXLEN	4096

enum panthor_debugfs_gem_state_flags {
	PANTHOR_DEBUGFS_GEM_STATE_IMPORTED_BIT = 0,
	PANTHOR_DEBUGFS_GEM_STATE_EXPORTED_BIT = 1,

	/** @PANTHOR_DEBUGFS_GEM_STATE_FLAG_IMPORTED: GEM BO is PRIME imported. */
	PANTHOR_DEBUGFS_GEM_STATE_FLAG_IMPORTED = BIT(PANTHOR_DEBUGFS_GEM_STATE_IMPORTED_BIT),

	/** @PANTHOR_DEBUGFS_GEM_STATE_FLAG_EXPORTED: GEM BO is PRIME exported. */
	PANTHOR_DEBUGFS_GEM_STATE_FLAG_EXPORTED = BIT(PANTHOR_DEBUGFS_GEM_STATE_EXPORTED_BIT),
};

enum panthor_debugfs_gem_usage_flags {
	PANTHOR_DEBUGFS_GEM_USAGE_KERNEL_BIT = 0,
	PANTHOR_DEBUGFS_GEM_USAGE_FW_MAPPED_BIT = 1,

	/** @PANTHOR_DEBUGFS_GEM_USAGE_FLAG_KERNEL: BO is for kernel use only. */
	PANTHOR_DEBUGFS_GEM_USAGE_FLAG_KERNEL = BIT(PANTHOR_DEBUGFS_GEM_USAGE_KERNEL_BIT),

	/** @PANTHOR_DEBUGFS_GEM_USAGE_FLAG_FW_MAPPED: BO is mapped on the FW VM. */
	PANTHOR_DEBUGFS_GEM_USAGE_FLAG_FW_MAPPED = BIT(PANTHOR_DEBUGFS_GEM_USAGE_FW_MAPPED_BIT),
};

/**
 * struct panthor_gem_debugfs - GEM object's DebugFS list information
 */
struct panthor_gem_debugfs {
	/**
	 * @node: Node used to insert the object in the device-wide list of
	 * GEM objects, to display information about it through a DebugFS file.
	 */
	struct list_head node;

	/** @creator: Information about the UM process which created the GEM. */
	struct {
		/** @creator.process_name: Group leader name in owning thread's process */
		char process_name[TASK_COMM_LEN];

		/** @creator.tgid: PID of the thread's group leader within its process */
		pid_t tgid;
	} creator;

	/** @flags: Combination of panthor_debugfs_gem_usage_flags flags */
	u32 flags;
};

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

	/** @flags: Combination of drm_panthor_bo_flags flags. */
	u32 flags;

	/**
	 * @label: BO tagging fields. The label can be assigned within the
	 * driver itself or through a specific IOCTL.
	 */
	struct {
		/**
		 * @label.str: Pointer to NULL-terminated string,
		 */
		const char *str;

		/** @lock.str: Protects access to the @label.str field. */
		struct mutex lock;
	} label;

#ifdef CONFIG_DEBUG_FS
	struct panthor_gem_debugfs debugfs;
#endif
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

void panthor_gem_bo_set_label(struct drm_gem_object *obj, const char *label);
void panthor_gem_kernel_bo_set_label(struct panthor_kernel_bo *bo, const char *label);

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

	ret = drm_gem_vmap(bo->obj, &map);
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

		drm_gem_vunmap(bo->obj, &map);
		bo->kmap = NULL;
	}
}

struct panthor_kernel_bo *
panthor_kernel_bo_create(struct panthor_device *ptdev, struct panthor_vm *vm,
			 size_t size, u32 bo_flags, u32 vm_map_flags,
			 u64 gpu_va, const char *name);

void panthor_kernel_bo_destroy(struct panthor_kernel_bo *bo);

#ifdef CONFIG_DEBUG_FS
void panthor_gem_debugfs_print_bos(struct panthor_device *pfdev,
				   struct seq_file *m);
#endif

#endif /* __PANTHOR_GEM_H__ */
