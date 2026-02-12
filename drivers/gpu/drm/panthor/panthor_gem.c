// SPDX-License-Identifier: GPL-2.0 or MIT
/* Copyright 2019 Linaro, Ltd, Rob Herring <robh@kernel.org> */
/* Copyright 2023 Collabora ltd. */
/* Copyright 2025 Amazon.com, Inc. or its affiliates */

#include <linux/cleanup.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/slab.h>

#include <drm/drm_print.h>
#include <drm/panthor_drm.h>

#include "panthor_device.h"
#include "panthor_drv.h"
#include "panthor_fw.h"
#include "panthor_gem.h"
#include "panthor_mmu.h"

void panthor_gem_init(struct panthor_device *ptdev)
{
	int err;

	if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE) &&
	    !panthor_transparent_hugepage)
		return;

	err = drm_gem_huge_mnt_create(&ptdev->base, "within_size");
	if (drm_gem_get_huge_mnt(&ptdev->base))
		drm_info(&ptdev->base, "Using Transparent Hugepage\n");
	else if (err)
		drm_warn(&ptdev->base, "Can't use Transparent Hugepage (%d)\n",
			 err);
}

#ifdef CONFIG_DEBUG_FS
static void panthor_gem_debugfs_bo_init(struct panthor_gem_object *bo)
{
	INIT_LIST_HEAD(&bo->debugfs.node);
}

static void panthor_gem_debugfs_bo_add(struct panthor_gem_object *bo)
{
	struct panthor_device *ptdev = container_of(bo->base.base.dev,
						    struct panthor_device, base);

	bo->debugfs.creator.tgid = current->tgid;
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

static bool
should_map_wc(struct panthor_gem_object *bo, struct panthor_vm *exclusive_vm)
{
	struct panthor_device *ptdev = container_of(bo->base.base.dev, struct panthor_device, base);

	/* We can't do uncached mappings if the device is coherent,
	 * because the zeroing done by the shmem layer at page allocation
	 * time happens on a cached mapping which isn't CPU-flushed (at least
	 * not on Arm64 where the flush is deferred to PTE setup time, and
	 * only done conditionally based on the mapping permissions). We can't
	 * rely on dma_map_sgtable()/dma_sync_sgtable_for_xxx() either to flush
	 * those, because they are NOPed if dma_dev_coherent() returns true.
	 *
	 * FIXME: Note that this problem is going to pop up again when we
	 * decide to support mapping buffers with the NO_MMAP flag as
	 * non-shareable (AKA buffers accessed only by the GPU), because we
	 * need the same CPU flush to happen after page allocation, otherwise
	 * there's a risk of data leak or late corruption caused by a dirty
	 * cacheline being evicted. At this point we'll need a way to force
	 * CPU cache maintenance regardless of whether the device is coherent
	 * or not.
	 */
	if (ptdev->coherent)
		return false;

	/* Cached mappings are explicitly requested, so no write-combine. */
	if (bo->flags & DRM_PANTHOR_BO_WB_MMAP)
		return false;

	/* The default is write-combine. */
	return true;
}

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

	if (IS_ERR_OR_NULL(bo))
		return;

	vm = bo->vm;
	panthor_kernel_bo_vunmap(bo);

	drm_WARN_ON(bo->obj->dev,
		    to_panthor_bo(bo->obj)->exclusive_vm_root_gem != panthor_vm_root_gem(vm));
	panthor_vm_unmap_range(vm, bo->va_node.start, bo->va_node.size);
	panthor_vm_free_va(vm, &bo->va_node);
	drm_gem_object_put(bo->obj);
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
	bo->base.map_wc = should_map_wc(bo, vm);
	bo->exclusive_vm_root_gem = panthor_vm_root_gem(vm);
	drm_gem_object_get(bo->exclusive_vm_root_gem);
	bo->base.base.resv = bo->exclusive_vm_root_gem->resv;

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
	return kbo;

err_free_va:
	panthor_vm_free_va(vm, &kbo->va_node);

err_put_obj:
	drm_gem_object_put(&obj->base);

err_free_bo:
	kfree(kbo);
	return ERR_PTR(ret);
}

static struct sg_table *
panthor_gem_prime_map_dma_buf(struct dma_buf_attachment *attach,
			      enum dma_data_direction dir)
{
	struct sg_table *sgt = drm_gem_map_dma_buf(attach, dir);

	if (!IS_ERR(sgt))
		attach->priv = sgt;

	return sgt;
}

static void
panthor_gem_prime_unmap_dma_buf(struct dma_buf_attachment *attach,
				struct sg_table *sgt,
				enum dma_data_direction dir)
{
	attach->priv = NULL;
	drm_gem_unmap_dma_buf(attach, sgt, dir);
}

static int
panthor_gem_prime_begin_cpu_access(struct dma_buf *dma_buf,
				   enum dma_data_direction dir)
{
	struct drm_gem_object *obj = dma_buf->priv;
	struct drm_device *dev = obj->dev;
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);
	struct dma_buf_attachment *attach;

	dma_resv_lock(obj->resv, NULL);
	if (shmem->sgt)
		dma_sync_sgtable_for_cpu(dev->dev, shmem->sgt, dir);

	if (shmem->vaddr)
		invalidate_kernel_vmap_range(shmem->vaddr, shmem->base.size);

	list_for_each_entry(attach, &dma_buf->attachments, node) {
		struct sg_table *sgt = attach->priv;

		if (sgt)
			dma_sync_sgtable_for_cpu(attach->dev, sgt, dir);
	}
	dma_resv_unlock(obj->resv);

	return 0;
}

static int
panthor_gem_prime_end_cpu_access(struct dma_buf *dma_buf,
				 enum dma_data_direction dir)
{
	struct drm_gem_object *obj = dma_buf->priv;
	struct drm_device *dev = obj->dev;
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);
	struct dma_buf_attachment *attach;

	dma_resv_lock(obj->resv, NULL);
	list_for_each_entry(attach, &dma_buf->attachments, node) {
		struct sg_table *sgt = attach->priv;

		if (sgt)
			dma_sync_sgtable_for_device(attach->dev, sgt, dir);
	}

	if (shmem->vaddr)
		flush_kernel_vmap_range(shmem->vaddr, shmem->base.size);

	if (shmem->sgt)
		dma_sync_sgtable_for_device(dev->dev, shmem->sgt, dir);

	dma_resv_unlock(obj->resv);
	return 0;
}

static const struct dma_buf_ops panthor_dma_buf_ops = {
	.attach = drm_gem_map_attach,
	.detach = drm_gem_map_detach,
	.map_dma_buf = panthor_gem_prime_map_dma_buf,
	.unmap_dma_buf = panthor_gem_prime_unmap_dma_buf,
	.release = drm_gem_dmabuf_release,
	.mmap = drm_gem_dmabuf_mmap,
	.vmap = drm_gem_dmabuf_vmap,
	.vunmap = drm_gem_dmabuf_vunmap,
	.begin_cpu_access = panthor_gem_prime_begin_cpu_access,
	.end_cpu_access = panthor_gem_prime_end_cpu_access,
};

static struct dma_buf *
panthor_gem_prime_export(struct drm_gem_object *obj, int flags)
{
	struct drm_device *dev = obj->dev;
	struct dma_buf_export_info exp_info = {
		.exp_name = KBUILD_MODNAME,
		.owner = THIS_MODULE,
		.ops = &panthor_dma_buf_ops,
		.size = obj->size,
		.flags = flags,
		.priv = obj,
		.resv = obj->resv,
	};

	/* We can't export GEMs that have an exclusive VM. */
	if (to_panthor_bo(obj)->exclusive_vm_root_gem)
		return ERR_PTR(-EINVAL);

	return drm_gem_dmabuf_export(dev, &exp_info);
}

struct drm_gem_object *
panthor_gem_prime_import(struct drm_device *dev,
			 struct dma_buf *dma_buf)
{
	struct drm_gem_object *obj = dma_buf->priv;

	if (dma_buf->ops == &panthor_dma_buf_ops && obj->dev == dev) {
		/* Importing dmabuf exported from our own gem increases
		 * refcount on gem itself instead of f_count of dmabuf.
		 */
		drm_gem_object_get(obj);
		return obj;
	}

	return drm_gem_prime_import(dev, dma_buf);
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
	struct panthor_gem_object *obj;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	obj->base.base.funcs = &panthor_gem_funcs;
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
	bo->base.map_wc = should_map_wc(bo, exclusive_vm);

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

int
panthor_gem_sync(struct drm_gem_object *obj, u32 type,
		 u64 offset, u64 size)
{
	struct panthor_gem_object *bo = to_panthor_bo(obj);
	struct drm_gem_shmem_object *shmem = &bo->base;
	const struct drm_device *dev = shmem->base.dev;
	struct sg_table *sgt;
	struct scatterlist *sgl;
	unsigned int count;

	/* Make sure the range is in bounds. */
	if (offset + size < offset || offset + size > shmem->base.size)
		return -EINVAL;

	/* Disallow CPU-cache maintenance on imported buffers. */
	if (drm_gem_is_imported(&shmem->base))
		return -EINVAL;

	switch (type) {
	case DRM_PANTHOR_BO_SYNC_CPU_CACHE_FLUSH:
	case DRM_PANTHOR_BO_SYNC_CPU_CACHE_FLUSH_AND_INVALIDATE:
		break;

	default:
		return -EINVAL;
	}

	/* Don't bother if it's WC-mapped */
	if (shmem->map_wc)
		return 0;

	/* Nothing to do if the size is zero. */
	if (size == 0)
		return 0;

	sgt = drm_gem_shmem_get_pages_sgt(shmem);
	if (IS_ERR(sgt))
		return PTR_ERR(sgt);

	for_each_sgtable_dma_sg(sgt, sgl, count) {
		if (size == 0)
			break;

		dma_addr_t paddr = sg_dma_address(sgl);
		size_t len = sg_dma_len(sgl);

		if (len <= offset) {
			offset -= len;
			continue;
		}

		paddr += offset;
		len -= offset;
		len = min_t(size_t, len, size);
		size -= len;
		offset = 0;

		/* It's unclear whether dma_sync_xxx() is the right API to do CPU
		 * cache maintenance given an IOMMU can register their own
		 * implementation doing more than just CPU cache flushes/invalidation,
		 * and what we really care about here is CPU caches only, but that's
		 * the best we have that is both arch-agnostic and does at least the
		 * CPU cache maintenance on a <page,offset,size> tuple.
		 *
		 * Also, I wish we could do a single
		 *
		 *      dma_sync_single_for_device(BIDIR)
		 *
		 * and get a flush+invalidate, but that's not how it's implemented
		 * in practice (at least on arm64), so we have to make it
		 *
		 *      dma_sync_single_for_device(TO_DEVICE)
		 *      dma_sync_single_for_cpu(FROM_DEVICE)
		 *
		 * for the flush+invalidate case.
		 */
		dma_sync_single_for_device(dev->dev, paddr, len, DMA_TO_DEVICE);
		if (type == DRM_PANTHOR_BO_SYNC_CPU_CACHE_FLUSH_AND_INVALIDATE)
			dma_sync_single_for_cpu(dev->dev, paddr, len, DMA_FROM_DEVICE);
	}

	return 0;
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
