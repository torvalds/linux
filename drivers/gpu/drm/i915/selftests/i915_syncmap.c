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

static char *
__sync_print(struct i915_syncmap *p,
	     char *buf, unsigned long *sz,
	     unsigned int depth,
	     unsigned int last,
	     unsigned int idx)
{
	unsigned long len;
	unsigned int i, X;

	if (depth) {
		unsigned int d;

		for (d = 0; d < depth - 1; d++) {
			if (last & BIT(depth - d - 1))
				len = scnprintf(buf, *sz, "|   ");
			else
				len = scnprintf(buf, *sz, "    ");
			buf += len;
			*sz -= len;
		}
		len = scnprintf(buf, *sz, "%x-> ", idx);
		buf += len;
		*sz -= len;
	}

	/* We mark bits after the prefix as "X" */
	len = scnprintf(buf, *sz, "0x%016llx", p->prefix << p->height << SHIFT);
	buf += len;
	*sz -= len;
	X = (p->height + SHIFT) / 4;
	scnprintf(buf - X, *sz + X, "%*s", X, "XXXXXXXXXXXXXXXXX");

	if (!p->height) {
		for_each_set_bit(i, (unsigned long *)&p->bitmap, KSYNCMAP) {
			len = scnprintf(buf, *sz, " %x:%x,",
					i, __sync_seqno(p)[i]);
			buf += len;
			*sz -= len;
		}
		buf -= 1;
		*sz += 1;
	}

	len = scnprintf(buf, *sz, "\n");
	buf += len;
	*sz -= len;

	if (p->height) {
		for_each_set_bit(i, (unsigned long *)&p->bitmap, KSYNCMAP) {
			buf = __sync_print(__sync_child(p)[i], buf, sz,
					   depth + 1,
					   last << 1 | !!(p->bitmap >> (i + 1)),
					   i);
		}
	}

	return buf;
}

static bool
i915_syncmap_print_to_buf(struct i915_syncmap *p, char *buf, unsigned long sz)
{
	if (!p)
		return false;

	while (p->parent)
		p = p->parent;

	__sync_print(p, buf, &sz, 0, 1, 0);
	return true;
}

static int check_syncmap_free(struct i915_syncmap **sync)
{
	i915_syncmap_free(sync);
	if (*sync) {
		pr_err("sync not cleared after free\n");
		return -EINVAL;
	}

	return 0;
}

static int dump_syncmap(struct i915_syncmap *sync, int err)
{
	char *buf;

	if (!err)
		return check_syncmap_free(&sync);

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		goto skip;

	if (i915_syncmap_print_to_buf(sync, buf, PAGE_SIZE))
		pr_err("%s", buf);

	kfree(buf);

skip:
	i915_syncmap_free(&sync);
	return err;
}

static int igt_syncmap_init(void *arg)
{
	struct i915_syncmap *sync = (void *)~0ul;

	/*
	 * Cursory check that we can initialise a random pointer and transform
	 * it into the root pointer of a syncmap.
	 */

	i915_syncmap_init(&sync);
	return check_syncmap_free(&sync);
}

static int check_seqno(struct i915_syncmap *leaf, unsigned int idx, u32 seqno)
{
	if (leaf->height) {
		pr_err("%s: not a leaf, height is %d\n",
		       __func__, leaf->height);
		return -EINVAL;
	}

	if (__sync_seqno(leaf)[idx] != seqno) {
		pr_err("%s: seqno[%d], found %x, expected %x\n",
		       __func__, idx, __sync_seqno(leaf)[idx], seqno);
		return -EINVAL;
	}

	return 0;
}

static int check_one(struct i915_syncmap **sync, u64 context, u32 seqno)
{
	int err;

	err = i915_syncmap_set(sync, context, seqno);
	if (err)
		return err;

	if ((*sync)->height) {
		pr_err("Inserting first context=%llx did not return leaf (height=%d, prefix=%llx\n",
		       context, (*sync)->height, (*sync)->prefix);
		return -EINVAL;
	}

	if ((*sync)->parent) {
		pr_err("Inserting first context=%llx created branches!\n",
		       context);
		return -EINVAL;
	}

	if (hweight32((*sync)->bitmap) != 1) {
		pr_err("First bitmap does not contain a single entry, found %x (count=%d)!\n",
		       (*sync)->bitmap, hweight32((*sync)->bitmap));
		return -EINVAL;
	}

	err = check_seqno((*sync), ilog2((*sync)->bitmap), seqno);
	if (err)
		return err;

	if (!i915_syncmap_is_later(sync, context, seqno)) {
		pr_err("Lookup of first context=%llx/seqno=%x failed!\n",
		       context, seqno);
		return -EINVAL;
	}

	return 0;
}

static int igt_syncmap_one(void *arg)
{
	I915_RND_STATE(prng);
	IGT_TIMEOUT(end_time);
	struct i915_syncmap *sync;
	unsigned long max = 1;
	int err;

	/*
	 * Check that inserting a new id, creates a leaf and only that leaf.
	 */

	i915_syncmap_init(&sync);

	do {
		u64 context = i915_prandom_u64_state(&prng);
		unsigned long loop;

		err = check_syncmap_free(&sync);
		if (err)
			goto out;

		for (loop = 0; loop <= max; loop++) {
			err = check_one(&sync, context,
					prandom_u32_state(&prng));
			if (err)
				goto out;
		}
		max++;
	} while (!__igt_timeout(end_time, NULL));
	pr_debug("%s: Completed %lu single insertions\n",
		 __func__, max * (max - 1) / 2);
out:
	return dump_syncmap(sync, err);
}

static int check_leaf(struct i915_syncmap **sync, u64 context, u32 seqno)
{
	int err;

	err = i915_syncmap_set(sync, context, seqno);
	if (err)
		return err;

	if ((*sync)->height) {
		pr_err("Inserting context=%llx did not return leaf (height=%d, prefix=%llx\n",
		       context, (*sync)->height, (*sync)->prefix);
		return -EINVAL;
	}

	if (hweight32((*sync)->bitmap) != 1) {
		pr_err("First entry into leaf (context=%llx) does not contain a single entry, found %x (count=%d)!\n",
		       context, (*sync)->bitmap, hweight32((*sync)->bitmap));
		return -EINVAL;
	}

	err = check_seqno((*sync), ilog2((*sync)->bitmap), seqno);
	if (err)
		return err;

	if (!i915_syncmap_is_later(sync, context, seqno)) {
		pr_err("Lookup of first entry context=%llx/seqno=%x failed!\n",
		       context, seqno);
		return -EINVAL;
	}

	return 0;
}

static int igt_syncmap_join_above(void *arg)
{
	struct i915_syncmap *sync;
	unsigned int pass, order;
	int err;

	i915_syncmap_init(&sync);

	/*
	 * When we have a new id that doesn't fit inside the existing tree,
	 * we need to add a new layer above.
	 *
	 * 1: 0x00000001
	 * 2: 0x00000010
	 * 3: 0x00000100
	 * 4: 0x00001000
	 * ...
	 * Each pass the common prefix shrinks and we have to insert a join.
	 * Each join will only contain two branches, the latest of which
	 * is always a leaf.
	 *
	 * If we then reuse the same set of contexts, we expect to build an
	 * identical tree.
	 */
	for (pass = 0; pass < 3; pass++) {
		for (order = 0; order < 64; order += SHIFT) {
			u64 context = BIT_ULL(order);
			struct i915_syncmap *join;

			err = check_leaf(&sync, context, 0);
			if (err)
				goto out;

			join = sync->parent;
			if (!join) /* very first insert will have no parents */
				continue;

			if (!join->height) {
				pr_err("Parent with no height!\n");
				err = -EINVAL;
				goto out;
			}

			if (hweight32(join->bitmap) != 2) {
				pr_err("Join does not have 2 children: %x (%d)\n",
				       join->bitmap, hweight32(join->bitmap));
				err = -EINVAL;
				goto out;
			}

			if (__sync_child(join)[__sync_branch_idx(join, context)] != sync) {
				pr_err("Leaf misplaced in parent!\n");
				err = -EINVAL;
				goto out;
			}
		}
	}
out:
	return dump_syncmap(sync, err);
}

static int igt_syncmap_join_below(void *arg)
{
	struct i915_syncmap *sync;
	unsigned int step, order, idx;
	int err;

	i915_syncmap_init(&sync);

	/*
	 * Check that we can split a compacted branch by replacing it with
	 * a join.
	 */
	for (step = 0; step < KSYNCMAP; step++) {
		for (order = 64 - SHIFT; order > 0; order -= SHIFT) {
			u64 context = step * BIT_ULL(order);

			err = i915_syncmap_set(&sync, context, 0);
			if (err)
				goto out;

			if (sync->height) {
				pr_err("Inserting context=%llx (order=%d, step=%d) did not return leaf (height=%d, prefix=%llx\n",
				       context, order, step, sync->height, sync->prefix);
				err = -EINVAL;
				goto out;
			}
		}
	}

	for (step = 0; step < KSYNCMAP; step++) {
		for (order = SHIFT; order < 64; order += SHIFT) {
			u64 context = step * BIT_ULL(order);

			if (!i915_syncmap_is_later(&sync, context, 0)) {
				pr_err("1: context %llx (order=%d, step=%d) not found\n",
				       context, order, step);
				err = -EINVAL;
				goto out;
			}

			for (idx = 1; idx < KSYNCMAP; idx++) {
				if (i915_syncmap_is_later(&sync, context + idx, 0)) {
					pr_err("1: context %llx (order=%d, step=%d) should not exist\n",
					       context + idx, order, step);
					err = -EINVAL;
					goto out;
				}
			}
		}
	}

	for (order = SHIFT; order < 64; order += SHIFT) {
		for (step = 0; step < KSYNCMAP; step++) {
			u64 context = step * BIT_ULL(order);

			if (!i915_syncmap_is_later(&sync, context, 0)) {
				pr_err("2: context %llx (order=%d, step=%d) not found\n",
				       context, order, step);
				err = -EINVAL;
				goto out;
			}
		}
	}

out:
	return dump_syncmap(sync, err);
}

static int igt_syncmap_neighbours(void *arg)
{
	I915_RND_STATE(prng);
	IGT_TIMEOUT(end_time);
	struct i915_syncmap *sync;
	int err;

	/*
	 * Each leaf holds KSYNCMAP seqno. Check that when we create KSYNCMAP
	 * neighbouring ids, they all fit into the same leaf.
	 */

	i915_syncmap_init(&sync);
	do {
		u64 context = i915_prandom_u64_state(&prng) & ~MASK;
		unsigned int idx;

		if (i915_syncmap_is_later(&sync, context, 0)) /* Skip repeats */
			continue;

		for (idx = 0; idx < KSYNCMAP; idx++) {
			err = i915_syncmap_set(&sync, context + idx, 0);
			if (err)
				goto out;

			if (sync->height) {
				pr_err("Inserting context=%llx did not return leaf (height=%d, prefix=%llx\n",
				       context, sync->height, sync->prefix);
				err = -EINVAL;
				goto out;
			}

			if (sync->bitmap != BIT(idx + 1) - 1) {
				pr_err("Inserting neighbouring context=0x%llx+%d, did not fit into the same leaf bitmap=%x (%d), expected %lx (%d)\n",
				       context, idx,
				       sync->bitmap, hweight32(sync->bitmap),
				       BIT(idx + 1) - 1, idx + 1);
				err = -EINVAL;
				goto out;
			}
		}
	} while (!__igt_timeout(end_time, NULL));
out:
	return dump_syncmap(sync, err);
}

static int igt_syncmap_compact(void *arg)
{
	struct i915_syncmap *sync;
	unsigned int idx, order;
	int err;

	i915_syncmap_init(&sync);

	/*
	 * The syncmap are "space efficient" compressed radix trees - any
	 * branch with only one child is skipped and replaced by the child.
	 *
	 * If we construct a tree with ids that are neighbouring at a non-zero
	 * height, we form a join but each child of that join is directly a
	 * leaf holding the single id.
	 */
	for (order = SHIFT; order < 64; order += SHIFT) {
		err = check_syncmap_free(&sync);
		if (err)
			goto out;

		/* Create neighbours in the parent */
		for (idx = 0; idx < KSYNCMAP; idx++) {
			u64 context = idx * BIT_ULL(order) + idx;

			err = i915_syncmap_set(&sync, context, 0);
			if (err)
				goto out;

			if (sync->height) {
				pr_err("Inserting context=%llx (order=%d, idx=%d) did not return leaf (height=%d, prefix=%llx\n",
				       context, order, idx,
				       sync->height, sync->prefix);
				err = -EINVAL;
				goto out;
			}
		}

		sync = sync->parent;
		if (sync->parent) {
			pr_err("Parent (join) of last leaf was not the sync!\n");
			err = -EINVAL;
			goto out;
		}

		if (sync->height != order) {
			pr_err("Join does not have the expected height, found %d, expected %d\n",
			       sync->height, order);
			err = -EINVAL;
			goto out;
		}

		if (sync->bitmap != BIT(KSYNCMAP) - 1) {
			pr_err("Join is not full!, found %x (%d) expected %lx (%d)\n",
			       sync->bitmap, hweight32(sync->bitmap),
			       BIT(KSYNCMAP) - 1, KSYNCMAP);
			err = -EINVAL;
			goto out;
		}

		/* Each of our children should be a leaf */
		for (idx = 0; idx < KSYNCMAP; idx++) {
			struct i915_syncmap *leaf = __sync_child(sync)[idx];

			if (leaf->height) {
				pr_err("Child %d is a not leaf!\n", idx);
				err = -EINVAL;
				goto out;
			}

			if (leaf->parent != sync) {
				pr_err("Child %d is not attached to us!\n",
				       idx);
				err = -EINVAL;
				goto out;
			}

			if (!is_power_of_2(leaf->bitmap)) {
				pr_err("Child %d holds more than one id, found %x (%d)\n",
				       idx, leaf->bitmap, hweight32(leaf->bitmap));
				err = -EINVAL;
				goto out;
			}

			if (leaf->bitmap != BIT(idx)) {
				pr_err("Child %d has wrong seqno idx, found %d, expected %d\n",
				       idx, ilog2(leaf->bitmap), idx);
				err = -EINVAL;
				goto out;
			}
		}
	}
out:
	return dump_syncmap(sync, err);
}

static int igt_syncmap_random(void *arg)
{
	I915_RND_STATE(prng);
	IGT_TIMEOUT(end_time);
	struct i915_syncmap *sync;
	unsigned long count, phase, i;
	u32 seqno;
	int err;

	i915_syncmap_init(&sync);

	/*
	 * Having tried to test the individual operations within i915_syncmap,
	 * run a smoketest exploring the entire u64 space with random
	 * insertions.
	 */

	count = 0;
	phase = jiffies + HZ/100 + 1;
	do {
		u64 context = i915_prandom_u64_state(&prng);

		err = i915_syncmap_set(&sync, context, 0);
		if (err)
			goto out;

		count++;
	} while (!time_after(jiffies, phase));
	seqno = 0;

	phase = 0;
	do {
		I915_RND_STATE(ctx);
		u32 last_seqno = seqno;
		bool expect;

		seqno = prandom_u32_state(&prng);
		expect = seqno_later(last_seqno, seqno);

		for (i = 0; i < count; i++) {
			u64 context = i915_prandom_u64_state(&ctx);

			if (i915_syncmap_is_later(&sync, context, seqno) != expect) {
				pr_err("context=%llu, last=%u this=%u did not match expectation (%d)\n",
				       context, last_seqno, seqno, expect);
				err = -EINVAL;
				goto out;
			}

			err = i915_syncmap_set(&sync, context, seqno);
			if (err)
				goto out;
		}

		phase++;
	} while (!__igt_timeout(end_time, NULL));
	pr_debug("Completed %lu passes, each of %lu contexts\n", phase, count);
out:
	return dump_syncmap(sync, err);
}

int i915_syncmap_mock_selftests(void)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_syncmap_init),
		SUBTEST(igt_syncmap_one),
		SUBTEST(igt_syncmap_join_above),
		SUBTEST(igt_syncmap_join_below),
		SUBTEST(igt_syncmap_neighbours),
		SUBTEST(igt_syncmap_compact),
		SUBTEST(igt_syncmap_random),
	};

	return i915_subtests(tests, NULL);
}
