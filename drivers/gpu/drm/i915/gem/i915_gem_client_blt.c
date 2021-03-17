// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"
#include "gt/intel_context.h"
#include "gt/intel_engine_pm.h"
#include "i915_gem_client_blt.h"
#include "i915_gem_object_blt.h"

struct i915_sleeve {
	struct i915_vma *vma;
	struct drm_i915_gem_object *obj;
	struct sg_table *pages;
	struct i915_page_sizes page_sizes;
};

static int vma_set_pages(struct i915_vma *vma)
{
	struct i915_sleeve *sleeve = vma->private;

	vma->pages = sleeve->pages;
	vma->page_sizes = sleeve->page_sizes;

	return 0;
}

static void vma_clear_pages(struct i915_vma *vma)
{
	GEM_BUG_ON(!vma->pages);
	vma->pages = NULL;
}

static void vma_bind(struct i915_address_space *vm,
		     struct i915_vm_pt_stash *stash,
		     struct i915_vma *vma,
		     enum i915_cache_level cache_level,
		     u32 flags)
{
	vm->vma_ops.bind_vma(vm, stash, vma, cache_level, flags);
}

static void vma_unbind(struct i915_address_space *vm, struct i915_vma *vma)
{
	vm->vma_ops.unbind_vma(vm, vma);
}

static const struct i915_vma_ops proxy_vma_ops = {
	.set_pages = vma_set_pages,
	.clear_pages = vma_clear_pages,
	.bind_vma = vma_bind,
	.unbind_vma = vma_unbind,
};

static struct i915_sleeve *create_sleeve(struct i915_address_space *vm,
					 struct drm_i915_gem_object *obj,
					 struct sg_table *pages,
					 struct i915_page_sizes *page_sizes)
{
	struct i915_sleeve *sleeve;
	struct i915_vma *vma;
	int err;

	sleeve = kzalloc(sizeof(*sleeve), GFP_KERNEL);
	if (!sleeve)
		return ERR_PTR(-ENOMEM);

	vma = i915_vma_instance(obj, vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto err_free;
	}

	vma->private = sleeve;
	vma->ops = &proxy_vma_ops;

	sleeve->vma = vma;
	sleeve->pages = pages;
	sleeve->page_sizes = *page_sizes;

	return sleeve;

err_free:
	kfree(sleeve);
	return ERR_PTR(err);
}

static void destroy_sleeve(struct i915_sleeve *sleeve)
{
	kfree(sleeve);
}

struct clear_pages_work {
	struct dma_fence dma;
	struct dma_fence_cb cb;
	struct i915_sw_fence wait;
	struct work_struct work;
	struct irq_work irq_work;
	struct i915_sleeve *sleeve;
	struct intel_context *ce;
	u32 value;
};

static const char *clear_pages_work_driver_name(struct dma_fence *fence)
{
	return DRIVER_NAME;
}

static const char *clear_pages_work_timeline_name(struct dma_fence *fence)
{
	return "clear";
}

static void clear_pages_work_release(struct dma_fence *fence)
{
	struct clear_pages_work *w = container_of(fence, typeof(*w), dma);

	destroy_sleeve(w->sleeve);

	i915_sw_fence_fini(&w->wait);

	BUILD_BUG_ON(offsetof(typeof(*w), dma));
	dma_fence_free(&w->dma);
}

static const struct dma_fence_ops clear_pages_work_ops = {
	.get_driver_name = clear_pages_work_driver_name,
	.get_timeline_name = clear_pages_work_timeline_name,
	.release = clear_pages_work_release,
};

static void clear_pages_signal_irq_worker(struct irq_work *work)
{
	struct clear_pages_work *w = container_of(work, typeof(*w), irq_work);

	dma_fence_signal(&w->dma);
	dma_fence_put(&w->dma);
}

static void clear_pages_dma_fence_cb(struct dma_fence *fence,
				     struct dma_fence_cb *cb)
{
	struct clear_pages_work *w = container_of(cb, typeof(*w), cb);

	if (fence->error)
		dma_fence_set_error(&w->dma, fence->error);

	/*
	 * Push the signalling of the fence into yet another worker to avoid
	 * the nightmare locking around the fence spinlock.
	 */
	irq_work_queue(&w->irq_work);
}

static void clear_pages_worker(struct work_struct *work)
{
	struct clear_pages_work *w = container_of(work, typeof(*w), work);
	struct drm_i915_gem_object *obj = w->sleeve->vma->obj;
	struct i915_vma *vma = w->sleeve->vma;
	struct i915_gem_ww_ctx ww;
	struct i915_request *rq;
	struct i915_vma *batch;
	int err = w->dma.error;

	if (unlikely(err))
		goto out_signal;

	if (obj->cache_dirty) {
		if (i915_gem_object_has_struct_page(obj))
			drm_clflush_sg(w->sleeve->pages);
		obj->cache_dirty = false;
	}
	obj->read_domains = I915_GEM_GPU_DOMAINS;
	obj->write_domain = 0;

	i915_gem_ww_ctx_init(&ww, false);
	intel_engine_pm_get(w->ce->engine);
retry:
	err = intel_context_pin_ww(w->ce, &ww);
	if (err)
		goto out_signal;

	batch = intel_emit_vma_fill_blt(w->ce, vma, &ww, w->value);
	if (IS_ERR(batch)) {
		err = PTR_ERR(batch);
		goto out_ctx;
	}

	rq = i915_request_create(w->ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto out_batch;
	}

	/* There's no way the fence has signalled */
	if (dma_fence_add_callback(&rq->fence, &w->cb,
				   clear_pages_dma_fence_cb))
		GEM_BUG_ON(1);

	err = intel_emit_vma_mark_active(batch, rq);
	if (unlikely(err))
		goto out_request;

	if (w->ce->engine->emit_init_breadcrumb) {
		err = w->ce->engine->emit_init_breadcrumb(rq);
		if (unlikely(err))
			goto out_request;
	}

	/*
	 * w->dma is already exported via (vma|obj)->resv we need only
	 * keep track of the GPU activity within this vma/request, and
	 * propagate the signal from the request to w->dma.
	 */
	err = __i915_vma_move_to_active(vma, rq);
	if (err)
		goto out_request;

	err = w->ce->engine->emit_bb_start(rq,
					   batch->node.start, batch->node.size,
					   0);
out_request:
	if (unlikely(err)) {
		i915_request_set_error_once(rq, err);
		err = 0;
	}

	i915_request_add(rq);
out_batch:
	intel_emit_vma_release(w->ce, batch);
out_ctx:
	intel_context_unpin(w->ce);
out_signal:
	if (err == -EDEADLK) {
		err = i915_gem_ww_ctx_backoff(&ww);
		if (!err)
			goto retry;
	}
	i915_gem_ww_ctx_fini(&ww);

	i915_vma_unpin(w->sleeve->vma);
	intel_engine_pm_put(w->ce->engine);

	if (unlikely(err)) {
		dma_fence_set_error(&w->dma, err);
		dma_fence_signal(&w->dma);
		dma_fence_put(&w->dma);
	}
}

static int pin_wait_clear_pages_work(struct clear_pages_work *w,
				     struct intel_context *ce)
{
	struct i915_vma *vma = w->sleeve->vma;
	struct i915_gem_ww_ctx ww;
	int err;

	i915_gem_ww_ctx_init(&ww, false);
retry:
	err = i915_gem_object_lock(vma->obj, &ww);
	if (err)
		goto out;

	err = i915_vma_pin_ww(vma, &ww, 0, 0, PIN_USER);
	if (unlikely(err))
		goto out;

	err = i915_sw_fence_await_reservation(&w->wait,
					      vma->obj->base.resv, NULL,
					      true, 0, I915_FENCE_GFP);
	if (err)
		goto err_unpin_vma;

	dma_resv_add_excl_fence(vma->obj->base.resv, &w->dma);

err_unpin_vma:
	if (err)
		i915_vma_unpin(vma);
out:
	if (err == -EDEADLK) {
		err = i915_gem_ww_ctx_backoff(&ww);
		if (!err)
			goto retry;
	}
	i915_gem_ww_ctx_fini(&ww);
	return err;
}

static int __i915_sw_fence_call
clear_pages_work_notify(struct i915_sw_fence *fence,
			enum i915_sw_fence_notify state)
{
	struct clear_pages_work *w = container_of(fence, typeof(*w), wait);

	switch (state) {
	case FENCE_COMPLETE:
		schedule_work(&w->work);
		break;

	case FENCE_FREE:
		dma_fence_put(&w->dma);
		break;
	}

	return NOTIFY_DONE;
}

static DEFINE_SPINLOCK(fence_lock);

/* XXX: better name please */
int i915_gem_schedule_fill_pages_blt(struct drm_i915_gem_object *obj,
				     struct intel_context *ce,
				     struct sg_table *pages,
				     struct i915_page_sizes *page_sizes,
				     u32 value)
{
	struct clear_pages_work *work;
	struct i915_sleeve *sleeve;
	int err;

	sleeve = create_sleeve(ce->vm, obj, pages, page_sizes);
	if (IS_ERR(sleeve))
		return PTR_ERR(sleeve);

	work = kmalloc(sizeof(*work), GFP_KERNEL);
	if (!work) {
		destroy_sleeve(sleeve);
		return -ENOMEM;
	}

	work->value = value;
	work->sleeve = sleeve;
	work->ce = ce;

	INIT_WORK(&work->work, clear_pages_worker);

	init_irq_work(&work->irq_work, clear_pages_signal_irq_worker);

	dma_fence_init(&work->dma, &clear_pages_work_ops, &fence_lock, 0, 0);
	i915_sw_fence_init(&work->wait, clear_pages_work_notify);

	err = pin_wait_clear_pages_work(work, ce);
	if (err < 0)
		dma_fence_set_error(&work->dma, err);

	dma_fence_get(&work->dma);
	i915_sw_fence_commit(&work->wait);

	return err;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/i915_gem_client_blt.c"
#endif
