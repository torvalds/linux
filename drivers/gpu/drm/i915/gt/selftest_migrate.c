// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/sort.h>

#include "selftests/i915_random.h"

static const unsigned int sizes[] = {
	SZ_4K,
	SZ_64K,
	SZ_2M,
	CHUNK_SZ - SZ_4K,
	CHUNK_SZ,
	CHUNK_SZ + SZ_4K,
	SZ_64M,
};

static struct drm_i915_gem_object *
create_lmem_or_internal(struct drm_i915_private *i915, size_t size)
{
	struct drm_i915_gem_object *obj;

	obj = i915_gem_object_create_lmem(i915, size, 0);
	if (!IS_ERR(obj))
		return obj;

	return i915_gem_object_create_internal(i915, size);
}

static int copy(struct intel_migrate *migrate,
		int (*fn)(struct intel_migrate *migrate,
			  struct i915_gem_ww_ctx *ww,
			  struct drm_i915_gem_object *src,
			  struct drm_i915_gem_object *dst,
			  struct i915_request **out),
		u32 sz, struct rnd_state *prng)
{
	struct drm_i915_private *i915 = migrate->context->engine->i915;
	struct drm_i915_gem_object *src, *dst;
	struct i915_request *rq;
	struct i915_gem_ww_ctx ww;
	u32 *vaddr;
	int err = 0;
	int i;

	src = create_lmem_or_internal(i915, sz);
	if (IS_ERR(src))
		return 0;

	dst = i915_gem_object_create_internal(i915, sz);
	if (IS_ERR(dst))
		goto err_free_src;

	for_i915_gem_ww(&ww, err, true) {
		err = i915_gem_object_lock(src, &ww);
		if (err)
			continue;

		err = i915_gem_object_lock(dst, &ww);
		if (err)
			continue;

		vaddr = i915_gem_object_pin_map(src, I915_MAP_WC);
		if (IS_ERR(vaddr)) {
			err = PTR_ERR(vaddr);
			continue;
		}

		for (i = 0; i < sz / sizeof(u32); i++)
			vaddr[i] = i;
		i915_gem_object_flush_map(src);

		vaddr = i915_gem_object_pin_map(dst, I915_MAP_WC);
		if (IS_ERR(vaddr)) {
			err = PTR_ERR(vaddr);
			goto unpin_src;
		}

		for (i = 0; i < sz / sizeof(u32); i++)
			vaddr[i] = ~i;
		i915_gem_object_flush_map(dst);

		err = fn(migrate, &ww, src, dst, &rq);
		if (!err)
			continue;

		if (err != -EDEADLK && err != -EINTR && err != -ERESTARTSYS)
			pr_err("%ps failed, size: %u\n", fn, sz);
		if (rq) {
			i915_request_wait(rq, 0, HZ);
			i915_request_put(rq);
		}
		i915_gem_object_unpin_map(dst);
unpin_src:
		i915_gem_object_unpin_map(src);
	}
	if (err)
		goto err_out;

	if (rq) {
		if (i915_request_wait(rq, 0, HZ) < 0) {
			pr_err("%ps timed out, size: %u\n", fn, sz);
			err = -ETIME;
		}
		i915_request_put(rq);
	}

	for (i = 0; !err && i < sz / PAGE_SIZE; i++) {
		int x = i * 1024 + i915_prandom_u32_max_state(1024, prng);

		if (vaddr[x] != x) {
			pr_err("%ps failed, size: %u, offset: %zu\n",
			       fn, sz, x * sizeof(u32));
			igt_hexdump(vaddr + i * 1024, 4096);
			err = -EINVAL;
		}
	}

	i915_gem_object_unpin_map(dst);
	i915_gem_object_unpin_map(src);

err_out:
	i915_gem_object_put(dst);
err_free_src:
	i915_gem_object_put(src);

	return err;
}

static int clear(struct intel_migrate *migrate,
		 int (*fn)(struct intel_migrate *migrate,
			   struct i915_gem_ww_ctx *ww,
			   struct drm_i915_gem_object *obj,
			   u32 value,
			   struct i915_request **out),
		 u32 sz, struct rnd_state *prng)
{
	struct drm_i915_private *i915 = migrate->context->engine->i915;
	struct drm_i915_gem_object *obj;
	struct i915_request *rq;
	struct i915_gem_ww_ctx ww;
	u32 *vaddr;
	int err = 0;
	int i;

	obj = create_lmem_or_internal(i915, sz);
	if (IS_ERR(obj))
		return 0;

	for_i915_gem_ww(&ww, err, true) {
		err = i915_gem_object_lock(obj, &ww);
		if (err)
			continue;

		vaddr = i915_gem_object_pin_map(obj, I915_MAP_WC);
		if (IS_ERR(vaddr)) {
			err = PTR_ERR(vaddr);
			continue;
		}

		for (i = 0; i < sz / sizeof(u32); i++)
			vaddr[i] = ~i;
		i915_gem_object_flush_map(obj);

		err = fn(migrate, &ww, obj, sz, &rq);
		if (!err)
			continue;

		if (err != -EDEADLK && err != -EINTR && err != -ERESTARTSYS)
			pr_err("%ps failed, size: %u\n", fn, sz);
		if (rq) {
			i915_request_wait(rq, 0, HZ);
			i915_request_put(rq);
		}
		i915_gem_object_unpin_map(obj);
	}
	if (err)
		goto err_out;

	if (rq) {
		if (i915_request_wait(rq, 0, HZ) < 0) {
			pr_err("%ps timed out, size: %u\n", fn, sz);
			err = -ETIME;
		}
		i915_request_put(rq);
	}

	for (i = 0; !err && i < sz / PAGE_SIZE; i++) {
		int x = i * 1024 + i915_prandom_u32_max_state(1024, prng);

		if (vaddr[x] != sz) {
			pr_err("%ps failed, size: %u, offset: %zu\n",
			       fn, sz, x * sizeof(u32));
			igt_hexdump(vaddr + i * 1024, 4096);
			err = -EINVAL;
		}
	}

	i915_gem_object_unpin_map(obj);
err_out:
	i915_gem_object_put(obj);

	return err;
}

static int __migrate_copy(struct intel_migrate *migrate,
			  struct i915_gem_ww_ctx *ww,
			  struct drm_i915_gem_object *src,
			  struct drm_i915_gem_object *dst,
			  struct i915_request **out)
{
	return intel_migrate_copy(migrate, ww, NULL,
				  src->mm.pages->sgl, src->cache_level,
				  i915_gem_object_is_lmem(src),
				  dst->mm.pages->sgl, dst->cache_level,
				  i915_gem_object_is_lmem(dst),
				  out);
}

static int __global_copy(struct intel_migrate *migrate,
			 struct i915_gem_ww_ctx *ww,
			 struct drm_i915_gem_object *src,
			 struct drm_i915_gem_object *dst,
			 struct i915_request **out)
{
	return intel_context_migrate_copy(migrate->context, NULL,
					  src->mm.pages->sgl, src->cache_level,
					  i915_gem_object_is_lmem(src),
					  dst->mm.pages->sgl, dst->cache_level,
					  i915_gem_object_is_lmem(dst),
					  out);
}

static int
migrate_copy(struct intel_migrate *migrate, u32 sz, struct rnd_state *prng)
{
	return copy(migrate, __migrate_copy, sz, prng);
}

static int
global_copy(struct intel_migrate *migrate, u32 sz, struct rnd_state *prng)
{
	return copy(migrate, __global_copy, sz, prng);
}

static int __migrate_clear(struct intel_migrate *migrate,
			   struct i915_gem_ww_ctx *ww,
			   struct drm_i915_gem_object *obj,
			   u32 value,
			   struct i915_request **out)
{
	return intel_migrate_clear(migrate, ww, NULL,
				   obj->mm.pages->sgl,
				   obj->cache_level,
				   i915_gem_object_is_lmem(obj),
				   value, out);
}

static int __global_clear(struct intel_migrate *migrate,
			  struct i915_gem_ww_ctx *ww,
			  struct drm_i915_gem_object *obj,
			  u32 value,
			  struct i915_request **out)
{
	return intel_context_migrate_clear(migrate->context, NULL,
					   obj->mm.pages->sgl,
					   obj->cache_level,
					   i915_gem_object_is_lmem(obj),
					   value, out);
}

static int
migrate_clear(struct intel_migrate *migrate, u32 sz, struct rnd_state *prng)
{
	return clear(migrate, __migrate_clear, sz, prng);
}

static int
global_clear(struct intel_migrate *migrate, u32 sz, struct rnd_state *prng)
{
	return clear(migrate, __global_clear, sz, prng);
}

static int live_migrate_copy(void *arg)
{
	struct intel_migrate *migrate = arg;
	struct drm_i915_private *i915 = migrate->context->engine->i915;
	I915_RND_STATE(prng);
	int i;

	for (i = 0; i < ARRAY_SIZE(sizes); i++) {
		int err;

		err = migrate_copy(migrate, sizes[i], &prng);
		if (err == 0)
			err = global_copy(migrate, sizes[i], &prng);
		i915_gem_drain_freed_objects(i915);
		if (err)
			return err;
	}

	return 0;
}

static int live_migrate_clear(void *arg)
{
	struct intel_migrate *migrate = arg;
	struct drm_i915_private *i915 = migrate->context->engine->i915;
	I915_RND_STATE(prng);
	int i;

	for (i = 0; i < ARRAY_SIZE(sizes); i++) {
		int err;

		err = migrate_clear(migrate, sizes[i], &prng);
		if (err == 0)
			err = global_clear(migrate, sizes[i], &prng);

		i915_gem_drain_freed_objects(i915);
		if (err)
			return err;
	}

	return 0;
}

struct threaded_migrate {
	struct intel_migrate *migrate;
	struct task_struct *tsk;
	struct rnd_state prng;
};

static int threaded_migrate(struct intel_migrate *migrate,
			    int (*fn)(void *arg),
			    unsigned int flags)
{
	const unsigned int n_cpus = num_online_cpus() + 1;
	struct threaded_migrate *thread;
	I915_RND_STATE(prng);
	unsigned int i;
	int err = 0;

	thread = kcalloc(n_cpus, sizeof(*thread), GFP_KERNEL);
	if (!thread)
		return 0;

	for (i = 0; i < n_cpus; ++i) {
		struct task_struct *tsk;

		thread[i].migrate = migrate;
		thread[i].prng =
			I915_RND_STATE_INITIALIZER(prandom_u32_state(&prng));

		tsk = kthread_run(fn, &thread[i], "igt-%d", i);
		if (IS_ERR(tsk)) {
			err = PTR_ERR(tsk);
			break;
		}

		get_task_struct(tsk);
		thread[i].tsk = tsk;
	}

	msleep(10); /* start all threads before we kthread_stop() */

	for (i = 0; i < n_cpus; ++i) {
		struct task_struct *tsk = thread[i].tsk;
		int status;

		if (IS_ERR_OR_NULL(tsk))
			continue;

		status = kthread_stop(tsk);
		if (status && !err)
			err = status;

		put_task_struct(tsk);
	}

	kfree(thread);
	return err;
}

static int __thread_migrate_copy(void *arg)
{
	struct threaded_migrate *tm = arg;

	return migrate_copy(tm->migrate, 2 * CHUNK_SZ, &tm->prng);
}

static int thread_migrate_copy(void *arg)
{
	return threaded_migrate(arg, __thread_migrate_copy, 0);
}

static int __thread_global_copy(void *arg)
{
	struct threaded_migrate *tm = arg;

	return global_copy(tm->migrate, 2 * CHUNK_SZ, &tm->prng);
}

static int thread_global_copy(void *arg)
{
	return threaded_migrate(arg, __thread_global_copy, 0);
}

static int __thread_migrate_clear(void *arg)
{
	struct threaded_migrate *tm = arg;

	return migrate_clear(tm->migrate, 2 * CHUNK_SZ, &tm->prng);
}

static int __thread_global_clear(void *arg)
{
	struct threaded_migrate *tm = arg;

	return global_clear(tm->migrate, 2 * CHUNK_SZ, &tm->prng);
}

static int thread_migrate_clear(void *arg)
{
	return threaded_migrate(arg, __thread_migrate_clear, 0);
}

static int thread_global_clear(void *arg)
{
	return threaded_migrate(arg, __thread_global_clear, 0);
}

int intel_migrate_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_migrate_copy),
		SUBTEST(live_migrate_clear),
		SUBTEST(thread_migrate_copy),
		SUBTEST(thread_migrate_clear),
		SUBTEST(thread_global_copy),
		SUBTEST(thread_global_clear),
	};
	struct intel_gt *gt = &i915->gt;

	if (!gt->migrate.context)
		return 0;

	return i915_subtests(tests, &gt->migrate);
}

static struct drm_i915_gem_object *
create_init_lmem_internal(struct intel_gt *gt, size_t sz, bool try_lmem)
{
	struct drm_i915_gem_object *obj = NULL;
	int err;

	if (try_lmem)
		obj = i915_gem_object_create_lmem(gt->i915, sz, 0);

	if (IS_ERR_OR_NULL(obj)) {
		obj = i915_gem_object_create_internal(gt->i915, sz);
		if (IS_ERR(obj))
			return obj;
	}

	i915_gem_object_trylock(obj);
	err = i915_gem_object_pin_pages(obj);
	if (err) {
		i915_gem_object_unlock(obj);
		i915_gem_object_put(obj);
		return ERR_PTR(err);
	}

	return obj;
}

static int wrap_ktime_compare(const void *A, const void *B)
{
	const ktime_t *a = A, *b = B;

	return ktime_compare(*a, *b);
}

static int __perf_clear_blt(struct intel_context *ce,
			    struct scatterlist *sg,
			    enum i915_cache_level cache_level,
			    bool is_lmem,
			    size_t sz)
{
	ktime_t t[5];
	int pass;
	int err = 0;

	for (pass = 0; pass < ARRAY_SIZE(t); pass++) {
		struct i915_request *rq;
		ktime_t t0, t1;

		t0 = ktime_get();

		err = intel_context_migrate_clear(ce, NULL, sg, cache_level,
						  is_lmem, 0, &rq);
		if (rq) {
			if (i915_request_wait(rq, 0, MAX_SCHEDULE_TIMEOUT) < 0)
				err = -EIO;
			i915_request_put(rq);
		}
		if (err)
			break;

		t1 = ktime_get();
		t[pass] = ktime_sub(t1, t0);
	}
	if (err)
		return err;

	sort(t, ARRAY_SIZE(t), sizeof(*t), wrap_ktime_compare, NULL);
	pr_info("%s: %zd KiB fill: %lld MiB/s\n",
		ce->engine->name, sz >> 10,
		div64_u64(mul_u32_u32(4 * sz,
				      1000 * 1000 * 1000),
			  t[1] + 2 * t[2] + t[3]) >> 20);
	return 0;
}

static int perf_clear_blt(void *arg)
{
	struct intel_gt *gt = arg;
	static const unsigned long sizes[] = {
		SZ_4K,
		SZ_64K,
		SZ_2M,
		SZ_64M
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(sizes); i++) {
		struct drm_i915_gem_object *dst;
		int err;

		dst = create_init_lmem_internal(gt, sizes[i], true);
		if (IS_ERR(dst))
			return PTR_ERR(dst);

		err = __perf_clear_blt(gt->migrate.context,
				       dst->mm.pages->sgl,
				       I915_CACHE_NONE,
				       i915_gem_object_is_lmem(dst),
				       sizes[i]);

		i915_gem_object_unlock(dst);
		i915_gem_object_put(dst);
		if (err)
			return err;
	}

	return 0;
}

static int __perf_copy_blt(struct intel_context *ce,
			   struct scatterlist *src,
			   enum i915_cache_level src_cache_level,
			   bool src_is_lmem,
			   struct scatterlist *dst,
			   enum i915_cache_level dst_cache_level,
			   bool dst_is_lmem,
			   size_t sz)
{
	ktime_t t[5];
	int pass;
	int err = 0;

	for (pass = 0; pass < ARRAY_SIZE(t); pass++) {
		struct i915_request *rq;
		ktime_t t0, t1;

		t0 = ktime_get();

		err = intel_context_migrate_copy(ce, NULL,
						 src, src_cache_level,
						 src_is_lmem,
						 dst, dst_cache_level,
						 dst_is_lmem,
						 &rq);
		if (rq) {
			if (i915_request_wait(rq, 0, MAX_SCHEDULE_TIMEOUT) < 0)
				err = -EIO;
			i915_request_put(rq);
		}
		if (err)
			break;

		t1 = ktime_get();
		t[pass] = ktime_sub(t1, t0);
	}
	if (err)
		return err;

	sort(t, ARRAY_SIZE(t), sizeof(*t), wrap_ktime_compare, NULL);
	pr_info("%s: %zd KiB copy: %lld MiB/s\n",
		ce->engine->name, sz >> 10,
		div64_u64(mul_u32_u32(4 * sz,
				      1000 * 1000 * 1000),
			  t[1] + 2 * t[2] + t[3]) >> 20);
	return 0;
}

static int perf_copy_blt(void *arg)
{
	struct intel_gt *gt = arg;
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

		src = create_init_lmem_internal(gt, sizes[i], true);
		if (IS_ERR(src))
			return PTR_ERR(src);

		dst = create_init_lmem_internal(gt, sizes[i], false);
		if (IS_ERR(dst)) {
			err = PTR_ERR(dst);
			goto err_src;
		}

		err = __perf_copy_blt(gt->migrate.context,
				      src->mm.pages->sgl,
				      I915_CACHE_NONE,
				      i915_gem_object_is_lmem(src),
				      dst->mm.pages->sgl,
				      I915_CACHE_NONE,
				      i915_gem_object_is_lmem(dst),
				      sizes[i]);

		i915_gem_object_unlock(dst);
		i915_gem_object_put(dst);
err_src:
		i915_gem_object_unlock(src);
		i915_gem_object_put(src);
		if (err)
			return err;
	}

	return 0;
}

int intel_migrate_perf_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(perf_clear_blt),
		SUBTEST(perf_copy_blt),
	};
	struct intel_gt *gt = &i915->gt;

	if (intel_gt_is_wedged(gt))
		return 0;

	if (!gt->migrate.context)
		return 0;

	return intel_gt_live_subtests(tests, gt);
}
