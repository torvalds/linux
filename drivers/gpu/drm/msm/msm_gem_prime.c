// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/dma-buf.h>

#include <drm/drm_drv.h>
#include <drm/drm_prime.h>

#include "msm_drv.h"
#include "msm_gem.h"

struct sg_table *msm_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	size_t npages = obj->size >> PAGE_SHIFT;

	if (msm_obj->flags & MSM_BO_NO_SHARE)
		return ERR_PTR(-EINVAL);

	if (WARN_ON(!msm_obj->pages))  /* should have already pinned! */
		return ERR_PTR(-ENOMEM);

	return drm_prime_pages_to_sg(obj->dev, msm_obj->pages, npages);
}

int msm_gem_prime_vmap(struct drm_gem_object *obj, struct iosys_map *map)
{
	void *vaddr;

	vaddr = msm_gem_get_vaddr_locked(obj);
	if (IS_ERR(vaddr))
		return PTR_ERR(vaddr);
	iosys_map_set_vaddr(map, vaddr);

	return 0;
}

void msm_gem_prime_vunmap(struct drm_gem_object *obj, struct iosys_map *map)
{
	msm_gem_put_vaddr_locked(obj);
}

static void msm_gem_dmabuf_release(struct dma_buf *dma_buf)
{
	struct drm_gem_object *obj = dma_buf->priv;

	msm_gem_vma_put(obj);
	drm_gem_dmabuf_release(dma_buf);
}

static const struct dma_buf_ops msm_gem_prime_dmabuf_ops =  {
	.attach = drm_gem_map_attach,
	.detach = drm_gem_map_detach,
	.map_dma_buf = drm_gem_map_dma_buf,
	.unmap_dma_buf = drm_gem_unmap_dma_buf,
	.release = msm_gem_dmabuf_release,
	.mmap = drm_gem_dmabuf_mmap,
	.vmap = drm_gem_dmabuf_vmap,
	.vunmap = drm_gem_dmabuf_vunmap,
};

struct drm_gem_object *msm_gem_prime_import(struct drm_device *dev,
					    struct dma_buf *buf)
{
	if (buf->ops == &msm_gem_prime_dmabuf_ops) {
		struct drm_gem_object *obj = buf->priv;
		if (obj->dev == dev) {
			/*
			 * Importing dmabuf exported from our own gem increases
			 * refcount on gem itself instead of f_count of dmabuf.
			 */
			drm_gem_object_get(obj);
			return obj;
		}
	}

	return drm_gem_prime_import(dev, buf);
}

struct drm_gem_object *msm_gem_prime_import_sg_table(struct drm_device *dev,
		struct dma_buf_attachment *attach, struct sg_table *sg)
{
	return msm_gem_import(dev, attach->dmabuf, sg);
}

struct dma_buf *msm_gem_prime_export(struct drm_gem_object *obj, int flags)
{
	if (to_msm_bo(obj)->flags & MSM_BO_NO_SHARE)
		return ERR_PTR(-EPERM);

	msm_gem_vma_get(obj);

	struct drm_device *dev = obj->dev;
	struct dma_buf_export_info exp_info = {
		.exp_name = KBUILD_MODNAME, /* white lie for debug */
		.owner = dev->driver->fops->owner,
		.ops = &msm_gem_prime_dmabuf_ops,
		.size = obj->size,
		.flags = flags,
		.priv = obj,
		.resv = obj->resv,
	};

	return drm_gem_dmabuf_export(dev, &exp_info);
}

int msm_gem_prime_pin(struct drm_gem_object *obj)
{
	struct page **pages;
	int ret = 0;

	if (drm_gem_is_imported(obj))
		return 0;

	if (to_msm_bo(obj)->flags & MSM_BO_NO_SHARE)
		return -EINVAL;

	pages = msm_gem_pin_pages_locked(obj);
	if (IS_ERR(pages))
		ret = PTR_ERR(pages);

	return ret;
}

void msm_gem_prime_unpin(struct drm_gem_object *obj)
{
	if (drm_gem_is_imported(obj))
		return;

	msm_gem_unpin_pages_locked(obj);
}
