// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 * Copyright © 2022 Maíra Canal <mairacanal@riseup.net>
 */

#include <kunit/test.h>

#include <linux/prime_numbers.h>
#include <linux/sched/signal.h>

#include <drm/drm_buddy.h>

#include "../lib/drm_random.h"

#define TIMEOUT(name__)								\
	unsigned long name__ = jiffies + MAX_SCHEDULE_TIMEOUT

static unsigned int random_seed;

static inline u64 get_size(int order, u64 chunk_size)
{
	return (1 << order) * chunk_size;
}

__printf(2, 3)
static bool __timeout(unsigned long timeout, const char *fmt, ...)
{
	va_list va;

	if (!signal_pending(current)) {
		cond_resched();
		if (time_before(jiffies, timeout))
			return false;
	}

	if (fmt) {
		va_start(va, fmt);
		vprintk(fmt, va);
		va_end(va);
	}

	return true;
}

static void __dump_block(struct kunit *test, struct drm_buddy *mm,
			 struct drm_buddy_block *block, bool buddy)
{
	kunit_err(test, "block info: header=%llx, state=%u, order=%d, offset=%llx size=%llx root=%d buddy=%d\n",
		  block->header, drm_buddy_block_state(block),
			  drm_buddy_block_order(block), drm_buddy_block_offset(block),
			  drm_buddy_block_size(mm, block), !block->parent, buddy);
}

static void dump_block(struct kunit *test, struct drm_buddy *mm,
		       struct drm_buddy_block *block)
{
	struct drm_buddy_block *buddy;

	__dump_block(test, mm, block, false);

	buddy = drm_get_buddy(block);
	if (buddy)
		__dump_block(test, mm, buddy, true);
}

static int check_block(struct kunit *test, struct drm_buddy *mm,
		       struct drm_buddy_block *block)
{
	struct drm_buddy_block *buddy;
	unsigned int block_state;
	u64 block_size;
	u64 offset;
	int err = 0;

	block_state = drm_buddy_block_state(block);

	if (block_state != DRM_BUDDY_ALLOCATED &&
	    block_state != DRM_BUDDY_FREE && block_state != DRM_BUDDY_SPLIT) {
		kunit_err(test, "block state mismatch\n");
		err = -EINVAL;
	}

	block_size = drm_buddy_block_size(mm, block);
	offset = drm_buddy_block_offset(block);

	if (block_size < mm->chunk_size) {
		kunit_err(test, "block size smaller than min size\n");
		err = -EINVAL;
	}

	/* We can't use is_power_of_2() for a u64 on 32-bit systems. */
	if (block_size & (block_size - 1)) {
		kunit_err(test, "block size not power of two\n");
		err = -EINVAL;
	}

	if (!IS_ALIGNED(block_size, mm->chunk_size)) {
		kunit_err(test, "block size not aligned to min size\n");
		err = -EINVAL;
	}

	if (!IS_ALIGNED(offset, mm->chunk_size)) {
		kunit_err(test, "block offset not aligned to min size\n");
		err = -EINVAL;
	}

	if (!IS_ALIGNED(offset, block_size)) {
		kunit_err(test, "block offset not aligned to block size\n");
		err = -EINVAL;
	}

	buddy = drm_get_buddy(block);

	if (!buddy && block->parent) {
		kunit_err(test, "buddy has gone fishing\n");
		err = -EINVAL;
	}

	if (buddy) {
		if (drm_buddy_block_offset(buddy) != (offset ^ block_size)) {
			kunit_err(test, "buddy has wrong offset\n");
			err = -EINVAL;
		}

		if (drm_buddy_block_size(mm, buddy) != block_size) {
			kunit_err(test, "buddy size mismatch\n");
			err = -EINVAL;
		}

		if (drm_buddy_block_state(buddy) == block_state &&
		    block_state == DRM_BUDDY_FREE) {
			kunit_err(test, "block and its buddy are free\n");
			err = -EINVAL;
		}
	}

	return err;
}

static int check_blocks(struct kunit *test, struct drm_buddy *mm,
			struct list_head *blocks, u64 expected_size, bool is_contiguous)
{
	struct drm_buddy_block *block;
	struct drm_buddy_block *prev;
	u64 total;
	int err = 0;

	block = NULL;
	prev = NULL;
	total = 0;

	list_for_each_entry(block, blocks, link) {
		err = check_block(test, mm, block);

		if (!drm_buddy_block_is_allocated(block)) {
			kunit_err(test, "block not allocated\n");
			err = -EINVAL;
		}

		if (is_contiguous && prev) {
			u64 prev_block_size;
			u64 prev_offset;
			u64 offset;

			prev_offset = drm_buddy_block_offset(prev);
			prev_block_size = drm_buddy_block_size(mm, prev);
			offset = drm_buddy_block_offset(block);

			if (offset != (prev_offset + prev_block_size)) {
				kunit_err(test, "block offset mismatch\n");
				err = -EINVAL;
			}
		}

		if (err)
			break;

		total += drm_buddy_block_size(mm, block);
		prev = block;
	}

	if (!err) {
		if (total != expected_size) {
			kunit_err(test, "size mismatch, expected=%llx, found=%llx\n",
				  expected_size, total);
			err = -EINVAL;
		}
		return err;
	}

	if (prev) {
		kunit_err(test, "prev block, dump:\n");
		dump_block(test, mm, prev);
	}

	kunit_err(test, "bad block, dump:\n");
	dump_block(test, mm, block);

	return err;
}

static int check_mm(struct kunit *test, struct drm_buddy *mm)
{
	struct drm_buddy_block *root;
	struct drm_buddy_block *prev;
	unsigned int i;
	u64 total;
	int err = 0;

	if (!mm->n_roots) {
		kunit_err(test, "n_roots is zero\n");
		return -EINVAL;
	}

	if (mm->n_roots != hweight64(mm->size)) {
		kunit_err(test, "n_roots mismatch, n_roots=%u, expected=%lu\n",
			  mm->n_roots, hweight64(mm->size));
		return -EINVAL;
	}

	root = NULL;
	prev = NULL;
	total = 0;

	for (i = 0; i < mm->n_roots; ++i) {
		struct drm_buddy_block *block;
		unsigned int order;

		root = mm->roots[i];
		if (!root) {
			kunit_err(test, "root(%u) is NULL\n", i);
			err = -EINVAL;
			break;
		}

		err = check_block(test, mm, root);

		if (!drm_buddy_block_is_free(root)) {
			kunit_err(test, "root not free\n");
			err = -EINVAL;
		}

		order = drm_buddy_block_order(root);

		if (!i) {
			if (order != mm->max_order) {
				kunit_err(test, "max order root missing\n");
				err = -EINVAL;
			}
		}

		if (prev) {
			u64 prev_block_size;
			u64 prev_offset;
			u64 offset;

			prev_offset = drm_buddy_block_offset(prev);
			prev_block_size = drm_buddy_block_size(mm, prev);
			offset = drm_buddy_block_offset(root);

			if (offset != (prev_offset + prev_block_size)) {
				kunit_err(test, "root offset mismatch\n");
				err = -EINVAL;
			}
		}

		block = list_first_entry_or_null(&mm->free_list[order],
						 struct drm_buddy_block, link);
		if (block != root) {
			kunit_err(test, "root mismatch at order=%u\n", order);
			err = -EINVAL;
		}

		if (err)
			break;

		prev = root;
		total += drm_buddy_block_size(mm, root);
	}

	if (!err) {
		if (total != mm->size) {
			kunit_err(test, "expected mm size=%llx, found=%llx\n",
				  mm->size, total);
			err = -EINVAL;
		}
		return err;
	}

	if (prev) {
		kunit_err(test, "prev root(%u), dump:\n", i - 1);
		dump_block(test, mm, prev);
	}

	if (root) {
		kunit_err(test, "bad root(%u), dump:\n", i);
		dump_block(test, mm, root);
	}

	return err;
}

static void mm_config(u64 *size, u64 *chunk_size)
{
	DRM_RND_STATE(prng, random_seed);
	u32 s, ms;

	/* Nothing fancy, just try to get an interesting bit pattern */

	prandom_seed_state(&prng, random_seed);

	/* Let size be a random number of pages up to 8 GB (2M pages) */
	s = 1 + drm_prandom_u32_max_state((BIT(33 - 12)) - 1, &prng);
	/* Let the chunk size be a random power of 2 less than size */
	ms = BIT(drm_prandom_u32_max_state(ilog2(s), &prng));
	/* Round size down to the chunk size */
	s &= -ms;

	/* Convert from pages to bytes */
	*chunk_size = (u64)ms << 12;
	*size = (u64)s << 12;
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

	drm_buddy_free_list(&mm, &holes);

	/* Nothing larger than blocks of chunk_size now available */
	for (order = 1; order <= max_order; order++) {
		size = get_size(order, PAGE_SIZE);
		KUNIT_ASSERT_TRUE_MSG(test, drm_buddy_alloc_blocks(&mm, start, mm_size,
								   size, size, &tmp, flags),
							  "buddy_alloc unexpectedly succeeded at order %d, it should be full!",
							  order);
	}

	list_splice_tail(&holes, &blocks);
	drm_buddy_free_list(&mm, &blocks);
	drm_buddy_fini(&mm);
}

static void drm_test_buddy_alloc_smoke(struct kunit *test)
{
	u64 mm_size, chunk_size, start = 0;
	unsigned long flags = 0;
	struct drm_buddy mm;
	int *order;
	int i;

	DRM_RND_STATE(prng, random_seed);
	TIMEOUT(end_time);

	mm_config(&mm_size, &chunk_size);

	KUNIT_ASSERT_FALSE_MSG(test, drm_buddy_init(&mm, mm_size, chunk_size),
			       "buddy_init failed\n");

	order = drm_random_order(mm.max_order + 1, &prng);
	KUNIT_ASSERT_TRUE(test, order);

	for (i = 0; i <= mm.max_order; ++i) {
		struct drm_buddy_block *block;
		int max_order = order[i];
		bool timeout = false;
		LIST_HEAD(blocks);
		u64 total, size;
		LIST_HEAD(tmp);
		int order, err;

		KUNIT_ASSERT_FALSE_MSG(test, check_mm(test, &mm),
				       "pre-mm check failed, abort\n");

		order = max_order;
		total = 0;

		do {
retry:
			size = get_size(order, chunk_size);
			err = drm_buddy_alloc_blocks(&mm, start, mm_size, size, size, &tmp, flags);
			if (err) {
				if (err == -ENOMEM) {
					KUNIT_FAIL(test, "buddy_alloc hit -ENOMEM with order=%d\n",
						   order);
				} else {
					if (order--) {
						err = 0;
						goto retry;
					}

					KUNIT_FAIL(test, "buddy_alloc with order=%d failed\n",
						   order);
				}

				break;
			}

			block = list_first_entry_or_null(&tmp, struct drm_buddy_block, link);
			KUNIT_ASSERT_TRUE_MSG(test, block, "alloc_blocks has no blocks\n");

			list_move_tail(&block->link, &blocks);
			KUNIT_EXPECT_EQ_MSG(test, drm_buddy_block_order(block), order,
					    "buddy_alloc order mismatch\n");

			total += drm_buddy_block_size(&mm, block);

			if (__timeout(end_time, NULL)) {
				timeout = true;
				break;
			}
		} while (total < mm.size);

		if (!err)
			err = check_blocks(test, &mm, &blocks, total, false);

		drm_buddy_free_list(&mm, &blocks);

		if (!err) {
			KUNIT_EXPECT_FALSE_MSG(test, check_mm(test, &mm),
					       "post-mm check failed\n");
		}

		if (err || timeout)
			break;

		cond_resched();
	}

	kfree(order);
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
	drm_buddy_free_list(&mm, &blocks);
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

	drm_buddy_free_list(&mm, &blocks);
	drm_buddy_fini(&mm);
}

static void drm_test_buddy_alloc_range(struct kunit *test)
{
	unsigned long flags = DRM_BUDDY_RANGE_ALLOCATION;
	u64 offset, size, rem, chunk_size, end;
	unsigned long page_num;
	struct drm_buddy mm;
	LIST_HEAD(blocks);

	mm_config(&size, &chunk_size);

	KUNIT_ASSERT_FALSE_MSG(test, drm_buddy_init(&mm, size, chunk_size),
			       "buddy_init failed");

	KUNIT_ASSERT_FALSE_MSG(test, check_mm(test, &mm),
			       "pre-mm check failed, abort!");

	rem = mm.size;
	offset = 0;

	for_each_prime_number_from(page_num, 1, ULONG_MAX - 1) {
		struct drm_buddy_block *block;
		LIST_HEAD(tmp);

		size = min(page_num * mm.chunk_size, rem);
		end = offset + size;

		KUNIT_ASSERT_FALSE_MSG(test, drm_buddy_alloc_blocks(&mm, offset, end,
								    size, mm.chunk_size,
									&tmp, flags),
				"alloc_range with offset=%llx, size=%llx failed\n", offset, size);

		block = list_first_entry_or_null(&tmp, struct drm_buddy_block, link);
		KUNIT_ASSERT_TRUE_MSG(test, block, "alloc_range has no blocks\n");

		KUNIT_ASSERT_EQ_MSG(test, drm_buddy_block_offset(block), offset,
				    "alloc_range start offset mismatch, found=%llx, expected=%llx\n",
							drm_buddy_block_offset(block), offset);

		KUNIT_ASSERT_FALSE(test, check_blocks(test, &mm, &tmp, size, true));

		list_splice_tail(&tmp, &blocks);

		offset += size;

		rem -= size;
		if (!rem)
			break;

		cond_resched();
	}

	drm_buddy_free_list(&mm, &blocks);

	KUNIT_EXPECT_FALSE_MSG(test, check_mm(test, &mm), "post-mm check failed\n");

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

	drm_buddy_free_list(&mm, &allocated);
	drm_buddy_fini(&mm);
}

static int drm_buddy_init_test(struct kunit *test)
{
	while (!random_seed)
		random_seed = get_random_u32();

	return 0;
}

static struct kunit_case drm_buddy_tests[] = {
	KUNIT_CASE(drm_test_buddy_alloc_limit),
	KUNIT_CASE(drm_test_buddy_alloc_range),
	KUNIT_CASE(drm_test_buddy_alloc_optimistic),
	KUNIT_CASE(drm_test_buddy_alloc_pessimistic),
	KUNIT_CASE(drm_test_buddy_alloc_smoke),
	KUNIT_CASE(drm_test_buddy_alloc_pathological),
	{}
};

static struct kunit_suite drm_buddy_test_suite = {
	.name = "drm_buddy",
	.init = drm_buddy_init_test,
	.test_cases = drm_buddy_tests,
};

kunit_test_suite(drm_buddy_test_suite);

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL");
