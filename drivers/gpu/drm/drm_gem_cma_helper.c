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
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>

#include <drm/drmP.h>
#include <drm/drm.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_vma_manager.h>

/*
 * __drm_gem_cma_create - Create a GEM CMA object without allocating memory
 * @drm: The drm device
 * @size: The GEM object size
 *
 * This function creates and initializes a GEM CMA object of the given size, but
 * doesn't allocate any memory to back the object.
 *
 * Return a struct drm_gem_cma_object* on success or ERR_PTR values on failure.
 */
static struct drm_gem_cma_object *
__drm_gem_cma_create(struct drm_device *drm, unsigned int size)
{
	struct drm_gem_cma_object *cma_obj;
	struct drm_gem_object *gem_obj;
	int ret;

	cma_obj = kzalloc(sizeof(*cma_obj), GFP_KERNEL);
	if (!cma_obj)
		return ERR_PTR(-ENOMEM);

	gem_obj = &cma_obj->base;

	ret = drm_gem_object_init(drm, gem_obj, size);
	if (ret)
		goto error;

	ret = drm_gem_create_mmap_offset(gem_obj);
	if (ret) {
		drm_gem_object_release(gem_obj);
		goto error;
	}

	return cma_obj;

error:
	kfree(cma_obj);
	return ERR_PTR(ret);
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
	int ret;

	size = round_up(size, PAGE_SIZE);

	cma_obj = __drm_gem_cma_create(drm, size);
	if (IS_ERR(cma_obj))
		return cma_obj;

	cma_obj->vaddr = dma_alloc_writecombine(drm->dev, size,
			&cma_obj->paddr, GFP_KERNEL | __GFP_NOWARN);
	if (!cma_obj->vaddr) {
		dev_err(drm->dev, "failed to allocate buffer with size %d\n",
			size);
		ret = -ENOMEM;
		goto error;
	}

	return cma_obj;

error:
	drm_gem_cma_free_object(&cma_obj->base);
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

	drm_gem_free_mmap_offset(gem_obj);

	cma_obj = to_drm_gem_cma_obj(gem_obj);

	if (cma_obj->vaddr) {
		dma_free_writecombine(gem_obj->dev->dev, cma_obj->base.size,
				      cma_obj->vaddr, cma_obj->paddr);
	} else if (gem_obj->import_attach) {
		drm_prime_gem_destroy(gem_obj, cma_obj->sgt);
	}

	drm_gem_object_release(gem_obj);

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
	return PTR_ERR_OR_ZERO(cma_obj);
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

	*offset = drm_vma_node_offset_addr(&gem_obj->vma_node);

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

static int drm_gem_cma_mmap_obj(struct drm_gem_cma_object *cma_obj,
				struct vm_area_struct *vma)
{
	int ret;

	/*
	 * Clear the VM_PFNMAP flag that was set by drm_gem_mmap(), and set the
	 * vm_pgoff (used as a fake buffer offset by DRM) to 0 as we want to map
	 * the whole buffer.
	 */
	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_pgoff = 0;

	ret = dma_mmap_writecombine(cma_obj->base.dev->dev, vma,
				    cma_obj->vaddr, cma_obj->paddr,
				    vma->vm_end - vma->vm_start);
	if (ret)
		drm_gem_vm_close(vma);

	return ret;
}

/*
 * drm_gem_cma_mmap - (struct file_operation)->mmap callback function
 */
int drm_gem_cma_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_gem_cma_object *cma_obj;
	struct drm_gem_object *gem_obj;
	int ret;

	ret = drm_gem_mmap(filp, vma);
	if (ret)
		return ret;

	gem_obj = vma->vm_private_data;
	cma_obj = to_drm_gem_cma_obj(gem_obj);

	return drm_gem_cma_mmap_obj(cma_obj, vma);
}
EXPORT_SYMBOL_GPL(drm_gem_cma_mmap);

#ifdef CONFIG_DEBUG_FS
void drm_gem_cma_describe(struct drm_gem_cma_object *cma_obj, struct seq_file *m)
{
	struct drm_gem_object *obj = &cma_obj->base;
	struct drm_device *dev = obj->dev;
	uint64_t off;

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

	off = drm_vma_node_start(&obj->vma_node);

	seq_printf(m, "%2d (%2d) %08llx %pad %p %d",
			obj->name, obj->refcount.refcount.counter,
			off, &cma_obj->paddr, cma_obj->vaddr, obj->size);

	seq_printf(m, "\n");
}
EXPORT_SYMBOL_GPL(drm_gem_cma_describe);
#endif

/* low-level interface prime helpers */
struct sg_table *drm_gem_cma_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct drm_gem_cma_object *cma_obj = to_drm_gem_cma_obj(obj);
	struct sg_table *sgt;
	int ret;

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return NULL;

	ret = dma_get_sgtable(obj->dev->dev, sgt, cma_obj->vaddr,
			      cma_obj->paddr, obj->size);
	if (ret < 0)
		goto out;

	return sgt;

out:
	kfree(sgt);
	return NULL;
}
EXPORT_SYMBOL_GPL(drm_gem_cma_prime_get_sg_table);

struct drm_gem_object *
drm_gem_cma_prime_import_sg_table(struct drm_device *dev, size_t size,
				  struct sg_table *sgt)
{
	struct drm_gem_cma_object *cma_obj;

	if (sgt->nents != 1)
		return ERR_PTR(-EINVAL);

	/* Create a CMA GEM buffer. */
	cma_obj = __drm_gem_cma_create(dev, size);
	if (IS_ERR(cma_obj))
		return ERR_PTR(PTR_ERR(cma_obj));

	cma_obj->paddr = sg_dma_address(sgt->sgl);
	cma_obj->sgt = sgt;

	DRM_DEBUG_PRIME("dma_addr = %pad, size = %zu\n", &cma_obj->paddr, size);

	return &cma_obj->base;
}
EXPORT_SYMBOL_GPL(drm_gem_cma_prime_import_sg_table);

int drm_gem_cma_prime_mmap(struct drm_gem_object *obj,
			   struct vm_area_struct *vma)
{
	struct drm_gem_cma_object *cma_obj;
	struct drm_device *dev = obj->dev;
	int ret;

	mutex_lock(&dev->struct_mutex);
	ret = drm_gem_mmap_obj(obj, obj->size, vma);
	mutex_unlock(&dev->struct_mutex);
	if (ret < 0)
		return ret;

	cma_obj = to_drm_gem_cma_obj(obj);
	return drm_gem_cma_mmap_obj(cma_obj, vma);
}
EXPORT_SYMBOL_GPL(drm_gem_cma_prime_mmap);

void *drm_gem_cma_prime_vmap(struct drm_gem_object *obj)
{
	struct drm_gem_cma_object *cma_obj = to_drm_gem_cma_obj(obj);

	return cma_obj->vaddr;
}
EXPORT_SYMBOL_GPL(drm_gem_cma_prime_vmap);

void drm_gem_cma_prime_vunmap(struct drm_gem_object *obj, void *vaddr)
{
	/* Nothing to do */
}
EXPORT_SYMBOL_GPL(drm_gem_cma_prime_vunmap);
