// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <linux/sort.h>

#include "gt/intel_gt.h"
#include "gt/intel_engine_user.h"

#include "i915_selftest.h"

#include "gem/i915_gem_context.h"
#include "selftests/igt_flush_test.h"
#include "selftests/i915_random.h"
#include "selftests/mock_drm.h"
#include "huge_gem_object.h"
#include "mock_context.h"

static int wrap_ktime_compare(const void *A, const void *B)
{
	const ktime_t *a = A, *b = B;

	return ktime_compare(*a, *b);
}

static int __perf_fill_blt(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	int inst = 0;

	do {
		struct intel_engine_cs *engine;
		ktime_t t[5];
		int pass;
		int err;

		engine = intel_engine_lookup_user(i915,
						  I915_ENGINE_CLASS_COPY,
						  inst++);
		if (!engine)
			return 0;

		intel_engine_pm_get(engine);
		for (pass = 0; pass < ARRAY_SIZE(t); pass++) {
			struct intel_context *ce = engine->kernel_context;
			ktime_t t0, t1;

			t0 = ktime_get();

			err = i915_gem_object_fill_blt(obj, ce, 0);
			if (err)
				break;

			err = i915_gem_object_wait(obj,
						   I915_WAIT_ALL,
						   MAX_SCHEDULE_TIMEOUT);
			if (err)
				break;

			t1 = ktime_get();
			t[pass] = ktime_sub(t1, t0);
		}
		intel_engine_pm_put(engine);
		if (err)
			return err;

		sort(t, ARRAY_SIZE(t), sizeof(*t), wrap_ktime_compare, NULL);
		pr_info("%s: blt %zd KiB fill: %lld MiB/s\n",
			engine->name,
			obj->base.size >> 10,
			div64_u64(mul_u32_u32(4 * obj->base.size,
					      1000 * 1000 * 1000),
				  t[1] + 2 * t[2] + t[3]) >> 20);
	} while (1);
}

static int perf_fill_blt(void *arg)
{
	struct drm_i915_private *i915 = arg;
	static const unsigned long sizes[] = {
		SZ_4K,
		SZ_64K,
		SZ_2M,
		SZ_64M
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(sizes); i++) {
		struct drm_i915_gem_object *obj;
		int err;

		obj = i915_gem_object_create_internal(i915, sizes[i]);
		if (IS_ERR(obj))
			return PTR_ERR(obj);

		err = __perf_fill_blt(obj);
		i915_gem_object_put(obj);
		if (err)
			return err;
	}

	return 0;
}

static int __perf_copy_blt(struct drm_i915_gem_object *src,
			   struct drm_i915_gem_object *dst)
{
	struct drm_i915_private *i915 = to_i915(src->base.dev);
	int inst = 0;

	do {
		struct intel_engine_cs *engine;
		ktime_t t[5];
		int pass;
		int err = 0;

		engine = intel_engine_lookup_user(i915,
						  I915_ENGINE_CLASS_COPY,
						  inst++);
		if (!engine)
			return 0;

		intel_engine_pm_get(engine);
		for (pass = 0; pass < ARRAY_SIZE(t); pass++) {
			struct intel_context *ce = engine->kernel_context;
			ktime_t t0, t1;

			t0 = ktime_get();

			err = i915_gem_object_copy_blt(src, dst, ce);
			if (err)
				break;

			err = i915_gem_object_wait(dst,
						   I915_WAIT_ALL,
						   MAX_SCHEDULE_TIMEOUT);
			if (err)
				break;

			t1 = ktime_get();
			t[pass] = ktime_sub(t1, t0);
		}
		intel_engine_pm_put(engine);
		if (err)
			return err;

		sort(t, ARRAY_SIZE(t), sizeof(*t), wrap_ktime_compare, NULL);
		pr_info("%s: blt %zd KiB copy: %lld MiB/s\n",
			engine->name,
			src->base.size >> 10,
			div64_u64(mul_u32_u32(4 * src->base.size,
					      1000 * 1000 * 1000),
				  t[1] + 2 * t[2] + t[3]) >> 20);
	} while (1);
}

static int perf_copy_blt(void *arg)
{
	struct drm_i915_private *i915 = arg;
	static const unsigned long sizes[] = {
		SZ_4K,
		SZ_64K,
		SZ_2M,
		SZ_64M
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(sizes); i++) {
		struct drm_i915_gem_object *src, *dst;
		int err;

		src = i915_gem_object_create_internal(i915, sizes[i]);
		if (IS_ERR(src))
			return PTR_ERR(src);

		dst = i915_gem_object_create_internal(i915, sizes[i]);
		if (IS_ERR(dst)) {
			err = PTR_ERR(dst);
			goto err_src;
		}

		err = __perf_copy_blt(src, dst);

		i915_gem_object_put(dst);
err_src:
		i915_gem_object_put(src);
		if (err)
			return err;
	}

	return 0;
}

struct igt_thread_arg {
	struct drm_i915_private *i915;
	struct i915_gem_context *ctx;
	struct file *file;
	struct rnd_state prng;
	unsigned int n_cpus;
};

static int igt_fill_blt_thread(void *arg)
{
	struct igt_thread_arg *thread = arg;
	struct drm_i915_private *i915 = thread->i915;
	struct rnd_state *prng = &thread->prng;
	struct drm_i915_gem_object *obj;
	struct i915_gem_context *ctx;
	struct intel_context *ce;
	unsigned int prio;
	IGT_TIMEOUT(end);
	u64 total, max;
	int err;

	ctx = thread->ctx;
	if (!ctx) {
		ctx = live_context(i915, thread->file);
		if (IS_ERR(ctx))
			return PTR_ERR(ctx);

		prio = i915_prandom_u32_max_state(I915_PRIORITY_MAX, prng);
		ctx->sched.priority = I915_USER_PRIORITY(prio);
	}

	ce = i915_gem_context_get_engine(ctx, BCS0);
	GEM_BUG_ON(IS_ERR(ce));

	/*
	 * If we have a tiny shared address space, like for the GGTT
	 * then we can't be too greedy.
	 */
	max = ce->vm->total;
	if (i915_is_ggtt(ce->vm) || thread->ctx)
		max = div_u64(max, thread->n_cpus);
	max >>= 4;

	total = PAGE_SIZE;
	do {
		/* Aim to keep the runtime under reasonable bounds! */
		const u32 max_phys_size = SZ_64K;
		u32 val = prandom_u32_state(prng);
		u32 phys_sz;
		u32 sz;
		u32 *vaddr;
		u32 i;

		total = min(total, max);
		sz = i915_prandom_u32_max_state(total, prng) + 1;
		phys_sz = sz % max_phys_size + 1;

		sz = round_up(sz, PAGE_SIZE);
		phys_sz = round_up(phys_sz, PAGE_SIZE);
		phys_sz = min(phys_sz, sz);

		pr_debug("%s with phys_sz= %x, sz=%x, val=%x\n", __func__,
			 phys_sz, sz, val);

		obj = huge_gem_object(i915, phys_sz, sz);
		if (IS_ERR(obj)) {
			err = PTR_ERR(obj);
			goto err_flush;
		}

		vaddr = i915_gem_object_pin_map(obj, I915_MAP_WB);
		if (IS_ERR(vaddr)) {
			err = PTR_ERR(vaddr);
			goto err_put;
		}

		/*
		 * Make sure the potentially async clflush does its job, if
		 * required.
		 */
		memset32(vaddr, val ^ 0xdeadbeaf,
			 huge_gem_object_phys_size(obj) / sizeof(u32));

		if (!(obj->cache_coherent & I915_BO_CACHE_COHERENT_FOR_WRITE))
			obj->cache_dirty = true;

		err = i915_gem_object_fill_blt(obj, ce, val);
		if (err)
			goto err_unpin;

		err = i915_gem_object_wait(obj, 0, MAX_SCHEDULE_TIMEOUT);
		if (err)
			goto err_unpin;

		for (i = 0; i < huge_gem_object_phys_size(obj) / sizeof(u32); i += 17) {
			if (!(obj->cache_coherent & I915_BO_CACHE_COHERENT_FOR_READ))
				drm_clflush_virt_range(&vaddr[i], sizeof(vaddr[i]));

			if (vaddr[i] != val) {
				pr_err("vaddr[%u]=%x, expected=%x\n", i,
				       vaddr[i], val);
				err = -EINVAL;
				goto err_unpin;
			}
		}

		i915_gem_object_unpin_map(obj);
		i915_gem_object_put(obj);

		total <<= 1;
	} while (!time_after(jiffies, end));

	goto err_flush;

err_unpin:
	i915_gem_object_unpin_map(obj);
err_put:
	i915_gem_object_put(obj);
err_flush:
	if (err == -ENOMEM)
		err = 0;

	intel_context_put(ce);
	return err;
}

static int igt_copy_blt_thread(void *arg)
{
	struct igt_thread_arg *thread = arg;
	struct drm_i915_private *i915 = thread->i915;
	struct rnd_state *prng = &thread->prng;
	struct drm_i915_gem_object *src, *dst;
	struct i915_gem_context *ctx;
	struct intel_context *ce;
	unsigned int prio;
	IGT_TIMEOUT(end);
	u64 total, max;
	int err;

	ctx = thread->ctx;
	if (!ctx) {
		ctx = live_context(i915, thread->file);
		if (IS_ERR(ctx))
			return PTR_ERR(ctx);

		prio = i915_prandom_u32_max_state(I915_PRIORITY_MAX, prng);
		ctx->sched.priority = I915_USER_PRIORITY(prio);
	}

	ce = i915_gem_context_get_engine(ctx, BCS0);
	GEM_BUG_ON(IS_ERR(ce));

	/*
	 * If we have a tiny shared address space, like for the GGTT
	 * then we can't be too greedy.
	 */
	max = ce->vm->total;
	if (i915_is_ggtt(ce->vm) || thread->ctx)
		max = div_u64(max, thread->n_cpus);
	max >>= 4;

	total = PAGE_SIZE;
	do {
		/* Aim to keep the runtime under reasonable bounds! */
		const u32 max_phys_size = SZ_64K;
		u32 val = prandom_u32_state(prng);
		u32 phys_sz;
		u32 sz;
		u32 *vaddr;
		u32 i;

		total = min(total, max);
		sz = i915_prandom_u32_max_state(total, prng) + 1;
		phys_sz = sz % max_phys_size + 1;

		sz = round_up(sz, PAGE_SIZE);
		phys_sz = round_up(phys_sz, PAGE_SIZE);
		phys_sz = min(phys_sz, sz);

		pr_debug("%s with phys_sz= %x, sz=%x, val=%x\n", __func__,
			 phys_sz, sz, val);

		src = huge_gem_object(i915, phys_sz, sz);
		if (IS_ERR(src)) {
			err = PTR_ERR(src);
			goto err_flush;
		}

		vaddr = i915_gem_object_pin_map(src, I915_MAP_WB);
		if (IS_ERR(vaddr)) {
			err = PTR_ERR(vaddr);
			goto err_put_src;
		}

		memset32(vaddr, val,
			 huge_gem_object_phys_size(src) / sizeof(u32));

		i915_gem_object_unpin_map(src);

		if (!(src->cache_coherent & I915_BO_CACHE_COHERENT_FOR_READ))
			src->cache_dirty = true;

		dst = huge_gem_object(i915, phys_sz, sz);
		if (IS_ERR(dst)) {
			err = PTR_ERR(dst);
			goto err_put_src;
		}

		vaddr = i915_gem_object_pin_map(dst, I915_MAP_WB);
		if (IS_ERR(vaddr)) {
			err = PTR_ERR(vaddr);
			goto err_put_dst;
		}

		memset32(vaddr, val ^ 0xdeadbeaf,
			 huge_gem_object_phys_size(dst) / sizeof(u32));

		if (!(dst->cache_coherent & I915_BO_CACHE_COHERENT_FOR_WRITE))
			dst->cache_dirty = true;

		err = i915_gem_object_copy_blt(src, dst, ce);
		if (err)
			goto err_unpin;

		err = i915_gem_object_wait(dst, 0, MAX_SCHEDULE_TIMEOUT);
		if (err)
			goto err_unpin;

		for (i = 0; i < huge_gem_object_phys_size(dst) / sizeof(u32); i += 17) {
			if (!(dst->cache_coherent & I915_BO_CACHE_COHERENT_FOR_READ))
				drm_clflush_virt_range(&vaddr[i], sizeof(vaddr[i]));

			if (vaddr[i] != val) {
				pr_err("vaddr[%u]=%x, expected=%x\n", i,
				       vaddr[i], val);
				err = -EINVAL;
				goto err_unpin;
			}
		}

		i915_gem_object_unpin_map(dst);

		i915_gem_object_put(src);
		i915_gem_object_put(dst);

		total <<= 1;
	} while (!time_after(jiffies, end));

	goto err_flush;

err_unpin:
	i915_gem_object_unpin_map(dst);
err_put_dst:
	i915_gem_object_put(dst);
err_put_src:
	i915_gem_object_put(src);
err_flush:
	if (err == -ENOMEM)
		err = 0;

	intel_context_put(ce);
	return err;
}

static int igt_threaded_blt(struct drm_i915_private *i915,
			    int (*blt_fn)(void *arg),
			    unsigned int flags)
#define SINGLE_CTX BIT(0)
{
	struct igt_thread_arg *thread;
	struct task_struct **tsk;
	unsigned int n_cpus, i;
	I915_RND_STATE(prng);
	int err = 0;

	n_cpus = num_online_cpus() + 1;

	tsk = kcalloc(n_cpus, sizeof(struct task_struct *), GFP_KERNEL);
	if (!tsk)
		return 0;

	thread = kcalloc(n_cpus, sizeof(struct igt_thread_arg), GFP_KERNEL);
	if (!thread)
		goto out_tsk;

	thread[0].file = mock_file(i915);
	if (IS_ERR(thread[0].file)) {
		err = PTR_ERR(thread[0].file);
		goto out_thread;
	}

	if (flags & SINGLE_CTX) {
		thread[0].ctx = live_context(i915, thread[0].file);
		if (IS_ERR(thread[0].ctx)) {
			err = PTR_ERR(thread[0].ctx);
			goto out_file;
		}
	}

	for (i = 0; i < n_cpus; ++i) {
		thread[i].i915 = i915;
		thread[i].file = thread[0].file;
		thread[i].ctx = thread[0].ctx;
		thread[i].n_cpus = n_cpus;
		thread[i].prng =
			I915_RND_STATE_INITIALIZER(prandom_u32_state(&prng));

		tsk[i] = kthread_run(blt_fn, &thread[i], "igt/blt-%d", i);
		if (IS_ERR(tsk[i])) {
			err = PTR_ERR(tsk[i]);
			break;
		}

		get_task_struct(tsk[i]);
	}

	yield(); /* start all threads before we kthread_stop() */

	for (i = 0; i < n_cpus; ++i) {
		int status;

		if (IS_ERR_OR_NULL(tsk[i]))
			continue;

		status = kthread_stop(tsk[i]);
		if (status && !err)
			err = status;

		put_task_struct(tsk[i]);
	}

out_file:
	fput(thread[0].file);
out_thread:
	kfree(thread);
out_tsk:
	kfree(tsk);
	return err;
}

static int igt_fill_blt(void *arg)
{
	return igt_threaded_blt(arg, igt_fill_blt_thread, 0);
}

static int igt_fill_blt_ctx0(void *arg)
{
	return igt_threaded_blt(arg, igt_fill_blt_thread, SINGLE_CTX);
}

static int igt_copy_blt(void *arg)
{
	return igt_threaded_blt(arg, igt_copy_blt_thread, 0);
}

static int igt_copy_blt_ctx0(void *arg)
{
	return igt_threaded_blt(arg, igt_copy_blt_thread, SINGLE_CTX);
}

int i915_gem_object_blt_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_fill_blt),
		SUBTEST(igt_fill_blt_ctx0),
		SUBTEST(igt_copy_blt),
		SUBTEST(igt_copy_blt_ctx0),
	};

	if (intel_gt_is_wedged(&i915->gt))
		return 0;

	if (!HAS_ENGINE(i915, BCS0))
		return 0;

	return i915_live_subtests(tests, i915);
}

int i915_gem_object_blt_perf_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(perf_fill_blt),
		SUBTEST(perf_copy_blt),
	};

	if (intel_gt_is_wedged(&i915->gt))
		return 0;

	return i915_live_subtests(tests, i915);
}
