// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 * Copyright © 2022 Maíra Canal <mairacanal@riseup.net>
 */

#include <kunit/test.h>

#include <linux/prime_numbers.h>
#include <linux/sched/signal.h>
#include <linux/sizes.h>

#include <drm/drm_buddy.h>

#include "../lib/drm_random.h"

static unsigned int random_seed;

static inline u64 get_size(int order, u64 chunk_size)
{
	return (1 << order) * chunk_size;
}

static void drm_test_buddy_alloc_range_bias(struct kunit *test)
{
	u32 mm_size, size, ps, bias_size, bias_start, bias_end, bias_rem;
	DRM_RND_STATE(prng, random_seed);
	unsigned int i, count, *order;
	struct drm_buddy_block *block;
	unsigned long flags;
	struct drm_buddy mm;
	LIST_HEAD(allocated);

	bias_size = SZ_1M;
	ps = roundup_pow_of_two(prandom_u32_state(&prng) % bias_size);
	ps = max(SZ_4K, ps);
	mm_size = (SZ_8M-1) & ~(ps-1); /* Multiple roots */

	kunit_info(test, "mm_size=%u, ps=%u\n", mm_size, ps);

	KUNIT_ASSERT_FALSE_MSG(test, drm_buddy_init(&mm, mm_size, ps),
			       "buddy_init failed\n");

	count = mm_size / bias_size;
	order = drm_random_order(count, &prng);
	KUNIT_EXPECT_TRUE(test, order);

	/*
	 * Idea is to split the address space into uniform bias ranges, and then
	 * in some random order allocate within each bias, using various
	 * patterns within. This should detect if allocations leak out from a
	 * given bias, for example.
	 */

	for (i = 0; i < count; i++) {
		LIST_HEAD(tmp);
		u32 size;

		bias_start = order[i] * bias_size;
		bias_end = bias_start + bias_size;
		bias_rem = bias_size;

		/* internal round_up too big */
		KUNIT_ASSERT_TRUE_MSG(test,
				      drm_buddy_alloc_blocks(&mm, bias_start,
							     bias_end, bias_size + ps, bias_size,
							     &allocated,
							     DRM_BUDDY_RANGE_ALLOCATION),
				      "buddy_alloc failed with bias(%x-%x), size=%u, ps=%u\n",
				      bias_start, bias_end, bias_size, bias_size);

		/* size too big */
		KUNIT_ASSERT_TRUE_MSG(test,
				      drm_buddy_alloc_blocks(&mm, bias_start,
							     bias_end, bias_size + ps, ps,
							     &allocated,
							     DRM_BUDDY_RANGE_ALLOCATION),
				      "buddy_alloc didn't fail with bias(%x-%x), size=%u, ps=%u\n",
				      bias_start, bias_end, bias_size + ps, ps);

		/* bias range too small for size */
		KUNIT_ASSERT_TRUE_MSG(test,
				      drm_buddy_alloc_blocks(&mm, bias_start + ps,
							     bias_end, bias_size, ps,
							     &allocated,
							     DRM_BUDDY_RANGE_ALLOCATION),
				      "buddy_alloc didn't fail with bias(%x-%x), size=%u, ps=%u\n",
				      bias_start + ps, bias_end, bias_size, ps);

		/* bias misaligned */
		KUNIT_ASSERT_TRUE_MSG(test,
				      drm_buddy_alloc_blocks(&mm, bias_start + ps,
							     bias_end - ps,
							     bias_size >> 1, bias_size >> 1,
							     &allocated,
							     DRM_BUDDY_RANGE_ALLOCATION),
				      "buddy_alloc h didn't fail with bias(%x-%x), size=%u, ps=%u\n",
				      bias_start + ps, bias_end - ps, bias_size >> 1, bias_size >> 1);

		/* single big page */
		KUNIT_ASSERT_FALSE_MSG(test,
				       drm_buddy_alloc_blocks(&mm, bias_start,
							      bias_end, bias_size, bias_size,
							      &tmp,
							      DRM_BUDDY_RANGE_ALLOCATION),
				       "buddy_alloc i failed with bias(%x-%x), size=%u, ps=%u\n",
				       bias_start, bias_end, bias_size, bias_size);
		drm_buddy_free_list(&mm, &tmp, 0);

		/* single page with internal round_up */
		KUNIT_ASSERT_FALSE_MSG(test,
				       drm_buddy_alloc_blocks(&mm, bias_start,
							      bias_end, ps, bias_size,
							      &tmp,
							      DRM_BUDDY_RANGE_ALLOCATION),
				       "buddy_alloc failed with bias(%x-%x), size=%u, ps=%u\n",
				       bias_start, bias_end, ps, bias_size);
		drm_buddy_free_list(&mm, &tmp, 0);

		/* random size within */
		size = max(round_up(prandom_u32_state(&prng) % bias_rem, ps), ps);
		if (size)
			KUNIT_ASSERT_FALSE_MSG(test,
					       drm_buddy_alloc_blocks(&mm, bias_start,
								      bias_end, size, ps,
								      &tmp,
								      DRM_BUDDY_RANGE_ALLOCATION),
					       "buddy_alloc failed with bias(%x-%x), size=%u, ps=%u\n",
					       bias_start, bias_end, size, ps);

		bias_rem -= size;
		/* too big for current avail */
		KUNIT_ASSERT_TRUE_MSG(test,
				      drm_buddy_alloc_blocks(&mm, bias_start,
							     bias_end, bias_rem + ps, ps,
							     &allocated,
							     DRM_BUDDY_RANGE_ALLOCATION),
				      "buddy_alloc didn't fail with bias(%x-%x), size=%u, ps=%u\n",
				      bias_start, bias_end, bias_rem + ps, ps);

		if (bias_rem) {
			/* random fill of the remainder */
			size = max(round_up(prandom_u32_state(&prng) % bias_rem, ps), ps);
			size = max(size, ps);

			KUNIT_ASSERT_FALSE_MSG(test,
					       drm_buddy_alloc_blocks(&mm, bias_start,
								      bias_end, size, ps,
								      &allocated,
								      DRM_BUDDY_RANGE_ALLOCATION),
					       "buddy_alloc failed with bias(%x-%x), size=%u, ps=%u\n",
					       bias_start, bias_end, size, ps);
			/*
			 * Intentionally allow some space to be left
			 * unallocated, and ideally not always on the bias
			 * boundaries.
			 */
			drm_buddy_free_list(&mm, &tmp, 0);
		} else {
			list_splice_tail(&tmp, &allocated);
		}
	}

	kfree(order);
	drm_buddy_free_list(&mm, &allocated, 0);
	drm_buddy_fini(&mm);

	/*
	 * Something more free-form. Idea is to pick a random starting bias
	 * range within the address space and then start filling it up. Also
	 * randomly grow the bias range in both directions as we go along. This
	 * should give us bias start/end which is not always uniform like above,
	 * and in some cases will require the allocator to jump over already
	 * allocated nodes in the middle of the address space.
	 */

	KUNIT_ASSERT_FALSE_MSG(test, drm_buddy_init(&mm, mm_size, ps),
			       "buddy_init failed\n");

	bias_start = round_up(prandom_u32_state(&prng) % (mm_size - ps), ps);
	bias_end = round_up(bias_start + prandom_u32_state(&prng) % (mm_size - bias_start), ps);
	bias_end = max(bias_end, bias_start + ps);
	bias_rem = bias_end - bias_start;

	do {
		u32 size = max(round_up(prandom_u32_state(&prng) % bias_rem, ps), ps);

		KUNIT_ASSERT_FALSE_MSG(test,
				       drm_buddy_alloc_blocks(&mm, bias_start,
							      bias_end, size, ps,
							      &allocated,
							      DRM_BUDDY_RANGE_ALLOCATION),
				       "buddy_alloc failed with bias(%x-%x), size=%u, ps=%u\n",
				       bias_start, bias_end, size, ps);
		bias_rem -= size;

		/*
		 * Try to randomly grow the bias range in both directions, or
		 * only one, or perhaps don't grow at all.
		 */
		do {
			u32 old_bias_start = bias_start;
			u32 old_bias_end = bias_end;

			if (bias_start)
				bias_start -= round_up(prandom_u32_state(&prng) % bias_start, ps);
			if (bias_end != mm_size)
				bias_end += round_up(prandom_u32_state(&prng) % (mm_size - bias_end), ps);

			bias_rem += old_bias_start - bias_start;
			bias_rem += bias_end - old_bias_end;
		} while (!bias_rem && (bias_start || bias_end != mm_size));
	} while (bias_rem);

	KUNIT_ASSERT_EQ(test, bias_start, 0);
	KUNIT_ASSERT_EQ(test, bias_end, mm_size);
	KUNIT_ASSERT_TRUE_MSG(test,
			      drm_buddy_alloc_blocks(&mm, bias_start, bias_end,
						     ps, ps,
						     &allocated,
						     DRM_BUDDY_RANGE_ALLOCATION),
			      "buddy_alloc passed with bias(%x-%x), size=%u\n",
			      bias_start, bias_end, ps);

	drm_buddy_free_list(&mm, &allocated, 0);
	drm_buddy_fini(&mm);

	/*
	 * Allocate cleared blocks in the bias range when the DRM buddy's clear avail is
	 * zero. This will validate the bias range allocation in scenarios like system boot
	 * when no cleared blocks are available and exercise the fallback path too. The resulting
	 * blocks should always be dirty.
	 */

	KUNIT_ASSERT_FALSE_MSG(test, drm_buddy_init(&mm, mm_size, ps),
			       "buddy_init failed\n");

	bias_start = round_up(prandom_u32_state(&prng) % (mm_size - ps), ps);
	bias_end = round_up(bias_start + prandom_u32_state(&prng) % (mm_size - bias_start), ps);
	bias_end = max(bias_end, bias_start + ps);
	bias_rem = bias_end - bias_start;

	flags = DRM_BUDDY_CLEAR_ALLOCATION | DRM_BUDDY_RANGE_ALLOCATION;
	size = max(round_up(prandom_u32_state(&prng) % bias_rem, ps), ps);

	KUNIT_ASSERT_FALSE_MSG(test,
			       drm_buddy_alloc_blocks(&mm, bias_start,
						      bias_end, size, ps,
						      &allocated,
						      flags),
			       "buddy_alloc failed with bias(%x-%x), size=%u, ps=%u\n",
			       bias_start, bias_end, size, ps);

	list_for_each_entry(block, &allocated, link)
		KUNIT_EXPECT_EQ(test, drm_buddy_block_is_clear(block), false);

	drm_buddy_free_list(&mm, &allocated, 0);
	drm_buddy_fini(&mm);
}

static void drm_test_buddy_alloc_clear(struct kunit *test)
{
	unsigned long n_pages, total, i = 0;
	DRM_RND_STATE(prng, random_seed);
	const unsigned long ps = SZ_4K;
	struct drm_buddy_block *block;
	const int max_order = 12;
	LIST_HEAD(allocated);
	struct drm_buddy mm;
	unsigned int order;
	u32 mm_size, size;
	LIST_HEAD(dirty);
	LIST_HEAD(clean);

	mm_size = SZ_4K << max_order;
	KUNIT_EXPECT_FALSE(test, drm_buddy_init(&mm, mm_size, ps));

	KUNIT_EXPECT_EQ(test, mm.max_order, max_order);

	/*
	 * Idea is to allocate and free some random portion of the address space,
	 * returning those pages as non-dirty and randomly alternate between
	 * requesting dirty and non-dirty pages (not going over the limit
	 * we freed as non-dirty), putting that into two separate lists.
	 * Loop over both lists at the end checking that the dirty list
	 * is indeed all dirty pages and vice versa. Free it all again,
	 * keeping the dirty/clear status.
	 */
	KUNIT_ASSERT_FALSE_MSG(test, drm_buddy_alloc_blocks(&mm, 0, mm_size,
							    5 * ps, ps, &allocated,
							    DRM_BUDDY_TOPDOWN_ALLOCATION),
				"buddy_alloc hit an error size=%lu\n", 5 * ps);
	drm_buddy_free_list(&mm, &allocated, DRM_BUDDY_CLEARED);

	n_pages = 10;
	do {
		unsigned long flags;
		struct list_head *list;
		int slot = i % 2;

		if (slot == 0) {
			list = &dirty;
			flags = 0;
		} else {
			list = &clean;
			flags = DRM_BUDDY_CLEAR_ALLOCATION;
		}

		KUNIT_ASSERT_FALSE_MSG(test, drm_buddy_alloc_blocks(&mm, 0, mm_size,
								    ps, ps, list,
								    flags),
					"buddy_alloc hit an error size=%lu\n", ps);
	} while (++i < n_pages);

	list_for_each_entry(block, &clean, link)
		KUNIT_EXPECT_EQ(test, drm_buddy_block_is_clear(block), true);

	list_for_each_entry(block, &dirty, link)
		KUNIT_EXPECT_EQ(test, drm_buddy_block_is_clear(block), false);

	drm_buddy_free_list(&mm, &clean, DRM_BUDDY_CLEARED);

	/*
	 * Trying to go over the clear limit for some allocation.
	 * The allocation should never fail with reasonable page-size.
	 */
	KUNIT_ASSERT_FALSE_MSG(test, drm_buddy_alloc_blocks(&mm, 0, mm_size,
							    10 * ps, ps, &clean,
							    DRM_BUDDY_CLEAR_ALLOCATION),
				"buddy_alloc hit an error size=%lu\n", 10 * ps);

	drm_buddy_free_list(&mm, &clean, DRM_BUDDY_CLEARED);
	drm_buddy_free_list(&mm, &dirty, 0);
	drm_buddy_fini(&mm);

	KUNIT_EXPECT_FALSE(test, drm_buddy_init(&mm, mm_size, ps));

	/*
	 * Create a new mm. Intentionally fragment the address space by creating
	 * two alternating lists. Free both lists, one as dirty the other as clean.
	 * Try to allocate double the previous size with matching min_page_size. The
	 * allocation should never fail as it calls the force_merge. Also check that
	 * the page is always dirty after force_merge. Free the page as dirty, then
	 * repeat the whole thing, increment the order until we hit the max_order.
	 */

	i = 0;
	n_pages = mm_size / ps;
	do {
		struct list_head *list;
		int slot = i % 2;

		if (slot == 0)
			list = &dirty;
		else
			list = &clean;

		KUNIT_ASSERT_FALSE_MSG(test, drm_buddy_alloc_blocks(&mm, 0, mm_size,
								    ps, ps, list, 0),
					"buddy_alloc hit an error size=%lu\n", ps);
	} while (++i < n_pages);

	drm_buddy_free_list(&mm, &clean, DRM_BUDDY_CLEARED);
	drm_buddy_free_list(&mm, &dirty, 0);

	order = 1;
	do {
		size = SZ_4K << order;

		KUNIT_ASSERT_FALSE_MSG(test, drm_buddy_alloc_blocks(&mm, 0, mm_size,
								    size, size, &allocated,
								    DRM_BUDDY_CLEAR_ALLOCATION),
					"buddy_alloc hit an error size=%u\n", size);
		total = 0;
		list_for_each_entry(block, &allocated, link) {
			if (size != mm_size)
				KUNIT_EXPECT_EQ(test, drm_buddy_block_is_clear(block), false);
			total += drm_buddy_block_size(&mm, block);
		}
		KUNIT_EXPECT_EQ(test, total, size);

		drm_buddy_free_list(&mm, &allocated, 0);
	} while (++order <= max_order);

	drm_buddy_fini(&mm);

	/*
	 * Create a new mm with a non power-of-two size. Allocate a random size, free as
	 * cleared and then call fini. This will ensure the multi-root force merge during
	 * fini.
	 */
	mm_size = 12 * SZ_4K;
	size = max(round_up(prandom_u32_state(&prng) % mm_size, ps), ps);
	KUNIT_EXPECT_FALSE(test, drm_buddy_init(&mm, mm_size, ps));
	KUNIT_ASSERT_FALSE_MSG(test, drm_buddy_alloc_blocks(&mm, 0, mm_size,
							    size, ps, &allocated,
							    DRM_BUDDY_TOPDOWN_ALLOCATION),
				"buddy_alloc hit an error size=%u\n", size);
	drm_buddy_free_list(&mm, &allocated, DRM_BUDDY_CLEARED);
	drm_buddy_fini(&mm);
}

static void drm_test_buddy_alloc_contiguous(struct kunit *test)
{
	const unsigned long ps = SZ_4K, mm_size = 16 * 3 * SZ_4K;
	unsigned long i, n_pages, total;
	struct drm_buddy_block *block;
	struct drm_buddy mm;
	LIST_HEAD(left);
	LIST_HEAD(middle);
	LIST_HEAD(right);
	LIST_HEAD(allocated);

	KUNIT_EXPECT_FALSE(test, drm_buddy_init(&mm, mm_size, ps));

	/*
	 * Idea is to fragment the address space by alternating block
	 * allocations between three different lists; one for left, middle and
	 * right. We can then free a list to simulate fragmentation. In
	 * particular we want to exercise the DRM_BUDDY_CONTIGUOUS_ALLOCATION,
	 * including the try_harder path.
	 */

	i = 0;
	n_pages = mm_size / ps;
	do {
		struct list_head *list;
		int slot = i % 3;

		if (slot == 0)
			list = &left;
		else if (slot == 1)
			list = &middle;
		else
			list = &right;
		KUNIT_ASSERT_FALSE_MSG(test,
				       drm_buddy_alloc_blocks(&mm, 0, mm_size,
							      ps, ps, list, 0),
				       "buddy_alloc hit an error size=%lu\n",
				       ps);
	} while (++i < n_pages);

	KUNIT_ASSERT_TRUE_MSG(test, drm_buddy_alloc_blocks(&mm, 0, mm_size,
							   3 * ps, ps, &allocated,
							   DRM_BUDDY_CONTIGUOUS_ALLOCATION),
			       "buddy_alloc didn't error size=%lu\n", 3 * ps);

	drm_buddy_free_list(&mm, &middle, 0);
	KUNIT_ASSERT_TRUE_MSG(test, drm_buddy_alloc_blocks(&mm, 0, mm_size,
							   3 * ps, ps, &allocated,
							   DRM_BUDDY_CONTIGUOUS_ALLOCATION),
			       "buddy_alloc didn't error size=%lu\n", 3 * ps);
	KUNIT_ASSERT_TRUE_MSG(test, drm_buddy_alloc_blocks(&mm, 0, mm_size,
							   2 * ps, ps, &allocated,
							   DRM_BUDDY_CONTIGUOUS_ALLOCATION),
			       "buddy_alloc didn't error size=%lu\n", 2 * ps);

	drm_buddy_free_list(&mm, &right, 0);
	KUNIT_ASSERT_TRUE_MSG(test, drm_buddy_alloc_blocks(&mm, 0, mm_size,
							   3 * ps, ps, &allocated,
							   DRM_BUDDY_CONTIGUOUS_ALLOCATION),
			       "buddy_alloc didn't error size=%lu\n", 3 * ps);
	/*
	 * At this point we should have enough contiguous space for 2 blocks,
	 * however they are never buddies (since we freed middle and right) so
	 * will require the try_harder logic to find them.
	 */
	KUNIT_ASSERT_FALSE_MSG(test, drm_buddy_alloc_blocks(&mm, 0, mm_size,
							    2 * ps, ps, &allocated,
							    DRM_BUDDY_CONTIGUOUS_ALLOCATION),
			       "buddy_alloc hit an error size=%lu\n", 2 * ps);

	drm_buddy_free_list(&mm, &left, 0);
	KUNIT_ASSERT_FALSE_MSG(test, drm_buddy_alloc_blocks(&mm, 0, mm_size,
							    3 * ps, ps, &allocated,
							    DRM_BUDDY_CONTIGUOUS_ALLOCATION),
			       "buddy_alloc hit an error size=%lu\n", 3 * ps);

	total = 0;
	list_for_each_entry(block, &allocated, link)
		total += drm_buddy_block_size(&mm, block);

	KUNIT_ASSERT_EQ(test, total, ps * 2 + ps * 3);

	drm_buddy_free_list(&mm, &allocated, 0);
	drm_buddy_fini(&mm);
}

static void drm_test_buddy_alloc_pathological(struct kunit *test)
{
	u64 mm_size, size, start = 0;
	struct drm_buddy_block *block;
	const int max_order = 3;
	unsigned long flags = 0;
	int order, top;
	struct drm_buddy mm;
	LIST_HEAD(blocks);
	LIST_HEAD(holes);
	LIST_HEAD(tmp);

	/*
	 * Create a pot-sized mm, then allocate one of each possible
	 * order within. This should leave the mm with exactly one
	 * page left. Free the largest block, then whittle down again.
	 * Eventually we will have a fully 50% fragmented mm.
	 */

	mm_size = PAGE_SIZE << max_order;
	KUNIT_ASSERT_FALSE_MSG(test, drm_buddy_init(&mm, mm_size, PAGE_SIZE),
			       "buddy_init failed\n");

	KUNIT_EXPECT_EQ(test, mm.max_order, max_order);

	for (top = max_order; top; top--) {
		/* Make room by freeing the largest allocated block */
		block = list_first_entry_or_null(&blocks, typeof(*block), link);
		if (block) {
			list_del(&block->link);
			drm_buddy_free_block(&mm, block);
		}

		for (order = top; order--;) {
			size = get_size(order, PAGE_SIZE);
			KUNIT_ASSERT_FALSE_MSG(test, drm_buddy_alloc_blocks(&mm, start,
									    mm_size, size, size,
										&tmp, flags),
					"buddy_alloc hit -ENOMEM with order=%d, top=%d\n",
					order, top);

			block = list_first_entry_or_null(&tmp, struct drm_buddy_block, link);
			KUNIT_ASSERT_TRUE_MSG(test, block, "alloc_blocks has no blocks\n");

			list_move_tail(&block->link, &blocks);
		}

		/* There should be one final page for this sub-allocation */
		size = get_size(0, PAGE_SIZE);
		KUNIT_ASSERT_FALSE_MSG(test, drm_buddy_alloc_blocks(&mm, start, mm_size,
								    size, size, &tmp, flags),
							   "buddy_alloc hit -ENOMEM for hole\n");

		block = list_first_entry_or_null(&tmp, struct drm_buddy_block, link);
		KUNIT_ASSERT_TRUE_MSG(test, block, "alloc_blocks has no blocks\n");

		list_move_tail(&block->link, &holes);

		size = get_size(top, PAGE_SIZE);
		KUNIT_ASSERT_TRUE_MSG(test, drm_buddy_alloc_blocks(&mm, start, mm_size,
								   size, size, &tmp, flags),
							  "buddy_alloc unexpectedly succeeded at top-order %d/%d, it should be full!",
							  top, max_order);
	}

	drm_buddy_free_list(&mm, &holes, 0);

	/* Nothing larger than blocks of chunk_size now available */
	for (order = 1; order <= max_order; order++) {
		size = get_size(order, PAGE_SIZE);
		KUNIT_ASSERT_TRUE_MSG(test, drm_buddy_alloc_blocks(&mm, start, mm_size,
								   size, size, &tmp, flags),
							  "buddy_alloc unexpectedly succeeded at order %d, it should be full!",
							  order);
	}

	list_splice_tail(&holes, &blocks);
	drm_buddy_free_list(&mm, &blocks, 0);
	drm_buddy_fini(&mm);
}

static void drm_test_buddy_alloc_pessimistic(struct kunit *test)
{
	u64 mm_size, size, start = 0;
	struct drm_buddy_block *block, *bn;
	const unsigned int max_order = 16;
	unsigned long flags = 0;
	struct drm_buddy mm;
	unsigned int order;
	LIST_HEAD(blocks);
	LIST_HEAD(tmp);

	/*
	 * Create a pot-sized mm, then allocate one of each possible
	 * order within. This should leave the mm with exactly one
	 * page left.
	 */

	mm_size = PAGE_SIZE << max_order;
	KUNIT_ASSERT_FALSE_MSG(test, drm_buddy_init(&mm, mm_size, PAGE_SIZE),
			       "buddy_init failed\n");

	KUNIT_EXPECT_EQ(test, mm.max_order, max_order);

	for (order = 0; order < max_order; order++) {
		size = get_size(order, PAGE_SIZE);
		KUNIT_ASSERT_FALSE_MSG(test, drm_buddy_alloc_blocks(&mm, start, mm_size,
								    size, size, &tmp, flags),
							   "buddy_alloc hit -ENOMEM with order=%d\n",
							   order);

		block = list_first_entry_or_null(&tmp, struct drm_buddy_block, link);
		KUNIT_ASSERT_TRUE_MSG(test, block, "alloc_blocks has no blocks\n");

		list_move_tail(&block->link, &blocks);
	}

	/* And now the last remaining block available */
	size = get_size(0, PAGE_SIZE);
	KUNIT_ASSERT_FALSE_MSG(test, drm_buddy_alloc_blocks(&mm, start, mm_size,
							    size, size, &tmp, flags),
						   "buddy_alloc hit -ENOMEM on final alloc\n");

	block = list_first_entry_or_null(&tmp, struct drm_buddy_block, link);
	KUNIT_ASSERT_TRUE_MSG(test, block, "alloc_blocks has no blocks\n");

	list_move_tail(&block->link, &blocks);

	/* Should be completely full! */
	for (order = max_order; order--;) {
		size = get_size(order, PAGE_SIZE);
		KUNIT_ASSERT_TRUE_MSG(test, drm_buddy_alloc_blocks(&mm, start, mm_size,
								   size, size, &tmp, flags),
							  "buddy_alloc unexpectedly succeeded, it should be full!");
	}

	block = list_last_entry(&blocks, typeof(*block), link);
	list_del(&block->link);
	drm_buddy_free_block(&mm, block);

	/* As we free in increasing size, we make available larger blocks */
	order = 1;
	list_for_each_entry_safe(block, bn, &blocks, link) {
		list_del(&block->link);
		drm_buddy_free_block(&mm, block);

		size = get_size(order, PAGE_SIZE);
		KUNIT_ASSERT_FALSE_MSG(test, drm_buddy_alloc_blocks(&mm, start, mm_size,
								    size, size, &tmp, flags),
							   "buddy_alloc hit -ENOMEM with order=%d\n",
							   order);

		block = list_first_entry_or_null(&tmp, struct drm_buddy_block, link);
		KUNIT_ASSERT_TRUE_MSG(test, block, "alloc_blocks has no blocks\n");

		list_del(&block->link);
		drm_buddy_free_block(&mm, block);
		order++;
	}

	/* To confirm, now the whole mm should be available */
	size = get_size(max_order, PAGE_SIZE);
	KUNIT_ASSERT_FALSE_MSG(test, drm_buddy_alloc_blocks(&mm, start, mm_size,
							    size, size, &tmp, flags),
						   "buddy_alloc (realloc) hit -ENOMEM with order=%d\n",
						   max_order);

	block = list_first_entry_or_null(&tmp, struct drm_buddy_block, link);
	KUNIT_ASSERT_TRUE_MSG(test, block, "alloc_blocks has no blocks\n");

	list_del(&block->link);
	drm_buddy_free_block(&mm, block);
	drm_buddy_free_list(&mm, &blocks, 0);
	drm_buddy_fini(&mm);
}

static void drm_test_buddy_alloc_optimistic(struct kunit *test)
{
	u64 mm_size, size, start = 0;
	struct drm_buddy_block *block;
	unsigned long flags = 0;
	const int max_order = 16;
	struct drm_buddy mm;
	LIST_HEAD(blocks);
	LIST_HEAD(tmp);
	int order;

	/*
	 * Create a mm with one block of each order available, and
	 * try to allocate them all.
	 */

	mm_size = PAGE_SIZE * ((1 << (max_order + 1)) - 1);

	KUNIT_ASSERT_FALSE_MSG(test, drm_buddy_init(&mm, mm_size, PAGE_SIZE),
			       "buddy_init failed\n");

	KUNIT_EXPECT_EQ(test, mm.max_order, max_order);

	for (order = 0; order <= max_order; order++) {
		size = get_size(order, PAGE_SIZE);
		KUNIT_ASSERT_FALSE_MSG(test, drm_buddy_alloc_blocks(&mm, start, mm_size,
								    size, size, &tmp, flags),
							   "buddy_alloc hit -ENOMEM with order=%d\n",
							   order);

		block = list_first_entry_or_null(&tmp, struct drm_buddy_block, link);
		KUNIT_ASSERT_TRUE_MSG(test, block, "alloc_blocks has no blocks\n");

		list_move_tail(&block->link, &blocks);
	}

	/* Should be completely full! */
	size = get_size(0, PAGE_SIZE);
	KUNIT_ASSERT_TRUE_MSG(test, drm_buddy_alloc_blocks(&mm, start, mm_size,
							   size, size, &tmp, flags),
						  "buddy_alloc unexpectedly succeeded, it should be full!");

	drm_buddy_free_list(&mm, &blocks, 0);
	drm_buddy_fini(&mm);
}

static void drm_test_buddy_alloc_limit(struct kunit *test)
{
	u64 size = U64_MAX, start = 0;
	struct drm_buddy_block *block;
	unsigned long flags = 0;
	LIST_HEAD(allocated);
	struct drm_buddy mm;

	KUNIT_EXPECT_FALSE(test, drm_buddy_init(&mm, size, PAGE_SIZE));

	KUNIT_EXPECT_EQ_MSG(test, mm.max_order, DRM_BUDDY_MAX_ORDER,
			    "mm.max_order(%d) != %d\n", mm.max_order,
						DRM_BUDDY_MAX_ORDER);

	size = mm.chunk_size << mm.max_order;
	KUNIT_EXPECT_FALSE(test, drm_buddy_alloc_blocks(&mm, start, size, size,
							PAGE_SIZE, &allocated, flags));

	block = list_first_entry_or_null(&allocated, struct drm_buddy_block, link);
	KUNIT_EXPECT_TRUE(test, block);

	KUNIT_EXPECT_EQ_MSG(test, drm_buddy_block_order(block), mm.max_order,
			    "block order(%d) != %d\n",
						drm_buddy_block_order(block), mm.max_order);

	KUNIT_EXPECT_EQ_MSG(test, drm_buddy_block_size(&mm, block),
			    BIT_ULL(mm.max_order) * PAGE_SIZE,
						"block size(%llu) != %llu\n",
						drm_buddy_block_size(&mm, block),
						BIT_ULL(mm.max_order) * PAGE_SIZE);

	drm_buddy_free_list(&mm, &allocated, 0);
	drm_buddy_fini(&mm);
}

static int drm_buddy_suite_init(struct kunit_suite *suite)
{
	while (!random_seed)
		random_seed = get_random_u32();

	kunit_info(suite, "Testing DRM buddy manager, with random_seed=0x%x\n",
		   random_seed);

	return 0;
}

static struct kunit_case drm_buddy_tests[] = {
	KUNIT_CASE(drm_test_buddy_alloc_limit),
	KUNIT_CASE(drm_test_buddy_alloc_optimistic),
	KUNIT_CASE(drm_test_buddy_alloc_pessimistic),
	KUNIT_CASE(drm_test_buddy_alloc_pathological),
	KUNIT_CASE(drm_test_buddy_alloc_contiguous),
	KUNIT_CASE(drm_test_buddy_alloc_clear),
	KUNIT_CASE(drm_test_buddy_alloc_range_bias),
	{}
};

static struct kunit_suite drm_buddy_test_suite = {
	.name = "drm_buddy",
	.suite_init = drm_buddy_suite_init,
	.test_cases = drm_buddy_tests,
};

kunit_test_suite(drm_buddy_test_suite);

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL");
