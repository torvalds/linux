// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

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
	if (HAS_LMEM(i915)) {
		struct drm_i915_gem_object *obj;

		obj = i915_gem_object_create_lmem(i915, size, 0);
		if (!IS_ERR(obj))
			return obj;
	}

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

int intel_migrate_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_migrate_copy),
		SUBTEST(thread_migrate_copy),
		SUBTEST(thread_global_copy),
	};
	struct intel_migrate m;
	int err;

	if (intel_migrate_init(&m, &i915->gt))
		return 0;

	err = i915_subtests(tests, &m);
	intel_migrate_fini(&m);

	return err;
}
