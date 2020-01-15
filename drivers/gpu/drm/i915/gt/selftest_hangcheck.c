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

#include "intel_gt.h"
#include "intel_engine_heartbeat.h"
#include "intel_engine_pm.h"

#include "i915_selftest.h"
#include "selftests/i915_random.h"
#include "selftests/igt_flush_test.h"
#include "selftests/igt_reset.h"
#include "selftests/igt_atomic.h"

#include "selftests/mock_drm.h"

#include "gem/selftests/mock_context.h"
#include "gem/selftests/igt_gem_utils.h"

#define IGT_IDLE_TIMEOUT 50 /* ms; time to wait after flushing between tests */

struct hang {
	struct intel_gt *gt;
	struct drm_i915_gem_object *hws;
	struct drm_i915_gem_object *obj;
	struct i915_gem_context *ctx;
	u32 *seqno;
	u32 *batch;
};

static int hang_init(struct hang *h, struct intel_gt *gt)
{
	void *vaddr;
	int err;

	memset(h, 0, sizeof(*h));
	h->gt = gt;

	h->ctx = kernel_context(gt->i915);
	if (IS_ERR(h->ctx))
		return PTR_ERR(h->ctx);

	GEM_BUG_ON(i915_gem_context_is_bannable(h->ctx));

	h->hws = i915_gem_object_create_internal(gt->i915, PAGE_SIZE);
	if (IS_ERR(h->hws)) {
		err = PTR_ERR(h->hws);
		goto err_ctx;
	}

	h->obj = i915_gem_object_create_internal(gt->i915, PAGE_SIZE);
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
					i915_coherent_map_type(gt->i915));
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
	err = i915_request_await_object(rq, vma->obj,
					flags & EXEC_OBJECT_WRITE);
	if (err == 0)
		err = i915_vma_move_to_active(vma, rq, flags);
	i915_vma_unlock(vma);

	return err;
}

static struct i915_request *
hang_create_request(struct hang *h, struct intel_engine_cs *engine)
{
	struct intel_gt *gt = h->gt;
	struct i915_address_space *vm = i915_gem_context_get_vm_rcu(h->ctx);
	struct drm_i915_gem_object *obj;
	struct i915_request *rq = NULL;
	struct i915_vma *hws, *vma;
	unsigned int flags;
	void *vaddr;
	u32 *batch;
	int err;

	obj = i915_gem_object_create_internal(gt->i915, PAGE_SIZE);
	if (IS_ERR(obj)) {
		i915_vm_put(vm);
		return ERR_CAST(obj);
	}

	vaddr = i915_gem_object_pin_map(obj, i915_coherent_map_type(gt->i915));
	if (IS_ERR(vaddr)) {
		i915_gem_object_put(obj);
		i915_vm_put(vm);
		return ERR_CAST(vaddr);
	}

	i915_gem_object_unpin_map(h->obj);
	i915_gem_object_put(h->obj);

	h->obj = obj;
	h->batch = vaddr;

	vma = i915_vma_instance(h->obj, vm, NULL);
	if (IS_ERR(vma)) {
		i915_vm_put(vm);
		return ERR_CAST(vma);
	}

	hws = i915_vma_instance(h->hws, vm, NULL);
	if (IS_ERR(hws)) {
		i915_vm_put(vm);
		return ERR_CAST(hws);
	}

	err = i915_vma_pin(vma, 0, 0, PIN_USER);
	if (err) {
		i915_vm_put(vm);
		return ERR_PTR(err);
	}

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
	if (INTEL_GEN(gt->i915) >= 8) {
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
	} else if (INTEL_GEN(gt->i915) >= 6) {
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
	} else if (INTEL_GEN(gt->i915) >= 4) {
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
	intel_gt_chipset_flush(engine->gt);

	if (rq->engine->emit_init_breadcrumb) {
		err = rq->engine->emit_init_breadcrumb(rq);
		if (err)
			goto cancel_rq;
	}

	flags = 0;
	if (INTEL_GEN(gt->i915) <= 5)
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
	i915_vm_put(vm);
	return err ? ERR_PTR(err) : rq;
}

static u32 hws_seqno(const struct hang *h, const struct i915_request *rq)
{
	return READ_ONCE(h->seqno[rq->fence.context % (PAGE_SIZE/sizeof(u32))]);
}

static void hang_fini(struct hang *h)
{
	*h->batch = MI_BATCH_BUFFER_END;
	intel_gt_chipset_flush(h->gt);

	i915_gem_object_unpin_map(h->obj);
	i915_gem_object_put(h->obj);

	i915_gem_object_unpin_map(h->hws);
	i915_gem_object_put(h->hws);

	kernel_context_close(h->ctx);

	igt_flush_test(h->gt->i915);
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

static void engine_heartbeat_disable(struct intel_engine_cs *engine,
				     unsigned long *saved)
{
	*saved = engine->props.heartbeat_interval_ms;
	engine->props.heartbeat_interval_ms = 0;

	intel_engine_pm_get(engine);
	intel_engine_park_heartbeat(engine);
}

static void engine_heartbeat_enable(struct intel_engine_cs *engine,
				    unsigned long saved)
{
	intel_engine_pm_put(engine);

	engine->props.heartbeat_interval_ms = saved;
}

static int igt_hang_sanitycheck(void *arg)
{
	struct intel_gt *gt = arg;
	struct i915_request *rq;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct hang h;
	int err;

	/* Basic check that we can execute our hanging batch */

	err = hang_init(&h, gt);
	if (err)
		return err;

	for_each_engine(engine, gt, id) {
		struct intel_wedge_me w;
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
		intel_gt_chipset_flush(engine->gt);

		i915_request_add(rq);

		timeout = 0;
		intel_wedge_on_timeout(&w, gt, HZ / 10 /* 100ms */)
			timeout = i915_request_wait(rq, 0,
						    MAX_SCHEDULE_TIMEOUT);
		if (intel_gt_is_wedged(gt))
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
	return err;
}

static bool wait_for_idle(struct intel_engine_cs *engine)
{
	return wait_for(intel_engine_is_idle(engine), IGT_IDLE_TIMEOUT) == 0;
}

static int igt_reset_nop(void *arg)
{
	struct intel_gt *gt = arg;
	struct i915_gpu_error *global = &gt->i915->gpu_error;
	struct intel_engine_cs *engine;
	unsigned int reset_count, count;
	enum intel_engine_id id;
	IGT_TIMEOUT(end_time);
	int err = 0;

	/* Check that we can reset during non-user portions of requests */

	reset_count = i915_reset_count(global);
	count = 0;
	do {
		for_each_engine(engine, gt, id) {
			struct intel_context *ce;
			int i;

			ce = intel_context_create(engine);
			if (IS_ERR(ce)) {
				err = PTR_ERR(ce);
				break;
			}

			for (i = 0; i < 16; i++) {
				struct i915_request *rq;

				rq = intel_context_create_request(ce);
				if (IS_ERR(rq)) {
					err = PTR_ERR(rq);
					break;
				}

				i915_request_add(rq);
			}

			intel_context_put(ce);
		}

		igt_global_reset_lock(gt);
		intel_gt_reset(gt, ALL_ENGINES, NULL);
		igt_global_reset_unlock(gt);

		if (intel_gt_is_wedged(gt)) {
			err = -EIO;
			break;
		}

		if (i915_reset_count(global) != reset_count + ++count) {
			pr_err("Full GPU reset not recorded!\n");
			err = -EINVAL;
			break;
		}

		err = igt_flush_test(gt->i915);
		if (err)
			break;
	} while (time_before(jiffies, end_time));
	pr_info("%s: %d resets\n", __func__, count);

	if (igt_flush_test(gt->i915))
		err = -EIO;
	return err;
}

static int igt_reset_nop_engine(void *arg)
{
	struct intel_gt *gt = arg;
	struct i915_gpu_error *global = &gt->i915->gpu_error;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	/* Check that we can engine-reset during non-user portions */

	if (!intel_has_reset_engine(gt))
		return 0;

	for_each_engine(engine, gt, id) {
		unsigned int reset_count, reset_engine_count, count;
		struct intel_context *ce;
		unsigned long heartbeat;
		IGT_TIMEOUT(end_time);
		int err;

		ce = intel_context_create(engine);
		if (IS_ERR(ce))
			return PTR_ERR(ce);

		reset_count = i915_reset_count(global);
		reset_engine_count = i915_reset_engine_count(global, engine);
		count = 0;

		engine_heartbeat_disable(engine, &heartbeat);
		set_bit(I915_RESET_ENGINE + id, &gt->reset.flags);
		do {
			int i;

			if (!wait_for_idle(engine)) {
				pr_err("%s failed to idle before reset\n",
				       engine->name);
				err = -EIO;
				break;
			}

			for (i = 0; i < 16; i++) {
				struct i915_request *rq;

				rq = intel_context_create_request(ce);
				if (IS_ERR(rq)) {
					err = PTR_ERR(rq);
					break;
				}

				i915_request_add(rq);
			}
			err = intel_engine_reset(engine, NULL);
			if (err) {
				pr_err("i915_reset_engine failed\n");
				break;
			}

			if (i915_reset_count(global) != reset_count) {
				pr_err("Full GPU reset recorded! (engine reset expected)\n");
				err = -EINVAL;
				break;
			}

			if (i915_reset_engine_count(global, engine) !=
			    reset_engine_count + ++count) {
				pr_err("%s engine reset not recorded!\n",
				       engine->name);
				err = -EINVAL;
				break;
			}
		} while (time_before(jiffies, end_time));
		clear_bit(I915_RESET_ENGINE + id, &gt->reset.flags);
		engine_heartbeat_enable(engine, heartbeat);

		pr_info("%s(%s): %d resets\n", __func__, engine->name, count);

		intel_context_put(ce);
		if (igt_flush_test(gt->i915))
			err = -EIO;
		if (err)
			return err;
	}

	return 0;
}

static int __igt_reset_engine(struct intel_gt *gt, bool active)
{
	struct i915_gpu_error *global = &gt->i915->gpu_error;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct hang h;
	int err = 0;

	/* Check that we can issue an engine reset on an idle engine (no-op) */

	if (!intel_has_reset_engine(gt))
		return 0;

	if (active) {
		err = hang_init(&h, gt);
		if (err)
			return err;
	}

	for_each_engine(engine, gt, id) {
		unsigned int reset_count, reset_engine_count;
		unsigned long heartbeat;
		IGT_TIMEOUT(end_time);

		if (active && !intel_engine_can_store_dword(engine))
			continue;

		if (!wait_for_idle(engine)) {
			pr_err("%s failed to idle before reset\n",
			       engine->name);
			err = -EIO;
			break;
		}

		reset_count = i915_reset_count(global);
		reset_engine_count = i915_reset_engine_count(global, engine);

		engine_heartbeat_disable(engine, &heartbeat);
		set_bit(I915_RESET_ENGINE + id, &gt->reset.flags);
		do {
			if (active) {
				struct i915_request *rq;

				rq = hang_create_request(&h, engine);
				if (IS_ERR(rq)) {
					err = PTR_ERR(rq);
					break;
				}

				i915_request_get(rq);
				i915_request_add(rq);

				if (!wait_until_running(&h, rq)) {
					struct drm_printer p = drm_info_printer(gt->i915->drm.dev);

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

			err = intel_engine_reset(engine, NULL);
			if (err) {
				pr_err("i915_reset_engine failed\n");
				break;
			}

			if (i915_reset_count(global) != reset_count) {
				pr_err("Full GPU reset recorded! (engine reset expected)\n");
				err = -EINVAL;
				break;
			}

			if (i915_reset_engine_count(global, engine) !=
			    ++reset_engine_count) {
				pr_err("%s engine reset not recorded!\n",
				       engine->name);
				err = -EINVAL;
				break;
			}
		} while (time_before(jiffies, end_time));
		clear_bit(I915_RESET_ENGINE + id, &gt->reset.flags);
		engine_heartbeat_enable(engine, heartbeat);

		if (err)
			break;

		err = igt_flush_test(gt->i915);
		if (err)
			break;
	}

	if (intel_gt_is_wedged(gt))
		err = -EIO;

	if (active)
		hang_fini(&h);

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

		intel_gt_set_wedged(rq->engine->gt);
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
	struct intel_context *ce[ARRAY_SIZE(rq)];
	unsigned long count;
	int err = 0;

	for (count = 0; count < ARRAY_SIZE(ce); count++) {
		ce[count] = intel_context_create(engine);
		if (IS_ERR(ce[count])) {
			err = PTR_ERR(ce[count]);
			while (--count)
				intel_context_put(ce[count]);
			return err;
		}
	}

	count = 0;
	while (!kthread_should_stop()) {
		unsigned int idx = count++ & (ARRAY_SIZE(rq) - 1);
		struct i915_request *old = rq[idx];
		struct i915_request *new;

		new = intel_context_create_request(ce[idx]);
		if (IS_ERR(new)) {
			err = PTR_ERR(new);
			break;
		}

		rq[idx] = i915_request_get(new);
		i915_request_add(new);

		if (engine->schedule && arg->flags & TEST_PRIORITY) {
			struct i915_sched_attr attr = {
				.priority =
					i915_prandom_u32_max_state(512, &prng),
			};
			engine->schedule(rq[idx], &attr);
		}

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

		intel_context_put(ce[count]);
	}

	return err;
}

static int __igt_reset_engines(struct intel_gt *gt,
			       const char *test_name,
			       unsigned int flags)
{
	struct i915_gpu_error *global = &gt->i915->gpu_error;
	struct intel_engine_cs *engine, *other;
	enum intel_engine_id id, tmp;
	struct hang h;
	int err = 0;

	/* Check that issuing a reset on one engine does not interfere
	 * with any other engine.
	 */

	if (!intel_has_reset_engine(gt))
		return 0;

	if (flags & TEST_ACTIVE) {
		err = hang_init(&h, gt);
		if (err)
			return err;

		if (flags & TEST_PRIORITY)
			h.ctx->sched.priority = 1024;
	}

	for_each_engine(engine, gt, id) {
		struct active_engine threads[I915_NUM_ENGINES] = {};
		unsigned long device = i915_reset_count(global);
		unsigned long count = 0, reported;
		unsigned long heartbeat;
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
		for_each_engine(other, gt, tmp) {
			struct task_struct *tsk;

			threads[tmp].resets =
				i915_reset_engine_count(global, other);

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

		yield(); /* start all threads before we begin */

		engine_heartbeat_disable(engine, &heartbeat);
		set_bit(I915_RESET_ENGINE + id, &gt->reset.flags);
		do {
			struct i915_request *rq = NULL;

			if (flags & TEST_ACTIVE) {
				rq = hang_create_request(&h, engine);
				if (IS_ERR(rq)) {
					err = PTR_ERR(rq);
					break;
				}

				i915_request_get(rq);
				i915_request_add(rq);

				if (!wait_until_running(&h, rq)) {
					struct drm_printer p = drm_info_printer(gt->i915->drm.dev);

					pr_err("%s: Failed to start request %llx, at %x\n",
					       __func__, rq->fence.seqno, hws_seqno(&h, rq));
					intel_engine_dump(engine, &p,
							  "%s\n", engine->name);

					i915_request_put(rq);
					err = -EIO;
					break;
				}
			}

			err = intel_engine_reset(engine, NULL);
			if (err) {
				pr_err("i915_reset_engine(%s:%s): failed, err=%d\n",
				       engine->name, test_name, err);
				break;
			}

			count++;

			if (rq) {
				if (i915_request_wait(rq, 0, HZ / 5) < 0) {
					struct drm_printer p =
						drm_info_printer(gt->i915->drm.dev);

					pr_err("i915_reset_engine(%s:%s):"
					       " failed to complete request after reset\n",
					       engine->name, test_name);
					intel_engine_dump(engine, &p,
							  "%s\n", engine->name);
					i915_request_put(rq);

					GEM_TRACE_DUMP();
					intel_gt_set_wedged(gt);
					err = -EIO;
					break;
				}

				i915_request_put(rq);
			}

			if (!(flags & TEST_SELF) && !wait_for_idle(engine)) {
				struct drm_printer p =
					drm_info_printer(gt->i915->drm.dev);

				pr_err("i915_reset_engine(%s:%s):"
				       " failed to idle after reset\n",
				       engine->name, test_name);
				intel_engine_dump(engine, &p,
						  "%s\n", engine->name);

				err = -EIO;
				break;
			}
		} while (time_before(jiffies, end_time));
		clear_bit(I915_RESET_ENGINE + id, &gt->reset.flags);
		engine_heartbeat_enable(engine, heartbeat);

		pr_info("i915_reset_engine(%s:%s): %lu resets\n",
			engine->name, test_name, count);

		reported = i915_reset_engine_count(global, engine);
		reported -= threads[engine->id].resets;
		if (reported != count) {
			pr_err("i915_reset_engine(%s:%s): reset %lu times, but reported %lu\n",
			       engine->name, test_name, count, reported);
			if (!err)
				err = -EINVAL;
		}

unwind:
		for_each_engine(other, gt, tmp) {
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

			if (other->uabi_class != engine->uabi_class &&
			    threads[tmp].resets !=
			    i915_reset_engine_count(global, other)) {
				pr_err("Innocent engine %s was reset (count=%ld)\n",
				       other->name,
				       i915_reset_engine_count(global, other) -
				       threads[tmp].resets);
				if (!err)
					err = -EINVAL;
			}
		}

		if (device != i915_reset_count(global)) {
			pr_err("Global reset (count=%ld)!\n",
			       i915_reset_count(global) - device);
			if (!err)
				err = -EINVAL;
		}

		if (err)
			break;

		err = igt_flush_test(gt->i915);
		if (err)
			break;
	}

	if (intel_gt_is_wedged(gt))
		err = -EIO;

	if (flags & TEST_ACTIVE)
		hang_fini(&h);

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
	struct intel_gt *gt = arg;
	typeof(*phases) *p;
	int err;

	for (p = phases; p->name; p++) {
		if (p->flags & TEST_PRIORITY) {
			if (!(gt->i915->caps.scheduler & I915_SCHEDULER_CAP_PRIORITY))
				continue;
		}

		err = __igt_reset_engines(arg, p->name, p->flags);
		if (err)
			return err;
	}

	return 0;
}

static u32 fake_hangcheck(struct intel_gt *gt, intel_engine_mask_t mask)
{
	u32 count = i915_reset_count(&gt->i915->gpu_error);

	intel_gt_reset(gt, mask, NULL);

	return count;
}

static int igt_reset_wait(void *arg)
{
	struct intel_gt *gt = arg;
	struct i915_gpu_error *global = &gt->i915->gpu_error;
	struct intel_engine_cs *engine = gt->engine[RCS0];
	struct i915_request *rq;
	unsigned int reset_count;
	struct hang h;
	long timeout;
	int err;

	if (!engine || !intel_engine_can_store_dword(engine))
		return 0;

	/* Check that we detect a stuck waiter and issue a reset */

	igt_global_reset_lock(gt);

	err = hang_init(&h, gt);
	if (err)
		goto unlock;

	rq = hang_create_request(&h, engine);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto fini;
	}

	i915_request_get(rq);
	i915_request_add(rq);

	if (!wait_until_running(&h, rq)) {
		struct drm_printer p = drm_info_printer(gt->i915->drm.dev);

		pr_err("%s: Failed to start request %llx, at %x\n",
		       __func__, rq->fence.seqno, hws_seqno(&h, rq));
		intel_engine_dump(rq->engine, &p, "%s\n", rq->engine->name);

		intel_gt_set_wedged(gt);

		err = -EIO;
		goto out_rq;
	}

	reset_count = fake_hangcheck(gt, ALL_ENGINES);

	timeout = i915_request_wait(rq, 0, 10);
	if (timeout < 0) {
		pr_err("i915_request_wait failed on a stuck request: err=%ld\n",
		       timeout);
		err = timeout;
		goto out_rq;
	}

	if (i915_reset_count(global) == reset_count) {
		pr_err("No GPU reset recorded!\n");
		err = -EINVAL;
		goto out_rq;
	}

out_rq:
	i915_request_put(rq);
fini:
	hang_fini(&h);
unlock:
	igt_global_reset_unlock(gt);

	if (intel_gt_is_wedged(gt))
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
	struct drm_mm_node evict = arg->vma->node;
	int err;

	complete(&arg->completion);

	mutex_lock(&vm->mutex);
	err = i915_gem_evict_for_node(vm, &evict, 0);
	mutex_unlock(&vm->mutex);

	return err;
}

static int evict_fence(void *data)
{
	struct evict_vma *arg = data;
	int err;

	complete(&arg->completion);

	/* Mark the fence register as dirty to force the mmio update. */
	err = i915_gem_object_set_tiling(arg->vma->obj, I915_TILING_Y, 512);
	if (err) {
		pr_err("Invalid Y-tiling settings; err:%d\n", err);
		return err;
	}

	err = i915_vma_pin(arg->vma, 0, 0, PIN_GLOBAL | PIN_MAPPABLE);
	if (err) {
		pr_err("Unable to pin vma for Y-tiled fence; err:%d\n", err);
		return err;
	}

	err = i915_vma_pin_fence(arg->vma);
	i915_vma_unpin(arg->vma);
	if (err) {
		pr_err("Unable to pin Y-tiled fence; err:%d\n", err);
		return err;
	}

	i915_vma_unpin_fence(arg->vma);

	return 0;
}

static int __igt_reset_evict_vma(struct intel_gt *gt,
				 struct i915_address_space *vm,
				 int (*fn)(void *),
				 unsigned int flags)
{
	struct intel_engine_cs *engine = gt->engine[RCS0];
	struct drm_i915_gem_object *obj;
	struct task_struct *tsk = NULL;
	struct i915_request *rq;
	struct evict_vma arg;
	struct hang h;
	unsigned int pin_flags;
	int err;

	if (!gt->ggtt->num_fences && flags & EXEC_OBJECT_NEEDS_FENCE)
		return 0;

	if (!engine || !intel_engine_can_store_dword(engine))
		return 0;

	/* Check that we can recover an unbind stuck on a hanging request */

	err = hang_init(&h, gt);
	if (err)
		return err;

	obj = i915_gem_object_create_internal(gt->i915, SZ_1M);
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

	rq = hang_create_request(&h, engine);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto out_obj;
	}

	pin_flags = i915_vma_is_ggtt(arg.vma) ? PIN_GLOBAL : PIN_USER;

	if (flags & EXEC_OBJECT_NEEDS_FENCE)
		pin_flags |= PIN_MAPPABLE;

	err = i915_vma_pin(arg.vma, 0, 0, pin_flags);
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
	err = i915_request_await_object(rq, arg.vma->obj,
					flags & EXEC_OBJECT_WRITE);
	if (err == 0)
		err = i915_vma_move_to_active(arg.vma, rq, flags);
	i915_vma_unlock(arg.vma);

	if (flags & EXEC_OBJECT_NEEDS_FENCE)
		i915_vma_unpin_fence(arg.vma);
	i915_vma_unpin(arg.vma);

	i915_request_get(rq);
	i915_request_add(rq);
	if (err)
		goto out_rq;

	if (!wait_until_running(&h, rq)) {
		struct drm_printer p = drm_info_printer(gt->i915->drm.dev);

		pr_err("%s: Failed to start request %llx, at %x\n",
		       __func__, rq->fence.seqno, hws_seqno(&h, rq));
		intel_engine_dump(rq->engine, &p, "%s\n", rq->engine->name);

		intel_gt_set_wedged(gt);
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
		struct drm_printer p = drm_info_printer(gt->i915->drm.dev);

		pr_err("igt/evict_vma kthread did not wait\n");
		intel_engine_dump(rq->engine, &p, "%s\n", rq->engine->name);

		intel_gt_set_wedged(gt);
		goto out_reset;
	}

out_reset:
	igt_global_reset_lock(gt);
	fake_hangcheck(gt, rq->engine->mask);
	igt_global_reset_unlock(gt);

	if (tsk) {
		struct intel_wedge_me w;

		/* The reset, even indirectly, should take less than 10ms. */
		intel_wedge_on_timeout(&w, gt, HZ / 10 /* 100ms */)
			err = kthread_stop(tsk);

		put_task_struct(tsk);
	}

out_rq:
	i915_request_put(rq);
out_obj:
	i915_gem_object_put(obj);
fini:
	hang_fini(&h);
	if (intel_gt_is_wedged(gt))
		return -EIO;

	return err;
}

static int igt_reset_evict_ggtt(void *arg)
{
	struct intel_gt *gt = arg;

	return __igt_reset_evict_vma(gt, &gt->ggtt->vm,
				     evict_vma, EXEC_OBJECT_WRITE);
}

static int igt_reset_evict_ppgtt(void *arg)
{
	struct intel_gt *gt = arg;
	struct i915_ppgtt *ppgtt;
	int err;

	/* aliasing == global gtt locking, covered above */
	if (INTEL_PPGTT(gt->i915) < INTEL_PPGTT_FULL)
		return 0;

	ppgtt = i915_ppgtt_create(gt);
	if (IS_ERR(ppgtt))
		return PTR_ERR(ppgtt);

	err = __igt_reset_evict_vma(gt, &ppgtt->vm,
				    evict_vma, EXEC_OBJECT_WRITE);
	i915_vm_put(&ppgtt->vm);

	return err;
}

static int igt_reset_evict_fence(void *arg)
{
	struct intel_gt *gt = arg;

	return __igt_reset_evict_vma(gt, &gt->ggtt->vm,
				     evict_fence, EXEC_OBJECT_NEEDS_FENCE);
}

static int wait_for_others(struct intel_gt *gt,
			   struct intel_engine_cs *exclude)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, gt, id) {
		if (engine == exclude)
			continue;

		if (!wait_for_idle(engine))
			return -EIO;
	}

	return 0;
}

static int igt_reset_queue(void *arg)
{
	struct intel_gt *gt = arg;
	struct i915_gpu_error *global = &gt->i915->gpu_error;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct hang h;
	int err;

	/* Check that we replay pending requests following a hang */

	igt_global_reset_lock(gt);

	err = hang_init(&h, gt);
	if (err)
		goto unlock;

	for_each_engine(engine, gt, id) {
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
			err = wait_for_others(gt, engine);
			if (err) {
				pr_err("%s(%s): Failed to idle other inactive engines after device reset\n",
				       __func__, engine->name);
				i915_request_put(rq);
				i915_request_put(prev);

				GEM_TRACE_DUMP();
				intel_gt_set_wedged(gt);
				goto fini;
			}

			if (!wait_until_running(&h, prev)) {
				struct drm_printer p = drm_info_printer(gt->i915->drm.dev);

				pr_err("%s(%s): Failed to start request %llx, at %x\n",
				       __func__, engine->name,
				       prev->fence.seqno, hws_seqno(&h, prev));
				intel_engine_dump(engine, &p,
						  "%s\n", engine->name);

				i915_request_put(rq);
				i915_request_put(prev);

				intel_gt_set_wedged(gt);

				err = -EIO;
				goto fini;
			}

			reset_count = fake_hangcheck(gt, BIT(id));

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

			if (i915_reset_count(global) == reset_count) {
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
		intel_gt_chipset_flush(engine->gt);

		i915_request_put(prev);

		err = igt_flush_test(gt->i915);
		if (err)
			break;
	}

fini:
	hang_fini(&h);
unlock:
	igt_global_reset_unlock(gt);

	if (intel_gt_is_wedged(gt))
		return -EIO;

	return err;
}

static int igt_handle_error(void *arg)
{
	struct intel_gt *gt = arg;
	struct i915_gpu_error *global = &gt->i915->gpu_error;
	struct intel_engine_cs *engine = gt->engine[RCS0];
	struct hang h;
	struct i915_request *rq;
	struct i915_gpu_coredump *error;
	int err;

	/* Check that we can issue a global GPU and engine reset */

	if (!intel_has_reset_engine(gt))
		return 0;

	if (!engine || !intel_engine_can_store_dword(engine))
		return 0;

	err = hang_init(&h, gt);
	if (err)
		return err;

	rq = hang_create_request(&h, engine);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_fini;
	}

	i915_request_get(rq);
	i915_request_add(rq);

	if (!wait_until_running(&h, rq)) {
		struct drm_printer p = drm_info_printer(gt->i915->drm.dev);

		pr_err("%s: Failed to start request %llx, at %x\n",
		       __func__, rq->fence.seqno, hws_seqno(&h, rq));
		intel_engine_dump(rq->engine, &p, "%s\n", rq->engine->name);

		intel_gt_set_wedged(gt);

		err = -EIO;
		goto err_request;
	}

	/* Temporarily disable error capture */
	error = xchg(&global->first_error, (void *)-1);

	intel_gt_handle_error(gt, engine->mask, 0, NULL);

	xchg(&global->first_error, error);

	if (rq->fence.error != -EIO) {
		pr_err("Guilty request not identified!\n");
		err = -EINVAL;
		goto err_request;
	}

err_request:
	i915_request_put(rq);
err_fini:
	hang_fini(&h);
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

	tasklet_disable(t);
	p->critical_section_begin();

	err = intel_engine_reset(engine, NULL);

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
	struct i915_request *rq;
	struct hang h;
	int err;

	err = __igt_atomic_reset_engine(engine, p, "idle");
	if (err)
		return err;

	err = hang_init(&h, engine->gt);
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
		intel_gt_set_wedged(engine->gt);
		err = -EIO;
	}

	if (err == 0) {
		struct intel_wedge_me w;

		intel_wedge_on_timeout(&w, engine->gt, HZ / 20 /* 50ms */)
			i915_request_wait(rq, 0, MAX_SCHEDULE_TIMEOUT);
		if (intel_gt_is_wedged(engine->gt))
			err = -EIO;
	}

	i915_request_put(rq);
out:
	hang_fini(&h);
	return err;
}

static int igt_reset_engines_atomic(void *arg)
{
	struct intel_gt *gt = arg;
	const typeof(*igt_atomic_phases) *p;
	int err = 0;

	/* Check that the engines resets are usable from atomic context */

	if (!intel_has_reset_engine(gt))
		return 0;

	if (USES_GUC_SUBMISSION(gt->i915))
		return 0;

	igt_global_reset_lock(gt);

	/* Flush any requests before we get started and check basics */
	if (!igt_force_reset(gt))
		goto unlock;

	for (p = igt_atomic_phases; p->name; p++) {
		struct intel_engine_cs *engine;
		enum intel_engine_id id;

		for_each_engine(engine, gt, id) {
			err = igt_atomic_reset_engine(engine, p);
			if (err)
				goto out;
		}
	}

out:
	/* As we poke around the guts, do a full reset before continuing. */
	igt_force_reset(gt);
unlock:
	igt_global_reset_unlock(gt);

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
	struct intel_gt *gt = &i915->gt;
	intel_wakeref_t wakeref;
	int err;

	if (!intel_has_gpu_reset(gt))
		return 0;

	if (intel_gt_is_wedged(gt))
		return -EIO; /* we're long past hope of a successful reset */

	wakeref = intel_runtime_pm_get(gt->uncore->rpm);

	err = intel_gt_live_subtests(tests, gt);

	intel_runtime_pm_put(gt->uncore->rpm, wakeref);

	return err;
}
