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

static struct mock_request *first_request(struct mock_engine *engine)
{
	return list_first_entry_or_null(&engine->hw_queue,
					struct mock_request,
					link);
}

static void hw_delay_complete(unsigned long data)
{
	struct mock_engine *engine = (typeof(engine))data;
	struct mock_request *request;

	spin_lock(&engine->hw_lock);

	request = first_request(engine);
	if (request) {
		list_del_init(&request->link);
		mock_seqno_advance(&engine->base, request->base.global_seqno);
	}

	request = first_request(engine);
	if (request)
		mod_timer(&engine->hw_delay, jiffies + request->delay);

	spin_unlock(&engine->hw_lock);
}

static int mock_context_pin(struct intel_engine_cs *engine,
			    struct i915_gem_context *ctx)
{
	i915_gem_context_get(ctx);
	return 0;
}

static void mock_context_unpin(struct intel_engine_cs *engine,
			       struct i915_gem_context *ctx)
{
	i915_gem_context_put(ctx);
}

static int mock_request_alloc(struct drm_i915_gem_request *request)
{
	struct mock_request *mock = container_of(request, typeof(*mock), base);

	INIT_LIST_HEAD(&mock->link);
	mock->delay = 0;

	request->ring = request->engine->buffer;
	return 0;
}

static int mock_emit_flush(struct drm_i915_gem_request *request,
			   unsigned int flags)
{
	return 0;
}

static void mock_emit_breadcrumb(struct drm_i915_gem_request *request,
				 u32 *flags)
{
}

static void mock_submit_request(struct drm_i915_gem_request *request)
{
	struct mock_request *mock = container_of(request, typeof(*mock), base);
	struct mock_engine *engine =
		container_of(request->engine, typeof(*engine), base);

	i915_gem_request_submit(request);
	GEM_BUG_ON(!request->global_seqno);

	spin_lock_irq(&engine->hw_lock);
	list_add_tail(&mock->link, &engine->hw_queue);
	if (mock->link.prev == &engine->hw_queue)
		mod_timer(&engine->hw_delay, jiffies + mock->delay);
	spin_unlock_irq(&engine->hw_lock);
}

static struct intel_ring *mock_ring(struct intel_engine_cs *engine)
{
	const unsigned long sz = roundup_pow_of_two(sizeof(struct intel_ring));
	struct intel_ring *ring;

	ring = kzalloc(sizeof(*ring) + sz, GFP_KERNEL);
	if (!ring)
		return NULL;

	ring->engine = engine;
	ring->size = sz;
	ring->effective_size = sz;
	ring->vaddr = (void *)(ring + 1);

	INIT_LIST_HEAD(&ring->request_list);
	intel_ring_update_space(ring);

	return ring;
}

struct intel_engine_cs *mock_engine(struct drm_i915_private *i915,
				    const char *name)
{
	struct mock_engine *engine;
	static int id;

	engine = kzalloc(sizeof(*engine) + PAGE_SIZE, GFP_KERNEL);
	if (!engine)
		return NULL;

	engine->base.buffer = mock_ring(&engine->base);
	if (!engine->base.buffer) {
		kfree(engine);
		return NULL;
	}

	/* minimal engine setup for requests */
	engine->base.i915 = i915;
	engine->base.name = name;
	engine->base.id = id++;
	engine->base.status_page.page_addr = (void *)(engine + 1);

	engine->base.context_pin = mock_context_pin;
	engine->base.context_unpin = mock_context_unpin;
	engine->base.request_alloc = mock_request_alloc;
	engine->base.emit_flush = mock_emit_flush;
	engine->base.emit_breadcrumb = mock_emit_breadcrumb;
	engine->base.submit_request = mock_submit_request;

	engine->base.timeline =
		&i915->gt.global_timeline.engine[engine->base.id];

	intel_engine_init_breadcrumbs(&engine->base);
	engine->base.breadcrumbs.mock = true; /* prevent touching HW for irqs */

	/* fake hw queue */
	spin_lock_init(&engine->hw_lock);
	setup_timer(&engine->hw_delay,
		    hw_delay_complete,
		    (unsigned long)engine);
	INIT_LIST_HEAD(&engine->hw_queue);

	return &engine->base;
}

void mock_engine_flush(struct intel_engine_cs *engine)
{
	struct mock_engine *mock =
		container_of(engine, typeof(*mock), base);
	struct mock_request *request, *rn;

	del_timer_sync(&mock->hw_delay);

	spin_lock_irq(&mock->hw_lock);
	list_for_each_entry_safe(request, rn, &mock->hw_queue, link) {
		list_del_init(&request->link);
		mock_seqno_advance(&mock->base, request->base.global_seqno);
	}
	spin_unlock_irq(&mock->hw_lock);
}

void mock_engine_reset(struct intel_engine_cs *engine)
{
	intel_write_status_page(engine, I915_GEM_HWS_INDEX, 0);
}

void mock_engine_free(struct intel_engine_cs *engine)
{
	struct mock_engine *mock =
		container_of(engine, typeof(*mock), base);

	GEM_BUG_ON(timer_pending(&mock->hw_delay));

	if (engine->last_retired_context)
		engine->context_unpin(engine, engine->last_retired_context);

	intel_engine_fini_breadcrumbs(engine);

	kfree(engine->buffer);
	kfree(engine);
}
