/*
 * Copyright © 2017 Intel Corporation
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
#include "i915_random.h"

#include "mock_gem_device.h"
#include "mock_timeline.h"

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
	struct intel_timeline *tl;
	int order, offset;
	int ret;

	tl = mock_timeline(0);
	if (!tl)
		return -ENOMEM;

	for (p = pass; p->name; p++) {
		for (order = 1; order < 64; order++) {
			for (offset = -1; offset <= (order > 1); offset++) {
				u64 ctx = BIT_ULL(order) + offset;

				ret = __igt_sync(tl, ctx, p, "1");
				if (ret)
					goto out;
			}
		}
	}
	mock_timeline_destroy(tl);

	tl = mock_timeline(0);
	if (!tl)
		return -ENOMEM;

	for (order = 1; order < 64; order++) {
		for (offset = -1; offset <= (order > 1); offset++) {
			u64 ctx = BIT_ULL(order) + offset;

			for (p = pass; p->name; p++) {
				ret = __igt_sync(tl, ctx, p, "2");
				if (ret)
					goto out;
			}
		}
	}

out:
	mock_timeline_destroy(tl);
	return ret;
}

static unsigned int random_engine(struct rnd_state *rnd)
{
	return ((u64)prandom_u32_state(rnd) * I915_NUM_ENGINES) >> 32;
}

static int bench_sync(void *arg)
{
#define M (1 << 20)
	struct rnd_state prng;
	struct intel_timeline *tl;
	unsigned long end_time, count;
	u64 prng32_1M;
	ktime_t kt;
	int order, last_order;

	tl = mock_timeline(0);
	if (!tl)
		return -ENOMEM;

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
	prng32_1M = ktime_to_ns(kt) * M / count;

	/* Benchmark (only) setting random context ids */
	prandom_seed_state(&prng, i915_selftest.random_seed);
	count = 0;
	kt = ktime_get();
	end_time = jiffies + HZ/10;
	do {
		u64 id = i915_prandom_u64_state(&prng);

		__intel_timeline_sync_set(tl, id, 0);
		count++;
	} while (!time_after(jiffies, end_time));
	kt = ktime_sub(ktime_get(), kt);
	kt = ktime_sub_ns(kt, count * prng32_1M * 2 / M);
	pr_info("%s: %lu random insertions, %lluns/insert\n",
		__func__, count, (long long)div64_ul(ktime_to_ns(kt), count));

	/* Benchmark looking up the exact same context ids as we just set */
	prandom_seed_state(&prng, i915_selftest.random_seed);
	end_time = count;
	kt = ktime_get();
	while (end_time--) {
		u64 id = i915_prandom_u64_state(&prng);

		if (!__intel_timeline_sync_is_later(tl, id, 0)) {
			mock_timeline_destroy(tl);
			pr_err("Lookup of %llu failed\n", id);
			return -EINVAL;
		}
	}
	kt = ktime_sub(ktime_get(), kt);
	kt = ktime_sub_ns(kt, count * prng32_1M * 2 / M);
	pr_info("%s: %lu random lookups, %lluns/lookup\n",
		__func__, count, (long long)div64_ul(ktime_to_ns(kt), count));

	mock_timeline_destroy(tl);
	cond_resched();

	tl = mock_timeline(0);
	if (!tl)
		return -ENOMEM;

	/* Benchmark setting the first N (in order) contexts */
	count = 0;
	kt = ktime_get();
	end_time = jiffies + HZ/10;
	do {
		__intel_timeline_sync_set(tl, count++, 0);
	} while (!time_after(jiffies, end_time));
	kt = ktime_sub(ktime_get(), kt);
	pr_info("%s: %lu in-order insertions, %lluns/insert\n",
		__func__, count, (long long)div64_ul(ktime_to_ns(kt), count));

	/* Benchmark looking up the exact same context ids as we just set */
	end_time = count;
	kt = ktime_get();
	while (end_time--) {
		if (!__intel_timeline_sync_is_later(tl, end_time, 0)) {
			pr_err("Lookup of %lu failed\n", end_time);
			mock_timeline_destroy(tl);
			return -EINVAL;
		}
	}
	kt = ktime_sub(ktime_get(), kt);
	pr_info("%s: %lu in-order lookups, %lluns/lookup\n",
		__func__, count, (long long)div64_ul(ktime_to_ns(kt), count));

	mock_timeline_destroy(tl);
	cond_resched();

	tl = mock_timeline(0);
	if (!tl)
		return -ENOMEM;

	/* Benchmark searching for a random context id and maybe changing it */
	prandom_seed_state(&prng, i915_selftest.random_seed);
	count = 0;
	kt = ktime_get();
	end_time = jiffies + HZ/10;
	do {
		u32 id = random_engine(&prng);
		u32 seqno = prandom_u32_state(&prng);

		if (!__intel_timeline_sync_is_later(tl, id, seqno))
			__intel_timeline_sync_set(tl, id, seqno);

		count++;
	} while (!time_after(jiffies, end_time));
	kt = ktime_sub(ktime_get(), kt);
	kt = ktime_sub_ns(kt, count * prng32_1M * 2 / M);
	pr_info("%s: %lu repeated insert/lookups, %lluns/op\n",
		__func__, count, (long long)div64_ul(ktime_to_ns(kt), count));
	mock_timeline_destroy(tl);
	cond_resched();

	/* Benchmark searching for a known context id and changing the seqno */
	for (last_order = 1, order = 1; order < 32;
	     ({ int tmp = last_order; last_order = order; order += tmp; })) {
		unsigned int mask = BIT(order) - 1;

		tl = mock_timeline(0);
		if (!tl)
			return -ENOMEM;

		count = 0;
		kt = ktime_get();
		end_time = jiffies + HZ/10;
		do {
			/* Without assuming too many details of the underlying
			 * implementation, try to identify its phase-changes
			 * (if any)!
			 */
			u64 id = (u64)(count & mask) << order;

			__intel_timeline_sync_is_later(tl, id, 0);
			__intel_timeline_sync_set(tl, id, 0);

			count++;
		} while (!time_after(jiffies, end_time));
		kt = ktime_sub(ktime_get(), kt);
		pr_info("%s: %lu cyclic/%d insert/lookups, %lluns/op\n",
			__func__, count, order,
			(long long)div64_ul(ktime_to_ns(kt), count));
		mock_timeline_destroy(tl);
		cond_resched();
	}

	return 0;
#undef M
}

int i915_gem_timeline_mock_selftests(void)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_sync),
		SUBTEST(bench_sync),
	};

	return i915_subtests(tests, NULL);
}
