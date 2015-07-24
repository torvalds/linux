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
render_state_get_rodata(struct drm_device *dev, const int gen)
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

static int render_state_init(struct render_state *so, struct drm_device *dev)
{
	int ret;

	so->gen = INTEL_INFO(dev)->gen;
	so->rodata = render_state_get_rodata(dev, so->gen);
	if (so->rodata == NULL)
		return 0;

	if (so->rodata->batch_items * 4 > 4096)
		return -EINVAL;

	so->obj = i915_gem_alloc_object(dev, 4096);
	if (so->obj == NULL)
		return -ENOMEM;

	ret = i915_gem_obj_ggtt_pin(so->obj, 4096, 0);
	if (ret)
		goto free_gem;

	so->ggtt_offset = i915_gem_obj_ggtt_offset(so->obj);
	return 0;

free_gem:
	drm_gem_object_unreference(&so->obj->base);
	return ret;
}

static int render_state_setup(struct render_state *so)
{
	const struct intel_renderstate_rodata *rodata = so->rodata;
	unsigned int i = 0, reloc_index = 0;
	struct page *page;
	u32 *d;
	int ret;

	ret = i915_gem_object_set_to_cpu_domain(so->obj, true);
	if (ret)
		return ret;

	page = sg_page(so->obj->pages->sgl);
	d = kmap(page);

	while (i < rodata->batch_items) {
		u32 s = rodata->batch[i];

		if (i * 4  == rodata->reloc[reloc_index]) {
			u64 r = s + so->ggtt_offset;
			s = lower_32_bits(r);
			if (so->gen >= 8) {
				if (i + 1 >= rodata->batch_items ||
				    rodata->batch[i + 1] != 0)
					return -EINVAL;

				d[i++] = s;
				s = upper_32_bits(r);
			}

			reloc_index++;
		}

		d[i++] = s;
	}
	kunmap(page);

	ret = i915_gem_object_set_to_gtt_domain(so->obj, false);
	if (ret)
		return ret;

	if (rodata->reloc[reloc_index] != -1) {
		DRM_ERROR("only %d relocs resolved\n", reloc_index);
		return -EINVAL;
	}

	return 0;
}

void i915_gem_render_state_fini(struct render_state *so)
{
	i915_gem_object_ggtt_unpin(so->obj);
	drm_gem_object_unreference(&so->obj->base);
}

int i915_gem_render_state_prepare(struct intel_engine_cs *ring,
				  struct render_state *so)
{
	int ret;

	if (WARN_ON(ring->id != RCS))
		return -ENOENT;

	ret = render_state_init(so, ring->dev);
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

	ret = i915_gem_render_state_prepare(req->ring, &so);
	if (ret)
		return ret;

	if (so.rodata == NULL)
		return 0;

	ret = req->ring->dispatch_execbuffer(req, so.ggtt_offset,
					     so.rodata->batch_items * 4,
					     I915_DISPATCH_SECURE);
	if (ret)
		goto out;

	i915_vma_move_to_active(i915_gem_obj_to_ggtt(so.obj), req);

out:
	i915_gem_render_state_fini(&so);
	return ret;
}
