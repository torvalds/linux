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
		KUNIT_FAIL(test, "mm not one hole on creation");
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
		KUNIT_FAIL(test, "mm has holes when filled");
		goto out;
	}

	/* And then after emptying it again, the massive hole should be back */
	drm_mm_remove_node(&tmp);
	if (!assert_one_hole(test, &mm, 0, size)) {
		KUNIT_FAIL(test, "mm does not have single hole after emptying");
		goto out;
	}

out:
	drm_mm_takedown(&mm);
}

static void drm_test_mm_debug(struct kunit *test)
{
	struct drm_printer p = drm_dbg_printer(NULL, DRM_UT_CORE, test->name);
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

	drm_mm_print(&mm, &p);
	KUNIT_SUCCEED(test);
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

static struct kunit_case drm_mm_tests[] = {
	KUNIT_CASE(drm_test_mm_init),
	KUNIT_CASE(drm_test_mm_debug),
	KUNIT_CASE(drm_test_mm_align32),
	KUNIT_CASE(drm_test_mm_align64),
	KUNIT_CASE(drm_test_mm_lowest),
	KUNIT_CASE(drm_test_mm_highest),
	{}
};

static struct kunit_suite drm_mm_test_suite = {
	.name = "drm_mm",
	.test_cases = drm_mm_tests,
};

kunit_test_suite(drm_mm_test_suite);

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL");
