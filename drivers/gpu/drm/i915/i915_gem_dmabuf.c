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

#include <linux/dma-buf.h>
#include <linux/reservation.h>

#include <drm/drmP.h>

#include "i915_drv.h"

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

	dma_unmap_sg(attachment->dev, sg->sgl, sg->nents, dir);
	sg_free_table(sg);
	kfree(sg);

	mutex_lock(&obj->base.dev->struct_mutex);
	i915_gem_object_unpin_pages(obj);
	mutex_unlock(&obj->base.dev->struct_mutex);
}

static void *i915_gem_dmabuf_vmap(struct dma_buf *dma_buf)
{
	struct drm_i915_gem_object *obj = dma_buf_to_obj(dma_buf);
	struct drm_device *dev = obj->base.dev;
	void *addr;
	int ret;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ERR_PTR(ret);

	addr = i915_gem_object_pin_map(obj, I915_MAP_WB);
	mutex_unlock(&dev->struct_mutex);

	return addr;
}

static void i915_gem_dmabuf_vunmap(struct dma_buf *dma_buf, void *vaddr)
{
	struct drm_i915_gem_object *obj = dma_buf_to_obj(dma_buf);
	struct drm_device *dev = obj->base.dev;

	mutex_lock(&dev->struct_mutex);
	i915_gem_object_unpin_map(obj);
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
	struct drm_i915_gem_object *obj = dma_buf_to_obj(dma_buf);
	int ret;

	if (obj->base.size < vma->vm_end - vma->vm_start)
		return -EINVAL;

	if (!obj->base.filp)
		return -ENODEV;

	ret = obj->base.filp->f_op->mmap(obj->base.filp, vma);
	if (ret)
		return ret;

	fput(vma->vm_file);
	vma->vm_file = get_file(obj->base.filp);

	return 0;
}

static int i915_gem_begin_cpu_access(struct dma_buf *dma_buf, enum dma_data_direction direction)
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

static int i915_gem_end_cpu_access(struct dma_buf *dma_buf, enum dma_data_direction direction)
{
	struct drm_i915_gem_object *obj = dma_buf_to_obj(dma_buf);
	struct drm_device *dev = obj->base.dev;
	int ret;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	ret = i915_gem_object_set_to_gtt_domain(obj, false);
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
	.end_cpu_access = i915_gem_end_cpu_access,
};

static void export_fences(struct drm_i915_gem_object *obj,
			  struct dma_buf *dma_buf)
{
	struct reservation_object *resv = dma_buf->resv;
	struct drm_i915_gem_request *req;
	unsigned long active;
	int idx;

	active = __I915_BO_ACTIVE(obj);
	if (!active)
		return;

	/* Serialise with execbuf to prevent concurrent fence-loops */
	mutex_lock(&obj->base.dev->struct_mutex);

	/* Mark the object for future fences before racily adding old fences */
	obj->base.dma_buf = dma_buf;

	ww_mutex_lock(&resv->lock, NULL);

	for_each_active(active, idx) {
		req = i915_gem_active_get(&obj->last_read[idx],
					  &obj->base.dev->struct_mutex);
		if (!req)
			continue;

		if (reservation_object_reserve_shared(resv) == 0)
			reservation_object_add_shared_fence(resv, &req->fence);

		i915_gem_request_put(req);
	}

	req = i915_gem_active_get(&obj->last_write,
				  &obj->base.dev->struct_mutex);
	if (req) {
		reservation_object_add_excl_fence(resv, &req->fence);
		i915_gem_request_put(req);
	}

	ww_mutex_unlock(&resv->lock);
	mutex_unlock(&obj->base.dev->struct_mutex);
}

struct dma_buf *i915_gem_prime_export(struct drm_device *dev,
				      struct drm_gem_object *gem_obj, int flags)
{
	struct drm_i915_gem_object *obj = to_intel_bo(gem_obj);
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct dma_buf *dma_buf;

	exp_info.ops = &i915_dmabuf_ops;
	exp_info.size = gem_obj->size;
	exp_info.flags = flags;
	exp_info.priv = gem_obj;

	if (obj->ops->dmabuf_export) {
		int ret = obj->ops->dmabuf_export(obj);
		if (ret)
			return ERR_PTR(ret);
	}

	dma_buf = dma_buf_export(&exp_info);
	if (IS_ERR(dma_buf))
		return dma_buf;

	export_fences(obj, dma_buf);
	return dma_buf;
}

static int i915_gem_object_get_pages_dmabuf(struct drm_i915_gem_object *obj)
{
	struct sg_table *sg;

	sg = dma_buf_map_attachment(obj->base.import_attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sg))
		return PTR_ERR(sg);

	obj->pages = sg;
	return 0;
}

static void i915_gem_object_put_pages_dmabuf(struct drm_i915_gem_object *obj)
{
	dma_buf_unmap_attachment(obj->base.import_attach,
				 obj->pages, DMA_BIDIRECTIONAL);
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
			return &i915_gem_object_get(obj)->base;
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

	/* We use GTT as shorthand for a coherent domain, one that is
	 * neither in the GPU cache nor in the CPU cache, where all
	 * writes are immediately visible in memory. (That's not strictly
	 * true, but it's close! There are internal buffers such as the
	 * write-combined buffer or a delay through the chipset for GTT
	 * writes that do require us to treat GTT as a separate cache domain.)
	 */
	obj->base.read_domains = I915_GEM_DOMAIN_GTT;
	obj->base.write_domain = 0;

	return &obj->base;

fail_detach:
	dma_buf_detach(dma_buf, attach);
	dma_buf_put(dma_buf);

	return ERR_PTR(ret);
}
