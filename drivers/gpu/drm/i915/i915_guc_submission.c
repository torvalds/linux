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
 */
#include <linux/firmware.h>
#include <linux/circ_buf.h>
#include "i915_drv.h"
#include "intel_guc.h"

/**
 * gem_allocate_guc_obj() - Allocate gem object for GuC usage
 * @dev:	drm device
 * @size:	size of object
 *
 * This is a wrapper to create a gem obj. In order to use it inside GuC, the
 * object needs to be pinned lifetime. Also we must pin it to gtt space other
 * than [0, GUC_WOPCM_TOP) because this range is reserved inside GuC.
 *
 * Return:	A drm_i915_gem_object if successful, otherwise NULL.
 */
static struct drm_i915_gem_object *gem_allocate_guc_obj(struct drm_device *dev,
							u32 size)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj;

	obj = i915_gem_alloc_object(dev, size);
	if (!obj)
		return NULL;

	if (i915_gem_object_get_pages(obj)) {
		drm_gem_object_unreference(&obj->base);
		return NULL;
	}

	if (i915_gem_obj_ggtt_pin(obj, PAGE_SIZE,
			PIN_OFFSET_BIAS | GUC_WOPCM_TOP)) {
		drm_gem_object_unreference(&obj->base);
		return NULL;
	}

	/* Invalidate GuC TLB to let GuC take the latest updates to GTT. */
	I915_WRITE(GEN8_GTCR, GEN8_GTCR_INVALIDATE);

	return obj;
}

/**
 * gem_release_guc_obj() - Release gem object allocated for GuC usage
 * @obj:	gem obj to be released
  */
static void gem_release_guc_obj(struct drm_i915_gem_object *obj)
{
	if (!obj)
		return;

	if (i915_gem_obj_is_pinned(obj))
		i915_gem_object_ggtt_unpin(obj);

	drm_gem_object_unreference(&obj->base);
}

/*
 * Set up the memory resources to be shared with the GuC.  At this point,
 * we require just one object that can be mapped through the GGTT.
 */
int i915_guc_submission_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	const size_t ctxsize = sizeof(struct guc_context_desc);
	const size_t poolsize = GUC_MAX_GPU_CONTEXTS * ctxsize;
	const size_t gemsize = round_up(poolsize, PAGE_SIZE);
	struct intel_guc *guc = &dev_priv->guc;

	if (!i915.enable_guc_submission)
		return 0; /* not enabled  */

	if (guc->ctx_pool_obj)
		return 0; /* already allocated */

	guc->ctx_pool_obj = gem_allocate_guc_obj(dev_priv->dev, gemsize);
	if (!guc->ctx_pool_obj)
		return -ENOMEM;

	ida_init(&guc->ctx_ids);

	return 0;
}

void i915_guc_submission_fini(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_guc *guc = &dev_priv->guc;

	if (guc->ctx_pool_obj)
		ida_destroy(&guc->ctx_ids);
	gem_release_guc_obj(guc->ctx_pool_obj);
	guc->ctx_pool_obj = NULL;
}
