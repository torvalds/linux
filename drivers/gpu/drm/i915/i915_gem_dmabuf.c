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
#include <drm/drmP.h>
#include "i915_drv.h"
#include <linux/dma-buf.h>

static struct drm_i915_gem_object *dma_buf_to_obj(struct dma_buf *buf)
{
	return to_intel_bo(buf->priv);
}

static struct sg_table *i915_gem_map_dma_buf(struct dma_buf_attachment *attachment,
					     enum dma_data_direction dir)
{
	struct drm_i915_gem_object *obj = dma_buf_to_obj(attachment->dmabuf);
	struct sg_table *st;
	struct scatterlist *src, *dst;
	int ret, i;

	ret = i915_mutex_lock_interruptible(obj->base.dev);
	if (ret)
		goto err;

	ret = i915_gem_object_get_pages(obj);
	if (ret)
		goto err_unlock;

	i915_gem_object_pin_pages(obj);

	/* Copy sg so that we make an independent mapping */
	st = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (st == NULL) {
		ret = -ENOMEM;
		goto err_unpin;
	}

	ret = sg_alloc_table(st, obj->pages->nents, GFP_KERNEL);
	if (ret)
		goto err_free;

	src = obj->pages->sgl;
	dst = st->sgl;
	for (i = 0; i < obj->pages->nents; i++) {
		sg_set_page(dst, sg_page(src), src->length, 0);
		dst = sg_next(dst);
		src = sg_next(src);
	}

	if (!dma_map_sg(attachment->dev, st->sgl, st->nents, dir)) {
		ret =-ENOMEM;
		goto err_free_sg;
	}

	mutex_unlock(&obj->base.dev->struct_mutex);
	return st;

err_free_sg:
	sg_free_table(st);
err_free:
	kfree(st);
err_unpin:
	i915_gem_object_unpin_pages(obj);
err_unlock:
	mutex_unlock(&obj->base.dev->struct_mutex);
err:
	return ERR_PTR(ret);
}

static void i915_gem_unmap_dma_buf(struct dma_buf_attachment *attachment,
				   struct sg_table *sg,
				   enum dma_data_direction dir)
{
	struct drm_i915_gem_object *obj = dma_buf_to_obj(attachment->dmabuf);

	mutex_lock(&obj->base.dev->struct_mutex);

	dma_unmap_sg(attachment->dev, sg->sgl, sg->nents, dir);
	sg_free_table(sg);
	kfree(sg);

	i915_gem_object_unpin_pages(obj);

	mutex_unlock(&obj->base.dev->struct_mutex);
}

static void *i915_gem_dmabuf_vmap(struct dma_buf *dma_buf)
{
	struct drm_i915_gem_object *obj = dma_buf_to_obj(dma_buf);
	struct drm_device *dev = obj->base.dev;
	struct sg_page_iter sg_iter;
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
		goto err;

	i915_gem_object_pin_pages(obj);

	ret = -ENOMEM;

	pages = drm_malloc_ab(obj->base.size >> PAGE_SHIFT, sizeof(*pages));
	if (pages == NULL)
		goto err_unpin;

	i = 0;
	for_each_sg_page(obj->pages->sgl, &sg_iter, obj->pages->nents, 0)
		pages[i++] = sg_page_iter_page(&sg_iter);

	obj->dma_buf_vmapping = vmap(pages, i, 0, PAGE_KERNEL);
	drm_free_large(pages);

	if (!obj->dma_buf_vmapping)
		goto err_unpin;

	obj->vmapping_count = 1;
out_unlock:
	mutex_unlock(&dev->struct_mutex);
	return obj->dma_buf_vmapping;

err_unpin:
	i915_gem_object_unpin_pages(obj);
err:
	mutex_unlock(&dev->struct_mutex);
	return ERR_PTR(ret);
}

static void i915_gem_dmabuf_vunmap(struct dma_buf *dma_buf, void *vaddr)
{
	struct drm_i915_gem_object *obj = dma_buf_to_obj(dma_buf);
	struct drm_device *dev = obj->base.dev;

	mutex_lock(&dev->struct_mutex);
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
	struct drm_i915_gem_object *obj = dma_buf_to_obj(dma_buf);
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
	.release = drm_gem_dmabuf_release,
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
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	exp_info.ops = &i915_dmabuf_ops;
	exp_info.size = gem_obj->size;
	exp_info.flags = flags;
	exp_info.priv = gem_obj;


	if (obj->ops->dmabuf_export) {
		int ret = obj->ops->dmabuf_export(obj);
		if (ret)
			return ERR_PTR(ret);
	}

	return dma_buf_export(&exp_info);
}

static int i915_gem_object_get_pages_dmabuf(struct drm_i915_gem_object *obj)
{
	struct sg_table *sg;

	sg = dma_buf_map_attachment(obj->base.import_attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sg))
		return PTR_ERR(sg);

	obj->pages = sg;
	obj->has_dma_mapping = true;
	return 0;
}

static void i915_gem_object_put_pages_dmabuf(struct drm_i915_gem_object *obj)
{
	dma_buf_unmap_attachment(obj->base.import_attach,
				 obj->pages, DMA_BIDIRECTIONAL);
	obj->has_dma_mapping = false;
}

static const struct drm_i915_gem_object_ops i915_gem_object_dmabuf_ops = {
	.get_pages = i915_gem_object_get_pages_dmabuf,
	.put_pages = i915_gem_object_put_pages_dmabuf,
};

struct drm_gem_object *i915_gem_prime_import(struct drm_device *dev,
					     struct dma_buf *dma_buf)
{
	struct dma_buf_attachment *attach;
	struct drm_i915_gem_object *obj;
	int ret;

	/* is this one of own objects? */
	if (dma_buf->ops == &i915_dmabuf_ops) {
		obj = dma_buf_to_obj(dma_buf);
		/* is it from our device? */
		if (obj->base.dev == dev) {
			/*
			 * Importing dmabuf exported from out own gem increases
			 * refcount on gem itself instead of f_count of dmabuf.
			 */
			drm_gem_object_reference(&obj->base);
			return &obj->base;
		}
	}

	/* need to attach */
	attach = dma_buf_attach(dma_buf, dev->dev);
	if (IS_ERR(attach))
		return ERR_CAST(attach);

	get_dma_buf(dma_buf);

	obj = i915_gem_object_alloc(dev);
	if (obj == NULL) {
		ret = -ENOMEM;
		goto fail_detach;
	}

	drm_gem_private_object_init(dev, &obj->base, dma_buf->size);
	i915_gem_object_init(obj, &i915_gem_object_dmabuf_ops);
	obj->base.import_attach = attach;

	return &obj->base;

fail_detach:
	dma_buf_detach(dma_buf, attach);
	dma_buf_put(dma_buf);

	return ERR_PTR(ret);
}
