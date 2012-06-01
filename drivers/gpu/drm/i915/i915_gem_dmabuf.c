/*
 * Copyright 2012 Red Hat Inc
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Dave Airlie <airlied@redhat.com>
 */
#include "drmP.h"
#include "i915_drv.h"
#include <linux/dma-buf.h>

static struct sg_table *i915_gem_map_dma_buf(struct dma_buf_attachment *attachment,
					     enum dma_data_direction dir)
{
	struct drm_i915_gem_object *obj = attachment->dmabuf->priv;
	struct sg_table *st;
	struct scatterlist *src, *dst;
	int ret, i;

	ret = i915_mutex_lock_interruptible(obj->base.dev);
	if (ret)
		return ERR_PTR(ret);

	ret = i915_gem_object_get_pages(obj);
	if (ret) {
		st = ERR_PTR(ret);
		goto out;
	}

	/* Copy sg so that we make an independent mapping */
	st = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (st == NULL) {
		st = ERR_PTR(-ENOMEM);
		goto out;
	}

	ret = sg_alloc_table(st, obj->pages->nents, GFP_KERNEL);
	if (ret) {
		kfree(st);
		st = ERR_PTR(ret);
		goto out;
	}

	src = obj->pages->sgl;
	dst = st->sgl;
	for (i = 0; i < obj->pages->nents; i++) {
		sg_set_page(dst, sg_page(src), PAGE_SIZE, 0);
		dst = sg_next(dst);
		src = sg_next(src);
	}

	if (!dma_map_sg(attachment->dev, st->sgl, st->nents, dir)) {
		sg_free_table(st);
		kfree(st);
		st = ERR_PTR(-ENOMEM);
		goto out;
	}

	i915_gem_object_pin_pages(obj);

out:
	mutex_unlock(&obj->base.dev->struct_mutex);
	return st;
}

static void i915_gem_unmap_dma_buf(struct dma_buf_attachment *attachment,
			    struct sg_table *sg, enum dma_data_direction dir)
{
	dma_unmap_sg(attachment->dev, sg->sgl, sg->nents, dir);
	sg_free_table(sg);
	kfree(sg);
}

static void i915_gem_dmabuf_release(struct dma_buf *dma_buf)
{
	struct drm_i915_gem_object *obj = dma_buf->priv;

	if (obj->base.export_dma_buf == dma_buf) {
		/* drop the reference on the export fd holds */
		obj->base.export_dma_buf = NULL;
		drm_gem_object_unreference_unlocked(&obj->base);
	}
}

static void *i915_gem_dmabuf_vmap(struct dma_buf *dma_buf)
{
	struct drm_i915_gem_object *obj = dma_buf->priv;
	struct drm_device *dev = obj->base.dev;
	struct scatterlist *sg;
	struct page **pages;
	int ret, i;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ERR_PTR(ret);

	if (obj->dma_buf_vmapping) {
		obj->vmapping_count++;
		goto out_unlock;
	}

	ret = i915_gem_object_get_pages(obj);
	if (ret)
		goto error;

	ret = -ENOMEM;

	pages = drm_malloc_ab(obj->pages->nents, sizeof(struct page *));
	if (pages == NULL)
		goto error;

	for_each_sg(obj->pages->sgl, sg, obj->pages->nents, i)
		pages[i] = sg_page(sg);

	obj->dma_buf_vmapping = vmap(pages, obj->pages->nents, 0, PAGE_KERNEL);
	drm_free_large(pages);

	if (!obj->dma_buf_vmapping)
		goto error;

	obj->vmapping_count = 1;
	i915_gem_object_pin_pages(obj);
out_unlock:
	mutex_unlock(&dev->struct_mutex);
	return obj->dma_buf_vmapping;

error:
	mutex_unlock(&dev->struct_mutex);
	return ERR_PTR(ret);
}

static void i915_gem_dmabuf_vunmap(struct dma_buf *dma_buf, void *vaddr)
{
	struct drm_i915_gem_object *obj = dma_buf->priv;
	struct drm_device *dev = obj->base.dev;
	int ret;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return;

	if (--obj->vmapping_count == 0) {
		vunmap(obj->dma_buf_vmapping);
		obj->dma_buf_vmapping = NULL;

		i915_gem_object_unpin_pages(obj);
	}
	mutex_unlock(&dev->struct_mutex);
}

static void *i915_gem_dmabuf_kmap_atomic(struct dma_buf *dma_buf, unsigned long page_num)
{
	return NULL;
}

static void i915_gem_dmabuf_kunmap_atomic(struct dma_buf *dma_buf, unsigned long page_num, void *addr)
{

}
static void *i915_gem_dmabuf_kmap(struct dma_buf *dma_buf, unsigned long page_num)
{
	return NULL;
}

static void i915_gem_dmabuf_kunmap(struct dma_buf *dma_buf, unsigned long page_num, void *addr)
{

}

static int i915_gem_dmabuf_mmap(struct dma_buf *dma_buf, struct vm_area_struct *vma)
{
	return -EINVAL;
}

static int i915_gem_begin_cpu_access(struct dma_buf *dma_buf, size_t start, size_t length, enum dma_data_direction direction)
{
	struct drm_i915_gem_object *obj = dma_buf->priv;
	struct drm_device *dev = obj->base.dev;
	int ret;
	bool write = (direction == DMA_BIDIRECTIONAL || direction == DMA_TO_DEVICE);

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	ret = i915_gem_object_set_to_cpu_domain(obj, write);
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

static const struct dma_buf_ops i915_dmabuf_ops =  {
	.map_dma_buf = i915_gem_map_dma_buf,
	.unmap_dma_buf = i915_gem_unmap_dma_buf,
	.release = i915_gem_dmabuf_release,
	.kmap = i915_gem_dmabuf_kmap,
	.kmap_atomic = i915_gem_dmabuf_kmap_atomic,
	.kunmap = i915_gem_dmabuf_kunmap,
	.kunmap_atomic = i915_gem_dmabuf_kunmap_atomic,
	.mmap = i915_gem_dmabuf_mmap,
	.vmap = i915_gem_dmabuf_vmap,
	.vunmap = i915_gem_dmabuf_vunmap,
	.begin_cpu_access = i915_gem_begin_cpu_access,
};

struct dma_buf *i915_gem_prime_export(struct drm_device *dev,
				      struct drm_gem_object *gem_obj, int flags)
{
	struct drm_i915_gem_object *obj = to_intel_bo(gem_obj);

	return dma_buf_export(obj, &i915_dmabuf_ops, obj->base.size, 0600);
}

struct drm_gem_object *i915_gem_prime_import(struct drm_device *dev,
					     struct dma_buf *dma_buf)
{
	struct dma_buf_attachment *attach;
	struct sg_table *sg;
	struct drm_i915_gem_object *obj;
	int ret;

	/* is this one of own objects? */
	if (dma_buf->ops == &i915_dmabuf_ops) {
		obj = dma_buf->priv;
		/* is it from our device? */
		if (obj->base.dev == dev) {
			drm_gem_object_reference(&obj->base);
			return &obj->base;
		}
	}

	/* need to attach */
	attach = dma_buf_attach(dma_buf, dev->dev);
	if (IS_ERR(attach))
		return ERR_CAST(attach);

	sg = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sg)) {
		ret = PTR_ERR(sg);
		goto fail_detach;
	}

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (obj == NULL) {
		ret = -ENOMEM;
		goto fail_unmap;
	}

	ret = drm_gem_private_object_init(dev, &obj->base, dma_buf->size);
	if (ret) {
		kfree(obj);
		goto fail_unmap;
	}

	obj->has_dma_mapping = true;
	obj->sg_table = sg;
	obj->base.import_attach = attach;

	return &obj->base;

fail_unmap:
	dma_buf_unmap_attachment(attach, sg, DMA_BIDIRECTIONAL);
fail_detach:
	dma_buf_detach(dma_buf, attach);
	return ERR_PTR(ret);
}

