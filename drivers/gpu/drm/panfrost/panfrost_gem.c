// SPDX-License-Identifier: GPL-2.0
/* Copyright 2019 Linaro, Ltd, Rob Herring <robh@kernel.org> */

#include <linux/cleanup.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>

#include <drm/panfrost_drm.h>
#include "panfrost_device.h"
#include "panfrost_gem.h"
#include "panfrost_mmu.h"

#ifdef CONFIG_DEBUG_FS
static void panfrost_gem_debugfs_bo_add(struct panfrost_device *pfdev,
					struct panfrost_gem_object *bo)
{
	bo->debugfs.creator.tgid = current->group_leader->pid;
	get_task_comm(bo->debugfs.creator.process_name, current->group_leader);

	mutex_lock(&pfdev->debugfs.gems_lock);
	list_add_tail(&bo->debugfs.node, &pfdev->debugfs.gems_list);
	mutex_unlock(&pfdev->debugfs.gems_lock);
}

static void panfrost_gem_debugfs_bo_rm(struct panfrost_gem_object *bo)
{
	struct panfrost_device *pfdev = bo->base.base.dev->dev_private;

	if (list_empty(&bo->debugfs.node))
		return;

	mutex_lock(&pfdev->debugfs.gems_lock);
	list_del_init(&bo->debugfs.node);
	mutex_unlock(&pfdev->debugfs.gems_lock);
}
#else
static void panfrost_gem_debugfs_bo_add(struct panfrost_device *pfdev,
					struct panfrost_gem_object *bo)
{}
static void panfrost_gem_debugfs_bo_rm(struct panfrost_gem_object *bo) {}
#endif

/* Called DRM core on the last userspace/kernel unreference of the
 * BO.
 */
static void panfrost_gem_free_object(struct drm_gem_object *obj)
{
	struct panfrost_gem_object *bo = to_panfrost_bo(obj);
	struct panfrost_device *pfdev = obj->dev->dev_private;

	/*
	 * Make sure the BO is no longer inserted in the shrinker list before
	 * taking care of the destruction itself. If we don't do that we have a
	 * race condition between this function and what's done in
	 * panfrost_gem_shrinker_scan().
	 */
	mutex_lock(&pfdev->shrinker_lock);
	list_del_init(&bo->base.madv_list);
	mutex_unlock(&pfdev->shrinker_lock);

	/*
	 * If we still have mappings attached to the BO, there's a problem in
	 * our refcounting.
	 */
	WARN_ON_ONCE(!list_empty(&bo->mappings.list));

	kfree_const(bo->label.str);
	panfrost_gem_debugfs_bo_rm(bo);
	mutex_destroy(&bo->label.lock);

	if (bo->sgts) {
		int i;
		int n_sgt = bo->base.base.size / SZ_2M;

		for (i = 0; i < n_sgt; i++) {
			if (bo->sgts[i].sgl) {
				dma_unmap_sgtable(pfdev->dev, &bo->sgts[i],
						  DMA_BIDIRECTIONAL, 0);
				sg_free_table(&bo->sgts[i]);
			}
		}
		kvfree(bo->sgts);
	}

	drm_gem_shmem_free(&bo->base);
}

struct panfrost_gem_mapping *
panfrost_gem_mapping_get(struct panfrost_gem_object *bo,
			 struct panfrost_file_priv *priv)
{
	struct panfrost_gem_mapping *iter, *mapping = NULL;

	mutex_lock(&bo->mappings.lock);
	list_for_each_entry(iter, &bo->mappings.list, node) {
		if (iter->mmu == priv->mmu) {
			kref_get(&iter->refcount);
			mapping = iter;
			break;
		}
	}
	mutex_unlock(&bo->mappings.lock);

	return mapping;
}

static void
panfrost_gem_teardown_mapping(struct panfrost_gem_mapping *mapping)
{
	if (mapping->active)
		panfrost_mmu_unmap(mapping);

	spin_lock(&mapping->mmu->mm_lock);
	if (drm_mm_node_allocated(&mapping->mmnode))
		drm_mm_remove_node(&mapping->mmnode);
	spin_unlock(&mapping->mmu->mm_lock);
}

static void panfrost_gem_mapping_release(struct kref *kref)
{
	struct panfrost_gem_mapping *mapping;

	mapping = container_of(kref, struct panfrost_gem_mapping, refcount);

	panfrost_gem_teardown_mapping(mapping);
	drm_gem_object_put(&mapping->obj->base.base);
	panfrost_mmu_ctx_put(mapping->mmu);
	kfree(mapping);
}

void panfrost_gem_mapping_put(struct panfrost_gem_mapping *mapping)
{
	if (!mapping)
		return;

	kref_put(&mapping->refcount, panfrost_gem_mapping_release);
}

void panfrost_gem_teardown_mappings_locked(struct panfrost_gem_object *bo)
{
	struct panfrost_gem_mapping *mapping;

	list_for_each_entry(mapping, &bo->mappings.list, node)
		panfrost_gem_teardown_mapping(mapping);
}

int panfrost_gem_open(struct drm_gem_object *obj, struct drm_file *file_priv)
{
	int ret;
	size_t size = obj->size;
	u64 align;
	struct panfrost_gem_object *bo = to_panfrost_bo(obj);
	unsigned long color = bo->noexec ? PANFROST_BO_NOEXEC : 0;
	struct panfrost_file_priv *priv = file_priv->driver_priv;
	struct panfrost_gem_mapping *mapping;

	mapping = kzalloc(sizeof(*mapping), GFP_KERNEL);
	if (!mapping)
		return -ENOMEM;

	INIT_LIST_HEAD(&mapping->node);
	kref_init(&mapping->refcount);
	drm_gem_object_get(obj);
	mapping->obj = bo;

	/*
	 * Executable buffers cannot cross a 16MB boundary as the program
	 * counter is 24-bits. We assume executable buffers will be less than
	 * 16MB and aligning executable buffers to their size will avoid
	 * crossing a 16MB boundary.
	 */
	if (!bo->noexec)
		align = size >> PAGE_SHIFT;
	else
		align = size >= SZ_2M ? SZ_2M >> PAGE_SHIFT : 0;

	mapping->mmu = panfrost_mmu_ctx_get(priv->mmu);
	spin_lock(&mapping->mmu->mm_lock);
	ret = drm_mm_insert_node_generic(&mapping->mmu->mm, &mapping->mmnode,
					 size >> PAGE_SHIFT, align, color, 0);
	spin_unlock(&mapping->mmu->mm_lock);
	if (ret)
		goto err;

	if (!bo->is_heap) {
		ret = panfrost_mmu_map(mapping);
		if (ret)
			goto err;
	}

	mutex_lock(&bo->mappings.lock);
	WARN_ON(bo->base.madv != PANFROST_MADV_WILLNEED);
	list_add_tail(&mapping->node, &bo->mappings.list);
	mutex_unlock(&bo->mappings.lock);

err:
	if (ret)
		panfrost_gem_mapping_put(mapping);
	return ret;
}

void panfrost_gem_close(struct drm_gem_object *obj, struct drm_file *file_priv)
{
	struct panfrost_file_priv *priv = file_priv->driver_priv;
	struct panfrost_gem_object *bo = to_panfrost_bo(obj);
	struct panfrost_gem_mapping *mapping = NULL, *iter;

	mutex_lock(&bo->mappings.lock);
	list_for_each_entry(iter, &bo->mappings.list, node) {
		if (iter->mmu == priv->mmu) {
			mapping = iter;
			list_del(&iter->node);
			break;
		}
	}
	mutex_unlock(&bo->mappings.lock);

	panfrost_gem_mapping_put(mapping);
}

static int panfrost_gem_pin(struct drm_gem_object *obj)
{
	struct panfrost_gem_object *bo = to_panfrost_bo(obj);

	if (bo->is_heap)
		return -EINVAL;

	return drm_gem_shmem_pin_locked(&bo->base);
}

static enum drm_gem_object_status panfrost_gem_status(struct drm_gem_object *obj)
{
	struct panfrost_gem_object *bo = to_panfrost_bo(obj);
	enum drm_gem_object_status res = 0;

	if (drm_gem_is_imported(&bo->base.base) || bo->base.pages)
		res |= DRM_GEM_OBJECT_RESIDENT;

	if (bo->base.madv == PANFROST_MADV_DONTNEED)
		res |= DRM_GEM_OBJECT_PURGEABLE;

	return res;
}

static size_t panfrost_gem_rss(struct drm_gem_object *obj)
{
	struct panfrost_gem_object *bo = to_panfrost_bo(obj);

	if (bo->is_heap) {
		return bo->heap_rss_size;
	} else if (bo->base.pages) {
		WARN_ON(bo->heap_rss_size);
		return bo->base.base.size;
	}

	return 0;
}

static const struct drm_gem_object_funcs panfrost_gem_funcs = {
	.free = panfrost_gem_free_object,
	.open = panfrost_gem_open,
	.close = panfrost_gem_close,
	.print_info = drm_gem_shmem_object_print_info,
	.pin = panfrost_gem_pin,
	.unpin = drm_gem_shmem_object_unpin,
	.get_sg_table = drm_gem_shmem_object_get_sg_table,
	.vmap = drm_gem_shmem_object_vmap,
	.vunmap = drm_gem_shmem_object_vunmap,
	.mmap = drm_gem_shmem_object_mmap,
	.status = panfrost_gem_status,
	.rss = panfrost_gem_rss,
	.vm_ops = &drm_gem_shmem_vm_ops,
};

/**
 * panfrost_gem_create_object - Implementation of driver->gem_create_object.
 * @dev: DRM device
 * @size: Size in bytes of the memory the object will reference
 *
 * This lets the GEM helpers allocate object structs for us, and keep
 * our BO stats correct.
 */
struct drm_gem_object *panfrost_gem_create_object(struct drm_device *dev, size_t size)
{
	struct panfrost_device *pfdev = dev->dev_private;
	struct panfrost_gem_object *obj;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&obj->mappings.list);
	mutex_init(&obj->mappings.lock);
	obj->base.base.funcs = &panfrost_gem_funcs;
	obj->base.map_wc = !pfdev->coherent;
	mutex_init(&obj->label.lock);

	panfrost_gem_debugfs_bo_add(pfdev, obj);

	return &obj->base.base;
}

struct panfrost_gem_object *
panfrost_gem_create(struct drm_device *dev, size_t size, u32 flags)
{
	struct drm_gem_shmem_object *shmem;
	struct panfrost_gem_object *bo;

	/* Round up heap allocations to 2MB to keep fault handling simple */
	if (flags & PANFROST_BO_HEAP)
		size = roundup(size, SZ_2M);

	shmem = drm_gem_shmem_create(dev, size);
	if (IS_ERR(shmem))
		return ERR_CAST(shmem);

	bo = to_panfrost_bo(&shmem->base);
	bo->noexec = !!(flags & PANFROST_BO_NOEXEC);
	bo->is_heap = !!(flags & PANFROST_BO_HEAP);

	return bo;
}

struct drm_gem_object *
panfrost_gem_prime_import_sg_table(struct drm_device *dev,
				   struct dma_buf_attachment *attach,
				   struct sg_table *sgt)
{
	struct drm_gem_object *obj;
	struct panfrost_gem_object *bo;

	obj = drm_gem_shmem_prime_import_sg_table(dev, attach, sgt);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	bo = to_panfrost_bo(obj);
	bo->noexec = true;

	/*
	 * We assign this generic label because this function cannot
	 * be reached through any of the Panfrost UM driver-specific
	 * code paths, unless one is given by explicitly calling the
	 * SET_LABEL_BO ioctl. It is therefore preferable to have a
	 * blanket BO tag that tells us the object was imported from
	 * another driver than nothing at all.
	 */
	panfrost_gem_internal_set_label(obj, "GEM PRIME buffer");

	return obj;
}

void
panfrost_gem_set_label(struct drm_gem_object *obj, const char *label)
{
	struct panfrost_gem_object *bo = to_panfrost_bo(obj);
	const char *old_label;

	scoped_guard(mutex, &bo->label.lock) {
		old_label = bo->label.str;
		bo->label.str = label;
	}

	kfree_const(old_label);
}

void
panfrost_gem_internal_set_label(struct drm_gem_object *obj, const char *label)
{
	struct panfrost_gem_object *bo = to_panfrost_bo(obj);
	const char *str;

	/* We should never attempt labelling a UM-exposed GEM object */
	if (drm_WARN_ON(bo->base.base.dev, bo->base.base.handle_count > 0))
		return;

	if (!label)
		return;

	str = kstrdup_const(label, GFP_KERNEL);
	if (!str) {
		/* Failing to allocate memory for a label isn't a fatal condition */
		drm_warn(bo->base.base.dev, "Not enough memory to allocate BO label");
		return;
	}

	panfrost_gem_set_label(obj, str);
}

#ifdef CONFIG_DEBUG_FS
struct gem_size_totals {
	size_t size;
	size_t resident;
	size_t reclaimable;
};

struct flag_def {
	u32 flag;
	const char *name;
};

static void panfrost_gem_debugfs_print_flag_names(struct seq_file *m)
{
	int len;
	int i;

	static const struct flag_def gem_state_flags_names[] = {
		{PANFROST_DEBUGFS_GEM_STATE_FLAG_IMPORTED, "imported"},
		{PANFROST_DEBUGFS_GEM_STATE_FLAG_EXPORTED, "exported"},
		{PANFROST_DEBUGFS_GEM_STATE_FLAG_PURGED, "purged"},
		{PANFROST_DEBUGFS_GEM_STATE_FLAG_PURGEABLE, "purgeable"},
	};

	seq_puts(m, "GEM state flags: ");
	for (i = 0, len = ARRAY_SIZE(gem_state_flags_names); i < len; i++) {
		seq_printf(m, "%s (0x%x)%s", gem_state_flags_names[i].name,
			   gem_state_flags_names[i].flag, (i < len - 1) ? ", " : "\n\n");
	}
}

static void panfrost_gem_debugfs_bo_print(struct panfrost_gem_object *bo,
					  struct seq_file *m,
					  struct gem_size_totals *totals)
{
	unsigned int refcount = kref_read(&bo->base.base.refcount);
	char creator_info[32] = {};
	size_t resident_size;
	u32 gem_state_flags = 0;

	/* Skip BOs being destroyed. */
	if (!refcount)
		return;

	resident_size = panfrost_gem_rss(&bo->base.base);

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
		gem_state_flags |= PANFROST_DEBUGFS_GEM_STATE_FLAG_IMPORTED;
	if (bo->base.base.dma_buf)
		gem_state_flags |= PANFROST_DEBUGFS_GEM_STATE_FLAG_EXPORTED;

	if (bo->base.madv < 0)
		gem_state_flags |= PANFROST_DEBUGFS_GEM_STATE_FLAG_PURGED;
	else if (bo->base.madv > 0)
		gem_state_flags |= PANFROST_DEBUGFS_GEM_STATE_FLAG_PURGEABLE;

	seq_printf(m, "0x%-10x", gem_state_flags);

	scoped_guard(mutex, &bo->label.lock) {
		seq_printf(m, "%s\n", bo->label.str ? : "");
	}

	totals->size += bo->base.base.size;
	totals->resident += resident_size;
	if (bo->base.madv > 0)
		totals->reclaimable += resident_size;
}

void panfrost_gem_debugfs_print_bos(struct panfrost_device *pfdev,
				    struct seq_file *m)
{
	struct gem_size_totals totals = {0};
	struct panfrost_gem_object *bo;

	panfrost_gem_debugfs_print_flag_names(m);

	seq_puts(m, "created-by                      global-name     refcount        size            resident-size   file-offset       state       label\n");
	seq_puts(m, "-----------------------------------------------------------------------------------------------------------------------------------\n");

	scoped_guard(mutex, &pfdev->debugfs.gems_lock) {
		list_for_each_entry(bo, &pfdev->debugfs.gems_list, debugfs.node) {
			panfrost_gem_debugfs_bo_print(bo, m, &totals);
		}
	}

	seq_puts(m, "===================================================================================================================================\n");
	seq_printf(m, "Total size: %zd, Total resident: %zd, Total reclaimable: %zd\n",
		   totals.size, totals.resident, totals.reclaimable);
}
#endif
