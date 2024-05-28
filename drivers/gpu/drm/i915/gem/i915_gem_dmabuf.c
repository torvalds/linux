/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright 2012 Red Hat Inc
 */

#include <linux/dma-buf.h>
#include <linux/highmem.h>
#include <linux/dma-resv.h>
#include <linux/module.h>

#include <asm/smp.h>

#include "gem/i915_gem_dmabuf.h"
#include "i915_drv.h"
#include "i915_gem_object.h"
#include "i915_scatterlist.h"

MODULE_IMPORT_NS(DMA_BUF);

I915_SELFTEST_DECLARE(static bool force_different_devices;)

static struct drm_i915_gem_object *dma_buf_to_obj(struct dma_buf *buf)
{
	return to_intel_bo(buf->priv);
}

static struct sg_table *i915_gem_map_dma_buf(struct dma_buf_attachment *attach,
					     enum dma_data_direction dir)
{
	struct drm_i915_gem_object *obj = dma_buf_to_obj(attach->dmabuf);
	struct sg_table *sgt;
	struct scatterlist *src, *dst;
	int ret, i;

	/*
	 * Make a copy of the object's sgt, so that we can make an independent
	 * mapping
	 */
	sgt = kmalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt) {
		ret = -ENOMEM;
		goto err;
	}

	ret = sg_alloc_table(sgt, obj->mm.pages->orig_nents, GFP_KERNEL);
	if (ret)
		goto err_free;

	dst = sgt->sgl;
	for_each_sg(obj->mm.pages->sgl, src, obj->mm.pages->orig_nents, i) {
		sg_set_page(dst, sg_page(src), src->length, 0);
		dst = sg_next(dst);
	}

	ret = dma_map_sgtable(attach->dev, sgt, dir, DMA_ATTR_SKIP_CPU_SYNC);
	if (ret)
		goto err_free_sg;

	return sgt;

err_free_sg:
	sg_free_table(sgt);
err_free:
	kfree(sgt);
err:
	return ERR_PTR(ret);
}

static int i915_gem_dmabuf_vmap(struct dma_buf *dma_buf,
				struct iosys_map *map)
{
	struct drm_i915_gem_object *obj = dma_buf_to_obj(dma_buf);
	void *vaddr;

	vaddr = i915_gem_object_pin_map(obj, I915_MAP_WB);
	if (IS_ERR(vaddr))
		return PTR_ERR(vaddr);

	iosys_map_set_vaddr(map, vaddr);

	return 0;
}

static void i915_gem_dmabuf_vunmap(struct dma_buf *dma_buf,
				   struct iosys_map *map)
{
	struct drm_i915_gem_object *obj = dma_buf_to_obj(dma_buf);

	i915_gem_object_flush_map(obj);
	i915_gem_object_unpin_map(obj);
}

static int i915_gem_dmabuf_mmap(struct dma_buf *dma_buf, struct vm_area_struct *vma)
{
	struct drm_i915_gem_object *obj = dma_buf_to_obj(dma_buf);
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	int ret;

	if (obj->base.size < vma->vm_end - vma->vm_start)
		return -EINVAL;

	if (HAS_LMEM(i915))
		return drm_gem_prime_mmap(&obj->base, vma);

	if (!obj->base.filp)
		return -ENODEV;

	ret = call_mmap(obj->base.filp, vma);
	if (ret)
		return ret;

	vma_set_file(vma, obj->base.filp);

	return 0;
}

static int i915_gem_begin_cpu_access(struct dma_buf *dma_buf, enum dma_data_direction direction)
{
	struct drm_i915_gem_object *obj = dma_buf_to_obj(dma_buf);
	bool write = (direction == DMA_BIDIRECTIONAL || direction == DMA_TO_DEVICE);
	struct i915_gem_ww_ctx ww;
	int err;

	i915_gem_ww_ctx_init(&ww, true);
retry:
	err = i915_gem_object_lock(obj, &ww);
	if (!err)
		err = i915_gem_object_pin_pages(obj);
	if (!err) {
		err = i915_gem_object_set_to_cpu_domain(obj, write);
		i915_gem_object_unpin_pages(obj);
	}
	if (err == -EDEADLK) {
		err = i915_gem_ww_ctx_backoff(&ww);
		if (!err)
			goto retry;
	}
	i915_gem_ww_ctx_fini(&ww);
	return err;
}

static int i915_gem_end_cpu_access(struct dma_buf *dma_buf, enum dma_data_direction direction)
{
	struct drm_i915_gem_object *obj = dma_buf_to_obj(dma_buf);
	struct i915_gem_ww_ctx ww;
	int err;

	i915_gem_ww_ctx_init(&ww, true);
retry:
	err = i915_gem_object_lock(obj, &ww);
	if (!err)
		err = i915_gem_object_pin_pages(obj);
	if (!err) {
		err = i915_gem_object_set_to_gtt_domain(obj, false);
		i915_gem_object_unpin_pages(obj);
	}
	if (err == -EDEADLK) {
		err = i915_gem_ww_ctx_backoff(&ww);
		if (!err)
			goto retry;
	}
	i915_gem_ww_ctx_fini(&ww);
	return err;
}

static int i915_gem_dmabuf_attach(struct dma_buf *dmabuf,
				  struct dma_buf_attachment *attach)
{
	struct drm_i915_gem_object *obj = dma_buf_to_obj(dmabuf);
	struct i915_gem_ww_ctx ww;
	int err;

	if (!i915_gem_object_can_migrate(obj, INTEL_REGION_SMEM))
		return -EOPNOTSUPP;

	for_i915_gem_ww(&ww, err, true) {
		err = i915_gem_object_lock(obj, &ww);
		if (err)
			continue;

		err = i915_gem_object_migrate(obj, &ww, INTEL_REGION_SMEM);
		if (err)
			continue;

		err = i915_gem_object_wait_migration(obj, 0);
		if (err)
			continue;

		err = i915_gem_object_pin_pages(obj);
	}

	return err;
}

static void i915_gem_dmabuf_detach(struct dma_buf *dmabuf,
				   struct dma_buf_attachment *attach)
{
	struct drm_i915_gem_object *obj = dma_buf_to_obj(dmabuf);

	i915_gem_object_unpin_pages(obj);
}

static const struct dma_buf_ops i915_dmabuf_ops =  {
	.attach = i915_gem_dmabuf_attach,
	.detach = i915_gem_dmabuf_detach,
	.map_dma_buf = i915_gem_map_dma_buf,
	.unmap_dma_buf = drm_gem_unmap_dma_buf,
	.release = drm_gem_dmabuf_release,
	.mmap = i915_gem_dmabuf_mmap,
	.vmap = i915_gem_dmabuf_vmap,
	.vunmap = i915_gem_dmabuf_vunmap,
	.begin_cpu_access = i915_gem_begin_cpu_access,
	.end_cpu_access = i915_gem_end_cpu_access,
};

struct dma_buf *i915_gem_prime_export(struct drm_gem_object *gem_obj, int flags)
{
	struct drm_i915_gem_object *obj = to_intel_bo(gem_obj);
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	exp_info.ops = &i915_dmabuf_ops;
	exp_info.size = gem_obj->size;
	exp_info.flags = flags;
	exp_info.priv = gem_obj;
	exp_info.resv = obj->base.resv;

	if (obj->ops->dmabuf_export) {
		int ret = obj->ops->dmabuf_export(obj);
		if (ret)
			return ERR_PTR(ret);
	}

	return drm_gem_dmabuf_export(gem_obj->dev, &exp_info);
}

static int i915_gem_object_get_pages_dmabuf(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct sg_table *sgt;

	assert_object_held(obj);

	sgt = dma_buf_map_attachment(obj->base.import_attach,
				     DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt))
		return PTR_ERR(sgt);

	/*
	 * DG1 is special here since it still snoops transactions even with
	 * CACHE_NONE. This is not the case with other HAS_SNOOP platforms. We
	 * might need to revisit this as we add new discrete platforms.
	 *
	 * XXX: Consider doing a vmap flush or something, where possible.
	 * Currently we just do a heavy handed wbinvd_on_all_cpus() here since
	 * the underlying sg_table might not even point to struct pages, so we
	 * can't just call drm_clflush_sg or similar, like we do elsewhere in
	 * the driver.
	 */
	if (i915_gem_object_can_bypass_llc(obj) ||
	    (!HAS_LLC(i915) && !IS_DG1(i915)))
		wbinvd_on_all_cpus();

	__i915_gem_object_set_pages(obj, sgt);

	return 0;
}

static void i915_gem_object_put_pages_dmabuf(struct drm_i915_gem_object *obj,
					     struct sg_table *sgt)
{
	dma_buf_unmap_attachment(obj->base.import_attach, sgt,
				 DMA_BIDIRECTIONAL);
}

static const struct drm_i915_gem_object_ops i915_gem_object_dmabuf_ops = {
	.name = "i915_gem_object_dmabuf",
	.get_pages = i915_gem_object_get_pages_dmabuf,
	.put_pages = i915_gem_object_put_pages_dmabuf,
};

struct drm_gem_object *i915_gem_prime_import(struct drm_device *dev,
					     struct dma_buf *dma_buf)
{
	static struct lock_class_key lock_class;
	struct dma_buf_attachment *attach;
	struct drm_i915_gem_object *obj;
	int ret;

	/* is this one of own objects? */
	if (dma_buf->ops == &i915_dmabuf_ops) {
		obj = dma_buf_to_obj(dma_buf);
		/* is it from our device? */
		if (obj->base.dev == dev &&
		    !I915_SELFTEST_ONLY(force_different_devices)) {
			/*
			 * Importing dmabuf exported from out own gem increases
			 * refcount on gem itself instead of f_count of dmabuf.
			 */
			return &i915_gem_object_get(obj)->base;
		}
	}

	if (i915_gem_object_size_2big(dma_buf->size))
		return ERR_PTR(-E2BIG);

	/* need to attach */
	attach = dma_buf_attach(dma_buf, dev->dev);
	if (IS_ERR(attach))
		return ERR_CAST(attach);

	get_dma_buf(dma_buf);

	obj = i915_gem_object_alloc();
	if (!obj) {
		ret = -ENOMEM;
		goto fail_detach;
	}

	drm_gem_private_object_init(dev, &obj->base, dma_buf->size);
	i915_gem_object_init(obj, &i915_gem_object_dmabuf_ops, &lock_class,
			     I915_BO_ALLOC_USER);
	obj->base.import_attach = attach;
	obj->base.resv = dma_buf->resv;

	/* We use GTT as shorthand for a coherent domain, one that is
	 * neither in the GPU cache nor in the CPU cache, where all
	 * writes are immediately visible in memory. (That's not strictly
	 * true, but it's close! There are internal buffers such as the
	 * write-combined buffer or a delay through the chipset for GTT
	 * writes that do require us to treat GTT as a separate cache domain.)
	 */
	obj->read_domains = I915_GEM_DOMAIN_GTT;
	obj->write_domain = 0;

	return &obj->base;

fail_detach:
	dma_buf_detach(dma_buf, attach);
	dma_buf_put(dma_buf);

	return ERR_PTR(ret);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/mock_dmabuf.c"
#include "selftests/i915_gem_dmabuf.c"
#endif
