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

#include "gem/i915_gem_context.h"

#include "i915_drv.h"
#include "intel_context.h"
#include "intel_engine_pm.h"

#include "mock_engine.h"
#include "selftests/mock_request.h"

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

	if (i915_timeline_init(engine->i915, &ring->timeline, NULL)) {
		kfree(ring);
		return NULL;
	}

	kref_init(&ring->base.ref);
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

static struct i915_request *first_request(struct mock_engine *engine)
{
	return list_first_entry_or_null(&engine->hw_queue,
					struct i915_request,
					mock.link);
}

static void advance(struct i915_request *request)
{
	list_del_init(&request->mock.link);
	i915_request_mark_complete(request);
	GEM_BUG_ON(!i915_request_completed(request));

	intel_engine_queue_breadcrumbs(request->engine);
}

static void hw_delay_complete(struct timer_list *t)
{
	struct mock_engine *engine = from_timer(engine, t, hw_delay);
	struct i915_request *request;
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
		if (request->mock.delay) {
			mod_timer(&engine->hw_delay,
				  jiffies + request->mock.delay);
			break;
		}

		advance(request);
	}

	spin_unlock_irqrestore(&engine->hw_lock, flags);
}

static void mock_context_unpin(struct intel_context *ce)
{
	mock_timeline_unpin(ce->ring->timeline);
}

static void mock_context_destroy(struct kref *ref)
{
	struct intel_context *ce = container_of(ref, typeof(*ce), ref);

	GEM_BUG_ON(intel_context_is_pinned(ce));

	if (ce->ring)
		mock_ring_free(ce->ring);

	intel_context_free(ce);
}

static int mock_context_pin(struct intel_context *ce)
{
	int ret;

	if (!ce->ring) {
		ce->ring = mock_ring(ce->engine);
		if (!ce->ring)
			return -ENOMEM;
	}

	ret = intel_context_active_acquire(ce, PIN_HIGH);
	if (ret)
		return ret;

	mock_timeline_pin(ce->ring->timeline);
	return 0;
}

static const struct intel_context_ops mock_context_ops = {
	.pin = mock_context_pin,
	.unpin = mock_context_unpin,

	.enter = intel_context_enter_engine,
	.exit = intel_context_exit_engine,

	.destroy = mock_context_destroy,
};

static int mock_request_alloc(struct i915_request *request)
{
	INIT_LIST_HEAD(&request->mock.link);
	request->mock.delay = 0;

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
	struct mock_engine *engine =
		container_of(request->engine, typeof(*engine), base);
	unsigned long flags;

	i915_request_submit(request);

	spin_lock_irqsave(&engine->hw_lock, flags);
	list_add_tail(&request->mock.link, &engine->hw_queue);
	if (list_is_first(&request->mock.link, &engine->hw_queue)) {
		if (request->mock.delay)
			mod_timer(&engine->hw_delay,
				  jiffies + request->mock.delay);
		else
			advance(request);
	}
	spin_unlock_irqrestore(&engine->hw_lock, flags);
}

static void mock_reset_prepare(struct intel_engine_cs *engine)
{
}

static void mock_reset(struct intel_engine_cs *engine, bool stalled)
{
	GEM_BUG_ON(stalled);
}

static void mock_reset_finish(struct intel_engine_cs *engine)
{
}

static void mock_cancel_requests(struct intel_engine_cs *engine)
{
	struct i915_request *request;
	unsigned long flags;

	spin_lock_irqsave(&engine->timeline.lock, flags);

	/* Mark all submitted requests as skipped. */
	list_for_each_entry(request, &engine->timeline.requests, sched.link) {
		if (!i915_request_signaled(request))
			dma_fence_set_error(&request->fence, -EIO);

		i915_request_mark_complete(request);
	}

	spin_unlock_irqrestore(&engine->timeline.lock, flags);
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
	engine->base.mask = BIT(id);
	engine->base.status_page.addr = (void *)(engine + 1);

	engine->base.cops = &mock_context_ops;
	engine->base.request_alloc = mock_request_alloc;
	engine->base.emit_flush = mock_emit_flush;
	engine->base.emit_fini_breadcrumb = mock_emit_breadcrumb;
	engine->base.submit_request = mock_submit_request;

	engine->base.reset.prepare = mock_reset_prepare;
	engine->base.reset.reset = mock_reset;
	engine->base.reset.finish = mock_reset_finish;
	engine->base.cancel_requests = mock_cancel_requests;

	/* fake hw queue */
	spin_lock_init(&engine->hw_lock);
	timer_setup(&engine->hw_delay, hw_delay_complete, 0);
	INIT_LIST_HEAD(&engine->hw_queue);

	return &engine->base;
}

int mock_engine_init(struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;
	int err;

	intel_engine_init_breadcrumbs(engine);
	intel_engine_init_execlists(engine);
	intel_engine_init__pm(engine);

	if (i915_timeline_init(i915, &engine->timeline, NULL))
		goto err_breadcrumbs;
	i915_timeline_set_subclass(&engine->timeline, TIMELINE_ENGINE);

	engine->kernel_context =
		i915_gem_context_get_engine(i915->kernel_context, engine->id);
	if (IS_ERR(engine->kernel_context))
		goto err_timeline;

	err = intel_context_pin(engine->kernel_context);
	intel_context_put(engine->kernel_context);
	if (err)
		goto err_timeline;

	return 0;

err_timeline:
	i915_timeline_fini(&engine->timeline);
err_breadcrumbs:
	intel_engine_fini_breadcrumbs(engine);
	return -ENOMEM;
}

void mock_engine_flush(struct intel_engine_cs *engine)
{
	struct mock_engine *mock =
		container_of(engine, typeof(*mock), base);
	struct i915_request *request, *rn;

	del_timer_sync(&mock->hw_delay);

	spin_lock_irq(&mock->hw_lock);
	list_for_each_entry_safe(request, rn, &mock->hw_queue, mock.link)
		advance(request);
	spin_unlock_irq(&mock->hw_lock);
}

void mock_engine_reset(struct intel_engine_cs *engine)
{
}

void mock_engine_free(struct intel_engine_cs *engine)
{
	struct mock_engine *mock =
		container_of(engine, typeof(*mock), base);

	GEM_BUG_ON(timer_pending(&mock->hw_delay));

	intel_context_unpin(engine->kernel_context);

	intel_engine_fini_breadcrumbs(engine);
	i915_timeline_fini(&engine->timeline);

	kfree(engine);
}
