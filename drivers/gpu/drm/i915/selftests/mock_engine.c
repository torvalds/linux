/*
 * Copyright © 2016 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include "mock_engine.h"
#include "mock_request.h"

struct mock_ring {
	struct intel_ring base;
	struct i915_timeline timeline;
};

static void mock_timeline_pin(struct i915_timeline *tl)
{
	tl->pin_count++;
}

static void mock_timeline_unpin(struct i915_timeline *tl)
{
	GEM_BUG_ON(!tl->pin_count);
	tl->pin_count--;
}

static struct intel_ring *mock_ring(struct intel_engine_cs *engine)
{
	const unsigned long sz = PAGE_SIZE / 2;
	struct mock_ring *ring;

	ring = kzalloc(sizeof(*ring) + sz, GFP_KERNEL);
	if (!ring)
		return NULL;

	if (i915_timeline_init(engine->i915,
			       &ring->timeline, engine->name,
			       NULL)) {
		kfree(ring);
		return NULL;
	}

	ring->base.size = sz;
	ring->base.effective_size = sz;
	ring->base.vaddr = (void *)(ring + 1);
	ring->base.timeline = &ring->timeline;

	INIT_LIST_HEAD(&ring->base.request_list);
	intel_ring_update_space(&ring->base);

	return &ring->base;
}

static void mock_ring_free(struct intel_ring *base)
{
	struct mock_ring *ring = container_of(base, typeof(*ring), base);

	i915_timeline_fini(&ring->timeline);
	kfree(ring);
}

static struct mock_request *first_request(struct mock_engine *engine)
{
	return list_first_entry_or_null(&engine->hw_queue,
					struct mock_request,
					link);
}

static void advance(struct mock_request *request)
{
	list_del_init(&request->link);
	intel_engine_write_global_seqno(request->base.engine,
					request->base.global_seqno);
	i915_request_mark_complete(&request->base);
	GEM_BUG_ON(!i915_request_completed(&request->base));

	intel_engine_queue_breadcrumbs(request->base.engine);
}

static void hw_delay_complete(struct timer_list *t)
{
	struct mock_engine *engine = from_timer(engine, t, hw_delay);
	struct mock_request *request;
	unsigned long flags;

	spin_lock_irqsave(&engine->hw_lock, flags);

	/* Timer fired, first request is complete */
	request = first_request(engine);
	if (request)
		advance(request);

	/*
	 * Also immediately signal any subsequent 0-delay requests, but
	 * requeue the timer for the next delayed request.
	 */
	while ((request = first_request(engine))) {
		if (request->delay) {
			mod_timer(&engine->hw_delay, jiffies + request->delay);
			break;
		}

		advance(request);
	}

	spin_unlock_irqrestore(&engine->hw_lock, flags);
}

static void mock_context_unpin(struct intel_context *ce)
{
	mock_timeline_unpin(ce->ring->timeline);
	i915_gem_context_put(ce->gem_context);
}

static void mock_context_destroy(struct intel_context *ce)
{
	GEM_BUG_ON(ce->pin_count);

	if (ce->ring)
		mock_ring_free(ce->ring);
}

static const struct intel_context_ops mock_context_ops = {
	.unpin = mock_context_unpin,
	.destroy = mock_context_destroy,
};

static struct intel_context *
mock_context_pin(struct intel_engine_cs *engine,
		 struct i915_gem_context *ctx)
{
	struct intel_context *ce = to_intel_context(ctx, engine);
	int err = -ENOMEM;

	if (ce->pin_count++)
		return ce;

	if (!ce->ring) {
		ce->ring = mock_ring(engine);
		if (!ce->ring)
			goto err;
	}

	mock_timeline_pin(ce->ring->timeline);

	ce->ops = &mock_context_ops;
	i915_gem_context_get(ctx);
	return ce;

err:
	ce->pin_count = 0;
	return ERR_PTR(err);
}

static int mock_request_alloc(struct i915_request *request)
{
	struct mock_request *mock = container_of(request, typeof(*mock), base);

	INIT_LIST_HEAD(&mock->link);
	mock->delay = 0;

	return 0;
}

static int mock_emit_flush(struct i915_request *request,
			   unsigned int flags)
{
	return 0;
}

static u32 *mock_emit_breadcrumb(struct i915_request *request, u32 *cs)
{
	return cs;
}

static void mock_submit_request(struct i915_request *request)
{
	struct mock_request *mock = container_of(request, typeof(*mock), base);
	struct mock_engine *engine =
		container_of(request->engine, typeof(*engine), base);
	unsigned long flags;

	i915_request_submit(request);
	GEM_BUG_ON(!request->global_seqno);

	spin_lock_irqsave(&engine->hw_lock, flags);
	list_add_tail(&mock->link, &engine->hw_queue);
	if (mock->link.prev == &engine->hw_queue) {
		if (mock->delay)
			mod_timer(&engine->hw_delay, jiffies + mock->delay);
		else
			advance(mock);
	}
	spin_unlock_irqrestore(&engine->hw_lock, flags);
}

struct intel_engine_cs *mock_engine(struct drm_i915_private *i915,
				    const char *name,
				    int id)
{
	struct mock_engine *engine;

	GEM_BUG_ON(id >= I915_NUM_ENGINES);

	engine = kzalloc(sizeof(*engine) + PAGE_SIZE, GFP_KERNEL);
	if (!engine)
		return NULL;

	/* minimal engine setup for requests */
	engine->base.i915 = i915;
	snprintf(engine->base.name, sizeof(engine->base.name), "%s", name);
	engine->base.id = id;
	engine->base.status_page.addr = (void *)(engine + 1);

	engine->base.context_pin = mock_context_pin;
	engine->base.request_alloc = mock_request_alloc;
	engine->base.emit_flush = mock_emit_flush;
	engine->base.emit_fini_breadcrumb = mock_emit_breadcrumb;
	engine->base.submit_request = mock_submit_request;

	if (i915_timeline_init(i915,
			       &engine->base.timeline,
			       engine->base.name,
			       NULL))
		goto err_free;
	i915_timeline_set_subclass(&engine->base.timeline, TIMELINE_ENGINE);

	intel_engine_init_breadcrumbs(&engine->base);

	/* fake hw queue */
	spin_lock_init(&engine->hw_lock);
	timer_setup(&engine->hw_delay, hw_delay_complete, 0);
	INIT_LIST_HEAD(&engine->hw_queue);

	if (IS_ERR(intel_context_pin(i915->kernel_context, &engine->base)))
		goto err_breadcrumbs;

	return &engine->base;

err_breadcrumbs:
	intel_engine_fini_breadcrumbs(&engine->base);
	i915_timeline_fini(&engine->base.timeline);
err_free:
	kfree(engine);
	return NULL;
}

void mock_engine_flush(struct intel_engine_cs *engine)
{
	struct mock_engine *mock =
		container_of(engine, typeof(*mock), base);
	struct mock_request *request, *rn;

	del_timer_sync(&mock->hw_delay);

	spin_lock_irq(&mock->hw_lock);
	list_for_each_entry_safe(request, rn, &mock->hw_queue, link)
		advance(request);
	spin_unlock_irq(&mock->hw_lock);
}

void mock_engine_reset(struct intel_engine_cs *engine)
{
	intel_engine_write_global_seqno(engine, 0);
}

void mock_engine_free(struct intel_engine_cs *engine)
{
	struct mock_engine *mock =
		container_of(engine, typeof(*mock), base);
	struct intel_context *ce;

	GEM_BUG_ON(timer_pending(&mock->hw_delay));

	ce = fetch_and_zero(&engine->last_retired_context);
	if (ce)
		intel_context_unpin(ce);

	__intel_context_unpin(engine->i915->kernel_context, engine);

	intel_engine_fini_breadcrumbs(engine);
	i915_timeline_fini(&engine->timeline);

	kfree(engine);
}
