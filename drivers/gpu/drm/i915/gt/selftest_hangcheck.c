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

#include "gem/i915_gem_context.h"
#include "intel_engine_pm.h"

#include "i915_selftest.h"
#include "selftests/i915_random.h"
#include "selftests/igt_flush_test.h"
#include "selftests/igt_reset.h"
#include "selftests/igt_wedge_me.h"
#include "selftests/igt_atomic.h"

#include "selftests/mock_drm.h"

#include "gem/selftests/mock_context.h"
#include "gem/selftests/igt_gem_utils.h"

#define IGT_IDLE_TIMEOUT 50 /* ms; time to wait after flushing between tests */

struct hang {
	struct drm_i915_private *i915;
	struct drm_i915_gem_object *hws;
	struct drm_i915_gem_object *obj;
	struct i915_gem_context *ctx;
	u32 *seqno;
	u32 *batch;
};

static int hang_init(struct hang *h, struct drm_i915_private *i915)
{
	void *vaddr;
	int err;

	memset(h, 0, sizeof(*h));
	h->i915 = i915;

	h->ctx = kernel_context(i915);
	if (IS_ERR(h->ctx))
		return PTR_ERR(h->ctx);

	GEM_BUG_ON(i915_gem_context_is_bannable(h->ctx));

	h->hws = i915_gem_object_create_internal(i915, PAGE_SIZE);
	if (IS_ERR(h->hws)) {
		err = PTR_ERR(h->hws);
		goto err_ctx;
	}

	h->obj = i915_gem_object_create_internal(i915, PAGE_SIZE);
	if (IS_ERR(h->obj)) {
		err = PTR_ERR(h->obj);
		goto err_hws;
	}

	i915_gem_object_set_cache_coherency(h->hws, I915_CACHE_LLC);
	vaddr = i915_gem_object_pin_map(h->hws, I915_MAP_WB);
	if (IS_ERR(vaddr)) {
		err = PTR_ERR(vaddr);
		goto err_obj;
	}
	h->seqno = memset(vaddr, 0xff, PAGE_SIZE);

	vaddr = i915_gem_object_pin_map(h->obj,
					i915_coherent_map_type(i915));
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
err_ctx:
	kernel_context_close(h->ctx);
	return err;
}

static u64 hws_address(const struct i915_vma *hws,
		       const struct i915_request *rq)
{
	return hws->node.start + offset_in_page(sizeof(u32)*rq->fence.context);
}

static int move_to_active(struct i915_vma *vma,
			  struct i915_request *rq,
			  unsigned int flags)
{
	int err;

	i915_vma_lock(vma);
	err = i915_vma_move_to_active(vma, rq, flags);
	i915_vma_unlock(vma);

	return err;
}

static struct i915_request *
hang_create_request(struct hang *h, struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = h->i915;
	struct i915_address_space *vm = h->ctx->vm ?: &i915->ggtt.vm;
	struct i915_request *rq = NULL;
	struct i915_vma *hws, *vma;
	unsigned int flags;
	u32 *batch;
	int err;

	if (i915_gem_object_is_active(h->obj)) {
		struct drm_i915_gem_object *obj;
		void *vaddr;

		obj = i915_gem_object_create_internal(h->i915, PAGE_SIZE);
		if (IS_ERR(obj))
			return ERR_CAST(obj);

		vaddr = i915_gem_object_pin_map(obj,
						i915_coherent_map_type(h->i915));
		if (IS_ERR(vaddr)) {
			i915_gem_object_put(obj);
			return ERR_CAST(vaddr);
		}

		i915_gem_object_unpin_map(h->obj);
		i915_gem_object_put(h->obj);

		h->obj = obj;
		h->batch = vaddr;
	}

	vma = i915_vma_instance(h->obj, vm, NULL);
	if (IS_ERR(vma))
		return ERR_CAST(vma);

	hws = i915_vma_instance(h->hws, vm, NULL);
	if (IS_ERR(hws))
		return ERR_CAST(hws);

	err = i915_vma_pin(vma, 0, 0, PIN_USER);
	if (err)
		return ERR_PTR(err);

	err = i915_vma_pin(hws, 0, 0, PIN_USER);
	if (err)
		goto unpin_vma;

	rq = igt_request_alloc(h->ctx, engine);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto unpin_hws;
	}

	err = move_to_active(vma, rq, 0);
	if (err)
		goto cancel_rq;

	err = move_to_active(hws, rq, 0);
	if (err)
		goto cancel_rq;

	batch = h->batch;
	if (INTEL_GEN(i915) >= 8) {
		*batch++ = MI_STORE_DWORD_IMM_GEN4;
		*batch++ = lower_32_bits(hws_address(hws, rq));
		*batch++ = upper_32_bits(hws_address(hws, rq));
		*batch++ = rq->fence.seqno;
		*batch++ = MI_ARB_CHECK;

		memset(batch, 0, 1024);
		batch += 1024 / sizeof(*batch);

		*batch++ = MI_ARB_CHECK;
		*batch++ = MI_BATCH_BUFFER_START | 1 << 8 | 1;
		*batch++ = lower_32_bits(vma->node.start);
		*batch++ = upper_32_bits(vma->node.start);
	} else if (INTEL_GEN(i915) >= 6) {
		*batch++ = MI_STORE_DWORD_IMM_GEN4;
		*batch++ = 0;
		*batch++ = lower_32_bits(hws_address(hws, rq));
		*batch++ = rq->fence.seqno;
		*batch++ = MI_ARB_CHECK;

		memset(batch, 0, 1024);
		batch += 1024 / sizeof(*batch);

		*batch++ = MI_ARB_CHECK;
		*batch++ = MI_BATCH_BUFFER_START | 1 << 8;
		*batch++ = lower_32_bits(vma->node.start);
	} else if (INTEL_GEN(i915) >= 4) {
		*batch++ = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT;
		*batch++ = 0;
		*batch++ = lower_32_bits(hws_address(hws, rq));
		*batch++ = rq->fence.seqno;
		*batch++ = MI_ARB_CHECK;

		memset(batch, 0, 1024);
		batch += 1024 / sizeof(*batch);

		*batch++ = MI_ARB_CHECK;
		*batch++ = MI_BATCH_BUFFER_START | 2 << 6;
		*batch++ = lower_32_bits(vma->node.start);
	} else {
		*batch++ = MI_STORE_DWORD_IMM | MI_MEM_VIRTUAL;
		*batch++ = lower_32_bits(hws_address(hws, rq));
		*batch++ = rq->fence.seqno;
		*batch++ = MI_ARB_CHECK;

		memset(batch, 0, 1024);
		batch += 1024 / sizeof(*batch);

		*batch++ = MI_ARB_CHECK;
		*batch++ = MI_BATCH_BUFFER_START | 2 << 6;
		*batch++ = lower_32_bits(vma->node.start);
	}
	*batch++ = MI_BATCH_BUFFER_END; /* not reached */
	i915_gem_chipset_flush(h->i915);

	if (rq->engine->emit_init_breadcrumb) {
		err = rq->engine->emit_init_breadcrumb(rq);
		if (err)
			goto cancel_rq;
	}

	flags = 0;
	if (INTEL_GEN(vm->i915) <= 5)
		flags |= I915_DISPATCH_SECURE;

	err = rq->engine->emit_bb_start(rq, vma->node.start, PAGE_SIZE, flags);

cancel_rq:
	if (err) {
		i915_request_skip(rq, err);
		i915_request_add(rq);
	}
unpin_hws:
	i915_vma_unpin(hws);
unpin_vma:
	i915_vma_unpin(vma);
	return err ? ERR_PTR(err) : rq;
}

static u32 hws_seqno(const struct hang *h, const struct i915_request *rq)
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

	kernel_context_close(h->ctx);

	igt_flush_test(h->i915, I915_WAIT_LOCKED);
}

static bool wait_until_running(struct hang *h, struct i915_request *rq)
{
	return !(wait_for_us(i915_seqno_passed(hws_seqno(h, rq),
					       rq->fence.seqno),
			     10) &&
		 wait_for(i915_seqno_passed(hws_seqno(h, rq),
					    rq->fence.seqno),
			  1000));
}

static int igt_hang_sanitycheck(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct i915_request *rq;
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
		struct igt_wedge_me w;
		long timeout;

		if (!intel_engine_can_store_dword(engine))
			continue;

		rq = hang_create_request(&h, engine);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			pr_err("Failed to create request for %s, err=%d\n",
			       engine->name, err);
			goto fini;
		}

		i915_request_get(rq);

		*h.batch = MI_BATCH_BUFFER_END;
		i915_gem_chipset_flush(i915);

		i915_request_add(rq);

		timeout = 0;
		igt_wedge_on_timeout(&w, i915, HZ / 10 /* 100ms timeout*/)
			timeout = i915_request_wait(rq, 0,
						    MAX_SCHEDULE_TIMEOUT);
		if (i915_reset_failed(i915))
			timeout = -EIO;

		i915_request_put(rq);

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

static bool wait_for_idle(struct intel_engine_cs *engine)
{
	return wait_for(intel_engine_is_idle(engine), IGT_IDLE_TIMEOUT) == 0;
}

static int igt_reset_nop(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_engine_cs *engine;
	struct i915_gem_context *ctx;
	unsigned int reset_count, count;
	enum intel_engine_id id;
	intel_wakeref_t wakeref;
	struct drm_file *file;
	IGT_TIMEOUT(end_time);
	int err = 0;

	/* Check that we can reset during non-user portions of requests */

	file = mock_file(i915);
	if (IS_ERR(file))
		return PTR_ERR(file);

	mutex_lock(&i915->drm.struct_mutex);
	ctx = live_context(i915, file);
	mutex_unlock(&i915->drm.struct_mutex);
	if (IS_ERR(ctx)) {
		err = PTR_ERR(ctx);
		goto out;
	}

	i915_gem_context_clear_bannable(ctx);
	wakeref = intel_runtime_pm_get(&i915->runtime_pm);
	reset_count = i915_reset_count(&i915->gpu_error);
	count = 0;
	do {
		mutex_lock(&i915->drm.struct_mutex);
		for_each_engine(engine, i915, id) {
			int i;

			for (i = 0; i < 16; i++) {
				struct i915_request *rq;

				rq = igt_request_alloc(ctx, engine);
				if (IS_ERR(rq)) {
					err = PTR_ERR(rq);
					break;
				}

				i915_request_add(rq);
			}
		}
		mutex_unlock(&i915->drm.struct_mutex);

		igt_global_reset_lock(i915);
		i915_reset(i915, ALL_ENGINES, NULL);
		igt_global_reset_unlock(i915);
		if (i915_reset_failed(i915)) {
			err = -EIO;
			break;
		}

		if (i915_reset_count(&i915->gpu_error) !=
		    reset_count + ++count) {
			pr_err("Full GPU reset not recorded!\n");
			err = -EINVAL;
			break;
		}

		err = igt_flush_test(i915, 0);
		if (err)
			break;
	} while (time_before(jiffies, end_time));
	pr_info("%s: %d resets\n", __func__, count);

	mutex_lock(&i915->drm.struct_mutex);
	err = igt_flush_test(i915, I915_WAIT_LOCKED);
	mutex_unlock(&i915->drm.struct_mutex);

	intel_runtime_pm_put(&i915->runtime_pm, wakeref);

out:
	mock_file_free(i915, file);
	if (i915_reset_failed(i915))
		err = -EIO;
	return err;
}

static int igt_reset_nop_engine(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_engine_cs *engine;
	struct i915_gem_context *ctx;
	enum intel_engine_id id;
	intel_wakeref_t wakeref;
	struct drm_file *file;
	int err = 0;

	/* Check that we can engine-reset during non-user portions */

	if (!intel_has_reset_engine(i915))
		return 0;

	file = mock_file(i915);
	if (IS_ERR(file))
		return PTR_ERR(file);

	mutex_lock(&i915->drm.struct_mutex);
	ctx = live_context(i915, file);
	mutex_unlock(&i915->drm.struct_mutex);
	if (IS_ERR(ctx)) {
		err = PTR_ERR(ctx);
		goto out;
	}

	i915_gem_context_clear_bannable(ctx);
	wakeref = intel_runtime_pm_get(&i915->runtime_pm);
	for_each_engine(engine, i915, id) {
		unsigned int reset_count, reset_engine_count;
		unsigned int count;
		IGT_TIMEOUT(end_time);

		reset_count = i915_reset_count(&i915->gpu_error);
		reset_engine_count = i915_reset_engine_count(&i915->gpu_error,
							     engine);
		count = 0;

		set_bit(I915_RESET_ENGINE + id, &i915->gpu_error.flags);
		do {
			int i;

			if (!wait_for_idle(engine)) {
				pr_err("%s failed to idle before reset\n",
				       engine->name);
				err = -EIO;
				break;
			}

			mutex_lock(&i915->drm.struct_mutex);
			for (i = 0; i < 16; i++) {
				struct i915_request *rq;

				rq = igt_request_alloc(ctx, engine);
				if (IS_ERR(rq)) {
					err = PTR_ERR(rq);
					break;
				}

				i915_request_add(rq);
			}
			mutex_unlock(&i915->drm.struct_mutex);

			err = i915_reset_engine(engine, NULL);
			if (err) {
				pr_err("i915_reset_engine failed\n");
				break;
			}

			if (i915_reset_count(&i915->gpu_error) != reset_count) {
				pr_err("Full GPU reset recorded! (engine reset expected)\n");
				err = -EINVAL;
				break;
			}

			if (i915_reset_engine_count(&i915->gpu_error, engine) !=
			    reset_engine_count + ++count) {
				pr_err("%s engine reset not recorded!\n",
				       engine->name);
				err = -EINVAL;
				break;
			}
		} while (time_before(jiffies, end_time));
		clear_bit(I915_RESET_ENGINE + id, &i915->gpu_error.flags);
		pr_info("%s(%s): %d resets\n", __func__, engine->name, count);

		if (err)
			break;

		err = igt_flush_test(i915, 0);
		if (err)
			break;
	}

	mutex_lock(&i915->drm.struct_mutex);
	err = igt_flush_test(i915, I915_WAIT_LOCKED);
	mutex_unlock(&i915->drm.struct_mutex);

	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
out:
	mock_file_free(i915, file);
	if (i915_reset_failed(i915))
		err = -EIO;
	return err;
}

static int __igt_reset_engine(struct drm_i915_private *i915, bool active)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct hang h;
	int err = 0;

	/* Check that we can issue an engine reset on an idle engine (no-op) */

	if (!intel_has_reset_engine(i915))
		return 0;

	if (active) {
		mutex_lock(&i915->drm.struct_mutex);
		err = hang_init(&h, i915);
		mutex_unlock(&i915->drm.struct_mutex);
		if (err)
			return err;
	}

	for_each_engine(engine, i915, id) {
		unsigned int reset_count, reset_engine_count;
		IGT_TIMEOUT(end_time);

		if (active && !intel_engine_can_store_dword(engine))
			continue;

		if (!wait_for_idle(engine)) {
			pr_err("%s failed to idle before reset\n",
			       engine->name);
			err = -EIO;
			break;
		}

		reset_count = i915_reset_count(&i915->gpu_error);
		reset_engine_count = i915_reset_engine_count(&i915->gpu_error,
							     engine);

		intel_engine_pm_get(engine);
		set_bit(I915_RESET_ENGINE + id, &i915->gpu_error.flags);
		do {
			if (active) {
				struct i915_request *rq;

				mutex_lock(&i915->drm.struct_mutex);
				rq = hang_create_request(&h, engine);
				if (IS_ERR(rq)) {
					err = PTR_ERR(rq);
					mutex_unlock(&i915->drm.struct_mutex);
					break;
				}

				i915_request_get(rq);
				i915_request_add(rq);
				mutex_unlock(&i915->drm.struct_mutex);

				if (!wait_until_running(&h, rq)) {
					struct drm_printer p = drm_info_printer(i915->drm.dev);

					pr_err("%s: Failed to start request %llx, at %x\n",
					       __func__, rq->fence.seqno, hws_seqno(&h, rq));
					intel_engine_dump(engine, &p,
							  "%s\n", engine->name);

					i915_request_put(rq);
					err = -EIO;
					break;
				}

				i915_request_put(rq);
			}

			err = i915_reset_engine(engine, NULL);
			if (err) {
				pr_err("i915_reset_engine failed\n");
				break;
			}

			if (i915_reset_count(&i915->gpu_error) != reset_count) {
				pr_err("Full GPU reset recorded! (engine reset expected)\n");
				err = -EINVAL;
				break;
			}

			if (i915_reset_engine_count(&i915->gpu_error, engine) !=
			    ++reset_engine_count) {
				pr_err("%s engine reset not recorded!\n",
				       engine->name);
				err = -EINVAL;
				break;
			}
		} while (time_before(jiffies, end_time));
		clear_bit(I915_RESET_ENGINE + id, &i915->gpu_error.flags);
		intel_engine_pm_put(engine);

		if (err)
			break;

		err = igt_flush_test(i915, 0);
		if (err)
			break;
	}

	if (i915_reset_failed(i915))
		err = -EIO;

	if (active) {
		mutex_lock(&i915->drm.struct_mutex);
		hang_fini(&h);
		mutex_unlock(&i915->drm.struct_mutex);
	}

	return err;
}

static int igt_reset_idle_engine(void *arg)
{
	return __igt_reset_engine(arg, false);
}

static int igt_reset_active_engine(void *arg)
{
	return __igt_reset_engine(arg, true);
}

struct active_engine {
	struct task_struct *task;
	struct intel_engine_cs *engine;
	unsigned long resets;
	unsigned int flags;
};

#define TEST_ACTIVE	BIT(0)
#define TEST_OTHERS	BIT(1)
#define TEST_SELF	BIT(2)
#define TEST_PRIORITY	BIT(3)

static int active_request_put(struct i915_request *rq)
{
	int err = 0;

	if (!rq)
		return 0;

	if (i915_request_wait(rq, 0, 5 * HZ) < 0) {
		GEM_TRACE("%s timed out waiting for completion of fence %llx:%lld\n",
			  rq->engine->name,
			  rq->fence.context,
			  rq->fence.seqno);
		GEM_TRACE_DUMP();

		i915_gem_set_wedged(rq->i915);
		err = -EIO;
	}

	i915_request_put(rq);

	return err;
}

static int active_engine(void *data)
{
	I915_RND_STATE(prng);
	struct active_engine *arg = data;
	struct intel_engine_cs *engine = arg->engine;
	struct i915_request *rq[8] = {};
	struct i915_gem_context *ctx[ARRAY_SIZE(rq)];
	struct drm_file *file;
	unsigned long count = 0;
	int err = 0;

	file = mock_file(engine->i915);
	if (IS_ERR(file))
		return PTR_ERR(file);

	for (count = 0; count < ARRAY_SIZE(ctx); count++) {
		mutex_lock(&engine->i915->drm.struct_mutex);
		ctx[count] = live_context(engine->i915, file);
		mutex_unlock(&engine->i915->drm.struct_mutex);
		if (IS_ERR(ctx[count])) {
			err = PTR_ERR(ctx[count]);
			while (--count)
				i915_gem_context_put(ctx[count]);
			goto err_file;
		}
	}

	while (!kthread_should_stop()) {
		unsigned int idx = count++ & (ARRAY_SIZE(rq) - 1);
		struct i915_request *old = rq[idx];
		struct i915_request *new;

		mutex_lock(&engine->i915->drm.struct_mutex);
		new = igt_request_alloc(ctx[idx], engine);
		if (IS_ERR(new)) {
			mutex_unlock(&engine->i915->drm.struct_mutex);
			err = PTR_ERR(new);
			break;
		}

		if (arg->flags & TEST_PRIORITY)
			ctx[idx]->sched.priority =
				i915_prandom_u32_max_state(512, &prng);

		rq[idx] = i915_request_get(new);
		i915_request_add(new);
		mutex_unlock(&engine->i915->drm.struct_mutex);

		err = active_request_put(old);
		if (err)
			break;

		cond_resched();
	}

	for (count = 0; count < ARRAY_SIZE(rq); count++) {
		int err__ = active_request_put(rq[count]);

		/* Keep the first error */
		if (!err)
			err = err__;
	}

err_file:
	mock_file_free(engine->i915, file);
	return err;
}

static int __igt_reset_engines(struct drm_i915_private *i915,
			       const char *test_name,
			       unsigned int flags)
{
	struct intel_engine_cs *engine, *other;
	enum intel_engine_id id, tmp;
	struct hang h;
	int err = 0;

	/* Check that issuing a reset on one engine does not interfere
	 * with any other engine.
	 */

	if (!intel_has_reset_engine(i915))
		return 0;

	if (flags & TEST_ACTIVE) {
		mutex_lock(&i915->drm.struct_mutex);
		err = hang_init(&h, i915);
		mutex_unlock(&i915->drm.struct_mutex);
		if (err)
			return err;

		if (flags & TEST_PRIORITY)
			h.ctx->sched.priority = 1024;
	}

	for_each_engine(engine, i915, id) {
		struct active_engine threads[I915_NUM_ENGINES] = {};
		unsigned long global = i915_reset_count(&i915->gpu_error);
		unsigned long count = 0, reported;
		IGT_TIMEOUT(end_time);

		if (flags & TEST_ACTIVE &&
		    !intel_engine_can_store_dword(engine))
			continue;

		if (!wait_for_idle(engine)) {
			pr_err("i915_reset_engine(%s:%s): failed to idle before reset\n",
			       engine->name, test_name);
			err = -EIO;
			break;
		}

		memset(threads, 0, sizeof(threads));
		for_each_engine(other, i915, tmp) {
			struct task_struct *tsk;

			threads[tmp].resets =
				i915_reset_engine_count(&i915->gpu_error,
							other);

			if (!(flags & TEST_OTHERS))
				continue;

			if (other == engine && !(flags & TEST_SELF))
				continue;

			threads[tmp].engine = other;
			threads[tmp].flags = flags;

			tsk = kthread_run(active_engine, &threads[tmp],
					  "igt/%s", other->name);
			if (IS_ERR(tsk)) {
				err = PTR_ERR(tsk);
				goto unwind;
			}

			threads[tmp].task = tsk;
			get_task_struct(tsk);
		}

		intel_engine_pm_get(engine);
		set_bit(I915_RESET_ENGINE + id, &i915->gpu_error.flags);
		do {
			struct i915_request *rq = NULL;

			if (flags & TEST_ACTIVE) {
				mutex_lock(&i915->drm.struct_mutex);
				rq = hang_create_request(&h, engine);
				if (IS_ERR(rq)) {
					err = PTR_ERR(rq);
					mutex_unlock(&i915->drm.struct_mutex);
					break;
				}

				i915_request_get(rq);
				i915_request_add(rq);
				mutex_unlock(&i915->drm.struct_mutex);

				if (!wait_until_running(&h, rq)) {
					struct drm_printer p = drm_info_printer(i915->drm.dev);

					pr_err("%s: Failed to start request %llx, at %x\n",
					       __func__, rq->fence.seqno, hws_seqno(&h, rq));
					intel_engine_dump(engine, &p,
							  "%s\n", engine->name);

					i915_request_put(rq);
					err = -EIO;
					break;
				}
			}

			err = i915_reset_engine(engine, NULL);
			if (err) {
				pr_err("i915_reset_engine(%s:%s): failed, err=%d\n",
				       engine->name, test_name, err);
				break;
			}

			count++;

			if (rq) {
				if (i915_request_wait(rq, 0, HZ / 5) < 0) {
					struct drm_printer p =
						drm_info_printer(i915->drm.dev);

					pr_err("i915_reset_engine(%s:%s):"
					       " failed to complete request after reset\n",
					       engine->name, test_name);
					intel_engine_dump(engine, &p,
							  "%s\n", engine->name);
					i915_request_put(rq);

					GEM_TRACE_DUMP();
					i915_gem_set_wedged(i915);
					err = -EIO;
					break;
				}

				i915_request_put(rq);
			}

			if (!(flags & TEST_SELF) && !wait_for_idle(engine)) {
				struct drm_printer p =
					drm_info_printer(i915->drm.dev);

				pr_err("i915_reset_engine(%s:%s):"
				       " failed to idle after reset\n",
				       engine->name, test_name);
				intel_engine_dump(engine, &p,
						  "%s\n", engine->name);

				err = -EIO;
				break;
			}
		} while (time_before(jiffies, end_time));
		clear_bit(I915_RESET_ENGINE + id, &i915->gpu_error.flags);
		intel_engine_pm_put(engine);
		pr_info("i915_reset_engine(%s:%s): %lu resets\n",
			engine->name, test_name, count);

		reported = i915_reset_engine_count(&i915->gpu_error, engine);
		reported -= threads[engine->id].resets;
		if (reported != count) {
			pr_err("i915_reset_engine(%s:%s): reset %lu times, but reported %lu\n",
			       engine->name, test_name, count, reported);
			if (!err)
				err = -EINVAL;
		}

unwind:
		for_each_engine(other, i915, tmp) {
			int ret;

			if (!threads[tmp].task)
				continue;

			ret = kthread_stop(threads[tmp].task);
			if (ret) {
				pr_err("kthread for other engine %s failed, err=%d\n",
				       other->name, ret);
				if (!err)
					err = ret;
			}
			put_task_struct(threads[tmp].task);

			if (other != engine &&
			    threads[tmp].resets !=
			    i915_reset_engine_count(&i915->gpu_error, other)) {
				pr_err("Innocent engine %s was reset (count=%ld)\n",
				       other->name,
				       i915_reset_engine_count(&i915->gpu_error,
							       other) -
				       threads[tmp].resets);
				if (!err)
					err = -EINVAL;
			}
		}

		if (global != i915_reset_count(&i915->gpu_error)) {
			pr_err("Global reset (count=%ld)!\n",
			       i915_reset_count(&i915->gpu_error) - global);
			if (!err)
				err = -EINVAL;
		}

		if (err)
			break;

		mutex_lock(&i915->drm.struct_mutex);
		err = igt_flush_test(i915, I915_WAIT_LOCKED);
		mutex_unlock(&i915->drm.struct_mutex);
		if (err)
			break;
	}

	if (i915_reset_failed(i915))
		err = -EIO;

	if (flags & TEST_ACTIVE) {
		mutex_lock(&i915->drm.struct_mutex);
		hang_fini(&h);
		mutex_unlock(&i915->drm.struct_mutex);
	}

	return err;
}

static int igt_reset_engines(void *arg)
{
	static const struct {
		const char *name;
		unsigned int flags;
	} phases[] = {
		{ "idle", 0 },
		{ "active", TEST_ACTIVE },
		{ "others-idle", TEST_OTHERS },
		{ "others-active", TEST_OTHERS | TEST_ACTIVE },
		{
			"others-priority",
			TEST_OTHERS | TEST_ACTIVE | TEST_PRIORITY
		},
		{
			"self-priority",
			TEST_OTHERS | TEST_ACTIVE | TEST_PRIORITY | TEST_SELF,
		},
		{ }
	};
	struct drm_i915_private *i915 = arg;
	typeof(*phases) *p;
	int err;

	for (p = phases; p->name; p++) {
		if (p->flags & TEST_PRIORITY) {
			if (!(i915->caps.scheduler & I915_SCHEDULER_CAP_PRIORITY))
				continue;
		}

		err = __igt_reset_engines(arg, p->name, p->flags);
		if (err)
			return err;
	}

	return 0;
}

static u32 fake_hangcheck(struct drm_i915_private *i915,
			  intel_engine_mask_t mask)
{
	u32 count = i915_reset_count(&i915->gpu_error);

	i915_reset(i915, mask, NULL);

	return count;
}

static int igt_reset_wait(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct i915_request *rq;
	unsigned int reset_count;
	struct hang h;
	long timeout;
	int err;

	if (!intel_engine_can_store_dword(i915->engine[RCS0]))
		return 0;

	/* Check that we detect a stuck waiter and issue a reset */

	igt_global_reset_lock(i915);

	mutex_lock(&i915->drm.struct_mutex);
	err = hang_init(&h, i915);
	if (err)
		goto unlock;

	rq = hang_create_request(&h, i915->engine[RCS0]);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto fini;
	}

	i915_request_get(rq);
	i915_request_add(rq);

	if (!wait_until_running(&h, rq)) {
		struct drm_printer p = drm_info_printer(i915->drm.dev);

		pr_err("%s: Failed to start request %llx, at %x\n",
		       __func__, rq->fence.seqno, hws_seqno(&h, rq));
		intel_engine_dump(rq->engine, &p, "%s\n", rq->engine->name);

		i915_gem_set_wedged(i915);

		err = -EIO;
		goto out_rq;
	}

	reset_count = fake_hangcheck(i915, ALL_ENGINES);

	timeout = i915_request_wait(rq, 0, 10);
	if (timeout < 0) {
		pr_err("i915_request_wait failed on a stuck request: err=%ld\n",
		       timeout);
		err = timeout;
		goto out_rq;
	}

	if (i915_reset_count(&i915->gpu_error) == reset_count) {
		pr_err("No GPU reset recorded!\n");
		err = -EINVAL;
		goto out_rq;
	}

out_rq:
	i915_request_put(rq);
fini:
	hang_fini(&h);
unlock:
	mutex_unlock(&i915->drm.struct_mutex);
	igt_global_reset_unlock(i915);

	if (i915_reset_failed(i915))
		return -EIO;

	return err;
}

struct evict_vma {
	struct completion completion;
	struct i915_vma *vma;
};

static int evict_vma(void *data)
{
	struct evict_vma *arg = data;
	struct i915_address_space *vm = arg->vma->vm;
	struct drm_i915_private *i915 = vm->i915;
	struct drm_mm_node evict = arg->vma->node;
	int err;

	complete(&arg->completion);

	mutex_lock(&i915->drm.struct_mutex);
	err = i915_gem_evict_for_node(vm, &evict, 0);
	mutex_unlock(&i915->drm.struct_mutex);

	return err;
}

static int evict_fence(void *data)
{
	struct evict_vma *arg = data;
	struct drm_i915_private *i915 = arg->vma->vm->i915;
	int err;

	complete(&arg->completion);

	mutex_lock(&i915->drm.struct_mutex);

	/* Mark the fence register as dirty to force the mmio update. */
	err = i915_gem_object_set_tiling(arg->vma->obj, I915_TILING_Y, 512);
	if (err) {
		pr_err("Invalid Y-tiling settings; err:%d\n", err);
		goto out_unlock;
	}

	err = i915_vma_pin_fence(arg->vma);
	if (err) {
		pr_err("Unable to pin Y-tiled fence; err:%d\n", err);
		goto out_unlock;
	}

	i915_vma_unpin_fence(arg->vma);

out_unlock:
	mutex_unlock(&i915->drm.struct_mutex);

	return err;
}

static int __igt_reset_evict_vma(struct drm_i915_private *i915,
				 struct i915_address_space *vm,
				 int (*fn)(void *),
				 unsigned int flags)
{
	struct drm_i915_gem_object *obj;
	struct task_struct *tsk = NULL;
	struct i915_request *rq;
	struct evict_vma arg;
	struct hang h;
	int err;

	if (!intel_engine_can_store_dword(i915->engine[RCS0]))
		return 0;

	/* Check that we can recover an unbind stuck on a hanging request */

	mutex_lock(&i915->drm.struct_mutex);
	err = hang_init(&h, i915);
	if (err)
		goto unlock;

	obj = i915_gem_object_create_internal(i915, SZ_1M);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		goto fini;
	}

	if (flags & EXEC_OBJECT_NEEDS_FENCE) {
		err = i915_gem_object_set_tiling(obj, I915_TILING_X, 512);
		if (err) {
			pr_err("Invalid X-tiling settings; err:%d\n", err);
			goto out_obj;
		}
	}

	arg.vma = i915_vma_instance(obj, vm, NULL);
	if (IS_ERR(arg.vma)) {
		err = PTR_ERR(arg.vma);
		goto out_obj;
	}

	rq = hang_create_request(&h, i915->engine[RCS0]);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto out_obj;
	}

	err = i915_vma_pin(arg.vma, 0, 0,
			   i915_vma_is_ggtt(arg.vma) ?
			   PIN_GLOBAL | PIN_MAPPABLE :
			   PIN_USER);
	if (err) {
		i915_request_add(rq);
		goto out_obj;
	}

	if (flags & EXEC_OBJECT_NEEDS_FENCE) {
		err = i915_vma_pin_fence(arg.vma);
		if (err) {
			pr_err("Unable to pin X-tiled fence; err:%d\n", err);
			i915_vma_unpin(arg.vma);
			i915_request_add(rq);
			goto out_obj;
		}
	}

	i915_vma_lock(arg.vma);
	err = i915_vma_move_to_active(arg.vma, rq, flags);
	i915_vma_unlock(arg.vma);

	if (flags & EXEC_OBJECT_NEEDS_FENCE)
		i915_vma_unpin_fence(arg.vma);
	i915_vma_unpin(arg.vma);

	i915_request_get(rq);
	i915_request_add(rq);
	if (err)
		goto out_rq;

	mutex_unlock(&i915->drm.struct_mutex);

	if (!wait_until_running(&h, rq)) {
		struct drm_printer p = drm_info_printer(i915->drm.dev);

		pr_err("%s: Failed to start request %llx, at %x\n",
		       __func__, rq->fence.seqno, hws_seqno(&h, rq));
		intel_engine_dump(rq->engine, &p, "%s\n", rq->engine->name);

		i915_gem_set_wedged(i915);
		goto out_reset;
	}

	init_completion(&arg.completion);

	tsk = kthread_run(fn, &arg, "igt/evict_vma");
	if (IS_ERR(tsk)) {
		err = PTR_ERR(tsk);
		tsk = NULL;
		goto out_reset;
	}
	get_task_struct(tsk);

	wait_for_completion(&arg.completion);

	if (wait_for(!list_empty(&rq->fence.cb_list), 10)) {
		struct drm_printer p = drm_info_printer(i915->drm.dev);

		pr_err("igt/evict_vma kthread did not wait\n");
		intel_engine_dump(rq->engine, &p, "%s\n", rq->engine->name);

		i915_gem_set_wedged(i915);
		goto out_reset;
	}

out_reset:
	igt_global_reset_lock(i915);
	fake_hangcheck(rq->i915, rq->engine->mask);
	igt_global_reset_unlock(i915);

	if (tsk) {
		struct igt_wedge_me w;

		/* The reset, even indirectly, should take less than 10ms. */
		igt_wedge_on_timeout(&w, i915, HZ / 10 /* 100ms timeout*/)
			err = kthread_stop(tsk);

		put_task_struct(tsk);
	}

	mutex_lock(&i915->drm.struct_mutex);
out_rq:
	i915_request_put(rq);
out_obj:
	i915_gem_object_put(obj);
fini:
	hang_fini(&h);
unlock:
	mutex_unlock(&i915->drm.struct_mutex);

	if (i915_reset_failed(i915))
		return -EIO;

	return err;
}

static int igt_reset_evict_ggtt(void *arg)
{
	struct drm_i915_private *i915 = arg;

	return __igt_reset_evict_vma(i915, &i915->ggtt.vm,
				     evict_vma, EXEC_OBJECT_WRITE);
}

static int igt_reset_evict_ppgtt(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct i915_gem_context *ctx;
	struct drm_file *file;
	int err;

	file = mock_file(i915);
	if (IS_ERR(file))
		return PTR_ERR(file);

	mutex_lock(&i915->drm.struct_mutex);
	ctx = live_context(i915, file);
	mutex_unlock(&i915->drm.struct_mutex);
	if (IS_ERR(ctx)) {
		err = PTR_ERR(ctx);
		goto out;
	}

	err = 0;
	if (ctx->vm) /* aliasing == global gtt locking, covered above */
		err = __igt_reset_evict_vma(i915, ctx->vm,
					    evict_vma, EXEC_OBJECT_WRITE);

out:
	mock_file_free(i915, file);
	return err;
}

static int igt_reset_evict_fence(void *arg)
{
	struct drm_i915_private *i915 = arg;

	return __igt_reset_evict_vma(i915, &i915->ggtt.vm,
				     evict_fence, EXEC_OBJECT_NEEDS_FENCE);
}

static int wait_for_others(struct drm_i915_private *i915,
			   struct intel_engine_cs *exclude)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, i915, id) {
		if (engine == exclude)
			continue;

		if (!wait_for_idle(engine))
			return -EIO;
	}

	return 0;
}

static int igt_reset_queue(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct hang h;
	int err;

	/* Check that we replay pending requests following a hang */

	igt_global_reset_lock(i915);

	mutex_lock(&i915->drm.struct_mutex);
	err = hang_init(&h, i915);
	if (err)
		goto unlock;

	for_each_engine(engine, i915, id) {
		struct i915_request *prev;
		IGT_TIMEOUT(end_time);
		unsigned int count;

		if (!intel_engine_can_store_dword(engine))
			continue;

		prev = hang_create_request(&h, engine);
		if (IS_ERR(prev)) {
			err = PTR_ERR(prev);
			goto fini;
		}

		i915_request_get(prev);
		i915_request_add(prev);

		count = 0;
		do {
			struct i915_request *rq;
			unsigned int reset_count;

			rq = hang_create_request(&h, engine);
			if (IS_ERR(rq)) {
				err = PTR_ERR(rq);
				goto fini;
			}

			i915_request_get(rq);
			i915_request_add(rq);

			/*
			 * XXX We don't handle resetting the kernel context
			 * very well. If we trigger a device reset twice in
			 * quick succession while the kernel context is
			 * executing, we may end up skipping the breadcrumb.
			 * This is really only a problem for the selftest as
			 * normally there is a large interlude between resets
			 * (hangcheck), or we focus on resetting just one
			 * engine and so avoid repeatedly resetting innocents.
			 */
			err = wait_for_others(i915, engine);
			if (err) {
				pr_err("%s(%s): Failed to idle other inactive engines after device reset\n",
				       __func__, engine->name);
				i915_request_put(rq);
				i915_request_put(prev);

				GEM_TRACE_DUMP();
				i915_gem_set_wedged(i915);
				goto fini;
			}

			if (!wait_until_running(&h, prev)) {
				struct drm_printer p = drm_info_printer(i915->drm.dev);

				pr_err("%s(%s): Failed to start request %llx, at %x\n",
				       __func__, engine->name,
				       prev->fence.seqno, hws_seqno(&h, prev));
				intel_engine_dump(engine, &p,
						  "%s\n", engine->name);

				i915_request_put(rq);
				i915_request_put(prev);

				i915_gem_set_wedged(i915);

				err = -EIO;
				goto fini;
			}

			reset_count = fake_hangcheck(i915, BIT(id));

			if (prev->fence.error != -EIO) {
				pr_err("GPU reset not recorded on hanging request [fence.error=%d]!\n",
				       prev->fence.error);
				i915_request_put(rq);
				i915_request_put(prev);
				err = -EINVAL;
				goto fini;
			}

			if (rq->fence.error) {
				pr_err("Fence error status not zero [%d] after unrelated reset\n",
				       rq->fence.error);
				i915_request_put(rq);
				i915_request_put(prev);
				err = -EINVAL;
				goto fini;
			}

			if (i915_reset_count(&i915->gpu_error) == reset_count) {
				pr_err("No GPU reset recorded!\n");
				i915_request_put(rq);
				i915_request_put(prev);
				err = -EINVAL;
				goto fini;
			}

			i915_request_put(prev);
			prev = rq;
			count++;
		} while (time_before(jiffies, end_time));
		pr_info("%s: Completed %d resets\n", engine->name, count);

		*h.batch = MI_BATCH_BUFFER_END;
		i915_gem_chipset_flush(i915);

		i915_request_put(prev);

		err = igt_flush_test(i915, I915_WAIT_LOCKED);
		if (err)
			break;
	}

fini:
	hang_fini(&h);
unlock:
	mutex_unlock(&i915->drm.struct_mutex);
	igt_global_reset_unlock(i915);

	if (i915_reset_failed(i915))
		return -EIO;

	return err;
}

static int igt_handle_error(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_engine_cs *engine = i915->engine[RCS0];
	struct hang h;
	struct i915_request *rq;
	struct i915_gpu_state *error;
	int err;

	/* Check that we can issue a global GPU and engine reset */

	if (!intel_has_reset_engine(i915))
		return 0;

	if (!engine || !intel_engine_can_store_dword(engine))
		return 0;

	mutex_lock(&i915->drm.struct_mutex);

	err = hang_init(&h, i915);
	if (err)
		goto err_unlock;

	rq = hang_create_request(&h, engine);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_fini;
	}

	i915_request_get(rq);
	i915_request_add(rq);

	if (!wait_until_running(&h, rq)) {
		struct drm_printer p = drm_info_printer(i915->drm.dev);

		pr_err("%s: Failed to start request %llx, at %x\n",
		       __func__, rq->fence.seqno, hws_seqno(&h, rq));
		intel_engine_dump(rq->engine, &p, "%s\n", rq->engine->name);

		i915_gem_set_wedged(i915);

		err = -EIO;
		goto err_request;
	}

	mutex_unlock(&i915->drm.struct_mutex);

	/* Temporarily disable error capture */
	error = xchg(&i915->gpu_error.first_error, (void *)-1);

	i915_handle_error(i915, engine->mask, 0, NULL);

	xchg(&i915->gpu_error.first_error, error);

	mutex_lock(&i915->drm.struct_mutex);

	if (rq->fence.error != -EIO) {
		pr_err("Guilty request not identified!\n");
		err = -EINVAL;
		goto err_request;
	}

err_request:
	i915_request_put(rq);
err_fini:
	hang_fini(&h);
err_unlock:
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

static int __igt_atomic_reset_engine(struct intel_engine_cs *engine,
				     const struct igt_atomic_section *p,
				     const char *mode)
{
	struct tasklet_struct * const t = &engine->execlists.tasklet;
	int err;

	GEM_TRACE("i915_reset_engine(%s:%s) under %s\n",
		  engine->name, mode, p->name);

	tasklet_disable_nosync(t);
	p->critical_section_begin();

	err = i915_reset_engine(engine, NULL);

	p->critical_section_end();
	tasklet_enable(t);

	if (err)
		pr_err("i915_reset_engine(%s:%s) failed under %s\n",
		       engine->name, mode, p->name);

	return err;
}

static int igt_atomic_reset_engine(struct intel_engine_cs *engine,
				   const struct igt_atomic_section *p)
{
	struct drm_i915_private *i915 = engine->i915;
	struct i915_request *rq;
	struct hang h;
	int err;

	err = __igt_atomic_reset_engine(engine, p, "idle");
	if (err)
		return err;

	err = hang_init(&h, i915);
	if (err)
		return err;

	rq = hang_create_request(&h, engine);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto out;
	}

	i915_request_get(rq);
	i915_request_add(rq);

	if (wait_until_running(&h, rq)) {
		err = __igt_atomic_reset_engine(engine, p, "active");
	} else {
		pr_err("%s(%s): Failed to start request %llx, at %x\n",
		       __func__, engine->name,
		       rq->fence.seqno, hws_seqno(&h, rq));
		i915_gem_set_wedged(i915);
		err = -EIO;
	}

	if (err == 0) {
		struct igt_wedge_me w;

		igt_wedge_on_timeout(&w, i915, HZ / 20 /* 50ms timeout*/)
			i915_request_wait(rq, 0, MAX_SCHEDULE_TIMEOUT);
		if (i915_reset_failed(i915))
			err = -EIO;
	}

	i915_request_put(rq);
out:
	hang_fini(&h);
	return err;
}

static int igt_reset_engines_atomic(void *arg)
{
	struct drm_i915_private *i915 = arg;
	const typeof(*igt_atomic_phases) *p;
	int err = 0;

	/* Check that the engines resets are usable from atomic context */

	if (!intel_has_reset_engine(i915))
		return 0;

	if (USES_GUC_SUBMISSION(i915))
		return 0;

	igt_global_reset_lock(i915);
	mutex_lock(&i915->drm.struct_mutex);

	/* Flush any requests before we get started and check basics */
	if (!igt_force_reset(i915))
		goto unlock;

	for (p = igt_atomic_phases; p->name; p++) {
		struct intel_engine_cs *engine;
		enum intel_engine_id id;

		for_each_engine(engine, i915, id) {
			err = igt_atomic_reset_engine(engine, p);
			if (err)
				goto out;
		}
	}

out:
	/* As we poke around the guts, do a full reset before continuing. */
	igt_force_reset(i915);

unlock:
	mutex_unlock(&i915->drm.struct_mutex);
	igt_global_reset_unlock(i915);

	return err;
}

int intel_hangcheck_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_hang_sanitycheck),
		SUBTEST(igt_reset_nop),
		SUBTEST(igt_reset_nop_engine),
		SUBTEST(igt_reset_idle_engine),
		SUBTEST(igt_reset_active_engine),
		SUBTEST(igt_reset_engines),
		SUBTEST(igt_reset_engines_atomic),
		SUBTEST(igt_reset_queue),
		SUBTEST(igt_reset_wait),
		SUBTEST(igt_reset_evict_ggtt),
		SUBTEST(igt_reset_evict_ppgtt),
		SUBTEST(igt_reset_evict_fence),
		SUBTEST(igt_handle_error),
	};
	intel_wakeref_t wakeref;
	bool saved_hangcheck;
	int err;

	if (!intel_has_gpu_reset(i915))
		return 0;

	if (i915_terminally_wedged(i915))
		return -EIO; /* we're long past hope of a successful reset */

	wakeref = intel_runtime_pm_get(&i915->runtime_pm);
	saved_hangcheck = fetch_and_zero(&i915_modparams.enable_hangcheck);
	drain_delayed_work(&i915->gpu_error.hangcheck_work); /* flush param */

	err = i915_subtests(tests, i915);

	mutex_lock(&i915->drm.struct_mutex);
	igt_flush_test(i915, I915_WAIT_LOCKED);
	mutex_unlock(&i915->drm.struct_mutex);

	i915_modparams.enable_hangcheck = saved_hangcheck;
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);

	return err;
}
