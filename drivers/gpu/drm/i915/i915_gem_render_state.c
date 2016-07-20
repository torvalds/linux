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

static const struct intel_renderstate_rodata *
render_state_get_rodata(const int gen)
{
	switch (gen) {
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

static int render_state_init(struct render_state *so,
			     struct drm_i915_private *dev_priv)
{
	int ret;

	so->gen = INTEL_GEN(dev_priv);
	so->rodata = render_state_get_rodata(so->gen);
	if (so->rodata == NULL)
		return 0;

	if (so->rodata->batch_items * 4 > 4096)
		return -EINVAL;

	so->obj = i915_gem_object_create(&dev_priv->drm, 4096);
	if (IS_ERR(so->obj))
		return PTR_ERR(so->obj);

	ret = i915_gem_obj_ggtt_pin(so->obj, 4096, 0);
	if (ret)
		goto free_gem;

	so->ggtt_offset = i915_gem_obj_ggtt_offset(so->obj);
	return 0;

free_gem:
	i915_gem_object_put(so->obj);
	return ret;
}

/*
 * Macro to add commands to auxiliary batch.
 * This macro only checks for page overflow before inserting the commands,
 * this is sufficient as the null state generator makes the final batch
 * with two passes to build command and state separately. At this point
 * the size of both are known and it compacts them by relocating the state
 * right after the commands taking care of aligment so we should sufficient
 * space below them for adding new commands.
 */
#define OUT_BATCH(batch, i, val)				\
	do {							\
		if (WARN_ON((i) >= PAGE_SIZE / sizeof(u32))) {	\
			ret = -ENOSPC;				\
			goto err_out;				\
		}						\
		(batch)[(i)++] = (val);				\
	} while(0)

static int render_state_setup(struct render_state *so)
{
	struct drm_device *dev = so->obj->base.dev;
	const struct intel_renderstate_rodata *rodata = so->rodata;
	unsigned int i = 0, reloc_index = 0;
	struct page *page;
	u32 *d;
	int ret;

	ret = i915_gem_object_set_to_cpu_domain(so->obj, true);
	if (ret)
		return ret;

	page = i915_gem_object_get_dirty_page(so->obj, 0);
	d = kmap(page);

	while (i < rodata->batch_items) {
		u32 s = rodata->batch[i];

		if (i * 4  == rodata->reloc[reloc_index]) {
			u64 r = s + so->ggtt_offset;
			s = lower_32_bits(r);
			if (so->gen >= 8) {
				if (i + 1 >= rodata->batch_items ||
				    rodata->batch[i + 1] != 0) {
					ret = -EINVAL;
					goto err_out;
				}

				d[i++] = s;
				s = upper_32_bits(r);
			}

			reloc_index++;
		}

		d[i++] = s;
	}

	while (i % CACHELINE_DWORDS)
		OUT_BATCH(d, i, MI_NOOP);

	so->aux_batch_offset = i * sizeof(u32);

	if (HAS_POOLED_EU(dev)) {
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
	so->aux_batch_size = (i * sizeof(u32)) - so->aux_batch_offset;

	/*
	 * Since we are sending length, we need to strictly conform to
	 * all requirements. For Gen2 this must be a multiple of 8.
	 */
	so->aux_batch_size = ALIGN(so->aux_batch_size, 8);

	kunmap(page);

	ret = i915_gem_object_set_to_gtt_domain(so->obj, false);
	if (ret)
		return ret;

	if (rodata->reloc[reloc_index] != -1) {
		DRM_ERROR("only %d relocs resolved\n", reloc_index);
		return -EINVAL;
	}

	return 0;

err_out:
	kunmap(page);
	return ret;
}

#undef OUT_BATCH

void i915_gem_render_state_fini(struct render_state *so)
{
	i915_gem_object_ggtt_unpin(so->obj);
	i915_gem_object_put(so->obj);
}

int i915_gem_render_state_prepare(struct intel_engine_cs *engine,
				  struct render_state *so)
{
	int ret;

	if (WARN_ON(engine->id != RCS))
		return -ENOENT;

	ret = render_state_init(so, engine->i915);
	if (ret)
		return ret;

	if (so->rodata == NULL)
		return 0;

	ret = render_state_setup(so);
	if (ret) {
		i915_gem_render_state_fini(so);
		return ret;
	}

	return 0;
}

int i915_gem_render_state_init(struct drm_i915_gem_request *req)
{
	struct render_state so;
	int ret;

	ret = i915_gem_render_state_prepare(req->engine, &so);
	if (ret)
		return ret;

	if (so.rodata == NULL)
		return 0;

	ret = req->engine->dispatch_execbuffer(req, so.ggtt_offset,
					     so.rodata->batch_items * 4,
					     I915_DISPATCH_SECURE);
	if (ret)
		goto out;

	if (so.aux_batch_size > 8) {
		ret = req->engine->dispatch_execbuffer(req,
						     (so.ggtt_offset +
						      so.aux_batch_offset),
						     so.aux_batch_size,
						     I915_DISPATCH_SECURE);
		if (ret)
			goto out;
	}

	i915_vma_move_to_active(i915_gem_obj_to_ggtt(so.obj), req);

out:
	i915_gem_render_state_fini(&so);
	return ret;
}
