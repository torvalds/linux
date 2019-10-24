// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */
#include "i915_gem_client_blt.h"

#include "i915_gem_object_blt.h"
#include "intel_drv.h"

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

static int vma_bind(struct i915_vma *vma,
		    enum i915_cache_level cache_level,
		    u32 flags)
{
	return vma->vm->vma_ops.bind_vma(vma, cache_level, flags);
}

static void vma_unbind(struct i915_vma *vma)
{
	vma->vm->vma_ops.unbind_vma(vma);
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
	sleeve->obj = i915_gem_object_get(obj);
	sleeve->pages = pages;
	sleeve->page_sizes = *page_sizes;

	return sleeve;

err_free:
	kfree(sleeve);
	return ERR_PTR(err);
}

static void destroy_sleeve(struct i915_sleeve *sleeve)
{
	i915_gem_object_put(sleeve->obj);
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
	struct drm_i915_private *i915 = w->ce->gem_context->i915;
	struct drm_i915_gem_object *obj = w->sleeve->obj;
	struct i915_vma *vma = w->sleeve->vma;
	struct i915_request *rq;
	int err = w->dma.error;

	if (unlikely(err))
		goto out_signal;

	if (obj->cache_dirty) {
		obj->write_domain = 0;
		if (i915_gem_object_has_struct_page(obj))
			drm_clflush_sg(w->sleeve->pages);
		obj->cache_dirty = false;
	}

	/* XXX: we need to kill this */
	mutex_lock(&i915->drm.struct_mutex);
	err = i915_vma_pin(vma, 0, 0, PIN_USER);
	if (unlikely(err))
		goto out_unlock;

	rq = i915_request_create(w->ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto out_unpin;
	}

	/* There's no way the fence has signalled */
	if (dma_fence_add_callback(&rq->fence, &w->cb,
				   clear_pages_dma_fence_cb))
		GEM_BUG_ON(1);

	if (w->ce->engine->emit_init_breadcrumb) {
		err = w->ce->engine->emit_init_breadcrumb(rq);
		if (unlikely(err))
			goto out_request;
	}

	/* XXX: more feverish nightmares await */
	i915_vma_lock(vma);
	err = i915_vma_move_to_active(vma, rq, EXEC_OBJECT_WRITE);
	i915_vma_unlock(vma);
	if (err)
		goto out_request;

	err = intel_emit_vma_fill_blt(rq, vma, w->value);
out_request:
	if (unlikely(err)) {
		i915_request_skip(rq, err);
		err = 0;
	}

	i915_request_add(rq);
out_unpin:
	i915_vma_unpin(vma);
out_unlock:
	mutex_unlock(&i915->drm.struct_mutex);
out_signal:
	if (unlikely(err)) {
		dma_fence_set_error(&w->dma, err);
		dma_fence_signal(&w->dma);
		dma_fence_put(&w->dma);
	}
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
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct i915_gem_context *ctx = ce->gem_context;
	struct i915_address_space *vm = ctx->vm ?: &i915->ggtt.vm;
	struct clear_pages_work *work;
	struct i915_sleeve *sleeve;
	int err;

	sleeve = create_sleeve(vm, obj, pages, page_sizes);
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

	dma_fence_init(&work->dma,
		       &clear_pages_work_ops,
		       &fence_lock,
		       i915->mm.unordered_timeline,
		       0);
	i915_sw_fence_init(&work->wait, clear_pages_work_notify);

	i915_gem_object_lock(obj);
	err = i915_sw_fence_await_reservation(&work->wait,
					      obj->base.resv, NULL,
					      true, I915_FENCE_TIMEOUT,
					      I915_FENCE_GFP);
	if (err < 0) {
		dma_fence_set_error(&work->dma, err);
	} else {
		reservation_object_add_excl_fence(obj->base.resv, &work->dma);
		err = 0;
	}
	i915_gem_object_unlock(obj);

	dma_fence_get(&work->dma);
	i915_sw_fence_commit(&work->wait);

	return err;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/i915_gem_client_blt.c"
#endif
