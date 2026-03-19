// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 * Copyright © 2022 Maíra Canal <mairacanal@riseup.net>
 */

#include <kunit/test.h>

#include <linux/prime_numbers.h>
#include <linux/sched/signal.h>
#include <linux/sizes.h>

#include <linux/gpu_buddy.h>

#include "gpu_random.h"

static unsigned int random_seed;

static inline u64 get_size(int order, u64 chunk_size)
{
	return (1 << order) * chunk_size;
}

static void gpu_test_buddy_subtree_offset_alignment_stress(struct kunit *test)
{
	struct gpu_buddy_block *block;
	struct rb_node *node = NULL;
	const u64 mm_size = SZ_2M;
	const u64 alignments[] = {
		SZ_1M,
		SZ_512K,
		SZ_256K,
		SZ_128K,
		SZ_64K,
		SZ_32K,
		SZ_16K,
		SZ_8K,
	};
	struct list_head allocated[ARRAY_SIZE(alignments)];
	unsigned int i, max_subtree_align = 0;
	int ret, tree, order;
	struct gpu_buddy mm;

	KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_init(&mm, mm_size, SZ_4K),
			       "buddy_init failed\n");

	for (i = 0; i < ARRAY_SIZE(allocated); i++)
		INIT_LIST_HEAD(&allocated[i]);

	/*
	 * Exercise subtree_max_alignment tracking by allocating blocks with descending
	 * alignment constraints and freeing them in reverse order. This verifies that
	 * free-tree augmentation correctly propagates the maximum offset alignment
	 * present in each subtree at every stage.
	 */

	for (i = 0; i < ARRAY_SIZE(alignments); i++) {
		struct gpu_buddy_block *root = NULL;
		unsigned int expected;
		u64 align;

		align = alignments[i];
		expected = ilog2(align) - 1;

		for (;;) {
			ret = gpu_buddy_alloc_blocks(&mm,
						     0, mm_size,
						     SZ_4K, align,
						     &allocated[i],
						     0);
			if (ret)
				break;

			block = list_last_entry(&allocated[i],
						struct gpu_buddy_block,
						link);
			KUNIT_EXPECT_TRUE(test, IS_ALIGNED(gpu_buddy_block_offset(block), align));
		}

		for (order = mm.max_order; order >= 0 && !root; order--) {
			for (tree = 0; tree < 2; tree++) {
				node = mm.free_trees[tree][order].rb_node;
				if (node) {
					root = container_of(node,
							    struct gpu_buddy_block,
							    rb);
					break;
				}
			}
		}

		KUNIT_ASSERT_NOT_NULL(test, root);
		KUNIT_EXPECT_EQ(test, root->subtree_max_alignment, expected);
	}

	for (i = ARRAY_SIZE(alignments); i-- > 0; ) {
		gpu_buddy_free_list(&mm, &allocated[i], 0);

		for (order = 0; order <= mm.max_order; order++) {
			for (tree = 0; tree < 2; tree++) {
				node = mm.free_trees[tree][order].rb_node;
				if (!node)
					continue;

				block = container_of(node, struct gpu_buddy_block, rb);
				max_subtree_align = max(max_subtree_align,
							block->subtree_max_alignment);
			}
		}

		KUNIT_EXPECT_GE(test, max_subtree_align, ilog2(alignments[i]));
	}

	gpu_buddy_fini(&mm);
}

static void gpu_test_buddy_offset_aligned_allocation(struct kunit *test)
{
	struct gpu_buddy_block *block, *tmp;
	int num_blocks, i, count = 0;
	LIST_HEAD(allocated);
	struct gpu_buddy mm;
	u64 mm_size = SZ_4M;
	LIST_HEAD(freed);

	KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_init(&mm, mm_size, SZ_4K),
			       "buddy_init failed\n");

	num_blocks = mm_size / SZ_256K;
	/*
	 * Allocate multiple sizes under a fixed offset alignment.
	 * Ensures alignment handling is independent of allocation size and
	 * exercises subtree max-alignment pruning for small requests.
	 */
	for (i = 0; i < num_blocks; i++)
		KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_alloc_blocks(&mm, 0, mm_size, SZ_8K, SZ_256K,
								    &allocated, 0),
					"buddy_alloc hit an error size=%u\n", SZ_8K);

	list_for_each_entry(block, &allocated, link) {
		/* Ensure the allocated block uses the expected 8 KB size */
		KUNIT_EXPECT_EQ(test, gpu_buddy_block_size(&mm, block), SZ_8K);
		/* Ensure the block starts at a 256 KB-aligned offset for proper alignment */
		KUNIT_EXPECT_TRUE(test, IS_ALIGNED(gpu_buddy_block_offset(block), SZ_256K));
	}
	gpu_buddy_free_list(&mm, &allocated, 0);

	for (i = 0; i < num_blocks; i++)
		KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_alloc_blocks(&mm, 0, mm_size, SZ_16K, SZ_256K,
								    &allocated, 0),
					"buddy_alloc hit an error size=%u\n", SZ_16K);

	list_for_each_entry(block, &allocated, link) {
		/* Ensure the allocated block uses the expected 16 KB size */
		KUNIT_EXPECT_EQ(test, gpu_buddy_block_size(&mm, block), SZ_16K);
		/* Ensure the block starts at a 256 KB-aligned offset for proper alignment */
		KUNIT_EXPECT_TRUE(test, IS_ALIGNED(gpu_buddy_block_offset(block), SZ_256K));
	}

	/*
	 * Free alternating aligned blocks to introduce fragmentation.
	 * Ensures offset-aligned allocations remain valid after frees and
	 * verifies subtree max-alignment metadata is correctly maintained.
	 */
	list_for_each_entry_safe(block, tmp, &allocated, link) {
		if (count % 2 == 0)
			list_move_tail(&block->link, &freed);
		count++;
	}
	gpu_buddy_free_list(&mm, &freed, 0);

	for (i = 0; i < num_blocks / 2; i++)
		KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_alloc_blocks(&mm, 0, mm_size, SZ_16K, SZ_256K,
								    &allocated, 0),
					"buddy_alloc hit an error size=%u\n", SZ_16K);

	/*
	 * Allocate with offset alignment after all slots are used; must fail.
	 * Confirms that no aligned offsets remain.
	 */
	KUNIT_ASSERT_TRUE_MSG(test, gpu_buddy_alloc_blocks(&mm, 0, mm_size, SZ_16K, SZ_256K,
							   &allocated, 0),
			       "buddy_alloc hit an error size=%u\n", SZ_16K);
	gpu_buddy_free_list(&mm, &allocated, 0);
	gpu_buddy_fini(&mm);
}

static void gpu_test_buddy_fragmentation_performance(struct kunit *test)
{
	struct gpu_buddy_block *block, *tmp;
	int num_blocks, i, ret, count = 0;
	LIST_HEAD(allocated_blocks);
	unsigned long elapsed_ms;
	LIST_HEAD(reverse_list);
	LIST_HEAD(test_blocks);
	LIST_HEAD(clear_list);
	LIST_HEAD(dirty_list);
	LIST_HEAD(free_list);
	struct gpu_buddy mm;
	u64 mm_size = SZ_4G;
	ktime_t start, end;

	/*
	 * Allocation under severe fragmentation
	 *
	 * Create severe fragmentation by allocating the entire 4 GiB address space
	 * as tiny 8 KiB blocks but forcing a 64 KiB alignment. The resulting pattern
	 * leaves many scattered holes. Split the allocations into two groups and
	 * return them with different flags to block coalescing, then repeatedly
	 * allocate and free 64 KiB blocks while timing the loop. This stresses how
	 * quickly the allocator can satisfy larger, aligned requests from a pool of
	 * highly fragmented space.
	 */
	KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_init(&mm, mm_size, SZ_4K),
			       "buddy_init failed\n");

	num_blocks = mm_size / SZ_64K;

	start = ktime_get();
	/* Allocate with maximum fragmentation - 8K blocks with 64K alignment */
	for (i = 0; i < num_blocks; i++)
		KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_alloc_blocks(&mm, 0, mm_size, SZ_8K, SZ_64K,
								    &allocated_blocks, 0),
					"buddy_alloc hit an error size=%u\n", SZ_8K);

	list_for_each_entry_safe(block, tmp, &allocated_blocks, link) {
		if (count % 4 == 0 || count % 4 == 3)
			list_move_tail(&block->link, &clear_list);
		else
			list_move_tail(&block->link, &dirty_list);
		count++;
	}

	/* Free with different flags to ensure no coalescing */
	gpu_buddy_free_list(&mm, &clear_list, GPU_BUDDY_CLEARED);
	gpu_buddy_free_list(&mm, &dirty_list, 0);

	for (i = 0; i < num_blocks; i++)
		KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_alloc_blocks(&mm, 0, mm_size, SZ_64K, SZ_64K,
								    &test_blocks, 0),
					"buddy_alloc hit an error size=%u\n", SZ_64K);
	gpu_buddy_free_list(&mm, &test_blocks, 0);

	end = ktime_get();
	elapsed_ms = ktime_to_ms(ktime_sub(end, start));

	kunit_info(test, "Fragmented allocation took %lu ms\n", elapsed_ms);

	gpu_buddy_fini(&mm);

	/*
	 * Reverse free order under fragmentation
	 *
	 * Construct a fragmented 4 GiB space by allocating every 8 KiB block with
	 * 64 KiB alignment, creating a dense scatter of small regions. Half of the
	 * blocks are selectively freed to form sparse gaps, while the remaining
	 * allocations are preserved, reordered in reverse, and released back with
	 * the cleared flag. This models a pathological reverse-ordered free pattern
	 * and measures how quickly the allocator can merge and reclaim space when
	 * deallocation occurs in the opposite order of allocation, exposing the
	 * cost difference between a linear freelist scan and an ordered tree lookup.
	 */
	ret = gpu_buddy_init(&mm, mm_size, SZ_4K);
	KUNIT_ASSERT_EQ(test, ret, 0);

	start = ktime_get();
	/* Allocate maximum fragmentation */
	for (i = 0; i < num_blocks; i++)
		KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_alloc_blocks(&mm, 0, mm_size, SZ_8K, SZ_64K,
								    &allocated_blocks, 0),
					"buddy_alloc hit an error size=%u\n", SZ_8K);

	list_for_each_entry_safe(block, tmp, &allocated_blocks, link) {
		if (count % 2 == 0)
			list_move_tail(&block->link, &free_list);
		count++;
	}
	gpu_buddy_free_list(&mm, &free_list, GPU_BUDDY_CLEARED);

	list_for_each_entry_safe_reverse(block, tmp, &allocated_blocks, link)
		list_move(&block->link, &reverse_list);
	gpu_buddy_free_list(&mm, &reverse_list, GPU_BUDDY_CLEARED);

	end = ktime_get();
	elapsed_ms = ktime_to_ms(ktime_sub(end, start));

	kunit_info(test, "Reverse-ordered free took %lu ms\n", elapsed_ms);

	gpu_buddy_fini(&mm);
}

static void gpu_test_buddy_alloc_range_bias(struct kunit *test)
{
	u32 mm_size, size, ps, bias_size, bias_start, bias_end, bias_rem;
	GPU_RND_STATE(prng, random_seed);
	unsigned int i, count, *order;
	struct gpu_buddy_block *block;
	unsigned long flags;
	struct gpu_buddy mm;
	LIST_HEAD(allocated);

	bias_size = SZ_1M;
	ps = roundup_pow_of_two(prandom_u32_state(&prng) % bias_size);
	ps = max(SZ_4K, ps);
	mm_size = (SZ_8M-1) & ~(ps-1); /* Multiple roots */

	kunit_info(test, "mm_size=%u, ps=%u\n", mm_size, ps);

	KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_init(&mm, mm_size, ps),
			       "buddy_init failed\n");

	count = mm_size / bias_size;
	order = gpu_random_order(count, &prng);
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
				      gpu_buddy_alloc_blocks(&mm, bias_start,
							     bias_end, bias_size + ps, bias_size,
							     &allocated,
							     GPU_BUDDY_RANGE_ALLOCATION),
				      "buddy_alloc failed with bias(%x-%x), size=%u, ps=%u\n",
				      bias_start, bias_end, bias_size, bias_size);

		/* size too big */
		KUNIT_ASSERT_TRUE_MSG(test,
				      gpu_buddy_alloc_blocks(&mm, bias_start,
							     bias_end, bias_size + ps, ps,
							     &allocated,
							     GPU_BUDDY_RANGE_ALLOCATION),
				      "buddy_alloc didn't fail with bias(%x-%x), size=%u, ps=%u\n",
				      bias_start, bias_end, bias_size + ps, ps);

		/* bias range too small for size */
		KUNIT_ASSERT_TRUE_MSG(test,
				      gpu_buddy_alloc_blocks(&mm, bias_start + ps,
							     bias_end, bias_size, ps,
							     &allocated,
							     GPU_BUDDY_RANGE_ALLOCATION),
				      "buddy_alloc didn't fail with bias(%x-%x), size=%u, ps=%u\n",
				      bias_start + ps, bias_end, bias_size, ps);

		/* bias misaligned */
		KUNIT_ASSERT_TRUE_MSG(test,
				      gpu_buddy_alloc_blocks(&mm, bias_start + ps,
							     bias_end - ps,
							     bias_size >> 1, bias_size >> 1,
							     &allocated,
							     GPU_BUDDY_RANGE_ALLOCATION),
				      "buddy_alloc h didn't fail with bias(%x-%x), size=%u, ps=%u\n",
				      bias_start + ps, bias_end - ps, bias_size >> 1, bias_size >> 1);

		/* single big page */
		KUNIT_ASSERT_FALSE_MSG(test,
				       gpu_buddy_alloc_blocks(&mm, bias_start,
							      bias_end, bias_size, bias_size,
							      &tmp,
							      GPU_BUDDY_RANGE_ALLOCATION),
				       "buddy_alloc i failed with bias(%x-%x), size=%u, ps=%u\n",
				       bias_start, bias_end, bias_size, bias_size);
		gpu_buddy_free_list(&mm, &tmp, 0);

		/* single page with internal round_up */
		KUNIT_ASSERT_FALSE_MSG(test,
				       gpu_buddy_alloc_blocks(&mm, bias_start,
							      bias_end, ps, bias_size,
							      &tmp,
							      GPU_BUDDY_RANGE_ALLOCATION),
				       "buddy_alloc failed with bias(%x-%x), size=%u, ps=%u\n",
				       bias_start, bias_end, ps, bias_size);
		gpu_buddy_free_list(&mm, &tmp, 0);

		/* random size within */
		size = max(round_up(prandom_u32_state(&prng) % bias_rem, ps), ps);
		if (size)
			KUNIT_ASSERT_FALSE_MSG(test,
					       gpu_buddy_alloc_blocks(&mm, bias_start,
								      bias_end, size, ps,
								      &tmp,
								      GPU_BUDDY_RANGE_ALLOCATION),
					       "buddy_alloc failed with bias(%x-%x), size=%u, ps=%u\n",
					       bias_start, bias_end, size, ps);

		bias_rem -= size;
		/* too big for current avail */
		KUNIT_ASSERT_TRUE_MSG(test,
				      gpu_buddy_alloc_blocks(&mm, bias_start,
							     bias_end, bias_rem + ps, ps,
							     &allocated,
							     GPU_BUDDY_RANGE_ALLOCATION),
				      "buddy_alloc didn't fail with bias(%x-%x), size=%u, ps=%u\n",
				      bias_start, bias_end, bias_rem + ps, ps);

		if (bias_rem) {
			/* random fill of the remainder */
			size = max(round_up(prandom_u32_state(&prng) % bias_rem, ps), ps);
			size = max(size, ps);

			KUNIT_ASSERT_FALSE_MSG(test,
					       gpu_buddy_alloc_blocks(&mm, bias_start,
								      bias_end, size, ps,
								      &allocated,
								      GPU_BUDDY_RANGE_ALLOCATION),
					       "buddy_alloc failed with bias(%x-%x), size=%u, ps=%u\n",
					       bias_start, bias_end, size, ps);
			/*
			 * Intentionally allow some space to be left
			 * unallocated, and ideally not always on the bias
			 * boundaries.
			 */
			gpu_buddy_free_list(&mm, &tmp, 0);
		} else {
			list_splice_tail(&tmp, &allocated);
		}
	}

	kfree(order);
	gpu_buddy_free_list(&mm, &allocated, 0);
	gpu_buddy_fini(&mm);

	/*
	 * Something more free-form. Idea is to pick a random starting bias
	 * range within the address space and then start filling it up. Also
	 * randomly grow the bias range in both directions as we go along. This
	 * should give us bias start/end which is not always uniform like above,
	 * and in some cases will require the allocator to jump over already
	 * allocated nodes in the middle of the address space.
	 */

	KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_init(&mm, mm_size, ps),
			       "buddy_init failed\n");

	bias_start = round_up(prandom_u32_state(&prng) % (mm_size - ps), ps);
	bias_end = round_up(bias_start + prandom_u32_state(&prng) % (mm_size - bias_start), ps);
	bias_end = max(bias_end, bias_start + ps);
	bias_rem = bias_end - bias_start;

	do {
		u32 size = max(round_up(prandom_u32_state(&prng) % bias_rem, ps), ps);

		KUNIT_ASSERT_FALSE_MSG(test,
				       gpu_buddy_alloc_blocks(&mm, bias_start,
							      bias_end, size, ps,
							      &allocated,
							      GPU_BUDDY_RANGE_ALLOCATION),
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
			      gpu_buddy_alloc_blocks(&mm, bias_start, bias_end,
						     ps, ps,
						     &allocated,
						     GPU_BUDDY_RANGE_ALLOCATION),
			      "buddy_alloc passed with bias(%x-%x), size=%u\n",
			      bias_start, bias_end, ps);

	gpu_buddy_free_list(&mm, &allocated, 0);
	gpu_buddy_fini(&mm);

	/*
	 * Allocate cleared blocks in the bias range when the GPU buddy's clear avail is
	 * zero. This will validate the bias range allocation in scenarios like system boot
	 * when no cleared blocks are available and exercise the fallback path too. The resulting
	 * blocks should always be dirty.
	 */

	KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_init(&mm, mm_size, ps),
			       "buddy_init failed\n");

	bias_start = round_up(prandom_u32_state(&prng) % (mm_size - ps), ps);
	bias_end = round_up(bias_start + prandom_u32_state(&prng) % (mm_size - bias_start), ps);
	bias_end = max(bias_end, bias_start + ps);
	bias_rem = bias_end - bias_start;

	flags = GPU_BUDDY_CLEAR_ALLOCATION | GPU_BUDDY_RANGE_ALLOCATION;
	size = max(round_up(prandom_u32_state(&prng) % bias_rem, ps), ps);

	KUNIT_ASSERT_FALSE_MSG(test,
			       gpu_buddy_alloc_blocks(&mm, bias_start,
						      bias_end, size, ps,
						      &allocated,
						      flags),
			       "buddy_alloc failed with bias(%x-%x), size=%u, ps=%u\n",
			       bias_start, bias_end, size, ps);

	list_for_each_entry(block, &allocated, link)
		KUNIT_EXPECT_EQ(test, gpu_buddy_block_is_clear(block), false);

	gpu_buddy_free_list(&mm, &allocated, 0);
	gpu_buddy_fini(&mm);
}

static void gpu_test_buddy_alloc_range(struct kunit *test)
{
	GPU_RND_STATE(prng, random_seed);
	struct gpu_buddy_block *block;
	struct gpu_buddy mm;
	u32 mm_size, total;
	LIST_HEAD(blocks);
	LIST_HEAD(tmp);
	u32 ps = SZ_4K;
	int ret;

	mm_size = SZ_16M;

	KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_init(&mm, mm_size, ps),
			       "buddy_init failed\n");

	/*
	 * Basic exact-range allocation.
	 * Allocate the entire mm as one exact range (start + size == end).
	 * This is the simplest case exercising __gpu_buddy_alloc_range.
	 */
	ret = gpu_buddy_alloc_blocks(&mm, 0, mm_size, mm_size, ps, &blocks, 0);
	KUNIT_ASSERT_EQ_MSG(test, ret, 0,
			    "exact-range alloc of full mm failed\n");

	total = 0;
	list_for_each_entry(block, &blocks, link) {
		u64 offset = gpu_buddy_block_offset(block);
		u64 bsize = gpu_buddy_block_size(&mm, block);

		KUNIT_EXPECT_TRUE_MSG(test, offset + bsize <= (u64)mm_size,
				      "block [%llx, %llx) outside mm\n", offset, offset + bsize);
		total += (u32)bsize;
	}
	KUNIT_EXPECT_EQ(test, total, mm_size);
	KUNIT_EXPECT_EQ(test, mm.avail, 0ULL);

	/* Full mm should be exhausted */
	ret = gpu_buddy_alloc_blocks(&mm, 0, ps, ps, ps, &tmp, 0);
	KUNIT_EXPECT_NE_MSG(test, ret, 0, "alloc should fail when mm is full\n");

	gpu_buddy_free_list(&mm, &blocks, 0);
	KUNIT_EXPECT_EQ(test, mm.avail, (u64)mm_size);
	gpu_buddy_fini(&mm);

	/*
	 * Exact-range allocation of sub-ranges.
	 * Split the mm into four equal quarters and allocate each as an exact
	 * range. Validates splitting and non-overlapping exact allocations.
	 */
	KUNIT_ASSERT_FALSE(test, gpu_buddy_init(&mm, mm_size, ps));

	{
		u32 quarter = mm_size / 4;
		int i;

		for (i = 0; i < 4; i++) {
			u32 start = i * quarter;
			u32 end = start + quarter;

			ret = gpu_buddy_alloc_blocks(&mm, start, end, quarter, ps, &blocks, 0);
			KUNIT_ASSERT_EQ_MSG(test, ret, 0,
					    "exact-range alloc quarter %d [%x, %x) failed\n",
					    i, start, end);
		}
		KUNIT_EXPECT_EQ(test, mm.avail, 0ULL);
		gpu_buddy_free_list(&mm, &blocks, 0);
	}

	gpu_buddy_fini(&mm);

	/*
	 * Minimum chunk-size exact range at various offsets.
	 * Allocate single-page exact ranges at the start, middle and end.
	 */
	KUNIT_ASSERT_FALSE(test, gpu_buddy_init(&mm, mm_size, ps));

	ret = gpu_buddy_alloc_blocks(&mm, 0, ps, ps, ps, &blocks, 0);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = gpu_buddy_alloc_blocks(&mm, mm_size / 2, mm_size / 2 + ps, ps, ps, &blocks, 0);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = gpu_buddy_alloc_blocks(&mm, mm_size - ps, mm_size, ps, ps, &blocks, 0);
	KUNIT_ASSERT_EQ(test, ret, 0);

	total = 0;
	list_for_each_entry(block, &blocks, link)
		total += (u32)gpu_buddy_block_size(&mm, block);
	KUNIT_EXPECT_EQ(test, total, 3 * ps);

	gpu_buddy_free_list(&mm, &blocks, 0);
	gpu_buddy_fini(&mm);

	/*
	 * Non power-of-two mm size (multiple roots).
	 * Exact-range allocations that span root boundaries must still work.
	 */
	mm_size = SZ_4M + SZ_2M + SZ_1M; /* 7 MiB, three roots */

	KUNIT_ASSERT_FALSE(test, gpu_buddy_init(&mm, mm_size, ps));
	KUNIT_EXPECT_GT(test, mm.n_roots, 1U);

	/* Allocate first 4M root exactly */
	ret = gpu_buddy_alloc_blocks(&mm, 0, SZ_4M, SZ_4M, ps, &blocks, 0);
	KUNIT_ASSERT_EQ(test, ret, 0);

	/* Allocate second root (4M-6M) exactly */
	ret = gpu_buddy_alloc_blocks(&mm, SZ_4M, SZ_4M + SZ_2M, SZ_2M, ps, &blocks, 0);
	KUNIT_ASSERT_EQ(test, ret, 0);

	/* Allocate third root (6M-7M) exactly */
	ret = gpu_buddy_alloc_blocks(&mm, SZ_4M + SZ_2M, mm_size, SZ_1M, ps, &blocks, 0);
	KUNIT_ASSERT_EQ(test, ret, 0);

	KUNIT_EXPECT_EQ(test, mm.avail, 0ULL);
	gpu_buddy_free_list(&mm, &blocks, 0);

	/* Cross-root exact-range: the entire non-pot mm */
	ret = gpu_buddy_alloc_blocks(&mm, 0, mm_size, mm_size, ps, &blocks, 0);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, mm.avail, 0ULL);

	gpu_buddy_free_list(&mm, &blocks, 0);
	gpu_buddy_fini(&mm);

	/*
	 * Randomized exact-range allocations.
	 * Divide the mm into N random-sized, contiguous, page-aligned slices
	 * and allocate each as an exact range in random order.
	 */
	mm_size = SZ_16M;
	KUNIT_ASSERT_FALSE(test, gpu_buddy_init(&mm, mm_size, ps));

	{
#define N_RAND_RANGES 16
		u32 ranges[N_RAND_RANGES + 1]; /* boundaries */
		u32 order_arr[N_RAND_RANGES];
		u32 remaining = mm_size;
		int i;

		ranges[0] = 0;
		for (i = 0; i < N_RAND_RANGES - 1; i++) {
			u32 max_chunk = remaining - (N_RAND_RANGES - 1 - i) * ps;
			u32 sz = max(round_up(prandom_u32_state(&prng) % max_chunk, ps), ps);

			ranges[i + 1] = ranges[i] + sz;
			remaining -= sz;
		}
		ranges[N_RAND_RANGES] = mm_size;

		/* Create a random order */
		for (i = 0; i < N_RAND_RANGES; i++)
			order_arr[i] = i;
		for (i = N_RAND_RANGES - 1; i > 0; i--) {
			u32 j = prandom_u32_state(&prng) % (i + 1);
			u32 tmp_val = order_arr[i];

			order_arr[i] = order_arr[j];
			order_arr[j] = tmp_val;
		}

		for (i = 0; i < N_RAND_RANGES; i++) {
			u32 idx = order_arr[i];
			u32 start = ranges[idx];
			u32 end = ranges[idx + 1];
			u32 sz = end - start;

			ret = gpu_buddy_alloc_blocks(&mm, start, end, sz, ps, &blocks, 0);
			KUNIT_ASSERT_EQ_MSG(test, ret, 0,
					    "random exact-range [%x, %x) sz=%x failed\n",
					    start, end, sz);
		}

		KUNIT_EXPECT_EQ(test, mm.avail, 0ULL);
		gpu_buddy_free_list(&mm, &blocks, 0);
#undef N_RAND_RANGES
	}

	gpu_buddy_fini(&mm);

	/*
	 * Negative case - partially allocated range.
	 * Allocate the first half, then try to exact-range allocate the full
	 * mm. This must fail because the first half is already occupied.
	 */
	mm_size = SZ_16M;
	KUNIT_ASSERT_FALSE(test, gpu_buddy_init(&mm, mm_size, ps));

	ret = gpu_buddy_alloc_blocks(&mm, 0, mm_size / 2, mm_size / 2, ps, &blocks, 0);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = gpu_buddy_alloc_blocks(&mm, 0, mm_size, mm_size, ps, &tmp, 0);
	KUNIT_EXPECT_NE_MSG(test, ret, 0,
			    "exact-range alloc should fail when range is partially used\n");

	/* Also try the already-occupied sub-range directly */
	ret = gpu_buddy_alloc_blocks(&mm, 0, mm_size / 2, mm_size / 2, ps, &tmp, 0);
	KUNIT_EXPECT_NE_MSG(test, ret, 0,
			    "double alloc of same exact range should fail\n");

	/* The free second half should still be allocatable */
	ret = gpu_buddy_alloc_blocks(&mm, mm_size / 2, mm_size, mm_size / 2, ps, &blocks, 0);
	KUNIT_ASSERT_EQ(test, ret, 0);

	KUNIT_EXPECT_EQ(test, mm.avail, 0ULL);
	gpu_buddy_free_list(&mm, &blocks, 0);
	gpu_buddy_fini(&mm);

	/*
	 * Negative case - checkerboard partial allocation.
	 * Allocate every other page-sized chunk in a small mm, then try to
	 * exact-range allocate a range covering two pages (one allocated, one
	 * free). This must fail.
	 */
	mm_size = SZ_64K;
	KUNIT_ASSERT_FALSE(test, gpu_buddy_init(&mm, mm_size, ps));

	{
		u32 off;

		for (off = 0; off < mm_size; off += 2 * ps) {
			ret = gpu_buddy_alloc_blocks(&mm, off, off + ps, ps, ps, &blocks, 0);
			KUNIT_ASSERT_EQ(test, ret, 0);
		}

		/* Try exact range over a pair [allocated, free] */
		ret = gpu_buddy_alloc_blocks(&mm, 0, 2 * ps, 2 * ps, ps, &tmp, 0);
		KUNIT_EXPECT_NE_MSG(test, ret, 0,
				    "exact-range over partially allocated pair should fail\n");

		/* The free pages individually should still work */
		ret = gpu_buddy_alloc_blocks(&mm, ps, 2 * ps, ps, ps, &blocks, 0);
		KUNIT_ASSERT_EQ(test, ret, 0);

		gpu_buddy_free_list(&mm, &blocks, 0);
	}

	gpu_buddy_fini(&mm);

	/* Negative case - misaligned start/end/size */
	mm_size = SZ_16M;
	KUNIT_ASSERT_FALSE(test, gpu_buddy_init(&mm, mm_size, ps));

	/* start not aligned to chunk_size */
	ret = gpu_buddy_alloc_blocks(&mm, ps / 2, ps / 2 + ps, ps, ps, &tmp, 0);
	KUNIT_EXPECT_NE(test, ret, 0);

	/* size not aligned */
	ret = gpu_buddy_alloc_blocks(&mm, 0, ps + 1, ps + 1, ps, &tmp, 0);
	KUNIT_EXPECT_NE(test, ret, 0);

	/* end exceeds mm size */
	ret = gpu_buddy_alloc_blocks(&mm, mm_size, mm_size + ps, ps, ps, &tmp, 0);
	KUNIT_EXPECT_NE(test, ret, 0);

	gpu_buddy_fini(&mm);

	/*
	 * Free and re-allocate the same exact range.
	 * This exercises merge-on-free followed by exact-range re-split.
	 */
	mm_size = SZ_16M;
	KUNIT_ASSERT_FALSE(test, gpu_buddy_init(&mm, mm_size, ps));

	{
		int i;

		for (i = 0; i < 5; i++) {
			ret = gpu_buddy_alloc_blocks(&mm, SZ_4M, SZ_4M + SZ_2M,
						     SZ_2M, ps, &blocks, 0);
			KUNIT_ASSERT_EQ_MSG(test, ret, 0,
					    "re-alloc iteration %d failed\n", i);

			total = 0;
			list_for_each_entry(block, &blocks, link) {
				u64 offset = gpu_buddy_block_offset(block);
				u64 bsize = gpu_buddy_block_size(&mm, block);

				KUNIT_EXPECT_GE(test, offset, (u64)SZ_4M);
				KUNIT_EXPECT_LE(test, offset + bsize, (u64)(SZ_4M + SZ_2M));
				total += (u32)bsize;
			}
			KUNIT_EXPECT_EQ(test, total, SZ_2M);

			gpu_buddy_free_list(&mm, &blocks, 0);
		}

		KUNIT_EXPECT_EQ(test, mm.avail, (u64)mm_size);
	}

	gpu_buddy_fini(&mm);

	/*
	 * Various power-of-two exact ranges within a large mm.
	 * Allocate non-overlapping power-of-two exact ranges at their natural
	 * alignment, validating that the allocator handles different orders.
	 */
	mm_size = SZ_16M;
	KUNIT_ASSERT_FALSE(test, gpu_buddy_init(&mm, mm_size, ps));

	/* Allocate 4K at offset 0 */
	ret = gpu_buddy_alloc_blocks(&mm, 0, SZ_4K, SZ_4K, ps, &blocks, 0);
	KUNIT_ASSERT_EQ(test, ret, 0);

	/* Allocate 64K at offset 64K */
	ret = gpu_buddy_alloc_blocks(&mm, SZ_64K, SZ_64K + SZ_64K, SZ_64K, ps, &blocks, 0);
	KUNIT_ASSERT_EQ(test, ret, 0);

	/* Allocate 1M at offset 1M */
	ret = gpu_buddy_alloc_blocks(&mm, SZ_1M, SZ_1M + SZ_1M, SZ_1M, ps, &blocks, 0);
	KUNIT_ASSERT_EQ(test, ret, 0);

	/* Allocate 4M at offset 4M */
	ret = gpu_buddy_alloc_blocks(&mm, SZ_4M, SZ_4M + SZ_4M, SZ_4M, ps, &blocks, 0);
	KUNIT_ASSERT_EQ(test, ret, 0);

	total = 0;
	list_for_each_entry(block, &blocks, link)
		total += (u32)gpu_buddy_block_size(&mm, block);
	KUNIT_EXPECT_EQ(test, total, SZ_4K + SZ_64K + SZ_1M + SZ_4M);

	gpu_buddy_free_list(&mm, &blocks, 0);
	gpu_buddy_fini(&mm);
}

static void gpu_test_buddy_alloc_clear(struct kunit *test)
{
	unsigned long n_pages, total, i = 0;
	const unsigned long ps = SZ_4K;
	struct gpu_buddy_block *block;
	const int max_order = 12;
	LIST_HEAD(allocated);
	struct gpu_buddy mm;
	unsigned int order;
	u32 mm_size, size;
	LIST_HEAD(dirty);
	LIST_HEAD(clean);

	mm_size = SZ_4K << max_order;
	KUNIT_EXPECT_FALSE(test, gpu_buddy_init(&mm, mm_size, ps));

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
	KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_alloc_blocks(&mm, 0, mm_size,
							    5 * ps, ps, &allocated,
							    GPU_BUDDY_TOPDOWN_ALLOCATION),
				"buddy_alloc hit an error size=%lu\n", 5 * ps);
	gpu_buddy_free_list(&mm, &allocated, GPU_BUDDY_CLEARED);

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
			flags = GPU_BUDDY_CLEAR_ALLOCATION;
		}

		KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_alloc_blocks(&mm, 0, mm_size,
								    ps, ps, list,
								    flags),
					"buddy_alloc hit an error size=%lu\n", ps);
	} while (++i < n_pages);

	list_for_each_entry(block, &clean, link)
		KUNIT_EXPECT_EQ(test, gpu_buddy_block_is_clear(block), true);

	list_for_each_entry(block, &dirty, link)
		KUNIT_EXPECT_EQ(test, gpu_buddy_block_is_clear(block), false);

	gpu_buddy_free_list(&mm, &clean, GPU_BUDDY_CLEARED);

	/*
	 * Trying to go over the clear limit for some allocation.
	 * The allocation should never fail with reasonable page-size.
	 */
	KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_alloc_blocks(&mm, 0, mm_size,
							    10 * ps, ps, &clean,
							    GPU_BUDDY_CLEAR_ALLOCATION),
				"buddy_alloc hit an error size=%lu\n", 10 * ps);

	gpu_buddy_free_list(&mm, &clean, GPU_BUDDY_CLEARED);
	gpu_buddy_free_list(&mm, &dirty, 0);
	gpu_buddy_fini(&mm);

	KUNIT_EXPECT_FALSE(test, gpu_buddy_init(&mm, mm_size, ps));

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

		KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_alloc_blocks(&mm, 0, mm_size,
								    ps, ps, list, 0),
					"buddy_alloc hit an error size=%lu\n", ps);
	} while (++i < n_pages);

	gpu_buddy_free_list(&mm, &clean, GPU_BUDDY_CLEARED);
	gpu_buddy_free_list(&mm, &dirty, 0);

	order = 1;
	do {
		size = SZ_4K << order;

		KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_alloc_blocks(&mm, 0, mm_size,
								    size, size, &allocated,
								    GPU_BUDDY_CLEAR_ALLOCATION),
					"buddy_alloc hit an error size=%u\n", size);
		total = 0;
		list_for_each_entry(block, &allocated, link) {
			if (size != mm_size)
				KUNIT_EXPECT_EQ(test, gpu_buddy_block_is_clear(block), false);
			total += gpu_buddy_block_size(&mm, block);
		}
		KUNIT_EXPECT_EQ(test, total, size);

		gpu_buddy_free_list(&mm, &allocated, 0);
	} while (++order <= max_order);

	gpu_buddy_fini(&mm);

	/*
	 * Create a new mm with a non power-of-two size. Allocate a random size from each
	 * root, free as cleared and then call fini. This will ensure the multi-root
	 * force merge during fini.
	 */
	mm_size = (SZ_4K << max_order) + (SZ_4K << (max_order - 2));

	KUNIT_EXPECT_FALSE(test, gpu_buddy_init(&mm, mm_size, ps));
	KUNIT_EXPECT_EQ(test, mm.max_order, max_order);
	KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_alloc_blocks(&mm, 0, SZ_4K << max_order,
							    4 * ps, ps, &allocated,
							    GPU_BUDDY_RANGE_ALLOCATION),
				"buddy_alloc hit an error size=%lu\n", 4 * ps);
	gpu_buddy_free_list(&mm, &allocated, GPU_BUDDY_CLEARED);
	KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_alloc_blocks(&mm, 0, SZ_4K << max_order,
							    2 * ps, ps, &allocated,
							    GPU_BUDDY_CLEAR_ALLOCATION),
				"buddy_alloc hit an error size=%lu\n", 2 * ps);
	gpu_buddy_free_list(&mm, &allocated, GPU_BUDDY_CLEARED);
	KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_alloc_blocks(&mm, SZ_4K << max_order, mm_size,
							    ps, ps, &allocated,
							    GPU_BUDDY_RANGE_ALLOCATION),
				"buddy_alloc hit an error size=%lu\n", ps);
	gpu_buddy_free_list(&mm, &allocated, GPU_BUDDY_CLEARED);
	gpu_buddy_fini(&mm);
}

static void gpu_test_buddy_alloc_contiguous(struct kunit *test)
{
	const unsigned long ps = SZ_4K, mm_size = 16 * 3 * SZ_4K;
	unsigned long i, n_pages, total;
	struct gpu_buddy_block *block;
	struct gpu_buddy mm;
	LIST_HEAD(left);
	LIST_HEAD(middle);
	LIST_HEAD(right);
	LIST_HEAD(allocated);

	KUNIT_EXPECT_FALSE(test, gpu_buddy_init(&mm, mm_size, ps));

	/*
	 * Idea is to fragment the address space by alternating block
	 * allocations between three different lists; one for left, middle and
	 * right. We can then free a list to simulate fragmentation. In
	 * particular we want to exercise the GPU_BUDDY_CONTIGUOUS_ALLOCATION,
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
				       gpu_buddy_alloc_blocks(&mm, 0, mm_size,
							      ps, ps, list, 0),
				       "buddy_alloc hit an error size=%lu\n",
				       ps);
	} while (++i < n_pages);

	KUNIT_ASSERT_TRUE_MSG(test, gpu_buddy_alloc_blocks(&mm, 0, mm_size,
							   3 * ps, ps, &allocated,
							   GPU_BUDDY_CONTIGUOUS_ALLOCATION),
			       "buddy_alloc didn't error size=%lu\n", 3 * ps);

	gpu_buddy_free_list(&mm, &middle, 0);
	KUNIT_ASSERT_TRUE_MSG(test, gpu_buddy_alloc_blocks(&mm, 0, mm_size,
							   3 * ps, ps, &allocated,
							   GPU_BUDDY_CONTIGUOUS_ALLOCATION),
			       "buddy_alloc didn't error size=%lu\n", 3 * ps);
	KUNIT_ASSERT_TRUE_MSG(test, gpu_buddy_alloc_blocks(&mm, 0, mm_size,
							   2 * ps, ps, &allocated,
							   GPU_BUDDY_CONTIGUOUS_ALLOCATION),
			       "buddy_alloc didn't error size=%lu\n", 2 * ps);

	gpu_buddy_free_list(&mm, &right, 0);
	KUNIT_ASSERT_TRUE_MSG(test, gpu_buddy_alloc_blocks(&mm, 0, mm_size,
							   3 * ps, ps, &allocated,
							   GPU_BUDDY_CONTIGUOUS_ALLOCATION),
			       "buddy_alloc didn't error size=%lu\n", 3 * ps);
	/*
	 * At this point we should have enough contiguous space for 2 blocks,
	 * however they are never buddies (since we freed middle and right) so
	 * will require the try_harder logic to find them.
	 */
	KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_alloc_blocks(&mm, 0, mm_size,
							    2 * ps, ps, &allocated,
							    GPU_BUDDY_CONTIGUOUS_ALLOCATION),
			       "buddy_alloc hit an error size=%lu\n", 2 * ps);

	gpu_buddy_free_list(&mm, &left, 0);
	KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_alloc_blocks(&mm, 0, mm_size,
							    3 * ps, ps, &allocated,
							    GPU_BUDDY_CONTIGUOUS_ALLOCATION),
			       "buddy_alloc hit an error size=%lu\n", 3 * ps);

	total = 0;
	list_for_each_entry(block, &allocated, link)
		total += gpu_buddy_block_size(&mm, block);

	KUNIT_ASSERT_EQ(test, total, ps * 2 + ps * 3);

	gpu_buddy_free_list(&mm, &allocated, 0);
	gpu_buddy_fini(&mm);
}

static void gpu_test_buddy_alloc_pathological(struct kunit *test)
{
	u64 mm_size, size, start = 0;
	struct gpu_buddy_block *block;
	const int max_order = 3;
	unsigned long flags = 0;
	int order, top;
	struct gpu_buddy mm;
	LIST_HEAD(blocks);
	LIST_HEAD(holes);
	LIST_HEAD(tmp);

	/*
	 * Create a pot-sized mm, then allocate one of each possible
	 * order within. This should leave the mm with exactly one
	 * page left. Free the largest block, then whittle down again.
	 * Eventually we will have a fully 50% fragmented mm.
	 */

	mm_size = SZ_4K << max_order;
	KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_init(&mm, mm_size, SZ_4K),
			       "buddy_init failed\n");

	KUNIT_EXPECT_EQ(test, mm.max_order, max_order);

	for (top = max_order; top; top--) {
		/* Make room by freeing the largest allocated block */
		block = list_first_entry_or_null(&blocks, typeof(*block), link);
		if (block) {
			list_del(&block->link);
			gpu_buddy_free_block(&mm, block);
		}

		for (order = top; order--;) {
			size = get_size(order, mm.chunk_size);
			KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_alloc_blocks(&mm, start,
									    mm_size, size, size,
										&tmp, flags),
					"buddy_alloc hit -ENOMEM with order=%d, top=%d\n",
					order, top);

			block = list_first_entry_or_null(&tmp, struct gpu_buddy_block, link);
			KUNIT_ASSERT_TRUE_MSG(test, block, "alloc_blocks has no blocks\n");

			list_move_tail(&block->link, &blocks);
		}

		/* There should be one final page for this sub-allocation */
		size = get_size(0, mm.chunk_size);
		KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_alloc_blocks(&mm, start, mm_size,
								    size, size, &tmp, flags),
							   "buddy_alloc hit -ENOMEM for hole\n");

		block = list_first_entry_or_null(&tmp, struct gpu_buddy_block, link);
		KUNIT_ASSERT_TRUE_MSG(test, block, "alloc_blocks has no blocks\n");

		list_move_tail(&block->link, &holes);

		size = get_size(top, mm.chunk_size);
		KUNIT_ASSERT_TRUE_MSG(test, gpu_buddy_alloc_blocks(&mm, start, mm_size,
								   size, size, &tmp, flags),
							  "buddy_alloc unexpectedly succeeded at top-order %d/%d, it should be full!",
							  top, max_order);
	}

	gpu_buddy_free_list(&mm, &holes, 0);

	/* Nothing larger than blocks of chunk_size now available */
	for (order = 1; order <= max_order; order++) {
		size = get_size(order, mm.chunk_size);
		KUNIT_ASSERT_TRUE_MSG(test, gpu_buddy_alloc_blocks(&mm, start, mm_size,
								   size, size, &tmp, flags),
							  "buddy_alloc unexpectedly succeeded at order %d, it should be full!",
							  order);
	}

	list_splice_tail(&holes, &blocks);
	gpu_buddy_free_list(&mm, &blocks, 0);
	gpu_buddy_fini(&mm);
}

static void gpu_test_buddy_alloc_pessimistic(struct kunit *test)
{
	u64 mm_size, size, start = 0;
	struct gpu_buddy_block *block, *bn;
	const unsigned int max_order = 16;
	unsigned long flags = 0;
	struct gpu_buddy mm;
	unsigned int order;
	LIST_HEAD(blocks);
	LIST_HEAD(tmp);

	/*
	 * Create a pot-sized mm, then allocate one of each possible
	 * order within. This should leave the mm with exactly one
	 * page left.
	 */

	mm_size = SZ_4K << max_order;
	KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_init(&mm, mm_size, SZ_4K),
			       "buddy_init failed\n");

	KUNIT_EXPECT_EQ(test, mm.max_order, max_order);

	for (order = 0; order < max_order; order++) {
		size = get_size(order, mm.chunk_size);
		KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_alloc_blocks(&mm, start, mm_size,
								    size, size, &tmp, flags),
							   "buddy_alloc hit -ENOMEM with order=%d\n",
							   order);

		block = list_first_entry_or_null(&tmp, struct gpu_buddy_block, link);
		KUNIT_ASSERT_TRUE_MSG(test, block, "alloc_blocks has no blocks\n");

		list_move_tail(&block->link, &blocks);
	}

	/* And now the last remaining block available */
	size = get_size(0, mm.chunk_size);
	KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_alloc_blocks(&mm, start, mm_size,
							    size, size, &tmp, flags),
						   "buddy_alloc hit -ENOMEM on final alloc\n");

	block = list_first_entry_or_null(&tmp, struct gpu_buddy_block, link);
	KUNIT_ASSERT_TRUE_MSG(test, block, "alloc_blocks has no blocks\n");

	list_move_tail(&block->link, &blocks);

	/* Should be completely full! */
	for (order = max_order; order--;) {
		size = get_size(order, mm.chunk_size);
		KUNIT_ASSERT_TRUE_MSG(test, gpu_buddy_alloc_blocks(&mm, start, mm_size,
								   size, size, &tmp, flags),
							  "buddy_alloc unexpectedly succeeded, it should be full!");
	}

	block = list_last_entry(&blocks, typeof(*block), link);
	list_del(&block->link);
	gpu_buddy_free_block(&mm, block);

	/* As we free in increasing size, we make available larger blocks */
	order = 1;
	list_for_each_entry_safe(block, bn, &blocks, link) {
		list_del(&block->link);
		gpu_buddy_free_block(&mm, block);

		size = get_size(order, mm.chunk_size);
		KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_alloc_blocks(&mm, start, mm_size,
								    size, size, &tmp, flags),
							   "buddy_alloc hit -ENOMEM with order=%d\n",
							   order);

		block = list_first_entry_or_null(&tmp, struct gpu_buddy_block, link);
		KUNIT_ASSERT_TRUE_MSG(test, block, "alloc_blocks has no blocks\n");

		list_del(&block->link);
		gpu_buddy_free_block(&mm, block);
		order++;
	}

	/* To confirm, now the whole mm should be available */
	size = get_size(max_order, mm.chunk_size);
	KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_alloc_blocks(&mm, start, mm_size,
							    size, size, &tmp, flags),
						   "buddy_alloc (realloc) hit -ENOMEM with order=%d\n",
						   max_order);

	block = list_first_entry_or_null(&tmp, struct gpu_buddy_block, link);
	KUNIT_ASSERT_TRUE_MSG(test, block, "alloc_blocks has no blocks\n");

	list_del(&block->link);
	gpu_buddy_free_block(&mm, block);
	gpu_buddy_free_list(&mm, &blocks, 0);
	gpu_buddy_fini(&mm);
}

static void gpu_test_buddy_alloc_optimistic(struct kunit *test)
{
	u64 mm_size, size, start = 0;
	struct gpu_buddy_block *block;
	unsigned long flags = 0;
	const int max_order = 16;
	struct gpu_buddy mm;
	LIST_HEAD(blocks);
	LIST_HEAD(tmp);
	int order;

	/*
	 * Create a mm with one block of each order available, and
	 * try to allocate them all.
	 */

	mm_size = SZ_4K * ((1 << (max_order + 1)) - 1);

	KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_init(&mm, mm_size, SZ_4K),
			       "buddy_init failed\n");

	KUNIT_EXPECT_EQ(test, mm.max_order, max_order);

	for (order = 0; order <= max_order; order++) {
		size = get_size(order, mm.chunk_size);
		KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_alloc_blocks(&mm, start, mm_size,
								    size, size, &tmp, flags),
							   "buddy_alloc hit -ENOMEM with order=%d\n",
							   order);

		block = list_first_entry_or_null(&tmp, struct gpu_buddy_block, link);
		KUNIT_ASSERT_TRUE_MSG(test, block, "alloc_blocks has no blocks\n");

		list_move_tail(&block->link, &blocks);
	}

	/* Should be completely full! */
	size = get_size(0, mm.chunk_size);
	KUNIT_ASSERT_TRUE_MSG(test, gpu_buddy_alloc_blocks(&mm, start, mm_size,
							   size, size, &tmp, flags),
						  "buddy_alloc unexpectedly succeeded, it should be full!");

	gpu_buddy_free_list(&mm, &blocks, 0);
	gpu_buddy_fini(&mm);
}

static void gpu_test_buddy_alloc_limit(struct kunit *test)
{
	u64 size = U64_MAX, start = 0;
	struct gpu_buddy_block *block;
	unsigned long flags = 0;
	LIST_HEAD(allocated);
	struct gpu_buddy mm;

	KUNIT_EXPECT_FALSE(test, gpu_buddy_init(&mm, size, SZ_4K));

	KUNIT_EXPECT_EQ_MSG(test, mm.max_order, GPU_BUDDY_MAX_ORDER,
			    "mm.max_order(%d) != %d\n", mm.max_order,
						GPU_BUDDY_MAX_ORDER);

	size = mm.chunk_size << mm.max_order;
	KUNIT_EXPECT_FALSE(test, gpu_buddy_alloc_blocks(&mm, start, size, size,
							mm.chunk_size, &allocated, flags));

	block = list_first_entry_or_null(&allocated, struct gpu_buddy_block, link);
	KUNIT_EXPECT_TRUE(test, block);

	KUNIT_EXPECT_EQ_MSG(test, gpu_buddy_block_order(block), mm.max_order,
			    "block order(%d) != %d\n",
						gpu_buddy_block_order(block), mm.max_order);

	KUNIT_EXPECT_EQ_MSG(test, gpu_buddy_block_size(&mm, block),
			    BIT_ULL(mm.max_order) * mm.chunk_size,
						"block size(%llu) != %llu\n",
						gpu_buddy_block_size(&mm, block),
						BIT_ULL(mm.max_order) * mm.chunk_size);

	gpu_buddy_free_list(&mm, &allocated, 0);
	gpu_buddy_fini(&mm);
}

static void gpu_test_buddy_alloc_exceeds_max_order(struct kunit *test)
{
	u64 mm_size = SZ_8G + SZ_2G, size = SZ_8G + SZ_1G, min_block_size = SZ_8G;
	struct gpu_buddy mm;
	LIST_HEAD(blocks);
	int err;

	KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_init(&mm, mm_size, SZ_4K),
			       "buddy_init failed\n");

	/* CONTIGUOUS allocation should succeed via try_harder fallback */
	KUNIT_ASSERT_FALSE_MSG(test, gpu_buddy_alloc_blocks(&mm, 0, mm_size, size,
							    SZ_4K, &blocks,
							    GPU_BUDDY_CONTIGUOUS_ALLOCATION),
			       "buddy_alloc hit an error size=%llu\n", size);
	gpu_buddy_free_list(&mm, &blocks, 0);

	/* Non-CONTIGUOUS with large min_block_size should return -EINVAL */
	err = gpu_buddy_alloc_blocks(&mm, 0, mm_size, size, min_block_size, &blocks, 0);
	KUNIT_EXPECT_EQ(test, err, -EINVAL);

	/* Non-CONTIGUOUS + RANGE with large min_block_size should return -EINVAL */
	err = gpu_buddy_alloc_blocks(&mm, 0, mm_size, size, min_block_size, &blocks,
				     GPU_BUDDY_RANGE_ALLOCATION);
	KUNIT_EXPECT_EQ(test, err, -EINVAL);

	/* CONTIGUOUS + RANGE should return -EINVAL (no try_harder for RANGE) */
	err = gpu_buddy_alloc_blocks(&mm, 0, mm_size, size, SZ_4K, &blocks,
				     GPU_BUDDY_CONTIGUOUS_ALLOCATION | GPU_BUDDY_RANGE_ALLOCATION);
	KUNIT_EXPECT_EQ(test, err, -EINVAL);

	gpu_buddy_fini(&mm);
}

static int gpu_buddy_suite_init(struct kunit_suite *suite)
{
	while (!random_seed)
		random_seed = get_random_u32();

	kunit_info(suite, "Testing GPU buddy manager, with random_seed=0x%x\n",
		   random_seed);

	return 0;
}

static struct kunit_case gpu_buddy_tests[] = {
	KUNIT_CASE(gpu_test_buddy_alloc_limit),
	KUNIT_CASE(gpu_test_buddy_alloc_optimistic),
	KUNIT_CASE(gpu_test_buddy_alloc_pessimistic),
	KUNIT_CASE(gpu_test_buddy_alloc_pathological),
	KUNIT_CASE(gpu_test_buddy_alloc_contiguous),
	KUNIT_CASE(gpu_test_buddy_alloc_clear),
	KUNIT_CASE(gpu_test_buddy_alloc_range),
	KUNIT_CASE(gpu_test_buddy_alloc_range_bias),
	KUNIT_CASE_SLOW(gpu_test_buddy_fragmentation_performance),
	KUNIT_CASE(gpu_test_buddy_alloc_exceeds_max_order),
	KUNIT_CASE(gpu_test_buddy_offset_aligned_allocation),
	KUNIT_CASE(gpu_test_buddy_subtree_offset_alignment_stress),
	{}
};

static struct kunit_suite gpu_buddy_test_suite = {
	.name = "gpu_buddy",
	.suite_init = gpu_buddy_suite_init,
	.test_cases = gpu_buddy_tests,
};

kunit_test_suite(gpu_buddy_test_suite);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Kunit test for gpu_buddy functions");
MODULE_LICENSE("GPL");
