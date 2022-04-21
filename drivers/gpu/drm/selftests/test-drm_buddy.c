// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#define pr_fmt(fmt) "drm_buddy: " fmt

#include <linux/module.h>
#include <linux/prime_numbers.h>
#include <linux/sched/signal.h>

#include <drm/drm_buddy.h>

#include "../lib/drm_random.h"

#define TESTS "drm_buddy_selftests.h"
#include "drm_selftest.h"

#define IGT_TIMEOUT(name__) \
	unsigned long name__ = jiffies + MAX_SCHEDULE_TIMEOUT

static unsigned int random_seed;

static inline u64 get_size(int order, u64 chunk_size)
{
	return (1 << order) * chunk_size;
}

__printf(2, 3)
static bool __igt_timeout(unsigned long timeout, const char *fmt, ...)
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

static inline const char *yesno(bool v)
{
	return v ? "yes" : "no";
}

static void __igt_dump_block(struct drm_buddy *mm,
			     struct drm_buddy_block *block,
			     bool buddy)
{
	pr_err("block info: header=%llx, state=%u, order=%d, offset=%llx size=%llx root=%s buddy=%s\n",
	       block->header,
	       drm_buddy_block_state(block),
	       drm_buddy_block_order(block),
	       drm_buddy_block_offset(block),
	       drm_buddy_block_size(mm, block),
	       yesno(!block->parent),
	       yesno(buddy));
}

static void igt_dump_block(struct drm_buddy *mm,
			   struct drm_buddy_block *block)
{
	struct drm_buddy_block *buddy;

	__igt_dump_block(mm, block, false);

	buddy = drm_get_buddy(block);
	if (buddy)
		__igt_dump_block(mm, buddy, true);
}

static int igt_check_block(struct drm_buddy *mm,
			   struct drm_buddy_block *block)
{
	struct drm_buddy_block *buddy;
	unsigned int block_state;
	u64 block_size;
	u64 offset;
	int err = 0;

	block_state = drm_buddy_block_state(block);

	if (block_state != DRM_BUDDY_ALLOCATED &&
	    block_state != DRM_BUDDY_FREE &&
	    block_state != DRM_BUDDY_SPLIT) {
		pr_err("block state mismatch\n");
		err = -EINVAL;
	}

	block_size = drm_buddy_block_size(mm, block);
	offset = drm_buddy_block_offset(block);

	if (block_size < mm->chunk_size) {
		pr_err("block size smaller than min size\n");
		err = -EINVAL;
	}

	if (!is_power_of_2(block_size)) {
		pr_err("block size not power of two\n");
		err = -EINVAL;
	}

	if (!IS_ALIGNED(block_size, mm->chunk_size)) {
		pr_err("block size not aligned to min size\n");
		err = -EINVAL;
	}

	if (!IS_ALIGNED(offset, mm->chunk_size)) {
		pr_err("block offset not aligned to min size\n");
		err = -EINVAL;
	}

	if (!IS_ALIGNED(offset, block_size)) {
		pr_err("block offset not aligned to block size\n");
		err = -EINVAL;
	}

	buddy = drm_get_buddy(block);

	if (!buddy && block->parent) {
		pr_err("buddy has gone fishing\n");
		err = -EINVAL;
	}

	if (buddy) {
		if (drm_buddy_block_offset(buddy) != (offset ^ block_size)) {
			pr_err("buddy has wrong offset\n");
			err = -EINVAL;
		}

		if (drm_buddy_block_size(mm, buddy) != block_size) {
			pr_err("buddy size mismatch\n");
			err = -EINVAL;
		}

		if (drm_buddy_block_state(buddy) == block_state &&
		    block_state == DRM_BUDDY_FREE) {
			pr_err("block and its buddy are free\n");
			err = -EINVAL;
		}
	}

	return err;
}

static int igt_check_blocks(struct drm_buddy *mm,
			    struct list_head *blocks,
			    u64 expected_size,
			    bool is_contiguous)
{
	struct drm_buddy_block *block;
	struct drm_buddy_block *prev;
	u64 total;
	int err = 0;

	block = NULL;
	prev = NULL;
	total = 0;

	list_for_each_entry(block, blocks, link) {
		err = igt_check_block(mm, block);

		if (!drm_buddy_block_is_allocated(block)) {
			pr_err("block not allocated\n"),
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
				pr_err("block offset mismatch\n");
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
			pr_err("size mismatch, expected=%llx, found=%llx\n",
			       expected_size, total);
			err = -EINVAL;
		}
		return err;
	}

	if (prev) {
		pr_err("prev block, dump:\n");
		igt_dump_block(mm, prev);
	}

	pr_err("bad block, dump:\n");
	igt_dump_block(mm, block);

	return err;
}

static int igt_check_mm(struct drm_buddy *mm)
{
	struct drm_buddy_block *root;
	struct drm_buddy_block *prev;
	unsigned int i;
	u64 total;
	int err = 0;

	if (!mm->n_roots) {
		pr_err("n_roots is zero\n");
		return -EINVAL;
	}

	if (mm->n_roots != hweight64(mm->size)) {
		pr_err("n_roots mismatch, n_roots=%u, expected=%lu\n",
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
			pr_err("root(%u) is NULL\n", i);
			err = -EINVAL;
			break;
		}

		err = igt_check_block(mm, root);

		if (!drm_buddy_block_is_free(root)) {
			pr_err("root not free\n");
			err = -EINVAL;
		}

		order = drm_buddy_block_order(root);

		if (!i) {
			if (order != mm->max_order) {
				pr_err("max order root missing\n");
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
				pr_err("root offset mismatch\n");
				err = -EINVAL;
			}
		}

		block = list_first_entry_or_null(&mm->free_list[order],
						 struct drm_buddy_block,
						 link);
		if (block != root) {
			pr_err("root mismatch at order=%u\n", order);
			err = -EINVAL;
		}

		if (err)
			break;

		prev = root;
		total += drm_buddy_block_size(mm, root);
	}

	if (!err) {
		if (total != mm->size) {
			pr_err("expected mm size=%llx, found=%llx\n", mm->size,
			       total);
			err = -EINVAL;
		}
		return err;
	}

	if (prev) {
		pr_err("prev root(%u), dump:\n", i - 1);
		igt_dump_block(mm, prev);
	}

	if (root) {
		pr_err("bad root(%u), dump:\n", i);
		igt_dump_block(mm, root);
	}

	return err;
}

static void igt_mm_config(u64 *size, u64 *chunk_size)
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

static int igt_buddy_alloc_pathological(void *arg)
{
	u64 mm_size, size, min_page_size, start = 0;
	struct drm_buddy_block *block;
	const int max_order = 3;
	unsigned long flags = 0;
	int order, top, err;
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
	err = drm_buddy_init(&mm, mm_size, PAGE_SIZE);
	if (err) {
		pr_err("buddy_init failed(%d)\n", err);
		return err;
	}
	BUG_ON(mm.max_order != max_order);

	for (top = max_order; top; top--) {
		/* Make room by freeing the largest allocated block */
		block = list_first_entry_or_null(&blocks, typeof(*block), link);
		if (block) {
			list_del(&block->link);
			drm_buddy_free_block(&mm, block);
		}

		for (order = top; order--; ) {
			size = min_page_size = get_size(order, PAGE_SIZE);
			err = drm_buddy_alloc_blocks(&mm, start, mm_size, size,
						     min_page_size, &tmp, flags);
			if (err) {
				pr_info("buddy_alloc hit -ENOMEM with order=%d, top=%d\n",
					order, top);
				goto err;
			}

			block = list_first_entry_or_null(&tmp,
							 struct drm_buddy_block,
							 link);
			if (!block) {
				pr_err("alloc_blocks has no blocks\n");
				err = -EINVAL;
				goto err;
			}

			list_move_tail(&block->link, &blocks);
		}

		/* There should be one final page for this sub-allocation */
		size = min_page_size = get_size(0, PAGE_SIZE);
		err = drm_buddy_alloc_blocks(&mm, start, mm_size, size, min_page_size, &tmp, flags);
		if (err) {
			pr_info("buddy_alloc hit -ENOMEM for hole\n");
			goto err;
		}

		block = list_first_entry_or_null(&tmp,
						 struct drm_buddy_block,
						 link);
		if (!block) {
			pr_err("alloc_blocks has no blocks\n");
			err = -EINVAL;
			goto err;
		}

		list_move_tail(&block->link, &holes);

		size = min_page_size = get_size(top, PAGE_SIZE);
		err = drm_buddy_alloc_blocks(&mm, start, mm_size, size, min_page_size, &tmp, flags);
		if (!err) {
			pr_info("buddy_alloc unexpectedly succeeded at top-order %d/%d, it should be full!",
				top, max_order);
			block = list_first_entry_or_null(&tmp,
							 struct drm_buddy_block,
							 link);
			if (!block) {
				pr_err("alloc_blocks has no blocks\n");
				err = -EINVAL;
				goto err;
			}

			list_move_tail(&block->link, &blocks);
			err = -EINVAL;
			goto err;
		}
	}

	drm_buddy_free_list(&mm, &holes);

	/* Nothing larger than blocks of chunk_size now available */
	for (order = 1; order <= max_order; order++) {
		size = min_page_size = get_size(order, PAGE_SIZE);
		err = drm_buddy_alloc_blocks(&mm, start, mm_size, size, min_page_size, &tmp, flags);
		if (!err) {
			pr_info("buddy_alloc unexpectedly succeeded at order %d, it should be full!",
				order);
			block = list_first_entry_or_null(&tmp,
							 struct drm_buddy_block,
							 link);
			if (!block) {
				pr_err("alloc_blocks has no blocks\n");
				err = -EINVAL;
				goto err;
			}

			list_move_tail(&block->link, &blocks);
			err = -EINVAL;
			goto err;
		}
	}

	if (err)
		err = 0;

err:
	list_splice_tail(&holes, &blocks);
	drm_buddy_free_list(&mm, &blocks);
	drm_buddy_fini(&mm);
	return err;
}

static int igt_buddy_alloc_smoke(void *arg)
{
	u64 mm_size, min_page_size, chunk_size, start = 0;
	unsigned long flags = 0;
	struct drm_buddy mm;
	int *order;
	int err, i;

	DRM_RND_STATE(prng, random_seed);
	IGT_TIMEOUT(end_time);

	igt_mm_config(&mm_size, &chunk_size);

	err = drm_buddy_init(&mm, mm_size, chunk_size);
	if (err) {
		pr_err("buddy_init failed(%d)\n", err);
		return err;
	}

	order = drm_random_order(mm.max_order + 1, &prng);
	if (!order)
		goto out_fini;

	for (i = 0; i <= mm.max_order; ++i) {
		struct drm_buddy_block *block;
		int max_order = order[i];
		bool timeout = false;
		LIST_HEAD(blocks);
		u64 total, size;
		LIST_HEAD(tmp);
		int order;

		err = igt_check_mm(&mm);
		if (err) {
			pr_err("pre-mm check failed, abort\n");
			break;
		}

		order = max_order;
		total = 0;

		do {
retry:
			size = min_page_size = get_size(order, chunk_size);
			err = drm_buddy_alloc_blocks(&mm, start, mm_size, size,
						     min_page_size, &tmp, flags);
			if (err) {
				if (err == -ENOMEM) {
					pr_info("buddy_alloc hit -ENOMEM with order=%d\n",
						order);
				} else {
					if (order--) {
						err = 0;
						goto retry;
					}

					pr_err("buddy_alloc with order=%d failed(%d)\n",
					       order, err);
				}

				break;
			}

			block = list_first_entry_or_null(&tmp,
							 struct drm_buddy_block,
							 link);
			if (!block) {
				pr_err("alloc_blocks has no blocks\n");
				err = -EINVAL;
				break;
			}

			list_move_tail(&block->link, &blocks);

			if (drm_buddy_block_order(block) != order) {
				pr_err("buddy_alloc order mismatch\n");
				err = -EINVAL;
				break;
			}

			total += drm_buddy_block_size(&mm, block);

			if (__igt_timeout(end_time, NULL)) {
				timeout = true;
				break;
			}
		} while (total < mm.size);

		if (!err)
			err = igt_check_blocks(&mm, &blocks, total, false);

		drm_buddy_free_list(&mm, &blocks);

		if (!err) {
			err = igt_check_mm(&mm);
			if (err)
				pr_err("post-mm check failed\n");
		}

		if (err || timeout)
			break;

		cond_resched();
	}

	if (err == -ENOMEM)
		err = 0;

	kfree(order);
out_fini:
	drm_buddy_fini(&mm);

	return err;
}

static int igt_buddy_alloc_pessimistic(void *arg)
{
	u64 mm_size, size, min_page_size, start = 0;
	struct drm_buddy_block *block, *bn;
	const unsigned int max_order = 16;
	unsigned long flags = 0;
	struct drm_buddy mm;
	unsigned int order;
	LIST_HEAD(blocks);
	LIST_HEAD(tmp);
	int err;

	/*
	 * Create a pot-sized mm, then allocate one of each possible
	 * order within. This should leave the mm with exactly one
	 * page left.
	 */

	mm_size = PAGE_SIZE << max_order;
	err = drm_buddy_init(&mm, mm_size, PAGE_SIZE);
	if (err) {
		pr_err("buddy_init failed(%d)\n", err);
		return err;
	}
	BUG_ON(mm.max_order != max_order);

	for (order = 0; order < max_order; order++) {
		size = min_page_size = get_size(order, PAGE_SIZE);
		err = drm_buddy_alloc_blocks(&mm, start, mm_size, size, min_page_size, &tmp, flags);
		if (err) {
			pr_info("buddy_alloc hit -ENOMEM with order=%d\n",
				order);
			goto err;
		}

		block = list_first_entry_or_null(&tmp,
						 struct drm_buddy_block,
						 link);
		if (!block) {
			pr_err("alloc_blocks has no blocks\n");
			err = -EINVAL;
			goto err;
		}

		list_move_tail(&block->link, &blocks);
	}

	/* And now the last remaining block available */
	size = min_page_size = get_size(0, PAGE_SIZE);
	err = drm_buddy_alloc_blocks(&mm, start, mm_size, size, min_page_size, &tmp, flags);
	if (err) {
		pr_info("buddy_alloc hit -ENOMEM on final alloc\n");
		goto err;
	}

	block = list_first_entry_or_null(&tmp,
					 struct drm_buddy_block,
					 link);
	if (!block) {
		pr_err("alloc_blocks has no blocks\n");
		err = -EINVAL;
		goto err;
	}

	list_move_tail(&block->link, &blocks);

	/* Should be completely full! */
	for (order = max_order; order--; ) {
		size = min_page_size = get_size(order, PAGE_SIZE);
		err = drm_buddy_alloc_blocks(&mm, start, mm_size, size, min_page_size, &tmp, flags);
		if (!err) {
			pr_info("buddy_alloc unexpectedly succeeded at order %d, it should be full!",
				order);
			block = list_first_entry_or_null(&tmp,
							 struct drm_buddy_block,
							 link);
			if (!block) {
				pr_err("alloc_blocks has no blocks\n");
				err = -EINVAL;
				goto err;
			}

			list_move_tail(&block->link, &blocks);
			err = -EINVAL;
			goto err;
		}
	}

	block = list_last_entry(&blocks, typeof(*block), link);
	list_del(&block->link);
	drm_buddy_free_block(&mm, block);

	/* As we free in increasing size, we make available larger blocks */
	order = 1;
	list_for_each_entry_safe(block, bn, &blocks, link) {
		list_del(&block->link);
		drm_buddy_free_block(&mm, block);

		size = min_page_size = get_size(order, PAGE_SIZE);
		err = drm_buddy_alloc_blocks(&mm, start, mm_size, size, min_page_size, &tmp, flags);
		if (err) {
			pr_info("buddy_alloc (realloc) hit -ENOMEM with order=%d\n",
				order);
			goto err;
		}

		block = list_first_entry_or_null(&tmp,
						 struct drm_buddy_block,
						 link);
		if (!block) {
			pr_err("alloc_blocks has no blocks\n");
			err = -EINVAL;
			goto err;
		}

		list_del(&block->link);
		drm_buddy_free_block(&mm, block);
		order++;
	}

	/* To confirm, now the whole mm should be available */
	size = min_page_size = get_size(max_order, PAGE_SIZE);
	err = drm_buddy_alloc_blocks(&mm, start, mm_size, size, min_page_size, &tmp, flags);
	if (err) {
		pr_info("buddy_alloc (realloc) hit -ENOMEM with order=%d\n",
			max_order);
		goto err;
	}

	block = list_first_entry_or_null(&tmp,
					 struct drm_buddy_block,
					 link);
	if (!block) {
		pr_err("alloc_blocks has no blocks\n");
		err = -EINVAL;
		goto err;
	}

	list_del(&block->link);
	drm_buddy_free_block(&mm, block);

err:
	drm_buddy_free_list(&mm, &blocks);
	drm_buddy_fini(&mm);
	return err;
}

static int igt_buddy_alloc_optimistic(void *arg)
{
	u64 mm_size, size, min_page_size, start = 0;
	struct drm_buddy_block *block;
	unsigned long flags = 0;
	const int max_order = 16;
	struct drm_buddy mm;
	LIST_HEAD(blocks);
	LIST_HEAD(tmp);
	int order, err;

	/*
	 * Create a mm with one block of each order available, and
	 * try to allocate them all.
	 */

	mm_size = PAGE_SIZE * ((1 << (max_order + 1)) - 1);
	err = drm_buddy_init(&mm,
			     mm_size,
			     PAGE_SIZE);
	if (err) {
		pr_err("buddy_init failed(%d)\n", err);
		return err;
	}

	BUG_ON(mm.max_order != max_order);

	for (order = 0; order <= max_order; order++) {
		size = min_page_size = get_size(order, PAGE_SIZE);
		err = drm_buddy_alloc_blocks(&mm, start, mm_size, size, min_page_size, &tmp, flags);
		if (err) {
			pr_info("buddy_alloc hit -ENOMEM with order=%d\n",
				order);
			goto err;
		}

		block = list_first_entry_or_null(&tmp,
						 struct drm_buddy_block,
						 link);
		if (!block) {
			pr_err("alloc_blocks has no blocks\n");
			err = -EINVAL;
			goto err;
		}

		list_move_tail(&block->link, &blocks);
	}

	/* Should be completely full! */
	size = min_page_size = get_size(0, PAGE_SIZE);
	err = drm_buddy_alloc_blocks(&mm, start, mm_size, size, min_page_size, &tmp, flags);
	if (!err) {
		pr_info("buddy_alloc unexpectedly succeeded, it should be full!");
		block = list_first_entry_or_null(&tmp,
						 struct drm_buddy_block,
						 link);
		if (!block) {
			pr_err("alloc_blocks has no blocks\n");
			err = -EINVAL;
			goto err;
		}

		list_move_tail(&block->link, &blocks);
		err = -EINVAL;
		goto err;
	} else {
		err = 0;
	}

err:
	drm_buddy_free_list(&mm, &blocks);
	drm_buddy_fini(&mm);
	return err;
}

static int igt_buddy_alloc_range(void *arg)
{
	unsigned long flags = DRM_BUDDY_RANGE_ALLOCATION;
	u64 offset, size, rem, chunk_size, end;
	unsigned long page_num;
	struct drm_buddy mm;
	LIST_HEAD(blocks);
	int err;

	igt_mm_config(&size, &chunk_size);

	err = drm_buddy_init(&mm, size, chunk_size);
	if (err) {
		pr_err("buddy_init failed(%d)\n", err);
		return err;
	}

	err = igt_check_mm(&mm);
	if (err) {
		pr_err("pre-mm check failed, abort, abort, abort!\n");
		goto err_fini;
	}

	rem = mm.size;
	offset = 0;

	for_each_prime_number_from(page_num, 1, ULONG_MAX - 1) {
		struct drm_buddy_block *block;
		LIST_HEAD(tmp);

		size = min(page_num * mm.chunk_size, rem);
		end = offset + size;

		err = drm_buddy_alloc_blocks(&mm, offset, end, size, mm.chunk_size, &tmp, flags);
		if (err) {
			if (err == -ENOMEM) {
				pr_info("alloc_range hit -ENOMEM with size=%llx\n",
					size);
			} else {
				pr_err("alloc_range with offset=%llx, size=%llx failed(%d)\n",
				       offset, size, err);
			}

			break;
		}

		block = list_first_entry_or_null(&tmp,
						 struct drm_buddy_block,
						 link);
		if (!block) {
			pr_err("alloc_range has no blocks\n");
			err = -EINVAL;
			break;
		}

		if (drm_buddy_block_offset(block) != offset) {
			pr_err("alloc_range start offset mismatch, found=%llx, expected=%llx\n",
			       drm_buddy_block_offset(block), offset);
			err = -EINVAL;
		}

		if (!err)
			err = igt_check_blocks(&mm, &tmp, size, true);

		list_splice_tail(&tmp, &blocks);

		if (err)
			break;

		offset += size;

		rem -= size;
		if (!rem)
			break;

		cond_resched();
	}

	if (err == -ENOMEM)
		err = 0;

	drm_buddy_free_list(&mm, &blocks);

	if (!err) {
		err = igt_check_mm(&mm);
		if (err)
			pr_err("post-mm check failed\n");
	}

err_fini:
	drm_buddy_fini(&mm);

	return err;
}

static int igt_buddy_alloc_limit(void *arg)
{
	u64 end, size = U64_MAX, start = 0;
	struct drm_buddy_block *block;
	unsigned long flags = 0;
	LIST_HEAD(allocated);
	struct drm_buddy mm;
	int err;

	size = end = round_down(size, 4096);
	err = drm_buddy_init(&mm, size, PAGE_SIZE);
	if (err)
		return err;

	if (mm.max_order != DRM_BUDDY_MAX_ORDER) {
		pr_err("mm.max_order(%d) != %d\n",
		       mm.max_order, DRM_BUDDY_MAX_ORDER);
		err = -EINVAL;
		goto out_fini;
	}

	err = drm_buddy_alloc_blocks(&mm, start, end, size,
				     PAGE_SIZE, &allocated, flags);

	if (unlikely(err))
		goto out_free;

	block = list_first_entry_or_null(&allocated,
					 struct drm_buddy_block,
					 link);

	if (!block) {
		err = -EINVAL;
		goto out_fini;
	}

	if (drm_buddy_block_order(block) != mm.max_order) {
		pr_err("block order(%d) != %d\n",
		       drm_buddy_block_order(block), mm.max_order);
		err = -EINVAL;
		goto out_free;
	}

	if (drm_buddy_block_size(&mm, block) !=
	    BIT_ULL(mm.max_order) * PAGE_SIZE) {
		pr_err("block size(%llu) != %llu\n",
		       drm_buddy_block_size(&mm, block),
		       BIT_ULL(mm.max_order) * PAGE_SIZE);
		err = -EINVAL;
		goto out_free;
	}

out_free:
	drm_buddy_free_list(&mm, &allocated);
out_fini:
	drm_buddy_fini(&mm);
	return err;
}

static int igt_sanitycheck(void *ignored)
{
	pr_info("%s - ok!\n", __func__);
	return 0;
}

#include "drm_selftest.c"

static int __init test_drm_buddy_init(void)
{
	int err;

	while (!random_seed)
		random_seed = get_random_int();

	pr_info("Testing DRM buddy manager (struct drm_buddy), with random_seed=0x%x\n",
		random_seed);
	err = run_selftests(selftests, ARRAY_SIZE(selftests), NULL);

	return err > 0 ? 0 : err;
}

static void __exit test_drm_buddy_exit(void)
{
}

module_init(test_drm_buddy_init);
module_exit(test_drm_buddy_exit);

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL");
