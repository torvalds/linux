// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * udl_dmabuf.c
 *
 * Copyright (c) 2014 The Chromium OS Authors
 */

#include <linux/shmem_fs.h>
#include <linux/dma-buf.h>

#include <drm/drm_prime.h>

#include "udl_drv.h"

struct udl_drm_dmabuf_attachment {
	struct sg_table sgt;
	enum dma_data_direction dir;
	bool is_mapped;
};

static int udl_attach_dma_buf(struct dma_buf *dmabuf,
			      struct dma_buf_attachment *attach)
{
	struct udl_drm_dmabuf_attachment *udl_attach;

	DRM_DEBUG_PRIME("[DEV:%s] size:%zd\n", dev_name(attach->dev),
			attach->dmabuf->size);

	udl_attach = kzalloc(sizeof(*udl_attach), GFP_KERNEL);
	if (!udl_attach)
		return -ENOMEM;

	udl_attach->dir = DMA_NONE;
	attach->priv = udl_attach;

	return 0;
}

static void udl_detach_dma_buf(struct dma_buf *dmabuf,
			       struct dma_buf_attachment *attach)
{
	struct udl_drm_dmabuf_attachment *udl_attach = attach->priv;
	struct sg_table *sgt;

	if (!udl_attach)
		return;

	DRM_DEBUG_PRIME("[DEV:%s] size:%zd\n", dev_name(attach->dev),
			attach->dmabuf->size);

	sgt = &udl_attach->sgt;

	if (udl_attach->dir != DMA_NONE)
		dma_unmap_sg(attach->dev, sgt->sgl, sgt->nents,
				udl_attach->dir);

	sg_free_table(sgt);
	kfree(udl_attach);
	attach->priv = NULL;
}

static struct sg_table *udl_map_dma_buf(struct dma_buf_attachment *attach,
					enum dma_data_direction dir)
{
	struct udl_drm_dmabuf_attachment *udl_attach = attach->priv;
	struct udl_gem_object *obj = to_udl_bo(attach->dmabuf->priv);
	struct drm_device *dev = obj->base.dev;
	struct udl_device *udl = dev->dev_private;
	struct scatterlist *rd, *wr;
	struct sg_table *sgt = NULL;
	unsigned int i;
	int page_count;
	int nents, ret;

	DRM_DEBUG_PRIME("[DEV:%s] size:%zd dir=%d\n", dev_name(attach->dev),
			attach->dmabuf->size, dir);

	/* just return current sgt if already requested. */
	if (udl_attach->dir == dir && udl_attach->is_mapped)
		return &udl_attach->sgt;

	if (!obj->pages) {
		ret = udl_gem_get_pages(obj);
		if (ret) {
			DRM_ERROR("failed to map pages.\n");
			return ERR_PTR(ret);
		}
	}

	page_count = obj->base.size / PAGE_SIZE;
	obj->sg = drm_prime_pages_to_sg(obj->pages, page_count);
	if (IS_ERR(obj->sg)) {
		DRM_ERROR("failed to allocate sgt.\n");
		return ERR_CAST(obj->sg);
	}

	sgt = &udl_attach->sgt;

	ret = sg_alloc_table(sgt, obj->sg->orig_nents, GFP_KERNEL);
	if (ret) {
		DRM_ERROR("failed to alloc sgt.\n");
		return ERR_PTR(-ENOMEM);
	}

	mutex_lock(&udl->gem_lock);

	rd = obj->sg->sgl;
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

	udl_attach->is_mapped = true;
	udl_attach->dir = dir;
	attach->priv = udl_attach;

err_unlock:
	mutex_unlock(&udl->gem_lock);
	return sgt;
}

static void udl_unmap_dma_buf(struct dma_buf_attachment *attach,
			      struct sg_table *sgt,
			      enum dma_data_direction dir)
{
	/* Nothing to do. */
	DRM_DEBUG_PRIME("[DEV:%s] size:%zd dir:%d\n", dev_name(attach->dev),
			attach->dmabuf->size, dir);
}

static void *udl_dmabuf_kmap(struct dma_buf *dma_buf, unsigned long page_num)
{
	/* TODO */

	return NULL;
}

static void udl_dmabuf_kunmap(struct dma_buf *dma_buf,
			      unsigned long page_num, void *addr)
{
	/* TODO */
}

static int udl_dmabuf_mmap(struct dma_buf *dma_buf,
			   struct vm_area_struct *vma)
{
	/* TODO */

	return -EINVAL;
}

static const struct dma_buf_ops udl_dmabuf_ops = {
	.attach			= udl_attach_dma_buf,
	.detach			= udl_detach_dma_buf,
	.map_dma_buf		= udl_map_dma_buf,
	.unmap_dma_buf		= udl_unmap_dma_buf,
	.map			= udl_dmabuf_kmap,
	.unmap			= udl_dmabuf_kunmap,
	.mmap			= udl_dmabuf_mmap,
	.release		= drm_gem_dmabuf_release,
};

struct dma_buf *udl_gem_prime_export(struct drm_gem_object *obj, int flags)
{
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	exp_info.ops = &udl_dmabuf_ops;
	exp_info.size = obj->size;
	exp_info.flags = flags;
	exp_info.priv = obj;

	return drm_gem_dmabuf_export(obj->dev, &exp_info);
}

static int udl_prime_create(struct drm_device *dev,
			    size_t size,
			    struct sg_table *sg,
			    struct udl_gem_object **obj_p)
{
	struct udl_gem_object *obj;
	int npages;

	npages = size / PAGE_SIZE;

	*obj_p = NULL;
	obj = udl_gem_alloc_object(dev, npages * PAGE_SIZE);
	if (!obj)
		return -ENOMEM;

	obj->sg = sg;
	obj->pages = kvmalloc_array(npages, sizeof(struct page *), GFP_KERNEL);
	if (obj->pages == NULL) {
		DRM_ERROR("obj pages is NULL %d\n", npages);
		return -ENOMEM;
	}

	drm_prime_sg_to_page_addr_arrays(sg, obj->pages, NULL, npages);

	*obj_p = obj;
	return 0;
}

struct drm_gem_object *udl_gem_prime_import(struct drm_device *dev,
				struct dma_buf *dma_buf)
{
	struct dma_buf_attachment *attach;
	struct sg_table *sg;
	struct udl_gem_object *uobj;
	int ret;

	/* need to attach */
	get_device(dev->dev);
	attach = dma_buf_attach(dma_buf, dev->dev);
	if (IS_ERR(attach)) {
		put_device(dev->dev);
		return ERR_CAST(attach);
	}

	get_dma_buf(dma_buf);

	sg = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sg)) {
		ret = PTR_ERR(sg);
		goto fail_detach;
	}

	ret = udl_prime_create(dev, dma_buf->size, sg, &uobj);
	if (ret)
		goto fail_unmap;

	uobj->base.import_attach = attach;
	uobj->flags = UDL_BO_WC;

	return &uobj->base;

fail_unmap:
	dma_buf_unmap_attachment(attach, sg, DMA_BIDIRECTIONAL);
fail_detach:
	dma_buf_detach(dma_buf, attach);
	dma_buf_put(dma_buf);
	put_device(dev->dev);
	return ERR_PTR(ret);
}
