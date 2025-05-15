// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_dma_buf.h"

#include <kunit/test.h>
#include <linux/dma-buf.h>
#include <linux/pci-p2pdma.h>

#include <drm/drm_device.h>
#include <drm/drm_prime.h>
#include <drm/ttm/ttm_tt.h>

#include "tests/xe_test.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_pm.h"
#include "xe_ttm_vram_mgr.h"
#include "xe_vm.h"

MODULE_IMPORT_NS("DMA_BUF");

static int xe_dma_buf_attach(struct dma_buf *dmabuf,
			     struct dma_buf_attachment *attach)
{
	struct drm_gem_object *obj = attach->dmabuf->priv;

	if (attach->peer2peer &&
	    pci_p2pdma_distance(to_pci_dev(obj->dev->dev), attach->dev, false) < 0)
		attach->peer2peer = false;

	if (!attach->peer2peer && !xe_bo_can_migrate(gem_to_xe_bo(obj), XE_PL_TT))
		return -EOPNOTSUPP;

	xe_pm_runtime_get(to_xe_device(obj->dev));
	return 0;
}

static void xe_dma_buf_detach(struct dma_buf *dmabuf,
			      struct dma_buf_attachment *attach)
{
	struct drm_gem_object *obj = attach->dmabuf->priv;

	xe_pm_runtime_put(to_xe_device(obj->dev));
}

static int xe_dma_buf_pin(struct dma_buf_attachment *attach)
{
	struct drm_gem_object *obj = attach->dmabuf->priv;
	struct xe_bo *bo = gem_to_xe_bo(obj);
	struct xe_device *xe = xe_bo_device(bo);
	int ret;

	/*
	 * For now only support pinning in TT memory, for two reasons:
	 * 1) Avoid pinning in a placement not accessible to some importers.
	 * 2) Pinning in VRAM requires PIN accounting which is a to-do.
	 */
	if (xe_bo_is_pinned(bo) && !xe_bo_is_mem_type(bo, XE_PL_TT)) {
		drm_dbg(&xe->drm, "Can't migrate pinned bo for dma-buf pin.\n");
		return -EINVAL;
	}

	ret = xe_bo_migrate(bo, XE_PL_TT);
	if (ret) {
		if (ret != -EINTR && ret != -ERESTARTSYS)
			drm_dbg(&xe->drm,
				"Failed migrating dma-buf to TT memory: %pe\n",
				ERR_PTR(ret));
		return ret;
	}

	ret = xe_bo_pin_external(bo);
	xe_assert(xe, !ret);

	return 0;
}

static void xe_dma_buf_unpin(struct dma_buf_attachment *attach)
{
	struct drm_gem_object *obj = attach->dmabuf->priv;
	struct xe_bo *bo = gem_to_xe_bo(obj);

	xe_bo_unpin_external(bo);
}

static struct sg_table *xe_dma_buf_map(struct dma_buf_attachment *attach,
				       enum dma_data_direction dir)
{
	struct dma_buf *dma_buf = attach->dmabuf;
	struct drm_gem_object *obj = dma_buf->priv;
	struct xe_bo *bo = gem_to_xe_bo(obj);
	struct sg_table *sgt;
	int r = 0;

	if (!attach->peer2peer && !xe_bo_can_migrate(bo, XE_PL_TT))
		return ERR_PTR(-EOPNOTSUPP);

	if (!xe_bo_is_pinned(bo)) {
		if (!attach->peer2peer)
			r = xe_bo_migrate(bo, XE_PL_TT);
		else
			r = xe_bo_validate(bo, NULL, false);
		if (r)
			return ERR_PTR(r);
	}

	switch (bo->ttm.resource->mem_type) {
	case XE_PL_TT:
		sgt = drm_prime_pages_to_sg(obj->dev,
					    bo->ttm.ttm->pages,
					    bo->ttm.ttm->num_pages);
		if (IS_ERR(sgt))
			return sgt;

		if (dma_map_sgtable(attach->dev, sgt, dir,
				    DMA_ATTR_SKIP_CPU_SYNC))
			goto error_free;
		break;

	case XE_PL_VRAM0:
	case XE_PL_VRAM1:
		r = xe_ttm_vram_mgr_alloc_sgt(xe_bo_device(bo),
					      bo->ttm.resource, 0,
					      bo->ttm.base.size, attach->dev,
					      dir, &sgt);
		if (r)
			return ERR_PTR(r);
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	return sgt;

error_free:
	sg_free_table(sgt);
	kfree(sgt);
	return ERR_PTR(-EBUSY);
}

static void xe_dma_buf_unmap(struct dma_buf_attachment *attach,
			     struct sg_table *sgt,
			     enum dma_data_direction dir)
{
	if (sg_page(sgt->sgl)) {
		dma_unmap_sgtable(attach->dev, sgt, dir, 0);
		sg_free_table(sgt);
		kfree(sgt);
	} else {
		xe_ttm_vram_mgr_free_sgt(attach->dev, dir, sgt);
	}
}

static int xe_dma_buf_begin_cpu_access(struct dma_buf *dma_buf,
				       enum dma_data_direction direction)
{
	struct drm_gem_object *obj = dma_buf->priv;
	struct xe_bo *bo = gem_to_xe_bo(obj);
	bool reads =  (direction == DMA_BIDIRECTIONAL ||
		       direction == DMA_FROM_DEVICE);

	if (!reads)
		return 0;

	/* Can we do interruptible lock here? */
	xe_bo_lock(bo, false);
	(void)xe_bo_migrate(bo, XE_PL_TT);
	xe_bo_unlock(bo);

	return 0;
}

static const struct dma_buf_ops xe_dmabuf_ops = {
	.attach = xe_dma_buf_attach,
	.detach = xe_dma_buf_detach,
	.pin = xe_dma_buf_pin,
	.unpin = xe_dma_buf_unpin,
	.map_dma_buf = xe_dma_buf_map,
	.unmap_dma_buf = xe_dma_buf_unmap,
	.release = drm_gem_dmabuf_release,
	.begin_cpu_access = xe_dma_buf_begin_cpu_access,
	.mmap = drm_gem_dmabuf_mmap,
	.vmap = drm_gem_dmabuf_vmap,
	.vunmap = drm_gem_dmabuf_vunmap,
};

struct dma_buf *xe_gem_prime_export(struct drm_gem_object *obj, int flags)
{
	struct xe_bo *bo = gem_to_xe_bo(obj);
	struct dma_buf *buf;

	if (bo->vm)
		return ERR_PTR(-EPERM);

	buf = drm_gem_prime_export(obj, flags);
	if (!IS_ERR(buf))
		buf->ops = &xe_dmabuf_ops;

	return buf;
}

static struct drm_gem_object *
xe_dma_buf_init_obj(struct drm_device *dev, struct xe_bo *storage,
		    struct dma_buf *dma_buf)
{
	struct dma_resv *resv = dma_buf->resv;
	struct xe_device *xe = to_xe_device(dev);
	struct xe_bo *bo;
	int ret;

	dma_resv_lock(resv, NULL);
	bo = ___xe_bo_create_locked(xe, storage, NULL, resv, NULL, dma_buf->size,
				    0, /* Will require 1way or 2way for vm_bind */
				    ttm_bo_type_sg, XE_BO_FLAG_SYSTEM);
	if (IS_ERR(bo)) {
		ret = PTR_ERR(bo);
		goto error;
	}
	dma_resv_unlock(resv);

	return &bo->ttm.base;

error:
	dma_resv_unlock(resv);
	return ERR_PTR(ret);
}

static void xe_dma_buf_move_notify(struct dma_buf_attachment *attach)
{
	struct drm_gem_object *obj = attach->importer_priv;
	struct xe_bo *bo = gem_to_xe_bo(obj);

	XE_WARN_ON(xe_bo_evict(bo, false));
}

static const struct dma_buf_attach_ops xe_dma_buf_attach_ops = {
	.allow_peer2peer = true,
	.move_notify = xe_dma_buf_move_notify
};

#if IS_ENABLED(CONFIG_DRM_XE_KUNIT_TEST)

struct dma_buf_test_params {
	struct xe_test_priv base;
	const struct dma_buf_attach_ops *attach_ops;
	bool force_different_devices;
	u32 mem_mask;
};

#define to_dma_buf_test_params(_priv) \
	container_of(_priv, struct dma_buf_test_params, base)
#endif

struct drm_gem_object *xe_gem_prime_import(struct drm_device *dev,
					   struct dma_buf *dma_buf)
{
	XE_TEST_DECLARE(struct dma_buf_test_params *test =
			to_dma_buf_test_params
			(xe_cur_kunit_priv(XE_TEST_LIVE_DMA_BUF));)
	const struct dma_buf_attach_ops *attach_ops;
	struct dma_buf_attachment *attach;
	struct drm_gem_object *obj;
	struct xe_bo *bo;

	if (dma_buf->ops == &xe_dmabuf_ops) {
		obj = dma_buf->priv;
		if (obj->dev == dev &&
		    !XE_TEST_ONLY(test && test->force_different_devices)) {
			/*
			 * Importing dmabuf exported from out own gem increases
			 * refcount on gem itself instead of f_count of dmabuf.
			 */
			drm_gem_object_get(obj);
			return obj;
		}
	}

	/*
	 * Don't publish the bo until we have a valid attachment, and a
	 * valid attachment needs the bo address. So pre-create a bo before
	 * creating the attachment and publish.
	 */
	bo = xe_bo_alloc();
	if (IS_ERR(bo))
		return ERR_CAST(bo);

	attach_ops = &xe_dma_buf_attach_ops;
#if IS_ENABLED(CONFIG_DRM_XE_KUNIT_TEST)
	if (test)
		attach_ops = test->attach_ops;
#endif

	attach = dma_buf_dynamic_attach(dma_buf, dev->dev, attach_ops, &bo->ttm.base);
	if (IS_ERR(attach)) {
		obj = ERR_CAST(attach);
		goto out_err;
	}

	/* Errors here will take care of freeing the bo. */
	obj = xe_dma_buf_init_obj(dev, bo, dma_buf);
	if (IS_ERR(obj))
		return obj;


	get_dma_buf(dma_buf);
	obj->import_attach = attach;
	return obj;

out_err:
	xe_bo_free(bo);

	return obj;
}

#if IS_ENABLED(CONFIG_DRM_XE_KUNIT_TEST)
#include "tests/xe_dma_buf.c"
#endif
