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

#include "i915_drv.h"
#include "intel_ringbuffer.h"
#include "intel_lrc.h"

static const struct engine_info {
	const char *name;
	unsigned exec_id;
	unsigned guc_id;
	u32 mmio_base;
	unsigned irq_shift;
	int (*init_legacy)(struct intel_engine_cs *engine);
	int (*init_execlists)(struct intel_engine_cs *engine);
} intel_engines[] = {
	[RCS] = {
		.name = "render ring",
		.exec_id = I915_EXEC_RENDER,
		.guc_id = GUC_RENDER_ENGINE,
		.mmio_base = RENDER_RING_BASE,
		.irq_shift = GEN8_RCS_IRQ_SHIFT,
		.init_execlists = logical_render_ring_init,
		.init_legacy = intel_init_render_ring_buffer,
	},
	[BCS] = {
		.name = "blitter ring",
		.exec_id = I915_EXEC_BLT,
		.guc_id = GUC_BLITTER_ENGINE,
		.mmio_base = BLT_RING_BASE,
		.irq_shift = GEN8_BCS_IRQ_SHIFT,
		.init_execlists = logical_xcs_ring_init,
		.init_legacy = intel_init_blt_ring_buffer,
	},
	[VCS] = {
		.name = "bsd ring",
		.exec_id = I915_EXEC_BSD,
		.guc_id = GUC_VIDEO_ENGINE,
		.mmio_base = GEN6_BSD_RING_BASE,
		.irq_shift = GEN8_VCS1_IRQ_SHIFT,
		.init_execlists = logical_xcs_ring_init,
		.init_legacy = intel_init_bsd_ring_buffer,
	},
	[VCS2] = {
		.name = "bsd2 ring",
		.exec_id = I915_EXEC_BSD,
		.guc_id = GUC_VIDEO_ENGINE2,
		.mmio_base = GEN8_BSD2_RING_BASE,
		.irq_shift = GEN8_VCS2_IRQ_SHIFT,
		.init_execlists = logical_xcs_ring_init,
		.init_legacy = intel_init_bsd2_ring_buffer,
	},
	[VECS] = {
		.name = "video enhancement ring",
		.exec_id = I915_EXEC_VEBOX,
		.guc_id = GUC_VIDEOENHANCE_ENGINE,
		.mmio_base = VEBOX_RING_BASE,
		.irq_shift = GEN8_VECS_IRQ_SHIFT,
		.init_execlists = logical_xcs_ring_init,
		.init_legacy = intel_init_vebox_ring_buffer,
	},
};

static struct intel_engine_cs *
intel_engine_setup(struct drm_i915_private *dev_priv,
		   enum intel_engine_id id)
{
	const struct engine_info *info = &intel_engines[id];
	struct intel_engine_cs *engine = &dev_priv->engine[id];

	engine->id = id;
	engine->i915 = dev_priv;
	engine->name = info->name;
	engine->exec_id = info->exec_id;
	engine->hw_id = engine->guc_id = info->guc_id;
	engine->mmio_base = info->mmio_base;
	engine->irq_shift = info->irq_shift;

	return engine;
}

/**
 * intel_engines_init() - allocate, populate and init the Engine Command Streamers
 * @dev: DRM device.
 *
 * Return: non-zero if the initialization failed.
 */
int intel_engines_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	unsigned int mask = 0;
	int (*init)(struct intel_engine_cs *engine);
	unsigned int i;
	int ret;

	WARN_ON(INTEL_INFO(dev_priv)->ring_mask &
		GENMASK(sizeof(mask) * BITS_PER_BYTE - 1, I915_NUM_ENGINES));

	for (i = 0; i < ARRAY_SIZE(intel_engines); i++) {
		if (!HAS_ENGINE(dev_priv, i))
			continue;

		if (i915.enable_execlists)
			init = intel_engines[i].init_execlists;
		else
			init = intel_engines[i].init_legacy;

		if (!init)
			continue;

		ret = init(intel_engine_setup(dev_priv, i));
		if (ret)
			goto cleanup;

		mask |= ENGINE_MASK(i);
	}

	/*
	 * Catch failures to update intel_engines table when the new engines
	 * are added to the driver by a warning and disabling the forgotten
	 * engines.
	 */
	if (WARN_ON(mask != INTEL_INFO(dev_priv)->ring_mask)) {
		struct intel_device_info *info =
			(struct intel_device_info *)&dev_priv->info;
		info->ring_mask = mask;
	}

	return 0;

cleanup:
	for (i = 0; i < I915_NUM_ENGINES; i++) {
		if (i915.enable_execlists)
			intel_logical_ring_cleanup(&dev_priv->engine[i]);
		else
			intel_cleanup_engine(&dev_priv->engine[i]);
	}

	return ret;
}

void intel_engine_init_hangcheck(struct intel_engine_cs *engine)
{
	memset(&engine->hangcheck, 0, sizeof(engine->hangcheck));
}

/**
 * intel_engines_setup_common - setup engine state not requiring hw access
 * @engine: Engine to setup.
 *
 * Initializes @engine@ structure members shared between legacy and execlists
 * submission modes which do not require hardware access.
 *
 * Typically done early in the submission mode specific engine setup stage.
 */
void intel_engine_setup_common(struct intel_engine_cs *engine)
{
	INIT_LIST_HEAD(&engine->active_list);
	INIT_LIST_HEAD(&engine->request_list);
	INIT_LIST_HEAD(&engine->buffers);
	INIT_LIST_HEAD(&engine->execlist_queue);
	spin_lock_init(&engine->execlist_lock);

	engine->fence_context = fence_context_alloc(1);

	intel_engine_init_hangcheck(engine);
	i915_gem_batch_pool_init(&engine->i915->drm, &engine->batch_pool);
}

/**
 * intel_engines_init_common - initialize cengine state which might require hw access
 * @engine: Engine to initialize.
 *
 * Initializes @engine@ structure members shared between legacy and execlists
 * submission modes which do require hardware access.
 *
 * Typcally done at later stages of submission mode specific engine setup.
 *
 * Returns zero on success or an error code on failure.
 */
int intel_engine_init_common(struct intel_engine_cs *engine)
{
	int ret;

	ret = intel_engine_init_breadcrumbs(engine);
	if (ret)
		return ret;

	return i915_cmd_parser_init_ring(engine);
}
