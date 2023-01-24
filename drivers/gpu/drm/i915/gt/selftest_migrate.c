// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/sort.h>

#include "gem/i915_gem_internal.h"
#include "gem/i915_gem_lmem.h"

#include "selftests/igt_spinner.h"
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

	sz = src->base.size;
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

static int intel_context_copy_ccs(struct intel_context *ce,
				  const struct i915_deps *deps,
				  struct scatterlist *sg,
				  enum i915_cache_level cache_level,
				  bool write_to_ccs,
				  struct i915_request **out)
{
	u8 src_access = write_to_ccs ? DIRECT_ACCESS : INDIRECT_ACCESS;
	u8 dst_access = write_to_ccs ? INDIRECT_ACCESS : DIRECT_ACCESS;
	struct sgt_dma it = sg_sgt(sg);
	struct i915_request *rq;
	u32 offset;
	int err;

	GEM_BUG_ON(ce->vm != ce->engine->gt->migrate.context->vm);
	*out = NULL;

	GEM_BUG_ON(ce->ring->size < SZ_64K);

	offset = 0;
	if (HAS_64K_PAGES(ce->engine->i915))
		offset = CHUNK_SZ;

	do {
		int len;

		rq = i915_request_create(ce);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto out_ce;
		}

		if (deps) {
			err = i915_request_await_deps(rq, deps);
			if (err)
				goto out_rq;

			if (rq->engine->emit_init_breadcrumb) {
				err = rq->engine->emit_init_breadcrumb(rq);
				if (err)
					goto out_rq;
			}

			deps = NULL;
		}

		/* The PTE updates + clear must not be interrupted. */
		err = emit_no_arbitration(rq);
		if (err)
			goto out_rq;

		len = emit_pte(rq, &it, cache_level, true, offset, CHUNK_SZ);
		if (len <= 0) {
			err = len;
			goto out_rq;
		}

		err = rq->engine->emit_flush(rq, EMIT_INVALIDATE);
		if (err)
			goto out_rq;

		err = emit_copy_ccs(rq, offset, dst_access,
				    offset, src_access, len);
		if (err)
			goto out_rq;

		err = rq->engine->emit_flush(rq, EMIT_INVALIDATE);

		/* Arbitration is re-enabled between requests. */
out_rq:
		if (*out)
			i915_request_put(*out);
		*out = i915_request_get(rq);
		i915_request_add(rq);
		if (err || !it.sg || !sg_dma_len(it.sg))
			break;

		cond_resched();
	} while (1);

out_ce:
	return err;
}

static int
intel_migrate_ccs_copy(struct intel_migrate *m,
		       struct i915_gem_ww_ctx *ww,
		       const struct i915_deps *deps,
		       struct scatterlist *sg,
		       enum i915_cache_level cache_level,
		       bool write_to_ccs,
		       struct i915_request **out)
{
	struct intel_context *ce;
	int err;

	*out = NULL;
	if (!m->context)
		return -ENODEV;

	ce = intel_migrate_create_context(m);
	if (IS_ERR(ce))
		ce = intel_context_get(m->context);
	GEM_BUG_ON(IS_ERR(ce));

	err = intel_context_pin_ww(ce, ww);
	if (err)
		goto out;

	err = intel_context_copy_ccs(ce, deps, sg, cache_level,
				     write_to_ccs, out);

	intel_context_unpin(ce);
out:
	intel_context_put(ce);
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
	u32 *vaddr, val = 0;
	bool ccs_cap = false;
	int err = 0;
	int i;

	obj = create_lmem_or_internal(i915, sz);
	if (IS_ERR(obj))
		return 0;

	/* Consider the rounded up memory too */
	sz = obj->base.size;

	if (HAS_FLAT_CCS(i915) && i915_gem_object_is_lmem(obj))
		ccs_cap = true;

	for_i915_gem_ww(&ww, err, true) {
		int ccs_bytes, ccs_bytes_per_chunk;

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

		if (ccs_cap && !val) {
			/* Write the obj data into ccs surface */
			err = intel_migrate_ccs_copy(migrate, &ww, NULL,
						     obj->mm.pages->sgl,
						     obj->cache_level,
						     true, &rq);
			if (rq && !err) {
				if (i915_request_wait(rq, 0, HZ) < 0) {
					pr_err("%ps timed out, size: %u\n",
					       fn, sz);
					err = -ETIME;
				}
				i915_request_put(rq);
				rq = NULL;
			}
			if (err)
				continue;
		}

		err = fn(migrate, &ww, obj, val, &rq);
		if (rq && !err) {
			if (i915_request_wait(rq, 0, HZ) < 0) {
				pr_err("%ps timed out, size: %u\n", fn, sz);
				err = -ETIME;
			}
			i915_request_put(rq);
			rq = NULL;
		}
		if (err)
			continue;

		i915_gem_object_flush_map(obj);

		/* Verify the set/clear of the obj mem */
		for (i = 0; !err && i < sz / PAGE_SIZE; i++) {
			int x = i * 1024 +
				i915_prandom_u32_max_state(1024, prng);

			if (vaddr[x] != val) {
				pr_err("%ps failed, (%u != %u), offset: %zu\n",
				       fn, vaddr[x], val,  x * sizeof(u32));
				igt_hexdump(vaddr + i * 1024, 4096);
				err = -EINVAL;
			}
		}
		if (err)
			continue;

		if (ccs_cap && !val) {
			for (i = 0; i < sz / sizeof(u32); i++)
				vaddr[i] = ~i;
			i915_gem_object_flush_map(obj);

			err = intel_migrate_ccs_copy(migrate, &ww, NULL,
						     obj->mm.pages->sgl,
						     obj->cache_level,
						     false, &rq);
			if (rq && !err) {
				if (i915_request_wait(rq, 0, HZ) < 0) {
					pr_err("%ps timed out, size: %u\n",
					       fn, sz);
					err = -ETIME;
				}
				i915_request_put(rq);
				rq = NULL;
			}
			if (err)
				continue;

			ccs_bytes = GET_CCS_BYTES(i915, sz);
			ccs_bytes_per_chunk = GET_CCS_BYTES(i915, CHUNK_SZ);
			i915_gem_object_flush_map(obj);

			for (i = 0; !err && i < DIV_ROUND_UP(ccs_bytes, PAGE_SIZE); i++) {
				int offset = ((i * PAGE_SIZE)  /
					ccs_bytes_per_chunk) * CHUNK_SZ / sizeof(u32);
				int ccs_bytes_left = (ccs_bytes - i * PAGE_SIZE) / sizeof(u32);
				int x = i915_prandom_u32_max_state(min_t(int, 1024,
									 ccs_bytes_left), prng);

				if (vaddr[offset + x]) {
					pr_err("%ps ccs clearing failed, offset: %ld/%d\n",
					       fn, i * PAGE_SIZE + x * sizeof(u32), ccs_bytes);
					igt_hexdump(vaddr + offset,
						    min_t(int, 4096,
							  ccs_bytes_left * sizeof(u32)));
					err = -EINVAL;
				}
			}

			if (err)
				continue;
		}
		i915_gem_object_unpin_map(obj);
	}

	if (err) {
		if (err != -EDEADLK && err != -EINTR && err != -ERESTARTSYS)
			pr_err("%ps failed, size: %u\n", fn, sz);
		if (rq && err != -EINVAL) {
			i915_request_wait(rq, 0, HZ);
			i915_request_put(rq);
		}

		i915_gem_object_unpin_map(obj);
	}

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
	struct intel_gt *gt = arg;
	struct intel_migrate *migrate = &gt->migrate;
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
	struct intel_gt *gt = arg;
	struct intel_migrate *migrate = &gt->migrate;
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

struct spinner_timer {
	struct timer_list timer;
	struct igt_spinner spin;
};

static void spinner_kill(struct timer_list *timer)
{
	struct spinner_timer *st = from_timer(st, timer, timer);

	igt_spinner_end(&st->spin);
	pr_info("%s\n", __func__);
}

static int live_emit_pte_full_ring(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_migrate *migrate = &gt->migrate;
	struct drm_i915_private *i915 = migrate->context->engine->i915;
	struct drm_i915_gem_object *obj;
	struct intel_context *ce;
	struct i915_request *rq, *prev;
	struct spinner_timer st;
	struct sgt_dma it;
	int len, sz, err;
	u32 *cs;

	/*
	 * Simple regression test to check that we don't trample the
	 * rq->reserved_space when returning from emit_pte(), if the ring is
	 * nearly full.
	 */

	if (igt_spinner_init(&st.spin, to_gt(i915)))
		return -ENOMEM;

	obj = i915_gem_object_create_internal(i915, 2 * PAGE_SIZE);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		goto out_spinner;
	}

	err = i915_gem_object_pin_pages_unlocked(obj);
	if (err)
		goto out_obj;

	ce = intel_migrate_create_context(migrate);
	if (IS_ERR(ce)) {
		err = PTR_ERR(ce);
		goto out_obj;
	}

	ce->ring_size = SZ_4K; /* Not too big */

	err = intel_context_pin(ce);
	if (err)
		goto out_put;

	rq = igt_spinner_create_request(&st.spin, ce, MI_ARB_CHECK);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto out_unpin;
	}

	i915_request_add(rq);
	if (!igt_wait_for_spinner(&st.spin, rq)) {
		err = -EIO;
		goto out_unpin;
	}

	/*
	 * Fill the rest of the ring leaving I915_EMIT_PTE_NUM_DWORDS +
	 * ring->reserved_space at the end. To actually emit the PTEs we require
	 * slightly more than I915_EMIT_PTE_NUM_DWORDS, since our object size is
	 * greater than PAGE_SIZE. The correct behaviour is to wait for more
	 * ring space in emit_pte(), otherwise we trample on the reserved_space
	 * resulting in crashes when later submitting the rq.
	 */

	prev = NULL;
	do {
		if (prev)
			i915_request_add(rq);

		rq = i915_request_create(ce);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto out_unpin;
		}

		sz = (rq->ring->space - rq->reserved_space) / sizeof(u32) -
			I915_EMIT_PTE_NUM_DWORDS;
		sz = min_t(u32, sz, (SZ_1K - rq->reserved_space) / sizeof(u32) -
			   I915_EMIT_PTE_NUM_DWORDS);
		cs = intel_ring_begin(rq, sz);
		if (IS_ERR(cs)) {
			err = PTR_ERR(cs);
			goto out_rq;
		}

		memset32(cs, MI_NOOP, sz);
		cs += sz;
		intel_ring_advance(rq, cs);

		pr_info("%s emit=%u sz=%d\n", __func__, rq->ring->emit, sz);

		prev = rq;
	} while (rq->ring->space > (rq->reserved_space +
				    I915_EMIT_PTE_NUM_DWORDS * sizeof(u32)));

	timer_setup_on_stack(&st.timer, spinner_kill, 0);
	mod_timer(&st.timer, jiffies + 2 * HZ);

	/*
	 * This should wait for the spinner to be killed, otherwise we should go
	 * down in flames when doing i915_request_add().
	 */
	pr_info("%s emite_pte ring space=%u\n", __func__, rq->ring->space);
	it = sg_sgt(obj->mm.pages->sgl);
	len = emit_pte(rq, &it, obj->cache_level, false, 0, CHUNK_SZ);
	if (!len) {
		err = -EINVAL;
		goto out_rq;
	}
	if (len < 0) {
		err = len;
		goto out_rq;
	}

out_rq:
	i915_request_add(rq); /* GEM_BUG_ON(rq->reserved_space > ring->space)? */
	del_timer_sync(&st.timer);
	destroy_timer_on_stack(&st.timer);
out_unpin:
	intel_context_unpin(ce);
out_put:
	intel_context_put(ce);
out_obj:
	i915_gem_object_put(obj);
out_spinner:
	igt_spinner_fini(&st.spin);
	return err;
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
	struct intel_gt *gt = arg;
	struct intel_migrate *migrate = &gt->migrate;

	return threaded_migrate(migrate, __thread_migrate_copy, 0);
}

static int __thread_global_copy(void *arg)
{
	struct threaded_migrate *tm = arg;

	return global_copy(tm->migrate, 2 * CHUNK_SZ, &tm->prng);
}

static int thread_global_copy(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_migrate *migrate = &gt->migrate;

	return threaded_migrate(migrate, __thread_global_copy, 0);
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
	struct intel_gt *gt = arg;
	struct intel_migrate *migrate = &gt->migrate;

	return threaded_migrate(migrate, __thread_migrate_clear, 0);
}

static int thread_global_clear(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_migrate *migrate = &gt->migrate;

	return threaded_migrate(migrate, __thread_global_clear, 0);
}

int intel_migrate_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_migrate_copy),
		SUBTEST(live_migrate_clear),
		SUBTEST(live_emit_pte_full_ring),
		SUBTEST(thread_migrate_copy),
		SUBTEST(thread_migrate_clear),
		SUBTEST(thread_global_copy),
		SUBTEST(thread_global_clear),
	};
	struct intel_gt *gt = to_gt(i915);

	if (!gt->migrate.context)
		return 0;

	return intel_gt_live_subtests(tests, gt);
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

	i915_gem_object_trylock(obj, NULL);
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
		size_t sz;
		int err;

		src = create_init_lmem_internal(gt, sizes[i], true);
		if (IS_ERR(src))
			return PTR_ERR(src);

		sz = src->base.size;
		dst = create_init_lmem_internal(gt, sz, false);
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
				      sz);

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
	struct intel_gt *gt = to_gt(i915);

	if (intel_gt_is_wedged(gt))
		return 0;

	if (!gt->migrate.context)
		return 0;

	return intel_gt_live_subtests(tests, gt);
}
