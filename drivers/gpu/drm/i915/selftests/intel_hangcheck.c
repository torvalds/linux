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

#include <linux/kthread.h>

#include "../i915_selftest.h"

#include "mock_context.h"
#include "mock_drm.h"

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
	i915_gem_chipset_flush(h->i915);

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
	i915_gem_chipset_flush(h->i915);

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

	mutex_lock(&i915->drm.struct_mutex);
	err = hang_init(&h, i915);
	if (err)
		goto unlock;

	for_each_engine(engine, i915, id) {
		long timeout;

		if (!intel_engine_can_store_dword(engine))
			continue;

		rq = hang_create_request(&h, engine, i915->kernel_context);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			pr_err("Failed to create request for %s, err=%d\n",
			       engine->name, err);
			goto fini;
		}

		i915_gem_request_get(rq);

		*h.batch = MI_BATCH_BUFFER_END;
		i915_gem_chipset_flush(i915);

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

static void global_reset_lock(struct drm_i915_private *i915)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	while (test_and_set_bit(I915_RESET_BACKOFF, &i915->gpu_error.flags))
		wait_event(i915->gpu_error.reset_queue,
			   !test_bit(I915_RESET_BACKOFF,
				     &i915->gpu_error.flags));

	for_each_engine(engine, i915, id) {
		while (test_and_set_bit(I915_RESET_ENGINE + id,
					&i915->gpu_error.flags))
			wait_on_bit(&i915->gpu_error.flags,
				    I915_RESET_ENGINE + id,
				    TASK_UNINTERRUPTIBLE);
	}
}

static void global_reset_unlock(struct drm_i915_private *i915)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, i915, id)
		clear_bit(I915_RESET_ENGINE + id, &i915->gpu_error.flags);

	clear_bit(I915_RESET_BACKOFF, &i915->gpu_error.flags);
	wake_up_all(&i915->gpu_error.reset_queue);
}

static int igt_global_reset(void *arg)
{
	struct drm_i915_private *i915 = arg;
	unsigned int reset_count;
	int err = 0;

	/* Check that we can issue a global GPU reset */

	global_reset_lock(i915);
	set_bit(I915_RESET_HANDOFF, &i915->gpu_error.flags);

	mutex_lock(&i915->drm.struct_mutex);
	reset_count = i915_reset_count(&i915->gpu_error);

	i915_reset(i915, I915_RESET_QUIET);

	if (i915_reset_count(&i915->gpu_error) == reset_count) {
		pr_err("No GPU reset recorded!\n");
		err = -EINVAL;
	}
	mutex_unlock(&i915->drm.struct_mutex);

	GEM_BUG_ON(test_bit(I915_RESET_HANDOFF, &i915->gpu_error.flags));
	global_reset_unlock(i915);

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

		err = i915_reset_engine(engine, I915_RESET_QUIET);
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

static int active_engine(void *data)
{
	struct intel_engine_cs *engine = data;
	struct drm_i915_gem_request *rq[2] = {};
	struct i915_gem_context *ctx[2];
	struct drm_file *file;
	unsigned long count = 0;
	int err = 0;

	file = mock_file(engine->i915);
	if (IS_ERR(file))
		return PTR_ERR(file);

	mutex_lock(&engine->i915->drm.struct_mutex);
	ctx[0] = live_context(engine->i915, file);
	mutex_unlock(&engine->i915->drm.struct_mutex);
	if (IS_ERR(ctx[0])) {
		err = PTR_ERR(ctx[0]);
		goto err_file;
	}

	mutex_lock(&engine->i915->drm.struct_mutex);
	ctx[1] = live_context(engine->i915, file);
	mutex_unlock(&engine->i915->drm.struct_mutex);
	if (IS_ERR(ctx[1])) {
		err = PTR_ERR(ctx[1]);
		i915_gem_context_put(ctx[0]);
		goto err_file;
	}

	while (!kthread_should_stop()) {
		unsigned int idx = count++ & 1;
		struct drm_i915_gem_request *old = rq[idx];
		struct drm_i915_gem_request *new;

		mutex_lock(&engine->i915->drm.struct_mutex);
		new = i915_gem_request_alloc(engine, ctx[idx]);
		if (IS_ERR(new)) {
			mutex_unlock(&engine->i915->drm.struct_mutex);
			err = PTR_ERR(new);
			break;
		}

		rq[idx] = i915_gem_request_get(new);
		i915_add_request(new);
		mutex_unlock(&engine->i915->drm.struct_mutex);

		if (old) {
			i915_wait_request(old, 0, MAX_SCHEDULE_TIMEOUT);
			i915_gem_request_put(old);
		}
	}

	for (count = 0; count < ARRAY_SIZE(rq); count++)
		i915_gem_request_put(rq[count]);

err_file:
	mock_file_free(engine->i915, file);
	return err;
}

static int igt_reset_active_engines(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_engine_cs *engine, *active;
	enum intel_engine_id id, tmp;
	int err = 0;

	/* Check that issuing a reset on one engine does not interfere
	 * with any other engine.
	 */

	if (!intel_has_reset_engine(i915))
		return 0;

	for_each_engine(engine, i915, id) {
		struct task_struct *threads[I915_NUM_ENGINES];
		unsigned long resets[I915_NUM_ENGINES];
		unsigned long global = i915_reset_count(&i915->gpu_error);
		IGT_TIMEOUT(end_time);

		memset(threads, 0, sizeof(threads));
		for_each_engine(active, i915, tmp) {
			struct task_struct *tsk;

			if (active == engine)
				continue;

			resets[tmp] = i915_reset_engine_count(&i915->gpu_error,
							      active);

			tsk = kthread_run(active_engine, active,
					  "igt/%s", active->name);
			if (IS_ERR(tsk)) {
				err = PTR_ERR(tsk);
				goto unwind;
			}

			threads[tmp] = tsk;
			get_task_struct(tsk);
		}

		set_bit(I915_RESET_ENGINE + engine->id, &i915->gpu_error.flags);
		do {
			err = i915_reset_engine(engine, I915_RESET_QUIET);
			if (err) {
				pr_err("i915_reset_engine(%s) failed, err=%d\n",
				       engine->name, err);
				break;
			}
		} while (time_before(jiffies, end_time));
		clear_bit(I915_RESET_ENGINE + engine->id,
			  &i915->gpu_error.flags);

unwind:
		for_each_engine(active, i915, tmp) {
			int ret;

			if (!threads[tmp])
				continue;

			ret = kthread_stop(threads[tmp]);
			if (ret) {
				pr_err("kthread for active engine %s failed, err=%d\n",
				       active->name, ret);
				if (!err)
					err = ret;
			}
			put_task_struct(threads[tmp]);

			if (resets[tmp] != i915_reset_engine_count(&i915->gpu_error,
								   active)) {
				pr_err("Innocent engine %s was reset (count=%ld)\n",
				       active->name,
				       i915_reset_engine_count(&i915->gpu_error,
							       active) - resets[tmp]);
				err = -EIO;
			}
		}

		if (global != i915_reset_count(&i915->gpu_error)) {
			pr_err("Global reset (count=%ld)!\n",
			       i915_reset_count(&i915->gpu_error) - global);
			err = -EIO;
		}

		if (err)
			break;

		cond_resched();
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

	if (!intel_engine_can_store_dword(i915->engine[RCS]))
		return 0;

	/* Check that we detect a stuck waiter and issue a reset */

	global_reset_lock(i915);

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
		struct drm_printer p = drm_info_printer(i915->drm.dev);

		pr_err("Failed to start request %x, at %x\n",
		       rq->fence.seqno, hws_seqno(&h, rq));
		intel_engine_dump(rq->engine, &p);

		i915_reset(i915, 0);
		i915_gem_set_wedged(i915);

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
	global_reset_unlock(i915);

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

	global_reset_lock(i915);

	mutex_lock(&i915->drm.struct_mutex);
	err = hang_init(&h, i915);
	if (err)
		goto unlock;

	for_each_engine(engine, i915, id) {
		struct drm_i915_gem_request *prev;
		IGT_TIMEOUT(end_time);
		unsigned int count;

		if (!intel_engine_can_store_dword(engine))
			continue;

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
				struct drm_printer p = drm_info_printer(i915->drm.dev);

				pr_err("Failed to start request %x, at %x\n",
				       prev->fence.seqno, hws_seqno(&h, prev));
				intel_engine_dump(rq->engine, &p);

				i915_gem_request_put(rq);
				i915_gem_request_put(prev);

				i915_reset(i915, 0);
				i915_gem_set_wedged(i915);

				err = -EIO;
				goto fini;
			}

			reset_count = fake_hangcheck(prev);

			i915_reset(i915, I915_RESET_QUIET);

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
		i915_gem_chipset_flush(i915);

		i915_gem_request_put(prev);
	}

fini:
	hang_fini(&h);
unlock:
	mutex_unlock(&i915->drm.struct_mutex);
	global_reset_unlock(i915);

	if (i915_terminally_wedged(&i915->gpu_error))
		return -EIO;

	return err;
}

static int igt_handle_error(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_engine_cs *engine = i915->engine[RCS];
	struct hang h;
	struct drm_i915_gem_request *rq;
	struct i915_gpu_state *error;
	int err;

	/* Check that we can issue a global GPU and engine reset */

	if (!intel_has_reset_engine(i915))
		return 0;

	if (!intel_engine_can_store_dword(i915->engine[RCS]))
		return 0;

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

	if (!wait_for_hang(&h, rq)) {
		struct drm_printer p = drm_info_printer(i915->drm.dev);

		pr_err("Failed to start request %x, at %x\n",
		       rq->fence.seqno, hws_seqno(&h, rq));
		intel_engine_dump(rq->engine, &p);

		i915_reset(i915, 0);
		i915_gem_set_wedged(i915);

		err = -EIO;
		goto err_request;
	}

	mutex_unlock(&i915->drm.struct_mutex);

	/* Temporarily disable error capture */
	error = xchg(&i915->gpu_error.first_error, (void *)-1);

	engine->hangcheck.stalled = true;
	engine->hangcheck.seqno = intel_engine_get_seqno(engine);

	i915_handle_error(i915, intel_engine_flag(engine), "%s", __func__);

	xchg(&i915->gpu_error.first_error, error);

	mutex_lock(&i915->drm.struct_mutex);

	if (rq->fence.error != -EIO) {
		pr_err("Guilty request not identified!\n");
		err = -EINVAL;
		goto err_request;
	}

err_request:
	i915_gem_request_put(rq);
err_fini:
	hang_fini(&h);
err_unlock:
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

int intel_hangcheck_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_global_reset), /* attempt to recover GPU first */
		SUBTEST(igt_hang_sanitycheck),
		SUBTEST(igt_reset_engine),
		SUBTEST(igt_reset_active_engines),
		SUBTEST(igt_wait_reset),
		SUBTEST(igt_reset_queue),
		SUBTEST(igt_handle_error),
	};
	int err;

	if (!intel_has_gpu_reset(i915))
		return 0;

	intel_runtime_pm_get(i915);

	err = i915_subtests(tests, i915);

	intel_runtime_pm_put(i915);

	return err;
}
