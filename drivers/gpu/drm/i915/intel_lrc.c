/*
 * Copyright © 2014 Intel Corporation
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
 * Authors:
 *    Ben Widawsky <ben@bwidawsk.net>
 *    Michel Thierry <michel.thierry@intel.com>
 *    Thomas Daniel <thomas.daniel@intel.com>
 *    Oscar Mateo <oscar.mateo@intel.com>
 *
 */

/*
 * GEN8 brings an expansion of the HW contexts: "Logical Ring Contexts".
 * These expanded contexts enable a number of new abilities, especially
 * "Execlists" (also implemented in this file).
 *
 * Execlists are the new method by which, on gen8+ hardware, workloads are
 * submitted for execution (as opposed to the legacy, ringbuffer-based, method).
 */

#include <drm/drmP.h>
#include <drm/i915_drm.h>
#include "i915_drv.h"

#define GEN8_LR_CONTEXT_RENDER_SIZE (20 * PAGE_SIZE)
#define GEN8_LR_CONTEXT_OTHER_SIZE (2 * PAGE_SIZE)

#define GEN8_LR_CONTEXT_ALIGN 4096

int intel_sanitize_enable_execlists(struct drm_device *dev, int enable_execlists)
{
	WARN_ON(i915.enable_ppgtt == -1);

	if (enable_execlists == 0)
		return 0;

	if (HAS_LOGICAL_RING_CONTEXTS(dev) && USES_PPGTT(dev))
		return 1;

	return 0;
}

void intel_lr_context_free(struct intel_context *ctx)
{
	int i;

	for (i = 0; i < I915_NUM_RINGS; i++) {
		struct drm_i915_gem_object *ctx_obj = ctx->engine[i].state;
		struct intel_ringbuffer *ringbuf = ctx->engine[i].ringbuf;

		if (ctx_obj) {
			intel_destroy_ringbuffer_obj(ringbuf);
			kfree(ringbuf);
			i915_gem_object_ggtt_unpin(ctx_obj);
			drm_gem_object_unreference(&ctx_obj->base);
		}
	}
}

static uint32_t get_lr_context_size(struct intel_engine_cs *ring)
{
	int ret = 0;

	WARN_ON(INTEL_INFO(ring->dev)->gen != 8);

	switch (ring->id) {
	case RCS:
		ret = GEN8_LR_CONTEXT_RENDER_SIZE;
		break;
	case VCS:
	case BCS:
	case VECS:
	case VCS2:
		ret = GEN8_LR_CONTEXT_OTHER_SIZE;
		break;
	}

	return ret;
}

int intel_lr_context_deferred_create(struct intel_context *ctx,
				     struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_gem_object *ctx_obj;
	uint32_t context_size;
	struct intel_ringbuffer *ringbuf;
	int ret;

	WARN_ON(ctx->legacy_hw_ctx.rcs_state != NULL);

	context_size = round_up(get_lr_context_size(ring), 4096);

	ctx_obj = i915_gem_alloc_context_obj(dev, context_size);
	if (IS_ERR(ctx_obj)) {
		ret = PTR_ERR(ctx_obj);
		DRM_DEBUG_DRIVER("Alloc LRC backing obj failed: %d\n", ret);
		return ret;
	}

	ret = i915_gem_obj_ggtt_pin(ctx_obj, GEN8_LR_CONTEXT_ALIGN, 0);
	if (ret) {
		DRM_DEBUG_DRIVER("Pin LRC backing obj failed: %d\n", ret);
		drm_gem_object_unreference(&ctx_obj->base);
		return ret;
	}

	ringbuf = kzalloc(sizeof(*ringbuf), GFP_KERNEL);
	if (!ringbuf) {
		DRM_DEBUG_DRIVER("Failed to allocate ringbuffer %s\n",
				ring->name);
		i915_gem_object_ggtt_unpin(ctx_obj);
		drm_gem_object_unreference(&ctx_obj->base);
		ret = -ENOMEM;
		return ret;
	}

	ringbuf->ring = ring;
	ringbuf->size = 32 * PAGE_SIZE;
	ringbuf->effective_size = ringbuf->size;
	ringbuf->head = 0;
	ringbuf->tail = 0;
	ringbuf->space = ringbuf->size;
	ringbuf->last_retired_head = -1;

	/* TODO: For now we put this in the mappable region so that we can reuse
	 * the existing ringbuffer code which ioremaps it. When we start
	 * creating many contexts, this will no longer work and we must switch
	 * to a kmapish interface.
	 */
	ret = intel_alloc_ringbuffer_obj(dev, ringbuf);
	if (ret) {
		DRM_DEBUG_DRIVER("Failed to allocate ringbuffer obj %s: %d\n",
				ring->name, ret);
		kfree(ringbuf);
		i915_gem_object_ggtt_unpin(ctx_obj);
		drm_gem_object_unreference(&ctx_obj->base);
		return ret;
	}

	ctx->engine[ring->id].ringbuf = ringbuf;
	ctx->engine[ring->id].state = ctx_obj;

	return 0;
}
