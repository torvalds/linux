/*
 * Copyright (C) ROCKCHIP, Inc.
 * Author:yzq<yzq@rock-chips.com>
 * 
 * based on exynos_drm_dmabuf.c
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drmP.h>
#include <drm/rockchip_drm.h>
#include "rockchip_drm_drv.h"
#include "rockchip_drm_gem.h"

#include <linux/dma-buf.h>

struct rockchip_drm_dmabuf_attachment {
	struct sg_table sgt;
	enum dma_data_direction dir;
	bool is_mapped;
};

static int rockchip_gem_attach_dma_buf(struct dma_buf *dmabuf,
					struct device *dev,
					struct dma_buf_attachment *attach)
{
	struct rockchip_drm_dmabuf_attachment *rockchip_attach;

	rockchip_attach = kzalloc(sizeof(*rockchip_attach), GFP_KERNEL);
	if (!rockchip_attach)
		return -ENOMEM;

	rockchip_attach->dir = DMA_NONE;
	attach->priv = rockchip_attach;

	return 0;
}

static void rockchip_gem_detach_dma_buf(struct dma_buf *dmabuf,
					struct dma_buf_attachment *attach)
{
	struct rockchip_drm_dmabuf_attachment *rockchip_attach = attach->priv;
	struct sg_table *sgt;

	if (!rockchip_attach)
		return;

	sgt = &rockchip_attach->sgt;

	if (rockchip_attach->dir != DMA_NONE)
		dma_unmap_sg(attach->dev, sgt->sgl, sgt->nents,
				rockchip_attach->dir);

	sg_free_table(sgt);
	kfree(rockchip_attach);
	attach->priv = NULL;
}

static struct sg_table *
		rockchip_gem_map_dma_buf(struct dma_buf_attachment *attach,
					enum dma_data_direction dir)
{
	struct rockchip_drm_dmabuf_attachment *rockchip_attach = attach->priv;
	struct rockchip_drm_gem_obj *gem_obj = attach->dmabuf->priv;
	struct drm_device *dev = gem_obj->base.dev;
	struct rockchip_drm_gem_buf *buf;
	struct scatterlist *rd, *wr;
	struct sg_table *sgt = NULL;
	unsigned int i;
	int nents, ret;

	DRM_DEBUG_PRIME("%s\n", __FILE__);

	/* just return current sgt if already requested. */
	if (rockchip_attach->dir == dir && rockchip_attach->is_mapped)
		return &rockchip_attach->sgt;

	buf = gem_obj->buffer;
	if (!buf) {
		DRM_ERROR("buffer is null.\n");
		return ERR_PTR(-ENOMEM);
	}

	sgt = &rockchip_attach->sgt;

	ret = sg_alloc_table(sgt, buf->sgt->orig_nents, GFP_KERNEL);
	if (ret) {
		DRM_ERROR("failed to alloc sgt.\n");
		return ERR_PTR(-ENOMEM);
	}

	mutex_lock(&dev->struct_mutex);

	rd = buf->sgt->sgl;
	wr = sgt->sgl;
	for (i = 0; i < sgt->orig_nents; ++i) {
		sg_set_page(wr, sg_page(rd), rd->length, rd->offset);
		rd = sg_next(rd);
		wr = sg_next(wr);
	}

	if (dir != DMA_NONE) {
		nents = dma_map_sg(attach->dev, sgt->sgl, sgt->orig_nents, dir);
		if (!nents) {
			DRM_ERROR("failed to map sgl with iommu.\n");
			sg_free_table(sgt);
			sgt = ERR_PTR(-EIO);
			goto err_unlock;
		}
	}

	rockchip_attach->is_mapped = true;
	rockchip_attach->dir = dir;
	attach->priv = rockchip_attach;

	DRM_DEBUG_PRIME("buffer size = 0x%lx\n", buf->size);

err_unlock:
	mutex_unlock(&dev->struct_mutex);
	return sgt;
}

static void rockchip_gem_unmap_dma_buf(struct dma_buf_attachment *attach,
						struct sg_table *sgt,
						enum dma_data_direction dir)
{
	/* Nothing to do. */
}

static void rockchip_dmabuf_release(struct dma_buf *dmabuf)
{
	struct rockchip_drm_gem_obj *rockchip_gem_obj = dmabuf->priv;

	DRM_DEBUG_PRIME("%s\n", __FILE__);

	/*
	 * rockchip_dmabuf_release() call means that file object's
	 * f_count is 0 and it calls drm_gem_object_handle_unreference()
	 * to drop the references that these values had been increased
	 * at drm_prime_handle_to_fd()
	 */
	if (rockchip_gem_obj->base.export_dma_buf == dmabuf) {
		rockchip_gem_obj->base.export_dma_buf = NULL;

		/*
		 * drop this gem object refcount to release allocated buffer
		 * and resources.
		 */
		drm_gem_object_unreference_unlocked(&rockchip_gem_obj->base);
	}
}

static void *rockchip_gem_dmabuf_kmap_atomic(struct dma_buf *dma_buf,
						unsigned long page_num)
{
	/* TODO */

	return NULL;
}

static void rockchip_gem_dmabuf_kunmap_atomic(struct dma_buf *dma_buf,
						unsigned long page_num,
						void *addr)
{
	/* TODO */
}

static void *rockchip_gem_dmabuf_kmap(struct dma_buf *dma_buf,
					unsigned long page_num)
{
	/* TODO */

	return NULL;
}

static void rockchip_gem_dmabuf_kunmap(struct dma_buf *dma_buf,
					unsigned long page_num, void *addr)
{
	/* TODO */
}

static int rockchip_gem_dmabuf_mmap(struct dma_buf *dma_buf,
	struct vm_area_struct *vma)
{
	return -ENOTTY;
}

static struct dma_buf_ops rockchip_dmabuf_ops = {
	.attach			= rockchip_gem_attach_dma_buf,
	.detach			= rockchip_gem_detach_dma_buf,
	.map_dma_buf		= rockchip_gem_map_dma_buf,
	.unmap_dma_buf		= rockchip_gem_unmap_dma_buf,
	.kmap			= rockchip_gem_dmabuf_kmap,
	.kmap_atomic		= rockchip_gem_dmabuf_kmap_atomic,
	.kunmap			= rockchip_gem_dmabuf_kunmap,
	.kunmap_atomic		= rockchip_gem_dmabuf_kunmap_atomic,
	.mmap			= rockchip_gem_dmabuf_mmap,
	.release		= rockchip_dmabuf_release,
};

struct dma_buf *rockchip_dmabuf_prime_export(struct drm_device *drm_dev,
				struct drm_gem_object *obj, int flags)
{
	struct rockchip_drm_gem_obj *rockchip_gem_obj = to_rockchip_gem_obj(obj);

	return dma_buf_export(rockchip_gem_obj, &rockchip_dmabuf_ops,
				rockchip_gem_obj->base.size, flags);
}

struct drm_gem_object *rockchip_dmabuf_prime_import(struct drm_device *drm_dev,
				struct dma_buf *dma_buf)
{
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct scatterlist *sgl;
	struct rockchip_drm_gem_obj *rockchip_gem_obj;
	struct rockchip_drm_gem_buf *buffer;
	int ret;

	DRM_DEBUG_PRIME("%s\n", __FILE__);

	/* is this one of own objects? */
	if (dma_buf->ops == &rockchip_dmabuf_ops) {
		struct drm_gem_object *obj;

		rockchip_gem_obj = dma_buf->priv;
		obj = &rockchip_gem_obj->base;

		/* is it from our device? */
		if (obj->dev == drm_dev) {
			/*
			 * Importing dmabuf exported from out own gem increases
			 * refcount on gem itself instead of f_count of dmabuf.
			 */
			drm_gem_object_reference(obj);
			return obj;
		}
	}

	attach = dma_buf_attach(dma_buf, drm_dev->dev);
	if (IS_ERR(attach))
		return ERR_PTR(-EINVAL);

	get_dma_buf(dma_buf);

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR_OR_NULL(sgt)) {
		ret = PTR_ERR(sgt);
		goto err_buf_detach;
	}

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer) {
		DRM_ERROR("failed to allocate rockchip_drm_gem_buf.\n");
		ret = -ENOMEM;
		goto err_unmap_attach;
	}

	rockchip_gem_obj = rockchip_drm_gem_init(drm_dev, dma_buf->size);
	if (!rockchip_gem_obj) {
		ret = -ENOMEM;
		goto err_free_buffer;
	}

	sgl = sgt->sgl;

	buffer->size = dma_buf->size;
	buffer->dma_addr = sg_dma_address(sgl);

	if (sgt->nents == 1) {
		/* always physically continuous memory if sgt->nents is 1. */
		rockchip_gem_obj->flags |= ROCKCHIP_BO_CONTIG;
	} else {
		/*
		 * this case could be CONTIG or NONCONTIG type but for now
		 * sets NONCONTIG.
		 * TODO. we have to find a way that exporter can notify
		 * the type of its own buffer to importer.
		 */
		rockchip_gem_obj->flags |= ROCKCHIP_BO_NONCONTIG;
	}

	rockchip_gem_obj->buffer = buffer;
	buffer->sgt = sgt;
	rockchip_gem_obj->base.import_attach = attach;

	DRM_DEBUG_PRIME("dma_addr = 0x%x, size = 0x%lx\n", buffer->dma_addr,
								buffer->size);

	return &rockchip_gem_obj->base;

err_free_buffer:
	kfree(buffer);
	buffer = NULL;
err_unmap_attach:
	dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
err_buf_detach:
	dma_buf_detach(dma_buf, attach);
	dma_buf_put(dma_buf);

	return ERR_PTR(ret);
}

