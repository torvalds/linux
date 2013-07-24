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
	struct sg_table *sgt = NULL;
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

	sgt = kzalloc(sizeof(*cma_obj->sgt), GFP_KERNEL);
	if (sgt == NULL) {
		ret = -ENOMEM;
		goto error;
	}

	ret = dma_get_sgtable(drm->dev, sgt, cma_obj->vaddr,
			      cma_obj->paddr, size);
	if (ret < 0)
		goto error;

	cma_obj->sgt = sgt;

	return cma_obj;

error:
	kfree(sgt);
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
		if (cma_obj->sgt) {
			sg_free_table(cma_obj->sgt);
			kfree(cma_obj->sgt);
		}
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

	ret = remap_pfn_range(vma, vma->vm_start, cma_obj->paddr >> PAGE_SHIFT,
			vma->vm_end - vma->vm_start, vma->vm_page_prot);
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

/*
 * drm_gem_cma_dumb_destroy - (struct drm_driver)->dumb_destroy callback function
 */
int drm_gem_cma_dumb_destroy(struct drm_file *file_priv,
		struct drm_device *drm, unsigned int handle)
{
	return drm_gem_handle_delete(file_priv, handle);
}
EXPORT_SYMBOL_GPL(drm_gem_cma_dumb_destroy);

#ifdef CONFIG_DEBUG_FS
void drm_gem_cma_describe(struct drm_gem_cma_object *cma_obj, struct seq_file *m)
{
	struct drm_gem_object *obj = &cma_obj->base;
	struct drm_device *dev = obj->dev;
	uint64_t off;

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

	off = drm_vma_node_start(&obj->vma_node);

	seq_printf(m, "%2d (%2d) %08llx %08Zx %p %d",
			obj->name, obj->refcount.refcount.counter,
			off, cma_obj->paddr, cma_obj->vaddr, obj->size);

	seq_printf(m, "\n");
}
EXPORT_SYMBOL_GPL(drm_gem_cma_describe);
#endif

/* -----------------------------------------------------------------------------
 * DMA-BUF
 */

struct drm_gem_cma_dmabuf_attachment {
	struct sg_table sgt;
	enum dma_data_direction dir;
};

static int drm_gem_cma_dmabuf_attach(struct dma_buf *dmabuf, struct device *dev,
				     struct dma_buf_attachment *attach)
{
	struct drm_gem_cma_dmabuf_attachment *cma_attach;

	cma_attach = kzalloc(sizeof(*cma_attach), GFP_KERNEL);
	if (!cma_attach)
		return -ENOMEM;

	cma_attach->dir = DMA_NONE;
	attach->priv = cma_attach;

	return 0;
}

static void drm_gem_cma_dmabuf_detach(struct dma_buf *dmabuf,
				      struct dma_buf_attachment *attach)
{
	struct drm_gem_cma_dmabuf_attachment *cma_attach = attach->priv;
	struct sg_table *sgt;

	if (cma_attach == NULL)
		return;

	sgt = &cma_attach->sgt;

	if (cma_attach->dir != DMA_NONE)
		dma_unmap_sg(attach->dev, sgt->sgl, sgt->nents,
				cma_attach->dir);

	sg_free_table(sgt);
	kfree(cma_attach);
	attach->priv = NULL;
}

static struct sg_table *
drm_gem_cma_dmabuf_map(struct dma_buf_attachment *attach,
		       enum dma_data_direction dir)
{
	struct drm_gem_cma_dmabuf_attachment *cma_attach = attach->priv;
	struct drm_gem_cma_object *cma_obj = attach->dmabuf->priv;
	struct drm_device *drm = cma_obj->base.dev;
	struct scatterlist *rd, *wr;
	struct sg_table *sgt;
	unsigned int i;
	int nents, ret;

	DRM_DEBUG_PRIME("\n");

	if (WARN_ON(dir == DMA_NONE))
		return ERR_PTR(-EINVAL);

	/* Return the cached mapping when possible. */
	if (cma_attach->dir == dir)
		return &cma_attach->sgt;

	/* Two mappings with different directions for the same attachment are
	 * not allowed.
	 */
	if (WARN_ON(cma_attach->dir != DMA_NONE))
		return ERR_PTR(-EBUSY);

	sgt = &cma_attach->sgt;

	ret = sg_alloc_table(sgt, cma_obj->sgt->orig_nents, GFP_KERNEL);
	if (ret) {
		DRM_ERROR("failed to alloc sgt.\n");
		return ERR_PTR(-ENOMEM);
	}

	mutex_lock(&drm->struct_mutex);

	rd = cma_obj->sgt->sgl;
	wr = sgt->sgl;
	for (i = 0; i < sgt->orig_nents; ++i) {
		sg_set_page(wr, sg_page(rd), rd->length, rd->offset);
		rd = sg_next(rd);
		wr = sg_next(wr);
	}

	nents = dma_map_sg(attach->dev, sgt->sgl, sgt->orig_nents, dir);
	if (!nents) {
		DRM_ERROR("failed to map sgl with iommu.\n");
		sg_free_table(sgt);
		sgt = ERR_PTR(-EIO);
		goto done;
	}

	cma_attach->dir = dir;
	attach->priv = cma_attach;

	DRM_DEBUG_PRIME("buffer size = %zu\n", cma_obj->base.size);

done:
	mutex_unlock(&drm->struct_mutex);
	return sgt;
}

static void drm_gem_cma_dmabuf_unmap(struct dma_buf_attachment *attach,
				     struct sg_table *sgt,
				     enum dma_data_direction dir)
{
	/* Nothing to do. */
}

static void drm_gem_cma_dmabuf_release(struct dma_buf *dmabuf)
{
	struct drm_gem_cma_object *cma_obj = dmabuf->priv;

	DRM_DEBUG_PRIME("%s\n", __FILE__);

	/*
	 * drm_gem_cma_dmabuf_release() call means that file object's
	 * f_count is 0 and it calls drm_gem_object_handle_unreference()
	 * to drop the references that these values had been increased
	 * at drm_prime_handle_to_fd()
	 */
	if (cma_obj->base.export_dma_buf == dmabuf) {
		cma_obj->base.export_dma_buf = NULL;

		/*
		 * drop this gem object refcount to release allocated buffer
		 * and resources.
		 */
		drm_gem_object_unreference_unlocked(&cma_obj->base);
	}
}

static void *drm_gem_cma_dmabuf_kmap_atomic(struct dma_buf *dmabuf,
					    unsigned long page_num)
{
	/* TODO */

	return NULL;
}

static void drm_gem_cma_dmabuf_kunmap_atomic(struct dma_buf *dmabuf,
					     unsigned long page_num, void *addr)
{
	/* TODO */
}

static void *drm_gem_cma_dmabuf_kmap(struct dma_buf *dmabuf,
				     unsigned long page_num)
{
	/* TODO */

	return NULL;
}

static void drm_gem_cma_dmabuf_kunmap(struct dma_buf *dmabuf,
				      unsigned long page_num, void *addr)
{
	/* TODO */
}

static int drm_gem_cma_dmabuf_mmap(struct dma_buf *dmabuf,
				   struct vm_area_struct *vma)
{
	struct drm_gem_cma_object *cma_obj = dmabuf->priv;
	struct drm_gem_object *gem_obj = &cma_obj->base;
	int ret;

	ret = drm_gem_mmap_obj(gem_obj, gem_obj->size, vma);
	if (ret < 0)
		return ret;

	return drm_gem_cma_mmap_obj(cma_obj, vma);
}

static void *drm_gem_cma_dmabuf_vmap(struct dma_buf *dmabuf)
{
	struct drm_gem_cma_object *cma_obj = dmabuf->priv;

	return cma_obj->vaddr;
}

static struct dma_buf_ops drm_gem_cma_dmabuf_ops = {
	.attach			= drm_gem_cma_dmabuf_attach,
	.detach			= drm_gem_cma_dmabuf_detach,
	.map_dma_buf		= drm_gem_cma_dmabuf_map,
	.unmap_dma_buf		= drm_gem_cma_dmabuf_unmap,
	.kmap			= drm_gem_cma_dmabuf_kmap,
	.kmap_atomic		= drm_gem_cma_dmabuf_kmap_atomic,
	.kunmap			= drm_gem_cma_dmabuf_kunmap,
	.kunmap_atomic		= drm_gem_cma_dmabuf_kunmap_atomic,
	.mmap			= drm_gem_cma_dmabuf_mmap,
	.vmap			= drm_gem_cma_dmabuf_vmap,
	.release		= drm_gem_cma_dmabuf_release,
};

struct dma_buf *drm_gem_cma_dmabuf_export(struct drm_device *drm,
					  struct drm_gem_object *obj, int flags)
{
	struct drm_gem_cma_object *cma_obj = to_drm_gem_cma_obj(obj);

	return dma_buf_export(cma_obj, &drm_gem_cma_dmabuf_ops,
			      cma_obj->base.size, flags);
}
EXPORT_SYMBOL_GPL(drm_gem_cma_dmabuf_export);

struct drm_gem_object *drm_gem_cma_dmabuf_import(struct drm_device *drm,
						 struct dma_buf *dma_buf)
{
	struct drm_gem_cma_object *cma_obj;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	int ret;

	DRM_DEBUG_PRIME("%s\n", __FILE__);

	/* is this one of own objects? */
	if (dma_buf->ops == &drm_gem_cma_dmabuf_ops) {
		struct drm_gem_object *obj;

		cma_obj = dma_buf->priv;
		obj = &cma_obj->base;

		/* is it from our device? */
		if (obj->dev == drm) {
			/*
			 * Importing dmabuf exported from out own gem increases
			 * refcount on gem itself instead of f_count of dmabuf.
			 */
			drm_gem_object_reference(obj);
			dma_buf_put(dma_buf);
			return obj;
		}
	}

	/* Create a CMA GEM buffer. */
	cma_obj = __drm_gem_cma_create(drm, dma_buf->size);
	if (IS_ERR(cma_obj))
		return ERR_PTR(PTR_ERR(cma_obj));

	/* Attach to the buffer and map it. Make sure the mapping is contiguous
	 * on the device memory bus, as that's all we support.
	 */
	attach = dma_buf_attach(dma_buf, drm->dev);
	if (IS_ERR(attach)) {
		ret = -EINVAL;
		goto error_gem_free;
	}

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR_OR_NULL(sgt)) {
		ret = sgt ? PTR_ERR(sgt) : -ENOMEM;
		goto error_buf_detach;
	}

	if (sgt->nents != 1) {
		ret = -EINVAL;
		goto error_buf_unmap;
	}

	cma_obj->base.import_attach = attach;
	cma_obj->paddr = sg_dma_address(sgt->sgl);
	cma_obj->sgt = sgt;

	DRM_DEBUG_PRIME("dma_addr = 0x%x, size = %zu\n", cma_obj->paddr,
			dma_buf->size);

	return &cma_obj->base;

error_buf_unmap:
	dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
error_buf_detach:
	dma_buf_detach(dma_buf, attach);
error_gem_free:
	drm_gem_cma_free_object(&cma_obj->base);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(drm_gem_cma_dmabuf_import);
