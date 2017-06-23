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

#include "../i915_selftest.h"

struct hang {
	struct drm_i915_private *i915;
	struct drm_i915_gem_object *hws;
	struct drm_i915_gem_object *obj;
	u32 *seqno;
	u32 *batch;
};

static int hang_init(struct hang *h, struct drm_i915_private *i915)
{
	void *vaddr;
	int err;

	memset(h, 0, sizeof(*h));
	h->i915 = i915;

	h->hws = i915_gem_object_create_internal(i915, PAGE_SIZE);
	if (IS_ERR(h->hws))
		return PTR_ERR(h->hws);

	h->obj = i915_gem_object_create_internal(i915, PAGE_SIZE);
	if (IS_ERR(h->obj)) {
		err = PTR_ERR(h->obj);
		goto err_hws;
	}

	i915_gem_object_set_cache_level(h->hws, I915_CACHE_LLC);
	vaddr = i915_gem_object_pin_map(h->hws, I915_MAP_WB);
	if (IS_ERR(vaddr)) {
		err = PTR_ERR(vaddr);
		goto err_obj;
	}
	h->seqno = memset(vaddr, 0xff, PAGE_SIZE);

	vaddr = i915_gem_object_pin_map(h->obj,
					HAS_LLC(i915) ? I915_MAP_WB : I915_MAP_WC);
	if (IS_ERR(vaddr)) {
		err = PTR_ERR(vaddr);
		goto err_unpin_hws;
	}
	h->batch = vaddr;

	return 0;

err_unpin_hws:
	i915_gem_object_unpin_map(h->hws);
err_obj:
	i915_gem_object_put(h->obj);
err_hws:
	i915_gem_object_put(h->hws);
	return err;
}

static u64 hws_address(const struct i915_vma *hws,
		       const struct drm_i915_gem_request *rq)
{
	return hws->node.start + offset_in_page(sizeof(u32)*rq->fence.context);
}

static int emit_recurse_batch(struct hang *h,
			      struct drm_i915_gem_request *rq)
{
	struct drm_i915_private *i915 = h->i915;
	struct i915_address_space *vm = rq->ctx->ppgtt ? &rq->ctx->ppgtt->base : &i915->ggtt.base;
	struct i915_vma *hws, *vma;
	unsigned int flags;
	u32 *batch;
	int err;

	vma = i915_vma_instance(h->obj, vm, NULL);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	hws = i915_vma_instance(h->hws, vm, NULL);
	if (IS_ERR(hws))
		return PTR_ERR(hws);

	err = i915_vma_pin(vma, 0, 0, PIN_USER);
	if (err)
		return err;

	err = i915_vma_pin(hws, 0, 0, PIN_USER);
	if (err)
		goto unpin_vma;

	err = rq->engine->emit_flush(rq, EMIT_INVALIDATE);
	if (err)
		goto unpin_hws;

	err = i915_switch_context(rq);
	if (err)
		goto unpin_hws;

	i915_vma_move_to_active(vma, rq, 0);
	if (!i915_gem_object_has_active_reference(vma->obj)) {
		i915_gem_object_get(vma->obj);
		i915_gem_object_set_active_reference(vma->obj);
	}

	i915_vma_move_to_active(hws, rq, 0);
	if (!i915_gem_object_has_active_reference(hws->obj)) {
		i915_gem_object_get(hws->obj);
		i915_gem_object_set_active_reference(hws->obj);
	}

	batch = h->batch;
	if (INTEL_GEN(i915) >= 8) {
		*batch++ = MI_STORE_DWORD_IMM_GEN4;
		*batch++ = lower_32_bits(hws_address(hws, rq));
		*batch++ = upper_32_bits(hws_address(hws, rq));
		*batch++ = rq->fence.seqno;
		*batch++ = MI_BATCH_BUFFER_START | 1 << 8 | 1;
		*batch++ = lower_32_bits(vma->node.start);
		*batch++ = upper_32_bits(vma->node.start);
	} else if (INTEL_GEN(i915) >= 6) {
		*batch++ = MI_STORE_DWORD_IMM_GEN4;
		*batch++ = 0;
		*batch++ = lower_32_bits(hws_address(hws, rq));
		*batch++ = rq->fence.seqno;
		*batch++ = MI_BATCH_BUFFER_START | 1 << 8;
		*batch++ = lower_32_bits(vma->node.start);
	} else if (INTEL_GEN(i915) >= 4) {
		*batch++ = MI_STORE_DWORD_IMM_GEN4 | 1 << 22;
		*batch++ = 0;
		*batch++ = lower_32_bits(hws_address(hws, rq));
		*batch++ = rq->fence.seqno;
		*batch++ = MI_BATCH_BUFFER_START | 2 << 6;
		*batch++ = lower_32_bits(vma->node.start);
	} else {
		*batch++ = MI_STORE_DWORD_IMM;
		*batch++ = lower_32_bits(hws_address(hws, rq));
		*batch++ = rq->fence.seqno;
		*batch++ = MI_BATCH_BUFFER_START | 2 << 6 | 1;
		*batch++ = lower_32_bits(vma->node.start);
	}
	*batch++ = MI_BATCH_BUFFER_END; /* not reached */

	flags = 0;
	if (INTEL_GEN(vm->i915) <= 5)
		flags |= I915_DISPATCH_SECURE;

	err = rq->engine->emit_bb_start(rq, vma->node.start, PAGE_SIZE, flags);

unpin_hws:
	i915_vma_unpin(hws);
unpin_vma:
	i915_vma_unpin(vma);
	return err;
}

static struct drm_i915_gem_request *
hang_create_request(struct hang *h,
		    struct intel_engine_cs *engine,
		    struct i915_gem_context *ctx)
{
	struct drm_i915_gem_request *rq;
	int err;

	if (i915_gem_object_is_active(h->obj)) {
		struct drm_i915_gem_object *obj;
		void *vaddr;

		obj = i915_gem_object_create_internal(h->i915, PAGE_SIZE);
		if (IS_ERR(obj))
			return ERR_CAST(obj);

		vaddr = i915_gem_object_pin_map(obj,
						HAS_LLC(h->i915) ? I915_MAP_WB : I915_MAP_WC);
		if (IS_ERR(vaddr)) {
			i915_gem_object_put(obj);
			return ERR_CAST(vaddr);
		}

		i915_gem_object_unpin_map(h->obj);
		i915_gem_object_put(h->obj);

		h->obj = obj;
		h->batch = vaddr;
	}

	rq = i915_gem_request_alloc(engine, ctx);
	if (IS_ERR(rq))
		return rq;

	err = emit_recurse_batch(h, rq);
	if (err) {
		__i915_add_request(rq, false);
		return ERR_PTR(err);
	}

	return rq;
}

static u32 hws_seqno(const struct hang *h,
		     const struct drm_i915_gem_request *rq)
{
	return READ_ONCE(h->seqno[rq->fence.context % (PAGE_SIZE/sizeof(u32))]);
}

static void hang_fini(struct hang *h)
{
	*h->batch = MI_BATCH_BUFFER_END;
	wmb();

	i915_gem_object_unpin_map(h->obj);
	i915_gem_object_put(h->obj);

	i915_gem_object_unpin_map(h->hws);
	i915_gem_object_put(h->hws);

	i915_gem_wait_for_idle(h->i915, I915_WAIT_LOCKED);
}

static int igt_hang_sanitycheck(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_request *rq;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct hang h;
	int err;

	/* Basic check that we can execute our hanging batch */

	if (!igt_can_mi_store_dword_imm(i915))
		return 0;

	mutex_lock(&i915->drm.struct_mutex);
	err = hang_init(&h, i915);
	if (err)
		goto unlock;

	for_each_engine(engine, i915, id) {
		long timeout;

		rq = hang_create_request(&h, engine, i915->kernel_context);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			pr_err("Failed to create request for %s, err=%d\n",
			       engine->name, err);
			goto fini;
		}

		i915_gem_request_get(rq);

		*h.batch = MI_BATCH_BUFFER_END;
		__i915_add_request(rq, true);

		timeout = i915_wait_request(rq,
					    I915_WAIT_LOCKED,
					    MAX_SCHEDULE_TIMEOUT);
		i915_gem_request_put(rq);

		if (timeout < 0) {
			err = timeout;
			pr_err("Wait for request failed on %s, err=%d\n",
			       engine->name, err);
			goto fini;
		}
	}

fini:
	hang_fini(&h);
unlock:
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

static int igt_global_reset(void *arg)
{
	struct drm_i915_private *i915 = arg;
	unsigned int reset_count;
	int err = 0;

	/* Check that we can issue a global GPU reset */

	set_bit(I915_RESET_BACKOFF, &i915->gpu_error.flags);
	set_bit(I915_RESET_HANDOFF, &i915->gpu_error.flags);

	mutex_lock(&i915->drm.struct_mutex);
	reset_count = i915_reset_count(&i915->gpu_error);

	i915_reset(i915);

	if (i915_reset_count(&i915->gpu_error) == reset_count) {
		pr_err("No GPU reset recorded!\n");
		err = -EINVAL;
	}
	mutex_unlock(&i915->drm.struct_mutex);

	GEM_BUG_ON(test_bit(I915_RESET_HANDOFF, &i915->gpu_error.flags));
	clear_bit(I915_RESET_BACKOFF, &i915->gpu_error.flags);
	wake_up_all(&i915->gpu_error.reset_queue);

	if (i915_terminally_wedged(&i915->gpu_error))
		err = -EIO;

	return err;
}

static int igt_reset_engine(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	unsigned int reset_count, reset_engine_count;
	int err = 0;

	/* Check that we can issue a global GPU and engine reset */

	if (!intel_has_reset_engine(i915))
		return 0;

	for_each_engine(engine, i915, id) {
		set_bit(I915_RESET_ENGINE + engine->id, &i915->gpu_error.flags);
		reset_count = i915_reset_count(&i915->gpu_error);
		reset_engine_count = i915_reset_engine_count(&i915->gpu_error,
							     engine);

		err = i915_reset_engine(engine);
		if (err) {
			pr_err("i915_reset_engine failed\n");
			break;
		}

		if (i915_reset_count(&i915->gpu_error) != reset_count) {
			pr_err("Full GPU reset recorded! (engine reset expected)\n");
			err = -EINVAL;
			break;
		}

		if (i915_reset_engine_count(&i915->gpu_error, engine) ==
		    reset_engine_count) {
			pr_err("No %s engine reset recorded!\n", engine->name);
			err = -EINVAL;
			break;
		}

		clear_bit(I915_RESET_ENGINE + engine->id,
			  &i915->gpu_error.flags);
	}

	if (i915_terminally_wedged(&i915->gpu_error))
		err = -EIO;

	return err;
}

static u32 fake_hangcheck(struct drm_i915_gem_request *rq)
{
	u32 reset_count;

	rq->engine->hangcheck.stalled = true;
	rq->engine->hangcheck.seqno = intel_engine_get_seqno(rq->engine);

	reset_count = i915_reset_count(&rq->i915->gpu_error);

	set_bit(I915_RESET_HANDOFF, &rq->i915->gpu_error.flags);
	wake_up_all(&rq->i915->gpu_error.wait_queue);

	return reset_count;
}

static bool wait_for_hang(struct hang *h, struct drm_i915_gem_request *rq)
{
	return !(wait_for_us(i915_seqno_passed(hws_seqno(h, rq),
					       rq->fence.seqno),
			     10) &&
		 wait_for(i915_seqno_passed(hws_seqno(h, rq),
					    rq->fence.seqno),
			  1000));
}

static int igt_wait_reset(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_request *rq;
	unsigned int reset_count;
	struct hang h;
	long timeout;
	int err;

	/* Check that we detect a stuck waiter and issue a reset */

	set_bit(I915_RESET_BACKOFF, &i915->gpu_error.flags);

	mutex_lock(&i915->drm.struct_mutex);
	err = hang_init(&h, i915);
	if (err)
		goto unlock;

	rq = hang_create_request(&h, i915->engine[RCS], i915->kernel_context);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto fini;
	}

	i915_gem_request_get(rq);
	__i915_add_request(rq, true);

	if (!wait_for_hang(&h, rq)) {
		pr_err("Failed to start request %x\n", rq->fence.seqno);
		err = -EIO;
		goto out_rq;
	}

	reset_count = fake_hangcheck(rq);

	timeout = i915_wait_request(rq, I915_WAIT_LOCKED, 10);
	if (timeout < 0) {
		pr_err("i915_wait_request failed on a stuck request: err=%ld\n",
		       timeout);
		err = timeout;
		goto out_rq;
	}

	GEM_BUG_ON(test_bit(I915_RESET_HANDOFF, &i915->gpu_error.flags));
	if (i915_reset_count(&i915->gpu_error) == reset_count) {
		pr_err("No GPU reset recorded!\n");
		err = -EINVAL;
		goto out_rq;
	}

out_rq:
	i915_gem_request_put(rq);
fini:
	hang_fini(&h);
unlock:
	mutex_unlock(&i915->drm.struct_mutex);
	clear_bit(I915_RESET_BACKOFF, &i915->gpu_error.flags);
	wake_up_all(&i915->gpu_error.reset_queue);

	if (i915_terminally_wedged(&i915->gpu_error))
		return -EIO;

	return err;
}

static int igt_reset_queue(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct hang h;
	int err;

	/* Check that we replay pending requests following a hang */

	if (!igt_can_mi_store_dword_imm(i915))
		return 0;

	set_bit(I915_RESET_BACKOFF, &i915->gpu_error.flags);
	mutex_lock(&i915->drm.struct_mutex);
	err = hang_init(&h, i915);
	if (err)
		goto unlock;

	for_each_engine(engine, i915, id) {
		struct drm_i915_gem_request *prev;
		IGT_TIMEOUT(end_time);
		unsigned int count;

		prev = hang_create_request(&h, engine, i915->kernel_context);
		if (IS_ERR(prev)) {
			err = PTR_ERR(prev);
			goto fini;
		}

		i915_gem_request_get(prev);
		__i915_add_request(prev, true);

		count = 0;
		do {
			struct drm_i915_gem_request *rq;
			unsigned int reset_count;

			rq = hang_create_request(&h,
						 engine,
						 i915->kernel_context);
			if (IS_ERR(rq)) {
				err = PTR_ERR(rq);
				goto fini;
			}

			i915_gem_request_get(rq);
			__i915_add_request(rq, true);

			if (!wait_for_hang(&h, prev)) {
				pr_err("Failed to start request %x\n",
				       prev->fence.seqno);
				i915_gem_request_put(rq);
				i915_gem_request_put(prev);
				err = -EIO;
				goto fini;
			}

			reset_count = fake_hangcheck(prev);

			i915_reset(i915);

			GEM_BUG_ON(test_bit(I915_RESET_HANDOFF,
					    &i915->gpu_error.flags));

			if (prev->fence.error != -EIO) {
				pr_err("GPU reset not recorded on hanging request [fence.error=%d]!\n",
				       prev->fence.error);
				i915_gem_request_put(rq);
				i915_gem_request_put(prev);
				err = -EINVAL;
				goto fini;
			}

			if (rq->fence.error) {
				pr_err("Fence error status not zero [%d] after unrelated reset\n",
				       rq->fence.error);
				i915_gem_request_put(rq);
				i915_gem_request_put(prev);
				err = -EINVAL;
				goto fini;
			}

			if (i915_reset_count(&i915->gpu_error) == reset_count) {
				pr_err("No GPU reset recorded!\n");
				i915_gem_request_put(rq);
				i915_gem_request_put(prev);
				err = -EINVAL;
				goto fini;
			}

			i915_gem_request_put(prev);
			prev = rq;
			count++;
		} while (time_before(jiffies, end_time));
		pr_info("%s: Completed %d resets\n", engine->name, count);

		*h.batch = MI_BATCH_BUFFER_END;
		wmb();

		i915_gem_request_put(prev);
	}

fini:
	hang_fini(&h);
unlock:
	mutex_unlock(&i915->drm.struct_mutex);
	clear_bit(I915_RESET_BACKOFF, &i915->gpu_error.flags);
	wake_up_all(&i915->gpu_error.reset_queue);

	if (i915_terminally_wedged(&i915->gpu_error))
		return -EIO;

	return err;
}

static int igt_render_engine_reset_fallback(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_engine_cs *engine = i915->engine[RCS];
	struct hang h;
	struct drm_i915_gem_request *rq;
	unsigned int reset_count, reset_engine_count;
	int err = 0;

	/* Check that we can issue a global GPU and engine reset */

	if (!intel_has_reset_engine(i915))
		return 0;

	set_bit(I915_RESET_BACKOFF, &i915->gpu_error.flags);
	mutex_lock(&i915->drm.struct_mutex);

	err = hang_init(&h, i915);
	if (err)
		goto err_unlock;

	rq = hang_create_request(&h, engine, i915->kernel_context);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_fini;
	}

	i915_gem_request_get(rq);
	__i915_add_request(rq, true);

	/* make reset engine fail */
	rq->fence.error = -EIO;

	if (!wait_for_hang(&h, rq)) {
		pr_err("Failed to start request %x\n", rq->fence.seqno);
		err = -EIO;
		goto err_request;
	}

	reset_engine_count = i915_reset_engine_count(&i915->gpu_error, engine);
	reset_count = fake_hangcheck(rq);

	/* unlock since we'll call handle_error */
	mutex_unlock(&i915->drm.struct_mutex);
	clear_bit(I915_RESET_BACKOFF, &i915->gpu_error.flags);
	wake_up_all(&i915->gpu_error.reset_queue);

	i915_handle_error(i915, intel_engine_flag(engine), "live test");

	if (i915_reset_engine_count(&i915->gpu_error, engine) !=
	    reset_engine_count) {
		pr_err("render engine reset recorded! (full reset expected)\n");
		err = -EINVAL;
		goto out_rq;
	}

	if (i915_reset_count(&i915->gpu_error) == reset_count) {
		pr_err("No full GPU reset recorded!\n");
		err = -EINVAL;
		goto out_rq;
	}

	/*
	 * by using fence.error = -EIO, full reset sets the wedged flag, do one
	 * more full reset to re-enable the hw.
	 */
	if (i915_terminally_wedged(&i915->gpu_error)) {
		set_bit(I915_RESET_BACKOFF, &i915->gpu_error.flags);
		rq->fence.error = 0;

		mutex_lock(&i915->drm.struct_mutex);
		set_bit(I915_RESET_HANDOFF, &i915->gpu_error.flags);
		i915_reset(i915);
		GEM_BUG_ON(test_bit(I915_RESET_HANDOFF,
				    &i915->gpu_error.flags));
		mutex_unlock(&i915->drm.struct_mutex);

		if (i915_reset_count(&i915->gpu_error) == reset_count) {
			pr_err("No full GPU reset recorded!\n");
			err = -EINVAL;
			goto out_rq;
		}
	}

out_rq:
	i915_gem_request_put(rq);
	hang_fini(&h);
out_backoff:
	clear_bit(I915_RESET_BACKOFF, &i915->gpu_error.flags);
	wake_up_all(&i915->gpu_error.reset_queue);

	if (i915_terminally_wedged(&i915->gpu_error))
		return -EIO;

	return err;

err_request:
	i915_gem_request_put(rq);
err_fini:
	hang_fini(&h);
err_unlock:
	mutex_unlock(&i915->drm.struct_mutex);
	goto out_backoff;
}

int intel_hangcheck_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_hang_sanitycheck),
		SUBTEST(igt_global_reset),
		SUBTEST(igt_reset_engine),
		SUBTEST(igt_wait_reset),
		SUBTEST(igt_reset_queue),
		SUBTEST(igt_render_engine_reset_fallback),
	};

	if (!intel_has_gpu_reset(i915))
		return 0;

	return i915_subtests(tests, i915);
}
