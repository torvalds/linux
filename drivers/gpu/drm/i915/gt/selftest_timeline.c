// SPDX-License-Identifier: MIT
/*
 * Copyright © 2017-2018 Intel Corporation
 */

#include <linux/prime_numbers.h>

#include "intel_context.h"
#include "intel_engine_heartbeat.h"
#include "intel_engine_pm.h"
#include "intel_gpu_commands.h"
#include "intel_gt.h"
#include "intel_gt_requests.h"
#include "intel_ring.h"
#include "selftest_engine_heartbeat.h"

#include "../selftests/i915_random.h"
#include "../i915_selftest.h"

#include "selftests/igt_flush_test.h"
#include "selftests/lib_sw_fence.h"
#include "selftests/mock_gem_device.h"
#include "selftests/mock_timeline.h"

static struct page *hwsp_page(struct intel_timeline *tl)
{
	struct drm_i915_gem_object *obj = tl->hwsp_ggtt->obj;

	GEM_BUG_ON(!i915_gem_object_has_pinned_pages(obj));
	return sg_page(obj->mm.pages->sgl);
}

static unsigned long hwsp_cacheline(struct intel_timeline *tl)
{
	unsigned long address = (unsigned long)page_address(hwsp_page(tl));

	return (address + offset_in_page(tl->hwsp_offset)) / TIMELINE_SEQNO_BYTES;
}

static int selftest_tl_pin(struct intel_timeline *tl)
{
	struct i915_gem_ww_ctx ww;
	int err;

	i915_gem_ww_ctx_init(&ww, false);
retry:
	err = i915_gem_object_lock(tl->hwsp_ggtt->obj, &ww);
	if (!err)
		err = intel_timeline_pin(tl, &ww);

	if (err == -EDEADLK) {
		err = i915_gem_ww_ctx_backoff(&ww);
		if (!err)
			goto retry;
	}
	i915_gem_ww_ctx_fini(&ww);
	return err;
}

/* Only half of seqno's are usable, see __intel_timeline_get_seqno() */
#define CACHELINES_PER_PAGE (PAGE_SIZE / TIMELINE_SEQNO_BYTES / 2)

struct mock_hwsp_freelist {
	struct intel_gt *gt;
	struct radix_tree_root cachelines;
	struct intel_timeline **history;
	unsigned long count, max;
	struct rnd_state prng;
};

enum {
	SHUFFLE = BIT(0),
};

static void __mock_hwsp_record(struct mock_hwsp_freelist *state,
			       unsigned int idx,
			       struct intel_timeline *tl)
{
	tl = xchg(&state->history[idx], tl);
	if (tl) {
		radix_tree_delete(&state->cachelines, hwsp_cacheline(tl));
		intel_timeline_unpin(tl);
		intel_timeline_put(tl);
	}
}

static int __mock_hwsp_timeline(struct mock_hwsp_freelist *state,
				unsigned int count,
				unsigned int flags)
{
	struct intel_timeline *tl;
	unsigned int idx;

	while (count--) {
		unsigned long cacheline;
		int err;

		tl = intel_timeline_create(state->gt);
		if (IS_ERR(tl))
			return PTR_ERR(tl);

		err = selftest_tl_pin(tl);
		if (err) {
			intel_timeline_put(tl);
			return err;
		}

		cacheline = hwsp_cacheline(tl);
		err = radix_tree_insert(&state->cachelines, cacheline, tl);
		if (err) {
			if (err == -EEXIST) {
				pr_err("HWSP cacheline %lu already used; duplicate allocation!\n",
				       cacheline);
			}
			intel_timeline_unpin(tl);
			intel_timeline_put(tl);
			return err;
		}

		idx = state->count++ % state->max;
		__mock_hwsp_record(state, idx, tl);
	}

	if (flags & SHUFFLE)
		i915_prandom_shuffle(state->history,
				     sizeof(*state->history),
				     min(state->count, state->max),
				     &state->prng);

	count = i915_prandom_u32_max_state(min(state->count, state->max),
					   &state->prng);
	while (count--) {
		idx = --state->count % state->max;
		__mock_hwsp_record(state, idx, NULL);
	}

	return 0;
}

static int mock_hwsp_freelist(void *arg)
{
	struct mock_hwsp_freelist state;
	struct drm_i915_private *i915;
	const struct {
		const char *name;
		unsigned int flags;
	} phases[] = {
		{ "linear", 0 },
		{ "shuffled", SHUFFLE },
		{ },
	}, *p;
	unsigned int na;
	int err = 0;

	i915 = mock_gem_device();
	if (!i915)
		return -ENOMEM;

	INIT_RADIX_TREE(&state.cachelines, GFP_KERNEL);
	state.prng = I915_RND_STATE_INITIALIZER(i915_selftest.random_seed);

	state.gt = &i915->gt;

	/*
	 * Create a bunch of timelines and check that their HWSP do not overlap.
	 * Free some, and try again.
	 */

	state.max = PAGE_SIZE / sizeof(*state.history);
	state.count = 0;
	state.history = kcalloc(state.max, sizeof(*state.history), GFP_KERNEL);
	if (!state.history) {
		err = -ENOMEM;
		goto err_put;
	}

	for (p = phases; p->name; p++) {
		pr_debug("%s(%s)\n", __func__, p->name);
		for_each_prime_number_from(na, 1, 2 * CACHELINES_PER_PAGE) {
			err = __mock_hwsp_timeline(&state, na, p->flags);
			if (err)
				goto out;
		}
	}

out:
	for (na = 0; na < state.max; na++)
		__mock_hwsp_record(&state, na, NULL);
	kfree(state.history);
err_put:
	mock_destroy_device(i915);
	return err;
}

struct __igt_sync {
	const char *name;
	u32 seqno;
	bool expected;
	bool set;
};

static int __igt_sync(struct intel_timeline *tl,
		      u64 ctx,
		      const struct __igt_sync *p,
		      const char *name)
{
	int ret;

	if (__intel_timeline_sync_is_later(tl, ctx, p->seqno) != p->expected) {
		pr_err("%s: %s(ctx=%llu, seqno=%u) expected passed %s but failed\n",
		       name, p->name, ctx, p->seqno, yesno(p->expected));
		return -EINVAL;
	}

	if (p->set) {
		ret = __intel_timeline_sync_set(tl, ctx, p->seqno);
		if (ret)
			return ret;
	}

	return 0;
}

static int igt_sync(void *arg)
{
	const struct __igt_sync pass[] = {
		{ "unset", 0, false, false },
		{ "new", 0, false, true },
		{ "0a", 0, true, true },
		{ "1a", 1, false, true },
		{ "1b", 1, true, true },
		{ "0b", 0, true, false },
		{ "2a", 2, false, true },
		{ "4", 4, false, true },
		{ "INT_MAX", INT_MAX, false, true },
		{ "INT_MAX-1", INT_MAX-1, true, false },
		{ "INT_MAX+1", (u32)INT_MAX+1, false, true },
		{ "INT_MAX", INT_MAX, true, false },
		{ "UINT_MAX", UINT_MAX, false, true },
		{ "wrap", 0, false, true },
		{ "unwrap", UINT_MAX, true, false },
		{},
	}, *p;
	struct intel_timeline tl;
	int order, offset;
	int ret = -ENODEV;

	mock_timeline_init(&tl, 0);
	for (p = pass; p->name; p++) {
		for (order = 1; order < 64; order++) {
			for (offset = -1; offset <= (order > 1); offset++) {
				u64 ctx = BIT_ULL(order) + offset;

				ret = __igt_sync(&tl, ctx, p, "1");
				if (ret)
					goto out;
			}
		}
	}
	mock_timeline_fini(&tl);

	mock_timeline_init(&tl, 0);
	for (order = 1; order < 64; order++) {
		for (offset = -1; offset <= (order > 1); offset++) {
			u64 ctx = BIT_ULL(order) + offset;

			for (p = pass; p->name; p++) {
				ret = __igt_sync(&tl, ctx, p, "2");
				if (ret)
					goto out;
			}
		}
	}

out:
	mock_timeline_fini(&tl);
	return ret;
}

static unsigned int random_engine(struct rnd_state *rnd)
{
	return i915_prandom_u32_max_state(I915_NUM_ENGINES, rnd);
}

static int bench_sync(void *arg)
{
	struct rnd_state prng;
	struct intel_timeline tl;
	unsigned long end_time, count;
	u64 prng32_1M;
	ktime_t kt;
	int order, last_order;

	mock_timeline_init(&tl, 0);

	/* Lookups from cache are very fast and so the random number generation
	 * and the loop itself becomes a significant factor in the per-iteration
	 * timings. We try to compensate the results by measuring the overhead
	 * of the prng and subtract it from the reported results.
	 */
	prandom_seed_state(&prng, i915_selftest.random_seed);
	count = 0;
	kt = ktime_get();
	end_time = jiffies + HZ/10;
	do {
		u32 x;

		/* Make sure the compiler doesn't optimise away the prng call */
		WRITE_ONCE(x, prandom_u32_state(&prng));

		count++;
	} while (!time_after(jiffies, end_time));
	kt = ktime_sub(ktime_get(), kt);
	pr_debug("%s: %lu random evaluations, %lluns/prng\n",
		 __func__, count, (long long)div64_ul(ktime_to_ns(kt), count));
	prng32_1M = div64_ul(ktime_to_ns(kt) << 20, count);

	/* Benchmark (only) setting random context ids */
	prandom_seed_state(&prng, i915_selftest.random_seed);
	count = 0;
	kt = ktime_get();
	end_time = jiffies + HZ/10;
	do {
		u64 id = i915_prandom_u64_state(&prng);

		__intel_timeline_sync_set(&tl, id, 0);
		count++;
	} while (!time_after(jiffies, end_time));
	kt = ktime_sub(ktime_get(), kt);
	kt = ktime_sub_ns(kt, (count * prng32_1M * 2) >> 20);
	pr_info("%s: %lu random insertions, %lluns/insert\n",
		__func__, count, (long long)div64_ul(ktime_to_ns(kt), count));

	/* Benchmark looking up the exact same context ids as we just set */
	prandom_seed_state(&prng, i915_selftest.random_seed);
	end_time = count;
	kt = ktime_get();
	while (end_time--) {
		u64 id = i915_prandom_u64_state(&prng);

		if (!__intel_timeline_sync_is_later(&tl, id, 0)) {
			mock_timeline_fini(&tl);
			pr_err("Lookup of %llu failed\n", id);
			return -EINVAL;
		}
	}
	kt = ktime_sub(ktime_get(), kt);
	kt = ktime_sub_ns(kt, (count * prng32_1M * 2) >> 20);
	pr_info("%s: %lu random lookups, %lluns/lookup\n",
		__func__, count, (long long)div64_ul(ktime_to_ns(kt), count));

	mock_timeline_fini(&tl);
	cond_resched();

	mock_timeline_init(&tl, 0);

	/* Benchmark setting the first N (in order) contexts */
	count = 0;
	kt = ktime_get();
	end_time = jiffies + HZ/10;
	do {
		__intel_timeline_sync_set(&tl, count++, 0);
	} while (!time_after(jiffies, end_time));
	kt = ktime_sub(ktime_get(), kt);
	pr_info("%s: %lu in-order insertions, %lluns/insert\n",
		__func__, count, (long long)div64_ul(ktime_to_ns(kt), count));

	/* Benchmark looking up the exact same context ids as we just set */
	end_time = count;
	kt = ktime_get();
	while (end_time--) {
		if (!__intel_timeline_sync_is_later(&tl, end_time, 0)) {
			pr_err("Lookup of %lu failed\n", end_time);
			mock_timeline_fini(&tl);
			return -EINVAL;
		}
	}
	kt = ktime_sub(ktime_get(), kt);
	pr_info("%s: %lu in-order lookups, %lluns/lookup\n",
		__func__, count, (long long)div64_ul(ktime_to_ns(kt), count));

	mock_timeline_fini(&tl);
	cond_resched();

	mock_timeline_init(&tl, 0);

	/* Benchmark searching for a random context id and maybe changing it */
	prandom_seed_state(&prng, i915_selftest.random_seed);
	count = 0;
	kt = ktime_get();
	end_time = jiffies + HZ/10;
	do {
		u32 id = random_engine(&prng);
		u32 seqno = prandom_u32_state(&prng);

		if (!__intel_timeline_sync_is_later(&tl, id, seqno))
			__intel_timeline_sync_set(&tl, id, seqno);

		count++;
	} while (!time_after(jiffies, end_time));
	kt = ktime_sub(ktime_get(), kt);
	kt = ktime_sub_ns(kt, (count * prng32_1M * 2) >> 20);
	pr_info("%s: %lu repeated insert/lookups, %lluns/op\n",
		__func__, count, (long long)div64_ul(ktime_to_ns(kt), count));
	mock_timeline_fini(&tl);
	cond_resched();

	/* Benchmark searching for a known context id and changing the seqno */
	for (last_order = 1, order = 1; order < 32;
	     ({ int tmp = last_order; last_order = order; order += tmp; })) {
		unsigned int mask = BIT(order) - 1;

		mock_timeline_init(&tl, 0);

		count = 0;
		kt = ktime_get();
		end_time = jiffies + HZ/10;
		do {
			/* Without assuming too many details of the underlying
			 * implementation, try to identify its phase-changes
			 * (if any)!
			 */
			u64 id = (u64)(count & mask) << order;

			__intel_timeline_sync_is_later(&tl, id, 0);
			__intel_timeline_sync_set(&tl, id, 0);

			count++;
		} while (!time_after(jiffies, end_time));
		kt = ktime_sub(ktime_get(), kt);
		pr_info("%s: %lu cyclic/%d insert/lookups, %lluns/op\n",
			__func__, count, order,
			(long long)div64_ul(ktime_to_ns(kt), count));
		mock_timeline_fini(&tl);
		cond_resched();
	}

	return 0;
}

int intel_timeline_mock_selftests(void)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(mock_hwsp_freelist),
		SUBTEST(igt_sync),
		SUBTEST(bench_sync),
	};

	return i915_subtests(tests, NULL);
}

static int emit_ggtt_store_dw(struct i915_request *rq, u32 addr, u32 value)
{
	u32 *cs;

	cs = intel_ring_begin(rq, 4);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	if (GRAPHICS_VER(rq->engine->i915) >= 8) {
		*cs++ = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT;
		*cs++ = addr;
		*cs++ = 0;
		*cs++ = value;
	} else if (GRAPHICS_VER(rq->engine->i915) >= 4) {
		*cs++ = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT;
		*cs++ = 0;
		*cs++ = addr;
		*cs++ = value;
	} else {
		*cs++ = MI_STORE_DWORD_IMM | MI_MEM_VIRTUAL;
		*cs++ = addr;
		*cs++ = value;
		*cs++ = MI_NOOP;
	}

	intel_ring_advance(rq, cs);

	return 0;
}

static struct i915_request *
checked_tl_write(struct intel_timeline *tl, struct intel_engine_cs *engine, u32 value)
{
	struct i915_request *rq;
	int err;

	err = selftest_tl_pin(tl);
	if (err) {
		rq = ERR_PTR(err);
		goto out;
	}

	if (READ_ONCE(*tl->hwsp_seqno) != tl->seqno) {
		pr_err("Timeline created with incorrect breadcrumb, found %x, expected %x\n",
		       *tl->hwsp_seqno, tl->seqno);
		intel_timeline_unpin(tl);
		return ERR_PTR(-EINVAL);
	}

	rq = intel_engine_create_kernel_request(engine);
	if (IS_ERR(rq))
		goto out_unpin;

	i915_request_get(rq);

	err = emit_ggtt_store_dw(rq, tl->hwsp_offset, value);
	i915_request_add(rq);
	if (err) {
		i915_request_put(rq);
		rq = ERR_PTR(err);
	}

out_unpin:
	intel_timeline_unpin(tl);
out:
	if (IS_ERR(rq))
		pr_err("Failed to write to timeline!\n");
	return rq;
}

static int live_hwsp_engine(void *arg)
{
#define NUM_TIMELINES 4096
	struct intel_gt *gt = arg;
	struct intel_timeline **timelines;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	unsigned long count, n;
	int err = 0;

	/*
	 * Create a bunch of timelines and check we can write
	 * independently to each of their breadcrumb slots.
	 */

	timelines = kvmalloc_array(NUM_TIMELINES * I915_NUM_ENGINES,
				   sizeof(*timelines),
				   GFP_KERNEL);
	if (!timelines)
		return -ENOMEM;

	count = 0;
	for_each_engine(engine, gt, id) {
		if (!intel_engine_can_store_dword(engine))
			continue;

		intel_engine_pm_get(engine);

		for (n = 0; n < NUM_TIMELINES; n++) {
			struct intel_timeline *tl;
			struct i915_request *rq;

			tl = intel_timeline_create(gt);
			if (IS_ERR(tl)) {
				err = PTR_ERR(tl);
				break;
			}

			rq = checked_tl_write(tl, engine, count);
			if (IS_ERR(rq)) {
				intel_timeline_put(tl);
				err = PTR_ERR(rq);
				break;
			}

			timelines[count++] = tl;
			i915_request_put(rq);
		}

		intel_engine_pm_put(engine);
		if (err)
			break;
	}

	if (igt_flush_test(gt->i915))
		err = -EIO;

	for (n = 0; n < count; n++) {
		struct intel_timeline *tl = timelines[n];

		if (!err && READ_ONCE(*tl->hwsp_seqno) != n) {
			GEM_TRACE_ERR("Invalid seqno:%lu stored in timeline %llu @ %x, found 0x%x\n",
				      n, tl->fence_context, tl->hwsp_offset, *tl->hwsp_seqno);
			GEM_TRACE_DUMP();
			err = -EINVAL;
		}
		intel_timeline_put(tl);
	}

	kvfree(timelines);
	return err;
#undef NUM_TIMELINES
}

static int live_hwsp_alternate(void *arg)
{
#define NUM_TIMELINES 4096
	struct intel_gt *gt = arg;
	struct intel_timeline **timelines;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	unsigned long count, n;
	int err = 0;

	/*
	 * Create a bunch of timelines and check we can write
	 * independently to each of their breadcrumb slots with adjacent
	 * engines.
	 */

	timelines = kvmalloc_array(NUM_TIMELINES * I915_NUM_ENGINES,
				   sizeof(*timelines),
				   GFP_KERNEL);
	if (!timelines)
		return -ENOMEM;

	count = 0;
	for (n = 0; n < NUM_TIMELINES; n++) {
		for_each_engine(engine, gt, id) {
			struct intel_timeline *tl;
			struct i915_request *rq;

			if (!intel_engine_can_store_dword(engine))
				continue;

			tl = intel_timeline_create(gt);
			if (IS_ERR(tl)) {
				err = PTR_ERR(tl);
				goto out;
			}

			intel_engine_pm_get(engine);
			rq = checked_tl_write(tl, engine, count);
			intel_engine_pm_put(engine);
			if (IS_ERR(rq)) {
				intel_timeline_put(tl);
				err = PTR_ERR(rq);
				goto out;
			}

			timelines[count++] = tl;
			i915_request_put(rq);
		}
	}

out:
	if (igt_flush_test(gt->i915))
		err = -EIO;

	for (n = 0; n < count; n++) {
		struct intel_timeline *tl = timelines[n];

		if (!err && READ_ONCE(*tl->hwsp_seqno) != n) {
			GEM_TRACE_ERR("Invalid seqno:%lu stored in timeline %llu @ %x, found 0x%x\n",
				      n, tl->fence_context, tl->hwsp_offset, *tl->hwsp_seqno);
			GEM_TRACE_DUMP();
			err = -EINVAL;
		}
		intel_timeline_put(tl);
	}

	kvfree(timelines);
	return err;
#undef NUM_TIMELINES
}

static int live_hwsp_wrap(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	struct intel_timeline *tl;
	enum intel_engine_id id;
	int err = 0;

	/*
	 * Across a seqno wrap, we need to keep the old cacheline alive for
	 * foreign GPU references.
	 */

	tl = intel_timeline_create(gt);
	if (IS_ERR(tl))
		return PTR_ERR(tl);

	if (!tl->has_initial_breadcrumb)
		goto out_free;

	err = selftest_tl_pin(tl);
	if (err)
		goto out_free;

	for_each_engine(engine, gt, id) {
		const u32 *hwsp_seqno[2];
		struct i915_request *rq;
		u32 seqno[2];

		if (!intel_engine_can_store_dword(engine))
			continue;

		rq = intel_engine_create_kernel_request(engine);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto out;
		}

		tl->seqno = -4u;

		mutex_lock_nested(&tl->mutex, SINGLE_DEPTH_NESTING);
		err = intel_timeline_get_seqno(tl, rq, &seqno[0]);
		mutex_unlock(&tl->mutex);
		if (err) {
			i915_request_add(rq);
			goto out;
		}
		pr_debug("seqno[0]:%08x, hwsp_offset:%08x\n",
			 seqno[0], tl->hwsp_offset);

		err = emit_ggtt_store_dw(rq, tl->hwsp_offset, seqno[0]);
		if (err) {
			i915_request_add(rq);
			goto out;
		}
		hwsp_seqno[0] = tl->hwsp_seqno;

		mutex_lock_nested(&tl->mutex, SINGLE_DEPTH_NESTING);
		err = intel_timeline_get_seqno(tl, rq, &seqno[1]);
		mutex_unlock(&tl->mutex);
		if (err) {
			i915_request_add(rq);
			goto out;
		}
		pr_debug("seqno[1]:%08x, hwsp_offset:%08x\n",
			 seqno[1], tl->hwsp_offset);

		err = emit_ggtt_store_dw(rq, tl->hwsp_offset, seqno[1]);
		if (err) {
			i915_request_add(rq);
			goto out;
		}
		hwsp_seqno[1] = tl->hwsp_seqno;

		/* With wrap should come a new hwsp */
		GEM_BUG_ON(seqno[1] >= seqno[0]);
		GEM_BUG_ON(hwsp_seqno[0] == hwsp_seqno[1]);

		i915_request_add(rq);

		if (i915_request_wait(rq, 0, HZ / 5) < 0) {
			pr_err("Wait for timeline writes timed out!\n");
			err = -EIO;
			goto out;
		}

		if (READ_ONCE(*hwsp_seqno[0]) != seqno[0] ||
		    READ_ONCE(*hwsp_seqno[1]) != seqno[1]) {
			pr_err("Bad timeline values: found (%x, %x), expected (%x, %x)\n",
			       *hwsp_seqno[0], *hwsp_seqno[1],
			       seqno[0], seqno[1]);
			err = -EINVAL;
			goto out;
		}

		intel_gt_retire_requests(gt); /* recycle HWSP */
	}

out:
	if (igt_flush_test(gt->i915))
		err = -EIO;

	intel_timeline_unpin(tl);
out_free:
	intel_timeline_put(tl);
	return err;
}

static int emit_read_hwsp(struct i915_request *rq,
			  u32 seqno, u32 hwsp,
			  u32 *addr)
{
	const u32 gpr = i915_mmio_reg_offset(GEN8_RING_CS_GPR(rq->engine->mmio_base, 0));
	u32 *cs;

	cs = intel_ring_begin(rq, 12);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT;
	*cs++ = *addr;
	*cs++ = 0;
	*cs++ = seqno;
	*addr += 4;

	*cs++ = MI_LOAD_REGISTER_MEM_GEN8 | MI_USE_GGTT;
	*cs++ = gpr;
	*cs++ = hwsp;
	*cs++ = 0;

	*cs++ = MI_STORE_REGISTER_MEM_GEN8 | MI_USE_GGTT;
	*cs++ = gpr;
	*cs++ = *addr;
	*cs++ = 0;
	*addr += 4;

	intel_ring_advance(rq, cs);

	return 0;
}

struct hwsp_watcher {
	struct i915_vma *vma;
	struct i915_request *rq;
	u32 addr;
	u32 *map;
};

static bool cmp_lt(u32 a, u32 b)
{
	return a < b;
}

static bool cmp_gte(u32 a, u32 b)
{
	return a >= b;
}

static int setup_watcher(struct hwsp_watcher *w, struct intel_gt *gt)
{
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;

	obj = i915_gem_object_create_internal(gt->i915, SZ_2M);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	w->map = i915_gem_object_pin_map_unlocked(obj, I915_MAP_WB);
	if (IS_ERR(w->map)) {
		i915_gem_object_put(obj);
		return PTR_ERR(w->map);
	}

	vma = i915_gem_object_ggtt_pin(obj, NULL, 0, 0, 0);
	if (IS_ERR(vma)) {
		i915_gem_object_put(obj);
		return PTR_ERR(vma);
	}

	w->vma = vma;
	w->addr = i915_ggtt_offset(vma);
	return 0;
}

static void switch_tl_lock(struct i915_request *from, struct i915_request *to)
{
	/* some light mutex juggling required; think co-routines */

	if (from) {
		lockdep_unpin_lock(&from->context->timeline->mutex, from->cookie);
		mutex_unlock(&from->context->timeline->mutex);
	}

	if (to) {
		mutex_lock(&to->context->timeline->mutex);
		to->cookie = lockdep_pin_lock(&to->context->timeline->mutex);
	}
}

static int create_watcher(struct hwsp_watcher *w,
			  struct intel_engine_cs *engine,
			  int ringsz)
{
	struct intel_context *ce;

	ce = intel_context_create(engine);
	if (IS_ERR(ce))
		return PTR_ERR(ce);

	ce->ring = __intel_context_ring_size(ringsz);
	w->rq = intel_context_create_request(ce);
	intel_context_put(ce);
	if (IS_ERR(w->rq))
		return PTR_ERR(w->rq);

	w->addr = i915_ggtt_offset(w->vma);

	switch_tl_lock(w->rq, NULL);

	return 0;
}

static int check_watcher(struct hwsp_watcher *w, const char *name,
			 bool (*op)(u32 hwsp, u32 seqno))
{
	struct i915_request *rq = fetch_and_zero(&w->rq);
	u32 offset, end;
	int err;

	GEM_BUG_ON(w->addr - i915_ggtt_offset(w->vma) > w->vma->size);

	i915_request_get(rq);
	switch_tl_lock(NULL, rq);
	i915_request_add(rq);

	if (i915_request_wait(rq, 0, HZ) < 0) {
		err = -ETIME;
		goto out;
	}

	err = 0;
	offset = 0;
	end = (w->addr - i915_ggtt_offset(w->vma)) / sizeof(*w->map);
	while (offset < end) {
		if (!op(w->map[offset + 1], w->map[offset])) {
			pr_err("Watcher '%s' found HWSP value %x for seqno %x\n",
			       name, w->map[offset + 1], w->map[offset]);
			err = -EINVAL;
		}

		offset += 2;
	}

out:
	i915_request_put(rq);
	return err;
}

static void cleanup_watcher(struct hwsp_watcher *w)
{
	if (w->rq) {
		switch_tl_lock(NULL, w->rq);

		i915_request_add(w->rq);
	}

	i915_vma_unpin_and_release(&w->vma, I915_VMA_RELEASE_MAP);
}

static bool retire_requests(struct intel_timeline *tl)
{
	struct i915_request *rq, *rn;

	mutex_lock(&tl->mutex);
	list_for_each_entry_safe(rq, rn, &tl->requests, link)
		if (!i915_request_retire(rq))
			break;
	mutex_unlock(&tl->mutex);

	return !i915_active_fence_isset(&tl->last_request);
}

static struct i915_request *wrap_timeline(struct i915_request *rq)
{
	struct intel_context *ce = rq->context;
	struct intel_timeline *tl = ce->timeline;
	u32 seqno = rq->fence.seqno;

	while (tl->seqno >= seqno) { /* Cause a wrap */
		i915_request_put(rq);
		rq = intel_context_create_request(ce);
		if (IS_ERR(rq))
			return rq;

		i915_request_get(rq);
		i915_request_add(rq);
	}

	i915_request_put(rq);
	rq = i915_request_create(ce);
	if (IS_ERR(rq))
		return rq;

	i915_request_get(rq);
	i915_request_add(rq);

	return rq;
}

static int live_hwsp_read(void *arg)
{
	struct intel_gt *gt = arg;
	struct hwsp_watcher watcher[2] = {};
	struct intel_engine_cs *engine;
	struct intel_timeline *tl;
	enum intel_engine_id id;
	int err = 0;
	int i;

	/*
	 * If we take a reference to the HWSP for reading on the GPU, that
	 * read may be arbitrarily delayed (either by foreign fence or
	 * priority saturation) and a wrap can happen within 30 minutes.
	 * When the GPU read is finally submitted it should be correct,
	 * even across multiple wraps.
	 */

	if (GRAPHICS_VER(gt->i915) < 8) /* CS convenience [SRM/LRM] */
		return 0;

	tl = intel_timeline_create(gt);
	if (IS_ERR(tl))
		return PTR_ERR(tl);

	if (!tl->has_initial_breadcrumb)
		goto out_free;

	for (i = 0; i < ARRAY_SIZE(watcher); i++) {
		err = setup_watcher(&watcher[i], gt);
		if (err)
			goto out;
	}

	for_each_engine(engine, gt, id) {
		struct intel_context *ce;
		unsigned long count = 0;
		IGT_TIMEOUT(end_time);

		/* Create a request we can use for remote reading of the HWSP */
		err = create_watcher(&watcher[1], engine, SZ_512K);
		if (err)
			goto out;

		do {
			struct i915_sw_fence *submit;
			struct i915_request *rq;
			u32 hwsp, dummy;

			submit = heap_fence_create(GFP_KERNEL);
			if (!submit) {
				err = -ENOMEM;
				goto out;
			}

			err = create_watcher(&watcher[0], engine, SZ_4K);
			if (err)
				goto out;

			ce = intel_context_create(engine);
			if (IS_ERR(ce)) {
				err = PTR_ERR(ce);
				goto out;
			}

			ce->timeline = intel_timeline_get(tl);

			/* Ensure timeline is mapped, done during first pin */
			err = intel_context_pin(ce);
			if (err) {
				intel_context_put(ce);
				goto out;
			}

			/*
			 * Start at a new wrap, and set seqno right before another wrap,
			 * saving 30 minutes of nops
			 */
			tl->seqno = -12u + 2 * (count & 3);
			__intel_timeline_get_seqno(tl, &dummy);

			rq = i915_request_create(ce);
			if (IS_ERR(rq)) {
				err = PTR_ERR(rq);
				intel_context_unpin(ce);
				intel_context_put(ce);
				goto out;
			}

			err = i915_sw_fence_await_dma_fence(&rq->submit,
							    &watcher[0].rq->fence, 0,
							    GFP_KERNEL);
			if (err < 0) {
				i915_request_add(rq);
				intel_context_unpin(ce);
				intel_context_put(ce);
				goto out;
			}

			switch_tl_lock(rq, watcher[0].rq);
			err = intel_timeline_read_hwsp(rq, watcher[0].rq, &hwsp);
			if (err == 0)
				err = emit_read_hwsp(watcher[0].rq, /* before */
						     rq->fence.seqno, hwsp,
						     &watcher[0].addr);
			switch_tl_lock(watcher[0].rq, rq);
			if (err) {
				i915_request_add(rq);
				intel_context_unpin(ce);
				intel_context_put(ce);
				goto out;
			}

			switch_tl_lock(rq, watcher[1].rq);
			err = intel_timeline_read_hwsp(rq, watcher[1].rq, &hwsp);
			if (err == 0)
				err = emit_read_hwsp(watcher[1].rq, /* after */
						     rq->fence.seqno, hwsp,
						     &watcher[1].addr);
			switch_tl_lock(watcher[1].rq, rq);
			if (err) {
				i915_request_add(rq);
				intel_context_unpin(ce);
				intel_context_put(ce);
				goto out;
			}

			i915_request_get(rq);
			i915_request_add(rq);

			rq = wrap_timeline(rq);
			intel_context_unpin(ce);
			intel_context_put(ce);
			if (IS_ERR(rq)) {
				err = PTR_ERR(rq);
				goto out;
			}

			err = i915_sw_fence_await_dma_fence(&watcher[1].rq->submit,
							    &rq->fence, 0,
							    GFP_KERNEL);
			if (err < 0) {
				i915_request_put(rq);
				goto out;
			}

			err = check_watcher(&watcher[0], "before", cmp_lt);
			i915_sw_fence_commit(submit);
			heap_fence_put(submit);
			if (err) {
				i915_request_put(rq);
				goto out;
			}
			count++;

			/* Flush the timeline before manually wrapping again */
			if (i915_request_wait(rq,
					      I915_WAIT_INTERRUPTIBLE,
					      HZ) < 0) {
				err = -ETIME;
				i915_request_put(rq);
				goto out;
			}
			retire_requests(tl);
			i915_request_put(rq);

			/* Single requests are limited to half a ring at most */
			if (8 * watcher[1].rq->ring->emit >
			    3 * watcher[1].rq->ring->size)
				break;

		} while (!__igt_timeout(end_time, NULL) &&
			 count < (PAGE_SIZE / TIMELINE_SEQNO_BYTES - 1) / 2);

		pr_info("%s: simulated %lu wraps\n", engine->name, count);
		err = check_watcher(&watcher[1], "after", cmp_gte);
		if (err)
			goto out;
	}

out:
	for (i = 0; i < ARRAY_SIZE(watcher); i++)
		cleanup_watcher(&watcher[i]);

	if (igt_flush_test(gt->i915))
		err = -EIO;

out_free:
	intel_timeline_put(tl);
	return err;
}

static int live_hwsp_rollover_kernel(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = 0;

	/*
	 * Run the host for long enough, and even the kernel context will
	 * see a seqno rollover.
	 */

	for_each_engine(engine, gt, id) {
		struct intel_context *ce = engine->kernel_context;
		struct intel_timeline *tl = ce->timeline;
		struct i915_request *rq[3] = {};
		int i;

		st_engine_heartbeat_disable(engine);
		if (intel_gt_wait_for_idle(gt, HZ / 2)) {
			err = -EIO;
			goto out;
		}

		GEM_BUG_ON(i915_active_fence_isset(&tl->last_request));
		tl->seqno = -2u;
		WRITE_ONCE(*(u32 *)tl->hwsp_seqno, tl->seqno);

		for (i = 0; i < ARRAY_SIZE(rq); i++) {
			struct i915_request *this;

			this = i915_request_create(ce);
			if (IS_ERR(this)) {
				err = PTR_ERR(this);
				goto out;
			}

			pr_debug("%s: create fence.seqnp:%d\n",
				 engine->name,
				 lower_32_bits(this->fence.seqno));

			GEM_BUG_ON(rcu_access_pointer(this->timeline) != tl);

			rq[i] = i915_request_get(this);
			i915_request_add(this);
		}

		/* We expected a wrap! */
		GEM_BUG_ON(rq[2]->fence.seqno > rq[0]->fence.seqno);

		if (i915_request_wait(rq[2], 0, HZ / 5) < 0) {
			pr_err("Wait for timeline wrap timed out!\n");
			err = -EIO;
			goto out;
		}

		for (i = 0; i < ARRAY_SIZE(rq); i++) {
			if (!i915_request_completed(rq[i])) {
				pr_err("Pre-wrap request not completed!\n");
				err = -EINVAL;
				goto out;
			}
		}

out:
		for (i = 0; i < ARRAY_SIZE(rq); i++)
			i915_request_put(rq[i]);
		st_engine_heartbeat_enable(engine);
		if (err)
			break;
	}

	if (igt_flush_test(gt->i915))
		err = -EIO;

	return err;
}

static int live_hwsp_rollover_user(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = 0;

	/*
	 * Simulate a long running user context, and force the seqno wrap
	 * on the user's timeline.
	 */

	for_each_engine(engine, gt, id) {
		struct i915_request *rq[3] = {};
		struct intel_timeline *tl;
		struct intel_context *ce;
		int i;

		ce = intel_context_create(engine);
		if (IS_ERR(ce))
			return PTR_ERR(ce);

		err = intel_context_alloc_state(ce);
		if (err)
			goto out;

		tl = ce->timeline;
		if (!tl->has_initial_breadcrumb)
			goto out;

		err = intel_context_pin(ce);
		if (err)
			goto out;

		tl->seqno = -4u;
		WRITE_ONCE(*(u32 *)tl->hwsp_seqno, tl->seqno);

		for (i = 0; i < ARRAY_SIZE(rq); i++) {
			struct i915_request *this;

			this = intel_context_create_request(ce);
			if (IS_ERR(this)) {
				err = PTR_ERR(this);
				goto out_unpin;
			}

			pr_debug("%s: create fence.seqnp:%d\n",
				 engine->name,
				 lower_32_bits(this->fence.seqno));

			GEM_BUG_ON(rcu_access_pointer(this->timeline) != tl);

			rq[i] = i915_request_get(this);
			i915_request_add(this);
		}

		/* We expected a wrap! */
		GEM_BUG_ON(rq[2]->fence.seqno > rq[0]->fence.seqno);

		if (i915_request_wait(rq[2], 0, HZ / 5) < 0) {
			pr_err("Wait for timeline wrap timed out!\n");
			err = -EIO;
			goto out_unpin;
		}

		for (i = 0; i < ARRAY_SIZE(rq); i++) {
			if (!i915_request_completed(rq[i])) {
				pr_err("Pre-wrap request not completed!\n");
				err = -EINVAL;
				goto out_unpin;
			}
		}
out_unpin:
		intel_context_unpin(ce);
out:
		for (i = 0; i < ARRAY_SIZE(rq); i++)
			i915_request_put(rq[i]);
		intel_context_put(ce);
		if (err)
			break;
	}

	if (igt_flush_test(gt->i915))
		err = -EIO;

	return err;
}

static int live_hwsp_recycle(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	unsigned long count;
	int err = 0;

	/*
	 * Check seqno writes into one timeline at a time. We expect to
	 * recycle the breadcrumb slot between iterations and neither
	 * want to confuse ourselves or the GPU.
	 */

	count = 0;
	for_each_engine(engine, gt, id) {
		IGT_TIMEOUT(end_time);

		if (!intel_engine_can_store_dword(engine))
			continue;

		intel_engine_pm_get(engine);

		do {
			struct intel_timeline *tl;
			struct i915_request *rq;

			tl = intel_timeline_create(gt);
			if (IS_ERR(tl)) {
				err = PTR_ERR(tl);
				break;
			}

			rq = checked_tl_write(tl, engine, count);
			if (IS_ERR(rq)) {
				intel_timeline_put(tl);
				err = PTR_ERR(rq);
				break;
			}

			if (i915_request_wait(rq, 0, HZ / 5) < 0) {
				pr_err("Wait for timeline writes timed out!\n");
				i915_request_put(rq);
				intel_timeline_put(tl);
				err = -EIO;
				break;
			}

			if (READ_ONCE(*tl->hwsp_seqno) != count) {
				GEM_TRACE_ERR("Invalid seqno:%lu stored in timeline %llu @ %x found 0x%x\n",
					      count, tl->fence_context,
					      tl->hwsp_offset, *tl->hwsp_seqno);
				GEM_TRACE_DUMP();
				err = -EINVAL;
			}

			i915_request_put(rq);
			intel_timeline_put(tl);
			count++;

			if (err)
				break;
		} while (!__igt_timeout(end_time, NULL));

		intel_engine_pm_put(engine);
		if (err)
			break;
	}

	return err;
}

int intel_timeline_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_hwsp_recycle),
		SUBTEST(live_hwsp_engine),
		SUBTEST(live_hwsp_alternate),
		SUBTEST(live_hwsp_wrap),
		SUBTEST(live_hwsp_read),
		SUBTEST(live_hwsp_rollover_kernel),
		SUBTEST(live_hwsp_rollover_user),
	};

	if (intel_gt_is_wedged(&i915->gt))
		return 0;

	return intel_gt_live_subtests(tests, &i915->gt);
}
