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
	enum intel_engine_hw_id hw_id;
	u32 mmio_base;
	unsigned irq_shift;
	int (*init_legacy)(struct intel_engine_cs *engine);
	int (*init_execlists)(struct intel_engine_cs *engine);
} intel_engines[] = {
	[RCS] = {
		.name = "render ring",
		.exec_id = I915_EXEC_RENDER,
		.hw_id = RCS_HW,
		.mmio_base = RENDER_RING_BASE,
		.irq_shift = GEN8_RCS_IRQ_SHIFT,
		.init_execlists = logical_render_ring_init,
		.init_legacy = intel_init_render_ring_buffer,
	},
	[BCS] = {
		.name = "blitter ring",
		.exec_id = I915_EXEC_BLT,
		.hw_id = BCS_HW,
		.mmio_base = BLT_RING_BASE,
		.irq_shift = GEN8_BCS_IRQ_SHIFT,
		.init_execlists = logical_xcs_ring_init,
		.init_legacy = intel_init_blt_ring_buffer,
	},
	[VCS] = {
		.name = "bsd ring",
		.exec_id = I915_EXEC_BSD,
		.hw_id = VCS_HW,
		.mmio_base = GEN6_BSD_RING_BASE,
		.irq_shift = GEN8_VCS1_IRQ_SHIFT,
		.init_execlists = logical_xcs_ring_init,
		.init_legacy = intel_init_bsd_ring_buffer,
	},
	[VCS2] = {
		.name = "bsd2 ring",
		.exec_id = I915_EXEC_BSD,
		.hw_id = VCS2_HW,
		.mmio_base = GEN8_BSD2_RING_BASE,
		.irq_shift = GEN8_VCS2_IRQ_SHIFT,
		.init_execlists = logical_xcs_ring_init,
		.init_legacy = intel_init_bsd2_ring_buffer,
	},
	[VECS] = {
		.name = "video enhancement ring",
		.exec_id = I915_EXEC_VEBOX,
		.hw_id = VECS_HW,
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
	engine->hw_id = engine->guc_id = info->hw_id;
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
	struct intel_device_info *device_info = mkwrite_device_info(dev_priv);
	unsigned int mask = 0;
	int (*init)(struct intel_engine_cs *engine);
	unsigned int i;
	int ret;

	WARN_ON(INTEL_INFO(dev_priv)->ring_mask == 0);
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
	if (WARN_ON(mask != INTEL_INFO(dev_priv)->ring_mask))
		device_info->ring_mask = mask;

	device_info->num_rings = hweight32(mask);

	return 0;

cleanup:
	for (i = 0; i < I915_NUM_ENGINES; i++) {
		if (i915.enable_execlists)
			intel_logical_ring_cleanup(&dev_priv->engine[i]);
		else
			intel_engine_cleanup(&dev_priv->engine[i]);
	}

	return ret;
}

void intel_engine_init_seqno(struct intel_engine_cs *engine, u32 seqno)
{
	struct drm_i915_private *dev_priv = engine->i915;

	/* Our semaphore implementation is strictly monotonic (i.e. we proceed
	 * so long as the semaphore value in the register/page is greater
	 * than the sync value), so whenever we reset the seqno,
	 * so long as we reset the tracking semaphore value to 0, it will
	 * always be before the next request's seqno. If we don't reset
	 * the semaphore value, then when the seqno moves backwards all
	 * future waits will complete instantly (causing rendering corruption).
	 */
	if (IS_GEN6(dev_priv) || IS_GEN7(dev_priv)) {
		I915_WRITE(RING_SYNC_0(engine->mmio_base), 0);
		I915_WRITE(RING_SYNC_1(engine->mmio_base), 0);
		if (HAS_VEBOX(dev_priv))
			I915_WRITE(RING_SYNC_2(engine->mmio_base), 0);
	}
	if (dev_priv->semaphore) {
		struct page *page = i915_vma_first_page(dev_priv->semaphore);
		void *semaphores;

		/* Semaphores are in noncoherent memory, flush to be safe */
		semaphores = kmap(page);
		memset(semaphores + GEN8_SEMAPHORE_OFFSET(engine->id, 0),
		       0, I915_NUM_ENGINES * gen8_semaphore_seqno_size);
		drm_clflush_virt_range(semaphores + GEN8_SEMAPHORE_OFFSET(engine->id, 0),
				       I915_NUM_ENGINES * gen8_semaphore_seqno_size);
		kunmap(page);
	}
	memset(engine->semaphore.sync_seqno, 0,
	       sizeof(engine->semaphore.sync_seqno));

	intel_write_status_page(engine, I915_GEM_HWS_INDEX, seqno);
	if (engine->irq_seqno_barrier)
		engine->irq_seqno_barrier(engine);
	engine->last_submitted_seqno = seqno;

	engine->hangcheck.seqno = seqno;

	/* After manually advancing the seqno, fake the interrupt in case
	 * there are any waiters for that seqno.
	 */
	intel_engine_wakeup(engine);
}

void intel_engine_init_hangcheck(struct intel_engine_cs *engine)
{
	memset(&engine->hangcheck, 0, sizeof(engine->hangcheck));
}

static void intel_engine_init_requests(struct intel_engine_cs *engine)
{
	init_request_active(&engine->last_request, NULL);
	INIT_LIST_HEAD(&engine->request_list);
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
	INIT_LIST_HEAD(&engine->execlist_queue);
	spin_lock_init(&engine->execlist_lock);

	engine->fence_context = fence_context_alloc(1);

	intel_engine_init_requests(engine);
	intel_engine_init_hangcheck(engine);
	i915_gem_batch_pool_init(engine, &engine->batch_pool);

	intel_engine_init_cmd_parser(engine);
}

int intel_engine_create_scratch(struct intel_engine_cs *engine, int size)
{
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	int ret;

	WARN_ON(engine->scratch);

	obj = i915_gem_object_create_stolen(&engine->i915->drm, size);
	if (!obj)
		obj = i915_gem_object_create(&engine->i915->drm, size);
	if (IS_ERR(obj)) {
		DRM_ERROR("Failed to allocate scratch page\n");
		return PTR_ERR(obj);
	}

	vma = i915_vma_create(obj, &engine->i915->ggtt.base, NULL);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto err_unref;
	}

	ret = i915_vma_pin(vma, 0, 4096, PIN_GLOBAL | PIN_HIGH);
	if (ret)
		goto err_unref;

	engine->scratch = vma;
	DRM_DEBUG_DRIVER("%s pipe control offset: 0x%08x\n",
			 engine->name, i915_ggtt_offset(vma));
	return 0;

err_unref:
	i915_gem_object_put(obj);
	return ret;
}

static void intel_engine_cleanup_scratch(struct intel_engine_cs *engine)
{
	i915_vma_unpin_and_release(&engine->scratch);
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

	return 0;
}

/**
 * intel_engines_cleanup_common - cleans up the engine state created by
 *                                the common initiailizers.
 * @engine: Engine to cleanup.
 *
 * This cleans up everything created by the common helpers.
 */
void intel_engine_cleanup_common(struct intel_engine_cs *engine)
{
	intel_engine_cleanup_scratch(engine);

	intel_engine_fini_breadcrumbs(engine);
	intel_engine_cleanup_cmd_parser(engine);
	i915_gem_batch_pool_fini(&engine->batch_pool);
}
