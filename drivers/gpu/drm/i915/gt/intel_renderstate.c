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
 *    Mika Kuoppala <mika.kuoppala@intel.com>
 *
 */

#include "i915_drv.h"
#include "intel_renderstate.h"
#include "intel_context.h"
#include "intel_gpu_commands.h"
#include "intel_ring.h"

static const struct intel_renderstate_rodata *
render_state_get_rodata(const struct intel_engine_cs *engine)
{
	if (engine->class != RENDER_CLASS)
		return NULL;

	switch (INTEL_GEN(engine->i915)) {
	case 6:
		return &gen6_null_state;
	case 7:
		return &gen7_null_state;
	case 8:
		return &gen8_null_state;
	case 9:
		return &gen9_null_state;
	}

	return NULL;
}

/*
 * Macro to add commands to auxiliary batch.
 * This macro only checks for page overflow before inserting the commands,
 * this is sufficient as the null state generator makes the final batch
 * with two passes to build command and state separately. At this point
 * the size of both are known and it compacts them by relocating the state
 * right after the commands taking care of alignment so we should sufficient
 * space below them for adding new commands.
 */
#define OUT_BATCH(batch, i, val)				\
	do {							\
		if ((i) >= PAGE_SIZE / sizeof(u32))		\
			goto out;				\
		(batch)[(i)++] = (val);				\
	} while(0)

static int render_state_setup(struct intel_renderstate *so,
			      struct drm_i915_private *i915)
{
	const struct intel_renderstate_rodata *rodata = so->rodata;
	unsigned int i = 0, reloc_index = 0;
	int ret = -EINVAL;
	u32 *d;

	d = i915_gem_object_pin_map(so->vma->obj, I915_MAP_WB);
	if (IS_ERR(d))
		return PTR_ERR(d);

	while (i < rodata->batch_items) {
		u32 s = rodata->batch[i];

		if (i * 4  == rodata->reloc[reloc_index]) {
			u64 r = s + so->vma->node.start;
			s = lower_32_bits(r);
			if (HAS_64BIT_RELOC(i915)) {
				if (i + 1 >= rodata->batch_items ||
				    rodata->batch[i + 1] != 0)
					goto out;

				d[i++] = s;
				s = upper_32_bits(r);
			}

			reloc_index++;
		}

		d[i++] = s;
	}

	if (rodata->reloc[reloc_index] != -1) {
		drm_err(&i915->drm, "only %d relocs resolved\n", reloc_index);
		goto out;
	}

	so->batch_offset = i915_ggtt_offset(so->vma);
	so->batch_size = rodata->batch_items * sizeof(u32);

	while (i % CACHELINE_DWORDS)
		OUT_BATCH(d, i, MI_NOOP);

	so->aux_offset = i * sizeof(u32);

	if (HAS_POOLED_EU(i915)) {
		/*
		 * We always program 3x6 pool config but depending upon which
		 * subslice is disabled HW drops down to appropriate config
		 * shown below.
		 *
		 * In the below table 2x6 config always refers to
		 * fused-down version, native 2x6 is not available and can
		 * be ignored
		 *
		 * SNo  subslices config                eu pool configuration
		 * -----------------------------------------------------------
		 * 1    3 subslices enabled (3x6)  -    0x00777000  (9+9)
		 * 2    ss0 disabled (2x6)         -    0x00777000  (3+9)
		 * 3    ss1 disabled (2x6)         -    0x00770000  (6+6)
		 * 4    ss2 disabled (2x6)         -    0x00007000  (9+3)
		 */
		u32 eu_pool_config = 0x00777000;

		OUT_BATCH(d, i, GEN9_MEDIA_POOL_STATE);
		OUT_BATCH(d, i, GEN9_MEDIA_POOL_ENABLE);
		OUT_BATCH(d, i, eu_pool_config);
		OUT_BATCH(d, i, 0);
		OUT_BATCH(d, i, 0);
		OUT_BATCH(d, i, 0);
	}

	OUT_BATCH(d, i, MI_BATCH_BUFFER_END);
	so->aux_size = i * sizeof(u32) - so->aux_offset;
	so->aux_offset += so->batch_offset;
	/*
	 * Since we are sending length, we need to strictly conform to
	 * all requirements. For Gen2 this must be a multiple of 8.
	 */
	so->aux_size = ALIGN(so->aux_size, 8);

	ret = 0;
out:
	__i915_gem_object_flush_map(so->vma->obj, 0, i * sizeof(u32));
	__i915_gem_object_release_map(so->vma->obj);
	return ret;
}

#undef OUT_BATCH

int intel_renderstate_init(struct intel_renderstate *so,
			   struct intel_context *ce)
{
	struct intel_engine_cs *engine = ce->engine;
	struct drm_i915_gem_object *obj = NULL;
	int err;

	memset(so, 0, sizeof(*so));

	so->rodata = render_state_get_rodata(engine);
	if (so->rodata) {
		if (so->rodata->batch_items * 4 > PAGE_SIZE)
			return -EINVAL;

		obj = i915_gem_object_create_internal(engine->i915, PAGE_SIZE);
		if (IS_ERR(obj))
			return PTR_ERR(obj);

		so->vma = i915_vma_instance(obj, &engine->gt->ggtt->vm, NULL);
		if (IS_ERR(so->vma)) {
			err = PTR_ERR(so->vma);
			goto err_obj;
		}
	}

	i915_gem_ww_ctx_init(&so->ww, true);
retry:
	err = intel_context_pin_ww(ce, &so->ww);
	if (err)
		goto err_fini;

	/* return early if there's nothing to setup */
	if (!err && !so->rodata)
		return 0;

	err = i915_gem_object_lock(so->vma->obj, &so->ww);
	if (err)
		goto err_context;

	err = i915_vma_pin(so->vma, 0, 0, PIN_GLOBAL | PIN_HIGH);
	if (err)
		goto err_context;

	err = render_state_setup(so, engine->i915);
	if (err)
		goto err_unpin;

	return 0;

err_unpin:
	i915_vma_unpin(so->vma);
err_context:
	intel_context_unpin(ce);
err_fini:
	if (err == -EDEADLK) {
		err = i915_gem_ww_ctx_backoff(&so->ww);
		if (!err)
			goto retry;
	}
	i915_gem_ww_ctx_fini(&so->ww);
err_obj:
	if (obj)
		i915_gem_object_put(obj);
	so->vma = NULL;
	return err;
}

int intel_renderstate_emit(struct intel_renderstate *so,
			   struct i915_request *rq)
{
	struct intel_engine_cs *engine = rq->engine;
	int err;

	if (!so->vma)
		return 0;

	err = i915_request_await_object(rq, so->vma->obj, false);
	if (err == 0)
		err = i915_vma_move_to_active(so->vma, rq, 0);
	if (err)
		return err;

	err = engine->emit_bb_start(rq,
				    so->batch_offset, so->batch_size,
				    I915_DISPATCH_SECURE);
	if (err)
		return err;

	if (so->aux_size > 8) {
		err = engine->emit_bb_start(rq,
					    so->aux_offset, so->aux_size,
					    I915_DISPATCH_SECURE);
		if (err)
			return err;
	}

	return 0;
}

void intel_renderstate_fini(struct intel_renderstate *so,
			    struct intel_context *ce)
{
	if (so->vma) {
		i915_vma_unpin(so->vma);
		i915_vma_close(so->vma);
	}

	intel_context_unpin(ce);
	i915_gem_ww_ctx_fini(&so->ww);

	if (so->vma)
		i915_gem_object_put(so->vma->obj);
}
