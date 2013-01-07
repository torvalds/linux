/*
 * drm gem CMA (contiguous memory allocator) helper functions
 *
 * Copyright (C) 2012 Sascha Hauer, Pengutronix
 *
 * Based on Samsung Exynos code
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/export.h>
#include <linux/dma-mapping.h>

#include <drm/drmP.h>
#include <drm/drm.h>
#include <drm/drm_gem_cma_helper.h>

static unsigned int get_gem_mmap_offset(struct drm_gem_object *obj)
{
	return (unsigned int)obj->map_list.hash.key << PAGE_SHIFT;
}

static void drm_gem_cma_buf_destroy(struct drm_device *drm,
		struct drm_gem_cma_object *cma_obj)
{
	dma_free_writecombine(drm->dev, cma_obj->base.size, cma_obj->vaddr,
			cma_obj->paddr);
}

/*
 * drm_gem_cma_create - allocate an object with the given size
 *
 * returns a struct drm_gem_cma_object* on success or ERR_PTR values
 * on failure.
 */
struct drm_gem_cma_object *drm_gem_cma_create(struct drm_device *drm,
		unsigned int size)
{
	struct drm_gem_cma_object *cma_obj;
	struct drm_gem_object *gem_obj;
	int ret;

	size = round_up(size, PAGE_SIZE);

	cma_obj = kzalloc(sizeof(*cma_obj), GFP_KERNEL);
	if (!cma_obj)
		return ERR_PTR(-ENOMEM);

	cma_obj->vaddr = dma_alloc_writecombine(drm->dev, size,
			&cma_obj->paddr, GFP_KERNEL | __GFP_NOWARN);
	if (!cma_obj->vaddr) {
		dev_err(drm->dev, "failed to allocate buffer with size %d\n", size);
		ret = -ENOMEM;
		goto err_dma_alloc;
	}

	gem_obj = &cma_obj->base;

	ret = drm_gem_object_init(drm, gem_obj, size);
	if (ret)
		goto err_obj_init;

	ret = drm_gem_create_mmap_offset(gem_obj);
	if (ret)
		goto err_create_mmap_offset;

	return cma_obj;

err_create_mmap_offset:
	drm_gem_object_release(gem_obj);

err_obj_init:
	drm_gem_cma_buf_destroy(drm, cma_obj);

err_dma_alloc:
	kfree(cma_obj);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(drm_gem_cma_create);

/*
 * drm_gem_cma_create_with_handle - allocate an object with the given
 * size and create a gem handle on it
 *
 * returns a struct drm_gem_cma_object* on success or ERR_PTR values
 * on failure.
 */
static struct drm_gem_cma_object *drm_gem_cma_create_with_handle(
		struct drm_file *file_priv,
		struct drm_device *drm, unsigned int size,
		unsigned int *handle)
{
	struct drm_gem_cma_object *cma_obj;
	struct drm_gem_object *gem_obj;
	int ret;

	cma_obj = drm_gem_cma_create(drm, size);
	if (IS_ERR(cma_obj))
		return cma_obj;

	gem_obj = &cma_obj->base;

	/*
	 * allocate a id of idr table where the obj is registered
	 * and handle has the id what user can see.
	 */
	ret = drm_gem_handle_create(file_priv, gem_obj, handle);
	if (ret)
		goto err_handle_create;

	/* drop reference from allocate - handle holds it now. */
	drm_gem_object_unreference_unlocked(gem_obj);

	return cma_obj;

err_handle_create:
	drm_gem_cma_free_object(gem_obj);

	return ERR_PTR(ret);
}

/*
 * drm_gem_cma_free_object - (struct drm_driver)->gem_free_object callback
 * function
 */
void drm_gem_cma_free_object(struct drm_gem_object *gem_obj)
{
	struct drm_gem_cma_object *cma_obj;

	if (gem_obj->map_list.map)
		drm_gem_free_mmap_offset(gem_obj);

	drm_gem_object_release(gem_obj);

	cma_obj = to_drm_gem_cma_obj(gem_obj);

	drm_gem_cma_buf_destroy(gem_obj->dev, cma_obj);

	kfree(cma_obj);
}
EXPORT_SYMBOL_GPL(drm_gem_cma_free_object);

/*
 * drm_gem_cma_dumb_create - (struct drm_driver)->dumb_create callback
 * function
 *
 * This aligns the pitch and size arguments to the minimum required. wrap
 * this into your own function if you need bigger alignment.
 */
int drm_gem_cma_dumb_create(struct drm_file *file_priv,
		struct drm_device *dev, struct drm_mode_create_dumb *args)
{
	struct drm_gem_cma_object *cma_obj;
	int min_pitch = DIV_ROUND_UP(args->width * args->bpp, 8);

	if (args->pitch < min_pitch)
		args->pitch = min_pitch;

	if (args->size < args->pitch * args->height)
		args->size = args->pitch * args->height;

	cma_obj = drm_gem_cma_create_with_handle(file_priv, dev,
			args->size, &args->handle);
	if (IS_ERR(cma_obj))
		return PTR_ERR(cma_obj);

	return 0;
}
EXPORT_SYMBOL_GPL(drm_gem_cma_dumb_create);

/*
 * drm_gem_cma_dumb_map_offset - (struct drm_driver)->dumb_map_offset callback
 * function
 */
int drm_gem_cma_dumb_map_offset(struct drm_file *file_priv,
		struct drm_device *drm, uint32_t handle, uint64_t *offset)
{
	struct drm_gem_object *gem_obj;

	mutex_lock(&drm->struct_mutex);

	gem_obj = drm_gem_object_lookup(drm, file_priv, handle);
	if (!gem_obj) {
		dev_err(drm->dev, "failed to lookup gem object\n");
		mutex_unlock(&drm->struct_mutex);
		return -EINVAL;
	}

	*offset = get_gem_mmap_offset(gem_obj);

	drm_gem_object_unreference(gem_obj);

	mutex_unlock(&drm->struct_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(drm_gem_cma_dumb_map_offset);

const struct vm_operations_struct drm_gem_cma_vm_ops = {
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};
EXPORT_SYMBOL_GPL(drm_gem_cma_vm_ops);

/*
 * drm_gem_cma_mmap - (struct file_operation)->mmap callback function
 */
int drm_gem_cma_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_gem_object *gem_obj;
	struct drm_gem_cma_object *cma_obj;
	int ret;

	ret = drm_gem_mmap(filp, vma);
	if (ret)
		return ret;

	gem_obj = vma->vm_private_data;
	cma_obj = to_drm_gem_cma_obj(gem_obj);

	ret = remap_pfn_range(vma, vma->vm_start, cma_obj->paddr >> PAGE_SHIFT,
			vma->vm_end - vma->vm_start, vma->vm_page_prot);
	if (ret)
		drm_gem_vm_close(vma);

	return ret;
}
EXPORT_SYMBOL_GPL(drm_gem_cma_mmap);

/*
 * drm_gem_cma_dumb_destroy - (struct drm_driver)->dumb_destroy callback function
 */
int drm_gem_cma_dumb_destroy(struct drm_file *file_priv,
		struct drm_device *drm, unsigned int handle)
{
	return drm_gem_handle_delete(file_priv, handle);
}
EXPORT_SYMBOL_GPL(drm_gem_cma_dumb_destroy);
