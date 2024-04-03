// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#include <linux/dma-buf.h>

#include <drm/drm_debugfs.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_prime.h>

#include "lsdc_drv.h"
#include "lsdc_gem.h"
#include "lsdc_ttm.h"

static int lsdc_gem_prime_pin(struct drm_gem_object *obj)
{
	struct lsdc_bo *lbo = gem_to_lsdc_bo(obj);
	int ret;

	ret = lsdc_bo_reserve(lbo);
	if (unlikely(ret))
		return ret;

	ret = lsdc_bo_pin(lbo, LSDC_GEM_DOMAIN_GTT, NULL);
	if (likely(ret == 0))
		lbo->sharing_count++;

	lsdc_bo_unreserve(lbo);

	return ret;
}

static void lsdc_gem_prime_unpin(struct drm_gem_object *obj)
{
	struct lsdc_bo *lbo = gem_to_lsdc_bo(obj);
	int ret;

	ret = lsdc_bo_reserve(lbo);
	if (unlikely(ret))
		return;

	lsdc_bo_unpin(lbo);
	if (lbo->sharing_count)
		lbo->sharing_count--;

	lsdc_bo_unreserve(lbo);
}

static struct sg_table *lsdc_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct ttm_buffer_object *tbo = to_ttm_bo(obj);
	struct ttm_tt *tt = tbo->ttm;

	if (!tt) {
		drm_err(obj->dev, "sharing a buffer without backing memory\n");
		return ERR_PTR(-ENOMEM);
	}

	return drm_prime_pages_to_sg(obj->dev, tt->pages, tt->num_pages);
}

static void lsdc_gem_object_free(struct drm_gem_object *obj)
{
	struct ttm_buffer_object *tbo = to_ttm_bo(obj);

	if (tbo)
		ttm_bo_put(tbo);
}

static int lsdc_gem_object_vmap(struct drm_gem_object *obj, struct iosys_map *map)
{
	struct ttm_buffer_object *tbo = to_ttm_bo(obj);
	struct lsdc_bo *lbo = to_lsdc_bo(tbo);
	int ret;

	if (lbo->vmap_count > 0) {
		++lbo->vmap_count;
		goto out;
	}

	ret = lsdc_bo_pin(lbo, 0, NULL);
	if (unlikely(ret)) {
		drm_err(obj->dev, "pin %p for vmap failed\n", lbo);
		return ret;
	}

	ret = ttm_bo_vmap(tbo, &lbo->map);
	if (ret) {
		drm_err(obj->dev, "ttm bo vmap failed\n");
		lsdc_bo_unpin(lbo);
		return ret;
	}

	lbo->vmap_count = 1;

out:
	*map = lbo->map;

	return 0;
}

static void lsdc_gem_object_vunmap(struct drm_gem_object *obj, struct iosys_map *map)
{
	struct ttm_buffer_object *tbo = to_ttm_bo(obj);
	struct lsdc_bo *lbo = to_lsdc_bo(tbo);

	if (unlikely(!lbo->vmap_count)) {
		drm_warn(obj->dev, "%p is not mapped\n", lbo);
		return;
	}

	--lbo->vmap_count;
	if (lbo->vmap_count == 0) {
		ttm_bo_vunmap(tbo, &lbo->map);

		lsdc_bo_unpin(lbo);
	}
}

static int lsdc_gem_object_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	struct ttm_buffer_object *tbo = to_ttm_bo(obj);
	int ret;

	ret = ttm_bo_mmap_obj(vma, tbo);
	if (unlikely(ret)) {
		drm_warn(obj->dev, "mmap %p failed\n", tbo);
		return ret;
	}

	drm_gem_object_put(obj);

	return 0;
}

static const struct drm_gem_object_funcs lsdc_gem_object_funcs = {
	.free = lsdc_gem_object_free,
	.export = drm_gem_prime_export,
	.pin = lsdc_gem_prime_pin,
	.unpin = lsdc_gem_prime_unpin,
	.get_sg_table = lsdc_gem_prime_get_sg_table,
	.vmap = lsdc_gem_object_vmap,
	.vunmap = lsdc_gem_object_vunmap,
	.mmap = lsdc_gem_object_mmap,
};

struct drm_gem_object *lsdc_gem_object_create(struct drm_device *ddev,
					      u32 domain,
					      size_t size,
					      bool kerenl,
					      struct sg_table *sg,
					      struct dma_resv *resv)
{
	struct lsdc_device *ldev = to_lsdc(ddev);
	struct drm_gem_object *gobj;
	struct lsdc_bo *lbo;
	int ret;

	lbo = lsdc_bo_create(ddev, domain, size, kerenl, sg, resv);
	if (IS_ERR(lbo)) {
		ret = PTR_ERR(lbo);
		return ERR_PTR(ret);
	}

	if (!sg) {
		/* VRAM is filled with random data */
		lsdc_bo_clear(lbo);
	}

	gobj = &lbo->tbo.base;
	gobj->funcs = &lsdc_gem_object_funcs;

	/* tracking the BOs we created */
	mutex_lock(&ldev->gem.mutex);
	list_add_tail(&lbo->list, &ldev->gem.objects);
	mutex_unlock(&ldev->gem.mutex);

	return gobj;
}

struct drm_gem_object *
lsdc_prime_import_sg_table(struct drm_device *ddev,
			   struct dma_buf_attachment *attach,
			   struct sg_table *sg)
{
	struct dma_resv *resv = attach->dmabuf->resv;
	u64 size = attach->dmabuf->size;
	struct drm_gem_object *gobj;
	struct lsdc_bo *lbo;

	dma_resv_lock(resv, NULL);
	gobj = lsdc_gem_object_create(ddev, LSDC_GEM_DOMAIN_GTT, size, false,
				      sg, resv);
	dma_resv_unlock(resv);

	if (IS_ERR(gobj)) {
		drm_err(ddev, "Failed to import sg table\n");
		return gobj;
	}

	lbo = gem_to_lsdc_bo(gobj);
	lbo->sharing_count = 1;

	return gobj;
}

int lsdc_dumb_create(struct drm_file *file, struct drm_device *ddev,
		     struct drm_mode_create_dumb *args)
{
	struct lsdc_device *ldev = to_lsdc(ddev);
	const struct lsdc_desc *descp = ldev->descp;
	u32 domain = LSDC_GEM_DOMAIN_VRAM;
	struct drm_gem_object *gobj;
	size_t size;
	u32 pitch;
	u32 handle;
	int ret;

	if (!args->width || !args->height)
		return -EINVAL;

	if (args->bpp != 32 && args->bpp != 16)
		return -EINVAL;

	pitch = args->width * args->bpp / 8;
	pitch = ALIGN(pitch, descp->pitch_align);
	size = pitch * args->height;
	size = ALIGN(size, PAGE_SIZE);

	/* Maximum single bo size allowed is the half vram size available */
	if (size > ldev->vram_size / 2) {
		drm_err(ddev, "Requesting(%zuMiB) failed\n", size >> 20);
		return -ENOMEM;
	}

	gobj = lsdc_gem_object_create(ddev, domain, size, false, NULL, NULL);
	if (IS_ERR(gobj)) {
		drm_err(ddev, "Failed to create gem object\n");
		return PTR_ERR(gobj);
	}

	ret = drm_gem_handle_create(file, gobj, &handle);

	/* drop reference from allocate, handle holds it now */
	drm_gem_object_put(gobj);
	if (ret)
		return ret;

	args->pitch = pitch;
	args->size = size;
	args->handle = handle;

	return 0;
}

int lsdc_dumb_map_offset(struct drm_file *filp, struct drm_device *ddev,
			 u32 handle, uint64_t *offset)
{
	struct drm_gem_object *gobj;

	gobj = drm_gem_object_lookup(filp, handle);
	if (!gobj)
		return -ENOENT;

	*offset = drm_vma_node_offset_addr(&gobj->vma_node);

	drm_gem_object_put(gobj);

	return 0;
}

void lsdc_gem_init(struct drm_device *ddev)
{
	struct lsdc_device *ldev = to_lsdc(ddev);

	mutex_init(&ldev->gem.mutex);
	INIT_LIST_HEAD(&ldev->gem.objects);
}

int lsdc_show_buffer_object(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_device *ddev = node->minor->dev;
	struct lsdc_device *ldev = to_lsdc(ddev);
	struct lsdc_bo *lbo;
	unsigned int i;

	mutex_lock(&ldev->gem.mutex);

	i = 0;

	list_for_each_entry(lbo, &ldev->gem.objects, list) {
		struct ttm_buffer_object *tbo = &lbo->tbo;
		struct ttm_resource *resource = tbo->resource;

		seq_printf(m, "bo[%04u][%p]: size: %8zuKiB %s offset: %8llx\n",
			   i, lbo, lsdc_bo_size(lbo) >> 10,
			   lsdc_mem_type_to_str(resource->mem_type),
			   lsdc_bo_gpu_offset(lbo));
		i++;
	}

	mutex_unlock(&ldev->gem.mutex);

	seq_printf(m, "Pinned BO size: VRAM: %zuKiB, GTT: %zu KiB\n",
		   ldev->vram_pinned_size >> 10, ldev->gtt_pinned_size >> 10);

	return 0;
}
