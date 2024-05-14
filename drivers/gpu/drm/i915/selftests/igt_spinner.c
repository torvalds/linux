/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */
#include "gt/intel_gpu_commands.h"
#include "gt/intel_gt.h"

#include "gem/i915_gem_internal.h"
#include "gem/selftests/igt_gem_utils.h"

#include "igt_spinner.h"

int igt_spinner_init(struct igt_spinner *spin, struct intel_gt *gt)
{
	int err;

	memset(spin, 0, sizeof(*spin));
	spin->gt = gt;

	spin->hws = i915_gem_object_create_internal(gt->i915, PAGE_SIZE);
	if (IS_ERR(spin->hws)) {
		err = PTR_ERR(spin->hws);
		goto err;
	}
	i915_gem_object_set_cache_coherency(spin->hws, I915_CACHE_LLC);

	spin->obj = i915_gem_object_create_internal(gt->i915, PAGE_SIZE);
	if (IS_ERR(spin->obj)) {
		err = PTR_ERR(spin->obj);
		goto err_hws;
	}

	return 0;

err_hws:
	i915_gem_object_put(spin->hws);
err:
	return err;
}

static void *igt_spinner_pin_obj(struct intel_context *ce,
				 struct i915_gem_ww_ctx *ww,
				 struct drm_i915_gem_object *obj,
				 unsigned int mode, struct i915_vma **vma)
{
	void *vaddr;
	int ret;

	*vma = i915_vma_instance(obj, ce->vm, NULL);
	if (IS_ERR(*vma))
		return ERR_CAST(*vma);

	ret = i915_gem_object_lock(obj, ww);
	if (ret)
		return ERR_PTR(ret);

	vaddr = i915_gem_object_pin_map(obj, mode);

	if (!ww)
		i915_gem_object_unlock(obj);

	if (IS_ERR(vaddr))
		return vaddr;

	if (ww)
		ret = i915_vma_pin_ww(*vma, ww, 0, 0, PIN_USER);
	else
		ret = i915_vma_pin(*vma, 0, 0, PIN_USER);

	if (ret) {
		i915_gem_object_unpin_map(obj);
		return ERR_PTR(ret);
	}

	return vaddr;
}

int igt_spinner_pin(struct igt_spinner *spin,
		    struct intel_context *ce,
		    struct i915_gem_ww_ctx *ww)
{
	void *vaddr;

	if (spin->ce && WARN_ON(spin->ce != ce))
		return -ENODEV;
	spin->ce = ce;

	if (!spin->seqno) {
		vaddr = igt_spinner_pin_obj(ce, ww, spin->hws, I915_MAP_WB, &spin->hws_vma);
		if (IS_ERR(vaddr))
			return PTR_ERR(vaddr);

		spin->seqno = memset(vaddr, 0xff, PAGE_SIZE);
	}

	if (!spin->batch) {
		unsigned int mode;

		mode = i915_coherent_map_type(spin->gt->i915, spin->obj, false);
		vaddr = igt_spinner_pin_obj(ce, ww, spin->obj, mode, &spin->batch_vma);
		if (IS_ERR(vaddr))
			return PTR_ERR(vaddr);

		spin->batch = vaddr;
	}

	return 0;
}

static unsigned int seqno_offset(u64 fence)
{
	return offset_in_page(sizeof(u32) * fence);
}

static u64 hws_address(const struct i915_vma *hws,
		       const struct i915_request *rq)
{
	return hws->node.start + seqno_offset(rq->fence.context);
}

static int move_to_active(struct i915_vma *vma,
			  struct i915_request *rq,
			  unsigned int flags)
{
	int err;

	i915_vma_lock(vma);
	err = i915_request_await_object(rq, vma->obj,
					flags & EXEC_OBJECT_WRITE);
	if (err == 0)
		err = i915_vma_move_to_active(vma, rq, flags);
	i915_vma_unlock(vma);

	return err;
}

struct i915_request *
igt_spinner_create_request(struct igt_spinner *spin,
			   struct intel_context *ce,
			   u32 arbitration_command)
{
	struct intel_engine_cs *engine = ce->engine;
	struct i915_request *rq = NULL;
	struct i915_vma *hws, *vma;
	unsigned int flags;
	u32 *batch;
	int err;

	GEM_BUG_ON(spin->gt != ce->vm->gt);

	if (!intel_engine_can_store_dword(ce->engine))
		return ERR_PTR(-ENODEV);

	if (!spin->batch) {
		err = igt_spinner_pin(spin, ce, NULL);
		if (err)
			return ERR_PTR(err);
	}

	hws = spin->hws_vma;
	vma = spin->batch_vma;

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq))
		return ERR_CAST(rq);

	err = move_to_active(vma, rq, 0);
	if (err)
		goto cancel_rq;

	err = move_to_active(hws, rq, 0);
	if (err)
		goto cancel_rq;

	batch = spin->batch;

	if (GRAPHICS_VER(rq->engine->i915) >= 8) {
		*batch++ = MI_STORE_DWORD_IMM_GEN4;
		*batch++ = lower_32_bits(hws_address(hws, rq));
		*batch++ = upper_32_bits(hws_address(hws, rq));
	} else if (GRAPHICS_VER(rq->engine->i915) >= 6) {
		*batch++ = MI_STORE_DWORD_IMM_GEN4;
		*batch++ = 0;
		*batch++ = hws_address(hws, rq);
	} else if (GRAPHICS_VER(rq->engine->i915) >= 4) {
		*batch++ = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT;
		*batch++ = 0;
		*batch++ = hws_address(hws, rq);
	} else {
		*batch++ = MI_STORE_DWORD_IMM | MI_MEM_VIRTUAL;
		*batch++ = hws_address(hws, rq);
	}
	*batch++ = rq->fence.seqno;

	*batch++ = arbitration_command;

	if (GRAPHICS_VER(rq->engine->i915) >= 8)
		*batch++ = MI_BATCH_BUFFER_START | BIT(8) | 1;
	else if (IS_HASWELL(rq->engine->i915))
		*batch++ = MI_BATCH_BUFFER_START | MI_BATCH_PPGTT_HSW;
	else if (GRAPHICS_VER(rq->engine->i915) >= 6)
		*batch++ = MI_BATCH_BUFFER_START;
	else
		*batch++ = MI_BATCH_BUFFER_START | MI_BATCH_GTT;
	*batch++ = lower_32_bits(vma->node.start);
	*batch++ = upper_32_bits(vma->node.start);

	*batch++ = MI_BATCH_BUFFER_END; /* not reached */

	intel_gt_chipset_flush(engine->gt);

	if (engine->emit_init_breadcrumb) {
		err = engine->emit_init_breadcrumb(rq);
		if (err)
			goto cancel_rq;
	}

	flags = 0;
	if (GRAPHICS_VER(rq->engine->i915) <= 5)
		flags |= I915_DISPATCH_SECURE;
	err = engine->emit_bb_start(rq, vma->node.start, PAGE_SIZE, flags);

cancel_rq:
	if (err) {
		i915_request_set_error_once(rq, err);
		i915_request_add(rq);
	}
	return err ? ERR_PTR(err) : rq;
}

static u32
hws_seqno(const struct igt_spinner *spin, const struct i915_request *rq)
{
	u32 *seqno = spin->seqno + seqno_offset(rq->fence.context);

	return READ_ONCE(*seqno);
}

void igt_spinner_end(struct igt_spinner *spin)
{
	if (!spin->batch)
		return;

	*spin->batch = MI_BATCH_BUFFER_END;
	intel_gt_chipset_flush(spin->gt);
}

void igt_spinner_fini(struct igt_spinner *spin)
{
	igt_spinner_end(spin);

	if (spin->batch) {
		i915_vma_unpin(spin->batch_vma);
		i915_gem_object_unpin_map(spin->obj);
	}
	i915_gem_object_put(spin->obj);

	if (spin->seqno) {
		i915_vma_unpin(spin->hws_vma);
		i915_gem_object_unpin_map(spin->hws);
	}
	i915_gem_object_put(spin->hws);
}

bool igt_wait_for_spinner(struct igt_spinner *spin, struct i915_request *rq)
{
	if (i915_request_is_ready(rq))
		intel_engine_flush_submission(rq->engine);

	return !(wait_for_us(i915_seqno_passed(hws_seqno(spin, rq),
					       rq->fence.seqno),
			     100) &&
		 wait_for(i915_seqno_passed(hws_seqno(spin, rq),
					    rq->fence.seqno),
			  50));
}
