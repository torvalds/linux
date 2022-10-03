// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test cases for the drm_mm range manager
 *
 * Copyright (c) 2022 Arthur Grillo <arthur.grillo@usp.br>
 */

#include <kunit/test.h>

#include <linux/prime_numbers.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/vmalloc.h>
#include <linux/ktime.h>

#include <drm/drm_mm.h>

#include "../lib/drm_random.h"

static unsigned int random_seed;
static unsigned int max_iterations = 8192;
static unsigned int max_prime = 128;

enum {
	BEST,
	BOTTOMUP,
	TOPDOWN,
	EVICT,
};

static const struct insert_mode {
	const char *name;
	enum drm_mm_insert_mode mode;
} insert_modes[] = {
	[BEST] = { "best", DRM_MM_INSERT_BEST },
	[BOTTOMUP] = { "bottom-up", DRM_MM_INSERT_LOW },
	[TOPDOWN] = { "top-down", DRM_MM_INSERT_HIGH },
	[EVICT] = { "evict", DRM_MM_INSERT_EVICT },
	{}
}, evict_modes[] = {
	{ "bottom-up", DRM_MM_INSERT_LOW },
	{ "top-down", DRM_MM_INSERT_HIGH },
	{}
};

static bool assert_no_holes(struct kunit *test, const struct drm_mm *mm)
{
	struct drm_mm_node *hole;
	u64 hole_start, __always_unused hole_end;
	unsigned long count;

	count = 0;
	drm_mm_for_each_hole(hole, mm, hole_start, hole_end)
		count++;
	if (count) {
		KUNIT_FAIL(test,
			   "Expected to find no holes (after reserve), found %lu instead\n", count);
		return false;
	}

	drm_mm_for_each_node(hole, mm) {
		if (drm_mm_hole_follows(hole)) {
			KUNIT_FAIL(test, "Hole follows node, expected none!\n");
			return false;
		}
	}

	return true;
}

static bool assert_one_hole(struct kunit *test, const struct drm_mm *mm, u64 start, u64 end)
{
	struct drm_mm_node *hole;
	u64 hole_start, hole_end;
	unsigned long count;
	bool ok = true;

	if (end <= start)
		return true;

	count = 0;
	drm_mm_for_each_hole(hole, mm, hole_start, hole_end) {
		if (start != hole_start || end != hole_end) {
			if (ok)
				KUNIT_FAIL(test,
					   "empty mm has incorrect hole, found (%llx, %llx), expect (%llx, %llx)\n",
					   hole_start, hole_end, start, end);
			ok = false;
		}
		count++;
	}
	if (count != 1) {
		KUNIT_FAIL(test, "Expected to find one hole, found %lu instead\n", count);
		ok = false;
	}

	return ok;
}

static bool assert_continuous(struct kunit *test, const struct drm_mm *mm, u64 size)
{
	struct drm_mm_node *node, *check, *found;
	unsigned long n;
	u64 addr;

	if (!assert_no_holes(test, mm))
		return false;

	n = 0;
	addr = 0;
	drm_mm_for_each_node(node, mm) {
		if (node->start != addr) {
			KUNIT_FAIL(test, "node[%ld] list out of order, expected %llx found %llx\n",
				   n, addr, node->start);
			return false;
		}

		if (node->size != size) {
			KUNIT_FAIL(test, "node[%ld].size incorrect, expected %llx, found %llx\n",
				   n, size, node->size);
			return false;
		}

		if (drm_mm_hole_follows(node)) {
			KUNIT_FAIL(test, "node[%ld] is followed by a hole!\n", n);
			return false;
		}

		found = NULL;
		drm_mm_for_each_node_in_range(check, mm, addr, addr + size) {
			if (node != check) {
				KUNIT_FAIL(test,
					   "lookup return wrong node, expected start %llx, found %llx\n",
					   node->start, check->start);
				return false;
			}
			found = check;
		}
		if (!found) {
			KUNIT_FAIL(test, "lookup failed for node %llx + %llx\n", addr, size);
			return false;
		}

		addr += size;
		n++;
	}

	return true;
}

static u64 misalignment(struct drm_mm_node *node, u64 alignment)
{
	u64 rem;

	if (!alignment)
		return 0;

	div64_u64_rem(node->start, alignment, &rem);
	return rem;
}

static bool assert_node(struct kunit *test, struct drm_mm_node *node, struct drm_mm *mm,
			u64 size, u64 alignment, unsigned long color)
{
	bool ok = true;

	if (!drm_mm_node_allocated(node) || node->mm != mm) {
		KUNIT_FAIL(test, "node not allocated\n");
		ok = false;
	}

	if (node->size != size) {
		KUNIT_FAIL(test, "node has wrong size, found %llu, expected %llu\n",
			   node->size, size);
		ok = false;
	}

	if (misalignment(node, alignment)) {
		KUNIT_FAIL(test,
			   "node is misaligned, start %llx rem %llu, expected alignment %llu\n",
			   node->start, misalignment(node, alignment), alignment);
		ok = false;
	}

	if (node->color != color) {
		KUNIT_FAIL(test, "node has wrong color, found %lu, expected %lu\n",
			   node->color, color);
		ok = false;
	}

	return ok;
}

static void drm_test_mm_init(struct kunit *test)
{
	const unsigned int size = 4096;
	struct drm_mm mm;
	struct drm_mm_node tmp;

	/* Start with some simple checks on initialising the struct drm_mm */
	memset(&mm, 0, sizeof(mm));
	KUNIT_ASSERT_FALSE_MSG(test, drm_mm_initialized(&mm),
			       "zeroed mm claims to be initialized\n");

	memset(&mm, 0xff, sizeof(mm));
	drm_mm_init(&mm, 0, size);
	if (!drm_mm_initialized(&mm)) {
		KUNIT_FAIL(test, "mm claims not to be initialized\n");
		goto out;
	}

	if (!drm_mm_clean(&mm)) {
		KUNIT_FAIL(test, "mm not empty on creation\n");
		goto out;
	}

	/* After creation, it should all be one massive hole */
	if (!assert_one_hole(test, &mm, 0, size)) {
		KUNIT_FAIL(test, "");
		goto out;
	}

	memset(&tmp, 0, sizeof(tmp));
	tmp.start = 0;
	tmp.size = size;
	if (drm_mm_reserve_node(&mm, &tmp)) {
		KUNIT_FAIL(test, "failed to reserve whole drm_mm\n");
		goto out;
	}

	/* After filling the range entirely, there should be no holes */
	if (!assert_no_holes(test, &mm)) {
		KUNIT_FAIL(test, "");
		goto out;
	}

	/* And then after emptying it again, the massive hole should be back */
	drm_mm_remove_node(&tmp);
	if (!assert_one_hole(test, &mm, 0, size)) {
		KUNIT_FAIL(test, "");
		goto out;
	}

out:
	drm_mm_takedown(&mm);
}

static void drm_test_mm_debug(struct kunit *test)
{
	struct drm_mm mm;
	struct drm_mm_node nodes[2];

	/* Create a small drm_mm with a couple of nodes and a few holes, and
	 * check that the debug iterator doesn't explode over a trivial drm_mm.
	 */

	drm_mm_init(&mm, 0, 4096);

	memset(nodes, 0, sizeof(nodes));
	nodes[0].start = 512;
	nodes[0].size = 1024;
	KUNIT_ASSERT_FALSE_MSG(test, drm_mm_reserve_node(&mm, &nodes[0]),
			       "failed to reserve node[0] {start=%lld, size=%lld)\n",
			       nodes[0].start, nodes[0].size);

	nodes[1].size = 1024;
	nodes[1].start = 4096 - 512 - nodes[1].size;
	KUNIT_ASSERT_FALSE_MSG(test, drm_mm_reserve_node(&mm, &nodes[1]),
			       "failed to reserve node[0] {start=%lld, size=%lld)\n",
			       nodes[0].start, nodes[0].size);
}

static struct drm_mm_node *set_node(struct drm_mm_node *node,
				    u64 start, u64 size)
{
	node->start = start;
	node->size = size;
	return node;
}

static bool expect_reserve_fail(struct kunit *test, struct drm_mm *mm, struct drm_mm_node *node)
{
	int err;

	err = drm_mm_reserve_node(mm, node);
	if (likely(err == -ENOSPC))
		return true;

	if (!err) {
		KUNIT_FAIL(test, "impossible reserve succeeded, node %llu + %llu\n",
			   node->start, node->size);
		drm_mm_remove_node(node);
	} else {
		KUNIT_FAIL(test,
			   "impossible reserve failed with wrong error %d [expected %d], node %llu + %llu\n",
		       err, -ENOSPC, node->start, node->size);
	}
	return false;
}

static bool check_reserve_boundaries(struct kunit *test, struct drm_mm *mm,
				     unsigned int count,
				     u64 size)
{
	const struct boundary {
		u64 start, size;
		const char *name;
	} boundaries[] = {
#define B(st, sz) { (st), (sz), "{ " #st ", " #sz "}" }
		B(0, 0),
		B(-size, 0),
		B(size, 0),
		B(size * count, 0),
		B(-size, size),
		B(-size, -size),
		B(-size, 2 * size),
		B(0, -size),
		B(size, -size),
		B(count * size, size),
		B(count * size, -size),
		B(count * size, count * size),
		B(count * size, -count * size),
		B(count * size, -(count + 1) * size),
		B((count + 1) * size, size),
		B((count + 1) * size, -size),
		B((count + 1) * size, -2 * size),
#undef B
	};
	struct drm_mm_node tmp = {};
	int n;

	for (n = 0; n < ARRAY_SIZE(boundaries); n++) {
		if (!expect_reserve_fail(test, mm, set_node(&tmp, boundaries[n].start,
							    boundaries[n].size))) {
			KUNIT_FAIL(test, "boundary[%d:%s] failed, count=%u, size=%lld\n",
				   n, boundaries[n].name, count, size);
			return false;
		}
	}

	return true;
}

static int __drm_test_mm_reserve(struct kunit *test, unsigned int count, u64 size)
{
	DRM_RND_STATE(prng, random_seed);
	struct drm_mm mm;
	struct drm_mm_node tmp, *nodes, *node, *next;
	unsigned int *order, n, m, o = 0;
	int ret, err;

	/* For exercising drm_mm_reserve_node(), we want to check that
	 * reservations outside of the drm_mm range are rejected, and to
	 * overlapping and otherwise already occupied ranges. Afterwards,
	 * the tree and nodes should be intact.
	 */

	DRM_MM_BUG_ON(!count);
	DRM_MM_BUG_ON(!size);

	ret = -ENOMEM;
	order = drm_random_order(count, &prng);
	if (!order)
		goto err;

	nodes = vzalloc(array_size(count, sizeof(*nodes)));
	KUNIT_ASSERT_TRUE(test, nodes);

	ret = -EINVAL;
	drm_mm_init(&mm, 0, count * size);

	if (!check_reserve_boundaries(test, &mm, count, size))
		goto out;

	for (n = 0; n < count; n++) {
		nodes[n].start = order[n] * size;
		nodes[n].size = size;

		err = drm_mm_reserve_node(&mm, &nodes[n]);
		if (err) {
			KUNIT_FAIL(test, "reserve failed, step %d, start %llu\n",
				   n, nodes[n].start);
			ret = err;
			goto out;
		}

		if (!drm_mm_node_allocated(&nodes[n])) {
			KUNIT_FAIL(test, "reserved node not allocated! step %d, start %llu\n",
				   n, nodes[n].start);
			goto out;
		}

		if (!expect_reserve_fail(test, &mm, &nodes[n]))
			goto out;
	}

	/* After random insertion the nodes should be in order */
	if (!assert_continuous(test, &mm, size))
		goto out;

	/* Repeated use should then fail */
	drm_random_reorder(order, count, &prng);
	for (n = 0; n < count; n++) {
		if (!expect_reserve_fail(test, &mm, set_node(&tmp, order[n] * size, 1)))
			goto out;

		/* Remove and reinsert should work */
		drm_mm_remove_node(&nodes[order[n]]);
		err = drm_mm_reserve_node(&mm, &nodes[order[n]]);
		if (err) {
			KUNIT_FAIL(test, "reserve failed, step %d, start %llu\n",
				   n, nodes[n].start);
			ret = err;
			goto out;
		}
	}

	if (!assert_continuous(test, &mm, size))
		goto out;

	/* Overlapping use should then fail */
	for (n = 0; n < count; n++) {
		if (!expect_reserve_fail(test, &mm, set_node(&tmp, 0, size * count)))
			goto out;
	}
	for (n = 0; n < count; n++) {
		if (!expect_reserve_fail(test, &mm, set_node(&tmp, size * n, size * (count - n))))
			goto out;
	}

	/* Remove several, reinsert, check full */
	for_each_prime_number(n, min(max_prime, count)) {
		for (m = 0; m < n; m++) {
			node = &nodes[order[(o + m) % count]];
			drm_mm_remove_node(node);
		}

		for (m = 0; m < n; m++) {
			node = &nodes[order[(o + m) % count]];
			err = drm_mm_reserve_node(&mm, node);
			if (err) {
				KUNIT_FAIL(test, "reserve failed, step %d/%d, start %llu\n",
					   m, n, node->start);
				ret = err;
				goto out;
			}
		}

		o += n;

		if (!assert_continuous(test, &mm, size))
			goto out;
	}

	ret = 0;
out:
	drm_mm_for_each_node_safe(node, next, &mm)
		drm_mm_remove_node(node);
	drm_mm_takedown(&mm);
	vfree(nodes);
	kfree(order);
err:
	return ret;
}

static void drm_test_mm_reserve(struct kunit *test)
{
	const unsigned int count = min_t(unsigned int, BIT(10), max_iterations);
	int n;

	for_each_prime_number_from(n, 1, 54) {
		u64 size = BIT_ULL(n);

		KUNIT_ASSERT_FALSE(test, __drm_test_mm_reserve(test, count, size - 1));
		KUNIT_ASSERT_FALSE(test, __drm_test_mm_reserve(test, count, size));
		KUNIT_ASSERT_FALSE(test, __drm_test_mm_reserve(test, count, size + 1));

		cond_resched();
	}
}

static bool expect_insert(struct kunit *test, struct drm_mm *mm,
			  struct drm_mm_node *node, u64 size, u64 alignment, unsigned long color,
			const struct insert_mode *mode)
{
	int err;

	err = drm_mm_insert_node_generic(mm, node,
					 size, alignment, color,
					 mode->mode);
	if (err) {
		KUNIT_FAIL(test,
			   "insert (size=%llu, alignment=%llu, color=%lu, mode=%s) failed with err=%d\n",
			   size, alignment, color, mode->name, err);
		return false;
	}

	if (!assert_node(test, node, mm, size, alignment, color)) {
		drm_mm_remove_node(node);
		return false;
	}

	return true;
}

static bool expect_insert_fail(struct kunit *test, struct drm_mm *mm, u64 size)
{
	struct drm_mm_node tmp = {};
	int err;

	err = drm_mm_insert_node(mm, &tmp, size);
	if (likely(err == -ENOSPC))
		return true;

	if (!err) {
		KUNIT_FAIL(test, "impossible insert succeeded, node %llu + %llu\n",
			   tmp.start, tmp.size);
		drm_mm_remove_node(&tmp);
	} else {
		KUNIT_FAIL(test,
			   "impossible insert failed with wrong error %d [expected %d], size %llu\n",
			   err, -ENOSPC, size);
	}
	return false;
}

static int __drm_test_mm_insert(struct kunit *test, unsigned int count, u64 size, bool replace)
{
	DRM_RND_STATE(prng, random_seed);
	const struct insert_mode *mode;
	struct drm_mm mm;
	struct drm_mm_node *nodes, *node, *next;
	unsigned int *order, n, m, o = 0;
	int ret;

	/* Fill a range with lots of nodes, check it doesn't fail too early */

	DRM_MM_BUG_ON(!count);
	DRM_MM_BUG_ON(!size);

	ret = -ENOMEM;
	nodes = vmalloc(array_size(count, sizeof(*nodes)));
	KUNIT_ASSERT_TRUE(test, nodes);

	order = drm_random_order(count, &prng);
	if (!order)
		goto err_nodes;

	ret = -EINVAL;
	drm_mm_init(&mm, 0, count * size);

	for (mode = insert_modes; mode->name; mode++) {
		for (n = 0; n < count; n++) {
			struct drm_mm_node tmp;

			node = replace ? &tmp : &nodes[n];
			memset(node, 0, sizeof(*node));
			if (!expect_insert(test, &mm, node, size, 0, n, mode)) {
				KUNIT_FAIL(test, "%s insert failed, size %llu step %d\n",
					   mode->name, size, n);
				goto out;
			}

			if (replace) {
				drm_mm_replace_node(&tmp, &nodes[n]);
				if (drm_mm_node_allocated(&tmp)) {
					KUNIT_FAIL(test,
						   "replaced old-node still allocated! step %d\n",
						   n);
					goto out;
				}

				if (!assert_node(test, &nodes[n], &mm, size, 0, n)) {
					KUNIT_FAIL(test,
						   "replaced node did not inherit parameters, size %llu step %d\n",
						   size, n);
					goto out;
				}

				if (tmp.start != nodes[n].start) {
					KUNIT_FAIL(test,
						   "replaced node mismatch location expected [%llx + %llx], found [%llx + %llx]\n",
						   tmp.start, size, nodes[n].start, nodes[n].size);
					goto out;
				}
			}
		}

		/* After random insertion the nodes should be in order */
		if (!assert_continuous(test, &mm, size))
			goto out;

		/* Repeated use should then fail */
		if (!expect_insert_fail(test, &mm, size))
			goto out;

		/* Remove one and reinsert, as the only hole it should refill itself */
		for (n = 0; n < count; n++) {
			u64 addr = nodes[n].start;

			drm_mm_remove_node(&nodes[n]);
			if (!expect_insert(test, &mm, &nodes[n], size, 0, n, mode)) {
				KUNIT_FAIL(test, "%s reinsert failed, size %llu step %d\n",
					   mode->name, size, n);
				goto out;
			}

			if (nodes[n].start != addr) {
				KUNIT_FAIL(test,
					   "%s reinsert node moved, step %d, expected %llx, found %llx\n",
					   mode->name, n, addr, nodes[n].start);
				goto out;
			}

			if (!assert_continuous(test, &mm, size))
				goto out;
		}

		/* Remove several, reinsert, check full */
		for_each_prime_number(n, min(max_prime, count)) {
			for (m = 0; m < n; m++) {
				node = &nodes[order[(o + m) % count]];
				drm_mm_remove_node(node);
			}

			for (m = 0; m < n; m++) {
				node = &nodes[order[(o + m) % count]];
				if (!expect_insert(test, &mm, node, size, 0, n, mode)) {
					KUNIT_FAIL(test,
						   "%s multiple reinsert failed, size %llu step %d\n",
							   mode->name, size, n);
					goto out;
				}
			}

			o += n;

			if (!assert_continuous(test, &mm, size))
				goto out;

			if (!expect_insert_fail(test, &mm, size))
				goto out;
		}

		drm_mm_for_each_node_safe(node, next, &mm)
			drm_mm_remove_node(node);
		DRM_MM_BUG_ON(!drm_mm_clean(&mm));

		cond_resched();
	}

	ret = 0;
out:
	drm_mm_for_each_node_safe(node, next, &mm)
		drm_mm_remove_node(node);
	drm_mm_takedown(&mm);
	kfree(order);
err_nodes:
	vfree(nodes);
	return ret;
}

static void drm_test_mm_insert(struct kunit *test)
{
	const unsigned int count = min_t(unsigned int, BIT(10), max_iterations);
	unsigned int n;

	for_each_prime_number_from(n, 1, 54) {
		u64 size = BIT_ULL(n);

		KUNIT_ASSERT_FALSE(test, __drm_test_mm_insert(test, count, size - 1, false));
		KUNIT_ASSERT_FALSE(test, __drm_test_mm_insert(test, count, size, false));
		KUNIT_ASSERT_FALSE(test, __drm_test_mm_insert(test, count, size + 1, false));

		cond_resched();
	}
}

static void drm_test_mm_replace(struct kunit *test)
{
	const unsigned int count = min_t(unsigned int, BIT(10), max_iterations);
	unsigned int n;

	/* Reuse __drm_test_mm_insert to exercise replacement by inserting a dummy node,
	 * then replacing it with the intended node. We want to check that
	 * the tree is intact and all the information we need is carried
	 * across to the target node.
	 */

	for_each_prime_number_from(n, 1, 54) {
		u64 size = BIT_ULL(n);

		KUNIT_ASSERT_FALSE(test, __drm_test_mm_insert(test, count, size - 1, true));
		KUNIT_ASSERT_FALSE(test, __drm_test_mm_insert(test, count, size, true));
		KUNIT_ASSERT_FALSE(test, __drm_test_mm_insert(test, count, size + 1, true));

		cond_resched();
	}
}

static bool expect_insert_in_range(struct kunit *test, struct drm_mm *mm, struct drm_mm_node *node,
				   u64 size, u64 alignment, unsigned long color,
				   u64 range_start, u64 range_end, const struct insert_mode *mode)
{
	int err;

	err = drm_mm_insert_node_in_range(mm, node,
					  size, alignment, color,
					  range_start, range_end,
					  mode->mode);
	if (err) {
		KUNIT_FAIL(test,
			   "insert (size=%llu, alignment=%llu, color=%lu, mode=%s) nto range [%llx, %llx] failed with err=%d\n",
				   size, alignment, color, mode->name,
				   range_start, range_end, err);
		return false;
	}

	if (!assert_node(test, node, mm, size, alignment, color)) {
		drm_mm_remove_node(node);
		return false;
	}

	return true;
}

static bool expect_insert_in_range_fail(struct kunit *test, struct drm_mm *mm,
					u64 size, u64 range_start, u64 range_end)
{
	struct drm_mm_node tmp = {};
	int err;

	err = drm_mm_insert_node_in_range(mm, &tmp, size, 0, 0, range_start, range_end,
					  0);
	if (likely(err == -ENOSPC))
		return true;

	if (!err) {
		KUNIT_FAIL(test,
			   "impossible insert succeeded, node %llx + %llu, range [%llx, %llx]\n",
				   tmp.start, tmp.size, range_start, range_end);
		drm_mm_remove_node(&tmp);
	} else {
		KUNIT_FAIL(test,
			   "impossible insert failed with wrong error %d [expected %d], size %llu, range [%llx, %llx]\n",
				   err, -ENOSPC, size, range_start, range_end);
	}

	return false;
}

static bool assert_contiguous_in_range(struct kunit *test, struct drm_mm *mm,
				       u64 size, u64 start, u64 end)
{
	struct drm_mm_node *node;
	unsigned int n;

	if (!expect_insert_in_range_fail(test, mm, size, start, end))
		return false;

	n = div64_u64(start + size - 1, size);
	drm_mm_for_each_node(node, mm) {
		if (node->start < start || node->start + node->size > end) {
			KUNIT_FAIL(test,
				   "node %d out of range, address [%llx + %llu], range [%llx, %llx]\n",
					   n, node->start, node->start + node->size, start, end);
			return false;
		}

		if (node->start != n * size) {
			KUNIT_FAIL(test, "node %d out of order, expected start %llx, found %llx\n",
				   n, n * size, node->start);
			return false;
		}

		if (node->size != size) {
			KUNIT_FAIL(test, "node %d has wrong size, expected size %llx, found %llx\n",
				   n, size, node->size);
			return false;
		}

		if (drm_mm_hole_follows(node) && drm_mm_hole_node_end(node) < end) {
			KUNIT_FAIL(test, "node %d is followed by a hole!\n", n);
			return false;
		}

		n++;
	}

	if (start > 0) {
		node = __drm_mm_interval_first(mm, 0, start - 1);
		if (drm_mm_node_allocated(node)) {
			KUNIT_FAIL(test, "node before start: node=%llx+%llu, start=%llx\n",
				   node->start, node->size, start);
			return false;
		}
	}

	if (end < U64_MAX) {
		node = __drm_mm_interval_first(mm, end, U64_MAX);
		if (drm_mm_node_allocated(node)) {
			KUNIT_FAIL(test, "node after end: node=%llx+%llu, end=%llx\n",
				   node->start, node->size, end);
			return false;
		}
	}

	return true;
}

static int __drm_test_mm_insert_range(struct kunit *test, unsigned int count, u64 size,
				      u64 start, u64 end)
{
	const struct insert_mode *mode;
	struct drm_mm mm;
	struct drm_mm_node *nodes, *node, *next;
	unsigned int n, start_n, end_n;
	int ret;

	DRM_MM_BUG_ON(!count);
	DRM_MM_BUG_ON(!size);
	DRM_MM_BUG_ON(end <= start);

	/* Very similar to __drm_test_mm_insert(), but now instead of populating the
	 * full range of the drm_mm, we try to fill a small portion of it.
	 */

	ret = -ENOMEM;
	nodes = vzalloc(array_size(count, sizeof(*nodes)));
	KUNIT_ASSERT_TRUE(test, nodes);

	ret = -EINVAL;
	drm_mm_init(&mm, 0, count * size);

	start_n = div64_u64(start + size - 1, size);
	end_n = div64_u64(end - size, size);

	for (mode = insert_modes; mode->name; mode++) {
		for (n = start_n; n <= end_n; n++) {
			if (!expect_insert_in_range(test, &mm, &nodes[n], size, size, n,
						    start, end, mode)) {
				KUNIT_FAIL(test,
					   "%s insert failed, size %llu, step %d [%d, %d], range [%llx, %llx]\n",
						   mode->name, size, n, start_n, end_n, start, end);
				goto out;
			}
		}

		if (!assert_contiguous_in_range(test, &mm, size, start, end)) {
			KUNIT_FAIL(test,
				   "%s: range [%llx, %llx] not full after initialisation, size=%llu\n",
				   mode->name, start, end, size);
			goto out;
		}

		/* Remove one and reinsert, it should refill itself */
		for (n = start_n; n <= end_n; n++) {
			u64 addr = nodes[n].start;

			drm_mm_remove_node(&nodes[n]);
			if (!expect_insert_in_range(test, &mm, &nodes[n], size, size, n,
						    start, end, mode)) {
				KUNIT_FAIL(test, "%s reinsert failed, step %d\n", mode->name, n);
				goto out;
			}

			if (nodes[n].start != addr) {
				KUNIT_FAIL(test,
					   "%s reinsert node moved, step %d, expected %llx, found %llx\n",
					   mode->name, n, addr, nodes[n].start);
				goto out;
			}
		}

		if (!assert_contiguous_in_range(test, &mm, size, start, end)) {
			KUNIT_FAIL(test,
				   "%s: range [%llx, %llx] not full after reinsertion, size=%llu\n",
				   mode->name, start, end, size);
			goto out;
		}

		drm_mm_for_each_node_safe(node, next, &mm)
			drm_mm_remove_node(node);
		DRM_MM_BUG_ON(!drm_mm_clean(&mm));

		cond_resched();
	}

	ret = 0;
out:
	drm_mm_for_each_node_safe(node, next, &mm)
		drm_mm_remove_node(node);
	drm_mm_takedown(&mm);
	vfree(nodes);
	return ret;
}

static int insert_outside_range(struct kunit *test)
{
	struct drm_mm mm;
	const unsigned int start = 1024;
	const unsigned int end = 2048;
	const unsigned int size = end - start;

	drm_mm_init(&mm, start, size);

	if (!expect_insert_in_range_fail(test, &mm, 1, 0, start))
		return -EINVAL;

	if (!expect_insert_in_range_fail(test, &mm, size,
					 start - size / 2, start + (size + 1) / 2))
		return -EINVAL;

	if (!expect_insert_in_range_fail(test, &mm, size,
					 end - (size + 1) / 2, end + size / 2))
		return -EINVAL;

	if (!expect_insert_in_range_fail(test, &mm, 1, end, end + size))
		return -EINVAL;

	drm_mm_takedown(&mm);
	return 0;
}

static void drm_test_mm_insert_range(struct kunit *test)
{
	const unsigned int count = min_t(unsigned int, BIT(13), max_iterations);
	unsigned int n;

	/* Check that requests outside the bounds of drm_mm are rejected. */
	KUNIT_ASSERT_FALSE(test, insert_outside_range(test));

	for_each_prime_number_from(n, 1, 50) {
		const u64 size = BIT_ULL(n);
		const u64 max = count * size;

		KUNIT_ASSERT_FALSE(test, __drm_test_mm_insert_range(test, count, size, 0, max));
		KUNIT_ASSERT_FALSE(test, __drm_test_mm_insert_range(test, count, size, 1, max));
		KUNIT_ASSERT_FALSE(test, __drm_test_mm_insert_range(test, count, size, 0, max - 1));
		KUNIT_ASSERT_FALSE(test, __drm_test_mm_insert_range(test, count, size, 0, max / 2));
		KUNIT_ASSERT_FALSE(test, __drm_test_mm_insert_range(test, count, size,
								    max / 2, max / 2));
		KUNIT_ASSERT_FALSE(test, __drm_test_mm_insert_range(test, count, size,
								    max / 4 + 1, 3 * max / 4 - 1));

		cond_resched();
	}
}

static int prepare_frag(struct kunit *test, struct drm_mm *mm, struct drm_mm_node *nodes,
			unsigned int num_insert, const struct insert_mode *mode)
{
	unsigned int size = 4096;
	unsigned int i;

	for (i = 0; i < num_insert; i++) {
		if (!expect_insert(test, mm, &nodes[i], size, 0, i, mode) != 0) {
			KUNIT_FAIL(test, "%s insert failed\n", mode->name);
			return -EINVAL;
		}
	}

	/* introduce fragmentation by freeing every other node */
	for (i = 0; i < num_insert; i++) {
		if (i % 2 == 0)
			drm_mm_remove_node(&nodes[i]);
	}

	return 0;
}

static u64 get_insert_time(struct kunit *test, struct drm_mm *mm,
			   unsigned int num_insert, struct drm_mm_node *nodes,
			   const struct insert_mode *mode)
{
	unsigned int size = 8192;
	ktime_t start;
	unsigned int i;

	start = ktime_get();
	for (i = 0; i < num_insert; i++) {
		if (!expect_insert(test, mm, &nodes[i], size, 0, i, mode) != 0) {
			KUNIT_FAIL(test, "%s insert failed\n", mode->name);
			return 0;
		}
	}

	return ktime_to_ns(ktime_sub(ktime_get(), start));
}

static void drm_test_mm_frag(struct kunit *test)
{
	struct drm_mm mm;
	const struct insert_mode *mode;
	struct drm_mm_node *nodes, *node, *next;
	unsigned int insert_size = 10000;
	unsigned int scale_factor = 4;

	/* We need 4 * insert_size nodes to hold intermediate allocated
	 * drm_mm nodes.
	 * 1 times for prepare_frag()
	 * 1 times for get_insert_time()
	 * 2 times for get_insert_time()
	 */
	nodes = vzalloc(array_size(insert_size * 4, sizeof(*nodes)));
	KUNIT_ASSERT_TRUE(test, nodes);

	/* For BOTTOMUP and TOPDOWN, we first fragment the
	 * address space using prepare_frag() and then try to verify
	 * that insertions scale quadratically from 10k to 20k insertions
	 */
	drm_mm_init(&mm, 1, U64_MAX - 2);
	for (mode = insert_modes; mode->name; mode++) {
		u64 insert_time1, insert_time2;

		if (mode->mode != DRM_MM_INSERT_LOW &&
		    mode->mode != DRM_MM_INSERT_HIGH)
			continue;

		if (prepare_frag(test, &mm, nodes, insert_size, mode))
			goto err;

		insert_time1 = get_insert_time(test, &mm, insert_size,
					       nodes + insert_size, mode);
		if (insert_time1 == 0)
			goto err;

		insert_time2 = get_insert_time(test, &mm, (insert_size * 2),
					       nodes + insert_size * 2, mode);
		if (insert_time2 == 0)
			goto err;

		kunit_info(test, "%s fragmented insert of %u and %u insertions took %llu and %llu nsecs\n",
			   mode->name, insert_size, insert_size * 2, insert_time1, insert_time2);

		if (insert_time2 > (scale_factor * insert_time1)) {
			KUNIT_FAIL(test, "%s fragmented insert took %llu nsecs more\n",
				   mode->name, insert_time2 - (scale_factor * insert_time1));
			goto err;
		}

		drm_mm_for_each_node_safe(node, next, &mm)
			drm_mm_remove_node(node);
	}

err:
	drm_mm_for_each_node_safe(node, next, &mm)
		drm_mm_remove_node(node);
	drm_mm_takedown(&mm);
	vfree(nodes);
}

static void drm_test_mm_align(struct kunit *test)
{
	const struct insert_mode *mode;
	const unsigned int max_count = min(8192u, max_prime);
	struct drm_mm mm;
	struct drm_mm_node *nodes, *node, *next;
	unsigned int prime;

	/* For each of the possible insertion modes, we pick a few
	 * arbitrary alignments and check that the inserted node
	 * meets our requirements.
	 */

	nodes = vzalloc(array_size(max_count, sizeof(*nodes)));
	KUNIT_ASSERT_TRUE(test, nodes);

	drm_mm_init(&mm, 1, U64_MAX - 2);

	for (mode = insert_modes; mode->name; mode++) {
		unsigned int i = 0;

		for_each_prime_number_from(prime, 1, max_count) {
			u64 size = next_prime_number(prime);

			if (!expect_insert(test, &mm, &nodes[i], size, prime, i, mode)) {
				KUNIT_FAIL(test, "%s insert failed with alignment=%d",
					   mode->name, prime);
				goto out;
			}

			i++;
		}

		drm_mm_for_each_node_safe(node, next, &mm)
			drm_mm_remove_node(node);
		DRM_MM_BUG_ON(!drm_mm_clean(&mm));

		cond_resched();
	}

out:
	drm_mm_for_each_node_safe(node, next, &mm)
		drm_mm_remove_node(node);
	drm_mm_takedown(&mm);
	vfree(nodes);
}

static void drm_test_mm_align_pot(struct kunit *test, int max)
{
	struct drm_mm mm;
	struct drm_mm_node *node, *next;
	int bit;

	/* Check that we can align to the full u64 address space */

	drm_mm_init(&mm, 1, U64_MAX - 2);

	for (bit = max - 1; bit; bit--) {
		u64 align, size;

		node = kzalloc(sizeof(*node), GFP_KERNEL);
		if (!node) {
			KUNIT_FAIL(test, "failed to allocate node");
			goto out;
		}

		align = BIT_ULL(bit);
		size = BIT_ULL(bit - 1) + 1;
		if (!expect_insert(test, &mm, node, size, align, bit, &insert_modes[0])) {
			KUNIT_FAIL(test, "insert failed with alignment=%llx [%d]", align, bit);
			goto out;
		}

		cond_resched();
	}

out:
	drm_mm_for_each_node_safe(node, next, &mm) {
		drm_mm_remove_node(node);
		kfree(node);
	}
	drm_mm_takedown(&mm);
}

static void drm_test_mm_align32(struct kunit *test)
{
	drm_test_mm_align_pot(test, 32);
}

static void drm_test_mm_align64(struct kunit *test)
{
	drm_test_mm_align_pot(test, 64);
}

static void show_scan(struct kunit *test, const struct drm_mm_scan *scan)
{
	kunit_info(test, "scan: hit [%llx, %llx], size=%lld, align=%lld, color=%ld\n",
		   scan->hit_start, scan->hit_end, scan->size, scan->alignment, scan->color);
}

static void show_holes(struct kunit *test, const struct drm_mm *mm, int count)
{
	u64 hole_start, hole_end;
	struct drm_mm_node *hole;

	drm_mm_for_each_hole(hole, mm, hole_start, hole_end) {
		struct drm_mm_node *next = list_next_entry(hole, node_list);
		const char *node1 = NULL, *node2 = NULL;

		if (drm_mm_node_allocated(hole))
			node1 = kasprintf(GFP_KERNEL, "[%llx + %lld, color=%ld], ",
					  hole->start, hole->size, hole->color);

		if (drm_mm_node_allocated(next))
			node2 = kasprintf(GFP_KERNEL, ", [%llx + %lld, color=%ld]",
					  next->start, next->size, next->color);

		kunit_info(test, "%sHole [%llx - %llx, size %lld]%s\n", node1,
			   hole_start, hole_end, hole_end - hole_start, node2);

		kfree(node2);
		kfree(node1);

		if (!--count)
			break;
	}
}

struct evict_node {
	struct drm_mm_node node;
	struct list_head link;
};

static bool evict_nodes(struct kunit *test, struct drm_mm_scan *scan,
			struct evict_node *nodes, unsigned int *order, unsigned int count,
			bool use_color, struct list_head *evict_list)
{
	struct evict_node *e, *en;
	unsigned int i;

	for (i = 0; i < count; i++) {
		e = &nodes[order ? order[i] : i];
		list_add(&e->link, evict_list);
		if (drm_mm_scan_add_block(scan, &e->node))
			break;
	}
	list_for_each_entry_safe(e, en, evict_list, link) {
		if (!drm_mm_scan_remove_block(scan, &e->node))
			list_del(&e->link);
	}
	if (list_empty(evict_list)) {
		KUNIT_FAIL(test,
			   "Failed to find eviction: size=%lld [avail=%d], align=%lld (color=%lu)\n",
			   scan->size, count, scan->alignment, scan->color);
		return false;
	}

	list_for_each_entry(e, evict_list, link)
		drm_mm_remove_node(&e->node);

	if (use_color) {
		struct drm_mm_node *node;

		while ((node = drm_mm_scan_color_evict(scan))) {
			e = container_of(node, typeof(*e), node);
			drm_mm_remove_node(&e->node);
			list_add(&e->link, evict_list);
		}
	} else {
		if (drm_mm_scan_color_evict(scan)) {
			KUNIT_FAIL(test,
				   "drm_mm_scan_color_evict unexpectedly reported overlapping nodes!\n");
			return false;
		}
	}

	return true;
}

static bool evict_nothing(struct kunit *test, struct drm_mm *mm,
			  unsigned int total_size, struct evict_node *nodes)
{
	struct drm_mm_scan scan;
	LIST_HEAD(evict_list);
	struct evict_node *e;
	struct drm_mm_node *node;
	unsigned int n;

	drm_mm_scan_init(&scan, mm, 1, 0, 0, 0);
	for (n = 0; n < total_size; n++) {
		e = &nodes[n];
		list_add(&e->link, &evict_list);
		drm_mm_scan_add_block(&scan, &e->node);
	}
	list_for_each_entry(e, &evict_list, link)
		drm_mm_scan_remove_block(&scan, &e->node);

	for (n = 0; n < total_size; n++) {
		e = &nodes[n];

		if (!drm_mm_node_allocated(&e->node)) {
			KUNIT_FAIL(test, "node[%d] no longer allocated!\n", n);
			return false;
		}

		e->link.next = NULL;
	}

	drm_mm_for_each_node(node, mm) {
		e = container_of(node, typeof(*e), node);
		e->link.next = &e->link;
	}

	for (n = 0; n < total_size; n++) {
		e = &nodes[n];

		if (!e->link.next) {
			KUNIT_FAIL(test, "node[%d] no longer connected!\n", n);
			return false;
		}
	}

	return assert_continuous(test, mm, nodes[0].node.size);
}

static bool evict_everything(struct kunit *test, struct drm_mm *mm,
			     unsigned int total_size, struct evict_node *nodes)
{
	struct drm_mm_scan scan;
	LIST_HEAD(evict_list);
	struct evict_node *e;
	unsigned int n;
	int err;

	drm_mm_scan_init(&scan, mm, total_size, 0, 0, 0);
	for (n = 0; n < total_size; n++) {
		e = &nodes[n];
		list_add(&e->link, &evict_list);
		if (drm_mm_scan_add_block(&scan, &e->node))
			break;
	}

	err = 0;
	list_for_each_entry(e, &evict_list, link) {
		if (!drm_mm_scan_remove_block(&scan, &e->node)) {
			if (!err) {
				KUNIT_FAIL(test, "Node %lld not marked for eviction!\n",
					   e->node.start);
				err = -EINVAL;
			}
		}
	}
	if (err)
		return false;

	list_for_each_entry(e, &evict_list, link)
		drm_mm_remove_node(&e->node);

	if (!assert_one_hole(test, mm, 0, total_size))
		return false;

	list_for_each_entry(e, &evict_list, link) {
		err = drm_mm_reserve_node(mm, &e->node);
		if (err) {
			KUNIT_FAIL(test, "Failed to reinsert node after eviction: start=%llx\n",
				   e->node.start);
			return false;
		}
	}

	return assert_continuous(test, mm, nodes[0].node.size);
}

static int evict_something(struct kunit *test, struct drm_mm *mm,
			   u64 range_start, u64 range_end, struct evict_node *nodes,
			   unsigned int *order, unsigned int count, unsigned int size,
			   unsigned int alignment, const struct insert_mode *mode)
{
	struct drm_mm_scan scan;
	LIST_HEAD(evict_list);
	struct evict_node *e;
	struct drm_mm_node tmp;
	int err;

	drm_mm_scan_init_with_range(&scan, mm, size, alignment, 0, range_start,
				    range_end, mode->mode);
	if (!evict_nodes(test, &scan, nodes, order, count, false, &evict_list))
		return -EINVAL;

	memset(&tmp, 0, sizeof(tmp));
	err = drm_mm_insert_node_generic(mm, &tmp, size, alignment, 0,
					 DRM_MM_INSERT_EVICT);
	if (err) {
		KUNIT_FAIL(test, "Failed to insert into eviction hole: size=%d, align=%d\n",
			   size, alignment);
		show_scan(test, &scan);
		show_holes(test, mm, 3);
		return err;
	}

	if (tmp.start < range_start || tmp.start + tmp.size > range_end) {
		KUNIT_FAIL(test,
			   "Inserted [address=%llu + %llu] did not fit into the request range [%llu, %llu]\n",
			   tmp.start, tmp.size, range_start, range_end);
		err = -EINVAL;
	}

	if (!assert_node(test, &tmp, mm, size, alignment, 0) ||
	    drm_mm_hole_follows(&tmp)) {
		KUNIT_FAIL(test,
			   "Inserted did not fill the eviction hole: size=%lld [%d], align=%d [rem=%lld], start=%llx, hole-follows?=%d\n",
			   tmp.size, size, alignment, misalignment(&tmp, alignment),
			   tmp.start, drm_mm_hole_follows(&tmp));
		err = -EINVAL;
	}

	drm_mm_remove_node(&tmp);
	if (err)
		return err;

	list_for_each_entry(e, &evict_list, link) {
		err = drm_mm_reserve_node(mm, &e->node);
		if (err) {
			KUNIT_FAIL(test, "Failed to reinsert node after eviction: start=%llx\n",
				   e->node.start);
			return err;
		}
	}

	if (!assert_continuous(test, mm, nodes[0].node.size)) {
		KUNIT_FAIL(test, "range is no longer continuous\n");
		return -EINVAL;
	}

	return 0;
}

static void drm_test_mm_evict(struct kunit *test)
{
	DRM_RND_STATE(prng, random_seed);
	const unsigned int size = 8192;
	const struct insert_mode *mode;
	struct drm_mm mm;
	struct evict_node *nodes;
	struct drm_mm_node *node, *next;
	unsigned int *order, n;

	/* Here we populate a full drm_mm and then try and insert a new node
	 * by evicting other nodes in a random order. The drm_mm_scan should
	 * pick the first matching hole it finds from the random list. We
	 * repeat that for different allocation strategies, alignments and
	 * sizes to try and stress the hole finder.
	 */

	nodes = vzalloc(array_size(size, sizeof(*nodes)));
	KUNIT_ASSERT_TRUE(test, nodes);

	order = drm_random_order(size, &prng);
	if (!order)
		goto err_nodes;

	drm_mm_init(&mm, 0, size);
	for (n = 0; n < size; n++) {
		if (drm_mm_insert_node(&mm, &nodes[n].node, 1)) {
			KUNIT_FAIL(test, "insert failed, step %d\n", n);
			goto out;
		}
	}

	/* First check that using the scanner doesn't break the mm */
	if (!evict_nothing(test, &mm, size, nodes)) {
		KUNIT_FAIL(test, "evict_nothing() failed\n");
		goto out;
	}
	if (!evict_everything(test, &mm, size, nodes)) {
		KUNIT_FAIL(test, "evict_everything() failed\n");
		goto out;
	}

	for (mode = evict_modes; mode->name; mode++) {
		for (n = 1; n <= size; n <<= 1) {
			drm_random_reorder(order, size, &prng);
			if (evict_something(test, &mm, 0, U64_MAX, nodes, order, size, n, 1,
					    mode)) {
				KUNIT_FAIL(test, "%s evict_something(size=%u) failed\n",
					   mode->name, n);
				goto out;
			}
		}

		for (n = 1; n < size; n <<= 1) {
			drm_random_reorder(order, size, &prng);
			if (evict_something(test, &mm, 0, U64_MAX, nodes, order, size,
					    size / 2, n, mode)) {
				KUNIT_FAIL(test,
					   "%s evict_something(size=%u, alignment=%u) failed\n",
					   mode->name, size / 2, n);
				goto out;
			}
		}

		for_each_prime_number_from(n, 1, min(size, max_prime)) {
			unsigned int nsize = (size - n + 1) / 2;

			DRM_MM_BUG_ON(!nsize);

			drm_random_reorder(order, size, &prng);
			if (evict_something(test, &mm, 0, U64_MAX, nodes, order, size,
					    nsize, n, mode)) {
				KUNIT_FAIL(test,
					   "%s evict_something(size=%u, alignment=%u) failed\n",
					   mode->name, nsize, n);
				goto out;
			}
		}

		cond_resched();
	}

out:
	drm_mm_for_each_node_safe(node, next, &mm)
		drm_mm_remove_node(node);
	drm_mm_takedown(&mm);
	kfree(order);
err_nodes:
	vfree(nodes);
}

static void drm_test_mm_evict_range(struct kunit *test)
{
	DRM_RND_STATE(prng, random_seed);
	const unsigned int size = 8192;
	const unsigned int range_size = size / 2;
	const unsigned int range_start = size / 4;
	const unsigned int range_end = range_start + range_size;
	const struct insert_mode *mode;
	struct drm_mm mm;
	struct evict_node *nodes;
	struct drm_mm_node *node, *next;
	unsigned int *order, n;

	/* Like drm_test_mm_evict() but now we are limiting the search to a
	 * small portion of the full drm_mm.
	 */

	nodes = vzalloc(array_size(size, sizeof(*nodes)));
	KUNIT_ASSERT_TRUE(test, nodes);

	order = drm_random_order(size, &prng);
	if (!order)
		goto err_nodes;

	drm_mm_init(&mm, 0, size);
	for (n = 0; n < size; n++) {
		if (drm_mm_insert_node(&mm, &nodes[n].node, 1)) {
			KUNIT_FAIL(test, "insert failed, step %d\n", n);
			goto out;
		}
	}

	for (mode = evict_modes; mode->name; mode++) {
		for (n = 1; n <= range_size; n <<= 1) {
			drm_random_reorder(order, size, &prng);
			if (evict_something(test, &mm, range_start, range_end, nodes,
					    order, size, n, 1, mode)) {
				KUNIT_FAIL(test,
					   "%s evict_something(size=%u) failed with range [%u, %u]\n",
					   mode->name, n, range_start, range_end);
				goto out;
			}
		}

		for (n = 1; n <= range_size; n <<= 1) {
			drm_random_reorder(order, size, &prng);
			if (evict_something(test, &mm, range_start, range_end, nodes,
					    order, size, range_size / 2, n, mode)) {
				KUNIT_FAIL(test,
					   "%s evict_something(size=%u, alignment=%u) failed with range [%u, %u]\n",
					   mode->name, range_size / 2, n, range_start, range_end);
				goto out;
			}
		}

		for_each_prime_number_from(n, 1, min(range_size, max_prime)) {
			unsigned int nsize = (range_size - n + 1) / 2;

			DRM_MM_BUG_ON(!nsize);

			drm_random_reorder(order, size, &prng);
			if (evict_something(test, &mm, range_start, range_end, nodes,
					    order, size, nsize, n, mode)) {
				KUNIT_FAIL(test,
					   "%s evict_something(size=%u, alignment=%u) failed with range [%u, %u]\n",
					   mode->name, nsize, n, range_start, range_end);
				goto out;
			}
		}

		cond_resched();
	}

out:
	drm_mm_for_each_node_safe(node, next, &mm)
		drm_mm_remove_node(node);
	drm_mm_takedown(&mm);
	kfree(order);
err_nodes:
	vfree(nodes);
}

static unsigned int node_index(const struct drm_mm_node *node)
{
	return div64_u64(node->start, node->size);
}

static void drm_test_mm_topdown(struct kunit *test)
{
	const struct insert_mode *topdown = &insert_modes[TOPDOWN];

	DRM_RND_STATE(prng, random_seed);
	const unsigned int count = 8192;
	unsigned int size;
	unsigned long *bitmap;
	struct drm_mm mm;
	struct drm_mm_node *nodes, *node, *next;
	unsigned int *order, n, m, o = 0;

	/* When allocating top-down, we expect to be returned a node
	 * from a suitable hole at the top of the drm_mm. We check that
	 * the returned node does match the highest available slot.
	 */

	nodes = vzalloc(array_size(count, sizeof(*nodes)));
	KUNIT_ASSERT_TRUE(test, nodes);

	bitmap = bitmap_zalloc(count, GFP_KERNEL);
	if (!bitmap)
		goto err_nodes;

	order = drm_random_order(count, &prng);
	if (!order)
		goto err_bitmap;

	for (size = 1; size <= 64; size <<= 1) {
		drm_mm_init(&mm, 0, size * count);
		for (n = 0; n < count; n++) {
			if (!expect_insert(test, &mm, &nodes[n], size, 0, n, topdown)) {
				KUNIT_FAIL(test, "insert failed, size %u step %d\n", size, n);
				goto out;
			}

			if (drm_mm_hole_follows(&nodes[n])) {
				KUNIT_FAIL(test,
					   "hole after topdown insert %d, start=%llx\n, size=%u",
					   n, nodes[n].start, size);
				goto out;
			}

			if (!assert_one_hole(test, &mm, 0, size * (count - n - 1)))
				goto out;
		}

		if (!assert_continuous(test, &mm, size))
			goto out;

		drm_random_reorder(order, count, &prng);
		for_each_prime_number_from(n, 1, min(count, max_prime)) {
			for (m = 0; m < n; m++) {
				node = &nodes[order[(o + m) % count]];
				drm_mm_remove_node(node);
				__set_bit(node_index(node), bitmap);
			}

			for (m = 0; m < n; m++) {
				unsigned int last;

				node = &nodes[order[(o + m) % count]];
				if (!expect_insert(test, &mm, node, size, 0, 0, topdown)) {
					KUNIT_FAIL(test, "insert failed, step %d/%d\n", m, n);
					goto out;
				}

				if (drm_mm_hole_follows(node)) {
					KUNIT_FAIL(test,
						   "hole after topdown insert %d/%d, start=%llx\n",
						   m, n, node->start);
					goto out;
				}

				last = find_last_bit(bitmap, count);
				if (node_index(node) != last) {
					KUNIT_FAIL(test,
						   "node %d/%d, size %d, not inserted into upmost hole, expected %d, found %d\n",
						   m, n, size, last, node_index(node));
					goto out;
				}

				__clear_bit(last, bitmap);
			}

			DRM_MM_BUG_ON(find_first_bit(bitmap, count) != count);

			o += n;
		}

		drm_mm_for_each_node_safe(node, next, &mm)
			drm_mm_remove_node(node);
		DRM_MM_BUG_ON(!drm_mm_clean(&mm));
		cond_resched();
	}

out:
	drm_mm_for_each_node_safe(node, next, &mm)
		drm_mm_remove_node(node);
	drm_mm_takedown(&mm);
	kfree(order);
err_bitmap:
	bitmap_free(bitmap);
err_nodes:
	vfree(nodes);
}

static void drm_test_mm_bottomup(struct kunit *test)
{
	const struct insert_mode *bottomup = &insert_modes[BOTTOMUP];

	DRM_RND_STATE(prng, random_seed);
	const unsigned int count = 8192;
	unsigned int size;
	unsigned long *bitmap;
	struct drm_mm mm;
	struct drm_mm_node *nodes, *node, *next;
	unsigned int *order, n, m, o = 0;

	/* Like drm_test_mm_topdown, but instead of searching for the last hole,
	 * we search for the first.
	 */

	nodes = vzalloc(array_size(count, sizeof(*nodes)));
	KUNIT_ASSERT_TRUE(test, nodes);

	bitmap = bitmap_zalloc(count, GFP_KERNEL);
	if (!bitmap)
		goto err_nodes;

	order = drm_random_order(count, &prng);
	if (!order)
		goto err_bitmap;

	for (size = 1; size <= 64; size <<= 1) {
		drm_mm_init(&mm, 0, size * count);
		for (n = 0; n < count; n++) {
			if (!expect_insert(test, &mm, &nodes[n], size, 0, n, bottomup)) {
				KUNIT_FAIL(test,
					   "bottomup insert failed, size %u step %d\n", size, n);
				goto out;
			}

			if (!assert_one_hole(test, &mm, size * (n + 1), size * count))
				goto out;
		}

		if (!assert_continuous(test, &mm, size))
			goto out;

		drm_random_reorder(order, count, &prng);
		for_each_prime_number_from(n, 1, min(count, max_prime)) {
			for (m = 0; m < n; m++) {
				node = &nodes[order[(o + m) % count]];
				drm_mm_remove_node(node);
				__set_bit(node_index(node), bitmap);
			}

			for (m = 0; m < n; m++) {
				unsigned int first;

				node = &nodes[order[(o + m) % count]];
				if (!expect_insert(test, &mm, node, size, 0, 0, bottomup)) {
					KUNIT_FAIL(test, "insert failed, step %d/%d\n", m, n);
					goto out;
				}

				first = find_first_bit(bitmap, count);
				if (node_index(node) != first) {
					KUNIT_FAIL(test,
						   "node %d/%d not inserted into bottom hole, expected %d, found %d\n",
						   m, n, first, node_index(node));
					goto out;
				}
				__clear_bit(first, bitmap);
			}

			DRM_MM_BUG_ON(find_first_bit(bitmap, count) != count);

			o += n;
		}

		drm_mm_for_each_node_safe(node, next, &mm)
			drm_mm_remove_node(node);
		DRM_MM_BUG_ON(!drm_mm_clean(&mm));
		cond_resched();
	}

out:
	drm_mm_for_each_node_safe(node, next, &mm)
		drm_mm_remove_node(node);
	drm_mm_takedown(&mm);
	kfree(order);
err_bitmap:
	bitmap_free(bitmap);
err_nodes:
	vfree(nodes);
}

static void drm_test_mm_once(struct kunit *test, unsigned int mode)
{
	struct drm_mm mm;
	struct drm_mm_node rsvd_lo, rsvd_hi, node;

	drm_mm_init(&mm, 0, 7);

	memset(&rsvd_lo, 0, sizeof(rsvd_lo));
	rsvd_lo.start = 1;
	rsvd_lo.size = 1;
	if (drm_mm_reserve_node(&mm, &rsvd_lo)) {
		KUNIT_FAIL(test, "Could not reserve low node\n");
		goto err;
	}

	memset(&rsvd_hi, 0, sizeof(rsvd_hi));
	rsvd_hi.start = 5;
	rsvd_hi.size = 1;
	if (drm_mm_reserve_node(&mm, &rsvd_hi)) {
		KUNIT_FAIL(test, "Could not reserve low node\n");
		goto err_lo;
	}

	if (!drm_mm_hole_follows(&rsvd_lo) || !drm_mm_hole_follows(&rsvd_hi)) {
		KUNIT_FAIL(test, "Expected a hole after lo and high nodes!\n");
		goto err_hi;
	}

	memset(&node, 0, sizeof(node));
	if (drm_mm_insert_node_generic(&mm, &node, 2, 0, 0, mode)) {
		KUNIT_FAIL(test, "Could not insert the node into the available hole!\n");
		goto err_hi;
	}

	drm_mm_remove_node(&node);
err_hi:
	drm_mm_remove_node(&rsvd_hi);
err_lo:
	drm_mm_remove_node(&rsvd_lo);
err:
	drm_mm_takedown(&mm);
}

static void drm_test_mm_lowest(struct kunit *test)
{
	drm_test_mm_once(test, DRM_MM_INSERT_LOW);
}

static void drm_test_mm_highest(struct kunit *test)
{
	drm_test_mm_once(test, DRM_MM_INSERT_HIGH);
}

static void separate_adjacent_colors(const struct drm_mm_node *node,
				     unsigned long color, u64 *start, u64 *end)
{
	if (drm_mm_node_allocated(node) && node->color != color)
		++*start;

	node = list_next_entry(node, node_list);
	if (drm_mm_node_allocated(node) && node->color != color)
		--*end;
}

static bool colors_abutt(struct kunit *test, const struct drm_mm_node *node)
{
	if (!drm_mm_hole_follows(node) &&
	    drm_mm_node_allocated(list_next_entry(node, node_list))) {
		KUNIT_FAIL(test, "colors abutt; %ld [%llx + %llx] is next to %ld [%llx + %llx]!\n",
			   node->color, node->start, node->size,
		       list_next_entry(node, node_list)->color,
		       list_next_entry(node, node_list)->start,
		       list_next_entry(node, node_list)->size);
		return true;
	}

	return false;
}

static void drm_test_mm_color(struct kunit *test)
{
	const unsigned int count = min(4096u, max_iterations);
	const struct insert_mode *mode;
	struct drm_mm mm;
	struct drm_mm_node *node, *nn;
	unsigned int n;

	/* Color adjustment complicates everything. First we just check
	 * that when we insert a node we apply any color_adjustment callback.
	 * The callback we use should ensure that there is a gap between
	 * any two nodes, and so after each insertion we check that those
	 * holes are inserted and that they are preserved.
	 */

	drm_mm_init(&mm, 0, U64_MAX);

	for (n = 1; n <= count; n++) {
		node = kzalloc(sizeof(*node), GFP_KERNEL);
		if (!node)
			goto out;

		if (!expect_insert(test, &mm, node, n, 0, n, &insert_modes[0])) {
			KUNIT_FAIL(test, "insert failed, step %d\n", n);
			kfree(node);
			goto out;
		}
	}

	drm_mm_for_each_node_safe(node, nn, &mm) {
		if (node->color != node->size) {
			KUNIT_FAIL(test, "invalid color stored: expected %lld, found %ld\n",
				   node->size, node->color);

			goto out;
		}

		drm_mm_remove_node(node);
		kfree(node);
	}

	/* Now, let's start experimenting with applying a color callback */
	mm.color_adjust = separate_adjacent_colors;
	for (mode = insert_modes; mode->name; mode++) {
		u64 last;

		node = kzalloc(sizeof(*node), GFP_KERNEL);
		if (!node)
			goto out;

		node->size = 1 + 2 * count;
		node->color = node->size;

		if (drm_mm_reserve_node(&mm, node)) {
			KUNIT_FAIL(test, "initial reserve failed!\n");
			goto out;
		}

		last = node->start + node->size;

		for (n = 1; n <= count; n++) {
			int rem;

			node = kzalloc(sizeof(*node), GFP_KERNEL);
			if (!node)
				goto out;

			node->start = last;
			node->size = n + count;
			node->color = node->size;

			if (drm_mm_reserve_node(&mm, node) != -ENOSPC) {
				KUNIT_FAIL(test, "reserve %d did not report color overlap!", n);
				goto out;
			}

			node->start += n + 1;
			rem = misalignment(node, n + count);
			node->start += n + count - rem;

			if (drm_mm_reserve_node(&mm, node)) {
				KUNIT_FAIL(test, "reserve %d failed", n);
				goto out;
			}

			last = node->start + node->size;
		}

		for (n = 1; n <= count; n++) {
			node = kzalloc(sizeof(*node), GFP_KERNEL);
			if (!node)
				goto out;

			if (!expect_insert(test, &mm, node, n, n, n, mode)) {
				KUNIT_FAIL(test, "%s insert failed, step %d\n", mode->name, n);
				kfree(node);
				goto out;
			}
		}

		drm_mm_for_each_node_safe(node, nn, &mm) {
			u64 rem;

			if (node->color != node->size) {
				KUNIT_FAIL(test,
					   "%s invalid color stored: expected %lld, found %ld\n",
					   mode->name, node->size, node->color);

				goto out;
			}

			if (colors_abutt(test, node))
				goto out;

			div64_u64_rem(node->start, node->size, &rem);
			if (rem) {
				KUNIT_FAIL(test,
					   "%s colored node misaligned, start=%llx expected alignment=%lld [rem=%lld]\n",
					   mode->name, node->start, node->size, rem);
				goto out;
			}

			drm_mm_remove_node(node);
			kfree(node);
		}

		cond_resched();
	}

out:
	drm_mm_for_each_node_safe(node, nn, &mm) {
		drm_mm_remove_node(node);
		kfree(node);
	}
	drm_mm_takedown(&mm);
}

static int evict_color(struct kunit *test, struct drm_mm *mm, u64 range_start,
		       u64 range_end, struct evict_node *nodes, unsigned int *order,
		unsigned int count, unsigned int size, unsigned int alignment,
		unsigned long color, const struct insert_mode *mode)
{
	struct drm_mm_scan scan;
	LIST_HEAD(evict_list);
	struct evict_node *e;
	struct drm_mm_node tmp;
	int err;

	drm_mm_scan_init_with_range(&scan, mm, size, alignment, color, range_start,
				    range_end, mode->mode);
	if (!evict_nodes(test, &scan, nodes, order, count, true, &evict_list))
		return -EINVAL;

	memset(&tmp, 0, sizeof(tmp));
	err = drm_mm_insert_node_generic(mm, &tmp, size, alignment, color,
					 DRM_MM_INSERT_EVICT);
	if (err) {
		KUNIT_FAIL(test,
			   "Failed to insert into eviction hole: size=%d, align=%d, color=%lu, err=%d\n",
			   size, alignment, color, err);
		show_scan(test, &scan);
		show_holes(test, mm, 3);
		return err;
	}

	if (tmp.start < range_start || tmp.start + tmp.size > range_end) {
		KUNIT_FAIL(test,
			   "Inserted [address=%llu + %llu] did not fit into the request range [%llu, %llu]\n",
			   tmp.start, tmp.size, range_start, range_end);
		err = -EINVAL;
	}

	if (colors_abutt(test, &tmp))
		err = -EINVAL;

	if (!assert_node(test, &tmp, mm, size, alignment, color)) {
		KUNIT_FAIL(test,
			   "Inserted did not fit the eviction hole: size=%lld [%d], align=%d [rem=%lld], start=%llx\n",
			   tmp.size, size, alignment, misalignment(&tmp, alignment), tmp.start);
		err = -EINVAL;
	}

	drm_mm_remove_node(&tmp);
	if (err)
		return err;

	list_for_each_entry(e, &evict_list, link) {
		err = drm_mm_reserve_node(mm, &e->node);
		if (err) {
			KUNIT_FAIL(test, "Failed to reinsert node after eviction: start=%llx\n",
				   e->node.start);
			return err;
		}
	}

	cond_resched();
	return 0;
}

static void drm_test_mm_color_evict(struct kunit *test)
{
	DRM_RND_STATE(prng, random_seed);
	const unsigned int total_size = min(8192u, max_iterations);
	const struct insert_mode *mode;
	unsigned long color = 0;
	struct drm_mm mm;
	struct evict_node *nodes;
	struct drm_mm_node *node, *next;
	unsigned int *order, n;

	/* Check that the drm_mm_scan also honours color adjustment when
	 * choosing its victims to create a hole. Our color_adjust does not
	 * allow two nodes to be placed together without an intervening hole
	 * enlarging the set of victims that must be evicted.
	 */

	nodes = vzalloc(array_size(total_size, sizeof(*nodes)));
	KUNIT_ASSERT_TRUE(test, nodes);

	order = drm_random_order(total_size, &prng);
	if (!order)
		goto err_nodes;

	drm_mm_init(&mm, 0, 2 * total_size - 1);
	mm.color_adjust = separate_adjacent_colors;
	for (n = 0; n < total_size; n++) {
		if (!expect_insert(test, &mm, &nodes[n].node,
				   1, 0, color++,
				   &insert_modes[0])) {
			KUNIT_FAIL(test, "insert failed, step %d\n", n);
			goto out;
		}
	}

	for (mode = evict_modes; mode->name; mode++) {
		for (n = 1; n <= total_size; n <<= 1) {
			drm_random_reorder(order, total_size, &prng);
			if (evict_color(test, &mm, 0, U64_MAX, nodes, order, total_size,
					n, 1, color++, mode)) {
				KUNIT_FAIL(test, "%s evict_color(size=%u) failed\n", mode->name, n);
				goto out;
			}
		}

		for (n = 1; n < total_size; n <<= 1) {
			drm_random_reorder(order, total_size, &prng);
			if (evict_color(test, &mm, 0, U64_MAX, nodes, order, total_size,
					total_size / 2, n, color++, mode)) {
				KUNIT_FAIL(test, "%s evict_color(size=%u, alignment=%u) failed\n",
					   mode->name, total_size / 2, n);
				goto out;
			}
		}

		for_each_prime_number_from(n, 1, min(total_size, max_prime)) {
			unsigned int nsize = (total_size - n + 1) / 2;

			DRM_MM_BUG_ON(!nsize);

			drm_random_reorder(order, total_size, &prng);
			if (evict_color(test, &mm, 0, U64_MAX, nodes, order, total_size,
					nsize, n, color++, mode)) {
				KUNIT_FAIL(test, "%s evict_color(size=%u, alignment=%u) failed\n",
					   mode->name, nsize, n);
				goto out;
			}
		}

		cond_resched();
	}

out:
	drm_mm_for_each_node_safe(node, next, &mm)
		drm_mm_remove_node(node);
	drm_mm_takedown(&mm);
	kfree(order);
err_nodes:
	vfree(nodes);
}

static void drm_test_mm_color_evict_range(struct kunit *test)
{
	DRM_RND_STATE(prng, random_seed);
	const unsigned int total_size = 8192;
	const unsigned int range_size = total_size / 2;
	const unsigned int range_start = total_size / 4;
	const unsigned int range_end = range_start + range_size;
	const struct insert_mode *mode;
	unsigned long color = 0;
	struct drm_mm mm;
	struct evict_node *nodes;
	struct drm_mm_node *node, *next;
	unsigned int *order, n;

	/* Like drm_test_mm_color_evict(), but limited to small portion of the full
	 * drm_mm range.
	 */

	nodes = vzalloc(array_size(total_size, sizeof(*nodes)));
	KUNIT_ASSERT_TRUE(test, nodes);

	order = drm_random_order(total_size, &prng);
	if (!order)
		goto err_nodes;

	drm_mm_init(&mm, 0, 2 * total_size - 1);
	mm.color_adjust = separate_adjacent_colors;
	for (n = 0; n < total_size; n++) {
		if (!expect_insert(test, &mm, &nodes[n].node,
				   1, 0, color++,
				   &insert_modes[0])) {
			KUNIT_FAIL(test, "insert failed, step %d\n", n);
			goto out;
		}
	}

	for (mode = evict_modes; mode->name; mode++) {
		for (n = 1; n <= range_size; n <<= 1) {
			drm_random_reorder(order, range_size, &prng);
			if (evict_color(test, &mm, range_start, range_end, nodes, order,
					total_size, n, 1, color++, mode)) {
				KUNIT_FAIL(test,
					   "%s evict_color(size=%u) failed for range [%x, %x]\n",
						mode->name, n, range_start, range_end);
				goto out;
			}
		}

		for (n = 1; n < range_size; n <<= 1) {
			drm_random_reorder(order, total_size, &prng);
			if (evict_color(test, &mm, range_start, range_end, nodes, order,
					total_size, range_size / 2, n, color++, mode)) {
				KUNIT_FAIL(test,
					   "%s evict_color(size=%u, alignment=%u) failed for range [%x, %x]\n",
					   mode->name, total_size / 2, n, range_start, range_end);
				goto out;
			}
		}

		for_each_prime_number_from(n, 1, min(range_size, max_prime)) {
			unsigned int nsize = (range_size - n + 1) / 2;

			DRM_MM_BUG_ON(!nsize);

			drm_random_reorder(order, total_size, &prng);
			if (evict_color(test, &mm, range_start, range_end, nodes, order,
					total_size, nsize, n, color++, mode)) {
				KUNIT_FAIL(test,
					   "%s evict_color(size=%u, alignment=%u) failed for range [%x, %x]\n",
					   mode->name, nsize, n, range_start, range_end);
				goto out;
			}
		}

		cond_resched();
	}

out:
	drm_mm_for_each_node_safe(node, next, &mm)
		drm_mm_remove_node(node);
	drm_mm_takedown(&mm);
	kfree(order);
err_nodes:
	vfree(nodes);
}

static int drm_mm_init_test(struct kunit *test)
{
	while (!random_seed)
		random_seed = get_random_int();

	return 0;
}

module_param(random_seed, uint, 0400);
module_param(max_iterations, uint, 0400);
module_param(max_prime, uint, 0400);

static struct kunit_case drm_mm_tests[] = {
	KUNIT_CASE(drm_test_mm_init),
	KUNIT_CASE(drm_test_mm_debug),
	KUNIT_CASE(drm_test_mm_reserve),
	KUNIT_CASE(drm_test_mm_insert),
	KUNIT_CASE(drm_test_mm_replace),
	KUNIT_CASE(drm_test_mm_insert_range),
	KUNIT_CASE(drm_test_mm_frag),
	KUNIT_CASE(drm_test_mm_align),
	KUNIT_CASE(drm_test_mm_align32),
	KUNIT_CASE(drm_test_mm_align64),
	KUNIT_CASE(drm_test_mm_evict),
	KUNIT_CASE(drm_test_mm_evict_range),
	KUNIT_CASE(drm_test_mm_topdown),
	KUNIT_CASE(drm_test_mm_bottomup),
	KUNIT_CASE(drm_test_mm_lowest),
	KUNIT_CASE(drm_test_mm_highest),
	KUNIT_CASE(drm_test_mm_color),
	KUNIT_CASE(drm_test_mm_color_evict),
	KUNIT_CASE(drm_test_mm_color_evict_range),
	{}
};

static struct kunit_suite drm_mm_test_suite = {
	.name = "drm_mm",
	.init = drm_mm_init_test,
	.test_cases = drm_mm_tests,
};

kunit_test_suite(drm_mm_test_suite);

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL");
