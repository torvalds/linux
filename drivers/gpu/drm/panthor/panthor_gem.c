// SPDX-License-Identifier: GPL-2.0 or MIT
/* Copyright 2019 Linaro, Ltd, Rob Herring <robh@kernel.org> */
/* Copyright 2023 Collabora ltd. */

#include <linux/cleanup.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/slab.h>

#include <drm/panthor_drm.h>

#include "panthor_device.h"
#include "panthor_fw.h"
#include "panthor_gem.h"
#include "panthor_mmu.h"

#ifdef CONFIG_DEBUG_FS
static void panthor_gem_debugfs_bo_init(struct panthor_gem_object *bo)
{
	INIT_LIST_HEAD(&bo->debugfs.node);
}

static void panthor_gem_debugfs_bo_add(struct panthor_gem_object *bo)
{
	struct panthor_device *ptdev = container_of(bo->base.base.dev,
						    struct panthor_device, base);

	bo->debugfs.creator.tgid = current->group_leader->pid;
	get_task_comm(bo->debugfs.creator.process_name, current->group_leader);

	mutex_lock(&ptdev->gems.lock);
	list_add_tail(&bo->debugfs.node, &ptdev->gems.node);
	mutex_unlock(&ptdev->gems.lock);
}

static void panthor_gem_debugfs_bo_rm(struct panthor_gem_object *bo)
{
	struct panthor_device *ptdev = container_of(bo->base.base.dev,
						    struct panthor_device, base);

	if (list_empty(&bo->debugfs.node))
		return;

	mutex_lock(&ptdev->gems.lock);
	list_del_init(&bo->debugfs.node);
	mutex_unlock(&ptdev->gems.lock);
}

static void panthor_gem_debugfs_set_usage_flags(struct panthor_gem_object *bo, u32 usage_flags)
{
	bo->debugfs.flags = usage_flags;
	panthor_gem_debugfs_bo_add(bo);
}
#else
static void panthor_gem_debugfs_bo_rm(struct panthor_gem_object *bo) {}
static void panthor_gem_debugfs_set_usage_flags(struct panthor_gem_object *bo, u32 usage_flags) {}
static void panthor_gem_debugfs_bo_init(struct panthor_gem_object *bo) {}
#endif

static void panthor_gem_free_object(struct drm_gem_object *obj)
{
	struct panthor_gem_object *bo = to_panthor_bo(obj);
	struct drm_gem_object *vm_root_gem = bo->exclusive_vm_root_gem;

	panthor_gem_debugfs_bo_rm(bo);

	/*
	 * Label might have been allocated with kstrdup_const(),
	 * we need to take that into account when freeing the memory
	 */
	kfree_const(bo->label.str);

	mutex_destroy(&bo->label.lock);

	drm_gem_free_mmap_offset(&bo->base.base);
	drm_gem_shmem_free(&bo->base);
	drm_gem_object_put(vm_root_gem);
}

/**
 * panthor_kernel_bo_destroy() - Destroy a kernel buffer object
 * @bo: Kernel buffer object to destroy. If NULL or an ERR_PTR(), the destruction
 * is skipped.
 */
void panthor_kernel_bo_destroy(struct panthor_kernel_bo *bo)
{
	struct panthor_vm *vm;
	int ret;

	if (IS_ERR_OR_NULL(bo))
		return;

	vm = bo->vm;
	panthor_kernel_bo_vunmap(bo);

	if (drm_WARN_ON(bo->obj->dev,
			to_panthor_bo(bo->obj)->exclusive_vm_root_gem != panthor_vm_root_gem(vm)))
		goto out_free_bo;

	ret = panthor_vm_unmap_range(vm, bo->va_node.start, bo->va_node.size);
	if (ret)
		goto out_free_bo;

	panthor_vm_free_va(vm, &bo->va_node);
	drm_gem_object_put(bo->obj);

out_free_bo:
	panthor_vm_put(vm);
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
 * @name: Descriptive label of the BO's contents
 *
 * Return: A valid pointer in case of success, an ERR_PTR() otherwise.
 */
struct panthor_kernel_bo *
panthor_kernel_bo_create(struct panthor_device *ptdev, struct panthor_vm *vm,
			 size_t size, u32 bo_flags, u32 vm_map_flags,
			 u64 gpu_va, const char *name)
{
	struct drm_gem_shmem_object *obj;
	struct panthor_kernel_bo *kbo;
	struct panthor_gem_object *bo;
	u32 debug_flags = PANTHOR_DEBUGFS_GEM_USAGE_FLAG_KERNEL;
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
	kbo->obj = &obj->base;
	bo->flags = bo_flags;

	if (vm == panthor_fw_vm(ptdev))
		debug_flags |= PANTHOR_DEBUGFS_GEM_USAGE_FLAG_FW_MAPPED;

	panthor_gem_kernel_bo_set_label(kbo, name);
	panthor_gem_debugfs_set_usage_flags(to_panthor_bo(kbo->obj), debug_flags);

	/* The system and GPU MMU page size might differ, which becomes a
	 * problem for FW sections that need to be mapped at explicit address
	 * since our PAGE_SIZE alignment might cover a VA range that's
	 * expected to be used for another section.
	 * Make sure we never map more than we need.
	 */
	size = ALIGN(size, panthor_vm_page_size(vm));
	ret = panthor_vm_alloc_va(vm, gpu_va, size, &kbo->va_node);
	if (ret)
		goto err_put_obj;

	ret = panthor_vm_map_bo_range(vm, bo, 0, size, kbo->va_node.start, vm_map_flags);
	if (ret)
		goto err_free_va;

	kbo->vm = panthor_vm_get(vm);
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

static struct dma_buf *
panthor_gem_prime_export(struct drm_gem_object *obj, int flags)
{
	/* We can't export GEMs that have an exclusive VM. */
	if (to_panthor_bo(obj)->exclusive_vm_root_gem)
		return ERR_PTR(-EINVAL);

	return drm_gem_prime_export(obj, flags);
}

static enum drm_gem_object_status panthor_gem_status(struct drm_gem_object *obj)
{
	struct panthor_gem_object *bo = to_panthor_bo(obj);
	enum drm_gem_object_status res = 0;

	if (drm_gem_is_imported(&bo->base.base) || bo->base.pages)
		res |= DRM_GEM_OBJECT_RESIDENT;

	return res;
}

static const struct drm_gem_object_funcs panthor_gem_funcs = {
	.free = panthor_gem_free_object,
	.print_info = drm_gem_shmem_object_print_info,
	.pin = drm_gem_shmem_object_pin,
	.unpin = drm_gem_shmem_object_unpin,
	.get_sg_table = drm_gem_shmem_object_get_sg_table,
	.vmap = drm_gem_shmem_object_vmap,
	.vunmap = drm_gem_shmem_object_vunmap,
	.mmap = drm_gem_shmem_object_mmap,
	.status = panthor_gem_status,
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
	mutex_init(&obj->label.lock);

	panthor_gem_debugfs_bo_init(obj);

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

	panthor_gem_debugfs_set_usage_flags(bo, 0);

	/* If this is a write-combine mapping, we query the sgt to force a CPU
	 * cache flush (dma_map_sgtable() is called when the sgt is created).
	 * This ensures the zero-ing is visible to any uncached mapping created
	 * by vmap/mmap.
	 * FIXME: Ideally this should be done when pages are allocated, not at
	 * BO creation time.
	 */
	if (shmem->map_wc) {
		struct sg_table *sgt;

		sgt = drm_gem_shmem_get_pages_sgt(shmem);
		if (IS_ERR(sgt)) {
			ret = PTR_ERR(sgt);
			goto out_put_gem;
		}
	}

	/*
	 * Allocate an id of idr table where the obj is registered
	 * and handle has the id what user can see.
	 */
	ret = drm_gem_handle_create(file, &shmem->base, handle);
	if (!ret)
		*size = bo->base.base.size;

out_put_gem:
	/* drop reference from allocate - handle holds it now. */
	drm_gem_object_put(&shmem->base);

	return ret;
}

void
panthor_gem_bo_set_label(struct drm_gem_object *obj, const char *label)
{
	struct panthor_gem_object *bo = to_panthor_bo(obj);
	const char *old_label;

	scoped_guard(mutex, &bo->label.lock) {
		old_label = bo->label.str;
		bo->label.str = label;
	}

	kfree_const(old_label);
}

void
panthor_gem_kernel_bo_set_label(struct panthor_kernel_bo *bo, const char *label)
{
	const char *str;

	/* We should never attempt labelling a UM-exposed GEM object */
	if (drm_WARN_ON(bo->obj->dev, bo->obj->handle_count > 0))
		return;

	if (!label)
		return;

	str = kstrdup_const(label, GFP_KERNEL);
	if (!str) {
		/* Failing to allocate memory for a label isn't a fatal condition */
		drm_warn(bo->obj->dev, "Not enough memory to allocate BO label");
		return;
	}

	panthor_gem_bo_set_label(bo->obj, str);
}

#ifdef CONFIG_DEBUG_FS
struct gem_size_totals {
	size_t size;
	size_t resident;
	size_t reclaimable;
};

static void panthor_gem_debugfs_print_flag_names(struct seq_file *m)
{
	int len;
	int i;

	static const char * const gem_state_flags_names[] = {
		[PANTHOR_DEBUGFS_GEM_STATE_IMPORTED_BIT] = "imported",
		[PANTHOR_DEBUGFS_GEM_STATE_EXPORTED_BIT] = "exported",
	};

	static const char * const gem_usage_flags_names[] = {
		[PANTHOR_DEBUGFS_GEM_USAGE_KERNEL_BIT] = "kernel",
		[PANTHOR_DEBUGFS_GEM_USAGE_FW_MAPPED_BIT] = "fw-mapped",
	};

	seq_puts(m, "GEM state flags: ");
	for (i = 0, len = ARRAY_SIZE(gem_state_flags_names); i < len; i++) {
		if (!gem_state_flags_names[i])
			continue;
		seq_printf(m, "%s (0x%x)%s", gem_state_flags_names[i],
			   (u32)BIT(i), (i < len - 1) ? ", " : "\n");
	}

	seq_puts(m, "GEM usage flags: ");
	for (i = 0, len = ARRAY_SIZE(gem_usage_flags_names); i < len; i++) {
		if (!gem_usage_flags_names[i])
			continue;
		seq_printf(m, "%s (0x%x)%s", gem_usage_flags_names[i],
			   (u32)BIT(i), (i < len - 1) ? ", " : "\n\n");
	}
}

static void panthor_gem_debugfs_bo_print(struct panthor_gem_object *bo,
					 struct seq_file *m,
					 struct gem_size_totals *totals)
{
	unsigned int refcount = kref_read(&bo->base.base.refcount);
	char creator_info[32] = {};
	size_t resident_size;
	u32 gem_usage_flags = bo->debugfs.flags;
	u32 gem_state_flags = 0;

	/* Skip BOs being destroyed. */
	if (!refcount)
		return;

	resident_size = bo->base.pages ? bo->base.base.size : 0;

	snprintf(creator_info, sizeof(creator_info),
		 "%s/%d", bo->debugfs.creator.process_name, bo->debugfs.creator.tgid);
	seq_printf(m, "%-32s%-16d%-16d%-16zd%-16zd0x%-16lx",
		   creator_info,
		   bo->base.base.name,
		   refcount,
		   bo->base.base.size,
		   resident_size,
		   drm_vma_node_start(&bo->base.base.vma_node));

	if (bo->base.base.import_attach)
		gem_state_flags |= PANTHOR_DEBUGFS_GEM_STATE_FLAG_IMPORTED;
	if (bo->base.base.dma_buf)
		gem_state_flags |= PANTHOR_DEBUGFS_GEM_STATE_FLAG_EXPORTED;

	seq_printf(m, "0x%-8x 0x%-10x", gem_state_flags, gem_usage_flags);

	scoped_guard(mutex, &bo->label.lock) {
		seq_printf(m, "%s\n", bo->label.str ? : "");
	}

	totals->size += bo->base.base.size;
	totals->resident += resident_size;
	if (bo->base.madv > 0)
		totals->reclaimable += resident_size;
}

void panthor_gem_debugfs_print_bos(struct panthor_device *ptdev,
				   struct seq_file *m)
{
	struct gem_size_totals totals = {0};
	struct panthor_gem_object *bo;

	panthor_gem_debugfs_print_flag_names(m);

	seq_puts(m, "created-by                      global-name     refcount        size            resident-size   file-offset       state      usage       label\n");
	seq_puts(m, "----------------------------------------------------------------------------------------------------------------------------------------------\n");

	scoped_guard(mutex, &ptdev->gems.lock) {
		list_for_each_entry(bo, &ptdev->gems.node, debugfs.node) {
			panthor_gem_debugfs_bo_print(bo, m, &totals);
		}
	}

	seq_puts(m, "==============================================================================================================================================\n");
	seq_printf(m, "Total size: %zd, Total resident: %zd, Total reclaimable: %zd\n",
		   totals.size, totals.resident, totals.reclaimable);
}
#endif
