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
#include "i915_random.h"

#include "mock_engine.h"

static int check_rbtree(struct intel_engine_cs *engine,
			const unsigned long *bitmap,
			const struct intel_wait *waiters,
			const int count)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;
	struct rb_node *rb;
	int n;

	if (&b->first_wait->node != rb_first(&b->waiters)) {
		pr_err("First waiter does not match first element of wait-tree\n");
		return -EINVAL;
	}

	n = find_first_bit(bitmap, count);
	for (rb = rb_first(&b->waiters); rb; rb = rb_next(rb)) {
		struct intel_wait *w = container_of(rb, typeof(*w), node);
		int idx = w - waiters;

		if (!test_bit(idx, bitmap)) {
			pr_err("waiter[%d, seqno=%d] removed but still in wait-tree\n",
			       idx, w->seqno);
			return -EINVAL;
		}

		if (n != idx) {
			pr_err("waiter[%d, seqno=%d] does not match expected next element in tree [%d]\n",
			       idx, w->seqno, n);
			return -EINVAL;
		}

		n = find_next_bit(bitmap, count, n + 1);
	}

	return 0;
}

static int check_rbtree_empty(struct intel_engine_cs *engine)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;

	if (b->first_wait) {
		pr_err("Empty breadcrumbs still has a waiter\n");
		return -EINVAL;
	}

	if (!RB_EMPTY_ROOT(&b->waiters)) {
		pr_err("Empty breadcrumbs, but wait-tree not empty\n");
		return -EINVAL;
	}

	return 0;
}

static int igt_random_insert_remove(void *arg)
{
	const u32 seqno_bias = 0x1000;
	I915_RND_STATE(prng);
	struct intel_engine_cs *engine = arg;
	struct intel_wait *waiters;
	const int count = 4096;
	unsigned int *order;
	unsigned long *bitmap;
	int err = -ENOMEM;
	int n;

	mock_engine_reset(engine);

	waiters = drm_malloc_gfp(count, sizeof(*waiters), GFP_TEMPORARY);
	if (!waiters)
		goto out_engines;

	bitmap = kcalloc(DIV_ROUND_UP(count, BITS_PER_LONG), sizeof(*bitmap),
			 GFP_TEMPORARY);
	if (!bitmap)
		goto out_waiters;

	order = i915_random_order(count, &prng);
	if (!order)
		goto out_bitmap;

	for (n = 0; n < count; n++)
		intel_wait_init(&waiters[n], seqno_bias + n);

	err = check_rbtree(engine, bitmap, waiters, count);
	if (err)
		goto out_order;

	/* Add and remove waiters into the rbtree in random order. At each
	 * step, we verify that the rbtree is correctly ordered.
	 */
	for (n = 0; n < count; n++) {
		int i = order[n];

		intel_engine_add_wait(engine, &waiters[i]);
		__set_bit(i, bitmap);

		err = check_rbtree(engine, bitmap, waiters, count);
		if (err)
			goto out_order;
	}

	i915_random_reorder(order, count, &prng);
	for (n = 0; n < count; n++) {
		int i = order[n];

		intel_engine_remove_wait(engine, &waiters[i]);
		__clear_bit(i, bitmap);

		err = check_rbtree(engine, bitmap, waiters, count);
		if (err)
			goto out_order;
	}

	err = check_rbtree_empty(engine);
out_order:
	kfree(order);
out_bitmap:
	kfree(bitmap);
out_waiters:
	drm_free_large(waiters);
out_engines:
	mock_engine_flush(engine);
	return err;
}

int intel_breadcrumbs_mock_selftests(void)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_random_insert_remove),
	};
	struct intel_engine_cs *engine;
	int err;

	engine = mock_engine("mock");
	if (!engine)
		return -ENOMEM;

	err = i915_subtests(tests, engine);
	kfree(engine);

	return err;
}
